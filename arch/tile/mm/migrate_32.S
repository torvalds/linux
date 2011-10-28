/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * This routine is a helper for migrating the home of a set of pages to
 * a new cpu.  See the documentation in homecache.c for more information.
 */

#include <linux/linkage.h>
#include <linux/threads.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/types.h>
#include <asm/asm-offsets.h>
#include <hv/hypervisor.h>

	.text

/*
 * First, some definitions that apply to all the code in the file.
 */

/* Locals (caller-save) */
#define r_tmp		r10
#define r_save_sp	r11

/* What we save where in the stack frame; must include all callee-saves. */
#define FRAME_SP	4
#define FRAME_R30	8
#define FRAME_R31	12
#define FRAME_R32	16
#define FRAME_R33	20
#define FRAME_R34	24
#define FRAME_R35	28
#define FRAME_SIZE	32




/*
 * On entry:
 *
 *   r0 low word of the new context PA to install (moved to r_context_lo)
 *   r1 high word of the new context PA to install (moved to r_context_hi)
 *   r2 low word of PTE to use for context access (moved to r_access_lo)
 *   r3 high word of PTE to use for context access (moved to r_access_lo)
 *   r4 ASID to use for new context (moved to r_asid)
 *   r5 pointer to cpumask with just this cpu set in it (r_my_cpumask)
 */

/* Arguments (caller-save) */
#define r_context_lo_in	r0
#define r_context_hi_in	r1
#define r_access_lo_in	r2
#define r_access_hi_in	r3
#define r_asid_in	r4
#define r_my_cpumask	r5

/* Locals (callee-save); must not be more than FRAME_xxx above. */
#define r_save_ics	r30
#define r_context_lo	r31
#define r_context_hi	r32
#define r_access_lo	r33
#define r_access_hi	r34
#define r_asid		r35

STD_ENTRY(flush_and_install_context)
	/*
	 * Create a stack frame; we can't touch it once we flush the
	 * cache until we install the new page table and flush the TLB.
	 */
	{
	 move r_save_sp, sp
	 sw sp, lr
	 addi sp, sp, -FRAME_SIZE
	}
	addi r_tmp, sp, FRAME_SP
	{
	 sw r_tmp, r_save_sp
	 addi r_tmp, sp, FRAME_R30
	}
	{
	 sw r_tmp, r30
	 addi r_tmp, sp, FRAME_R31
	}
	{
	 sw r_tmp, r31
	 addi r_tmp, sp, FRAME_R32
	}
	{
	 sw r_tmp, r32
	 addi r_tmp, sp, FRAME_R33
	}
	{
	 sw r_tmp, r33
	 addi r_tmp, sp, FRAME_R34
	}
	{
	 sw r_tmp, r34
	 addi r_tmp, sp, FRAME_R35
	}
	sw r_tmp, r35

	/* Move some arguments to callee-save registers. */
	{
	 move r_context_lo, r_context_lo_in
	 move r_context_hi, r_context_hi_in
	}
	{
	 move r_access_lo, r_access_lo_in
	 move r_access_hi, r_access_hi_in
	}
	move r_asid, r_asid_in

	/* Disable interrupts, since we can't use our stack. */
	{
	 mfspr r_save_ics, INTERRUPT_CRITICAL_SECTION
	 movei r_tmp, 1
	}
	mtspr INTERRUPT_CRITICAL_SECTION, r_tmp

	/* First, flush our L2 cache. */
	{
	 move r0, zero  /* cache_pa */
	 move r1, zero
	}
	{
	 auli r2, zero, ha16(HV_FLUSH_EVICT_L2)  /* cache_control */
	 move r3, r_my_cpumask  /* cache_cpumask */
	}
	{
	 move r4, zero  /* tlb_va */
	 move r5, zero  /* tlb_length */
	}
	{
	 move r6, zero  /* tlb_pgsize */
	 move r7, zero  /* tlb_cpumask */
	}
	{
	 move r8, zero  /* asids */
	 move r9, zero  /* asidcount */
	}
	jal hv_flush_remote
	bnz r0, .Ldone

	/* Now install the new page table. */
	{
	 move r0, r_context_lo
	 move r1, r_context_hi
	}
	{
	 move r2, r_access_lo
	 move r3, r_access_hi
	}
	{
	 move r4, r_asid
	 movei r5, HV_CTX_DIRECTIO
	}
	jal hv_install_context
	bnz r0, .Ldone

	/* Finally, flush the TLB. */
	{
	 movei r0, 0   /* preserve_global */
	 jal hv_flush_all
	}

.Ldone:
	/* Reset interrupts back how they were before. */
	mtspr INTERRUPT_CRITICAL_SECTION, r_save_ics

	/* Restore the callee-saved registers and return. */
	addli lr, sp, FRAME_SIZE
	{
	 lw lr, lr
	 addli r_tmp, sp, FRAME_R30
	}
	{
	 lw r30, r_tmp
	 addli r_tmp, sp, FRAME_R31
	}
	{
	 lw r31, r_tmp
	 addli r_tmp, sp, FRAME_R32
	}
	{
	 lw r32, r_tmp
	 addli r_tmp, sp, FRAME_R33
	}
	{
	 lw r33, r_tmp
	 addli r_tmp, sp, FRAME_R34
	}
	{
	 lw r34, r_tmp
	 addli r_tmp, sp, FRAME_R35
	}
	{
	 lw r35, r_tmp
	 addi sp, sp, FRAME_SIZE
	}
	jrp lr
	STD_ENDPROC(flush_and_install_context)
