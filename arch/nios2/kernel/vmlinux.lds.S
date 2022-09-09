/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2009 Thomas Chou <thomas@wytron.com.tw>
 */
#include <asm/page.h>
#include <asm-generic/vmlinux.lds.h>
#include <asm/cache.h>
#include <asm/thread_info.h>

OUTPUT_FORMAT("elf32-littlenios2", "elf32-littlenios2", "elf32-littlenios2")

OUTPUT_ARCH(nios)
ENTRY(_start)	/* Defined in head.S */

jiffies = jiffies_64;

SECTIONS
{
	. = CONFIG_NIOS2_MEM_BASE | CONFIG_NIOS2_KERNEL_REGION_BASE;

	_text = .;
	_stext = .;
	HEAD_TEXT_SECTION
	.text : {
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		KPROBES_TEXT
	} =0
	_etext = .;

	.got : {
		*(.got.plt)
		*(.igot.plt)
		*(.got)
		*(.igot)
	}

	EXCEPTION_TABLE(L1_CACHE_BYTES)

	. = ALIGN(PAGE_SIZE);
	__init_begin = .;
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(PAGE_SIZE)
	PERCPU_SECTION(L1_CACHE_BYTES)
	__init_end = .;

	_sdata = .;
	RO_DATA(PAGE_SIZE)
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	BSS_SECTION(0, 0, 0)
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	DISCARDS
}
