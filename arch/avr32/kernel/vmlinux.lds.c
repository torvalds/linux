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

OUTPUT_FORMAT("elf32-avr32", "elf32-avr32", "elf32-avr32")
OUTPUT_ARCH(avr32)
ENTRY(_start)

/* Big endian */
jiffies = jiffies_64 + 4;

SECTIONS
{
	. = CONFIG_ENTRY_ADDRESS;
	.init		: AT(ADDR(.init) - LOAD_OFFSET) {
		_stext = .;
		__init_begin = .;
			_sinittext = .;
			*(.text.reset)
			*(.init.text)
			/*
			 * .exit.text is discarded at runtime, not
			 * link time, to deal with references from
			 * __bug_table
			 */
			*(.exit.text)
			_einittext = .;
		. = ALIGN(4);
		__tagtable_begin = .;
			*(.taglist)
		__tagtable_end = .;
			*(.init.data)
		. = ALIGN(16);
		__setup_start = .;
			*(.init.setup)
		__setup_end = .;
		. = ALIGN(4);
		__initcall_start = .;
			INITCALLS
		__initcall_end = .;
		__con_initcall_start = .;
			*(.con_initcall.init)
		__con_initcall_end = .;
		__security_initcall_start = .;
			*(.security_initcall.init)
		__security_initcall_end = .;
#ifdef CONFIG_BLK_DEV_INITRD
		. = ALIGN(32);
		__initramfs_start = .;
			*(.init.ramfs)
		__initramfs_end = .;
#endif
		. = ALIGN(4096);
		__init_end = .;
	}

	. = ALIGN(8192);
	.text		: AT(ADDR(.text) - LOAD_OFFSET) {
		_evba = .;
		_text = .;
		*(.ex.text)
		. = 0x50;
		*(.tlbx.ex.text)
		. = 0x60;
		*(.tlbr.ex.text)
		. = 0x70;
		*(.tlbw.ex.text)
		. = 0x100;
		*(.scall.text)
		*(.irq.text)
		*(.text)
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.fixup)
		*(.gnu.warning)
		_etext = .;
	} = 0xd703d703

	. = ALIGN(4);
	__ex_table	: AT(ADDR(__ex_table) - LOAD_OFFSET) {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}

	BUG_TABLE

	RODATA

	. = ALIGN(8192);

	.data		: AT(ADDR(.data) - LOAD_OFFSET) {
		_data = .;
		_sdata = .;
		/*
		 * First, the init task union, aligned to an 8K boundary.
		 */
		*(.data.init_task)

		/* Then, the cacheline aligned data */
		. = ALIGN(32);
		*(.data.cacheline_aligned)

		/* And the rest... */
		*(.data.rel*)
		*(.data)
		CONSTRUCTORS

		_edata = .;
	}


	. = ALIGN(8);
	.bss    	: AT(ADDR(.bss) - LOAD_OFFSET) {
		__bss_start = .;
		*(.bss)
		*(COMMON)
		. = ALIGN(8);
		__bss_stop = .;
		_end = .;
	}

	/* When something in the kernel is NOT compiled as a module, the module
	 * cleanup code and data are put into these segments. Both can then be
	 * thrown away, as cleanup code is never called unless it's a module.
	 */
	/DISCARD/       	: {
		*(.exit.data)
		*(.exitcall.exit)
	}

	DWARF_DEBUG
}
