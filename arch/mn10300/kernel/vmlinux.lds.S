/* MN10300 Main kernel linker script
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#define __VMLINUX_LDS__
#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>

OUTPUT_FORMAT("elf32-am33lin", "elf32-am33lin", "elf32-am33lin")
OUTPUT_ARCH(mn10300)
ENTRY(_start)
jiffies = jiffies_64;
#ifndef CONFIG_MN10300_CURRENT_IN_E2
current = __current;
#endif
SECTIONS
{
  . = CONFIG_KERNEL_TEXT_ADDRESS;
  /* read-only */
  _stext = .;
  _text = .;			/* Text and read-only data */
  .text : {
	HEAD_TEXT
	TEXT_TEXT
	SCHED_TEXT
	LOCK_TEXT
	KPROBES_TEXT
	*(.fixup)
	*(.gnu.warning)
	} = 0xcb

  _etext = .;			/* End of text section */

  EXCEPTION_TABLE(16)
  BUG_TABLE

  RO_DATA(PAGE_SIZE)

  /* writeable */
  .data : {			/* Data */
	DATA_DATA
	CONSTRUCTORS
	}

  .data_nosave : { NOSAVE_DATA; }

  .data.page_aligned : { PAGE_ALIGNED_DATA(PAGE_SIZE); }
  .data.cacheline_aligned : { CACHELINE_ALIGNED_DATA(32); }

  /* rarely changed data like cpu maps */
  . = ALIGN(32);
  .data.read_mostly : AT(ADDR(.data.read_mostly)) {
	READ_MOSTLY_DATA(32);
	_edata = .;		/* End of data section */
  }

  .data.init_task : { INIT_TASK_DATA(THREAD_SIZE); }

  /* might get freed after init */
  . = ALIGN(PAGE_SIZE);
  .smp_locks : AT(ADDR(.smp_locks) - LOAD_OFFSET) {
  	__smp_locks = .;
	*(.smp_locks)
	__smp_locks_end = .;
  }

  /* will be freed after init */
  . = ALIGN(PAGE_SIZE);		/* Init code and data */
  __init_begin = .;
  .init.text : {
	_sinittext = .;
	INIT_TEXT;
	_einittext = .;
  }
  .init.data : { INIT_DATA; }
  .setup.init : { INIT_SETUP(16); }

  __initcall_start = .;
  .initcall.init : {
	INITCALLS
  }
  __initcall_end = .;
  .con_initcall.init : { CON_INITCALL; }

  SECURITY_INIT
  . = ALIGN(4);
  __alt_instructions = .;
  .altinstructions : { *(.altinstructions) }
  __alt_instructions_end = .;
 .altinstr_replacement : { *(.altinstr_replacement) }
  /* .exit.text is discard at runtime, not link time, to deal with references
     from .altinstructions and .eh_frame */
	.exit.text : { EXIT_TEXT; }
	.exit.data : { EXIT_DATA; }

  .init.ramfs : { INIT_RAM_FS; }

  PERCPU(32)
  . = ALIGN(PAGE_SIZE);
  __init_end = .;
  /* freed after init ends here */

  BSS(4)

  _end = . ;

  /* This is where the kernel creates the early boot page tables */
  . = ALIGN(PAGE_SIZE);
  pg0 = .;

  /* Sections to be discarded */
  /DISCARD/ : {
	EXIT_CALL
	}

  STABS_DEBUG

  DWARF_DEBUG
}
