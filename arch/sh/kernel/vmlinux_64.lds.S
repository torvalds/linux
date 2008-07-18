/*
 * ld script to make SH64 Linux kernel
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 * benedict.gaster@superh.com:	 2nd May 2002
 *    Add definition of empty_zero_page to be the first page of kernel image.
 *
 * benedict.gaster@superh.com:	 3rd May 2002
 *    Added support for ramdisk, removing statically linked romfs at the
 *    same time.
 *
 * lethal@linux-sh.org:          9th May 2003
 *    Kill off GLOBAL_NAME() usage and other CDC-isms.
 *
 * lethal@linux-sh.org:         19th May 2003
 *    Remove support for ancient toolchains.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/thread_info.h>

#define LOAD_OFFSET	CONFIG_PAGE_OFFSET
#include <asm-generic/vmlinux.lds.h>

OUTPUT_ARCH(sh:sh5)

#define C_PHYS(x) AT (ADDR(x) - LOAD_OFFSET)

ENTRY(__start)
SECTIONS
{
	. = CONFIG_PAGE_OFFSET + CONFIG_MEMORY_START + PAGE_SIZE;
	_text = .;			/* Text and read-only data */

	.empty_zero_page : C_PHYS(.empty_zero_page) {
		*(.empty_zero_page)
	} = 0

	.text : C_PHYS(.text) {
		*(.text.head)
		TEXT_TEXT
		*(.text64)
		*(.text..SHmedia32)
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.fixup)
		*(.gnu.warning)
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	} = 0x6ff0fff0
#else
	} = 0xf0fff06f
#endif

	/* We likely want __ex_table to be Cache Line aligned */
	. = ALIGN(L1_CACHE_BYTES);		/* Exception table */
	__start___ex_table = .;
	__ex_table : C_PHYS(__ex_table) { *(__ex_table) }
	__stop___ex_table = .;

	_etext = .;			/* End of text section */

	NOTES 
	RO_DATA(PAGE_SIZE)

	. = ALIGN(THREAD_SIZE);
	.data : C_PHYS(.data) {			/* Data */
		*(.data.init_task)

		. = ALIGN(L1_CACHE_BYTES);
		*(.data.cacheline_aligned)

		. = ALIGN(L1_CACHE_BYTES);
		*(.data.read_mostly)

		. = ALIGN(PAGE_SIZE);
		*(.data.page_aligned)

		__nosave_begin = .;
		*(.data.nosave)
		. = ALIGN(PAGE_SIZE);
		__nosave_end = .;

		DATA_DATA
		CONSTRUCTORS
	}

	_edata = .;			/* End of data section */

	. = ALIGN(PAGE_SIZE);		/* Init code and data */
	__init_begin = .;
	_sinittext = .;
	.init.text : C_PHYS(.init.text) { INIT_TEXT }
	_einittext = .;
	.init.data : C_PHYS(.init.data) { INIT_DATA }
	. = ALIGN(L1_CACHE_BYTES);	/* Better if Cache Line aligned */
	__setup_start = .;
	.init.setup : C_PHYS(.init.setup) { *(.init.setup) }
	__setup_end = .;
	__initcall_start = .;
	.initcall.init : C_PHYS(.initcall.init) {
		INITCALLS
	}
	__initcall_end = .;
	__con_initcall_start = .;
	.con_initcall.init : C_PHYS(.con_initcall.init) {
		*(.con_initcall.init)
	}
	__con_initcall_end = .;

	SECURITY_INIT

#ifdef CONFIG_BLK_DEV_INITRD
	. = ALIGN(PAGE_SIZE);
	__initramfs_start = .;
	.init.ramfs : C_PHYS(.init.ramfs) { *(.init.ramfs) }
	__initramfs_end = .;
#endif

	. = ALIGN(8);
	__machvec_start = .;
	.machvec.init : C_PHYS(.machvec.init) { *(.machvec.init) }
	__machvec_end = .;

	PERCPU(PAGE_SIZE)

	/*
	 * .exit.text is discarded at runtime, not link time, to deal with
	 * references from __bug_table
	 */
	.exit.text : C_PHYS(.exit.text) { EXIT_TEXT }
	.exit.data : C_PHYS(.exit.data) { EXIT_DATA }

	. = ALIGN(PAGE_SIZE);
	.bss : C_PHYS(.bss) {
		__init_end = .;
		__bss_start = .;		/* BSS */
		*(.bss.page_aligned)
		*(.bss)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;			/* uClinux MTD sucks */
		_end = . ;
	}

	/*
	 * When something in the kernel is NOT compiled as a module, the
	 * module cleanup code and data are put into these segments. Both
	 * can then be thrown away, as cleanup code is never called unless
	 * it's a module.
	 */
	/DISCARD/ : {
		*(.exitcall.exit)
	}

	STABS_DEBUG
	DWARF_DEBUG
}
