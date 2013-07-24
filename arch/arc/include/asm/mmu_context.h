/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include <asm/arcregs.h>
#include <asm/tlb.h>

#include <asm-generic/mm_hooks.h>

/*		ARC700 ASID Management
 *
 * ARC MMU provides 8-bit ASID (0..255) to TAG TLB entries, allowing entries
 * with same vaddr (different tasks) to co-exit. This provides for
 * "Fast Context Switch" i.e. no TLB flush on ctxt-switch
 *
 * Linux assigns each task a unique ASID. A simple round-robin allocation
 * of H/w ASID is done using software tracker @asid_cache.
 * When it reaches max 255, the allocation cycle starts afresh by flushing
 * the entire TLB and wrapping ASID back to zero.
 *
 * For book-keeping, Linux uses a couple of data-structures:
 *  -mm_struct has an @asid field to keep a note of task's ASID (needed at the
 *   time of say switch_mm( )
 *  -An array of mm structs @asid_mm_map[] for asid->mm the reverse mapping,
 *  given an ASID, finding the mm struct associated.
 *
 * The round-robin allocation algorithm allows for ASID stealing.
 * If asid tracker is at "x-1", a new req will allocate "x", even if "x" was
 * already assigned to another (switched-out) task. Obviously the prev owner
 * is marked with an invalid ASID to make it request for a new ASID when it
 * gets scheduled next time. However its TLB entries (with ASID "x") could
 * exist, which must be cleared before the same ASID is used by the new owner.
 * Flushing them would be plausible but costly solution. Instead we force a
 * allocation policy quirk, which ensures that a stolen ASID won't have any
 * TLB entries associates, alleviating the need to flush.
 * The quirk essentially is not allowing ASID allocated in prev cycle
 * to be used past a roll-over in the next cycle.
 * When this happens (i.e. task ASID > asid tracker), task needs to refresh
 * its ASID, aligning it to current value of tracker. If the task doesn't get
 * scheduled past a roll-over, hence its ASID is not yet realigned with
 * tracker, such ASID is anyways safely reusable because it is
 * gauranteed that TLB entries with that ASID wont exist.
 */

#define FIRST_ASID  0
#define MAX_ASID    255			/* 8 bit PID field in PID Aux reg */
#define NO_ASID     (MAX_ASID + 1)	/* ASID Not alloc to mmu ctxt */
#define NUM_ASID    ((MAX_ASID - FIRST_ASID) + 1)

/* ASID to mm struct mapping */
extern struct mm_struct *asid_mm_map[NUM_ASID + 1];

extern int asid_cache;

/*
 * Get a new ASID if task doesn't have a valid one (unalloc or from prev cycle)
 * Also set the MMU PID register to existing/updated ASID
 */
static inline void get_new_mmu_context(struct mm_struct *mm)
{
	struct mm_struct *prev_owner;
	unsigned long flags;

	local_irq_save(flags);

	/*
	 * Move to new ASID if it was not from current alloc-cycle/generation.
	 *
	 * Note: Callers needing new ASID unconditionally, independent of
	 * 	 generation, e.g. local_flush_tlb_mm() for forking  parent,
	 * 	 first need to destroy the context, setting it to invalid
	 * 	 value.
	 */
	if (mm->context.asid <= asid_cache)
		goto set_hw;

	/*
	 * Relinquish the currently owned ASID (if any).
	 * Doing unconditionally saves a cmp-n-branch; for already unused
	 * ASID slot, the value was/remains NULL
	 */
	asid_mm_map[mm->context.asid] = (struct mm_struct *)NULL;

	/* move to new ASID */
	if (++asid_cache > MAX_ASID) {	/* ASID roll-over */
		asid_cache = FIRST_ASID;
		flush_tlb_all();
	}

	/*
	 * Is next ASID already owned by some-one else (we are stealing it).
	 * If so, let the orig owner be aware of this, so when it runs, it
	 * asks for a brand new ASID. This would only happen for a long-lived
	 * task with ASID from prev allocation cycle (before ASID roll-over).
	 *
	 * This might look wrong - if we are re-using some other task's ASID,
	 * won't we use it's stale TLB entries too. Actually the algorithm takes
	 * care of such a case: it ensures that task with ASID from prev alloc
	 * cycle, when scheduled will refresh it's ASID
	 * The stealing scenario described here will only happen if that task
	 * didn't get a chance to refresh it's ASID - implying stale entries
	 * won't exist.
	 */
	prev_owner = asid_mm_map[asid_cache];
	if (prev_owner)
		prev_owner->context.asid = NO_ASID;

	/* Assign new ASID to tsk */
	asid_mm_map[asid_cache] = mm;
	mm->context.asid = asid_cache;

set_hw:
	write_aux_reg(ARC_REG_PID, mm->context.asid | MMU_ENABLE);

	local_irq_restore(flags);
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context.asid = NO_ASID;
	return 0;
}

/* Prepare the MMU for task: setup PID reg with allocated ASID
    If task doesn't have an ASID (never alloc or stolen, get a new ASID)
*/
static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
#ifndef CONFIG_SMP
	/* PGD cached in MMU reg to avoid 3 mem lookups: task->mm->pgd */
	write_aux_reg(ARC_REG_SCRATCH_DATA0, next->pgd);
#endif

	get_new_mmu_context(next);
}

static inline void destroy_context(struct mm_struct *mm)
{
	unsigned long flags;

	local_irq_save(flags);

	asid_mm_map[mm->context.asid] = NULL;
	mm->context.asid = NO_ASID;

	local_irq_restore(flags);
}

/* it seemed that deactivate_mm( ) is a reasonable place to do book-keeping
 * for retiring-mm. However destroy_context( ) still needs to do that because
 * between mm_release( ) = >deactive_mm( ) and
 * mmput => .. => __mmdrop( ) => destroy_context( )
 * there is a good chance that task gets sched-out/in, making it's ASID valid
 * again (this teased me for a whole day).
 */
#define deactivate_mm(tsk, mm)   do { } while (0)

static inline void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
#ifndef CONFIG_SMP
	write_aux_reg(ARC_REG_SCRATCH_DATA0, next->pgd);
#endif

	/* Unconditionally get a new ASID */
	get_new_mmu_context(next);

}

#define enter_lazy_tlb(mm, tsk)

#endif /* __ASM_ARC_MMU_CONTEXT_H */
