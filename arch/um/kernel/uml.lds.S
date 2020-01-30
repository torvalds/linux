/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/vmlinux.lds.h>
#include <asm/page.h>

OUTPUT_FORMAT(ELF_FORMAT)
OUTPUT_ARCH(ELF_ARCH)
ENTRY(_start)
jiffies = jiffies_64;

SECTIONS
{
  /* This must contain the right address - not quite the default ELF one.*/
  PROVIDE (__executable_start = START);
  /* Static binaries stick stuff here, like the sigreturn trampoline,
   * invisibly to objdump.  So, just make __binary_start equal to the very
   * beginning of the executable, and if there are unmapped pages after this,
   * they are forever unusable.
   */
  __binary_start = START;

  . = START + SIZEOF_HEADERS;

  _text = .;
  INIT_TEXT_SECTION(0)
  . = ALIGN(PAGE_SIZE);

  .text      :
  {
    _stext = .;
    TEXT_TEXT
    SCHED_TEXT
    CPUIDLE_TEXT
    LOCK_TEXT
    IRQENTRY_TEXT
    SOFTIRQENTRY_TEXT
    *(.fixup)
    /* .gnu.warning sections are handled specially by elf32.em.  */
    *(.gnu.warning)
    *(.gnu.linkonce.t*)
  }

  . = ALIGN(PAGE_SIZE);
  .syscall_stub : {
	__syscall_stub_start = .;
	*(.__syscall_stub*)
	__syscall_stub_end = .;
  }

  /*
   * These are needed even in a static link, even if they wind up being empty.
   * Newer glibc needs these __rel{,a}_iplt_{start,end} symbols.
   */
  .rel.plt : {
	*(.rel.plt)
	PROVIDE_HIDDEN(__rel_iplt_start = .);
	*(.rel.iplt)
	PROVIDE_HIDDEN(__rel_iplt_end = .);
  }
  .rela.plt : {
	*(.rela.plt)
	PROVIDE_HIDDEN(__rela_iplt_start = .);
	*(.rela.iplt)
	PROVIDE_HIDDEN(__rela_iplt_end = .);
  }

  #include <asm/common.lds.S>

  __init_begin = .;
  init.data : { INIT_DATA }
  __init_end = .;

  .data    :
  {
    INIT_TASK_DATA(KERNEL_STACK_SIZE)
    . = ALIGN(KERNEL_STACK_SIZE);
    *(.data..init_irqstack)
    DATA_DATA
    *(.gnu.linkonce.d*)
    CONSTRUCTORS
  }
  .data1   : { *(.data1) }
  .ctors         :
  {
    *(.ctors)
  }
  .dtors         :
  {
    *(.dtors)
  }

  .got           : { *(.got.plt) *(.got) }
  .dynamic       : { *(.dynamic) }
  .tdata	  : { *(.tdata .tdata.* .gnu.linkonce.td.*) }
  .tbss		  : { *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon) }
  /* We want the small data sections together, so single-instruction offsets
     can access them all, and initialized data all before uninitialized, so
     we can shorten the on-disk segment size.  */
  .sdata     : { *(.sdata) }
  _edata  =  .;
  PROVIDE (edata = .);
  . = ALIGN(PAGE_SIZE);
  __bss_start = .;
  PROVIDE(_bss_start = .);
  SBSS(0)
  BSS(0)
   __bss_stop = .;
  _end = .;
  PROVIDE (end = .);

  STABS_DEBUG

  DWARF_DEBUG

  DISCARDS
}
