/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#define RO_EXCEPTION_TABLE_ALIGN	4
#define RUNTIME_DISCARD_EXIT

#ifdef CONFIG_XIP_KERNEL
#include "vmlinux-xip.lds.S"
#else

#include <asm/pgtable.h>
#define LOAD_OFFSET KERNEL_LINK_ADDR

#include <asm/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/set_memory.h>
#include "image-vars.h"

#include <linux/sizes.h>
OUTPUT_ARCH(riscv)
ENTRY(_start)

jiffies = jiffies_64;

PECOFF_SECTION_ALIGNMENT = 0x1000;
PECOFF_FILE_ALIGNMENT = 0x200;

SECTIONS
{
	/* Beginning of code and text segment */
	. = LOAD_OFFSET;
	_start = .;
	HEAD_TEXT_SECTION
	. = ALIGN(PAGE_SIZE);

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

	. = ALIGN(SECTION_ALIGN);
	__init_begin = .;
	__init_text_begin = .;
	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) ALIGN(SECTION_ALIGN) { \
		_sinittext = .;						\
		INIT_TEXT						\
		_einittext = .;						\
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
	/* we have to discard exit text and such at runtime, not link time */
	.exit.text :
	{
		EXIT_TEXT
	}

	__init_text_end = .;
	. = ALIGN(SECTION_ALIGN);
#ifdef CONFIG_EFI
	. = ALIGN(PECOFF_SECTION_ALIGNMENT);
	__pecoff_text_end = .;
#endif
	/* Start of init data section */
	__init_data_begin = .;
	INIT_DATA_SECTION(16)
	.init.bss : {
		*(.init.bss)	/* from the EFI stub */
	}
	.exit.data :
	{
		EXIT_DATA
	}
	PERCPU_SECTION(L1_CACHE_BYTES)

	.rel.dyn : {
		*(.rel.dyn*)
	}

	.rela.dyn : {
		*(.rela*)
	}

	__init_data_end = .;

	. = ALIGN(8);
	.alternative : {
		__alt_start = .;
		*(.alternative)
		__alt_end = .;
	}
	__init_end = .;

	/* Start of data section */
	_sdata = .;
	RO_DATA(SECTION_ALIGN)
	.srodata : {
		*(.srodata*)
	}

	. = ALIGN(SECTION_ALIGN);
	_data = .;

	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_ALIGN)
	.sdata : {
		__global_pointer$ = . + 0x800;
		*(.sdata*)
	}

#ifdef CONFIG_EFI
	.pecoff_edata_padding : { BYTE(0); . = ALIGN(PECOFF_FILE_ALIGNMENT); }
	__pecoff_data_raw_size = ABSOLUTE(. - __pecoff_text_end);
#endif

	/* End of data section */
	_edata = .;

	BSS_SECTION(PAGE_SIZE, PAGE_SIZE, 0)

#ifdef CONFIG_EFI
	. = ALIGN(PECOFF_SECTION_ALIGNMENT);
	__pecoff_data_virt_size = ABSOLUTE(. - __pecoff_text_end);
#endif
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS
	.riscv.attributes 0 : { *(.riscv.attributes) }

	DISCARDS
}
#endif /* CONFIG_XIP_KERNEL */
