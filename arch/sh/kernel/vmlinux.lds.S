/*
 * ld script to make SuperH Linux kernel
 * Written by Niibe Yutaka and Paul Mundt
 */
#ifdef CONFIG_SUPERH64
#define LOAD_OFFSET	CONFIG_PAGE_OFFSET
OUTPUT_ARCH(sh:sh5)
#else
#define LOAD_OFFSET	0
OUTPUT_ARCH(sh)
#endif

#include <asm/thread_info.h>
#include <asm/cache.h>
#include <asm/vmlinux.lds.h>

ENTRY(_start)
SECTIONS
{
#ifdef CONFIG_PMB_FIXED
	. = CONFIG_PAGE_OFFSET + (CONFIG_MEMORY_START & 0x1fffffff) +
	    CONFIG_ZERO_PAGE_OFFSET;
#elif defined(CONFIG_32BIT)
	. = CONFIG_PAGE_OFFSET + CONFIG_ZERO_PAGE_OFFSET;
#else
	. = CONFIG_PAGE_OFFSET + CONFIG_MEMORY_START + CONFIG_ZERO_PAGE_OFFSET;
#endif

	_text = .;		/* Text and read-only data */

	.empty_zero_page : AT(ADDR(.empty_zero_page) - LOAD_OFFSET) {
		*(.empty_zero_page)
	} = 0

	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		HEAD_TEXT
		TEXT_TEXT

#ifdef CONFIG_SUPERH64
		*(.text64)
		*(.text..SHmedia32)
#endif

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
	RO_DATA(PAGE_SIZE)

	/*
	 * Code which must be executed uncached and the associated data
	 */
	. = ALIGN(PAGE_SIZE);
	.uncached : AT(ADDR(.uncached) - LOAD_OFFSET) {
		__uncached_start = .;
		*(.uncached.text)
		*(.uncached.data)
		__uncached_end = .;
	}

	RW_DATA_SECTION(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)

	_edata = .;			/* End of data section */

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

	PERCPU(PAGE_SIZE)

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
