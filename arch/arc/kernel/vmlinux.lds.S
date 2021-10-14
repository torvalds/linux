/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/thread_info.h>

OUTPUT_ARCH(arc)
ENTRY(res_service)

#ifdef CONFIG_CPU_BIG_ENDIAN
jiffies = jiffies_64 + 4;
#else
jiffies = jiffies_64;
#endif

SECTIONS
{
	/*
	 * ICCM starts at 0x8000_0000. So if kernel is relocated to some other
	 * address, make sure peripheral at 0x8z doesn't clash with ICCM
	 * Essentially vector is also in ICCM.
	 */

	. = CONFIG_LINUX_LINK_BASE;

	_int_vec_base_lds = .;
	.vector : {
		*(.vector)
		. = ALIGN(PAGE_SIZE);
	}

#ifdef CONFIG_ARC_HAS_ICCM
	.text.arcfp : {
		*(.text.arcfp)
		. = ALIGN(CONFIG_ARC_ICCM_SZ * 1024);
	}
#endif

	/*
	 * The reason for having a seperate subsection .init.ramfs is to
	 * prevent objump from including it in kernel dumps
	 *
	 * Reason for having .init.ramfs above .init is to make sure that the
	 * binary blob is tucked away to one side, reducing the displacement
	 * between .init.text and .text, avoiding any possible relocation
	 * errors because of calls from .init.text to .text
	 * Yes such calls do exist. e.g.
	 *	decompress_inflate.c:gunzip( ) -> zlib_inflate_workspace( )
	 */

	__init_begin = .;

	.init.ramfs : { INIT_RAM_FS }

	. = ALIGN(PAGE_SIZE);
	_stext = .;

	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(L1_CACHE_BYTES)

	/* INIT_DATA_SECTION open-coded: special INIT_RAM_FS handling */
	.init.data : {
		INIT_DATA
		INIT_SETUP(L1_CACHE_BYTES)
		INIT_CALLS
		CON_INITCALL
	}

	.init.arch.info : {
		__arch_info_begin = .;
		*(.arch.info.init)
		__arch_info_end = .;
	}

	PERCPU_SECTION(L1_CACHE_BYTES)

	. = ALIGN(PAGE_SIZE);
	__init_end = .;

	.text : {
		_text = .;
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.fixup)
		*(.gnu.warning)
	}
	EXCEPTION_TABLE(L1_CACHE_BYTES)
	_etext = .;

	_sdata = .;
	RO_DATA(PAGE_SIZE)

	/*
	 * 1. this is .data essentially
	 * 2. THREAD_SIZE for init.task, must be kernel-stk sz aligned
	 */
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)

	_edata = .;

	BSS_SECTION(4, 4, 4)

#ifdef CONFIG_ARC_DW2_UNWIND
	. = ALIGN(PAGE_SIZE);
	.eh_frame  : {
		__start_unwind = .;
		*(.eh_frame)
		__end_unwind = .;
	}
#else
	/DISCARD/ : {	*(.eh_frame) }
#endif

	. = ALIGN(PAGE_SIZE);
	_end = . ;

	STABS_DEBUG
	ELF_DETAILS
	DISCARDS

	.arcextmap 0 : {
		*(.gnu.linkonce.arcextmap.*)
		*(.arcextmap.*)
	}

#ifndef CONFIG_DEBUG_INFO
	/DISCARD/ : { *(.debug_frame) }
	/DISCARD/ : { *(.debug_aranges) }
	/DISCARD/ : { *(.debug_pubnames) }
	/DISCARD/ : { *(.debug_info) }
	/DISCARD/ : { *(.debug_abbrev) }
	/DISCARD/ : { *(.debug_line) }
	/DISCARD/ : { *(.debug_str) }
	/DISCARD/ : { *(.debug_loc) }
	/DISCARD/ : { *(.debug_macinfo) }
	/DISCARD/ : { *(.debug_ranges) }
#endif

#ifdef CONFIG_ARC_HAS_DCCM
	. = CONFIG_ARC_DCCM_BASE;
	__arc_dccm_base = .;
	.data.arcfp : {
		*(.data.arcfp)
	}
	. = ALIGN(CONFIG_ARC_DCCM_SZ * 1024);
#endif
}
