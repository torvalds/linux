/* ld script to make m68k Linux kernel */

#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf32-m68k", "elf32-m68k", "elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)
jiffies = jiffies_64 + 4;
SECTIONS
{
  . = 0x1000;
  _text = .;			/* Text and read-only data */
  .text : {
	TEXT_TEXT
	SCHED_TEXT
	LOCK_TEXT
	*(.fixup)
	*(.gnu.warning)
	} :text = 0x4e75

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  _etext = .;			/* End of text section */

  .data : {			/* Data */
	DATA_DATA
	CONSTRUCTORS
	}

  .bss : { *(.bss) }		/* BSS */

  . = ALIGN(16);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) } :data

  _edata = .;			/* End of data section */

  /* will be freed after init */
  . = ALIGN(4096);		/* Init code and data */
  __init_begin = .;
  .init.text : {
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
  .init.data : { *(.init.data) }
  . = ALIGN(16);
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
  .m68k_fixup : {
	__start_fixup = .;
	*(.m68k_fixup)
	__stop_fixup = .;
  }
  SECURITY_INIT
#ifdef CONFIG_BLK_DEV_INITRD
  . = ALIGN(8192);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
#endif
  . = ALIGN(8192);
  __init_end = .;

  .data.init_task : { *(.data.init_task) }	/* The initial task and kernel stack */

  _end = . ;

  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exit.text)
	*(.exit.data)
	*(.exitcall.exit)
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
