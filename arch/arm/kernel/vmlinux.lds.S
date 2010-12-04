/* ld script to make ARM Linux kernel
 * taken from the i386 version by Russell King
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/memory.h>
#include <asm/page.h>
	
#define PROC_INFO							\
	VMLINUX_SYMBOL(__proc_info_begin) = .;				\
	*(.proc.info.init)						\
	VMLINUX_SYMBOL(__proc_info_end) = .;

#ifdef CONFIG_HOTPLUG_CPU
#define ARM_CPU_DISCARD(x)
#define ARM_CPU_KEEP(x)		x
#else
#define ARM_CPU_DISCARD(x)	x
#define ARM_CPU_KEEP(x)
#endif

OUTPUT_ARCH(arm)
ENTRY(stext)

#ifndef __ARMEB__
jiffies = jiffies_64;
#else
jiffies = jiffies_64 + 4;
#endif

SECTIONS
{
#ifdef CONFIG_XIP_KERNEL
	. = XIP_VIRT_ADDR(CONFIG_XIP_PHYS_ADDR);
#else
	. = PAGE_OFFSET + TEXT_OFFSET;
#endif

	.init : {			/* Init code and data		*/
		_stext = .;
		_sinittext = .;
			HEAD_TEXT
			INIT_TEXT
		_einittext = .;
		ARM_CPU_DISCARD(PROC_INFO)
		__arch_info_begin = .;
			*(.arch.info.init)
		__arch_info_end = .;
		__tagtable_begin = .;
			*(.taglist.init)
		__tagtable_end = .;
#ifdef CONFIG_SMP_ON_UP
		__smpalt_begin = .;
			*(.alt.smp.init)
		__smpalt_end = .;
#endif

		INIT_SETUP(16)

		INIT_CALLS
		CON_INITCALL
		SECURITY_INITCALL
		INIT_RAM_FS

#ifndef CONFIG_XIP_KERNEL
		__init_begin = _stext;
		INIT_DATA
#endif
	}

	PERCPU(PAGE_SIZE)

#ifndef CONFIG_XIP_KERNEL
	. = ALIGN(PAGE_SIZE);
	__init_end = .;
#endif

	/*
	 * unwind exit sections must be discarded before the rest of the
	 * unwind sections get included.
	 */
	/DISCARD/ : {
		*(.ARM.exidx.exit.text)
		*(.ARM.extab.exit.text)
		ARM_CPU_DISCARD(*(.ARM.exidx.cpuexit.text))
		ARM_CPU_DISCARD(*(.ARM.extab.cpuexit.text))
#ifndef CONFIG_HOTPLUG
		*(.ARM.exidx.devexit.text)
		*(.ARM.extab.devexit.text)
#endif
#ifndef CONFIG_MMU
		*(.fixup)
		*(__ex_table)
#endif
	}

	.text : {			/* Real text segment		*/
		_text = .;		/* Text and read-only data	*/
			__exception_text_start = .;
			*(.exception.text)
			__exception_text_end = .;
			TEXT_TEXT
			SCHED_TEXT
			LOCK_TEXT
			KPROBES_TEXT
#ifdef CONFIG_MMU
			*(.fixup)
#endif
			*(.gnu.warning)
			*(.rodata)
			*(.rodata.*)
			*(.glue_7)
			*(.glue_7t)
		. = ALIGN(4);
		*(.got)			/* Global offset table		*/
			ARM_CPU_KEEP(PROC_INFO)
	}

	RO_DATA(PAGE_SIZE)

#ifdef CONFIG_ARM_UNWIND
	/*
	 * Stack unwinding tables
	 */
	. = ALIGN(8);
	.ARM.unwind_idx : {
		__start_unwind_idx = .;
		*(.ARM.exidx*)
		__stop_unwind_idx = .;
	}
	.ARM.unwind_tab : {
		__start_unwind_tab = .;
		*(.ARM.extab*)
		__stop_unwind_tab = .;
	}
#endif

	_etext = .;			/* End of text and rodata section */

#ifdef CONFIG_XIP_KERNEL
	__data_loc = ALIGN(4);		/* location in binary */
	. = PAGE_OFFSET + TEXT_OFFSET;
#else
	. = ALIGN(THREAD_SIZE);
	__data_loc = .;
#endif

	.data : AT(__data_loc) {
		_data = .;		/* address in memory */
		_sdata = .;

		/*
		 * first, the init task union, aligned
		 * to an 8192 byte boundary.
		 */
		INIT_TASK_DATA(THREAD_SIZE)

#ifdef CONFIG_XIP_KERNEL
		. = ALIGN(PAGE_SIZE);
		__init_begin = .;
		INIT_DATA
		. = ALIGN(PAGE_SIZE);
		__init_end = .;
#endif

		NOSAVE_DATA
		CACHELINE_ALIGNED_DATA(32)
		READ_MOSTLY_DATA(32)

		/*
		 * The exception fixup table (might need resorting at runtime)
		 */
		. = ALIGN(32);
		__start___ex_table = .;
#ifdef CONFIG_MMU
		*(__ex_table)
#endif
		__stop___ex_table = .;

		/*
		 * and the usual data section
		 */
		DATA_DATA
		CONSTRUCTORS

		_edata = .;
	}
	_edata_loc = __data_loc + SIZEOF(.data);

#ifdef CONFIG_HAVE_TCM
        /*
	 * We align everything to a page boundary so we can
	 * free it after init has commenced and TCM contents have
	 * been copied to its destination.
	 */
	.tcm_start : {
		. = ALIGN(PAGE_SIZE);
		__tcm_start = .;
		__itcm_start = .;
	}

	/*
	 * Link these to the ITCM RAM
	 * Put VMA to the TCM address and LMA to the common RAM
	 * and we'll upload the contents from RAM to TCM and free
	 * the used RAM after that.
	 */
	.text_itcm ITCM_OFFSET : AT(__itcm_start)
	{
		__sitcm_text = .;
		*(.tcm.text)
		*(.tcm.rodata)
		. = ALIGN(4);
		__eitcm_text = .;
	}

	/*
	 * Reset the dot pointer, this is needed to create the
	 * relative __dtcm_start below (to be used as extern in code).
	 */
	. = ADDR(.tcm_start) + SIZEOF(.tcm_start) + SIZEOF(.text_itcm);

	.dtcm_start : {
		__dtcm_start = .;
	}

	/* TODO: add remainder of ITCM as well, that can be used for data! */
	.data_dtcm DTCM_OFFSET : AT(__dtcm_start)
	{
		. = ALIGN(4);
		__sdtcm_data = .;
		*(.tcm.data)
		. = ALIGN(4);
		__edtcm_data = .;
	}

	/* Reset the dot pointer or the linker gets confused */
	. = ADDR(.dtcm_start) + SIZEOF(.data_dtcm);

	/* End marker for freeing TCM copy in linked object */
	.tcm_end : AT(ADDR(.dtcm_start) + SIZEOF(.data_dtcm)){
		. = ALIGN(PAGE_SIZE);
		__tcm_end = .;
	}
#endif

	BSS_SECTION(0, 0, 0)
	_end = .;

	STABS_DEBUG
	.comment 0 : { *(.comment) }

	/* Default discards */
	DISCARDS

#ifndef CONFIG_SMP_ON_UP
	/DISCARD/ : {
		*(.alt.smp.init)
	}
#endif
}

/*
 * These must never be empty
 * If you have to comment these two assert statements out, your
 * binutils is too old (for other reasons as well)
 */
ASSERT((__proc_info_end - __proc_info_begin), "missing CPU support")
ASSERT((__arch_info_end - __arch_info_begin), "no machine record defined")
