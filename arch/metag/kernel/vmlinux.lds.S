/* SPDX-License-Identifier: GPL-2.0 */
/* ld script to make Meta Linux kernel */

#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/cache.h>

#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf32-metag", "elf32-metag", "elf32-metag")
OUTPUT_ARCH(metag)
ENTRY(__start)

_jiffies = _jiffies_64;
SECTIONS
{
  . = CONFIG_PAGE_OFFSET;
  _text = .;
  __text = .;
  __stext = .;
  HEAD_TEXT_SECTION
  .text : {
	TEXT_TEXT
	SCHED_TEXT
	CPUIDLE_TEXT
	LOCK_TEXT
	KPROBES_TEXT
	IRQENTRY_TEXT
	SOFTIRQENTRY_TEXT
	*(.text.*)
	*(.gnu.warning)
	}

  __etext = .;			/* End of text section */

  __sdata = .;
  RO_DATA_SECTION(PAGE_SIZE)
  RW_DATA_SECTION(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
  __edata = .;			/* End of data section */

  EXCEPTION_TABLE(16)
  NOTES

  . = ALIGN(PAGE_SIZE);		/* Init code and data */
  ___init_begin = .;
  INIT_TEXT_SECTION(PAGE_SIZE)
  INIT_DATA_SECTION(16)

  .init.arch.info : {
	  ___arch_info_begin = .;
	  *(.arch.info.init)
	  ___arch_info_end = .;
  }

  PERCPU_SECTION(L1_CACHE_BYTES)

  ___init_end = .;

  BSS_SECTION(0, PAGE_SIZE, 0)

  __end = .;

  . = ALIGN(PAGE_SIZE);
  __heap_start = .;

  DWARF_DEBUG

  /* When something in the kernel is NOT compiled as a module, the
   * module cleanup code and data are put into these segments.  Both
   * can then be thrown away, as cleanup code is never called unless
   * it's a module.
   */
  DISCARDS
}
