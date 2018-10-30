/* SPDX-License-Identifier: GPL-2.0 */
/* ld script to make m68k Linux kernel */

#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/thread_info.h>

OUTPUT_FORMAT("elf32-m68k", "elf32-m68k", "elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)
jiffies = jiffies_64 + 4;
SECTIONS
{
  . = 0xE002000;
  _text = .;			/* Text and read-only data */
  .text : {
	HEAD_TEXT
	TEXT_TEXT
	IRQENTRY_TEXT
	SOFTIRQENTRY_TEXT
	SCHED_TEXT
	CPUIDLE_TEXT
	LOCK_TEXT
	*(.fixup)
	*(.gnu.warning)
	} :text = 0x4e75
	RODATA

  _etext = .;			/* End of text section */

  EXCEPTION_TABLE(16) :data
  _sdata = .;			/* Start of rw data section */
  RW_DATA_SECTION(16, PAGE_SIZE, THREAD_SIZE) :data
  /* End of data goes *here* so that freeing init code works properly. */
  _edata = .;
  NOTES

  /* will be freed after init */
  . = ALIGN(PAGE_SIZE);	/* Init code and data */
__init_begin = .;
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)
	.m68k_fixup : {
		__start_fixup = .;
		*(.m68k_fixup)
		__stop_fixup = .;
	}
	. = ALIGN(PAGE_SIZE);
	__init_end = .;

  BSS_SECTION(0, 0, 0)

  _end = . ;

  STABS_DEBUG

  /* Sections to be discarded */
  DISCARDS
}
