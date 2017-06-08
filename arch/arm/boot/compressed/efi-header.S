/*
 * Copyright (C) 2013-2015 Linaro Ltd
 * Authors: Roy Franz <roy.franz@linaro.org>
 *          Ard Biesheuvel <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

		.macro	__nop
#ifdef CONFIG_EFI_STUB
		@ This is almost but not quite a NOP, since it does clobber the
		@ condition flags. But it is the best we can do for EFI, since
		@ PE/COFF expects the magic string "MZ" at offset 0, while the
		@ ARM/Linux boot protocol expects an executable instruction
		@ there.
		.inst	'M' | ('Z' << 8) | (0x1310 << 16)   @ tstne r0, #0x4d000
#else
		W(mov)	r0, r0
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
		.long	pe_header - start	@ Offset to the PE header.

pe_header:
		.ascii	"PE\0\0"

coff_header:
		.short	0x01c2			@ ARM or Thumb
		.short	2			@ nr_sections
		.long	0 			@ TimeDateStamp
		.long	0			@ PointerToSymbolTable
		.long	1			@ NumberOfSymbols
		.short	section_table - optional_header
						@ SizeOfOptionalHeader
		.short	0x306			@ Characteristics.
						@ IMAGE_FILE_32BIT_MACHINE |
						@ IMAGE_FILE_DEBUG_STRIPPED |
						@ IMAGE_FILE_EXECUTABLE_IMAGE |
						@ IMAGE_FILE_LINE_NUMS_STRIPPED

optional_header:
		.short	0x10b			@ PE32 format
		.byte	0x02			@ MajorLinkerVersion
		.byte	0x14			@ MinorLinkerVersion
		.long	_end - __efi_start	@ SizeOfCode
		.long	0			@ SizeOfInitializedData
		.long	0			@ SizeOfUninitializedData
		.long	efi_stub_entry - start	@ AddressOfEntryPoint
		.long	start_offset		@ BaseOfCode
		.long	0			@ data

extra_header_fields:
		.long	0			@ ImageBase
		.long	0x200			@ SectionAlignment
		.long	0x200			@ FileAlignment
		.short	0			@ MajorOperatingSystemVersion
		.short	0			@ MinorOperatingSystemVersion
		.short	0			@ MajorImageVersion
		.short	0			@ MinorImageVersion
		.short	0			@ MajorSubsystemVersion
		.short	0			@ MinorSubsystemVersion
		.long	0			@ Win32VersionValue

		.long	_end - start		@ SizeOfImage
		.long	start_offset		@ SizeOfHeaders
		.long	0			@ CheckSum
		.short	0xa			@ Subsystem (EFI application)
		.short	0			@ DllCharacteristics
		.long	0			@ SizeOfStackReserve
		.long	0			@ SizeOfStackCommit
		.long	0			@ SizeOfHeapReserve
		.long	0			@ SizeOfHeapCommit
		.long	0			@ LoaderFlags
		.long	0x6			@ NumberOfRvaAndSizes

		.quad	0			@ ExportTable
		.quad	0			@ ImportTable
		.quad	0			@ ResourceTable
		.quad	0			@ ExceptionTable
		.quad	0			@ CertificationTable
		.quad	0			@ BaseRelocationTable

section_table:
		@
		@ The EFI application loader requires a relocation section
		@ because EFI applications must be relocatable. This is a
		@ dummy section as far as we are concerned.
		@
		.ascii	".reloc\0\0"
		.long	0			@ VirtualSize
		.long	0			@ VirtualAddress
		.long	0			@ SizeOfRawData
		.long	0			@ PointerToRawData
		.long	0			@ PointerToRelocations
		.long	0			@ PointerToLineNumbers
		.short	0			@ NumberOfRelocations
		.short	0			@ NumberOfLineNumbers
		.long	0x42100040		@ Characteristics

		.ascii	".text\0\0\0"
		.long	_end - __efi_start	@ VirtualSize
		.long	__efi_start		@ VirtualAddress
		.long	_edata - __efi_start	@ SizeOfRawData
		.long	__efi_start		@ PointerToRawData
		.long	0			@ PointerToRelocations
		.long	0			@ PointerToLineNumbers
		.short	0			@ NumberOfRelocations
		.short	0			@ NumberOfLineNumbers
		.long	0xe0500020		@ Characteristics

		.align	9
__efi_start:
#endif
		.endm
