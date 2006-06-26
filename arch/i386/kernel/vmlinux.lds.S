/* ld script to make i386 Linux kernel
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>;
 */

#define LOAD_OFFSET __PAGE_OFFSET

#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/cache.h>

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(phys_startup_32)
jiffies = jiffies_64;
SECTIONS
{
  . = __KERNEL_START;
  phys_startup_32 = startup_32 - LOAD_OFFSET;
  /* read-only */
  _text = .;			/* Text and read-only data */
  .text : AT(ADDR(.text) - LOAD_OFFSET) {
	*(.text)
	SCHED_TEXT
	LOCK_TEXT
	KPROBES_TEXT
	*(.fixup)
	*(.gnu.warning)
	} = 0x9090

  _etext = .;			/* End of text section */

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : AT(ADDR(__ex_table) - LOAD_OFFSET) { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  . = ALIGN(4);
  __tracedata_start = .;
  .tracedata : AT(ADDR(.tracedata) - LOAD_OFFSET) {
	*(.tracedata)
  }
  __tracedata_end = .;

  /* writeable */
  .data : AT(ADDR(.data) - LOAD_OFFSET) {	/* Data */
	*(.data)
	CONSTRUCTORS
	}

  . = ALIGN(4096);
  __nosave_begin = .;
  .data_nosave : AT(ADDR(.data_nosave) - LOAD_OFFSET) { *(.data.nosave) }
  . = ALIGN(4096);
  __nosave_end = .;

  . = ALIGN(4096);
  .data.page_aligned : AT(ADDR(.data.page_aligned) - LOAD_OFFSET) {
	*(.data.idt)
  }

  . = ALIGN(32);
  .data.cacheline_aligned : AT(ADDR(.data.cacheline_aligned) - LOAD_OFFSET) {
	*(.data.cacheline_aligned)
  }

  /* rarely changed data like cpu maps */
  . = ALIGN(32);
  .data.read_mostly : AT(ADDR(.data.read_mostly) - LOAD_OFFSET) { *(.data.read_mostly) }
  _edata = .;			/* End of data section */

  . = ALIGN(THREAD_SIZE);	/* init_task */
  .data.init_task : AT(ADDR(.data.init_task) - LOAD_OFFSET) {
	*(.data.init_task)
  }

  /* might get freed after init */
  . = ALIGN(4096);
  __smp_alt_begin = .;
  __smp_alt_instructions = .;
  .smp_altinstructions : AT(ADDR(.smp_altinstructions) - LOAD_OFFSET) {
	*(.smp_altinstructions)
  }
  __smp_alt_instructions_end = .;
  . = ALIGN(4);
  __smp_locks = .;
  .smp_locks : AT(ADDR(.smp_locks) - LOAD_OFFSET) {
	*(.smp_locks)
  }
  __smp_locks_end = .;
  .smp_altinstr_replacement : AT(ADDR(.smp_altinstr_replacement) - LOAD_OFFSET) {
	*(.smp_altinstr_replacement)
  }
  . = ALIGN(4096);
  __smp_alt_end = .;

  /* will be freed after init */
  . = ALIGN(4096);		/* Init code and data */
  __init_begin = .;
  .init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
  .init.data : AT(ADDR(.init.data) - LOAD_OFFSET) { *(.init.data) }
  . = ALIGN(16);
  __setup_start = .;
  .init.setup : AT(ADDR(.init.setup) - LOAD_OFFSET) { *(.init.setup) }
  __setup_end = .;
  __initcall_start = .;
  .initcall.init : AT(ADDR(.initcall.init) - LOAD_OFFSET) {
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
  .con_initcall.init : AT(ADDR(.con_initcall.init) - LOAD_OFFSET) {
	*(.con_initcall.init)
  }
  __con_initcall_end = .;
  SECURITY_INIT
  . = ALIGN(4);
  __alt_instructions = .;
  .altinstructions : AT(ADDR(.altinstructions) - LOAD_OFFSET) {
	*(.altinstructions)
  }
  __alt_instructions_end = .; 
  .altinstr_replacement : AT(ADDR(.altinstr_replacement) - LOAD_OFFSET) {
	*(.altinstr_replacement)
  }
  /* .exit.text is discard at runtime, not link time, to deal with references
     from .altinstructions and .eh_frame */
  .exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) { *(.exit.text) }
  .exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) { *(.exit.data) }
  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : AT(ADDR(.init.ramfs) - LOAD_OFFSET) { *(.init.ramfs) }
  __initramfs_end = .;
  . = ALIGN(L1_CACHE_BYTES);
  __per_cpu_start = .;
  .data.percpu  : AT(ADDR(.data.percpu) - LOAD_OFFSET) { *(.data.percpu) }
  __per_cpu_end = .;
  . = ALIGN(4096);
  __init_end = .;
  /* freed after init ends here */
	
  __bss_start = .;		/* BSS */
  .bss.page_aligned : AT(ADDR(.bss.page_aligned) - LOAD_OFFSET) {
	*(.bss.page_aligned)
  }
  .bss : AT(ADDR(.bss) - LOAD_OFFSET) {
	*(.bss)
  }
  . = ALIGN(4);
  __bss_stop = .; 

  _end = . ;

  /* This is where the kernel creates the early boot page tables */
  . = ALIGN(4096);
  pg0 = .;

  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exitcall.exit)
	}

  STABS_DEBUG

  DWARF_DEBUG
}
