/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

OUTPUT_FORMAT("elf64-loongarch", "elf64-loongarch", "elf64-loongarch")

OUTPUT_ARCH(loongarch)

SECTIONS
{
	PROVIDE(_start = .);
	. = SIZEOF_HEADERS;

	.hash		: { *(.hash) }			:text
	.gnu.hash	: { *(.gnu.hash) }
	.dynsym		: { *(.dynsym) }
	.dynstr		: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }

	.note		: { *(.note.*) }		:text :note

	.text		: { *(.text*) }			:text
	PROVIDE (__etext = .);
	PROVIDE (_etext = .);
	PROVIDE (etext = .);

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text :eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text

	.dynamic	: { *(.dynamic) }		:text :dynamic

	.rodata		: { *(.rodata*) }		:text

	_end = .;
	PROVIDE(end = .);

	/DISCARD/	: {
		*(.gnu.attributes)
		*(.note.GNU-stack)
		*(.data .data.* .gnu.linkonce.d.* .sdata*)
		*(.bss .sbss .dynbss .dynsbss)
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
		__vdso_getcpu;
		__vdso_clock_getres;
		__vdso_clock_gettime;
		__vdso_gettimeofday;
		__vdso_getrandom;
		__vdso_rt_sigreturn;
	local: *;
	};
}

/*
 * Make the sigreturn code visible to the kernel.
 */
VDSO_sigreturn		= __vdso_rt_sigreturn;
