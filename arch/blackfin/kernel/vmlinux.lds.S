/*
 * File:         arch/blackfin/kernel/vmlinux.lds.S
 * Based on:     none - original work
 * Author:
 *
 * Created:      Tue Sep 21 2004
 * Description:  Master linker script for blackfin architecture
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define VMLINUX_SYMBOL(_sym_) _##_sym_

#include <asm-generic/vmlinux.lds.h>
#include <asm/mem_map.h>
#include <asm/page.h>

OUTPUT_FORMAT("elf32-bfin")
ENTRY(__start)
_jiffies = _jiffies_64;

SECTIONS
{
	. = CONFIG_BOOT_LOAD;
	.text :
	{
		__text = .;
		_text = .;
		__stext = .;
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		*(.text.lock)
		*(.fixup)

		. = ALIGN(16);
		___start___ex_table = .;
		*(__ex_table)
		___stop___ex_table = .;

		. = ALIGN(4);
		__etext = .;
	}

	RODATA

	.data :
	{
		. = ALIGN(PAGE_SIZE);
		__sdata = .;
		*(.data.init_task)
		DATA_DATA
		CONSTRUCTORS

		. = ALIGN(32);
		*(.data.cacheline_aligned)

		. = ALIGN(PAGE_SIZE);
		__edata = .;
	}

	. = ALIGN(PAGE_SIZE);
	___init_begin = .;
	.init :
	{
		__sinittext = .;
		*(.init.text)
		__einittext = .;
		*(.init.data)
		. = ALIGN(16);
		___setup_start = .;
		*(.init.setup)
		___setup_end = .;
		___start___param = .;
		*(__param)
		___stop___param = .;
		___initcall_start = .;
		INITCALLS
		___initcall_end = .;
		___con_initcall_start = .;
		*(.con_initcall.init)
		___con_initcall_end = .;
		___security_initcall_start = .;
		*(.security_initcall.init)
		___security_initcall_end = .;
		. = ALIGN(4);
		___initramfs_start = .;
		*(.init.ramfs)
		___initramfs_end = .;
		. = ALIGN(4);
	}

	__l1_lma_start = .;

	.text_l1 L1_CODE_START : AT(LOADADDR(.init) + SIZEOF(.init))
	{
		. = ALIGN(4);
		__stext_l1 = .;
		*(.l1.text)

		. = ALIGN(4);
		__etext_l1 = .;
	}

	.data_l1 L1_DATA_A_START : AT(LOADADDR(.text_l1) + SIZEOF(.text_l1))
	{
		. = ALIGN(4);
		__sdata_l1 = .;
		*(.l1.data)
		__edata_l1 = .;

		. = ALIGN(4);
		__sbss_l1 = .;
		*(.l1.bss)

		. = ALIGN(32);
		*(.data_l1.cacheline_aligned)

		. = ALIGN(4);
		__ebss_l1 = .;
	}

	.data_b_l1 L1_DATA_B_START : AT(LOADADDR(.data_l1) + SIZEOF(.data_l1))
	{
		. = ALIGN(4);
		__sdata_b_l1 = .;
		*(.l1.data.B)
		__edata_b_l1 = .;

		. = ALIGN(4);
		__sbss_b_l1 = .;
		*(.l1.bss.B)

		. = ALIGN(4);
		__ebss_b_l1 = .;
	}

	. = LOADADDR(.data_b_l1) + SIZEOF(.data_b_l1);
	___init_end = ALIGN(PAGE_SIZE);

	.bss ___init_end :
	{
		. = ALIGN(4);
		___bss_start = .;
		*(.bss)
		*(COMMON)
		. = ALIGN(4);
		___bss_stop = .;
		__end = .;
	}

	/DISCARD/ :
	{
		*(.exit.text)
		*(.exit.data)
		*(.exitcall.exit)
	}
}
