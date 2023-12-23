/* SPDX-License-Identifier: GPL-2.0 */
/*    Kernel link layout for various "sections"
 *
 *    Copyright (C) 1999-2003 Matthew Wilcox <willy at parisc-linux.org>
 *    Copyright (C) 2000-2003 Paul Bame <bame at parisc-linux.org>
 *    Copyright (C) 2000 John Marvin <jsm at parisc-linux.org>
 *    Copyright (C) 2000 Michael Ang <mang with subcarrier.org>
 *    Copyright (C) 2002 Randolph Chung <tausq with parisc-linux.org>
 *    Copyright (C) 2003 James Bottomley <jejb with parisc-linux.org>
 *    Copyright (C) 2006-2013 Helge Deller <deller@gmx.de>
 */

/*
 * Put page table entries (swapper_pg_dir) as the first thing in .bss. This
 * will ensure that it has .bss alignment (PAGE_SIZE).
 */
#define BSS_FIRST_SECTIONS	*(.data..vm0.pmd) \
				*(.data..vm0.pgd) \
				*(.data..vm0.pte)

#define CC_USING_PATCHABLE_FUNCTION_ENTRY
#define RO_EXCEPTION_TABLE_ALIGN	8

#include <asm-generic/vmlinux.lds.h>

/* needed for the processor specific cache alignment size */	
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
	
/* ld script to make hppa Linux kernel */
#ifndef CONFIG_64BIT
OUTPUT_FORMAT("elf32-hppa-linux")
OUTPUT_ARCH(hppa)
#else
OUTPUT_FORMAT("elf64-hppa-linux")
OUTPUT_ARCH(hppa:hppa2.0w)
#endif

#define EXIT_TEXT_SECTIONS()	.exit.text : { EXIT_TEXT }
#if !defined(CONFIG_64BIT) || defined(CONFIG_MLONGCALLS)
#define MLONGCALL_KEEP(x)
#define MLONGCALL_DISCARD(x)	x
#else
#define MLONGCALL_KEEP(x)	x
#define MLONGCALL_DISCARD(x)
#endif

ENTRY(parisc_kernel_start)
#ifndef CONFIG_64BIT
jiffies = jiffies_64 + 4;
#else
jiffies = jiffies_64;
#endif
SECTIONS
{
	. = KERNEL_BINARY_TEXT_START;

	__init_begin = .;
	HEAD_TEXT_SECTION
	MLONGCALL_DISCARD(INIT_TEXT_SECTION(8))

	. = ALIGN(PAGE_SIZE);
	INIT_DATA_SECTION(PAGE_SIZE)
	MLONGCALL_DISCARD(EXIT_TEXT_SECTIONS())
	.exit.data :
	{
		EXIT_DATA
	}
	PERCPU_SECTION(8)
	. = ALIGN(4);
	.altinstructions : {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}
	. = ALIGN(HUGEPAGE_SIZE);
	__init_end = .;
	/* freed after init ends here */

	_text = .;		/* Text and read-only data */
	_stext = .;
	MLONGCALL_KEEP(INIT_TEXT_SECTION(8))
	.text ALIGN(PAGE_SIZE) : {
		TEXT_TEXT
		LOCK_TEXT
		SCHED_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.text.do_softirq)
		*(.text.sys_exit)
		*(.text.do_sigaltstack)
		*(.text.do_fork)
		*(.text.div)
		*($$*)			/* millicode routines */
		*(.text.*)
		*(.fixup)
		*(.lock.text)		/* out-of-line lock text */
		*(.gnu.warning)
	}
	MLONGCALL_KEEP(EXIT_TEXT_SECTIONS())
	. = ALIGN(PAGE_SIZE);
	_etext = .;
	/* End of text section */

	/* Start of data section */
	_sdata = .;

	/* Architecturally we need to keep __gp below 0x1000000 and thus
	 * in front of RO_DATA() which stores lots of tracepoint
	 * and ftrace symbols. */
#ifdef CONFIG_64BIT
	. = ALIGN(16);
	/* Linkage tables */
	.opd : {
		__start_opd = .;
		*(.opd)
		__end_opd = .;
	} PROVIDE (__gp = .);
	.plt : {
		*(.plt)
	}
	.dlt : {
		*(.dlt)
	}
#endif

	RO_DATA(8)

	/* unwind info */
	. = ALIGN(4);
	.PARISC.unwind : {
		__start___unwind = .;
		*(.PARISC.unwind)
		__stop___unwind = .;
	}

	/* writeable */
	/* Make sure this is page aligned so
	 * that we can properly leave these
	 * as writable
	 */
	. = ALIGN(HUGEPAGE_SIZE);
	data_start = .;

	/* Data */
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, PAGE_SIZE)

	/* PA-RISC locks requires 16-byte alignment */
	. = ALIGN(16);
	.data..lock_aligned : {
		*(.data..lock_aligned)
	}

	/* End of data section */
	. = ALIGN(PAGE_SIZE);
	_edata = .;

	/* BSS */
	BSS_SECTION(PAGE_SIZE, PAGE_SIZE, PAGE_SIZE)

	. = ALIGN(HUGEPAGE_SIZE);
	_end = . ;

	STABS_DEBUG
	ELF_DETAILS
	.note 0 : { *(.note) }

	/* Sections to be discarded */
	DISCARDS
	/DISCARD/ : {
#ifdef CONFIG_64BIT
		/* temporary hack until binutils is fixed to not emit these
	 	 * for static binaries
		 */
		*(.interp)
		*(.dynsym)
		*(.dynstr)
		*(.dynamic)
		*(.hash)
		*(.gnu.hash)
#endif
	}
}
