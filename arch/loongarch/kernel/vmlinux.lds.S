/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/sizes.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

#define PAGE_SIZE _PAGE_SIZE

/*
 * Put .bss..swapper_pg_dir as the first thing in .bss. This will
 * ensure that it has .bss alignment (64K).
 */
#define BSS_FIRST_SECTIONS *(.bss..swapper_pg_dir)

#include <asm-generic/vmlinux.lds.h>

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
	.text : {
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.fixup)
		*(.gnu.warning)
	} :text = 0
	. = ALIGN(PECOFF_SEGMENT_ALIGN);
	_etext = .;

	EXCEPTION_TABLE(16)

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
	RW_DATA(1 << CONFIG_L1_CACHE_SHIFT, PAGE_SIZE, THREAD_SIZE)

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
		*(.gnu.attributes)
		*(.options)
		*(.eh_frame)
	}
}
