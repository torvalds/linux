/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is the infamous ld script for the 64 bits vdso
 * library
 */
#include <asm/vdso.h>
#include <asm/page.h>
#include <asm-generic/vmlinux.lds.h>

#ifdef __LITTLE_ENDIAN__
OUTPUT_FORMAT("elf64-powerpcle", "elf64-powerpcle", "elf64-powerpcle")
#else
OUTPUT_FORMAT("elf64-powerpc", "elf64-powerpc", "elf64-powerpc")
#endif
OUTPUT_ARCH(powerpc:common64)

SECTIONS
{
	PROVIDE(_vdso_datapage = . - 2 * PAGE_SIZE);
	. = SIZEOF_HEADERS;

	.hash		: { *(.hash) }			:text
	.gnu.hash	: { *(.gnu.hash) }
	.dynsym		: { *(.dynsym) }
	.dynstr		: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }

	.note		: { *(.note.*) }		:text	:note

	. = ALIGN(16);
	.text		: {
		*(.text .stub .text.* .gnu.linkonce.t.* __ftr_alt_*)
		*(.sfpr .glink)
	}						:text
	PROVIDE(__etext = .);
	PROVIDE(_etext = .);
	PROVIDE(etext = .);

	. = ALIGN(8);
	VDSO_ftr_fixup_start = .;
	__ftr_fixup	: { *(__ftr_fixup) }
	VDSO_ftr_fixup_end = .;

	. = ALIGN(8);
	VDSO_mmu_ftr_fixup_start = .;
	__mmu_ftr_fixup	: { *(__mmu_ftr_fixup) }
	VDSO_mmu_ftr_fixup_end = .;

	. = ALIGN(8);
	VDSO_lwsync_fixup_start = .;
	__lwsync_fixup	: { *(__lwsync_fixup) }
	VDSO_lwsync_fixup_end = .;

	. = ALIGN(8);
	VDSO_fw_ftr_fixup_start = .;
	__fw_ftr_fixup	: { *(__fw_ftr_fixup) }
	VDSO_fw_ftr_fixup_end = .;

	/*
	 * Other stuff is appended to the text segment:
	 */
	.rodata		: { *(.rodata .rodata.* .gnu.linkonce.r.*) }
	.rodata1	: { *(.rodata1) }

	.dynamic	: { *(.dynamic) }		:text	:dynamic

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text
	.gcc_except_table : { *(.gcc_except_table) }
	.rela.dyn ALIGN(8) : { *(.rela.dyn) }

	.got ALIGN(8)	: { *(.got .toc) }

	_end = .;
	PROVIDE(end = .);

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	/DISCARD/	: {
		*(.note.GNU-stack)
		*(.branch_lt)
		*(.data .data.* .gnu.linkonce.d.* .sdata*)
		*(.bss .sbss .dynbss .dynsbss)
		*(.opd)
	}
}

/*
 * Very old versions of ld do not recognize this name token; use the constant.
 */
#define PT_GNU_EH_FRAME	0x6474e550

/*
 * We must supply the ELF program headers explicitly to get just one
 * PT_LOAD segment, and set the flags explicitly to make segments read-only.
 */
PHDRS
{
	text		PT_LOAD FILEHDR PHDRS FLAGS(5);	/* PF_R|PF_X */
	dynamic		PT_DYNAMIC FLAGS(4);		/* PF_R */
	note		PT_NOTE FLAGS(4);		/* PF_R */
	eh_frame_hdr	PT_GNU_EH_FRAME;
}

/*
 * This controls what symbols we export from the DSO.
 */
VERSION
{
	VDSO_VERSION_STRING {
	global:
		__kernel_get_syscall_map;
		__kernel_gettimeofday;
		__kernel_clock_gettime;
		__kernel_clock_getres;
		__kernel_get_tbfreq;
		__kernel_sync_dicache;
		__kernel_sigtramp_rt64;
		__kernel_getcpu;
		__kernel_time;

	local: *;
	};
}

/*
 * Make the sigreturn code visible to the kernel.
 */
VDSO_sigtramp_rt64	= __kernel_start_sigtramp_rt64;
