/*
 * AVR32 linker script for the Linux kernel
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define LOAD_OFFSET 0x00000000
#include <asm-generic/vmlinux.lds.h>
#include <asm/cache.h>
#include <asm/thread_info.h>

OUTPUT_FORMAT("elf32-avr32", "elf32-avr32", "elf32-avr32")
OUTPUT_ARCH(avr32)
ENTRY(_start)

/* Big endian */
jiffies = jiffies_64 + 4;

SECTIONS
{
	. = CONFIG_ENTRY_ADDRESS;
	.init		: AT(ADDR(.init) - LOAD_OFFSET) {
		_text = .;
		__init_begin = .;
			_sinittext = .;
			*(.text.reset)
			INIT_TEXT
			/*
			 * .exit.text is discarded at runtime, not
			 * link time, to deal with references from
			 * __bug_table
			 */
			EXIT_TEXT
			_einittext = .;
		. = ALIGN(4);
		__tagtable_begin = .;
			*(.taglist.init)
		__tagtable_end = .;
	}
	INIT_DATA_SECTION(16)
	. = ALIGN(PAGE_SIZE);
	__init_end = .;

	.text		: AT(ADDR(.text) - LOAD_OFFSET) {
		_evba = .;
		_stext = .;
		*(.ex.text)
		*(.irq.text)
		KPROBES_TEXT
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		*(.fixup)
		*(.gnu.warning)
		_etext = .;
	} = 0xd703d703

	EXCEPTION_TABLE(4)
	RODATA

	.data		: AT(ADDR(.data) - LOAD_OFFSET) {
		_data = .;
		_sdata = .;

		INIT_TASK_DATA(THREAD_SIZE)
		PAGE_ALIGNED_DATA(PAGE_SIZE);
		CACHELINE_ALIGNED_DATA(L1_CACHE_BYTES)
		*(.data.rel*)
		DATA_DATA
		CONSTRUCTORS

		_edata = .;
	}

	BSS_SECTION(0, 8, 8)
	_end = .;

	DWARF_DEBUG

	/* When something in the kernel is NOT compiled as a module, the module
	 * cleanup code and data are put into these segments. Both can then be
	 * thrown away, as cleanup code is never called unless it's a module.
	 */
	DISCARDS
}
