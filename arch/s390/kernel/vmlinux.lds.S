/* ld script to make s390 Linux kernel
 * Written by Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <asm/thread_info.h>
#include <asm/page.h>
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
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.fixup)
		*(.gnu.warning)
	} :text = 0x0700

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

	.exit.data : {
		EXIT_DATA
	}

	/*
	 * struct alt_inst entries. From the header (alternative.h):
	 * "Alternative instructions for different CPU types or capabilities"
	 * Think locking instructions on spinlocks.
	 * Note, that it is a part of __init region.
	 */
	. = ALIGN(8);
	.altinstructions : {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}

	/*
	 * And here are the replacement instructions. The linker sticks
	 * them as binary blobs. The .altinstructions has enough data to
	 * get the address and the length of them to patch the kernel safely.
	 * Note, that it is a part of __init region.
	 */
	.altinstr_replacement : {
		*(.altinstr_replacement)
	}

	/*
	 * Table with the patch locations to undo expolines
	*/
	.nospec_call_table : {
		__nospec_call_start = . ;
		*(.s390_indirect*)
		__nospec_call_end = . ;
	}
	.nospec_return_table : {
		__nospec_return_start = . ;
		*(.s390_return*)
		__nospec_return_end = . ;
	}

	/* early.c uses stsi, which requires page aligned data. */
	. = ALIGN(PAGE_SIZE);
	INIT_DATA_SECTION(0x100)

	PERCPU_SECTION(0x100)
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
