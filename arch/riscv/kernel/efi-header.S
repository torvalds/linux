/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 * Adapted from arch/arm64/kernel/efi-header.S
 */

#include <linux/pe.h>
#include <linux/sizes.h>
#include <asm/set_memory.h>

	.macro	__EFI_PE_HEADER
	.long	PE_MAGIC
coff_header:
#ifdef CONFIG_64BIT
	.short	IMAGE_FILE_MACHINE_RISCV64		// Machine
#else
	.short	IMAGE_FILE_MACHINE_RISCV32		// Machine
#endif
	.short	section_count				// NumberOfSections
	.long	0 					// TimeDateStamp
	.long	0					// PointerToSymbolTable
	.long	0					// NumberOfSymbols
	.short	section_table - optional_header		// SizeOfOptionalHeader
	.short	IMAGE_FILE_DEBUG_STRIPPED | \
		IMAGE_FILE_EXECUTABLE_IMAGE | \
		IMAGE_FILE_LINE_NUMS_STRIPPED		// Characteristics

optional_header:
#ifdef CONFIG_64BIT
	.short	PE_OPT_MAGIC_PE32PLUS			// PE32+ format
#else
	.short	PE_OPT_MAGIC_PE32			// PE32 format
#endif
	.byte	0x02					// MajorLinkerVersion
	.byte	0x14					// MinorLinkerVersion
	.long	__pecoff_text_end - efi_header_end	// SizeOfCode
#ifdef __clang__
	.long   __pecoff_data_virt_size			// SizeOfInitializedData
#else
	.long	__pecoff_data_virt_end - __pecoff_text_end	// SizeOfInitializedData
#endif
	.long	0					// SizeOfUninitializedData
	.long	__efistub_efi_pe_entry - _start		// AddressOfEntryPoint
	.long	efi_header_end - _start			// BaseOfCode
#ifdef CONFIG_32BIT
	.long  __pecoff_text_end - _start		// BaseOfData
#endif

extra_header_fields:
	.quad	0					// ImageBase
	.long	PECOFF_SECTION_ALIGNMENT		// SectionAlignment
	.long	PECOFF_FILE_ALIGNMENT			// FileAlignment
	.short	0					// MajorOperatingSystemVersion
	.short	0					// MinorOperatingSystemVersion
	.short	LINUX_EFISTUB_MAJOR_VERSION		// MajorImageVersion
	.short	LINUX_EFISTUB_MINOR_VERSION		// MinorImageVersion
	.short	0					// MajorSubsystemVersion
	.short	0					// MinorSubsystemVersion
	.long	0					// Win32VersionValue

	.long	_end - _start				// SizeOfImage

	// Everything before the kernel image is considered part of the header
	.long	efi_header_end - _start			// SizeOfHeaders
	.long	0					// CheckSum
	.short	IMAGE_SUBSYSTEM_EFI_APPLICATION		// Subsystem
	.short	IMAGE_DLL_CHARACTERISTICS_NX_COMPAT	// DllCharacteristics
	.quad	0					// SizeOfStackReserve
	.quad	0					// SizeOfStackCommit
	.quad	0					// SizeOfHeapReserve
	.quad	0					// SizeOfHeapCommit
	.long	0					// LoaderFlags
	.long	(section_table - .) / 8			// NumberOfRvaAndSizes

	.quad	0					// ExportTable
	.quad	0					// ImportTable
	.quad	0					// ResourceTable
	.quad	0					// ExceptionTable
	.quad	0					// CertificationTable
	.quad	0					// BaseRelocationTable

	// Section table
section_table:
	.ascii	".text\0\0\0"
	.long	__pecoff_text_end - efi_header_end	// VirtualSize
	.long	efi_header_end - _start			// VirtualAddress
	.long	__pecoff_text_end - efi_header_end	// SizeOfRawData
	.long	efi_header_end - _start			// PointerToRawData

	.long	0					// PointerToRelocations
	.long	0					// PointerToLineNumbers
	.short	0					// NumberOfRelocations
	.short	0					// NumberOfLineNumbers
	.long	IMAGE_SCN_CNT_CODE | \
		IMAGE_SCN_MEM_READ | \
		IMAGE_SCN_MEM_EXECUTE			// Characteristics

	.ascii	".data\0\0\0"
#ifdef __clang__
	.long   __pecoff_data_virt_size			// VirtualSize
#else
	.long	__pecoff_data_virt_end - __pecoff_text_end	// VirtualSize
#endif
	.long	__pecoff_text_end - _start		// VirtualAddress
#ifdef __clang__
	.long   __pecoff_data_raw_size			// SizeOfRawData
#else
	.long	__pecoff_data_raw_end - __pecoff_text_end	// SizeOfRawData
#endif
	.long	__pecoff_text_end - _start		// PointerToRawData

	.long	0					// PointerToRelocations
	.long	0					// PointerToLineNumbers
	.short	0					// NumberOfRelocations
	.short	0					// NumberOfLineNumbers
	.long	IMAGE_SCN_CNT_INITIALIZED_DATA | \
		IMAGE_SCN_MEM_READ | \
		IMAGE_SCN_MEM_WRITE			// Characteristics

	.set	section_count, (. - section_table) / 40

	.balign	0x1000
efi_header_end:
	.endm
