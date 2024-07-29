/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/sizes.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/orc_lookup.h>

#define PAGE_SIZE _PAGE_SIZE
#define RO_EXCEPTION_TABLE_ALIGN	4
#define PHYSADDR_MASK			0xffffffffffff /* 48-bit */

/*
 * Put .bss..swapper_pg_dir as the first thing in .bss. This will
 * ensure that it has .bss alignment (64K).
 */
#define BSS_FIRST_SECTIONS *(.bss..swapper_pg_dir)

#include <asm-generic/vmlinux.lds.h>
#include "image-vars.h"

/*
 * Max avaliable Page Size is 64K, so we set SectionAlignment
 * field of EFI application to 64K.
 */
PECOFF_FILE_ALIGN = 0x200;
PECOFF_SEGMENT_ALIGN = 0x10000;

OUTPUT_ARCH(loongarch)
ENTRY(kernel_entry)
PHDRS {
	text PT_LOAD FLAGS(7);	/* RWX */
	note PT_NOTE FLAGS(4);	/* R__ */
}

jiffies	 = jiffies_64;

SECTIONS
{
	. = VMLINUX_LOAD_ADDRESS;

	_text = .;
	HEAD_TEXT_SECTION

	. = ALIGN(PECOFF_SEGMENT_ALIGN);
	_stext = .;
	.text : {
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.fixup)
		*(.gnu.warning)
	} :text = 0
	. = ALIGN(PECOFF_SEGMENT_ALIGN);
	_etext = .;

	. = ALIGN(PECOFF_SEGMENT_ALIGN);
	__init_begin = .;
	__inittext_begin = .;

	INIT_TEXT_SECTION(PAGE_SIZE)
	.exit.text : {
		EXIT_TEXT
	}

	. = ALIGN(PECOFF_SEGMENT_ALIGN);
	__inittext_end = .;

	__initdata_begin = .;

	/*
	 * struct alt_inst entries. From the header (alternative.h):
	 * "Alternative instructions for different CPU types or capabilities"
	 * Think locking instructions on spinlocks.
	 */
	. = ALIGN(4);
	.altinstructions : AT(ADDR(.altinstructions) - LOAD_OFFSET) {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}

	INIT_DATA_SECTION(16)
	.exit.data : {
		EXIT_DATA
	}

#ifdef CONFIG_SMP
	PERCPU_SECTION(1 << CONFIG_L1_CACHE_SHIFT)
#endif

	.init.bss : {
		*(.init.bss)
	}
	. = ALIGN(PECOFF_SEGMENT_ALIGN);
	__initdata_end = .;

	__init_end = .;

	_sdata = .;
	RO_DATA(4096)

	.got : ALIGN(16) { *(.got) }
	.plt : ALIGN(16) { *(.plt) }
	.got.plt : ALIGN(16) { *(.got.plt) }

	RW_DATA(1 << CONFIG_L1_CACHE_SHIFT, PAGE_SIZE, THREAD_SIZE)

	.rela.dyn : ALIGN(8) {
		__rela_dyn_begin = .;
		 *(.rela.dyn) *(.rela*)
		__rela_dyn_end = .;
	}

	.data.rel : { *(.data.rel*) }

#ifdef CONFIG_RELOCATABLE
	. = ALIGN(8);
	.la_abs : AT(ADDR(.la_abs) - LOAD_OFFSET) {
		__la_abs_begin = .;
		*(.la_abs)
		__la_abs_end = .;
	}
#endif

	ORC_UNWIND_TABLE

	.sdata : {
		*(.sdata)
	}
	.edata_padding : { BYTE(0); . = ALIGN(PECOFF_FILE_ALIGN); }
	_edata =  .;

	BSS_SECTION(0, SZ_64K, 8)
	. = ALIGN(PECOFF_SEGMENT_ALIGN);

	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

#ifdef CONFIG_EFI_STUB
	/* header symbols */
	_kernel_entry = ABSOLUTE(kernel_entry & PHYSADDR_MASK);
	_kernel_asize = ABSOLUTE(_end - _text);
	_kernel_fsize = ABSOLUTE(_edata - _text);
	_kernel_vsize = ABSOLUTE(_end - __initdata_begin);
	_kernel_rsize = ABSOLUTE(_edata - __initdata_begin);
#endif

	.gptab.sdata : {
		*(.gptab.data)
		*(.gptab.sdata)
	}
	.gptab.sbss : {
		*(.gptab.bss)
		*(.gptab.sbss)
	}

	DISCARDS
	/DISCARD/ : {
		*(.dynamic .dynsym .dynstr .hash .gnu.hash)
		*(.gnu.attributes)
		*(.options)
		*(.eh_frame)
	}
}
