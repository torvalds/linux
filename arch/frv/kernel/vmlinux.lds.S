/* SPDX-License-Identifier: GPL-2.0 */
/* ld script to make FRV Linux kernel
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>;
 */
OUTPUT_FORMAT("elf32-frv", "elf32-frv", "elf32-frv")
OUTPUT_ARCH(frv)
ENTRY(_start)

#include <asm-generic/vmlinux.lds.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/thread_info.h>

jiffies = jiffies_64 + 4;

__page_offset = CONFIG_PAGE_OFFSET;	/* start of area covered by struct pages */
__kernel_image_start = __page_offset;	/* address at which kernel image resides */

SECTIONS
{
  . = __kernel_image_start;

  /* discardable initialisation code and data */
  . = ALIGN(PAGE_SIZE);			/* Init code and data */
  __init_begin = .;

  _sinittext = .;
  .init.text : {
	HEAD_TEXT
#ifndef CONFIG_DEBUG_INFO
	INIT_TEXT
	EXIT_TEXT
	EXIT_DATA
	*(.exitcall.exit)
#endif
  }
  _einittext = .;

  INIT_DATA_SECTION(8)
  PERCPU_SECTION(L1_CACHE_BYTES)

  . = ALIGN(PAGE_SIZE);
  __init_end = .;

  .trap : {
	/* trap table management - read entry-table.S before modifying */
	. = ALIGN(8192);
	__trap_tables = .;
	*(.trap.user)
	*(.trap.kernel)
	. = ALIGN(4096);
	*(.trap.break)
  }

  /* Text and read-only data */
  . = ALIGN(4);
  _text = .;
  _stext = .;
  .text : {
	*(.text..start)
	*(.text..entry)
	*(.text..break)
	*(.text..tlbmiss)
	TEXT_TEXT
	SCHED_TEXT
	CPUIDLE_TEXT
	LOCK_TEXT
#ifdef CONFIG_DEBUG_INFO
	INIT_TEXT
	EXIT_TEXT
	*(.exitcall.exit)
#endif
	*(.fixup)
	*(.gnu.warning)
	*(.exitcall.exit)
	} = 0x9090

  _etext = .;			/* End of text section */

  RODATA

  .rodata : {
	*(.trap.vector)

	/* this clause must not be modified - the ordering and adjacency are imperative */
	__trap_fixup_tables = .;
	*(.trap.fixup.user .trap.fixup.kernel)

	}

  EXCEPTION_TABLE(8)

  _sdata = .;
  .data : {			/* Data */
	INIT_TASK_DATA(THREAD_SIZE)
	CACHELINE_ALIGNED_DATA(L1_CACHE_BYTES)
	DATA_DATA
	*(.data.*)
	EXIT_DATA
	CONSTRUCTORS
	}

  _edata = .;			/* End of data section */

  BUG_TABLE

  /* GP section */
  . = ALIGN(L1_CACHE_BYTES);
  _gp = . + 2048;
  PROVIDE (gp = _gp);

  .sdata : { *(.sdata .sdata.*) }

  /* BSS */
  . = ALIGN(L1_CACHE_BYTES);
  __bss_start = .;

  .sbss		: { *(.sbss .sbss.*) }
  .bss		: { *(.bss .bss.*) }
  .bss..stack	: { *(.bss) }

  __bss_stop = .;
  _end = . ;
  . = ALIGN(PAGE_SIZE);
  __kernel_image_end = .;

  STABS_DEBUG
  DWARF_DEBUG

  .comment 0 : { *(.comment) }

  DISCARDS
}

__kernel_image_size_no_bss = __bss_start - __kernel_image_start;
