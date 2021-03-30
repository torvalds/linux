// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TLB flush routines for radix kernels.
 *
 * Copyright 2015-2016, Aneesh Kumar K.V, IBM Corporation.
 */

#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/memblock.h>
#include <linux/mmu_context.h>
#include <linux/sched/mm.h>

#include <asm/ppc-opcode.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/trace.h>
#include <asm/cputhreads.h>
#include <asm/plpar_wrappers.h>

#define RIC_FLUSH_TLB 0
#define RIC_FLUSH_PWC 1
#define RIC_FLUSH_ALL 2

/*
 * tlbiel instruction for radix, set invalidation
 * i.e., r=1 and is=01 or is=10 or is=11
 */
static __always_inline void tlbiel_radix_set_isa300(unsigned int set, unsigned int is,
					unsigned int pid,
					unsigned int ric, unsigned int prs)
{
	unsigned long rb;
	unsigned long rs;

	rb = (set << PPC_BITLSHIFT(51)) | (is << PPC_BITLSHIFT(53));
	rs = ((unsigned long)pid << PPC_BITLSHIFT(31));

	asm volatile(PPC_TLBIEL(%0, %1, %2, %3, 1)
		     : : "r"(rb), "r"(rs), "i"(ric), "i"(prs)
		     : "memory");
}

static void tlbiel_all_isa300(unsigned int num_sets, unsigned int is)
{
	unsigned int set;

	asm volatile("ptesync": : :"memory");

	/*
	 * Flush the first set of the TLB, and the entire Page Walk Cache
	 * and partition table entries. Then flush the remaining sets of the
	 * TLB.
	 */

	if (early_cpu_has_feature(CPU_FTR_HVMODE)) {
		/* MSR[HV] should flush partition scope translations first. */
		tlbiel_radix_set_isa300(0, is, 0, RIC_FLUSH_ALL, 0);

		if (!early_cpu_has_feature(CPU_FTR_ARCH_31)) {
			for (set = 1; set < num_sets; set++)
				tlbiel_radix_set_isa300(set, is, 0,
							RIC_FLUSH_TLB, 0);
		}
	}

	/* Flush process scoped entries. */
	tlbiel_radix_set_isa300(0, is, 0, RIC_FLUSH_ALL, 1);

	if (!early_cpu_has_feature(CPU_FTR_ARCH_31)) {
		for (set = 1; set < num_sets; set++)
			tlbiel_radix_set_isa300(set, is, 0, RIC_FLUSH_TLB, 1);
	}

	ppc_after_tlbiel_barrier();
}

void radix__tlbiel_all(unsigned int action)
{
	unsigned int is;

	switch (action) {
	case TLB_INVAL_SCOPE_GLOBAL:
		is = 3;
		break;
	case TLB_INVAL_SCOPE_LPID:
		is = 2;
		break;
	default:
		BUG();
	}

	if (early_cpu_has_feature(CPU_FTR_ARCH_300))
		tlbiel_all_isa300(POWER9_TLB_SETS_RADIX, is);
	else
		WARN(1, "%s called on pre-POWER9 CPU\n", __func__);

	asm volatile(PPC_ISA_3_0_INVALIDATE_ERAT "; isync" : : :"memory");
}

