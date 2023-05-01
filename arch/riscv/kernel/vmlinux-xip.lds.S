/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 * Copyright (C) 2020 Vitaly Wool, Konsulko AB
 */

#include <asm/pgtable.h>
#define LOAD_OFFSET KERNEL_LINK_ADDR
/* No __ro_after_init data in the .rodata section - which will always be ro */
#define RO_AFTER_INIT_DATA

#include <asm/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/thread_info.h>

OUTPUT_ARCH(riscv)
ENTRY(_start)

jiffies = jiffies_64;

SECTIONS
{
	/* Beginning of code and text segment */
	. = LOAD_OFFSET;
	_xiprom = .;
	_start = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	/* we have to discard exit text and such at runtime, not link time */
	.exit.text :
	{
		EXIT_TEXT
	}

	.text : {
		_text = .;
		_stext = .;
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		ENTRY_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		_etext = .;
	}
	RO_DATA(L1_CACHE_BYTES)
	.srodata : {
		*(.srodata*)
	}
	.init.rodata : {
		INIT_SETUP(16)
		INIT_CALLS
		CON_INITCALL
		INIT_RAM_FS
	}
	_exiprom = .;			/* End of XIP ROM area */


/*
 * From this point, stuff is considered writable and will be copied to RAM
 */
	__data_loc = ALIGN(PAGE_SIZE);		/* location in file */
	. = KERNEL_LINK_ADDR + XIP_OFFSET;	/* location in memory */

#undef LOAD_OFFSET
#define LOAD_OFFSET (KERNEL_LINK_ADDR + XIP_OFFSET - (__data_loc & XIP_OFFSET_MASK))

	_sdata = .;			/* Start of data section */
	_data = .;
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;
	__start_ro_after_init = .;
	.data.ro_after_init : AT(ADDR(.data.ro_after_init) - LOAD_OFFSET) {
		*(.data..ro_after_init)
	}
	__end_ro_after_init = .;

	. = ALIGN(PAGE_SIZE);
	__init_begin = .;
	.init.data : {
		INIT_DATA
	}
	.exit.data : {
		EXIT_DATA
	}
	. = ALIGN(8);
	__soc_early_init_table : {
		__soc_early_init_table_start = .;
		KEEP(*(__soc_early_init_table))
		__soc_early_init_table_end = .;
	}
	__soc_builtin_dtb_table : {
		__soc_builtin_dtb_table_start = .;
		KEEP(*(__soc_builtin_dtb_table))
		__soc_builtin_dtb_table_end = .;
	}

	. = ALIGN(8);
	.alternative : {
		__alt_start = .;
		*(.alternative)
		__alt_end = .;
	}
	__init_end = .;

	. = ALIGN(16);
	.xip.traps : {
		__xip_traps_start = .;
		*(.xip.traps)
		__xip_traps_end = .;
	}

	. = ALIGN(PAGE_SIZE);
	.sdata : {
		__global_pointer$ = . + 0x800;
		*(.sdata*)
		*(.sbss*)
	}

	BSS_SECTION(PAGE_SIZE, PAGE_SIZE, 0)

	PERCPU_SECTION(L1_CACHE_BYTES)

	.rel.dyn : AT(ADDR(.rel.dyn) - LOAD_OFFSET) {
		*(.rel.dyn*)
	}

	/*
	 * End of copied data. We need a dummy section to get its LMA.
	 * Also located before final ALIGN() as trailing padding is not stored
	 * in the resulting binary file and useless to copy.
	 */
	.data.endmark : AT(ADDR(.data.endmark) - LOAD_OFFSET) { }
	_edata_loc = LOADADDR(.data.endmark);

	. = ALIGN(PAGE_SIZE);
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG

	DISCARDS
}
