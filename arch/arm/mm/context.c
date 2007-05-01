/*
 *  linux/arch/arm/mm/context.c
 *
 *  Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

unsigned int cpu_last_asid = { 1 << ASID_BITS };

/*
 * We fork()ed a process, and we need a new context for the child
 * to run in.  We reserve version 0 for initial tasks so we will
 * always allocate an ASID. The ASID 0 is reserved for the TTBR
 * register changing sequence.
 */
void __init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context.id = 0;
}

void __new_context(struct mm_struct *mm)
{
	unsigned int asid;

	asid = ++cpu_last_asid;
	if (asid == 0)
		asid = cpu_last_asid = 1 << ASID_BITS;

	/*
	 * If we've used up all our ASIDs, we need
	 * to start a new version and flush the TLB.
	 */
	if ((asid & ~ASID_MASK) == 0) {
		asid = ++cpu_last_asid;
		/* set the reserved ASID before flushing the TLB */
		asm("mcr	p15, 0, %0, c13, c0, 1	@ set reserved context ID\n"
		    :
		    : "r" (0));
		isb();
		flush_tlb_all();
	}

	mm->context.id = asid;
}
