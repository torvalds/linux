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
	/* Global TLB flush for the mm */
	asm volatile(
		"	.insn	rrf,0xb98e0000,0,%0,%1,0"
		: : "a" (2048), "a" (asce) : "cc");
}

/*
 * Flush TLB entries for a specific ASCE on the local CPU
 */
static inline void __tlb_flush_idte_local(unsigned long asce)
{
	/* Local TLB flush for the mm */
	asm volatile(
		"	.insn	rrf,0xb98e0000,0,%0,%1,1"
		: : "a" (2048), "a" (asce) : "cc");
}

#ifdef CONFIG_SMP
void smp_ptlb_all(void);

/*
 * Flush all TLB entries on all CPUs.
 */
static inline void __tlb_flush_global(void)
{
	register unsigned long reg2 asm("2");
	register unsigned long reg3 asm("3");
	register unsigned long reg4 asm("4");
	long dummy;

	dummy = 0;
	reg2 = reg3 = 0;
	reg4 = ((unsigned long) &dummy) + 1;
	asm volatile(
		"	csp	%0,%2"
		: : "d" (reg2), "d" (reg3), "d" (reg4), "m" (dummy) : "cc" );
}

/*
 * Flush TLB entries for a specific mm on all CPUs (in case gmap is used
 * this implicates multiple ASCEs!).
 */
static inline void __tlb_flush_full(struct mm_struct *mm)
{
	preempt_disable();
	atomic_add(0x10000, &mm->context.attach_count);
	if (cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id()))) {
		/* Local TLB flush */
		__tlb_flush_local();
	} else {
		/* Global TLB flush */
		__tlb_flush_global();
		/* Reset TLB flush mask */
		if (MACHINE_HAS_TLB_LC)
			cpumask_copy(mm_cpumask(mm),
				     &mm->context.cpu_attach_mask);
	}
	atomic_sub(0x10000, &mm->context.attach_count);
	preempt_enable();
}

/*
 * Flush TLB entries for a specific ASCE on all CPUs.
 */
static inline void __tlb_flush_asce(struct mm_struct *mm, unsigned long asce)
{
	int active, count;

	preempt_disable();
	active = (mm == current->active_mm) ? 1 : 0;
	count = atomic_add_return(0x10000, &mm->context.attach_count);
	if (MACHINE_HAS_TLB_LC && (count & 0xffff) <= active &&
	    cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id()))) {
		__tlb_flush_idte_local(asce);
	} else {
		if (MACHINE_HAS_IDTE)
			__tlb_flush_idte(asce);
		else
			__tlb_flush_global();
		/* Reset TLB flush mask */
		if (MACHINE_HAS_TLB_LC)
			cpumask_copy(mm_cpumask(mm),
				     &mm->context.cpu_attach_mask);
	}
	atomic_sub(0x10000, &mm->context.attach_count);
	preempt_enable();
}

static inline void __tlb_flush_kernel(void)
{
	if (MACHINE_HAS_IDTE)
		__tlb_flush_idte((unsigned long) init_mm.pgd |
				 init_mm.context.asce_bits);
	else
		__tlb_flush_global();
}
#else
#define __tlb_flush_global()	__tlb_flush_local()
#define __tlb_flush_full(mm)	__tlb_flush_local()

/*
 * Flush TLB entries for a specific ASCE on all CPUs.
 */
static inline void __tlb_flush_asce(struct mm_struct *mm, unsigned long asce)
{
	if (MACHINE_HAS_TLB_LC)
		__tlb_flush_idte_local(asce);
	else
		__tlb_flush_local();
}

static inline void __tlb_flush_kernel(void)
{
	if (MACHINE_HAS_TLB_LC)
		__tlb_flush_idte_local((unsigned long) init_mm.pgd |
				       init_mm.context.asce_bits);
	else
		__tlb_flush_local();
}
#endif

static inline void __tlb_flush_mm(struct mm_struct * mm)
{
	/*
	 * If the machine has IDTE we prefer to do a per mm flush
	 * on all cpus instead of doing a local flush if the mm
	 * only ran on the local cpu.
	 */
	if (MACHINE_HAS_IDTE && list_empty(&mm->context.gmap_list))
		__tlb_flush_asce(mm, (unsigned long) mm->pgd |
				 mm->context.asce_bits);
	else
		__tlb_flush_full(mm);
}

static inline void __tlb_flush_mm_lazy(struct mm_struct * mm)
{
	if (mm->context.flush_mm) {
		__tlb_flush_mm(mm);
		mm->context.flush_mm = 0;
	}
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
