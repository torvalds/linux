/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_MMU_CONTEXT_H
#define _ASM_IA64_MMU_CONTEXT_H

/*
 * Copyright (C) 1998-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

/*
 * Routines to manage the allocation of task context numbers.  Task context
 * numbers are used to reduce or eliminate the need to perform TLB flushes
 * due to context switches.  Context numbers are implemented using ia-64
 * region ids.  Since the IA-64 TLB does not consider the region number when
 * performing a TLB lookup, we need to assign a unique region id to each
 * region in a process.  We use the least significant three bits in aregion
 * id for this purpose.
 */

#define IA64_REGION_ID_KERNEL	0 /* the kernel's region id (tlb.c depends on this being 0) */

#define ia64_rid(ctx,addr)	(((ctx) << 3) | (addr >> 61))

# include <asm/page.h>
# ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/spinlock.h>

#include <asm/processor.h>
#include <asm-generic/mm_hooks.h>

struct ia64_ctx {
	spinlock_t lock;
	unsigned int next;	/* next context number to use */
	unsigned int limit;     /* available free range */
	unsigned int max_ctx;   /* max. context value supported by all CPUs */
				/* call wrap_mmu_context when next >= max */
	unsigned long *bitmap;  /* bitmap size is max_ctx+1 */
	unsigned long *flushmap;/* pending rid to be flushed */
};

extern struct ia64_ctx ia64_ctx;
DECLARE_PER_CPU(u8, ia64_need_tlb_flush);

extern void mmu_context_init (void);
extern void wrap_mmu_context (struct mm_struct *mm);

/*
 * When the context counter wraps around all TLBs need to be flushed because
 * an old context number might have been reused. This is signalled by the
 * ia64_need_tlb_flush per-CPU variable, which is checked in the routine
 * below. Called by activate_mm(). <efocht@ess.nec.de>
 */
static inline void
delayed_tlb_flush (void)
{
	extern void local_flush_tlb_all (void);
	unsigned long flags;

	if (unlikely(__ia64_per_cpu_var(ia64_need_tlb_flush))) {
		spin_lock_irqsave(&ia64_ctx.lock, flags);
		if (__ia64_per_cpu_var(ia64_need_tlb_flush)) {
			local_flush_tlb_all();
			__ia64_per_cpu_var(ia64_need_tlb_flush) = 0;
		}
		spin_unlock_irqrestore(&ia64_ctx.lock, flags);
	}
}

static inline nv_mm_context_t
get_mmu_context (struct mm_struct *mm)
{
	unsigned long flags;
	nv_mm_context_t context = mm->context;

	if (likely(context))
		goto out;

	spin_lock_irqsave(&ia64_ctx.lock, flags);
	/* re-check, now that we've got the lock: */
	context = mm->context;
	if (context == 0) {
		cpumask_clear(mm_cpumask(mm));
		if (ia64_ctx.next >= ia64_ctx.limit) {
			ia64_ctx.next = find_next_zero_bit(ia64_ctx.bitmap,
					ia64_ctx.max_ctx, ia64_ctx.next);
			ia64_ctx.limit = find_next_bit(ia64_ctx.bitmap,
					ia64_ctx.max_ctx, ia64_ctx.next);
			if (ia64_ctx.next >= ia64_ctx.max_ctx)
				wrap_mmu_context(mm);
		}
		mm->context = context = ia64_ctx.next++;
		__set_bit(context, ia64_ctx.bitmap);
	}
	spin_unlock_irqrestore(&ia64_ctx.lock, flags);
out:
	/*
	 * Ensure we're not starting to use "context" before any old
	 * uses of it are gone from our TLB.
	 */
	delayed_tlb_flush();

	return context;
}

/*
 * Initialize context number to some sane value.  MM is guaranteed to be a
 * brand-new address-space, so no TLB flushing is needed, ever.
 */
#define init_new_context init_new_context
static inline int
init_new_context (struct task_struct *p, struct mm_struct *mm)
{
	mm->context = 0;
	return 0;
}

static inline void
reload_context (nv_mm_context_t context)
{
	unsigned long rid;
	unsigned long rid_incr = 0;
	unsigned long rr0, rr1, rr2, rr3, rr4;

#ifdef CONFIG_HUGETLB_PAGE
	unsigned long old_rr4;
	old_rr4 = ia64_get_rr(RGN_BASE(RGN_HPAGE));
#endif
	rid = context << 3;	/* make space for encoding the region number */
	rid_incr = 1 << 8;

	/* encode the region id, preferred page size, and VHPT enable bit: */
	rr0 = (rid << 8) | (PAGE_SHIFT << 2) | 1;
	rr1 = rr0 + 1*rid_incr;
	rr2 = rr0 + 2*rid_incr;
	rr3 = rr0 + 3*rid_incr;
	rr4 = rr0 + 4*rid_incr;
#ifdef  CONFIG_HUGETLB_PAGE
	rr4 = (rr4 & (~(0xfcUL))) | (old_rr4 & 0xfc);

#  if RGN_HPAGE != 4
#    error "reload_context assumes RGN_HPAGE is 4"
#  endif
#endif

	ia64_set_rr0_to_rr4(rr0, rr1, rr2, rr3, rr4);
	ia64_srlz_i();			/* srlz.i implies srlz.d */
}

/*
 * Must be called with preemption off
 */
static inline void
activate_context (struct mm_struct *mm)
{
	nv_mm_context_t context;

	do {
		context = get_mmu_context(mm);
		if (!cpumask_test_cpu(smp_processor_id(), mm_cpumask(mm)))
			cpumask_set_cpu(smp_processor_id(), mm_cpumask(mm));
		reload_context(context);
		/*
		 * in the unlikely event of a TLB-flush by another thread,
		 * redo the load.
		 */
	} while (unlikely(context != mm->context));
}

/*
 * Switch from address space PREV to address space NEXT.
 */
#define activate_mm activate_mm
static inline void
activate_mm (struct mm_struct *prev, struct mm_struct *next)
{
	/*
	 * We may get interrupts here, but that's OK because interrupt
	 * handlers cannot touch user-space.
	 */
	ia64_set_kr(IA64_KR_PT_BASE, __pa(next->pgd));
	activate_context(next);
}

#define switch_mm(prev_mm,next_mm,next_task)	activate_mm(prev_mm, next_mm)

#include <asm-generic/mmu_context.h>

# endif /* ! __ASSEMBLY__ */
#endif /* _ASM_IA64_MMU_CONTEXT_H */
