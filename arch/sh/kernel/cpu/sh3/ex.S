/*
 *  arch/sh/kernel/cpu/sh3/ex.S
 *
 *  The SH-3 and SH-4 exception vector table.

 *  Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 *  Copyright (C) 2003 - 2008  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/linkage.h>

#if !defined(CONFIG_MMU)
#define	tlb_miss_load			exception_error
#define tlb_miss_store			exception_error
#define initial_page_write		exception_error
#define tlb_protection_violation_load	exception_error
#define tlb_protection_violation_store	exception_error
#define address_error_load		exception_error
#define address_error_store		exception_error
#endif

#if !defined(CONFIG_SH_FPU)
#define	fpu_error_trap_handler		exception_error
#endif

#if !defined(CONFIG_KGDB_NMI)
#define kgdb_handle_exception		exception_error
#endif

	.align 2
	.data

ENTRY(exception_handling_table)
	.long	exception_error		/* 000 */
	.long	exception_error
	.long	tlb_miss_load		/* 040 */
	.long	tlb_miss_store
	.long	initial_page_write
	.long	tlb_protection_violation_load
	.long	tlb_protection_violation_store
	.long	address_error_load
	.long	address_error_store	/* 100 */
	.long	fpu_error_trap_handler	/* 120 */
	.long	exception_error		/* 140 */
	.long	system_call	! Unconditional Trap	 /* 160 */
	.long	exception_error	! reserved_instruction (filled by trap_init) /* 180 */
	.long	exception_error	! illegal_slot_instruction (filled by trap_init) /*1A0*/
ENTRY(nmi_slot)
	.long	kgdb_handle_exception	/* 1C0 */	! Allow trap to debugger
ENTRY(user_break_point_trap)
	.long	break_point_trap	/* 1E0 */

	/*
	 * Pad the remainder of the table out, exceptions residing in far
	 * away offsets can be manually inserted in to their appropriate
	 * location via set_exception_table_{evt,vec}().
	 */
	.balign	4096,0,4096
