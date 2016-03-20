/*
 * MMU context handling.
 *
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/mm.h>

#include <asm/cpuinfo.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>

/* The pids position and mask in context */
#define PID_SHIFT	0
#define PID_BITS	(cpuinfo.tlb_pid_num_bits)
#define PID_MASK	((1UL << PID_BITS) - 1)

/* The versions position and mask in context */
#define VERSION_BITS	(32 - PID_BITS)
#define VERSION_SHIFT	(PID_SHIFT + PID_BITS)
#define VERSION_MASK	((1UL << VERSION_BITS) - 1)

/* Return the version part of a context */
#define CTX_VERSION(c)	(((c) >> VERSION_SHIFT) & VERSION_MASK)

/* Return the pid part of a context */
#define CTX_PID(c)	(((c) >> PID_SHIFT) & PID_MASK)

/* Value of the first context (version 1, pid 0) */
#define FIRST_CTX	((1UL << VERSION_SHIFT) | (0 << PID_SHIFT))

static mm_context_t next_mmu_context;

/*
 * Initialize MMU context management stuff.
 */
void __init mmu_context_init(void)
{
	/* We need to set this here because the value depends on runtime data
	 * from cpuinfo */
	next_mmu_context = FIRST_CTX;
}

/*
 * Set new context (pid), keep way
 */
static void set_context(mm_context_t context)
{
	set_mmu_pid(CTX_PID(context));
}

static mm_context_t get_new_context(void)
{
	/* Return the next pid */
	next_mmu_context += (1UL << PID_SHIFT);

	/* If the pid field wraps around we increase the version and
	 * flush the tlb */
	if (unlikely(CTX_PID(next_mmu_context) == 0)) {
		/* Version is incremented since the pid increment above
		 * overflows info version */
		flush_cache_all();
		flush_tlb_all();
	}

	/* If the version wraps we start over with the first generation, we do
	 * not need to flush the tlb here since it's always done above */
	if (unlikely(CTX_VERSION(next_mmu_context) == 0))
		next_mmu_context = FIRST_CTX;

	return next_mmu_context;
}

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *tsk)
{
	unsigned long flags;

	local_irq_save(flags);

	/* If the process context we are swapping in has a different context
	 * generation then we have it should get a new generation/pid */
	if (unlikely(CTX_VERSION(next->context) !=
		CTX_VERSION(next_mmu_context)))
		next->context = get_new_context();

	/* Save the current pgd so the fast tlb handler can find it */
	pgd_current = next->pgd;

	/* Set the current context */
	set_context(next->context);

	local_irq_restore(flags);
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	next->context = get_new_context();
	set_context(next->context);
	pgd_current = next->pgd;
}

unsigned long get_pid_from_context(mm_context_t *context)
{
	return CTX_PID((*context));
}
