/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

OUTPUT_ARCH(microblaze)
ENTRY(microblaze_start)

#include <asm/page.h>
#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>

#ifdef __MICROBLAZEEL__
jiffies = jiffies_64;
#else
jiffies = jiffies_64 + 4;
#endif

SECTIONS {
	. = CONFIG_KERNEL_START;
	microblaze_start = CONFIG_KERNEL_BASE_ADDR;
	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		_text = . ;
		_stext = . ;
		HEAD_TEXT
		TEXT_TEXT
		*(.fixup)
		EXIT_TEXT
		EXIT_CALL
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		. = ALIGN (4) ;
		_etext = . ;
	}

	. = ALIGN (4) ;
	__fdt_blob : AT(ADDR(__fdt_blob) - LOAD_OFFSET) {
		_fdt_start = . ;		/* place for fdt blob */
		*(__fdt_blob) ;			/* Any link-placed DTB */
	        . = _fdt_start + 0x8000;	/* Pad up to 32kbyte */
		_fdt_end = . ;
	}

	. = ALIGN(16);
	RODATA
	EXCEPTION_TABLE(16)

	/*
	 * sdata2 section can go anywhere, but must be word aligned
	 * and SDA2_BASE must point to the middle of it
	 */
	.sdata2 : AT(ADDR(.sdata2) - LOAD_OFFSET) {
		_ssrw = .;
		. = ALIGN(PAGE_SIZE); /* page aligned when MMU used */
		*(.sdata2)
	. = ALIGN(8);
	_essrw = .;
	_ssrw_size = _essrw - _ssrw;
	_KERNEL_SDA2_BASE_ = _ssrw + (_ssrw_size / 2);
	}

	_sdata = . ;
	RW_DATA_SECTION(32, PAGE_SIZE, THREAD_SIZE)
	_edata = . ;

	/* Under the microblaze ABI, .sdata and .sbss must be contiguous */
	. = ALIGN(8);
	.sdata : AT(ADDR(.sdata) - LOAD_OFFSET) {
		_ssro = .;
		*(.sdata)
	}

	.sbss :	AT(ADDR(.sbss) - LOAD_OFFSET) {
		_ssbss = .;
		*(.sbss)
		_esbss = .;
		_essro = .;
		_ssro_size = _essro - _ssro ;
		_KERNEL_SDA_BASE_ = _ssro + (_ssro_size / 2) ;
	}

	. = ALIGN(PAGE_SIZE);
	__init_begin = .;

	INIT_TEXT_SECTION(PAGE_SIZE)

	.init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {
		INIT_DATA
	}

	. = ALIGN(4);
	.init.ivt : AT(ADDR(.init.ivt) - LOAD_OFFSET) {
		__ivt_start = .;
		*(.init.ivt)
		__ivt_end = .;
	}

	.init.setup : AT(ADDR(.init.setup) - LOAD_OFFSET) {
		INIT_SETUP(0)
	}

	.initcall.init : AT(ADDR(.initcall.init) - LOAD_OFFSET ) {
		INIT_CALLS
	}

	.con_initcall.init : AT(ADDR(.con_initcall.init) - LOAD_OFFSET) {
		CON_INITCALL
	}

	SECURITY_INIT

	__init_end_before_initramfs = .;

	.init.ramfs : AT(ADDR(.init.ramfs) - LOAD_OFFSET) {
		INIT_RAM_FS
	}

	__init_end = .;

	.bss ALIGN (PAGE_SIZE) : AT(ADDR(.bss) - LOAD_OFFSET) {
		/* page aligned when MMU used */
		__bss_start = . ;
			*(.bss*)
			*(COMMON)
		. = ALIGN (4) ;
		__bss_stop = . ;
	}
	. = ALIGN(PAGE_SIZE);
	_end = .;

	DISCARDS
}
