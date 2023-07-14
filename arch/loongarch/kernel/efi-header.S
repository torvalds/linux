/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/pe.h>
#include <linux/sizes.h>

	.macro	__EFI_PE_HEADER
	.long	PE_MAGIC
.Lcoff_header:
	.short	IMAGE_FILE_MACHINE_LOONGARCH64		/* Machine */
	.short	.Lsection_count				/* NumberOfSections */
	.long	0 					/* TimeDateStamp */
	.long	0					/* PointerToSymbolTable */
	.long	0					/* NumberOfSymbols */
	.short	.Lsection_table - .Loptional_header	/* SizeOfOptionalHeader */
	.short	IMAGE_FILE_DEBUG_STRIPPED | \
		IMAGE_FILE_EXECUTABLE_IMAGE | \
		IMAGE_FILE_LINE_NUMS_STRIPPED		/* Characteristics */

.Loptional_header:
	.short	PE_OPT_MAGIC_PE32PLUS			/* PE32+ format */
	.byte	0x02					/* MajorLinkerVersion */
	.byte	0x14					/* MinorLinkerVersion */
	.long	__inittext_end - .Lefi_header_end	/* SizeOfCode */
	.long	_kernel_vsize				/* SizeOfInitializedData */
	.long	0					/* SizeOfUninitializedData */
	.long	__efistub_efi_pe_entry - _head		/* AddressOfEntryPoint */
	.long	.Lefi_header_end - _head		/* BaseOfCode */

.Lextra_header_fields:
	.quad	0					/* ImageBase */
	.long	PECOFF_SEGMENT_ALIGN			/* SectionAlignment */
	.long	PECOFF_FILE_ALIGN			/* FileAlignment */
	.short	0					/* MajorOperatingSystemVersion */
	.short	0					/* MinorOperatingSystemVersion */
	.short	LINUX_EFISTUB_MAJOR_VERSION		/* MajorImageVersion */
	.short	LINUX_EFISTUB_MINOR_VERSION		/* MinorImageVersion */
	.short	0					/* MajorSubsystemVersion */
	.short	0					/* MinorSubsystemVersion */
	.long	0					/* Win32VersionValue */

	.long	_end - _head				/* SizeOfImage */

	/* Everything before the kernel image is considered part of the header */
	.long	.Lefi_header_end - _head		/* SizeOfHeaders */
	.long	0					/* CheckSum */
	.short	IMAGE_SUBSYSTEM_EFI_APPLICATION		/* Subsystem */
	.short	0					/* DllCharacteristics */
	.quad	0					/* SizeOfStackReserve */
	.quad	0					/* SizeOfStackCommit */
	.quad	0					/* SizeOfHeapReserve */
	.quad	0					/* SizeOfHeapCommit */
	.long	0					/* LoaderFlags */
	.long	(.Lsection_table - .) / 8		/* NumberOfRvaAndSizes */

	.quad	0					/* ExportTable */
	.quad	0					/* ImportTable */
	.quad	0					/* ResourceTable */
	.quad	0					/* ExceptionTable */
	.quad	0					/* CertificationTable */
	.quad	0					/* BaseRelocationTable */

	/* Section table */
.Lsection_table:
	.ascii	".text\0\0\0"
	.long	__inittext_end - .Lefi_header_end	/* VirtualSize */
	.long	.Lefi_header_end - _head		/* VirtualAddress */
	.long	__inittext_end - .Lefi_header_end	/* SizeOfRawData */
	.long	.Lefi_header_end - _head		/* PointerToRawData */

	.long	0					/* PointerToRelocations */
	.long	0					/* PointerToLineNumbers */
	.short	0					/* NumberOfRelocations */
	.short	0					/* NumberOfLineNumbers */
	.long	IMAGE_SCN_CNT_CODE | \
		IMAGE_SCN_MEM_READ | \
		IMAGE_SCN_MEM_EXECUTE			/* Characteristics */

	.ascii	".data\0\0\0"
	.long	_kernel_vsize				/* VirtualSize */
	.long	__initdata_begin - _head		/* VirtualAddress */
	.long	_kernel_rsize				/* SizeOfRawData */
	.long	__initdata_begin - _head		/* PointerToRawData */

	.long	0					/* PointerToRelocations */
	.long	0					/* PointerToLineNumbers */
	.short	0					/* NumberOfRelocations */
	.short	0					/* NumberOfLineNumbers */
	.long	IMAGE_SCN_CNT_INITIALIZED_DATA | \
		IMAGE_SCN_MEM_READ | \
		IMAGE_SCN_MEM_WRITE			/* Characteristics */

	.set	.Lsection_count, (. - .Lsection_table) / 40

	.balign	0x10000					/* PECOFF_SEGMENT_ALIGN */
.Lefi_header_end:
	.endm
