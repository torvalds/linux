/* ld script to make UltraLinux kernel */

#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-sparc", "elf64-sparc", "elf64-sparc")
OUTPUT_ARCH(sparc:v9a)
ENTRY(_start)

jiffies = jiffies_64;
SECTIONS
{
  swapper_low_pmd_dir = 0x0000000000402000;
  . = 0x4000;
  .text 0x0000000000404000 :
  {
    *(.text)
    SCHED_TEXT
    LOCK_TEXT
    KPROBES_TEXT
    *(.gnu.warning)
  } =0
  _etext = .;
  PROVIDE (etext = .);

  RODATA

  .data    :
  {
    *(.data)
    CONSTRUCTORS
  }
  .data1   : { *(.data1) }
  . = ALIGN(64);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }
  . = ALIGN(64);
  .data.read_mostly : { *(.data.read_mostly) }
  _edata  =  .;
  PROVIDE (edata = .);
  .fixup   : { *(.fixup) }

  . = ALIGN(16);
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  . = ALIGN(8192);
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
  .con_initcall.init : { *(.con_initcall.init) }
  __con_initcall_end = .;
  SECURITY_INIT
  . = ALIGN(8192); 
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
  . = ALIGN(8192);
  __per_cpu_start = .;
  .data.percpu  : { *(.data.percpu) }
  __per_cpu_end = .;
  . = ALIGN(8192);
  __init_end = .;
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
  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
  .debug          0 : { *(.debug) }
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  .line           0 : { *(.line) }
  /DISCARD/ : { *(.exit.text) *(.exit.data) *(.exitcall.exit) }
}
