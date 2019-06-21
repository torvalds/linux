/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <asm/sgidefs.h>

#if _MIPS_SIM == _MIPS_SIM_ABI64
OUTPUT_FORMAT("elf64-tradlittlemips", "elf64-tradbigmips", "elf64-tradlittlemips")
#elif _MIPS_SIM == _MIPS_SIM_NABI32
OUTPUT_FORMAT("elf32-ntradlittlemips", "elf32-ntradbigmips", "elf32-ntradlittlemips")
#else
OUTPUT_FORMAT("elf32-tradlittlemips", "elf32-tradbigmips", "elf32-tradlittlemips")
#endif

OUTPUT_ARCH(mips)

SECTIONS
{
	PROVIDE(_start = .);
	. = SIZEOF_HEADERS;

	/*
	 * In order to retain compatibility with older toolchains we provide the
	 * ABI flags section ourself. Newer assemblers will automatically
	 * generate .MIPS.abiflags sections so we discard such input sections,
	 * and then manually define our own section here. genvdso will patch
	 * this section to have the correct name/type.
	 */
	.mips_abiflags	: { *(.mips_abiflags) } 	:text :abiflags

	.reginfo	: { *(.reginfo) }		:text :reginfo

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
		*(.MIPS.abiflags)
		*(.gnu.attributes)
		*(.note.GNU-stack)
		*(.data .data.* .gnu.linkonce.d.* .sdata*)
		*(.bss .sbss .dynbss .dynsbss)
	}
}

PHDRS
{
	/*
	 * Provide a PT_MIPS_ABIFLAGS header to assign the ABI flags section
	 * to. We can specify the header type directly here so no modification
	 * is needed later on.
	 */
	abiflags	0x70000003;

	/*
	 * The ABI flags header must exist directly after the PT_INTERP header,
	 * so we must explicitly place the PT_MIPS_REGINFO header after it to
	 * stop the linker putting one in at the start.
	 */
	reginfo		0x70000000;

	text		PT_LOAD		FLAGS(5) FILEHDR PHDRS; /* PF_R|PF_X */
	dynamic		PT_DYNAMIC	FLAGS(4);		/* PF_R */
	note		PT_NOTE		FLAGS(4);		/* PF_R */
	eh_frame_hdr	PT_GNU_EH_FRAME;
}

VERSION
{
	LINUX_2.6 {
#ifndef DISABLE_MIPS_VDSO
	global:
		__vdso_clock_gettime;
		__vdso_gettimeofday;
		__vdso_clock_getres;
#if _MIPS_SIM != _MIPS_SIM_ABI64
		__vdso_clock_gettime64;
#endif
#endif
	local: *;
	};
}
