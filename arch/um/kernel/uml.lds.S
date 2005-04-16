#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT(ELF_FORMAT)
OUTPUT_ARCH(ELF_ARCH)
ENTRY(_start)
jiffies = jiffies_64;

SECTIONS
{
  /*This must contain the right address - not quite the default ELF one.*/
  PROVIDE (__executable_start = START);
  . = START + SIZEOF_HEADERS;

  /* Used in arch/um/kernel/mem.c. Any memory between START and __binary_start
   * is remapped.*/
  __binary_start = .;
#ifdef MODE_TT
  .thread_private : {
    __start_thread_private = .;
    errno = .;
    . += 4;
    arch/um/kernel/tt/unmap_fin.o (.data)
    __end_thread_private = .;
  }
  . = ALIGN(4096);
  .remap : { arch/um/kernel/tt/unmap_fin.o (.text) }

  /* We want it only if we are in MODE_TT. In both cases, however, when MODE_TT
   * is off the resulting binary segfaults.*/

  . = ALIGN(4096);		/* Init code and data */
#endif

  _stext = .;
  __init_begin = .;
  .init.text : {
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
  . = ALIGN(4096);
  .text      :
  {
    *(.text)
    SCHED_TEXT
    LOCK_TEXT
    *(.fixup)
    /* .gnu.warning sections are handled specially by elf32.em.  */
    *(.gnu.warning)
    *(.gnu.linkonce.t*)
  }

  #include "asm/common.lds.S"

  init.data : { *(init.data) }
  .data    :
  {
    . = ALIGN(KERNEL_STACK_SIZE);		/* init_task */
    *(.data.init_task)
    *(.data)
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
  /* We want the small data sections together, so single-instruction offsets
     can access them all, and initialized data all before uninitialized, so
     we can shorten the on-disk segment size.  */
  .sdata     : { *(.sdata) }
  _edata  =  .;
  PROVIDE (edata = .);
  . = ALIGN(0x1000);
  .sbss      :
  {
   __bss_start = .;
   PROVIDE(_bss_start = .);
   *(.sbss)
   *(.scommon)
  }
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
}