static __always_inline void __tlbiel_pid(unsigned long pid, int set,
				unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = PPC_BIT(53); /* IS = 1 */
	rb |= set << PPC_BITLSHIFT(51);
	rs = ((unsigned long)pid) << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* radix format */

	asm volatile(PPC_TLBIEL(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(0, 1, rb, rs, ric, prs, r);
}

static __always_inline void __tlbie_pid(unsigned long pid, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = PPC_BIT(53); /* IS = 1 */
	rs = pid << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* radix format */

	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(0, 0, rb, rs, ric, prs, r);
}

static __always_inline void __tlbie_lpid(unsigned long lpid, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = PPC_BIT(52); /* IS = 2 */
	rs = lpid;
	prs = 0; /* partition scoped */
	r = 1;   /* radix format */

	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(lpid, 0, rb, rs, ric, prs, r);
}

static __always_inline void __tlbie_lpid_guest(unsigned long lpid, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = PPC_BIT(52); /* IS = 2 */
	rs = lpid;
	prs = 1; /* process scoped */
	r = 1;   /* radix format */

	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(lpid, 0, rb, rs, ric, prs, r);
}

static __always_inline void __tlbiel_va(unsigned long va, unsigned long pid,
					unsigned long ap, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = va & ~(PPC_BITMASK(52, 63));
	rb |= ap << PPC_BITLSHIFT(58);
	rs = pid << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* radix format */

	asm volatile(PPC_TLBIEL(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(0, 1, rb, rs, ric, prs, r);
}

static __always_inline void __tlbie_va(unsigned long va, unsigned long pid,
				       unsigned long ap, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = va & ~(PPC_BITMASK(52, 63));
	rb |= ap << PPC_BITLSHIFT(58);
	rs = pid << PPC_BITLSHIFT(31);
	prs = 1; /* process scoped */
	r = 1;   /* radix format */

	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(0, 0, rb, rs, ric, prs, r);
}

static __always_inline void __tlbie_lpid_va(unsigned long va, unsigned long lpid,
					    unsigned long ap, unsigned long ric)
{
	unsigned long rb,rs,prs,r;

	rb = va & ~(PPC_BITMASK(52, 63));
	rb |= ap << PPC_BITLSHIFT(58);
	rs = lpid;
	prs = 0; /* partition scoped */
	r = 1;   /* radix format */

	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
	trace_tlbie(lpid, 0, rb, rs, ric, prs, r);
}


static inline void fixup_tlbie_va(unsigned long va, unsigned long pid,
				  unsigned long ap)
{
	if (cpu_has_feature(CPU_FTR_P9_TLBIE_ERAT_BUG)) {
		asm volatile("ptesync": : :"memory");
		__tlbie_va(va, 0, ap, RIC_FLUSH_TLB);
	}

	if (cpu_has_feature(CPU_FTR_P9_TLBIE_STQ_BUG)) {
		asm volatile("ptesync": : :"memory");
		__tlbie_va(va, pid, ap, RIC_FLUSH_TLB);
	}
}

static inline void fixup_tlbie_va_range(unsigned long va, unsigned long pid,
					unsigned long ap)
{
	if (cpu_has_feature(CPU_FTR_P9_TLBIE_ERAT_BUG)) {
		asm volatile("ptesync": : :"memory");
		__tlbie_pid(0, RIC_FLUSH_TLB);
	}

	if (cpu_has_feature(CPU_FTR_P9_TLBIE_STQ_BUG)) {
		asm volatile("ptesync": : :"memory");
		__tlbie_va(va, pid, ap, RIC_FLUSH_TLB);
	}
}

static inline void fixup_tlbie_pid(unsigned long pid)
{
	/*
	 * We can use any address for the invalidation, pick one which is
	 * probably unused as an optimisation.
	 */
	unsigned long va = ((1UL << 52) - 1);

	if (cpu_has_feature(CPU_FTR_P9_TLBIE_ERAT_BUG)) {
		asm volatile("ptesync": : :"memory");
		__tlbie_pid(0, RIC_FLUSH_TLB);
	}

	if (cpu_has_feature(CPU_FTR_P9_TLBIE_STQ_BUG)) {
		asm volatile("ptesync": : :"memory");
		__tlbie_va(va, pid, mmu_get_ap(MMU_PAGE_64K), RIC_FLUSH_TLB);
	}
}


static inline void fixup_tlbie_lpid_va(unsigned long va, unsigned long lpid,
				       unsigned long ap)
{
	if (cpu_has_feature(CPU_FTR_P9_TLBIE_ERAT_BUG)) {
		asm volatile("ptesync": : :"memory");
		__tlbie_lpid_va(va, 0, ap, RIC_FLUSH_TLB);
	}

	if (cpu_has_feature(CPU_FTR_P9_TLBIE_STQ_BUG)) {
		asm volatile("ptesync": : :"memory");
		__tlbie_lpid_va(va, lpid, ap, RIC_FLUSH_TLB);
	}
}

static inline void fixup_tlbie_lpid(unsigned long lpid)
{
	/*
	 * We can use any address for the invalidation, pick one which is
	 * probably unused as an optimisation.
	 */
	unsigned long va = ((1UL << 52) - 1);

	if (cpu_has_feature(CPU_FTR_P9_TLBIE_ERAT_BUG)) {
		asm volatile("ptesync": : :"memory");
		__tlbie_lpid(0, RIC_FLUSH_TLB);
	}

	if (cpu_has_feature(CPU_FTR_P9_TLBIE_STQ_BUG)) {
		asm volatile("ptesync": : :"memory");
		__tlbie_lpid_va(va, lpid, mmu_get_ap(MMU_PAGE_64K), RIC_FLUSH_TLB);
	}
}

/*
 * We use 128 set in radix mode and 256 set in hpt mode.
 */
static __always_inline void _tlbiel_pid(unsigned long pid, unsigned long ric)
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
		ppc_after_tlbiel_barrier();
		return;
	}

	if (!cpu_has_feature(CPU_FTR_ARCH_31)) {
		/* For the remaining sets, just flush the TLB */
		for (set = 1; set < POWER9_TLB_SETS_RADIX ; set++)
			__tlbiel_pid(pid, set, RIC_FLUSH_TLB);
	}

	ppc_after_tlbiel_barrier();
	asm volatile(PPC_RADIX_INVALIDATE_ERAT_USER "; isync" : : :"memory");
}

static inline void _tlbie_pid(unsigned long pid, unsigned long ric)
{
	asm volatile("ptesync": : :"memory");

	/*
	 * Workaround the fact that the "ric" argument to __tlbie_pid
	 * must be a compile-time contraint to match the "i" constraint
	 * in the asm statement.
	 */
	switch (ric) {
	case RIC_FLUSH_TLB:
		__tlbie_pid(pid, RIC_FLUSH_TLB);
		fixup_tlbie_pid(pid);
		break;
	case RIC_FLUSH_PWC:
		__tlbie_pid(pid, RIC_FLUSH_PWC);
		break;
	case RIC_FLUSH_ALL:
	default:
		__tlbie_pid(pid, RIC_FLUSH_ALL);
		fixup_tlbie_pid(pid);
	}
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

struct tlbiel_pid {
	unsigned long pid;
	unsigned long ric;
};

static void do_tlbiel_pid(void *info)
{
	struct tlbiel_pid *t = info;

	if (t->ric == RIC_FLUSH_TLB)
		_tlbiel_pid(t->pid, RIC_FLUSH_TLB);
	else if (t->ric == RIC_FLUSH_PWC)
		_tlbiel_pid(t->pid, RIC_FLUSH_PWC);
	else
		_tlbiel_pid(t->pid, RIC_FLUSH_ALL);
}

static inline void _tlbiel_pid_multicast(struct mm_struct *mm,
				unsigned long pid, unsigned long ric)
{
	struct cpumask *cpus = mm_cpumask(mm);
	struct tlbiel_pid t = { .pid = pid, .ric = ric };

	on_each_cpu_mask(cpus, do_tlbiel_pid, &t, 1);
	/*
	 * Always want the CPU translations to be invalidated with tlbiel in
	 * these paths, so while coprocessors must use tlbie, we can not
	 * optimise away the tlbiel component.
	 */
	if (atomic_read(&mm->context.copros) > 0)
		_tlbie_pid(pid, RIC_FLUSH_ALL);
}

static inline void _tlbie_lpid(unsigned long lpid, unsigned long ric)
{
	asm volatile("ptesync": : :"memory");

	/*
	 * Workaround the fact that the "ric" argument to __tlbie_pid
	 * must be a compile-time contraint to match the "i" constraint
	 * in the asm statement.
	 */
	switch (ric) {
	case RIC_FLUSH_TLB:
		__tlbie_lpid(lpid, RIC_FLUSH_TLB);
		fixup_tlbie_lpid(lpid);
		break;
	case RIC_FLUSH_PWC:
		__tlbie_lpid(lpid, RIC_FLUSH_PWC);
		break;
	case RIC_FLUSH_ALL:
	default:
		__tlbie_lpid(lpid, RIC_FLUSH_ALL);
		fixup_tlbie_lpid(lpid);
	}
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

static __always_inline void _tlbie_lpid_guest(unsigned long lpid, unsigned long ric)
{
	/*
	 * Workaround the fact that the "ric" argument to __tlbie_pid
	 * must be a compile-time contraint to match the "i" constraint
	 * in the asm statement.
	 */
	switch (ric) {
	case RIC_FLUSH_TLB:
		__tlbie_lpid_guest(lpid, RIC_FLUSH_TLB);
		break;
	case RIC_FLUSH_PWC:
		__tlbie_lpid_guest(lpid, RIC_FLUSH_PWC);
		break;
	case RIC_FLUSH_ALL:
	default:
		__tlbie_lpid_guest(lpid, RIC_FLUSH_ALL);
	}
	fixup_tlbie_lpid(lpid);
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

static inline void __tlbiel_va_range(unsigned long start, unsigned long end,
				    unsigned long pid, unsigned long page_size,
				    unsigned long psize)
{
	unsigned long addr;
	unsigned long ap = mmu_get_ap(psize);

	for (addr = start; addr < end; addr += page_size)
		__tlbiel_va(addr, pid, ap, RIC_FLUSH_TLB);
}

static __always_inline void _tlbiel_va(unsigned long va, unsigned long pid,
				       unsigned long psize, unsigned long ric)
{
	unsigned long ap = mmu_get_ap(psize);

	asm volatile("ptesync": : :"memory");
	__tlbiel_va(va, pid, ap, ric);
	ppc_after_tlbiel_barrier();
}

static inline void _tlbiel_va_range(unsigned long start, unsigned long end,
				    unsigned long pid, unsigned long page_size,
				    unsigned long psize, bool also_pwc)
{
	asm volatile("ptesync": : :"memory");
	if (also_pwc)
		__tlbiel_pid(pid, 0, RIC_FLUSH_PWC);
	__tlbiel_va_range(start, end, pid, page_size, psize);
	ppc_after_tlbiel_barrier();
}

static inline void __tlbie_va_range(unsigned long start, unsigned long end,
				    unsigned long pid, unsigned long page_size,
				    unsigned long psize)
{
	unsigned long addr;
	unsigned long ap = mmu_get_ap(psize);

	for (addr = start; addr < end; addr += page_size)
		__tlbie_va(addr, pid, ap, RIC_FLUSH_TLB);

	fixup_tlbie_va_range(addr - page_size, pid, ap);
}

static __always_inline void _tlbie_va(unsigned long va, unsigned long pid,
				      unsigned long psize, unsigned long ric)
{
	unsigned long ap = mmu_get_ap(psize);

	asm volatile("ptesync": : :"memory");
	__tlbie_va(va, pid, ap, ric);
	fixup_tlbie_va(va, pid, ap);
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

struct tlbiel_va {
	unsigned long pid;
	unsigned long va;
	unsigned long psize;
	unsigned long ric;
};

static void do_tlbiel_va(void *info)
{
	struct tlbiel_va *t = info;

	if (t->ric == RIC_FLUSH_TLB)
		_tlbiel_va(t->va, t->pid, t->psize, RIC_FLUSH_TLB);
	else if (t->ric == RIC_FLUSH_PWC)
		_tlbiel_va(t->va, t->pid, t->psize, RIC_FLUSH_PWC);
	else
		_tlbiel_va(t->va, t->pid, t->psize, RIC_FLUSH_ALL);
}

static inline void _tlbiel_va_multicast(struct mm_struct *mm,
				unsigned long va, unsigned long pid,
				unsigned long psize, unsigned long ric)
{
	struct cpumask *cpus = mm_cpumask(mm);
	struct tlbiel_va t = { .va = va, .pid = pid, .psize = psize, .ric = ric };
	on_each_cpu_mask(cpus, do_tlbiel_va, &t, 1);
	if (atomic_read(&mm->context.copros) > 0)
		_tlbie_va(va, pid, psize, RIC_FLUSH_TLB);
}

struct tlbiel_va_range {
	unsigned long pid;
	unsigned long start;
	unsigned long end;
	unsigned long page_size;
	unsigned long psize;
	bool also_pwc;
};

static void do_tlbiel_va_range(void *info)
{
	struct tlbiel_va_range *t = info;

	_tlbiel_va_range(t->start, t->end, t->pid, t->page_size,
				    t->psize, t->also_pwc);
}

static __always_inline void _tlbie_lpid_va(unsigned long va, unsigned long lpid,
			      unsigned long psize, unsigned long ric)
{
	unsigned long ap = mmu_get_ap(psize);

	asm volatile("ptesync": : :"memory");
	__tlbie_lpid_va(va, lpid, ap, ric);
	fixup_tlbie_lpid_va(va, lpid, ap);
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

static inline void _tlbie_va_range(unsigned long start, unsigned long end,
				    unsigned long pid, unsigned long page_size,
				    unsigned long psize, bool also_pwc)
{
	asm volatile("ptesync": : :"memory");
	if (also_pwc)
		__tlbie_pid(pid, RIC_FLUSH_PWC);
	__tlbie_va_range(start, end, pid, page_size, psize);
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

static inline void _tlbiel_va_range_multicast(struct mm_struct *mm,
				unsigned long start, unsigned long end,
				unsigned long pid, unsigned long page_size,
				unsigned long psize, bool also_pwc)
{
	struct cpumask *cpus = mm_cpumask(mm);
	struct tlbiel_va_range t = { .start = start, .end = end,
				.pid = pid, .page_size = page_size,
				.psize = psize, .also_pwc = also_pwc };

	on_each_cpu_mask(cpus, do_tlbiel_va_range, &t, 1);
	if (atomic_read(&mm->context.copros) > 0)
		_tlbie_va_range(start, end, pid, page_size, psize, also_pwc);
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

static void __flush_all_mm(struct mm_struct *mm, bool fullmm)
{
	radix__local_flush_all_mm(mm);
}
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

static bool mm_is_singlethreaded(struct mm_struct *mm)
{
	if (atomic_read(&mm->context.copros) > 0)
		return false;
	if (atomic_read(&mm->mm_users) <= 1 && current->mm == mm)
		return true;
	return false;
}

static bool mm_needs_flush_escalation(struct mm_struct *mm)
{
	/*
	 * P9 nest MMU has issues with the page walk cache
	 * caching PTEs and not flushing them properly when
	 * RIC = 0 for a PID/LPID invalidate
	 */
	if (atomic_read(&mm->context.copros) > 0)
		return true;
	return false;
}

#ifdef CONFIG_SMP
static void do_exit_flush_lazy_tlb(void *arg)
{
	struct mm_struct *mm = arg;
	unsigned long pid = mm->context.id;

	/*
	 * A kthread could have done a mmget_not_zero() after the flushing CPU
	 * checked mm_is_singlethreaded, and be in the process of
	 * kthread_use_mm when interrupted here. In that case, current->mm will
	 * be set to mm, because kthread_use_mm() setting ->mm and switching to
	 * the mm is done with interrupts off.
	 */
	if (current->mm == mm)
		goto out_flush;

	if (current->active_mm == mm) {
		WARN_ON_ONCE(current->mm != NULL);
		/* Is a kernel thread and is using mm as the lazy tlb */
		mmgrab(&init_mm);
		current->active_mm = &init_mm;
		switch_mm_irqs_off(mm, &init_mm, current);
		mmdrop(mm);
	}

	atomic_dec(&mm->context.active_cpus);
	cpumask_clear_cpu(smp_processor_id(), mm_cpumask(mm));

out_flush:
	_tlbiel_pid(pid, RIC_FLUSH_ALL);
}

static void exit_flush_lazy_tlbs(struct mm_struct *mm)
{
	/*
	 * Would be nice if this was async so it could be run in
	 * parallel with our local flush, but generic code does not
	 * give a good API for it. Could extend the generic code or
	 * make a special powerpc IPI for flushing TLBs.
	 * For now it's not too performance critical.
	 */
	smp_call_function_many(mm_cpumask(mm), do_exit_flush_lazy_tlb,
				(void *)mm, 1);
}

void radix__flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long pid;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	preempt_disable();
	/*
	 * Order loads of mm_cpumask vs previous stores to clear ptes before
	 * the invalidate. See barrier in switch_mm_irqs_off
	 */
	smp_mb();
	if (!mm_is_thread_local(mm)) {
		if (unlikely(mm_is_singlethreaded(mm))) {
			exit_flush_lazy_tlbs(mm);
			goto local;
		}

		if (!mmu_has_feature(MMU_FTR_GTSE)) {
			unsigned long tgt = H_RPTI_TARGET_CMMU;

			if (atomic_read(&mm->context.copros) > 0)
				tgt |= H_RPTI_TARGET_NMMU;
			pseries_rpt_invalidate(pid, tgt, H_RPTI_TYPE_TLB,
					       H_RPTI_PAGE_ALL, 0, -1UL);
		} else if (cputlb_use_tlbie()) {
			if (mm_needs_flush_escalation(mm))
				_tlbie_pid(pid, RIC_FLUSH_ALL);
			else
				_tlbie_pid(pid, RIC_FLUSH_TLB);
		} else {
			_tlbiel_pid_multicast(mm, pid, RIC_FLUSH_TLB);
		}
	} else {
local:
		_tlbiel_pid(pid, RIC_FLUSH_TLB);
	}
	preempt_enable();
}
EXPORT_SYMBOL(radix__flush_tlb_mm);

static void __flush_all_mm(struct mm_struct *mm, bool fullmm)
{
	unsigned long pid;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	preempt_disable();
	smp_mb(); /* see radix__flush_tlb_mm */
	if (!mm_is_thread_local(mm)) {
		if (unlikely(mm_is_singlethreaded(mm))) {
			if (!fullmm) {
				exit_flush_lazy_tlbs(mm);
				goto local;
			}
		}
		if (!mmu_has_feature(MMU_FTR_GTSE)) {
			unsigned long tgt = H_RPTI_TARGET_CMMU;
			unsigned long type = H_RPTI_TYPE_TLB | H_RPTI_TYPE_PWC |
					     H_RPTI_TYPE_PRT;

			if (atomic_read(&mm->context.copros) > 0)
				tgt |= H_RPTI_TARGET_NMMU;
			pseries_rpt_invalidate(pid, tgt, type,
					       H_RPTI_PAGE_ALL, 0, -1UL);
		} else if (cputlb_use_tlbie())
			_tlbie_pid(pid, RIC_FLUSH_ALL);
		else
			_tlbiel_pid_multicast(mm, pid, RIC_FLUSH_ALL);
	} else {
local:
		_tlbiel_pid(pid, RIC_FLUSH_ALL);
	}
	preempt_enable();
}

void radix__flush_all_mm(struct mm_struct *mm)
{
	__flush_all_mm(mm, false);
}
EXPORT_SYMBOL(radix__flush_all_mm);

void radix__flush_tlb_page_psize(struct mm_struct *mm, unsigned long vmaddr,
				 int psize)
{
	unsigned long pid;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	preempt_disable();
	smp_mb(); /* see radix__flush_tlb_mm */
	if (!mm_is_thread_local(mm)) {
		if (unlikely(mm_is_singlethreaded(mm))) {
			exit_flush_lazy_tlbs(mm);
			goto local;
		}
		if (!mmu_has_feature(MMU_FTR_GTSE)) {
			unsigned long tgt, pg_sizes, size;

			tgt = H_RPTI_TARGET_CMMU;
			pg_sizes = psize_to_rpti_pgsize(psize);
			size = 1UL << mmu_psize_to_shift(psize);

			if (atomic_read(&mm->context.copros) > 0)
				tgt |= H_RPTI_TARGET_NMMU;
			pseries_rpt_invalidate(pid, tgt, H_RPTI_TYPE_TLB,
					       pg_sizes, vmaddr,
					       vmaddr + size);
		} else if (cputlb_use_tlbie())
			_tlbie_va(vmaddr, pid, psize, RIC_FLUSH_TLB);
		else
			_tlbiel_va_multicast(mm, vmaddr, pid, psize, RIC_FLUSH_TLB);
	} else {
local:
		_tlbiel_va(vmaddr, pid, psize, RIC_FLUSH_TLB);
	}
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
static inline void exit_flush_lazy_tlbs(struct mm_struct *mm) { }
#endif /* CONFIG_SMP */

static void do_tlbiel_kernel(void *info)
{
	_tlbiel_pid(0, RIC_FLUSH_ALL);
}

static inline void _tlbiel_kernel_broadcast(void)
{
	on_each_cpu(do_tlbiel_kernel, NULL, 1);
	if (tlbie_capable) {
		/*
		 * Coherent accelerators don't refcount kernel memory mappings,
		 * so have to always issue a tlbie for them. This is quite a
		 * slow path anyway.
		 */
		_tlbie_pid(0, RIC_FLUSH_ALL);
	}
}

/*
 * If kernel TLBIs ever become local rather than global, then
 * drivers/misc/ocxl/link.c:ocxl_link_add_pe will need some work, as it
 * assumes kernel TLBIs are global.
 */
void radix__flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	if (!mmu_has_feature(MMU_FTR_GTSE)) {
		unsigned long tgt = H_RPTI_TARGET_CMMU | H_RPTI_TARGET_NMMU;
		unsigned long type = H_RPTI_TYPE_TLB | H_RPTI_TYPE_PWC |
				     H_RPTI_TYPE_PRT;

		pseries_rpt_invalidate(0, tgt, type, H_RPTI_PAGE_ALL,
				       start, end);
	} else if (cputlb_use_tlbie())
		_tlbie_pid(0, RIC_FLUSH_ALL);
	else
		_tlbiel_kernel_broadcast();
}
EXPORT_SYMBOL(radix__flush_tlb_kernel_range);

#define TLB_FLUSH_ALL -1UL

/*
 * Number of pages above which we invalidate the entire PID rather than
 * flush individual pages, for local and global flushes respectively.
 *
 * tlbie goes out to the interconnect and individual ops are more costly.
 * It also does not iterate over sets like the local tlbiel variant when
 * invalidating a full PID, so it has a far lower threshold to change from
 * individual page flushes to full-pid flushes.
 */
static unsigned long tlb_single_page_flush_ceiling __read_mostly = 33;
static unsigned long tlb_local_single_page_flush_ceiling __read_mostly = POWER9_TLB_SETS_RADIX * 2;

static inline void __radix__flush_tlb_range(struct mm_struct *mm,
					    unsigned long start, unsigned long end)

{
	unsigned long pid;
	unsigned int page_shift = mmu_psize_defs[mmu_virtual_psize].shift;
	unsigned long page_size = 1UL << page_shift;
	unsigned long nr_pages = (end - start) >> page_shift;
	bool local, full;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	preempt_disable();
	smp_mb(); /* see radix__flush_tlb_mm */
	if (!mm_is_thread_local(mm)) {
		if (unlikely(mm_is_singlethreaded(mm))) {
			if (end != TLB_FLUSH_ALL) {
				exit_flush_lazy_tlbs(mm);
				goto is_local;
			}
		}
		local = false;
		full = (end == TLB_FLUSH_ALL ||
				nr_pages > tlb_single_page_flush_ceiling);
	} else {
is_local:
		local = true;
		full = (end == TLB_FLUSH_ALL ||
				nr_pages > tlb_local_single_page_flush_ceiling);
	}

	if (!mmu_has_feature(MMU_FTR_GTSE) && !local) {
		unsigned long tgt = H_RPTI_TARGET_CMMU;
		unsigned long pg_sizes = psize_to_rpti_pgsize(mmu_virtual_psize);

		if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE))
			pg_sizes |= psize_to_rpti_pgsize(MMU_PAGE_2M);
		if (atomic_read(&mm->context.copros) > 0)
			tgt |= H_RPTI_TARGET_NMMU;
		pseries_rpt_invalidate(pid, tgt, H_RPTI_TYPE_TLB, pg_sizes,
				       start, end);
	} else if (full) {
		if (local) {
			_tlbiel_pid(pid, RIC_FLUSH_TLB);
		} else {
			if (cputlb_use_tlbie()) {
				if (mm_needs_flush_escalation(mm))
					_tlbie_pid(pid, RIC_FLUSH_ALL);
				else
					_tlbie_pid(pid, RIC_FLUSH_TLB);
			} else {
				_tlbiel_pid_multicast(mm, pid, RIC_FLUSH_TLB);
			}
		}
	} else {
		bool hflush = false;
		unsigned long hstart, hend;

		if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
			hstart = (start + PMD_SIZE - 1) & PMD_MASK;
			hend = end & PMD_MASK;
			if (hstart < hend)
				hflush = true;
		}

		if (local) {
			asm volatile("ptesync": : :"memory");
			__tlbiel_va_range(start, end, pid, page_size, mmu_virtual_psize);
			if (hflush)
				__tlbiel_va_range(hstart, hend, pid,
						PMD_SIZE, MMU_PAGE_2M);
			ppc_after_tlbiel_barrier();
		} else if (cputlb_use_tlbie()) {
			asm volatile("ptesync": : :"memory");
			__tlbie_va_range(start, end, pid, page_size, mmu_virtual_psize);
			if (hflush)
				__tlbie_va_range(hstart, hend, pid,
						PMD_SIZE, MMU_PAGE_2M);
			asm volatile("eieio; tlbsync; ptesync": : :"memory");
		} else {
			_tlbiel_va_range_multicast(mm,
					start, end, pid, page_size, mmu_virtual_psize, false);
			if (hflush)
				_tlbiel_va_range_multicast(mm,
					hstart, hend, pid, PMD_SIZE, MMU_PAGE_2M, false);
		}
	}
	preempt_enable();
}

void radix__flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)

{
#ifdef CONFIG_HUGETLB_PAGE
	if (is_vm_hugetlb_page(vma))
		return radix__flush_hugetlb_tlb_range(vma, start, end);
#endif

	__radix__flush_tlb_range(vma->vm_mm, start, end);
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

/*
 * Flush partition scoped LPID address translation for all CPUs.
 */
void radix__flush_tlb_lpid_page(unsigned int lpid,
					unsigned long addr,
					unsigned long page_size)
{
	int psize = radix_get_mmu_psize(page_size);

	_tlbie_lpid_va(addr, lpid, psize, RIC_FLUSH_TLB);
}
EXPORT_SYMBOL_GPL(radix__flush_tlb_lpid_page);

/*
 * Flush partition scoped PWC from LPID for all CPUs.
 */
void radix__flush_pwc_lpid(unsigned int lpid)
{
	_tlbie_lpid(lpid, RIC_FLUSH_PWC);
}
EXPORT_SYMBOL_GPL(radix__flush_pwc_lpid);

/*
 * Flush partition scoped translations from LPID (=LPIDR)
 */
void radix__flush_all_lpid(unsigned int lpid)
{
	_tlbie_lpid(lpid, RIC_FLUSH_ALL);
}
EXPORT_SYMBOL_GPL(radix__flush_all_lpid);

/*
 * Flush process scoped translations from LPID (=LPIDR)
 */
void radix__flush_all_lpid_guest(unsigned int lpid)
{
	_tlbie_lpid_guest(lpid, RIC_FLUSH_ALL);
}

static void radix__flush_tlb_pwc_range_psize(struct mm_struct *mm, unsigned long start,
				  unsigned long end, int psize);

void radix__tlb_flush(struct mmu_gather *tlb)
{
	int psize = 0;
	struct mm_struct *mm = tlb->mm;
	int page_size = tlb->page_size;
	unsigned long start = tlb->start;
	unsigned long end = tlb->end;

	/*
	 * if page size is not something we understand, do a full mm flush
	 *
	 * A "fullmm" flush must always do a flush_all_mm (RIC=2) flush
	 * that flushes the process table entry cache upon process teardown.
	 * See the comment for radix in arch_exit_mmap().
	 */
	if (tlb->fullmm || tlb->need_flush_all) {
		__flush_all_mm(mm, true);
	} else if ( (psize = radix_get_mmu_psize(page_size)) == -1) {
		if (!tlb->freed_tables)
			radix__flush_tlb_mm(mm);
		else
			radix__flush_all_mm(mm);
	} else {
		if (!tlb->freed_tables)
			radix__flush_tlb_range_psize(mm, start, end, psize);
		else
			radix__flush_tlb_pwc_range_psize(mm, start, end, psize);
	}
}

static __always_inline void __radix__flush_tlb_range_psize(struct mm_struct *mm,
				unsigned long start, unsigned long end,
				int psize, bool also_pwc)
{
	unsigned long pid;
	unsigned int page_shift = mmu_psize_defs[psize].shift;
	unsigned long page_size = 1UL << page_shift;
	unsigned long nr_pages = (end - start) >> page_shift;
	bool local, full;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	preempt_disable();
	smp_mb(); /* see radix__flush_tlb_mm */
	if (!mm_is_thread_local(mm)) {
		if (unlikely(mm_is_singlethreaded(mm))) {
			if (end != TLB_FLUSH_ALL) {
				exit_flush_lazy_tlbs(mm);
				goto is_local;
			}
		}
		local = false;
		full = (end == TLB_FLUSH_ALL ||
				nr_pages > tlb_single_page_flush_ceiling);
	} else {
is_local:
		local = true;
		full = (end == TLB_FLUSH_ALL ||
				nr_pages > tlb_local_single_page_flush_ceiling);
	}

	if (!mmu_has_feature(MMU_FTR_GTSE) && !local) {
		unsigned long tgt = H_RPTI_TARGET_CMMU;
		unsigned long type = H_RPTI_TYPE_TLB;
		unsigned long pg_sizes = psize_to_rpti_pgsize(psize);

		if (also_pwc)
			type |= H_RPTI_TYPE_PWC;
		if (atomic_read(&mm->context.copros) > 0)
			tgt |= H_RPTI_TARGET_NMMU;
		pseries_rpt_invalidate(pid, tgt, type, pg_sizes, start, end);
	} else if (full) {
		if (local) {
			_tlbiel_pid(pid, also_pwc ? RIC_FLUSH_ALL : RIC_FLUSH_TLB);
		} else {
			if (cputlb_use_tlbie()) {
				if (mm_needs_flush_escalation(mm))
					also_pwc = true;

				_tlbie_pid(pid,
					also_pwc ?  RIC_FLUSH_ALL : RIC_FLUSH_TLB);
			} else {
				_tlbiel_pid_multicast(mm, pid,
					also_pwc ?  RIC_FLUSH_ALL : RIC_FLUSH_TLB);
			}

		}
	} else {
		if (local)
			_tlbiel_va_range(start, end, pid, page_size, psize, also_pwc);
		else if (cputlb_use_tlbie())
			_tlbie_va_range(start, end, pid, page_size, psize, also_pwc);
		else
			_tlbiel_va_range_multicast(mm,
					start, end, pid, page_size, psize, also_pwc);
	}
	preempt_enable();
}

void radix__flush_tlb_range_psize(struct mm_struct *mm, unsigned long start,
				  unsigned long end, int psize)
{
	return __radix__flush_tlb_range_psize(mm, start, end, psize, false);
}

static void radix__flush_tlb_pwc_range_psize(struct mm_struct *mm, unsigned long start,
				  unsigned long end, int psize)
{
	__radix__flush_tlb_range_psize(mm, start, end, psize, true);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void radix__flush_tlb_collapsed_pmd(struct mm_struct *mm, unsigned long addr)
{
	unsigned long pid, end;

	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	/* 4k page size, just blow the world */
	if (PAGE_SIZE == 0x1000) {
		radix__flush_all_mm(mm);
		return;
	}

	end = addr + HPAGE_PMD_SIZE;

	/* Otherwise first do the PWC, then iterate the pages. */
	preempt_disable();
	smp_mb(); /* see radix__flush_tlb_mm */
	if (!mm_is_thread_local(mm)) {
		if (unlikely(mm_is_singlethreaded(mm))) {
			exit_flush_lazy_tlbs(mm);
			goto local;
		}
		if (!mmu_has_feature(MMU_FTR_GTSE)) {
			unsigned long tgt, type, pg_sizes;

			tgt = H_RPTI_TARGET_CMMU;
			type = H_RPTI_TYPE_TLB | H_RPTI_TYPE_PWC |
			       H_RPTI_TYPE_PRT;
			pg_sizes = psize_to_rpti_pgsize(mmu_virtual_psize);

			if (atomic_read(&mm->context.copros) > 0)
				tgt |= H_RPTI_TARGET_NMMU;
			pseries_rpt_invalidate(pid, tgt, type, pg_sizes,
					       addr, end);
		} else if (cputlb_use_tlbie())
			_tlbie_va_range(addr, end, pid, PAGE_SIZE, mmu_virtual_psize, true);
		else
			_tlbiel_va_range_multicast(mm,
					addr, end, pid, PAGE_SIZE, mmu_virtual_psize, true);
	} else {
local:
		_tlbiel_va_range(addr, end, pid, PAGE_SIZE, mmu_virtual_psize, true);
	}

	preempt_enable();
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

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
	r = 1;   /* radix format */
	rs = 1 & ((1UL << 32) - 1); /* any LPID value to flush guest mappings */

	asm volatile("ptesync": : :"memory");
	/*
	 * now flush guest entries by passing PRS = 1 and LPID != 0
	 */
	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(1), "i"(ric), "r"(rs) : "memory");
	/*
	 * now flush host entires by passing PRS = 0 and LPID == 0
	 */
	asm volatile(PPC_TLBIE_5(%0, %4, %3, %2, %1)
		     : : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(0) : "memory");
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
extern void radix_kvm_prefetch_workaround(struct mm_struct *mm)
{
	unsigned long pid = mm->context.id;

	if (unlikely(pid == MMU_NO_CONTEXT))
		return;

	if (!cpu_has_feature(CPU_FTR_P9_RADIX_PREFETCH_BUG))
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
			if (!cpu_possible(sib))
				continue;
			if (paca_ptrs[sib]->kvm_hstate.kvm_vcpu)
				flush = true;
		}
		if (flush)
			_tlbiel_pid(pid, RIC_FLUSH_ALL);
	}
}
EXPORT_SYMBOL_GPL(radix_kvm_prefetch_workaround);
#endif /* CONFIG_KVM_BOOK3S_HV_POSSIBLE */
