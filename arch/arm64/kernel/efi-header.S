/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2017 Linaro, Ltd.
 * Copyright (C) 2013, 2014 Red Hat, Inc.
 */

#include <linux/pe.h>
#include <linux/sizes.h>

	.macro	efi_signature_nop
#ifdef CONFIG_EFI
.L_head:
	/*
	 * This ccmp instruction has no meaningful effect except that
	 * its opcode forms the magic "MZ" signature required by UEFI.
	 */
	ccmp	x18, #0, #0xd, pl
#else
	/*
	 * Bootloaders may inspect the opcode at the start of the kernel
	 * image to decide if the kernel is capable of booting via UEFI.
	 * So put an ordinary NOP here, not the "MZ.." pseudo-nop above.
	 */
	nop
#endif
	.endm

	.macro	__EFI_PE_HEADER
#ifdef CONFIG_EFI
	.set	.Lpe_header_offset, . - .L_head
	.long	PE_MAGIC
	.short	IMAGE_FILE_MACHINE_ARM64		// Machine
	.short	.Lsection_count				// NumberOfSections
	.long	0 					// TimeDateStamp
	.long	0					// PointerToSymbolTable
	.long	0					// NumberOfSymbols
	.short	.Lsection_table - .Loptional_header	// SizeOfOptionalHeader
	.short	IMAGE_FILE_DEBUG_STRIPPED | \
		IMAGE_FILE_EXECUTABLE_IMAGE | \
		IMAGE_FILE_LINE_NUMS_STRIPPED		// Characteristics

.Loptional_header:
	.short	PE_OPT_MAGIC_PE32PLUS			// PE32+ format
	.byte	0x02					// MajorLinkerVersion
	.byte	0x14					// MinorLinkerVersion
	.long	__initdata_begin - .Lefi_header_end	// SizeOfCode
	.long	__pecoff_data_size			// SizeOfInitializedData
	.long	0					// SizeOfUninitializedData
	.long	__efistub_efi_pe_entry - .L_head	// AddressOfEntryPoint
	.long	.Lefi_header_end - .L_head		// BaseOfCode

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

	.long	_end - .L_head				// SizeOfImage

	// Everything before the kernel image is considered part of the header
	.long	.Lefi_header_end - .L_head		// SizeOfHeaders
	.long	0					// CheckSum
	.short	IMAGE_SUBSYSTEM_EFI_APPLICATION		// Subsystem
	.short	IMAGE_DLL_CHARACTERISTICS_NX_COMPAT	// DllCharacteristics
	.quad	0					// SizeOfStackReserve
	.quad	0					// SizeOfStackCommit
	.quad	0					// SizeOfHeapReserve
	.quad	0					// SizeOfHeapCommit
	.long	0					// LoaderFlags
	.long	(.Lsection_table - .) / 8		// NumberOfRvaAndSizes

	.quad	0					// ExportTable
	.quad	0					// ImportTable
	.quad	0					// ResourceTable
	.quad	0					// ExceptionTable
	.quad	0					// CertificationTable
	.quad	0					// BaseRelocationTable

#ifdef CONFIG_DEBUG_EFI
	.long	.Lefi_debug_table - .L_head		// DebugTable
	.long	.Lefi_debug_table_size
#endif

	// Section table
.Lsection_table:
	.ascii	".text\0\0\0"
	.long	__initdata_begin - .Lefi_header_end	// VirtualSize
	.long	.Lefi_header_end - .L_head		// VirtualAddress
	.long	__initdata_begin - .Lefi_header_end	// SizeOfRawData
	.long	.Lefi_header_end - .L_head		// PointerToRawData

	.long	0					// PointerToRelocations
	.long	0					// PointerToLineNumbers
	.short	0					// NumberOfRelocations
	.short	0					// NumberOfLineNumbers
	.long	IMAGE_SCN_CNT_CODE | \
		IMAGE_SCN_MEM_READ | \
		IMAGE_SCN_MEM_EXECUTE			// Characteristics

	.ascii	".data\0\0\0"
	.long	__pecoff_data_size			// VirtualSize
	.long	__initdata_begin - .L_head		// VirtualAddress
	.long	__pecoff_data_rawsize			// SizeOfRawData
	.long	__initdata_begin - .L_head		// PointerToRawData

	.long	0					// PointerToRelocations
	.long	0					// PointerToLineNumbers
	.short	0					// NumberOfRelocations
	.short	0					// NumberOfLineNumbers
	.long	IMAGE_SCN_CNT_INITIALIZED_DATA | \
		IMAGE_SCN_MEM_READ | \
		IMAGE_SCN_MEM_WRITE			// Characteristics

	.set	.Lsection_count, (. - .Lsection_table) / 40

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
.Lefi_debug_table:
	// EFI_IMAGE_DEBUG_DIRECTORY_ENTRY
	.long	0					// Characteristics
	.long	0					// TimeDateStamp
	.short	0					// MajorVersion
	.short	0					// MinorVersion
	.long	IMAGE_DEBUG_TYPE_CODEVIEW		// Type
	.long	.Lefi_debug_entry_size			// SizeOfData
	.long	0					// RVA
	.long	.Lefi_debug_entry - .L_head		// FileOffset

	.set	.Lefi_debug_table_size, . - .Lefi_debug_table
	.previous

.Lefi_debug_entry:
	// EFI_IMAGE_DEBUG_CODEVIEW_NB10_ENTRY
	.ascii	"NB10"					// Signature
	.long	0					// Unknown
	.long	0					// Unknown2
	.long	0					// Unknown3

	.asciz	VMLINUX_PATH

	.set	.Lefi_debug_entry_size, . - .Lefi_debug_entry
#endif

	.balign	SEGMENT_ALIGN
.Lefi_header_end:
#else
	.set	.Lpe_header_offset, 0x0
#endif
	.endm
