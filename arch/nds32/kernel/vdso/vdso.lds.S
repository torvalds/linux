/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2005-2017 Andes Technology Corporation
 */


#include <linux/const.h>
#include <asm/page.h>
#include <asm/vdso.h>

OUTPUT_ARCH(nds32)

SECTIONS
{
	. = SIZEOF_HEADERS;

	.hash		: { *(.hash) }			:text
	.gnu.hash	: { *(.gnu.hash) }
	.dynsym		: { *(.dynsym) }
	.dynstr		: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }

	.note		: { *(.note.*) }		:text	:note


	.text		: { *(.text*) }			:text

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text

	.dynamic	: { *(.dynamic) }		:text	:dynamic

	.rodata		: { *(.rodata*) }		:text


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
	eh_frame_hdr	PT_GNU_EH_FRAME;
}

/*
 * This controls what symbols we export from the DSO.
 */
VERSION
{
	LINUX_4 {
	global:
		__kernel_rt_sigreturn;
		__vdso_gettimeofday;
		__vdso_clock_getres;
		__vdso_clock_gettime;
	local: *;
	};
}

/*
 * Make the rt_sigreturn code visible to the kernel.
 */
VDSO_rt_sigtramp	= __kernel_rt_sigreturn;
