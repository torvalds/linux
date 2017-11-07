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

#include <asm/ppc-opcode.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/trace.h>
#include <asm/cputhreads.h>

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

	asm volatile(PPC_TLBIEL(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(0, 1, rb, rs, ric, prs, r);
}

/*
 * We use 128 set in radix mode and 256 set in hpt mode.
 */
static inline void _tlbiel_pid(unsigned long pid, unsigned long ric)
{
	int set;

	asm volatile("ptesync": : :"memory");

	/*
	 * Flush the first set of the TLB, and if we're doing a RIC_FLUSH_ALL,
	 * also flush the entire Page Walk Cache.
	 */
	__tlbiel_pid(pid, 0, ric);

	/* For PWC, only one flush is needed */
	if (ric == RIC_FLUSH_PWC) {
		asm volatile("ptesync": : :"memory");
		return;
	}

	/* For the remaining sets, just flush the TLB */
	for (set = 1; set < POWER9_TLB_SETS_RADIX ; set++)
		__tlbiel_pid(pid, set, RIC_FLUSH_TLB);

	asm volatile("ptesync": : :"memory");
	asm volatile(PPC_INVALIDATE_ERAT "; isync" : : :"memory");
}

static inline void _tlbie_pid(unsigned long pid, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = PPC_BIT(53); /* IS = 1 */
	rs = pid << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* raidx format */

	asm volatile("ptesync": : :"memory");
	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
	trace_tlbie(0, 0, rb, rs, ric, prs, r);
}

static inline void __tlbiel_va(unsigned long va, unsigned long pid,
			       unsigned long ap, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = va & ~(PPC_BITMASK(52, 63));
	rb |= ap << PPC_BITLSHIFT(58);
	rs = pid << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* raidx format */

	asm volatile(PPC_TLBIEL(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(0, 1, rb, rs, ric, prs, r);
}

static inline void _tlbiel_va(unsigned long va, unsigned long pid,
			      unsigned long psize, unsigned long ric)
{
	unsigned long ap = mmu_get_ap(psize);

	asm volatile("ptesync": : :"memory");
	__tlbiel_va(va, pid, ap, ric);
	asm volatile("ptesync": : :"memory");
}

static inline void _tlbiel_va_range(unsigned long start, unsigned long end,
				    unsigned long pid, unsigned long page_size,
				    unsigned long psize)
{
	unsigned long addr;
	unsigned long ap = mmu_get_ap(psize);

	asm volatile("ptesync": : :"memory");
	for (addr = start; addr < end; addr += page_size)
		__tlbiel_va(addr, pid, ap, RIC_FLUSH_TLB);
	asm volatile("ptesync": : :"memory");
}

static inline void __tlbie_va(unsigned long va, unsigned long pid,
			     unsigned long ap, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = va & ~(PPC_BITMASK(52, 63));
	rb |= ap << PPC_BITLSHIFT(58);
	rs = pid << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* raidx format */

	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(0, 0, rb, rs, ric, prs, r);
}

static inline void _tlbie_va(unsigned long va, unsigned long pid,
			      unsigned long psize, unsigned long ric)
{
	unsigned long ap = mmu_get_ap(psize);

	asm volatile("ptesync": : :"memory");
	__tlbie_va(va, pid, ap, ric);
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

static inline void _tlbie_va_range(unsigned long start, unsigned long end,
				    unsigned long pid, unsigned long page_size,
				    unsigned long psize)
{
	unsigned long addr;
	unsigned long ap = mmu_get_ap(psize);

	asm volatile("ptesync": : :"memory");
	for (addr = start; addr < end; addr += page_size)
		__tlbie_va(addr, pid, ap, RIC_FLUSH_TLB);
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
		_tlbiel_pid(pid, RIC_FLUSH_TLB);
	preempt_enable();
}
EXPORT_SYMBOL(radix__local_flush_tlb_mm);

#ifndef CONFIG_SMP
void radix__local_flush_all_mm(struct mm_struct *mm)
{
	unsigned long pid;

	preempt_disable();
	pid = mm->context.id;
	if (pid != MMU_NO_CONTEXT)
		_tlbiel_pid(pid, RIC_FLUSH_ALL);
	preempt_enable();
}
EXPORT_SYMBOL(radix__local_flush_all_mm);
#endif /* CONFIG_SMP */

void radix__local_flush_tlb_page_psize(struct mm_struct *mm, unsigned long vmaddr,
				       int psize)
{
	unsigned long pid;

	preempt_disable();
	pid = mm->context.id;
	if (pid != MMU_NO_CONTEXT)
		_tlbiel_va(vmaddr, pid, psize, RIC_FLUSH_TLB);
	preempt_enable();
}

void radix__local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
#ifdef CONFIG_HUGETLB_PAGE
	/* need the return fix for nohash.c */
	if (is_vm_hugetlb_page(vma))
		return radix__local_flush_hugetlb_page(vma, vmaddr);
#endif
	radix__local_flush_tlb_page_psize(vma->vm_mm, vmaddr, mmu_virtual_psize);
}
EXPORT_SYMBOL(radix__local_flush_tlb_page);

#ifdef CONFIG_SMP
void radix__flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long pid;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	preempt_disable();
	if (!mm_is_thread_local(mm))
		_tlbie_pid(pid, RIC_FLUSH_TLB);
	else
		_tlbiel_pid(pid, RIC_FLUSH_TLB);
	preempt_enable();
}
EXPORT_SYMBOL(radix__flush_tlb_mm);

void radix__flush_all_mm(struct mm_struct *mm)
{
	unsigned long pid;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	preempt_disable();
	if (!mm_is_thread_local(mm))
		_tlbie_pid(pid, RIC_FLUSH_ALL);
	else
		_tlbiel_pid(pid, RIC_FLUSH_ALL);
	preempt_enable();
}
EXPORT_SYMBOL(radix__flush_all_mm);

void radix__flush_tlb_pwc(struct mmu_gather *tlb, unsigned long addr)
{
	tlb->need_flush_all = 1;
}
EXPORT_SYMBOL(radix__flush_tlb_pwc);

void radix__flush_tlb_page_psize(struct mm_struct *mm, unsigned long vmaddr,
				 int psize)
{
	unsigned long pid;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	preempt_disable();
	if (!mm_is_thread_local(mm))
		_tlbie_va(vmaddr, pid, psize, RIC_FLUSH_TLB);
	else
		_tlbiel_va(vmaddr, pid, psize, RIC_FLUSH_TLB);
	preempt_enable();
}

void radix__flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
#ifdef CONFIG_HUGETLB_PAGE
	if (is_vm_hugetlb_page(vma))
		return radix__flush_hugetlb_page(vma, vmaddr);
#endif
	radix__flush_tlb_page_psize(vma->vm_mm, vmaddr, mmu_virtual_psize);
}
EXPORT_SYMBOL(radix__flush_tlb_page);

#else /* CONFIG_SMP */
#define radix__flush_all_mm radix__local_flush_all_mm
#endif /* CONFIG_SMP */

void radix__flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	_tlbie_pid(0, RIC_FLUSH_ALL);
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

static int radix_get_mmu_psize(int page_size)
{
	int psize;

	if (page_size == (1UL << mmu_psize_defs[mmu_virtual_psize].shift))
		psize = mmu_virtual_psize;
	else if (page_size == (1UL << mmu_psize_defs[MMU_PAGE_2M].shift))
		psize = MMU_PAGE_2M;
	else if (page_size == (1UL << mmu_psize_defs[MMU_PAGE_1G].shift))
		psize = MMU_PAGE_1G;
	else
		return -1;
	return psize;
}

void radix__tlb_flush(struct mmu_gather *tlb)
{
	int psize = 0;
	struct mm_struct *mm = tlb->mm;
	int page_size = tlb->page_size;

	psize = radix_get_mmu_psize(page_size);
	/*
	 * if page size is not something we understand, do a full mm flush
	 *
	 * A "fullmm" flush must always do a flush_all_mm (RIC=2) flush
	 * that flushes the process table entry cache upon process teardown.
	 * See the comment for radix in arch_exit_mmap().
	 */
	if (psize != -1 && !tlb->fullmm && !tlb->need_flush_all)
		radix__flush_tlb_range_psize(mm, tlb->start, tlb->end, psize);
	else if (tlb->fullmm || tlb->need_flush_all) {
		tlb->need_flush_all = 0;
		radix__flush_all_mm(mm);
	} else
		radix__flush_tlb_mm(mm);
}

#define TLB_FLUSH_ALL -1UL
/*
 * Number of pages above which we will do a bcast tlbie. Just a
 * number at this point copied from x86
 */
static unsigned long tlb_single_page_flush_ceiling __read_mostly = 33;

void radix__flush_tlb_range_psize(struct mm_struct *mm, unsigned long start,
				  unsigned long end, int psize)
{
	unsigned long pid;
	bool local;
	unsigned long page_size = 1UL << mmu_psize_defs[psize].shift;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	preempt_disable();
	local = mm_is_thread_local(mm);
	if (end == TLB_FLUSH_ALL ||
	    (end - start) > tlb_single_page_flush_ceiling * page_size) {
		if (local)
			_tlbiel_pid(pid, RIC_FLUSH_TLB);
		else
			_tlbie_pid(pid, RIC_FLUSH_TLB);
	} else {
		if (local)
			_tlbiel_va_range(start, end, pid, page_size, psize);
		else
			_tlbie_va_range(start, end, pid, page_size, psize);
	}
	preempt_enable();
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void radix__flush_tlb_collapsed_pmd(struct mm_struct *mm, unsigned long addr)
{
	unsigned long pid, end;
	bool local;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	/* 4k page size, just blow the world */
	if (PAGE_SIZE == 0x1000) {
		radix__flush_all_mm(mm);
		return;
	}

	preempt_disable();
	local = mm_is_thread_local(mm);
	/* Otherwise first do the PWC */
	if (local)
		_tlbiel_pid(pid, RIC_FLUSH_PWC);
	else
		_tlbie_pid(pid, RIC_FLUSH_PWC);

	/* Then iterate the pages */
	end = addr + HPAGE_PMD_SIZE;
	if (local)
		_tlbiel_va_range(addr, end, pid, PAGE_SIZE, mmu_virtual_psize);
	else
		_tlbie_va_range(addr, end, pid, PAGE_SIZE, mmu_virtual_psize);

	preempt_enable();
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

void radix__flush_tlb_lpid_va(unsigned long lpid, unsigned long gpa,
			      unsigned long page_size)
{
	unsigned long rb,rs,prs,r;
	unsigned long ap;
	unsigned long ric = RIC_FLUSH_TLB;

	ap = mmu_get_ap(radix_get_mmu_psize(page_size));
	rb = gpa & ~(PPC_BITMASK(52, 63));
	rb |= ap << PPC_BITLSHIFT(58);
	rs = lpid & ((1UL << 32) - 1);
	prs = 0; /* process scoped */
	r = 1;   /* raidx format */

	asm volatile("ptesync": : :"memory");
	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
	trace_tlbie(lpid, 0, rb, rs, ric, prs, r);
}
EXPORT_SYMBOL(radix__flush_tlb_lpid_va);

void radix__flush_tlb_lpid(unsigned long lpid)
{
	unsigned long rb,rs,prs,r;
	unsigned long ric = RIC_FLUSH_ALL;

	rb = 0x2 << PPC_BITLSHIFT(53); /* IS = 2 */
	rs = lpid & ((1UL << 32) - 1);
	prs = 0; /* partition scoped */
	r = 1;   /* raidx format */

	asm volatile("ptesync": : :"memory");
	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
	trace_tlbie(lpid, 0, rb, rs, ric, prs, r);
}
EXPORT_SYMBOL(radix__flush_tlb_lpid);

void radix__flush_pmd_tlb_range(struct vm_area_struct *vma,
				unsigned long start, unsigned long end)
{
	radix__flush_tlb_range_psize(vma->vm_mm, start, end, MMU_PAGE_2M);
}
EXPORT_SYMBOL(radix__flush_pmd_tlb_range);

void radix__flush_tlb_all(void)
{
	unsigned long rb,prs,r,rs;
	unsigned long ric = RIC_FLUSH_ALL;

	rb = 0x3 << PPC_BITLSHIFT(53); /* IS = 3 */
	prs = 0; /* partition scoped */
	r = 1;   /* raidx format */
	rs = 1 & ((1UL << 32) - 1); /* any LPID value to flush guest mappings */

	asm volatile("ptesync": : :"memory");
	/*
	 * now flush guest entries by passing PRS = 1 and LPID != 0
	 */
	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(1), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(0, 0, rb, rs, ric, prs, r);
	/*
	 * now flush host entires by passing PRS = 0 and LPID == 0
	 */
	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(0) : "memory");
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
	trace_tlbie(0, 0, rb, 0, ric, prs, r);
}

void radix__flush_tlb_pte_p9_dd1(unsigned long old_pte, struct mm_struct *mm,
				 unsigned long address)
{
	/*
	 * We track page size in pte only for DD1, So we can
	 * call this only on DD1.
	 */
	if (!cpu_has_feature(CPU_FTR_POWER9_DD1)) {
		VM_WARN_ON(1);
		return;
	}

	if (old_pte & R_PAGE_LARGE)
		radix__flush_tlb_page_psize(mm, address, MMU_PAGE_2M);
	else
		radix__flush_tlb_page_psize(mm, address, mmu_virtual_psize);
}

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
extern void radix_kvm_prefetch_workaround(struct mm_struct *mm)
{
	unsigned int pid = mm->context.id;

	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	/*
	 * If this context hasn't run on that CPU before and KVM is
	 * around, there's a slim chance that the guest on another
	 * CPU just brought in obsolete translation into the TLB of
	 * this CPU due to a bad prefetch using the guest PID on
	 * the way into the hypervisor.
	 *
	 * We work around this here. If KVM is possible, we check if
	 * any sibling thread is in KVM. If it is, the window may exist
	 * and thus we flush that PID from the core.
	 *
	 * A potential future improvement would be to mark which PIDs
	 * have never been used on the system and avoid it if the PID
	 * is new and the process has no other cpumask bit set.
	 */
	if (cpu_has_feature(CPU_FTR_HVMODE) && radix_enabled()) {
		int cpu = smp_processor_id();
		int sib = cpu_first_thread_sibling(cpu);
		bool flush = false;

		for (; sib <= cpu_last_thread_sibling(cpu) && !flush; sib++) {
			if (sib == cpu)
				continue;
			if (paca[sib].kvm_hstate.kvm_vcpu)
				flush = true;
		}
		if (flush)
			_tlbiel_pid(pid, RIC_FLUSH_ALL);
	}
}
EXPORT_SYMBOL_GPL(radix_kvm_prefetch_workaround);
#endif /* CONFIG_KVM_BOOK3S_HV_POSSIBLE */
