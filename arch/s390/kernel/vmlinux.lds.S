/* ld script to make s390 Linux kernel
 * Written by Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm-generic/vmlinux.lds.h>

#ifndef CONFIG_64BIT
OUTPUT_FORMAT("elf32-s390", "elf32-s390", "elf32-s390")
OUTPUT_ARCH(s390)
ENTRY(_start)
jiffies = jiffies_64 + 4;
#else
OUTPUT_FORMAT("elf64-s390", "elf64-s390", "elf64-s390")
OUTPUT_ARCH(s390:64-bit)
ENTRY(_start)
jiffies = jiffies_64;
#endif

PHDRS {
	text PT_LOAD FLAGS(5);	/* R_E */
	data PT_LOAD FLAGS(7);	/* RWE */
	note PT_NOTE FLAGS(0);	/* ___ */
}

SECTIONS
{
	. = 0x00000000;
	.text : {
	_text = .;		/* Text and read-only data */
		HEAD_TEXT
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		*(.fixup)
		*(.gnu.warning)
	} :text = 0x0700

	_etext = .;		/* End of text section */

	NOTES :text :note

	RODATA

#ifdef CONFIG_SHARED_KERNEL
	. = ALIGN(0x100000);	/* VM shared segments are 1MB aligned */
#endif

	. = ALIGN(PAGE_SIZE);
	_eshared = .;		/* End of shareable data */
	_sdata = .;		/* Start of data section */

	EXCEPTION_TABLE(16) :data

	RW_DATA_SECTION(0x100, PAGE_SIZE, THREAD_SIZE)

	_edata = .;		/* End of data section */

	/* will be freed after init */
	. = ALIGN(PAGE_SIZE);	/* Init code and data */
	__init_begin = .;

	INIT_TEXT_SECTION(PAGE_SIZE)

	/*
	 * .exit.text is discarded at runtime, not link time,
	 * to deal with references from __bug_table
	*/
	.exit.text : {
		EXIT_TEXT
	}

	/* early.c uses stsi, which requires page aligned data. */
	. = ALIGN(PAGE_SIZE);
	INIT_DATA_SECTION(0x100)

	PERCPU(0x100, PAGE_SIZE)
	. = ALIGN(PAGE_SIZE);
	__init_end = .;		/* freed after init ends here */

	BSS_SECTION(0, 2, 0)

	_end = . ;

	/* Debugging sections.	*/
	STABS_DEBUG
	DWARF_DEBUG

	/* Sections to be discarded */
	DISCARDS
}
