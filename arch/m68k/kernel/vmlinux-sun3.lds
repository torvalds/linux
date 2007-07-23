/* ld script to make m68k Linux kernel */

#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf32-m68k", "elf32-m68k", "elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)
jiffies = jiffies_64 + 4;
SECTIONS
{
  . = 0xE002000;
  _text = .;			/* Text and read-only data */
  .text : {
	*(.text.head)
	TEXT_TEXT
	SCHED_TEXT
	LOCK_TEXT
	*(.fixup)
	*(.gnu.warning)
	} :text = 0x4e75
	RODATA

  _etext = .;			/* End of text section */

  .data : {			/* Data */
	DATA_DATA
	CONSTRUCTORS
	. = ALIGN(16);		/* Exception table */
	__start___ex_table = .;
	*(__ex_table)
	__stop___ex_table = .;
	} :data
  /* End of data goes *here* so that freeing init code works properly. */
  _edata = .;

  /* will be freed after init */
  . = ALIGN(8192);	/* Init code and data */
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
	.data.init.task : { *(.data.init_task) }


  .bss : { *(.bss) }		/* BSS */

  _end = . ;

  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exit.text)
	*(.exit.data)
	*(.exitcall.exit)
	}

  .crap : {
	/* Stabs debugging sections.  */
	*(.stab)
	*(.stabstr)
	*(.stab.excl)
	*(.stab.exclstr)
	*(.stab.index)
	*(.stab.indexstr)
	*(.comment)
	*(.note)
  }

}
