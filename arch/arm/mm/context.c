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

static DEFINE_SPINLOCK(cpu_asid_lock);
unsigned int cpu_last_asid = ASID_FIRST_VERSION;

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

	spin_lock(&cpu_asid_lock);
	asid = ++cpu_last_asid;
	if (asid == 0)
		asid = cpu_last_asid = ASID_FIRST_VERSION;

	/*
	 * If we've used up all our ASIDs, we need
	 * to start a new version and flush the TLB.
	 */
	if (unlikely((asid & ~ASID_MASK) == 0)) {
		asid = ++cpu_last_asid;
		/* set the reserved ASID before flushing the TLB */
		asm("mcr	p15, 0, %0, c13, c0, 1	@ set reserved context ID\n"
		    :
		    : "r" (0));
		isb();
		flush_tlb_all();
		if (icache_is_vivt_asid_tagged()) {
			asm("mcr	p15, 0, %0, c7, c5, 0	@ invalidate I-cache\n"
			    "mcr	p15, 0, %0, c7, c5, 6	@ flush BTAC/BTB\n"
			    :
			    : "r" (0));
			dsb();
		}
	}
	spin_unlock(&cpu_asid_lock);

	cpumask_copy(mm_cpumask(mm), cpumask_of(smp_processor_id()));
	mm->context.id = asid;
}
