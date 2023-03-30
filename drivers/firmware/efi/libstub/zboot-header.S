/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/pe.h>

#ifdef CONFIG_64BIT
	.set		.Lextra_characteristics, 0x0
	.set		.Lpe_opt_magic, PE_OPT_MAGIC_PE32PLUS
#else
	.set		.Lextra_characteristics, IMAGE_FILE_32BIT_MACHINE
	.set		.Lpe_opt_magic, PE_OPT_MAGIC_PE32
#endif

	.section	".head", "a"
	.globl		__efistub_efi_zboot_header
__efistub_efi_zboot_header:
.Ldoshdr:
	.long		MZ_MAGIC
	.ascii		"zimg"					// image type
	.long		__efistub__gzdata_start - .Ldoshdr	// payload offset
	.long		__efistub__gzdata_size - 12		// payload size
	.long		0, 0					// reserved
	.asciz		COMP_TYPE				// compression type
	.org		.Ldoshdr + 0x38
	.long		LINUX_PE_MAGIC
	.long		.Lpehdr - .Ldoshdr			// PE header offset

.Lpehdr:
	.long		PE_MAGIC
	.short		MACHINE_TYPE
	.short		.Lsection_count
	.long		0
	.long		0
	.long		0
	.short		.Lsection_table - .Loptional_header
	.short		IMAGE_FILE_DEBUG_STRIPPED | \
			IMAGE_FILE_EXECUTABLE_IMAGE | \
			IMAGE_FILE_LINE_NUMS_STRIPPED |\
			.Lextra_characteristics

.Loptional_header:
	.short		.Lpe_opt_magic
	.byte		0, 0
	.long		_etext - .Lefi_header_end
	.long		__data_size
	.long		0
	.long		__efistub_efi_zboot_entry - .Ldoshdr
	.long		.Lefi_header_end - .Ldoshdr

#ifdef CONFIG_64BIT
	.quad		0
#else
	.long		_etext - .Ldoshdr, 0x0
#endif
	.long		4096
	.long		512
	.short		0, 0
	.short		LINUX_EFISTUB_MAJOR_VERSION	// MajorImageVersion
	.short		LINUX_EFISTUB_MINOR_VERSION	// MinorImageVersion
	.short		0, 0
	.long		0
	.long		_end - .Ldoshdr

	.long		.Lefi_header_end - .Ldoshdr
	.long		0
	.short		IMAGE_SUBSYSTEM_EFI_APPLICATION
	.short		IMAGE_DLL_CHARACTERISTICS_NX_COMPAT
#ifdef CONFIG_64BIT
	.quad		0, 0, 0, 0
#else
	.long		0, 0, 0, 0
#endif
	.long		0
	.long		(.Lsection_table - .) / 8

	.quad		0				// ExportTable
	.quad		0				// ImportTable
	.quad		0				// ResourceTable
	.quad		0				// ExceptionTable
	.quad		0				// CertificationTable
	.quad		0				// BaseRelocationTable
#ifdef CONFIG_DEBUG_EFI
	.long		.Lefi_debug_table - .Ldoshdr	// DebugTable
	.long		.Lefi_debug_table_size
#endif

.Lsection_table:
	.ascii		".text\0\0\0"
	.long		_etext - .Lefi_header_end
	.long		.Lefi_header_end - .Ldoshdr
	.long		_etext - .Lefi_header_end
	.long		.Lefi_header_end - .Ldoshdr

	.long		0, 0
	.short		0, 0
	.long		IMAGE_SCN_CNT_CODE | \
			IMAGE_SCN_MEM_READ | \
			IMAGE_SCN_MEM_EXECUTE

	.ascii		".data\0\0\0"
	.long		__data_size
	.long		_etext - .Ldoshdr
	.long		__data_rawsize
	.long		_etext - .Ldoshdr

	.long		0, 0
	.short		0, 0
	.long		IMAGE_SCN_CNT_INITIALIZED_DATA | \
			IMAGE_SCN_MEM_READ | \
			IMAGE_SCN_MEM_WRITE

	.set		.Lsection_count, (. - .Lsection_table) / 40

#ifdef CONFIG_DEBUG_EFI
	.section	".rodata", "a"
	.align		2
.Lefi_debug_table:
	// EFI_IMAGE_DEBUG_DIRECTORY_ENTRY
	.long		0				// Characteristics
	.long		0				// TimeDateStamp
	.short		0				// MajorVersion
	.short		0				// MinorVersion
	.long		IMAGE_DEBUG_TYPE_CODEVIEW	// Type
	.long		.Lefi_debug_entry_size		// SizeOfData
	.long		0				// RVA
	.long		.Lefi_debug_entry - .Ldoshdr	// FileOffset

	.set		.Lefi_debug_table_size, . - .Lefi_debug_table
	.previous

.Lefi_debug_entry:
	// EFI_IMAGE_DEBUG_CODEVIEW_NB10_ENTRY
	.ascii		"NB10"				// Signature
	.long		0				// Unknown
	.long		0				// Unknown2
	.long		0				// Unknown3

	.asciz		ZBOOT_EFI_PATH

	.set		.Lefi_debug_entry_size, . - .Lefi_debug_entry
#endif

	.p2align	12
.Lefi_header_end:

