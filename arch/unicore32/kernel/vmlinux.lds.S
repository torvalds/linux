/*
 * linux/arch/unicore32/kernel/vmlinux.lds.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/memory.h>
#include <asm/page.h>
#include <asm/cache.h>

OUTPUT_ARCH(unicore32)
ENTRY(stext)

jiffies = jiffies_64;

SECTIONS
{
	. = PAGE_OFFSET + KERNEL_IMAGE_START;

	_text = .;
	__init_begin = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)
	PERCPU(L1_CACHE_BYTES, PAGE_SIZE)
	__init_end = .;

	_stext = .;
	.text : {		/* Real text segment */
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT

		*(.fixup)
		*(.gnu.warning)
	}
	_etext = .;

	_sdata = .;
	RO_DATA_SECTION(PAGE_SIZE)
	RW_DATA_SECTION(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	EXCEPTION_TABLE(L1_CACHE_BYTES)
	NOTES

	BSS_SECTION(0, 0, 0)
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG

	DISCARDS		/* Exit code and data */
}
