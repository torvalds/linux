/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#define LOAD_OFFSET PAGE_OFFSET
#include <asm/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/thread_info.h>

OUTPUT_ARCH(riscv)
ENTRY(_start)

jiffies = jiffies_64;

SECTIONS
{
	/* Beginning of code and text segment */
	. = LOAD_OFFSET;
	_start = .;
	__init_begin = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)
	/* we have to discard exit text and such at runtime, not link time */
	.exit.text :
	{
		EXIT_TEXT
	}
	.exit.data :
	{
		EXIT_DATA
	}
	PERCPU_SECTION(L1_CACHE_BYTES)
	__init_end = .;

	.text : {
		_text = .;
		_stext = .;
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		ENTRY_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.fixup)
		_etext = .;
	}

	/* Start of data section */
	_sdata = .;
	RO_DATA(L1_CACHE_BYTES)
	.srodata : {
		*(.srodata*)
	}

	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	.sdata : {
		__global_pointer$ = . + 0x800;
		*(.sdata*)
		/* End of data section */
		_edata = .;
		*(.sbss*)
	}

	BSS_SECTION(PAGE_SIZE, PAGE_SIZE, 0)

	EXCEPTION_TABLE(0x10)

	.rel.dyn : {
		*(.rel.dyn*)
	}

	_end = .;

	STABS_DEBUG
	DWARF_DEBUG

	DISCARDS
}
