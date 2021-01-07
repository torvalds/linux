/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PARISC_MMU_CONTEXT_H
#define __PARISC_MMU_CONTEXT_H

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <asm-generic/mm_hooks.h>

/* on PA-RISC, we actually have enough contexts to justify an allocator
 * for them.  prumpf */

extern unsigned long alloc_sid(void);
extern void free_sid(unsigned long);

#define init_new_context init_new_context
static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	BUG_ON(atomic_read(&mm->mm_users) != 1);

	mm->context = alloc_sid();
	return 0;
}

#define destroy_context destroy_context
static inline void
destroy_context(struct mm_struct *mm)
{
	free_sid(mm->context);
	mm->context = 0;
}

static inline unsigned long __space_to_prot(mm_context_t context)
{
#if SPACEID_SHIFT == 0
	return context << 1;
#else
	return context >> (SPACEID_SHIFT - 1);
#endif
}

static inline void load_context(mm_context_t context)
{
	mtsp(context, 3);
	mtctl(__space_to_prot(context), 8);
}

static inline void switch_mm_irqs_off(struct mm_struct *prev,
		struct mm_struct *next, struct task_struct *tsk)
{
	if (prev != next) {
		mtctl(__pa(next->pgd), 25);
		load_context(next->context);
	}
}

static inline void switch_mm(struct mm_struct *prev,
		struct mm_struct *next, struct task_struct *tsk)
{
	unsigned long flags;

	if (prev == next)
		return;

	local_irq_save(flags);
	switch_mm_irqs_off(prev, next, tsk);
	local_irq_restore(flags);
}
#define switch_mm_irqs_off switch_mm_irqs_off

#define activate_mm activate_mm
static inline void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	/*
	 * Activate_mm is our one chance to allocate a space id
	 * for a new mm created in the exec path. There's also
	 * some lazy tlb stuff, which is currently dead code, but
	 * we only allocate a space id if one hasn't been allocated
	 * already, so we should be OK.
	 */

	BUG_ON(next == &init_mm); /* Should never happen */

	if (next->context == 0)
	    next->context = alloc_sid();

	switch_mm(prev,next,current);
}

#include <asm-generic/mmu_context.h>

#endif
