/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2017 Linaro, Ltd.
 * Copyright (C) 2013, 2014 Red Hat, Inc.
 */

#include <linux/pe.h>
#include <linux/sizes.h>

	.macro	__EFI_PE_HEADER
	.long	PE_MAGIC
coff_header:
	.short	IMAGE_FILE_MACHINE_ARM64		// Machine
	.short	section_count				// NumberOfSections
	.long	0 					// TimeDateStamp
	.long	0					// PointerToSymbolTable
	.long	0					// NumberOfSymbols
	.short	section_table - optional_header		// SizeOfOptionalHeader
	.short	IMAGE_FILE_DEBUG_STRIPPED | \
		IMAGE_FILE_EXECUTABLE_IMAGE | \
		IMAGE_FILE_LINE_NUMS_STRIPPED		// Characteristics

optional_header:
	.short	PE_OPT_MAGIC_PE32PLUS			// PE32+ format
	.byte	0x02					// MajorLinkerVersion
	.byte	0x14					// MinorLinkerVersion
	.long	__initdata_begin - efi_header_end	// SizeOfCode
	.long	__pecoff_data_size			// SizeOfInitializedData
	.long	0					// SizeOfUninitializedData
	.long	__efistub_efi_pe_entry - _head		// AddressOfEntryPoint
	.long	efi_header_end - _head			// BaseOfCode

extra_header_fields:
	.quad	0					// ImageBase
	.long	SEGMENT_ALIGN				// SectionAlignment
	.long	PECOFF_FILE_ALIGNMENT			// FileAlignment
	.short	0					// MajorOperatingSystemVersion
	.short	0					// MinorOperatingSystemVersion
	.short	LINUX_EFISTUB_MAJOR_VERSION		// MajorImageVersion
	.short	LINUX_EFISTUB_MINOR_VERSION		// MinorImageVersion
	.short	0					// MajorSubsystemVersion
	.short	0					// MinorSubsystemVersion
	.long	0					// Win32VersionValue

	.long	_end - _head				// SizeOfImage

	// Everything before the kernel image is considered part of the header
	.long	efi_header_end - _head			// SizeOfHeaders
	.long	0					// CheckSum
	.short	IMAGE_SUBSYSTEM_EFI_APPLICATION		// Subsystem
	.short	0					// DllCharacteristics
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

#ifdef CONFIG_DEBUG_EFI
	.long	efi_debug_table - _head			// DebugTable
	.long	efi_debug_table_size
#endif

	// Section table
section_table:
	.ascii	".text\0\0\0"
	.long	__initdata_begin - efi_header_end	// VirtualSize
	.long	efi_header_end - _head			// VirtualAddress
	.long	__initdata_begin - efi_header_end	// SizeOfRawData
	.long	efi_header_end - _head			// PointerToRawData

	.long	0					// PointerToRelocations
	.long	0					// PointerToLineNumbers
	.short	0					// NumberOfRelocations
	.short	0					// NumberOfLineNumbers
	.long	IMAGE_SCN_CNT_CODE | \
		IMAGE_SCN_MEM_READ | \
		IMAGE_SCN_MEM_EXECUTE			// Characteristics

	.ascii	".data\0\0\0"
	.long	__pecoff_data_size			// VirtualSize
	.long	__initdata_begin - _head		// VirtualAddress
	.long	__pecoff_data_rawsize			// SizeOfRawData
	.long	__initdata_begin - _head		// PointerToRawData

	.long	0					// PointerToRelocations
	.long	0					// PointerToLineNumbers
	.short	0					// NumberOfRelocations
	.short	0					// NumberOfLineNumbers
	.long	IMAGE_SCN_CNT_INITIALIZED_DATA | \
		IMAGE_SCN_MEM_READ | \
		IMAGE_SCN_MEM_WRITE			// Characteristics

	.set	section_count, (. - section_table) / 40

#ifdef CONFIG_DEBUG_EFI
	/*
	 * The debug table is referenced via its Relative Virtual Address (RVA),
	 * which is only defined for those parts of the image that are covered
	 * by a section declaration. Since this header is not covered by any
	 * section, the debug table must be emitted elsewhere. So stick it in
	 * the .init.rodata section instead.
	 *
	 * Note that the EFI debug entry itself may legally have a zero RVA,
	 * which means we can simply put it right after the section headers.
	 */
	__INITRODATA

	.align	2
efi_debug_table:
	// EFI_IMAGE_DEBUG_DIRECTORY_ENTRY
	.long	0					// Characteristics
	.long	0					// TimeDateStamp
	.short	0					// MajorVersion
	.short	0					// MinorVersion
	.long	IMAGE_DEBUG_TYPE_CODEVIEW		// Type
	.long	efi_debug_entry_size			// SizeOfData
	.long	0					// RVA
	.long	efi_debug_entry - _head			// FileOffset

	.set	efi_debug_table_size, . - efi_debug_table
	.previous

efi_debug_entry:
	// EFI_IMAGE_DEBUG_CODEVIEW_NB10_ENTRY
	.ascii	"NB10"					// Signature
	.long	0					// Unknown
	.long	0					// Unknown2
	.long	0					// Unknown3

	.asciz	VMLINUX_PATH

	.set	efi_debug_entry_size, . - efi_debug_entry
#endif

	/*
	 * EFI will load .text onwards at the 4k section alignment
	 * described in the PE/COFF header. To ensure that instruction
	 * sequences using an adrp and a :lo12: immediate will function
	 * correctly at this alignment, we must ensure that .text is
	 * placed at a 4k boundary in the Image to begin with.
	 */
	.balign	SEGMENT_ALIGN
efi_header_end:
	.endm
