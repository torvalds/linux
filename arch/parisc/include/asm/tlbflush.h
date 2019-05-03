/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC_TLBFLUSH_H
#define _PARISC_TLBFLUSH_H

/* TLB flushing routines.... */

#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/mmu_context.h>


/* This is for the serialisation of PxTLB broadcasts.  At least on the
 * N class systems, only one PxTLB inter processor broadcast can be
 * active at any one time on the Merced bus.  This tlb purge
 * synchronisation is fairly lightweight and harmless so we activate
 * it on all systems not just the N class.

 * It is also used to ensure PTE updates are atomic and consistent
 * with the TLB.
 */
extern spinlock_t pa_tlb_lock;

#define purge_tlb_start(flags)	spin_lock_irqsave(&pa_tlb_lock, flags)
#define purge_tlb_end(flags)	spin_unlock_irqrestore(&pa_tlb_lock, flags)

extern void flush_tlb_all(void);
extern void flush_tlb_all_local(void *);

#define smp_flush_tlb_all()	flush_tlb_all()

int __flush_tlb_range(unsigned long sid,
	unsigned long start, unsigned long end);

#define flush_tlb_range(vma, start, end) \
	__flush_tlb_range((vma)->vm_mm->context, start, end)

#define flush_tlb_kernel_range(start, end) \
	__flush_tlb_range(0, start, end)

/*
 * flush_tlb_mm()
 *
 * The code to switch to a new context is NOT valid for processes
 * which play with the space id's.  Thus, we have to preserve the
 * space and just flush the entire tlb.  However, the compilers,
 * dynamic linker, etc, do not manipulate space id's, so there
 * could be a significant performance benefit in switching contexts
 * and not flushing the whole tlb.
 */

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	BUG_ON(mm == &init_mm); /* Should never happen */

#if 1 || defined(CONFIG_SMP)
	/* Except for very small threads, flushing the whole TLB is
	 * faster than using __flush_tlb_range.  The pdtlb and pitlb
	 * instructions are very slow because of the TLB broadcast.
	 * It might be faster to do local range flushes on all CPUs
	 * on PA 2.0 systems.
	 */
	flush_tlb_all();
#else
	/* FIXME: currently broken, causing space id and protection ids
	 * to go out of sync, resulting in faults on userspace accesses.
	 * This approach needs further investigation since running many
	 * small applications (e.g., GCC testsuite) is faster on HP-UX.
	 */
	if (mm) {
		if (mm->context != 0)
			free_sid(mm->context);
		mm->context = alloc_sid();
		if (mm == current->active_mm)
			load_context(mm->context);
	}
#endif
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
	unsigned long addr)
{
	unsigned long flags, sid;

	sid = vma->vm_mm->context;
	purge_tlb_start(flags);
	mtsp(sid, 1);
	pdtlb(addr);
	pitlb(addr);
	purge_tlb_end(flags);
}
#endif
