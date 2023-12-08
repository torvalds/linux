/* SPDX-License-Identifier: GPL-2.0 */
/* ld script to make ARM Linux kernel
 * taken from the i386 version by Russell King
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

/* No __ro_after_init data in the .rodata section - which will always be ro */
#define RO_AFTER_INIT_DATA

#include <linux/sizes.h>

#include <asm/vmlinux.lds.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/memory.h>
#include <asm/mpu.h>
#include <asm/page.h>

OUTPUT_ARCH(arm)
ENTRY(stext)

#ifndef __ARMEB__
jiffies = jiffies_64;
#else
jiffies = jiffies_64 + 4;
#endif

SECTIONS
{
	/*
	 * XXX: The linker does not define how output sections are
	 * assigned to input sections when there are multiple statements
	 * matching the same input section name.  There is no documented
	 * order of matching.
	 *
	 * unwind exit sections must be discarded before the rest of the
	 * unwind sections get included.
	 */
	/DISCARD/ : {
		ARM_DISCARD
		*(.alt.smp.init)
		*(.pv_table)
#ifndef CONFIG_ARM_UNWIND
		*(.ARM.exidx) *(.ARM.exidx.*)
		*(.ARM.extab) *(.ARM.extab.*)
#endif
	}

	. = XIP_VIRT_ADDR(CONFIG_XIP_PHYS_ADDR);
	_xiprom = .;			/* XIP ROM area to be mapped */

	.head.text : {
		_text = .;
		HEAD_TEXT
	}

	.text : {			/* Real text segment		*/
		_stext = .;		/* Text and read-only data	*/
		ARM_TEXT
	}

	RO_DATA(PAGE_SIZE)

	. = ALIGN(4);
	__ex_table : AT(ADDR(__ex_table) - LOAD_OFFSET) {
		__start___ex_table = .;
		ARM_MMU_KEEP(*(__ex_table))
		__stop___ex_table = .;
	}

#ifdef CONFIG_ARM_UNWIND
	ARM_UNWIND_SECTIONS
#endif

	_etext = .;			/* End of text and rodata section */

	ARM_VECTORS
	INIT_TEXT_SECTION(8)
	.exit.text : {
		ARM_EXIT_KEEP(EXIT_TEXT)
	}
	.init.proc.info : {
		ARM_CPU_DISCARD(PROC_INFO)
	}
	.init.arch.info : {
		__arch_info_begin = .;
		*(.arch.info.init)
		__arch_info_end = .;
	}
	.init.tagtable : {
		__tagtable_begin = .;
		*(.taglist.init)
		__tagtable_end = .;
	}
	.init.rodata : {
		INIT_SETUP(16)
		INIT_CALLS
		CON_INITCALL
		INIT_RAM_FS
	}

#ifdef CONFIG_ARM_MPU
	. = ALIGN(SZ_128K);
#endif
	_exiprom = .;			/* End of XIP ROM area */

/*
 * From this point, stuff is considered writable and will be copied to RAM
 */
	__data_loc = ALIGN(4);		/* location in file */
	. = PAGE_OFFSET + TEXT_OFFSET;	/* location in memory */
#undef LOAD_OFFSET
#define LOAD_OFFSET (PAGE_OFFSET + TEXT_OFFSET - __data_loc)

	. = ALIGN(THREAD_SIZE);
	_sdata = .;
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	.data.ro_after_init : AT(ADDR(.data.ro_after_init) - LOAD_OFFSET) {
		*(.data..ro_after_init)
	}
	_edata = .;

	. = ALIGN(PAGE_SIZE);
	__init_begin = .;
	.init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {
		INIT_DATA
	}
	.exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) {
		ARM_EXIT_KEEP(EXIT_DATA)
	}
#ifdef CONFIG_SMP
	PERCPU_SECTION(L1_CACHE_BYTES)
#endif

#ifdef CONFIG_HAVE_TCM
	ARM_TCM
#endif

	/*
	 * End of copied data. We need a dummy section to get its LMA.
	 * Also located before final ALIGN() as trailing padding is not stored
	 * in the resulting binary file and useless to copy.
	 */
	.data.endmark : AT(ADDR(.data.endmark) - LOAD_OFFSET) { }
	_edata_loc = LOADADDR(.data.endmark);

	. = ALIGN(PAGE_SIZE);
	__init_end = .;

	BSS_SECTION(0, 0, 8)
#ifdef CONFIG_ARM_MPU
	. = ALIGN(PMSAv8_MINALIGN);
#endif
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	ARM_DETAILS

	ARM_ASSERTS
}

/*
 * These must never be empty
 * If you have to comment these two assert statements out, your
 * binutils is too old (for other reasons as well)
 */
ASSERT((__proc_info_end - __proc_info_begin), "missing CPU support")
#ifndef CONFIG_COMPILE_TEST
ASSERT((__arch_info_end - __arch_info_begin), "no machine record defined")
#endif

#ifdef CONFIG_XIP_DEFLATED_DATA
/*
 * The .bss is used as a stack area for __inflate_kernel_data() whose stack
 * frame is 9568 bytes. Make sure it has extra room left.
 */
ASSERT((_end - __bss_start) >= 12288, ".bss too small for CONFIG_XIP_DEFLATED_DATA")
#endif

#if defined(CONFIG_ARM_MPU) && !defined(CONFIG_COMPILE_TEST)
/*
 * Due to PMSAv7 restriction on base address and size we have to
 * enforce minimal alignment restrictions. It was seen that weaker
 * alignment restriction on _xiprom will likely force XIP address
 * space spawns multiple MPU regions thus it is likely we run in
 * situation when we are reprogramming MPU region we run on with
 * something which doesn't cover reprogramming code itself, so as soon
 * as we update MPU settings we'd immediately try to execute straight
 * from background region which is XN.
 * It seem that alignment in 1M should suit most users.
 * _exiprom is aligned as 1/8 of 1M so can be covered by subregion
 * disable
 */
ASSERT(!(_xiprom & (SZ_1M - 1)), "XIP start address may cause MPU programming issues")
ASSERT(!(_exiprom & (SZ_128K - 1)), "XIP end address may cause MPU programming issues")
#endif
