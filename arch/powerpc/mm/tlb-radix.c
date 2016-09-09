/*
 * TLB flush routines for radix kernels.
 *
 * Copyright 2015-2016, Aneesh Kumar K.V, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/memblock.h>

#include <asm/tlb.h>
#include <asm/tlbflush.h>

static DEFINE_RAW_SPINLOCK(native_tlbie_lock);

#define RIC_FLUSH_TLB 0
#define RIC_FLUSH_PWC 1
#define RIC_FLUSH_ALL 2

static inline void __tlbiel_pid(unsigned long pid, int set,
				unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = PPC_BIT(53); /* IS = 1 */
	rb |= set << PPC_BITLSHIFT(51);
	rs = ((unsigned long)pid) << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* raidx format */

	asm volatile("ptesync": : :"memory");
	asm volatile(".long 0x7c000224 | (%0 << 11) | (%1 << 16) |"
		     "(%2 << 17) | (%3 << 18) | (%4 << 21)"
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	asm volatile("ptesync": : :"memory");
}

/*
 * We use 128 set in radix mode and 256 set in hpt mode.
 */
static inline void _tlbiel_pid(unsigned long pid, unsigned long ric)
{
	int set;

	for (set = 0; set < POWER9_TLB_SETS_RADIX ; set++) {
		__tlbiel_pid(pid, set, ric);
	}
	return;
}

static inline void _tlbie_pid(unsigned long pid, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = PPC_BIT(53); /* IS = 1 */
	rs = pid << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* raidx format */

	asm volatile("ptesync": : :"memory");
	asm volatile(".long 0x7c000264 | (%0 << 11) | (%1 << 16) |"
		     "(%2 << 17) | (%3 << 18) | (%4 << 21)"
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

static inline void _tlbiel_va(unsigned long va, unsigned long pid,
			      unsigned long ap, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = va & ~(PPC_BITMASK(52, 63));
	rb |= ap << PPC_BITLSHIFT(58);
	rs = pid << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* raidx format */

	asm volatile("ptesync": : :"memory");
	asm volatile(".long 0x7c000224 | (%0 << 11) | (%1 << 16) |"
		     "(%2 << 17) | (%3 << 18) | (%4 << 21)"
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	asm volatile("ptesync": : :"memory");
}

static inline void _tlbie_va(unsigned long va, unsigned long pid,
			     unsigned long ap, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = va & ~(PPC_BITMASK(52, 63));
	rb |= ap << PPC_BITLSHIFT(58);
	rs = pid << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* raidx format */

	asm volatile("ptesync": : :"memory");
	asm volatile(".long 0x7c000264 | (%0 << 11) | (%1 << 16) |"
		     "(%2 << 17) | (%3 << 18) | (%4 << 21)"
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

/*
 * Base TLB flushing operations:
 *
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes kernel pages
 *
 *  - local_* variants of page and mm only apply to the current
 *    processor
 */
void radix__local_flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long pid;

	preempt_disable();
	pid = mm->context.id;
	if (pid != MMU_NO_CONTEXT)
		_tlbiel_pid(pid, RIC_FLUSH_ALL);
	preempt_enable();
}
EXPORT_SYMBOL(radix__local_flush_tlb_mm);

void radix__local_flush_tlb_pwc(struct mmu_gather *tlb, unsigned long addr)
{
	unsigned long pid;
	struct mm_struct *mm = tlb->mm;

	preempt_disable();

	pid = mm->context.id;
	if (pid != MMU_NO_CONTEXT)
		_tlbiel_pid(pid, RIC_FLUSH_PWC);

	preempt_enable();
}
EXPORT_SYMBOL(radix__local_flush_tlb_pwc);

void radix___local_flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
			    unsigned long ap, int nid)
{
	unsigned long pid;

	preempt_disable();
	pid = mm ? mm->context.id : 0;
	if (pid != MMU_NO_CONTEXT)
		_tlbiel_va(vmaddr, pid, ap, RIC_FLUSH_TLB);
	preempt_enable();
}

void radix__local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
#ifdef CONFIG_HUGETLB_PAGE
	/* need the return fix for nohash.c */
	if (vma && is_vm_hugetlb_page(vma))
		return __local_flush_hugetlb_page(vma, vmaddr);
#endif
	radix___local_flush_tlb_page(vma ? vma->vm_mm : NULL, vmaddr,
			       mmu_get_ap(mmu_virtual_psize), 0);
}
EXPORT_SYMBOL(radix__local_flush_tlb_page);

#ifdef CONFIG_SMP
static int mm_is_core_local(struct mm_struct *mm)
{
	return cpumask_subset(mm_cpumask(mm),
			      topology_sibling_cpumask(smp_processor_id()));
}

void radix__flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long pid;

	preempt_disable();
	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		goto no_context;

	if (!mm_is_core_local(mm)) {
		int lock_tlbie = !mmu_has_feature(MMU_FTR_LOCKLESS_TLBIE);

		if (lock_tlbie)
			raw_spin_lock(&native_tlbie_lock);
		_tlbie_pid(pid, RIC_FLUSH_ALL);
		if (lock_tlbie)
			raw_spin_unlock(&native_tlbie_lock);
	} else
		_tlbiel_pid(pid, RIC_FLUSH_ALL);
no_context:
	preempt_enable();
}
EXPORT_SYMBOL(radix__flush_tlb_mm);

void radix__flush_tlb_pwc(struct mmu_gather *tlb, unsigned long addr)
{
	unsigned long pid;
	struct mm_struct *mm = tlb->mm;

	preempt_disable();

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		goto no_context;

	if (!mm_is_core_local(mm)) {
		int lock_tlbie = !mmu_has_feature(MMU_FTR_LOCKLESS_TLBIE);

		if (lock_tlbie)
			raw_spin_lock(&native_tlbie_lock);
		_tlbie_pid(pid, RIC_FLUSH_PWC);
		if (lock_tlbie)
			raw_spin_unlock(&native_tlbie_lock);
	} else
		_tlbiel_pid(pid, RIC_FLUSH_PWC);
no_context:
	preempt_enable();
}
EXPORT_SYMBOL(radix__flush_tlb_pwc);

void radix___flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
		       unsigned long ap, int nid)
{
	unsigned long pid;

	preempt_disable();
	pid = mm ? mm->context.id : 0;
	if (unlikely(pid == MMU_NO_CONTEXT))
		goto bail;
	if (!mm_is_core_local(mm)) {
		int lock_tlbie = !mmu_has_feature(MMU_FTR_LOCKLESS_TLBIE);

		if (lock_tlbie)
			raw_spin_lock(&native_tlbie_lock);
		_tlbie_va(vmaddr, pid, ap, RIC_FLUSH_TLB);
		if (lock_tlbie)
			raw_spin_unlock(&native_tlbie_lock);
	} else
		_tlbiel_va(vmaddr, pid, ap, RIC_FLUSH_TLB);
bail:
	preempt_enable();
}

