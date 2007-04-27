/* ld script to make s390 Linux kernel
 * Written by Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <asm-generic/vmlinux.lds.h>

#ifndef CONFIG_64BIT
OUTPUT_FORMAT("elf32-s390", "elf32-s390", "elf32-s390")
OUTPUT_ARCH(s390)
ENTRY(_start)
jiffies = jiffies_64 + 4;
#else
OUTPUT_FORMAT("elf64-s390", "elf64-s390", "elf64-s390")
OUTPUT_ARCH(s390:64-bit)
ENTRY(_start)
jiffies = jiffies_64;
#endif

SECTIONS
{
  . = 0x00000000;
  _text = .;			/* Text and read-only data */
  .text : {
	*(.text)
	SCHED_TEXT
	LOCK_TEXT
	KPROBES_TEXT
	*(.fixup)
	*(.gnu.warning)
	} = 0x0700

  _etext = .;			/* End of text section */

  RODATA

#ifdef CONFIG_SHARED_KERNEL
  . = ALIGN(1048576);		/* VM shared segments are 1MB aligned */
#endif

  . = ALIGN(4096);
  _eshared = .;			/* End of shareable data */

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  BUG_TABLE

  .data : {			/* Data */
	*(.data)
	CONSTRUCTORS
	}

  . = ALIGN(4096);
  __nosave_begin = .;
  .data_nosave : { *(.data.nosave) }
  . = ALIGN(4096);
  __nosave_end = .;

  . = ALIGN(4096);
  .data.page_aligned : { *(.data.idt) }

  . = ALIGN(256);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  . = ALIGN(256);
  .data.read_mostly : { *(.data.read_mostly) }
  _edata = .;			/* End of data section */

  . = ALIGN(8192);		/* init_task */
  .data.init_task : { *(.data.init_task) }

  /* will be freed after init */
  . = ALIGN(4096);		/* Init code and data */
  __init_begin = .;
  .init.text : { 
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
  /*
   * .exit.text is discarded at runtime, not link time,
   * to deal with references from __bug_table
   */
  .exit.text :	 { *(.exit.text) }

  .init.data : { *(.init.data) }
  . = ALIGN(256);
  __setup_start = .;
  .init.setup : { *(.init.setup) }
  __setup_end = .;
  __initcall_start = .;
  .initcall.init : {
	INITCALLS
  }
  __initcall_end = .;
  __con_initcall_start = .;
  .con_initcall.init : { *(.con_initcall.init) }
  __con_initcall_end = .;
  SECURITY_INIT

#ifdef CONFIG_BLK_DEV_INITRD
  . = ALIGN(256);
  __initramfs_start = .;
  .init.ramfs : { *(.init.initramfs) }
  . = ALIGN(2);
  __initramfs_end = .;
#endif
  . = ALIGN(256);
  __per_cpu_start = .;
  .data.percpu  : { *(.data.percpu) }
  __per_cpu_end = .;
  . = ALIGN(4096);
  __init_end = .;
  /* freed after init ends here */

  __bss_start = .;		/* BSS */
  .bss : { *(.bss) }
  . = ALIGN(2);
  __bss_stop = .;

  _end = . ;

  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exit.data) *(.exitcall.exit)
	}

  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
}
