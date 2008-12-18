/*
 * This file contains the routines for handling the MMU on those
 * PowerPC implementations where the MMU is not using the hash
 * table, such as 8xx, 4xx, BookE's etc...
 *
 * Copyright 2008 Ben Herrenschmidt <benh@kernel.crashing.org>
 *                IBM Corp.
 *
 *  Derived from previous arch/powerpc/mm/mmu_context.c
 *  and arch/powerpc/include/asm/mmu_context.h
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/mm.h>
#include <linux/init.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

/*
 *   The MPC8xx has only 16 contexts.  We rotate through them on each
 * task switch.  A better way would be to keep track of tasks that
 * own contexts, and implement an LRU usage.  That way very active
 * tasks don't always have to pay the TLB reload overhead.  The
 * kernel pages are mapped shared, so the kernel can run on behalf
 * of any task that makes a kernel entry.  Shared does not mean they
 * are not protected, just that the ASID comparison is not performed.
 *      -- Dan
 *
 * The IBM4xx has 256 contexts, so we can just rotate through these
 * as a way of "switching" contexts.  If the TID of the TLB is zero,
 * the PID/TID comparison is disabled, so we can use a TID of zero
 * to represent all kernel pages as shared among all contexts.
 * 	-- Dan
 */

#ifdef CONFIG_8xx
#define NO_CONTEXT      	16
#define LAST_CONTEXT    	15
#define FIRST_CONTEXT    	0

#elif defined(CONFIG_4xx)
#define NO_CONTEXT      	256
#define LAST_CONTEXT    	255
#define FIRST_CONTEXT    	1

#elif defined(CONFIG_E200) || defined(CONFIG_E500)
#define NO_CONTEXT      	256
#define LAST_CONTEXT    	255
#define FIRST_CONTEXT    	1

#else
#error Unsupported processor type
#endif

static unsigned long next_mmu_context;
static unsigned long context_map[LAST_CONTEXT / BITS_PER_LONG + 1];
static atomic_t nr_free_contexts;
static struct mm_struct *context_mm[LAST_CONTEXT+1];
static void steal_context(void);

/* Steal a context from a task that has one at the moment.
 * This is only used on 8xx and 4xx and we presently assume that
 * they don't do SMP.  If they do then this will have to check
 * whether the MM we steal is in use.
 * We also assume that this is only used on systems that don't
 * use an MMU hash table - this is true for 8xx and 4xx.
 * This isn't an LRU system, it just frees up each context in
 * turn (sort-of pseudo-random replacement :).  This would be the
 * place to implement an LRU scheme if anyone was motivated to do it.
 *  -- paulus
 */
static void steal_context(void)
{
	struct mm_struct *mm;

	/* free up context `next_mmu_context' */
	/* if we shouldn't free context 0, don't... */
	if (next_mmu_context < FIRST_CONTEXT)
		next_mmu_context = FIRST_CONTEXT;
	mm = context_mm[next_mmu_context];
	flush_tlb_mm(mm);
	destroy_context(mm);
}


/*
 * Get a new mmu context for the address space described by `mm'.
 */
static inline void get_mmu_context(struct mm_struct *mm)
{
	unsigned long ctx;

	if (mm->context.id != NO_CONTEXT)
		return;

	while (atomic_dec_if_positive(&nr_free_contexts) < 0)
		steal_context();

	ctx = next_mmu_context;
	while (test_and_set_bit(ctx, context_map)) {
		ctx = find_next_zero_bit(context_map, LAST_CONTEXT+1, ctx);
		if (ctx > LAST_CONTEXT)
			ctx = 0;
	}
	next_mmu_context = (ctx + 1) & LAST_CONTEXT;
	mm->context.id = ctx;
	context_mm[ctx] = mm;
}

void switch_mmu_context(struct mm_struct *prev, struct mm_struct *next)
{
	get_mmu_context(next);

	set_context(next->context.id, next->pgd);
}

/*
 * Set up the context for a new address space.
 */
int init_new_context(struct task_struct *t, struct mm_struct *mm)
{
	mm->context.id = NO_CONTEXT;
	return 0;
}

/*
 * We're finished using the context for an address space.
 */
void destroy_context(struct mm_struct *mm)
{
	preempt_disable();
	if (mm->context.id != NO_CONTEXT) {
		clear_bit(mm->context.id, context_map);
		mm->context.id = NO_CONTEXT;
		atomic_inc(&nr_free_contexts);
	}
	preempt_enable();
}


/*
 * Initialize the context management stuff.
 */
void __init mmu_context_init(void)
{
	/*
	 * Some processors have too few contexts to reserve one for
	 * init_mm, and require using context 0 for a normal task.
	 * Other processors reserve the use of context zero for the kernel.
	 * This code assumes FIRST_CONTEXT < 32.
	 */
	context_map[0] = (1 << FIRST_CONTEXT) - 1;
	next_mmu_context = FIRST_CONTEXT;
	atomic_set(&nr_free_contexts, LAST_CONTEXT - FIRST_CONTEXT + 1);
}

