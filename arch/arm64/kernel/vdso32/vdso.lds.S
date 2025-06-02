/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Adapted from arm64 version.
 *
 * GNU linker script for the VDSO library.
 * Heavily based on the vDSO linker scripts for other archs.
 *
 * Copyright (C) 2012-2018 ARM Limited
 */

#include <linux/const.h>
#include <asm/page.h>
#include <asm/vdso.h>
#include <asm-generic/vmlinux.lds.h>
#include <vdso/datapage.h>

OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)

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

	.dynamic	: { *(.dynamic) }		:text	:dynamic

	.rodata		: {
		*(.rodata*)
		*(.got)
		*(.got.plt)
		*(.plt)
		*(.rel.iplt)
		*(.iplt)
		*(.igot.plt)
	}						:text

	.text		: {
		*(.text*)
		*(.glue_7)
		*(.glue_7t)
		*(.vfp11_veneer)
		*(.v4_bx)
	}						:text	=0xe7f001f2

	.rel.dyn	: { *(.rel*) }

	.ARM.exidx : { *(.ARM.exidx*) }
	DWARF_DEBUG
	ELF_DETAILS
	.ARM.attributes 0 : { *(.ARM.attributes) }

	/DISCARD/	: {
		*(.note.GNU-stack)
		*(.data .data.* .gnu.linkonce.d.* .sdata*)
		*(.bss .sbss .dynbss .dynsbss)
	}
}

/*
 * We must supply the ELF program headers explicitly to get just one
 * PT_LOAD segment, and set the flags explicitly to make segments read-only.
 */
PHDRS
{
	text		PT_LOAD		FLAGS(5) FILEHDR PHDRS; /* PF_R|PF_X */
	dynamic		PT_DYNAMIC	FLAGS(4);		/* PF_R */
	note		PT_NOTE		FLAGS(4);		/* PF_R */
}

VERSION
{
	LINUX_2.6 {
	global:
		__vdso_clock_gettime;
		__vdso_gettimeofday;
		__vdso_clock_getres;
		__vdso_clock_gettime64;
	local: *;
	};
}
