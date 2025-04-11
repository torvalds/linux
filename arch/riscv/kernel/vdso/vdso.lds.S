/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */
#include <asm/page.h>
#include <asm/vdso.h>
#include <vdso/datapage.h>

OUTPUT_ARCH(riscv)

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

	.dynamic	: { *(.dynamic) }		:text	:dynamic

	.rodata		: {
		*(.rodata .rodata.* .gnu.linkonce.r.*)
		*(.got.plt) *(.got)
		*(.data .data.* .gnu.linkonce.d.*)
		*(.dynbss)
		*(.bss .bss.* .gnu.linkonce.b.*)
	}

	.note		: { *(.note.*) }		:text	:note

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text

	/*
	 * Text is well-separated from actual data: there's plenty of
	 * stuff that isn't used at runtime in between.
	 */
	. = ALIGN(16);
	.text		: { *(.text .text.*) }		:text

	. = ALIGN(4);
	.alternative : {
		*(.alternative)
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
	eh_frame_hdr	PT_GNU_EH_FRAME;
}

/*
 * This controls what symbols we export from the DSO.
 */
VERSION
{
	LINUX_4.15 {
	global:
		__vdso_rt_sigreturn;
#ifdef HAS_VGETTIMEOFDAY
		__vdso_gettimeofday;
		__vdso_clock_gettime;
		__vdso_clock_getres;
#endif
		__vdso_getcpu;
		__vdso_flush_icache;
#ifndef COMPAT_VDSO
		__vdso_riscv_hwprobe;
#endif
		__vdso_getrandom;
	local: *;
	};
}
