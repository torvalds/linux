/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_MMU_CONTEXT_H
#define _ASM_MICROBLAZE_MMU_CONTEXT_H

#include <linux/atomic.h>
#include <linux/mm_types.h>

#include <asm/bitops.h>
#include <asm/mmu.h>
#include <asm-generic/mm_hooks.h>

# ifdef __KERNEL__
/*
 * This function defines the mapping from contexts to VSIDs (virtual
 * segment IDs).  We use a skew on both the context and the high 4 bits
 * of the 32-bit virtual address (the "effective segment ID") in order
 * to spread out the entries in the MMU hash table.
 */
# define CTX_TO_VSID(ctx, va)	(((ctx) * (897 * 16) + ((va) >> 28) * 0x111) \
				 & 0xffffff)

/*
   MicroBlaze has 256 contexts, so we can just rotate through these
   as a way of "switching" contexts.  If the TID of the TLB is zero,
   the PID/TID comparison is disabled, so we can use a TID of zero
   to represent all kernel pages as shared among all contexts.
 */

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

# define NO_CONTEXT	256
# define LAST_CONTEXT	255
# define FIRST_CONTEXT	1

/*
 * Set the current MMU context.
 * This is done byloading up the segment registers for the user part of the
 * address space.
 *
 * Since the PGD is immediately available, it is much faster to simply
 * pass this along as a second parameter, which is required for 8xx and
 * can be used for debugging on all processors (if you happen to have
 * an Abatron).
 */
extern void set_context(mm_context_t context, pgd_t *pgd);

/*
 * Bitmap of contexts in use.
 * The size of this bitmap is LAST_CONTEXT + 1 bits.
 */
extern unsigned long context_map[];

/*
 * This caches the next context number that we expect to be free.
 * Its use is an optimization only, we can't rely on this context
 * number to be free, but it usually will be.
 */
extern mm_context_t next_mmu_context;

/*
 * Since we don't have sufficient contexts to give one to every task
 * that could be in the system, we need to be able to steal contexts.
 * These variables support that.
 */
extern atomic_t nr_free_contexts;
extern struct mm_struct *context_mm[LAST_CONTEXT+1];
extern void steal_context(void);

/*
 * Get a new mmu context for the address space described by `mm'.
 */
static inline void get_mmu_context(struct mm_struct *mm)
{
	mm_context_t ctx;

	if (mm->context != NO_CONTEXT)
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
	mm->context = ctx;
	context_mm[ctx] = mm;
}

/*
 * Set up the context for a new address space.
 */
# define init_new_context(tsk, mm)	(((mm)->context = NO_CONTEXT), 0)

/*
 * We're finished using the context for an address space.
 */
static inline void destroy_context(struct mm_struct *mm)
{
	if (mm->context != NO_CONTEXT) {
		clear_bit(mm->context, context_map);
		mm->context = NO_CONTEXT;
		atomic_inc(&nr_free_contexts);
	}
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	tsk->thread.pgdir = next->pgd;
	get_mmu_context(next);
	set_context(next->context, next->pgd);
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void activate_mm(struct mm_struct *active_mm,
			struct mm_struct *mm)
{
	current->thread.pgdir = mm->pgd;
	get_mmu_context(mm);
	set_context(mm->context, mm->pgd);
}

extern void mmu_context_init(void);

# endif /* __KERNEL__ */
#endif /* _ASM_MICROBLAZE_MMU_CONTEXT_H */
