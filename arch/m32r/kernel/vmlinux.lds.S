/* ld script to make M32R Linux kernel
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/addrspace.h>
#include <asm/page.h>

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
	TEXT_TEXT
	SCHED_TEXT
	LOCK_TEXT
	*(.fixup)
	*(.gnu.warning)
	} = 0x9090
#ifdef CONFIG_SMP
  . = ALIGN(65536);
  .eit_vector4 : { *(.eit_vector4) }
#endif
  _etext = .;			/* End of text section */

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  /* writeable */
  .data : {			/* Data */
	*(.spu)
	*(.spi)
	DATA_DATA
	CONSTRUCTORS
	}

  . = ALIGN(4096);
  __nosave_begin = .;
  .data_nosave : { *(.data.nosave) }
  . = ALIGN(4096);
  __nosave_end = .;

  . = ALIGN(4096);
  .data.page_aligned : { *(.data.idt) }

  . = ALIGN(32);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  _edata = .;			/* End of data section */

  . = ALIGN(8192);		/* init_task */
  .data.init_task : { *(.data.init_task) }

  /* will be freed after init */
  . = ALIGN(4096);		/* Init code and data */
  __init_begin = .;
  .init.text : {
	_sinittext = .;
	INIT_TEXT
	_einittext = .;
  }
  .init.data : { INIT_DATA }
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
  . = ALIGN(4);
  __alt_instructions = .;
  .altinstructions : { *(.altinstructions) }
  __alt_instructions_end = .;
  .altinstr_replacement : { *(.altinstr_replacement) }
  /* .exit.text is discard at runtime, not link time, to deal with references
     from .altinstructions and .eh_frame */
  .exit.text : { EXIT_TEXT }
  .exit.data : { EXIT_DATA }

#ifdef CONFIG_BLK_DEV_INITRD
  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
#endif

  PERCPU(4096)
  . = ALIGN(4096);
  __init_end = .;
  /* freed after init ends here */

  __bss_start = .;		/* BSS */
  .bss : { *(.bss) }
  . = ALIGN(4);
  __bss_stop = .;

  _end = . ;

  /* Sections to be discarded */
  /DISCARD/ : {
	EXIT_TEXT
	EXIT_DATA
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
