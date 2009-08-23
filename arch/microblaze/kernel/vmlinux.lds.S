/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

OUTPUT_FORMAT("elf32-microblaze", "elf32-microblaze", "elf32-microblaze")
OUTPUT_ARCH(microblaze)
ENTRY(_start)

#include <asm-generic/vmlinux.lds.h>

jiffies = jiffies_64 + 4;

SECTIONS {
	. = CONFIG_KERNEL_START;
	.text : {
		_text = . ;
		_stext = . ;
		*(.text .text.*)
		*(.fixup)

		*(.exitcall.exit)
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		. = ALIGN (4) ;
		_etext = . ;
	}

	. = ALIGN (4) ;
	_fdt_start = . ; /* place for fdt blob */
	. = . + 0x4000;
	_fdt_end = . ;

	. = ALIGN(16);
	RODATA
	. = ALIGN(16);
	__ex_table : {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}

	/*
	 * sdata2 section can go anywhere, but must be word aligned
	 * and SDA2_BASE must point to the middle of it
	 */
	.sdata2 : {
		_ssrw = .;
		. = ALIGN(4096); /* page aligned when MMU used - origin 0x8 */
		*(.sdata2)
	. = ALIGN(8);
	_essrw = .;
	_ssrw_size = _essrw - _ssrw;
	_KERNEL_SDA2_BASE_ = _ssrw + (_ssrw_size / 2);
	}

	_sdata = . ;
	.data ALIGN (4096) : { /* page aligned when MMU used - origin 0x4 */
		DATA_DATA
		CONSTRUCTORS
	}
	. = ALIGN(32);
	.data.cacheline_aligned : { *(.data.cacheline_aligned) }
	_edata = . ;

	/* Reserve some low RAM for r0 based memory references */
	. = ALIGN(0x4) ;
	r0_ram = . ;
	. = . +  4096;	/* a page should be enough */

	/* The initial task */
	. = ALIGN(8192);
	.data.init_task : { *(.data.init_task) }

	/* Under the microblaze ABI, .sdata and .sbss must be contiguous */
	. = ALIGN(8);
	.sdata : {
		_ssro = .;
		*(.sdata)
	}

	.sbss :	{
		_ssbss = .;
		*(.sbss)
		_esbss = .;
		_essro = .;
		_ssro_size = _essro - _ssro ;
		_KERNEL_SDA_BASE_ = _ssro + (_ssro_size / 2) ;
	}

	__init_begin = .;

	. = ALIGN(4096);
	.init.text : {
		_sinittext = . ;
		INIT_TEXT
		_einittext = .;
	}

	.init.data : {
		INIT_DATA
	}

	. = ALIGN(4);
	.init.ivt : {
		__ivt_start = .;
		*(.init.ivt)
		__ivt_end = .;
	}

	.init.setup : {
		__setup_start = .;
		*(.init.setup)
		__setup_end = .;
	}

	.initcall.init : {
		__initcall_start = .;
		INITCALLS
		__initcall_end = .;
	}

	.con_initcall.init : {
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
	}

	SECURITY_INIT

	__init_end_before_initramfs = .;

	.init.ramfs ALIGN(4096) : {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
		. = ALIGN(4);
		LONG(0);
/*
 * FIXME this can break initramfs for MMU.
 * Pad init.ramfs up to page boundary,
 * so that __init_end == __bss_start. This will make image.elf
 * consistent with the image.bin
 */
		/* . = ALIGN(4096); */
	}
	__init_end = .;

	.bss ALIGN (4096) : { /* page aligned when MMU used */
		__bss_start = . ;
			*(.bss*)
			*(COMMON)
		. = ALIGN (4) ;
		__bss_stop = . ;
		_ebss = . ;
	}
	. = ALIGN(4096);
	_end = .;
}
