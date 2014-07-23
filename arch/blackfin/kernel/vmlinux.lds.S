/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/mem_map.h>
#include <asm/page.h>
#include <asm/thread_info.h>

OUTPUT_FORMAT("elf32-bfin")
ENTRY(__start)
_jiffies = _jiffies_64;

SECTIONS
{
#ifdef CONFIG_RAMKERNEL
	. = CONFIG_BOOT_LOAD;
#else
	. = CONFIG_ROM_BASE;
#endif

	/* Neither the text, ro_data or bss section need to be aligned
	 * So pack them back to back
	 */
	.text :
	{
		__text = .;
		_text = .;
		__stext = .;
		TEXT_TEXT
#ifndef CONFIG_SCHEDULE_L1
		SCHED_TEXT
#endif
		LOCK_TEXT
		IRQENTRY_TEXT
		KPROBES_TEXT
#ifdef CONFIG_ROMKERNEL
		__sinittext = .;
		INIT_TEXT
		__einittext = .;
		EXIT_TEXT
#endif
		*(.text.*)
		*(.fixup)

#if !L1_CODE_LENGTH
		*(.l1.text)
#endif
		__etext = .;
	}

	EXCEPTION_TABLE(4)
	NOTES

	/* Just in case the first read only is a 32-bit access */
	RO_DATA(4)
	__rodata_end = .;

#ifdef CONFIG_ROMKERNEL
	. = CONFIG_BOOT_LOAD;
	.bss : AT(__rodata_end)
#else
	.bss :
#endif
	{
		. = ALIGN(4);
		___bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
#if !L1_DATA_A_LENGTH
		*(.l1.bss)
#endif
#if !L1_DATA_B_LENGTH
		*(.l1.bss.B)
#endif
		. = ALIGN(4);
		___bss_stop = .;
	}

#if defined(CONFIG_ROMKERNEL)
	.data : AT(LOADADDR(.bss) + SIZEOF(.bss))
#else
	.data :
#endif
	{
		__sdata = .;
		/* This gets done first, so the glob doesn't suck it in */
		CACHELINE_ALIGNED_DATA(32)

#if !L1_DATA_A_LENGTH
		. = ALIGN(32);
		*(.data_l1.cacheline_aligned)
		*(.l1.data)
#endif
#if !L1_DATA_B_LENGTH
		*(.l1.data.B)
#endif
#if !L2_LENGTH
		. = ALIGN(32);
		*(.data_l2.cacheline_aligned)
		*(.l2.data)
#endif

		DATA_DATA
		CONSTRUCTORS

		INIT_TASK_DATA(THREAD_SIZE)

		__edata = .;
	}
	__data_lma = LOADADDR(.data);
	__data_len = SIZEOF(.data);

	/* The init section should be last, so when we free it, it goes into
	 * the general memory pool, and (hopefully) will decrease fragmentation
	 * a tiny bit. The init section has a _requirement_ that it be
	 * PAGE_SIZE aligned
	 */
	. = ALIGN(PAGE_SIZE);
	___init_begin = .;

#ifdef CONFIG_RAMKERNEL
	INIT_TEXT_SECTION(PAGE_SIZE)

	/* We have to discard exit text and such at runtime, not link time, to
	 * handle embedded cross-section references (alt instructions, bug
	 * table, eh_frame, etc...).  We need all of our .text up front and
	 * .data after it for PCREL call issues.
	 */
	.exit.text :
	{
		EXIT_TEXT
	}

	. = ALIGN(16);
	INIT_DATA_SECTION(16)
	PERCPU_SECTION(32)

	.exit.data :
	{
		EXIT_DATA
	}

	.text_l1 L1_CODE_START : AT(LOADADDR(.exit.data) + SIZEOF(.exit.data))
#else
	.init.data : AT(__data_lma + __data_len + 32)
	{
		__sinitdata = .;
		INIT_DATA
		INIT_SETUP(16)
		INIT_CALLS
		CON_INITCALL
		SECURITY_INITCALL
		INIT_RAM_FS

		. = ALIGN(PAGE_SIZE);
		___per_cpu_load = .;
		PERCPU_INPUT(32)

		EXIT_DATA
		__einitdata = .;
	}
	__init_data_lma = LOADADDR(.init.data);
	__init_data_len = SIZEOF(.init.data);
	__init_data_end = .;

	.text_l1 L1_CODE_START : AT(__init_data_lma + __init_data_len)
#endif
	{
		. = ALIGN(4);
		__stext_l1 = .;
		*(.l1.text.head)
		*(.l1.text)
#ifdef CONFIG_SCHEDULE_L1
		SCHED_TEXT
#endif
		. = ALIGN(4);
		__etext_l1 = .;
	}
	__text_l1_lma = LOADADDR(.text_l1);
	__text_l1_len = SIZEOF(.text_l1);
	ASSERT (__text_l1_len <= L1_CODE_LENGTH, "L1 text overflow!")

	.data_l1 L1_DATA_A_START : AT(__text_l1_lma + __text_l1_len)
	{
		. = ALIGN(4);
		__sdata_l1 = .;
		*(.l1.data)
		__edata_l1 = .;

		. = ALIGN(32);
		*(.data_l1.cacheline_aligned)

		. = ALIGN(4);
		__sbss_l1 = .;
		*(.l1.bss)
		. = ALIGN(4);
		__ebss_l1 = .;
	}
	__data_l1_lma = LOADADDR(.data_l1);
	__data_l1_len = SIZEOF(.data_l1);
	ASSERT (__data_l1_len <= L1_DATA_A_LENGTH, "L1 data A overflow!")

	.data_b_l1 L1_DATA_B_START : AT(__data_l1_lma + __data_l1_len)
	{
		. = ALIGN(4);
		__sdata_b_l1 = .;
		*(.l1.data.B)
		__edata_b_l1 = .;

		. = ALIGN(4);
		__sbss_b_l1 = .;
		*(.l1.bss.B)
		. = ALIGN(4);
		__ebss_b_l1 = .;
	}
	__data_b_l1_lma = LOADADDR(.data_b_l1);
	__data_b_l1_len = SIZEOF(.data_b_l1);
	ASSERT (__data_b_l1_len <= L1_DATA_B_LENGTH, "L1 data B overflow!")

	.text_data_l2 L2_START : AT(__data_b_l1_lma + __data_b_l1_len)
	{
		. = ALIGN(4);
		__stext_l2 = .;
		*(.l2.text)
		. = ALIGN(4);
		__etext_l2 = .;

		. = ALIGN(4);
		__sdata_l2 = .;
		*(.l2.data)
		__edata_l2 = .;

		. = ALIGN(32);
		*(.data_l2.cacheline_aligned)

		. = ALIGN(4);
		__sbss_l2 = .;
		*(.l2.bss)
		. = ALIGN(4);
		__ebss_l2 = .;
	}
	__l2_lma = LOADADDR(.text_data_l2);
	__l2_len = SIZEOF(.text_data_l2);
	ASSERT (__l2_len <= L2_LENGTH, "L2 overflow!")

	/* Force trailing alignment of our init section so that when we
	 * free our init memory, we don't leave behind a partial page.
	 */
#ifdef CONFIG_RAMKERNEL
	. = __l2_lma + __l2_len;
#else
	. = __init_data_end;
#endif
	. = ALIGN(PAGE_SIZE);
	___init_end = .;

	__end =.;

	STABS_DEBUG

	DWARF_DEBUG

	DISCARDS
}
