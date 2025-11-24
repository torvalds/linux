/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is the infamous ld script for the 64 bits vdso
 * library
 */

#include <asm/vdso/vsyscall.h>
#include <asm/page.h>
#include <asm/vdso.h>
#include <asm-generic/vmlinux.lds.h>
#include <vdso/datapage.h>

OUTPUT_FORMAT("elf64-s390", "elf64-s390", "elf64-s390")
OUTPUT_ARCH(s390:64-bit)

SECTIONS
{
	VDSO_VVAR_SYMS

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
		*(.text .stub .text.* .gnu.linkonce.t.*)
	} :text
	PROVIDE(__etext = .);
	PROVIDE(_etext = .);
	PROVIDE(etext = .);

	/*
	 * Other stuff is appended to the text segment:
	 */
	.rodata		: { *(.rodata .rodata.* .gnu.linkonce.r.*) }
	.rodata1	: { *(.rodata1) }

	. = ALIGN(8);
	.altinstructions	: { *(.altinstructions) }
	.altinstr_replacement	: { *(.altinstr_replacement) }

	.dynamic	: { *(.dynamic) }		:text	:dynamic

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text
	.gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) }

	.rela.dyn ALIGN(8) : { *(.rela.dyn) }
	.got ALIGN(8)	: { *(.got .toc) }
	.got.plt ALIGN(8) : { *(.got.plt) }

	_end = .;
	PROVIDE(end = .);

	STABS_DEBUG
	DWARF_DEBUG
	.comment       0 : { *(.comment) }
	.gnu.attributes 0 : { KEEP (*(.gnu.attributes)) }

	/DISCARD/	: {
		*(.note.GNU-stack)
		*(.branch_lt)
		*(.data .data.* .gnu.linkonce.d.* .sdata*)
		*(.bss .sbss .dynbss .dynsbss)
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
		/*
		 * Has to be there for the kernel to find
		 */
		__kernel_gettimeofday;
		__kernel_clock_gettime;
		__kernel_clock_getres;
		__kernel_getcpu;
		__kernel_restart_syscall;
		__kernel_rt_sigreturn;
		__kernel_sigreturn;
		__kernel_getrandom;
	local: *;
	};
}
