/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * arch/sh5/vmlinux.lds.S
 *
 * ld script to make ST50 Linux kernel
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 * benedict.gaster@superh.com:	 2nd May 2002
 *    Add definition of empty_zero_page to be the first page of kernel image.
 *
 * benedict.gaster@superh.com:	 3rd May 2002
 *    Added support for ramdisk, removing statically linked romfs at the same time.
 *
 * lethal@linux-sh.org:          9th May 2003
 *    Kill off GLOBAL_NAME() usage and other CDC-isms.
 *
 * lethal@linux-sh.org:         19th May 2003
 *    Remove support for ancient toolchains.
 */

#include <linux/config.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/processor.h>
#include <asm/thread_info.h>

#define LOAD_OFFSET	CONFIG_CACHED_MEMORY_OFFSET
#include <asm-generic/vmlinux.lds.h>

#ifdef NOTDEF
#ifdef CONFIG_LITTLE_ENDIAN
OUTPUT_FORMAT("elf32-sh64l-linux", "elf32-sh64l-linux", "elf32-sh64l-linux")
#else
OUTPUT_FORMAT("elf32-sh64", "elf32-sh64", "elf32-sh64")
#endif
#endif

OUTPUT_ARCH(sh:sh5)

#define C_PHYS(x) AT (ADDR(x) - LOAD_OFFSET)

ENTRY(__start)
SECTIONS
{
  . = CONFIG_CACHED_MEMORY_OFFSET + CONFIG_MEMORY_START + PAGE_SIZE;
  _text = .;			/* Text and read-only data */
  text = .;			/* Text and read-only data */

  .empty_zero_page : C_PHYS(.empty_zero_page) {
	*(.empty_zero_page)
	} = 0

  .text : C_PHYS(.text) {
	*(.text)
	*(.text64)
        *(.text..SHmedia32)
	SCHED_TEXT
	LOCK_TEXT
	*(.fixup)
	*(.gnu.warning)
#ifdef CONFIG_LITTLE_ENDIAN
	} = 0x6ff0fff0
#else
	} = 0xf0fff06f
#endif

  /* We likely want __ex_table to be Cache Line aligned */
  . = ALIGN(L1_CACHE_BYTES);		/* Exception table */
  __start___ex_table = .;
  __ex_table : C_PHYS(__ex_table) { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  _etext = .;			/* End of text section */

  .data : C_PHYS(.data) {			/* Data */
	*(.data)
	CONSTRUCTORS
	}

  . = ALIGN(PAGE_SIZE);
  .data.page_aligned : C_PHYS(.data.page_aligned) { *(.data.page_aligned) }

  . = ALIGN(L1_CACHE_BYTES);
  __per_cpu_start = .;
  .data.percpu : C_PHYS(.data.percpu) { *(.data.percpu) }
  __per_cpu_end = . ;
  .data.cacheline_aligned : C_PHYS(.data.cacheline_aligned) { *(.data.cacheline_aligned) }

  _edata = .;			/* End of data section */

  . = ALIGN(THREAD_SIZE);	/* init_task: structure size aligned */
  .data.init_task : C_PHYS(.data.init_task) { *(.data.init_task) }

  . = ALIGN(PAGE_SIZE);		/* Init code and data */
  __init_begin = .;
  _sinittext = .;
  .init.text : C_PHYS(.init.text) { *(.init.text) }
  _einittext = .;
  .init.data : C_PHYS(.init.data) { *(.init.data) }
  . = ALIGN(L1_CACHE_BYTES);	/* Better if Cache Line aligned */
  __setup_start = .;
  .init.setup : C_PHYS(.init.setup) { *(.init.setup) }
  __setup_end = .;
  __initcall_start = .;
  .initcall.init : C_PHYS(.initcall.init) {
  	*(.initcall1.init)
  	*(.initcall2.init)
  	*(.initcall3.init)
  	*(.initcall4.init)
  	*(.initcall5.init)
  	*(.initcall6.init)
  	*(.initcall7.init)
  }
  __initcall_end = .;
  __con_initcall_start = .;
  .con_initcall.init : C_PHYS(.con_initcall.init) { *(.con_initcall.init) }
  __con_initcall_end = .;
  SECURITY_INIT
  __initramfs_start = .;
  .init.ramfs : C_PHYS(.init.ramfs) { *(.init.ramfs) }
  __initramfs_end = .;
  . = ALIGN(PAGE_SIZE);
  __init_end = .;

  /* Align to the biggest single data representation, head and tail */
  . = ALIGN(8);
  __bss_start = .;		/* BSS */
  .bss : C_PHYS(.bss) {
	*(.bss)
	}
  . = ALIGN(8);
  _end = . ;

  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exit.text)
	*(.exit.data)
	*(.exitcall.exit)
	}

  /* Stabs debugging sections.  */
  .stab 0 : C_PHYS(.stab) { *(.stab) }
  .stabstr 0 : C_PHYS(.stabstr) { *(.stabstr) }
  .stab.excl 0 : C_PHYS(.stab.excl) { *(.stab.excl) }
  .stab.exclstr 0 : C_PHYS(.stab.exclstr) { *(.stab.exclstr) }
  .stab.index 0 : C_PHYS(.stab.index) { *(.stab.index) }
  .stab.indexstr 0 : C_PHYS(.stab.indexstr) { *(.stab.indexstr) }
  .comment 0 : C_PHYS(.comment) { *(.comment) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging section are relative to the beginning
     of the section so we begin .debug at 0.  */
  /* DWARF 1 */
  .debug          0 : C_PHYS(.debug) { *(.debug) }
  .line           0 : C_PHYS(.line) { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : C_PHYS(.debug_srcinfo) { *(.debug_srcinfo) }
  .debug_sfnames  0 : C_PHYS(.debug_sfnames) { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : C_PHYS(.debug_aranges) { *(.debug_aranges) }
  .debug_pubnames 0 : C_PHYS(.debug_pubnames) { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info     0 : C_PHYS(.debug_info) { *(.debug_info) }
  .debug_abbrev   0 : C_PHYS(.debug_abbrev) { *(.debug_abbrev) }
  .debug_line     0 : C_PHYS(.debug_line) { *(.debug_line) }
  .debug_frame    0 : C_PHYS(.debug_frame) { *(.debug_frame) }
  .debug_str      0 : C_PHYS(.debug_str) { *(.debug_str) }
  .debug_loc      0 : C_PHYS(.debug_loc) { *(.debug_loc) }
  .debug_macinfo  0 : C_PHYS(.debug_macinfo) { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : C_PHYS(.debug_weaknames) { *(.debug_weaknames) }
  .debug_funcnames 0 : C_PHYS(.debug_funcnames) { *(.debug_funcnames) }
  .debug_typenames 0 : C_PHYS(.debug_typenames) { *(.debug_typenames) }
  .debug_varnames  0 : C_PHYS(.debug_varnames) { *(.debug_varnames) }
  /* These must appear regardless of  .  */
}
