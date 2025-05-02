/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ld script to make SuperH Linux kernel
 * Written by Niibe Yutaka and Paul Mundt
 */
OUTPUT_ARCH(sh)
#define RUNTIME_DISCARD_EXIT
#include <asm/thread_info.h>
#include <asm/cache.h>
#include <asm/vmlinux.lds.h>

#ifdef CONFIG_PMB
 #define MEMORY_OFFSET	0
#else
 #define MEMORY_OFFSET	__MEMORY_START
#endif

ENTRY(_start)
SECTIONS
{
	. = PAGE_OFFSET + MEMORY_OFFSET + PHYSICAL_OFFSET + CONFIG_ZERO_PAGE_OFFSET;

	_text = .;		/* Text and read-only data */

	.empty_zero_page : AT(ADDR(.empty_zero_page)) {
		*(.empty_zero_page)
	} = 0

	.text : AT(ADDR(.text)) {
		HEAD_TEXT
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.fixup)
		*(.gnu.warning)
		_etext = .;		/* End of text section */
	} = 0x0009

	EXCEPTION_TABLE(16)

	_sdata = .;
	RO_DATA(PAGE_SIZE)
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	DWARF_EH_FRAME

	. = ALIGN(PAGE_SIZE);		/* Init code and data */
	__init_begin = .;
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)

	. = ALIGN(4);
	.machvec.init : AT(ADDR(.machvec.init)) {
		__machvec_start = .;
		*(.machvec.init)
		__machvec_end = .;
	}

	PERCPU_SECTION(L1_CACHE_BYTES)

	/*
	 * .exit.text is discarded at runtime, not link time, to deal with
	 * references from __bug_table
	 */
	.exit.text : AT(ADDR(.exit.text)) { EXIT_TEXT }
	.exit.data : AT(ADDR(.exit.data)) { EXIT_DATA }

	. = ALIGN(PAGE_SIZE);
	__init_end = .;
	__bss_start = .;
	SBSS(0)
	. = ALIGN(PAGE_SIZE);
	.bss : AT(ADDR(.bss) - LOAD_OFFSET) {
		BSS_FIRST_SECTIONS
		. = ALIGN(PAGE_SIZE);
		*(.bss..page_aligned)
		. = ALIGN(PAGE_SIZE);
		*(.dynbss)
		*(BSS_MAIN)
		*(COMMON)
		. = ALIGN(8);
	}
	__bss_stop = .;
	_end = . ;

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	DISCARDS
}
