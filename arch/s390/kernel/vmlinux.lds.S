/* SPDX-License-Identifier: GPL-2.0 */
/* ld script to make s390 Linux kernel
 * Written by Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <asm/thread_info.h>
#include <asm/page.h>

/*
 * Put .bss..swapper_pg_dir as the first thing in .bss. This will
 * make sure it has 16k alignment.
 */
#define BSS_FIRST_SECTIONS *(.bss..swapper_pg_dir)

/* Handle ro_after_init data on our own. */
#define RO_AFTER_INIT_DATA

#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-s390", "elf64-s390", "elf64-s390")
OUTPUT_ARCH(s390:64-bit)
ENTRY(startup)
jiffies = jiffies_64;

PHDRS {
	text PT_LOAD FLAGS(5);	/* R_E */
	data PT_LOAD FLAGS(7);	/* RWE */
	note PT_NOTE FLAGS(0);	/* ___ */
}

SECTIONS
{
	. = 0x00000000;
	.text : {
		/* Text and read-only data */
		HEAD_TEXT
		/*
		 * E.g. perf doesn't like symbols starting at address zero,
		 * therefore skip the initial PSW and channel program located
		 * at address zero and let _text start at 0x200.
		 */
	_text = 0x200;
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.fixup)
		*(.gnu.warning)
	} :text = 0x0700

	. = ALIGN(PAGE_SIZE);
	_etext = .;		/* End of text section */

	NOTES :text :note

	.dummy : { *(.dummy) } :data

	RO_DATA_SECTION(PAGE_SIZE)

#ifdef CONFIG_SHARED_KERNEL
	. = ALIGN(0x100000);	/* VM shared segments are 1MB aligned */
#endif

	. = ALIGN(PAGE_SIZE);
	_eshared = .;		/* End of shareable data */
	_sdata = .;		/* Start of data section */

	. = ALIGN(PAGE_SIZE);
	__start_ro_after_init = .;
	.data..ro_after_init : {
		 *(.data..ro_after_init)
	}
	EXCEPTION_TABLE(16)
	. = ALIGN(PAGE_SIZE);
	__end_ro_after_init = .;

	RW_DATA_SECTION(0x100, PAGE_SIZE, THREAD_SIZE)

	_edata = .;		/* End of data section */

	/* will be freed after init */
	. = ALIGN(PAGE_SIZE);	/* Init code and data */
	__init_begin = .;

	. = ALIGN(PAGE_SIZE);
	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {
		VMLINUX_SYMBOL(_sinittext) = . ;
		INIT_TEXT
		. = ALIGN(PAGE_SIZE);
		VMLINUX_SYMBOL(_einittext) = . ;
	}

	/*
	 * .exit.text is discarded at runtime, not link time,
	 * to deal with references from __bug_table
	*/
	.exit.text : {
		EXIT_TEXT
	}

	.exit.data : {
		EXIT_DATA
	}

	/* early.c uses stsi, which requires page aligned data. */
	. = ALIGN(PAGE_SIZE);
	INIT_DATA_SECTION(0x100)

	PERCPU_SECTION(0x100)
	. = ALIGN(PAGE_SIZE);
	__init_end = .;		/* freed after init ends here */

	BSS_SECTION(PAGE_SIZE, 4 * PAGE_SIZE, PAGE_SIZE)

	_end = . ;

	/* Debugging sections.	*/
	STABS_DEBUG
	DWARF_DEBUG

	/* Sections to be discarded */
	DISCARDS
}