void radix__flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
#ifdef CONFIG_HUGETLB_PAGE
	if (vma && is_vm_hugetlb_page(vma))
		return flush_hugetlb_page(vma, vmaddr);
#endif
	radix___flush_tlb_page(vma ? vma->vm_mm : NULL, vmaddr,
			 mmu_get_ap(mmu_virtual_psize), 0);
}
EXPORT_SYMBOL(radix__flush_tlb_page);

#endif /* CONFIG_SMP */

void radix__flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	int lock_tlbie = !mmu_has_feature(MMU_FTR_LOCKLESS_TLBIE);

	if (lock_tlbie)
		raw_spin_lock(&native_tlbie_lock);
	_tlbie_pid(0, RIC_FLUSH_ALL);
	if (lock_tlbie)
		raw_spin_unlock(&native_tlbie_lock);
}
EXPORT_SYMBOL(radix__flush_tlb_kernel_range);

/*
 * Currently, for range flushing, we just do a full mm flush. Because
 * we use this in code path where we don' track the page size.
 */
void radix__flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)

{
	struct mm_struct *mm = vma->vm_mm;
	radix__flush_tlb_mm(mm);
}
EXPORT_SYMBOL(radix__flush_tlb_range);


void radix__tlb_flush(struct mmu_gather *tlb)
{
	struct mm_struct *mm = tlb->mm;
	radix__flush_tlb_mm(mm);
}
