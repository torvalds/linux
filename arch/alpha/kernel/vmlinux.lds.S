#include <linux/config.h>
#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-alpha")
OUTPUT_ARCH(alpha)
ENTRY(__start)
PHDRS { kernel PT_LOAD ; }
jiffies = jiffies_64;
SECTIONS
{
#ifdef CONFIG_ALPHA_LEGACY_START_ADDRESS
  . = 0xfffffc0000310000;
#else
  . = 0xfffffc0001010000;
#endif

  _text = .;					/* Text and read-only data */
  .text : { 
	*(.text) 
	SCHED_TEXT
	LOCK_TEXT
	*(.fixup)
	*(.gnu.warning)
  } :kernel
  _etext = .;					/* End of text section */

  . = ALIGN(16);
  __start___ex_table = .;			/* Exception table */
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  /* Will be freed after init */
  . = ALIGN(8192);				/* Init code and data */
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

  . = ALIGN(8);
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

  . = ALIGN(8192);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;

  . = ALIGN(8);
  .con_initcall.init : {
	__con_initcall_start = .;
	*(.con_initcall.init)
	__con_initcall_end = .;
  }

  . = ALIGN(8);
  SECURITY_INIT

  . = ALIGN(64);
  __per_cpu_start = .;
  .data.percpu : { *(.data.percpu) }
  __per_cpu_end = .;

  . = ALIGN(2*8192);
  __init_end = .;
  /* Freed after init ends here */

  /* Note 2 page alignment above.  */
  .data.init_thread : { *(.data.init_thread) }

  . = ALIGN(8192);
  .data.page_aligned : { *(.data.page_aligned) }

  . = ALIGN(64);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  _data = .;
  .data : {					/* Data */
	*(.data)
	CONSTRUCTORS
  }

  .got : { *(.got) }
  .sdata : { *(.sdata) }

  _edata = .;					/* End of data section */

  __bss_start = .;
  .sbss : { *(.sbss) *(.scommon) }
  .bss : { *(.bss) *(COMMON) }
  __bss_stop = .;

  _end = .;

  /* Sections to be discarded */
  /DISCARD/ : { *(.exit.text) *(.exit.data) *(.exitcall.exit) }

  .mdebug 0 : { *(.mdebug) }
  .note 0 : { *(.note) }
  .comment 0 : { *(.comment) }

  /* Stabs debugging sections */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
}
