/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
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
#define FRAME_SP	8
#define FRAME_R30	16
#define FRAME_R31	24
#define FRAME_R32	32
#define FRAME_SIZE	40




/*
 * On entry:
 *
 *   r0 the new context PA to install (moved to r_context)
 *   r1 PTE to use for context access (moved to r_access)
 *   r2 ASID to use for new context (moved to r_asid)
 *   r3 pointer to cpumask with just this cpu set in it (r_my_cpumask)
 */

/* Arguments (caller-save) */
#define r_context_in	r0
#define r_access_in	r1
#define r_asid_in	r2
#define r_my_cpumask	r3

/* Locals (callee-save); must not be more than FRAME_xxx above. */
#define r_context	r30
#define r_access	r31
#define r_asid		r32

/*
 * Caller-save locals and frame constants are the same as
 * for homecache_migrate_stack_and_flush.
 */

STD_ENTRY(flush_and_install_context)
	/*
	 * Create a stack frame; we can't touch it once we flush the
	 * cache until we install the new page table and flush the TLB.
	 */
	{
	 move r_save_sp, sp
	 st sp, lr
	 addi sp, sp, -FRAME_SIZE
	}
	addi r_tmp, sp, FRAME_SP
	{
	 st r_tmp, r_save_sp
	 addi r_tmp, sp, FRAME_R30
	}
	{
	 st r_tmp, r30
	 addi r_tmp, sp, FRAME_R31
	}
	{
	 st r_tmp, r31
	 addi r_tmp, sp, FRAME_R32
	}
	st r_tmp, r32

	/* Move some arguments to callee-save registers. */
	{
	 move r_context, r_context_in
	 move r_access, r_access_in
	}
	move r_asid, r_asid_in

	/* First, flush our L2 cache. */
	{
	 move r0, zero  /* cache_pa */
	 moveli r1, hw2_last(HV_FLUSH_EVICT_L2)  /* cache_control */
	}
	{
	 shl16insli r1, r1, hw1(HV_FLUSH_EVICT_L2)
	 move r2, r_my_cpumask  /* cache_cpumask */
	}
	{
	 shl16insli r1, r1, hw0(HV_FLUSH_EVICT_L2)
	 move r3, zero  /* tlb_va */
	}
	{
	 move r4, zero  /* tlb_length */
	 move r5, zero  /* tlb_pgsize */
	}
	{
	 move r6, zero  /* tlb_cpumask */
	 move r7, zero  /* asids */
	}
	{
	 move r8, zero  /* asidcount */
	 jal hv_flush_remote
	}
	bnez r0, 1f

	/* Now install the new page table. */
	{
	 move r0, r_context
	 move r1, r_access
	}
	{
	 move r2, r_asid
	 moveli r3, HV_CTX_DIRECTIO | CTX_PAGE_FLAG
	}
	jal hv_install_context
	bnez r0, 1f

	/* Finally, flush the TLB. */
	{
	 movei r0, 0   /* preserve_global */
	 jal hv_flush_all
	}

1:	/* Restore the callee-saved registers and return. */
	addli lr, sp, FRAME_SIZE
	{
	 ld lr, lr
	 addli r_tmp, sp, FRAME_R30
	}
	{
	 ld r30, r_tmp
	 addli r_tmp, sp, FRAME_R31
	}
	{
	 ld r31, r_tmp
	 addli r_tmp, sp, FRAME_R32
	}
	{
	 ld r32, r_tmp
	 addi sp, sp, FRAME_SIZE
	}
	jrp lr
	STD_ENDPROC(flush_and_install_context)
