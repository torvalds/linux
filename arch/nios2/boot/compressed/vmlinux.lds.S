/*
 * Copyright (C) 2009 Thomas Chou <thomas@wytron.com.tw>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
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
