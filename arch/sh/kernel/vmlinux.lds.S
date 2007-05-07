/* $Id: vmlinux.lds.S,v 1.8 2003/05/16 17:18:14 lethal Exp $
 * ld script to make SuperH Linux kernel
 * Written by Niibe Yutaka
 */
#include <asm/thread_info.h>
#include <asm/cache.h>
#include <asm-generic/vmlinux.lds.h>

#ifdef CONFIG_CPU_LITTLE_ENDIAN
OUTPUT_FORMAT("elf32-sh-linux", "elf32-sh-linux", "elf32-sh-linux")
#else
OUTPUT_FORMAT("elf32-shbig-linux", "elf32-shbig-linux", "elf32-shbig-linux")
#endif
OUTPUT_ARCH(sh)
ENTRY(_start)
SECTIONS
{
  . = CONFIG_PAGE_OFFSET + CONFIG_MEMORY_START + CONFIG_ZERO_PAGE_OFFSET;
  _text = .;			/* Text and read-only data */
  text = .;			/* Text and read-only data */
  .empty_zero_page : {
	*(.empty_zero_page)
	} = 0
  .text : {
	*(.text)
	SCHED_TEXT
	LOCK_TEXT
	*(.fixup)
	*(.gnu.warning)
	} = 0x0009

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  _etext = .;			/* End of text section */

  RODATA

  BUG_TABLE

  .data : {			/* Data */
	*(.data)

 	 /* Align the initial ramdisk image (INITRD) on page boundaries. */
 	 . = ALIGN(PAGE_SIZE);
 	 __rd_start = .;
 	 *(.initrd)
 	 . = ALIGN(PAGE_SIZE);
 	 __rd_end = .;

	CONSTRUCTORS
	}

  . = ALIGN(PAGE_SIZE);
  .data.page_aligned : { *(.data.page_aligned) }
  __nosave_begin = .;
  .data_nosave : { *(.data.nosave) }
  . = ALIGN(PAGE_SIZE);
  __nosave_end = .;

  . = ALIGN(PAGE_SIZE);
  __per_cpu_start = .;
  .data.percpu : { *(.data.percpu) }
  __per_cpu_end = .;
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  _edata = .;			/* End of data section */

  . = ALIGN(THREAD_SIZE);		/* init_task */
  .data.init_task : { *(.data.init_task) }

  . = ALIGN(PAGE_SIZE);		/* Init code and data */
  __init_begin = .;
  _sinittext = .;
  .init.text : { *(.init.text) }
  _einittext = .;
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
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
#endif

  __machvec_start = .;
  .init.machvec : { *(.init.machvec) }
  __machvec_end = .;
  . = ALIGN(PAGE_SIZE);
  __init_end = .;

  . = ALIGN(4);
  __bss_start = .;		/* BSS */
  .bss : { *(.bss) }

  . = ALIGN(4);
  _end = . ;

  /* When something in the kernel is NOT compiled as a module, the
   * module cleanup code and data are put into these segments.  Both
   * can then be thrown away, as cleanup code is never called unless
   * it's a module.
   */
  /DISCARD/ : {
	*(.exitcall.exit)
	}

  STABS_DEBUG

  DWARF_DEBUG
}
