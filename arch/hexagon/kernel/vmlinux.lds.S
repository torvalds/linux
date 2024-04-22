/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linker script for Hexagon kernel
 *
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/asm-offsets.h>	/*  Most of the kernel defines are here  */
#include <asm/mem-layout.h>	/*  except for page_offset  */
#include <asm/cache.h>		/*  and now we're pulling cache line size  */
#include <asm/thread_info.h>	/*  and we need THREAD_SIZE too */

OUTPUT_ARCH(hexagon)
ENTRY(stext)

jiffies = jiffies_64;

/*
See asm-generic/vmlinux.lds.h for expansion of some of these macros.
See asm-generic/sections.h for seemingly required labels.
*/

#define PAGE_SIZE _PAGE_SIZE

SECTIONS
{
	. = PAGE_OFFSET;

	__init_begin = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	PERCPU_SECTION(L1_CACHE_BYTES)
	__init_end = .;

        . = ALIGN(_PAGE_SIZE);
	_stext = .;
	.text : AT(ADDR(.text)) {
		_text = .;
		TEXT_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.fixup)
	}
	_etext = .;

	INIT_DATA_SECTION(PAGE_SIZE)

	_sdata = .;
		RW_DATA(32,PAGE_SIZE,_THREAD_SIZE)
		RO_DATA(PAGE_SIZE)
	_edata = .;

	EXCEPTION_TABLE(16)

	BSS_SECTION(_PAGE_SIZE, _PAGE_SIZE, _PAGE_SIZE)

	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS
	.hexagon.attributes 0 : { *(.hexagon.attributes) }

	DISCARDS
}
