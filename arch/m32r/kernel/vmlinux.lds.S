/* ld script to make M32R Linux kernel
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/thread_info.h>

OUTPUT_ARCH(m32r)
#if defined(__LITTLE_ENDIAN__)
       jiffies = jiffies_64;
#else
       jiffies = jiffies_64 + 4;
#endif

kernel_entry = boot - 0x80000000;
ENTRY(kernel_entry)

SECTIONS
{
  . = CONFIG_MEMORY_START + __PAGE_OFFSET;
  eit_vector = .;

  . = . + 0x1000;
  .empty_zero_page : { *(.empty_zero_page) } = 0

  /* read-only */
  _text = .;			/* Text and read-only data */
  .boot : { *(.boot) } = 0
  .text : {
	HEAD_TEXT
	TEXT_TEXT
	SCHED_TEXT
	CPUIDLE_TEXT
	LOCK_TEXT
	*(.fixup)
	*(.gnu.warning)
	} = 0x9090
#ifdef CONFIG_SMP
  . = ALIGN(65536);
  .eit_vector4 : { *(.eit_vector4) }
#endif
  _etext = .;			/* End of text section */

  EXCEPTION_TABLE(16)
  NOTES

  _sdata = .;			/* Start of data section */
  RODATA
  RW_DATA_SECTION(32, PAGE_SIZE, THREAD_SIZE)
  _edata = .;			/* End of data section */

  /* will be freed after init */
  . = ALIGN(PAGE_SIZE);		/* Init code and data */
  __init_begin = .;
  INIT_TEXT_SECTION(PAGE_SIZE)
  INIT_DATA_SECTION(16)
  PERCPU_SECTION(32)
  . = ALIGN(PAGE_SIZE);
  __init_end = .;
  /* freed after init ends here */

  BSS_SECTION(0, 0, 4)

  _end = . ;

  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }

  /* Sections to be discarded */
  DISCARDS
}
