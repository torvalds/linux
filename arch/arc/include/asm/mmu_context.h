/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * vineetg: May 2011
 *  -Refactored get_new_mmu_context( ) to only handle live-mm.
 *   retiring-mm handled in other hooks
 *
 * Vineetg: March 25th, 2008: Bug #92690
 *  -Major rewrite of Core ASID allocation routine get_new_mmu_context
 *
 * Amit Bhor, Sameer Dhavale: Codito Technologies 2004
 */

#ifndef _ASM_ARC_MMU_CONTEXT_H
#define _ASM_ARC_MMU_CONTEXT_H

#include <linux/sched/mm.h>

#include <asm/tlb.h>
#include <asm-generic/mm_hooks.h>

/*		ARC ASID Management
 *
 * MMU tags TLBs with an 8-bit ASID, avoiding need to flush the TLB on
 * context-switch.
 *
 * ASID is managed per cpu, so task threads across CPUs can have different
 * ASID. Global ASID management is needed if hardware supports TLB shootdown
 * and/or shared TLB across cores, which ARC doesn't.
 *
 * Each task is assigned unique ASID, with a simple round-robin allocator
 * tracked in @asid_cpu. When 8-bit value rolls over,a new cycle is started
 * over from 0, and TLB is flushed
 *
 * A new allocation cycle, post rollover, could potentially reassign an ASID
 * to a different task. Thus the rule is to refresh the ASID in a new cycle.
 * The 32 bit @asid_cpu (and mm->asid) have 8 bits MMU PID and rest 24 bits
 * serve as cycle/generation indicator and natural 32 bit unsigned math
 * automagically increments the generation when lower 8 bits rollover.
 */

#define MM_CTXT_ASID_MASK	0x000000ff /* MMU PID reg :8 bit PID */
#define MM_CTXT_CYCLE_MASK	(~MM_CTXT_ASID_MASK)

#define MM_CTXT_FIRST_CYCLE	(MM_CTXT_ASID_MASK + 1)
#define MM_CTXT_NO_ASID		0UL

#define asid_mm(mm, cpu)	mm->context.asid[cpu]
#define hw_pid(mm, cpu)		(asid_mm(mm, cpu) & MM_CTXT_ASID_MASK)

DECLARE_PER_CPU(unsigned int, asid_cache);
#define asid_cpu(cpu)		per_cpu(asid_cache, cpu)

/*
 * Get a new ASID if task doesn't have a valid one (unalloc or from prev cycle)
 * Also set the MMU PID register to existing/updated ASID
 */
static inline void get_new_mmu_context(struct mm_struct *mm)
{
	const unsigned int cpu = smp_processor_id();
	unsigned long flags;

	local_irq_save(flags);

	/*
	 * Move to new ASID if it was not from current alloc-cycle/generation.
	 * This is done by ensuring that the generation bits in both mm->ASID
	 * and cpu's ASID counter are exactly same.
	 *
	 * Note: Callers needing new ASID unconditionally, independent of
	 * 	 generation, e.g. local_flush_tlb_mm() for forking  parent,
	 * 	 first need to destroy the context, setting it to invalid
	 * 	 value.
	 */
	if (!((asid_mm(mm, cpu) ^ asid_cpu(cpu)) & MM_CTXT_CYCLE_MASK))
		goto set_hw;

	/* move to new ASID and handle rollover */
	if (unlikely(!(++asid_cpu(cpu) & MM_CTXT_ASID_MASK))) {

		local_flush_tlb_all();

		/*
		 * Above check for rollover of 8 bit ASID in 32 bit container.
		 * If the container itself wrapped around, set it to a non zero
		 * "generation" to distinguish from no context
		 */
		if (!asid_cpu(cpu))
			asid_cpu(cpu) = MM_CTXT_FIRST_CYCLE;
	}

	/* Assign new ASID to tsk */
	asid_mm(mm, cpu) = asid_cpu(cpu);

set_hw:
	mmu_setup_asid(mm, hw_pid(mm, cpu));

	local_irq_restore(flags);
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
#define init_new_context init_new_context
static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	int i;

	for_each_possible_cpu(i)
		asid_mm(mm, i) = MM_CTXT_NO_ASID;

	return 0;
}

#define destroy_context destroy_context
static inline void destroy_context(struct mm_struct *mm)
{
	unsigned long flags;

	/* Needed to elide CONFIG_DEBUG_PREEMPT warning */
	local_irq_save(flags);
	asid_mm(mm, smp_processor_id()) = MM_CTXT_NO_ASID;
	local_irq_restore(flags);
}

/* Prepare the MMU for task: setup PID reg with allocated ASID
    If task doesn't have an ASID (never alloc or stolen, get a new ASID)
*/
static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	const int cpu = smp_processor_id();

	/*
	 * Note that the mm_cpumask is "aggregating" only, we don't clear it
	 * for the switched-out task, unlike some other arches.
	 * It is used to enlist cpus for sending TLB flush IPIs and not sending
	 * it to CPUs where a task once ran-on, could cause stale TLB entry
	 * re-use, specially for a multi-threaded task.
	 * e.g. T1 runs on C1, migrates to C3. T2 running on C2 munmaps.
	 *      For a non-aggregating mm_cpumask, IPI not sent C1, and if T1
	 *      were to re-migrate to C1, it could access the unmapped region
	 *      via any existing stale TLB entries.
	 */
	cpumask_set_cpu(cpu, mm_cpumask(next));

	mmu_setup_pgd(next, next->pgd);

	get_new_mmu_context(next);
}

/*
 * activate_mm defaults (in asm-generic) to switch_mm and is called at the
 * time of execve() to get a new ASID Note the subtlety here:
 * get_new_mmu_context() behaves differently here vs. in switch_mm(). Here
 * it always returns a new ASID, because mm has an unallocated "initial"
 * value, while in latter, it moves to a new ASID, only if it was
 * unallocated
 */

/* it seemed that deactivate_mm( ) is a reasonable place to do book-keeping
 * for retiring-mm. However destroy_context( ) still needs to do that because
 * between mm_release( ) = >deactive_mm( ) and
 * mmput => .. => __mmdrop( ) => destroy_context( )
 * there is a good chance that task gets sched-out/in, making its ASID valid
 * again (this teased me for a whole day).
 */

#include <asm-generic/mmu_context.h>

#endif /* __ASM_ARC_MMU_CONTEXT_H */
