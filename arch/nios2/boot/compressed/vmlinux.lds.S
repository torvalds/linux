/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2009 Thomas Chou <thomas@wytron.com.tw>
 */

#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf32-littlenios2", "elf32-littlenios2", "elf32-littlenios2")

OUTPUT_ARCH(nios)
ENTRY(_start)	/* Defined in head.S */

SECTIONS
{
	. = (CONFIG_NIOS2_MEM_BASE + CONFIG_NIOS2_BOOT_LINK_OFFSET) |	\
		CONFIG_NIOS2_KERNEL_REGION_BASE;

	_text = .;
	.text : { *(.text) } = 0
	.rodata : { *(.rodata) *(.rodata.*) }
	_etext = .;

	. = ALIGN(32 / 8);
	.data : { *(.data) }
	. = ALIGN(32 / 8);
	_got = .;
	.got : {
		*(.got.plt)
		*(.igot.plt)
		*(.got)
		*(.igot)
	}
	_egot = .;
	_edata =  .;

	. = ALIGN(32 / 8);
	__bss_start = .;
	.bss : { *(.bss) *(.sbss) }
	. = ALIGN(32 / 8);
	_ebss = .;
	end = . ;
	_end = . ;

	got_len = (_egot - _got);
}
