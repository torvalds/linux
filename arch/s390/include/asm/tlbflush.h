/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _S390_TLBFLUSH_H
#define _S390_TLBFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/pgalloc.h>

/*
 * Flush all TLB entries on the local CPU.
 */
static inline void __tlb_flush_local(void)
{
	asm volatile("ptlb" : : : "memory");
}

/*
 * Flush TLB entries for a specific ASCE on all CPUs
 */
static inline void __tlb_flush_idte(unsigned long asce)
{
	unsigned long opt;

	opt = IDTE_PTOA;
	if (MACHINE_HAS_TLB_GUEST)
		opt |= IDTE_GUEST_ASCE;
	/* Global TLB flush for the mm */
	asm volatile(
		"	.insn	rrf,0xb98e0000,0,%0,%1,0"
		: : "a" (opt), "a" (asce) : "cc");
}

void smp_ptlb_all(void);

/*
 * Flush all TLB entries on all CPUs.
 */
static inline void __tlb_flush_global(void)
{
	unsigned int dummy = 0;

	csp(&dummy, 0, 0);
}

/*
 * Flush TLB entries for a specific mm on all CPUs (in case gmap is used
 * this implicates multiple ASCEs!).
 */
static inline void __tlb_flush_mm(struct mm_struct *mm)
{
	unsigned long gmap_asce;

	/*
	 * If the machine has IDTE we prefer to do a per mm flush
	 * on all cpus instead of doing a local flush if the mm
	 * only ran on the local cpu.
	 */
	preempt_disable();
	atomic_inc(&mm->context.flush_count);
	/* Reset TLB flush mask */
	cpumask_copy(mm_cpumask(mm), &mm->context.cpu_attach_mask);
	barrier();
	gmap_asce = READ_ONCE(mm->context.gmap_asce);
	if (MACHINE_HAS_IDTE && gmap_asce != -1UL) {
		if (gmap_asce)
			__tlb_flush_idte(gmap_asce);
		__tlb_flush_idte(mm->context.asce);
	} else {
		/* Global TLB flush */
		__tlb_flush_global();
	}
	atomic_dec(&mm->context.flush_count);
	preempt_enable();
}

static inline void __tlb_flush_kernel(void)
{
	if (MACHINE_HAS_IDTE)
		__tlb_flush_idte(init_mm.context.asce);
	else
		__tlb_flush_global();
}

static inline void __tlb_flush_mm_lazy(struct mm_struct * mm)
{
	spin_lock(&mm->context.lock);
	if (mm->context.flush_mm) {
		mm->context.flush_mm = 0;
		__tlb_flush_mm(mm);
	}
	spin_unlock(&mm->context.lock);
}

/*
 * TLB flushing:
 *  flush_tlb() - flushes the current mm struct TLBs
 *  flush_tlb_all() - flushes all processes TLBs
 *  flush_tlb_mm(mm) - flushes the specified mm context TLB's
 *  flush_tlb_page(vma, vmaddr) - flushes one page
 *  flush_tlb_range(vma, start, end) - flushes a range of pages
 *  flush_tlb_kernel_range(start, end) - flushes a range of kernel pages
 */

/*
 * flush_tlb_mm goes together with ptep_set_wrprotect for the
 * copy_page_range operation and flush_tlb_range is related to
 * ptep_get_and_clear for change_protection. ptep_set_wrprotect and
 * ptep_get_and_clear do not flush the TLBs directly if the mm has
 * only one user. At the end of the update the flush_tlb_mm and
 * flush_tlb_range functions need to do the flush.
 */
#define flush_tlb()				do { } while (0)
#define flush_tlb_all()				do { } while (0)
#define flush_tlb_page(vma, addr)		do { } while (0)

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	__tlb_flush_mm_lazy(mm);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	__tlb_flush_mm_lazy(vma->vm_mm);
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	__tlb_flush_kernel();
}

#endif /* _S390_TLBFLUSH_H */
