/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013-2017 Linaro Ltd
 * Authors: Roy Franz <roy.franz@linaro.org>
 *          Ard Biesheuvel <ard.biesheuvel@linaro.org>
 */

#include <linux/pe.h>
#include <linux/sizes.h>

		.macro	__nop
 AR_CLASS(	mov	r0, r0		)
  M_CLASS(	nop.w			)
		.endm

		.macro __initial_nops
#ifdef CONFIG_EFI_STUB
		@ This is a two-instruction NOP, which happens to bear the
		@ PE/COFF signature "MZ" in the first two bytes, so the kernel
		@ is accepted as an EFI binary. Booting via the UEFI stub
		@ will not execute those instructions, but the ARM/Linux
		@ boot protocol does, so we need some NOPs here.
		.inst	MZ_MAGIC | (0xe225 << 16)	@ eor r5, r5, 0x4d000
		eor	r5, r5, 0x4d000			@ undo previous insn
#else
		__nop
		__nop
#endif
		.endm

		.macro	__EFI_HEADER
#ifdef CONFIG_EFI_STUB
		.set	start_offset, __efi_start - start
		.org	start + 0x3c
		@
		@ The PE header can be anywhere in the file, but for
		@ simplicity we keep it together with the MSDOS header
		@ The offset to the PE/COFF header needs to be at offset
		@ 0x3C in the MSDOS header.
		@ The only 2 fields of the MSDOS header that are used are this
		@ PE/COFF offset, and the "MZ" bytes at offset 0x0.
		@
		.long	pe_header - start		@ Offset to the PE header.

pe_header:
		.long	PE_MAGIC

coff_header:
		.short	IMAGE_FILE_MACHINE_THUMB	@ Machine
		.short	section_count			@ NumberOfSections
		.long	0 				@ TimeDateStamp
		.long	0				@ PointerToSymbolTable
		.long	0				@ NumberOfSymbols
		.short	section_table - optional_header	@ SizeOfOptionalHeader
		.short	IMAGE_FILE_32BIT_MACHINE | \
			IMAGE_FILE_DEBUG_STRIPPED | \
			IMAGE_FILE_EXECUTABLE_IMAGE | \
			IMAGE_FILE_LINE_NUMS_STRIPPED	@ Characteristics

#define __pecoff_code_size (__pecoff_data_start - __efi_start)

optional_header:
		.short	PE_OPT_MAGIC_PE32		@ PE32 format
		.byte	0x02				@ MajorLinkerVersion
		.byte	0x14				@ MinorLinkerVersion
		.long	__pecoff_code_size		@ SizeOfCode
		.long	__pecoff_data_size		@ SizeOfInitializedData
		.long	0				@ SizeOfUninitializedData
		.long	efi_pe_entry - start		@ AddressOfEntryPoint
		.long	start_offset			@ BaseOfCode
		.long	__pecoff_data_start - start	@ BaseOfData

extra_header_fields:
		.long	0				@ ImageBase
		.long	SZ_4K				@ SectionAlignment
		.long	SZ_512				@ FileAlignment
		.short	0				@ MajorOsVersion
		.short	0				@ MinorOsVersion
		.short	LINUX_EFISTUB_MAJOR_VERSION	@ MajorImageVersion
		.short	LINUX_EFISTUB_MINOR_VERSION	@ MinorImageVersion
		.short	0				@ MajorSubsystemVersion
		.short	0				@ MinorSubsystemVersion
		.long	0				@ Win32VersionValue

		.long	__pecoff_end - start		@ SizeOfImage
		.long	start_offset			@ SizeOfHeaders
		.long	0				@ CheckSum
		.short	IMAGE_SUBSYSTEM_EFI_APPLICATION	@ Subsystem
		.short	0				@ DllCharacteristics
		.long	0				@ SizeOfStackReserve
		.long	0				@ SizeOfStackCommit
		.long	0				@ SizeOfHeapReserve
		.long	0				@ SizeOfHeapCommit
		.long	0				@ LoaderFlags
		.long	(section_table - .) / 8		@ NumberOfRvaAndSizes

		.quad	0				@ ExportTable
		.quad	0				@ ImportTable
		.quad	0				@ ResourceTable
		.quad	0				@ ExceptionTable
		.quad	0				@ CertificationTable
		.quad	0				@ BaseRelocationTable

section_table:
		.ascii	".text\0\0\0"
		.long	__pecoff_code_size		@ VirtualSize
		.long	__efi_start			@ VirtualAddress
		.long	__pecoff_code_size		@ SizeOfRawData
		.long	__efi_start			@ PointerToRawData
		.long	0				@ PointerToRelocations
		.long	0				@ PointerToLineNumbers
		.short	0				@ NumberOfRelocations
		.short	0				@ NumberOfLineNumbers
		.long	IMAGE_SCN_CNT_CODE | \
			IMAGE_SCN_MEM_READ | \
			IMAGE_SCN_MEM_EXECUTE		@ Characteristics

		.ascii	".data\0\0\0"
		.long	__pecoff_data_size		@ VirtualSize
		.long	__pecoff_data_start - start	@ VirtualAddress
		.long	__pecoff_data_rawsize		@ SizeOfRawData
		.long	__pecoff_data_start - start	@ PointerToRawData
		.long	0				@ PointerToRelocations
		.long	0				@ PointerToLineNumbers
		.short	0				@ NumberOfRelocations
		.short	0				@ NumberOfLineNumbers
		.long	IMAGE_SCN_CNT_INITIALIZED_DATA | \
			IMAGE_SCN_MEM_READ | \
			IMAGE_SCN_MEM_WRITE		@ Characteristics

		.set	section_count, (. - section_table) / 40

		.align	12
__efi_start:
#endif
		.endm
