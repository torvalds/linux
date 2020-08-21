/* SPDX-License-Identifier: GPL-2.0 */
/* ld script to make ARM Linux kernel
 * taken from the i386 version by Russell King
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#ifdef CONFIG_XIP_KERNEL
#include "vmlinux-xip.lds.S"
#else

#include <linux/pgtable.h>
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
#ifndef CONFIG_SMP_ON_UP
		*(.alt.smp.init)
#endif
	}

	. = PAGE_OFFSET + TEXT_OFFSET;
	.head.text : {
		_text = .;
		HEAD_TEXT
	}

#ifdef CONFIG_STRICT_KERNEL_RWX
	. = ALIGN(1<<SECTION_SHIFT);
#endif

#ifdef CONFIG_ARM_MPU
	. = ALIGN(PMSAv8_MINALIGN);
#endif
	.text : {			/* Real text segment		*/
		_stext = .;		/* Text and read-only data	*/
		ARM_TEXT
	}

#ifdef CONFIG_DEBUG_ALIGN_RODATA
	. = ALIGN(1<<SECTION_SHIFT);
#endif
	_etext = .;			/* End of text section */

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

#ifdef CONFIG_STRICT_KERNEL_RWX
	. = ALIGN(1<<SECTION_SHIFT);
#else
	. = ALIGN(PAGE_SIZE);
#endif
	__init_begin = .;

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
#ifdef CONFIG_SMP_ON_UP
	.init.smpalt : {
		__smpalt_begin = .;
		*(.alt.smp.init)
		__smpalt_end = .;
	}
#endif
	.init.pv_table : {
		__pv_table_begin = .;
		*(.pv_table)
		__pv_table_end = .;
	}

	INIT_DATA_SECTION(16)

	.exit.data : {
		ARM_EXIT_KEEP(EXIT_DATA)
	}

#ifdef CONFIG_SMP
	PERCPU_SECTION(L1_CACHE_BYTES)
#endif

#ifdef CONFIG_HAVE_TCM
	ARM_TCM
#endif

#ifdef CONFIG_STRICT_KERNEL_RWX
	. = ALIGN(1<<SECTION_SHIFT);
#else
	. = ALIGN(THREAD_SIZE);
#endif
	__init_end = .;

	_sdata = .;
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	BSS_SECTION(0, 0, 0)
#ifdef CONFIG_ARM_MPU
	. = ALIGN(PMSAv8_MINALIGN);
#endif
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	ARM_DETAILS

	ARM_ASSERTS
}

#ifdef CONFIG_STRICT_KERNEL_RWX
/*
 * Without CONFIG_DEBUG_ALIGN_RODATA, __start_rodata_section_aligned will
 * be the first section-aligned location after __start_rodata. Otherwise,
 * it will be equal to __start_rodata.
 */
__start_rodata_section_aligned = ALIGN(__start_rodata, 1 << SECTION_SHIFT);
#endif

/*
 * These must never be empty
 * If you have to comment these two assert statements out, your
 * binutils is too old (for other reasons as well)
 */
ASSERT((__proc_info_end - __proc_info_begin), "missing CPU support")
ASSERT((__arch_info_end - __arch_info_begin), "no machine record defined")

#endif /* CONFIG_XIP_KERNEL */
