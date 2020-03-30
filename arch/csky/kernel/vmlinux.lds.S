/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/memory.h>

OUTPUT_ARCH(csky)
ENTRY(_start)

#ifndef __cskyBE__
jiffies = jiffies_64;
#else
jiffies = jiffies_64 + 4;
#endif

#define VBR_BASE \
	. = ALIGN(1024); \
	vec_base = .; \
	. += 512;

SECTIONS
{
	. = PAGE_OFFSET + PHYS_OFFSET_OFFSET;

	_stext = .;
	__init_begin = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(PAGE_SIZE)
	PERCPU_SECTION(L1_CACHE_BYTES)
	. = ALIGN(PAGE_SIZE);
	__init_end = .;

	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		_text = .;
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.fixup)
		*(.gnu.warning)
	} = 0
	_etext = .;

	/* __init_begin __init_end must be page aligned for free_initmem */
	. = ALIGN(PAGE_SIZE);


	_sdata = .;
	RO_DATA(PAGE_SIZE)
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

#ifdef CONFIG_HAVE_TCM
	.tcm_start : {
		. = ALIGN(PAGE_SIZE);
		__tcm_start = .;
	}

	.text_data_tcm FIXADDR_TCM : AT(__tcm_start)
	{
		. = ALIGN(4);
		__stcm_text_data = .;
		*(.tcm.text)
		*(.tcm.rodata)
#ifndef CONFIG_HAVE_DTCM
		*(.tcm.data)
#endif
		. = ALIGN(4);
		__etcm_text_data = .;
	}

	. = ADDR(.tcm_start) + SIZEOF(.tcm_start) + SIZEOF(.text_data_tcm);

#ifdef CONFIG_HAVE_DTCM
	#define ITCM_SIZE	CONFIG_ITCM_NR_PAGES * PAGE_SIZE

	.dtcm_start : {
		__dtcm_start = .;
	}

	.data_tcm FIXADDR_TCM + ITCM_SIZE : AT(__dtcm_start)
	{
		. = ALIGN(4);
		__stcm_data = .;
		*(.tcm.data)
		. = ALIGN(4);
		__etcm_data = .;
	}

	. = ADDR(.dtcm_start) + SIZEOF(.data_tcm);

	.tcm_end : AT(ADDR(.dtcm_start) + SIZEOF(.data_tcm)) {
#else
	.tcm_end : AT(ADDR(.tcm_start) + SIZEOF(.text_data_tcm)) {
#endif
		. = ALIGN(PAGE_SIZE);
		__tcm_end = .;
	}
#endif

	EXCEPTION_TABLE(L1_CACHE_BYTES)
	BSS_SECTION(L1_CACHE_BYTES, PAGE_SIZE, L1_CACHE_BYTES)
	VBR_BASE
	_end = . ;

	STABS_DEBUG
	DWARF_DEBUG

	DISCARDS
}
