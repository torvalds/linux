// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/cache.h>
#include <asm/memory.h>

#define LOAD_OFFSET	(PAGE_OFFSET - PHYS_OFFSET)
#include <asm-generic/vmlinux.lds.h>

OUTPUT_ARCH(nds32)
ENTRY(_stext_lma)
jiffies = jiffies_64;

#if defined(CONFIG_GCOV_KERNEL)
#define NDS32_EXIT_KEEP(x)	x
#else
#define NDS32_EXIT_KEEP(x)
#endif

SECTIONS
{
	_stext_lma = TEXTADDR - LOAD_OFFSET;
	. = TEXTADDR;
	__init_begin = .;
	HEAD_TEXT_SECTION
	.exit.text : {
		NDS32_EXIT_KEEP(EXIT_TEXT)
	}
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)
	.exit.data : {
		NDS32_EXIT_KEEP(EXIT_DATA)
	}
	PERCPU_SECTION(L1_CACHE_BYTES)
	__init_end = .;

	. = ALIGN(PAGE_SIZE);
	_stext = .;
	/* Real text segment */
	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		_text = .;		/* Text and read-only data	*/
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.fixup)
	}

	_etext = .;			/* End of text and rodata section */

	_sdata = .;
	RO_DATA(PAGE_SIZE)
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata  =  .;

	EXCEPTION_TABLE(16)
	BSS_SECTION(4, 4, 4)
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	DISCARDS
}
