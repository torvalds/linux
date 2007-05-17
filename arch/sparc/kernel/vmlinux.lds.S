/* ld script to make SparcLinux kernel */

#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf32-sparc", "elf32-sparc", "elf32-sparc")
OUTPUT_ARCH(sparc)
ENTRY(_start)
jiffies = jiffies_64 + 4;
SECTIONS
{
  . = 0x10000 + SIZEOF_HEADERS;
  .text 0xf0004000 :
  {
    _text = .;
    TEXT_TEXT
    SCHED_TEXT
    LOCK_TEXT
    *(.gnu.warning)
  } =0
  _etext = .;
  PROVIDE (etext = .);
  RODATA
  .data    :
  {
    DATA_DATA
    CONSTRUCTORS
  }
  .data1   : { *(.data1) }
  _edata  =  .;
  PROVIDE (edata = .);
  __start___fixup = .;
  .fixup   : { *(.fixup) }
  __stop___fixup = .;
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  . = ALIGN(4096);
  __init_begin = .;
  _sinittext = .;
  .init.text : { 
	*(.init.text)
  }
  _einittext = .;
  __init_text_end = .;
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
  SECURITY_INIT

#ifdef CONFIG_BLK_DEV_INITRD
  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
#endif

  . = ALIGN(4096);
  __per_cpu_start = .;
  .data.percpu  : { *(.data.percpu) }
  __per_cpu_end = .;
  . = ALIGN(4096);
  __init_end = .;
  . = ALIGN(32);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  __bss_start = .;
  .sbss      : { *(.sbss) *(.scommon) }
  .bss       :
  {
   *(.dynbss)
   *(.bss)
   *(COMMON)
  }
  _end = . ;
  PROVIDE (end = .);
  /DISCARD/ : { *(.exit.text) *(.exit.data) *(.exitcall.exit) }

  STABS_DEBUG

  DWARF_DEBUG
}
