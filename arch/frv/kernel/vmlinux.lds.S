/* ld script to make FRV Linux kernel
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>;
 */
OUTPUT_FORMAT("elf32-frv", "elf32-frv", "elf32-frv")
OUTPUT_ARCH(frv)
ENTRY(_start)

#include <asm-generic/vmlinux.lds.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/thread_info.h>

jiffies = jiffies_64 + 4;

__page_offset = 0xc0000000;		/* start of area covered by struct pages */
__kernel_image_start = __page_offset;	/* address at which kernel image resides */

SECTIONS
{
  . = __kernel_image_start;

  /* discardable initialisation code and data */
  . = ALIGN(PAGE_SIZE);			/* Init code and data */
  __init_begin = .;

  _sinittext = .;
  .init.text : {
	*(.text.head)
#ifndef CONFIG_DEBUG_INFO
	*(.init.text)
	*(.exit.text)
	*(.exit.data)
	*(.exitcall.exit)
#endif
  }
  _einittext = .;
  .init.data : { *(.init.data) }

  . = ALIGN(8);
  __setup_start = .;
  .setup.init : { KEEP(*(.init.setup)) }
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

  PERCPU(4096)

#ifdef CONFIG_BLK_DEV_INITRD
  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
#endif

  . = ALIGN(THREAD_SIZE);
  __init_end = .;

  /* put sections together that have massive alignment issues */
  . = ALIGN(THREAD_SIZE);
  .data.init_task : {
	  /* init task record & stack */
	  *(.data.init_task)
  }

  . = ALIGN(4096);
  .data.page_aligned : { *(.data.idt) }

  . = ALIGN(L1_CACHE_BYTES);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  .trap : {
	/* trap table management - read entry-table.S before modifying */
	. = ALIGN(8192);
	__trap_tables = .;
	*(.trap.user)
	*(.trap.kernel)
	. = ALIGN(4096);
	*(.trap.break)
  }

  /* Text and read-only data */
  . = ALIGN(4);
  _text = .;
  _stext = .;
  .text : {
	*(.text.start)
	*(.text.entry)
	*(.text.break)
	*(.text.tlbmiss)
	TEXT_TEXT
	SCHED_TEXT
	LOCK_TEXT
#ifdef CONFIG_DEBUG_INFO
	*(
	.init.text
	.exit.text
	.exitcall.exit
	)
#endif
	*(.fixup)
	*(.gnu.warning)
	*(.exitcall.exit)
	} = 0x9090

  _etext = .;			/* End of text section */

  RODATA

  .rodata : {
	*(.trap.vector)

	/* this clause must not be modified - the ordering and adjacency are imperative */
	__trap_fixup_tables = .;
	*(.trap.fixup.user .trap.fixup.kernel)

	}

  . = ALIGN(8);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { KEEP(*(__ex_table)) }
  __stop___ex_table = .;

  _sdata = .;
  .data : {			/* Data */
	DATA_DATA
	*(.data.*)
	*(.exit.data)
	CONSTRUCTORS
	}

  _edata = .;			/* End of data section */

  /* GP section */
  . = ALIGN(L1_CACHE_BYTES);
  _gp = . + 2048;
  PROVIDE (gp = _gp);

  .sdata : { *(.sdata .sdata.*) }

  /* BSS */
  . = ALIGN(L1_CACHE_BYTES);
  __bss_start = .;

  .sbss		: { *(.sbss .sbss.*) }
  .bss		: { *(.bss .bss.*) }
  .bss.stack	: { *(.bss) }

  __bss_stop = .;
  _end = . ;
  . = ALIGN(PAGE_SIZE);
  __kernel_image_end = .;

  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }

  .debug_line		0 : { *(.debug_line) }
  .debug_info		0 : { *(.debug_info) }
  .debug_abbrev		0 : { *(.debug_abbrev) }
  .debug_aranges	0 : { *(.debug_aranges) }
  .debug_frame		0 : { *(.debug_frame) }
  .debug_pubnames	0 : { *(.debug_pubnames) }
  .debug_str		0 : { *(.debug_str) }
  .debug_ranges		0 : { *(.debug_ranges) }

  .comment 0 : { *(.comment) }
}

__kernel_image_size_no_bss = __bss_start - __kernel_image_start;
