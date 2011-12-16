/*
 * Linker script for Hexagon kernel
 *
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#define LOAD_OFFSET PAGE_OFFSET

#include <asm-generic/vmlinux.lds.h>
#include <asm/asm-offsets.h>	/*  Most of the kernel defines are here  */
#include <asm/mem-layout.h>	/*  except for page_offset  */
#include <asm/cache.h>		/*  and now we're pulling cache line size  */
OUTPUT_ARCH(hexagon)
ENTRY(stext)

jiffies = jiffies_64;

/*
See asm-generic/vmlinux.lds.h for expansion of some of these macros.
See asm-generic/sections.h for seemingly required labels.
*/

#define PAGE_SIZE _PAGE_SIZE

/*  This LOAD_OFFSET is temporary for debugging on the simulator; it may change
    for hypervisor pseudo-physical memory.  */


SECTIONS
{
	. = PAGE_OFFSET + LOAD_ADDRESS;

	__init_begin = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	PERCPU_SECTION(L1_CACHE_BYTES)
	__init_end = .;

        . = ALIGN(_PAGE_SIZE);
	_stext = .;
	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		_text = .;
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.fixup)
	}
	_etext = .;

	INIT_DATA_SECTION(PAGE_SIZE)

	_sdata = .;
		RW_DATA_SECTION(32,PAGE_SIZE,PAGE_SIZE)
		RO_DATA_SECTION(PAGE_SIZE)
	_edata = .;

	EXCEPTION_TABLE(16)
	NOTES

	BSS_SECTION(_PAGE_SIZE, _PAGE_SIZE, _PAGE_SIZE)

	_end = .;

	/DISCARD/ : {
		EXIT_TEXT
		EXIT_DATA
		EXIT_CALL
	}

	STABS_DEBUG
	DWARF_DEBUG

}
