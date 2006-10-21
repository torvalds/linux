/*
 *  arch/sh/kernel/cpu/sh4/ex.S
 *
 *  The SH-4 exception vector table.

 *  Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 *  Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <linux/linkage.h>

	.align 2
	.data

ENTRY(exception_handling_table)
	.long	exception_error		/* 000 */
	.long	exception_error
#if defined(CONFIG_MMU)
	.long	tlb_miss_load		/* 040 */
	.long	tlb_miss_store
	.long	initial_page_write
	.long	tlb_protection_violation_load
	.long	tlb_protection_violation_store
	.long	address_error_load
	.long	address_error_store	/* 100 */
#else
	.long	exception_error	! tlb miss load		/* 040 */
	.long	exception_error	! tlb miss store
	.long	exception_error	! initial page write
	.long	exception_error	! tlb prot violation load
	.long	exception_error	! tlb prot violation store
	.long	exception_error	! address error load
	.long	exception_error	! address error store	/* 100 */
#endif
#if defined(CONFIG_SH_FPU)
	.long	do_fpu_error		/* 120 */
#else
	.long	exception_error		/* 120 */
#endif
	.long	exception_error		/* 140 */
	.long	system_call	! Unconditional Trap	 /* 160 */
	.long	exception_error	! reserved_instruction (filled by trap_init) /* 180 */
	.long	exception_error	! illegal_slot_instruction (filled by trap_init) /*1A0*/
ENTRY(nmi_slot)
#if defined (CONFIG_KGDB_NMI)
	.long	debug_enter	/* 1C0 */	! Allow trap to debugger
#else
	.long	exception_none	/* 1C0 */	! Not implemented yet
#endif
ENTRY(user_break_point_trap)
	.long	break_point_trap	/* 1E0 */
