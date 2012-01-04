/*
 * ld script to make SuperH Linux kernel
 * Written by Niibe Yutaka and Paul Mundt
 */
#ifdef CONFIG_SUPERH64
#define LOAD_OFFSET	PAGE_OFFSET
OUTPUT_ARCH(sh:sh5)
#else
#define LOAD_OFFSET	0
OUTPUT_ARCH(sh)
#endif

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

	.empty_zero_page : AT(ADDR(.empty_zero_page) - LOAD_OFFSET) {
		*(.empty_zero_page)
	} = 0

	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		HEAD_TEXT
		TEXT_TEXT
		EXTRA_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		*(.fixup)
		*(.gnu.warning)
		_etext = .;		/* End of text section */
	} = 0x0009

	EXCEPTION_TABLE(16)
	NOTES

	_sdata = .;
	RO_DATA(PAGE_SIZE)
	RW_DATA_SECTION(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	DWARF_EH_FRAME

	. = ALIGN(PAGE_SIZE);		/* Init code and data */
	__init_begin = .;
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)

	. = ALIGN(4);
	.machvec.init : AT(ADDR(.machvec.init) - LOAD_OFFSET) {
		__machvec_start = .;
		*(.machvec.init)
		__machvec_end = .;
	}

	PERCPU_SECTION(L1_CACHE_BYTES)

	/*
	 * .exit.text is discarded at runtime, not link time, to deal with
	 * references from __bug_table
	 */
	.exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) { EXIT_TEXT }
	.exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) { EXIT_DATA }

	. = ALIGN(PAGE_SIZE);
	__init_end = .;
	BSS_SECTION(0, PAGE_SIZE, 4)
	_ebss = .;			/* uClinux MTD sucks */
	_end = . ;

	STABS_DEBUG
	DWARF_DEBUG

	DISCARDS
}
