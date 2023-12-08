/* SPDX-License-Identifier: GPL-2.0-only */

#include <asm/page.h>

OUTPUT_ARCH(csky)

SECTIONS
{
	PROVIDE(_vdso_data = . + PAGE_SIZE);
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

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text

	.rodata		: { *(.rodata .rodata.* .gnu.linkonce.r.*) }

	. = 0x800;
	.text		: { *(.text .text.*) }		:text

	.data		: {
		*(.got.plt) *(.got)
		*(.data .data.* .gnu.linkonce.d.*)
		*(.dynbss)
		*(.bss .bss.* .gnu.linkonce.b.*)
	}
}

PHDRS
{
	text		PT_LOAD		FLAGS(5) FILEHDR PHDRS; /* PF_R|PF_X */
	dynamic		PT_DYNAMIC	FLAGS(4);		/* PF_R */
	note		PT_NOTE		FLAGS(4);		/* PF_R */
	eh_frame_hdr	PT_GNU_EH_FRAME;
}

VERSION
{
	LINUX_5.10 {
	global:
		__vdso_rt_sigreturn;
		__vdso_clock_gettime;
		__vdso_clock_gettime64;
		__vdso_gettimeofday;
		__vdso_clock_getres;
	local: *;
	};
}
