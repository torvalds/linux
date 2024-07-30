// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains the routines for TLB flushing.
 * On machines where the MMU does not use a hash table to store virtual to
 * physical translations (ie, SW loaded TLBs or Book3E compilant processors,
 * this does -not- include 603 however which shares the implementation with
 * hash based processors)
 *
 *  -- BenH
 *
 * Copyright 2008,2009 Ben Herrenschmidt <benh@kernel.crashing.org>
 *                     IBM Corp.
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/hugetlb.h>

#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/code-patching.h>
#include <asm/cputhreads.h>
#include <asm/hugetlb.h>
#include <asm/paca.h>

#include <mm/mmu_decl.h>

/*
 * This struct lists the sw-supported page sizes.  The hardawre MMU may support
 * other sizes not listed here.   The .ind field is only used on MMUs that have
 * indirect page table entries.
 */
#ifdef CONFIG_PPC_E500
struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT] = {
	[MMU_PAGE_4K] = {
		.shift	= 12,
	},
	[MMU_PAGE_2M] = {
		.shift	= 21,
	},
	[MMU_PAGE_4M] = {
		.shift	= 22,
	},
	[MMU_PAGE_16M] = {
		.shift	= 24,
	},
	[MMU_PAGE_64M] = {
		.shift	= 26,
	},
	[MMU_PAGE_256M] = {
		.shift	= 28,
	},
	[MMU_PAGE_1G] = {
		.shift	= 30,
	},
};

static inline int mmu_get_tsize(int psize)
{
	return mmu_psize_defs[psize].shift - 10;
}
#else
static inline int mmu_get_tsize(int psize)
{
	/* This isn't used on !Book3E for now */
	return 0;
}
#endif

#ifdef CONFIG_PPC_8xx
struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT] = {
	[MMU_PAGE_4K] = {
		.shift	= 12,
	},
	[MMU_PAGE_16K] = {
		.shift	= 14,
	},
	[MMU_PAGE_512K] = {
		.shift	= 19,
	},
	[MMU_PAGE_8M] = {
		.shift	= 23,
	},
};
#endif

#ifdef CONFIG_PPC_E500
/* next_tlbcam_idx is used to round-robin tlbcam entry assignment */
DEFINE_PER_CPU(int, next_tlbcam_idx);
EXPORT_PER_CPU_SYMBOL(next_tlbcam_idx);
#endif

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

#ifndef CONFIG_PPC_8xx
/*
 * These are the base non-SMP variants of page and mm flushing
 */
void local_flush_tlb_mm(struct mm_struct *mm)
{
	unsigned int pid;

	preempt_disable();
	pid = mm->context.id;
	if (pid != MMU_NO_CONTEXT)
		_tlbil_pid(pid);
	preempt_enable();
}
EXPORT_SYMBOL(local_flush_tlb_mm);

void __local_flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
			    int tsize, int ind)
{
	unsigned int pid;

	preempt_disable();
	pid = mm ? mm->context.id : 0;
	if (pid != MMU_NO_CONTEXT)
		_tlbil_va(vmaddr, pid, tsize, ind);
	preempt_enable();
}

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	__local_flush_tlb_page(vma ? vma->vm_mm : NULL, vmaddr,
			       mmu_get_tsize(mmu_virtual_psize), 0);
}
EXPORT_SYMBOL(local_flush_tlb_page);

void local_flush_tlb_page_psize(struct mm_struct *mm,
				unsigned long vmaddr, int psize)
{
	__local_flush_tlb_page(mm, vmaddr, mmu_get_tsize(psize), 0);
}
EXPORT_SYMBOL(local_flush_tlb_page_psize);

#endif

/*
 * And here are the SMP non-local implementations
 */
#ifdef CONFIG_SMP

static DEFINE_RAW_SPINLOCK(tlbivax_lock);

struct tlb_flush_param {
	unsigned long addr;
	unsigned int pid;
	unsigned int tsize;
	unsigned int ind;
};

static void do_flush_tlb_mm_ipi(void *param)
{
	struct tlb_flush_param *p = param;

	_tlbil_pid(p ? p->pid : 0);
}

static void do_flush_tlb_page_ipi(void *param)
{
	struct tlb_flush_param *p = param;

	_tlbil_va(p->addr, p->pid, p->tsize, p->ind);
}


/* Note on invalidations and PID:
 *
 * We snapshot the PID with preempt disabled. At this point, it can still
 * change either because:
 * - our context is being stolen (PID -> NO_CONTEXT) on another CPU
 * - we are invaliating some target that isn't currently running here
 *   and is concurrently acquiring a new PID on another CPU
 * - some other CPU is re-acquiring a lost PID for this mm
 * etc...
 *
 * However, this shouldn't be a problem as we only guarantee
 * invalidation of TLB entries present prior to this call, so we
 * don't care about the PID changing, and invalidating a stale PID
 * is generally harmless.
 */

void flush_tlb_mm(struct mm_struct *mm)
{
	unsigned int pid;

	preempt_disable();
	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		goto no_context;
	if (!mm_is_core_local(mm)) {
		struct tlb_flush_param p = { .pid = pid };
		/* Ignores smp_processor_id() even if set. */
		smp_call_function_many(mm_cpumask(mm),
				       do_flush_tlb_mm_ipi, &p, 1);
	}
	_tlbil_pid(pid);
 no_context:
	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_mm);

void __flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
		      int tsize, int ind)
{
	struct cpumask *cpu_mask;
	unsigned int pid;

	/*
	 * This function as well as __local_flush_tlb_page() must only be called
	 * for user contexts.
	 */
	if (WARN_ON(!mm))
		return;

	preempt_disable();
	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		goto bail;
	cpu_mask = mm_cpumask(mm);
	if (!mm_is_core_local(mm)) {
		/* If broadcast tlbivax is supported, use it */
		if (mmu_has_feature(MMU_FTR_USE_TLBIVAX_BCAST)) {
			int lock = mmu_has_feature(MMU_FTR_LOCK_BCAST_INVAL);
			if (lock)
				raw_spin_lock(&tlbivax_lock);
			_tlbivax_bcast(vmaddr, pid, tsize, ind);
			if (lock)
				raw_spin_unlock(&tlbivax_lock);
			goto bail;
		} else {
			struct tlb_flush_param p = {
				.pid = pid,
				.addr = vmaddr,
				.tsize = tsize,
				.ind = ind,
			};
			/* Ignores smp_processor_id() even if set in cpu_mask */
			smp_call_function_many(cpu_mask,
					       do_flush_tlb_page_ipi, &p, 1);
		}
	}
	_tlbil_va(vmaddr, pid, tsize, ind);
 bail:
	preempt_enable();
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
#ifdef CONFIG_HUGETLB_PAGE
	if (vma && is_vm_hugetlb_page(vma))
		flush_hugetlb_page(vma, vmaddr);
#endif

	__flush_tlb_page(vma ? vma->vm_mm : NULL, vmaddr,
			 mmu_get_tsize(mmu_virtual_psize), 0);
}
EXPORT_SYMBOL(flush_tlb_page);

#endif /* CONFIG_SMP */

/*
 * Flush kernel TLB entries in the given range
 */
#ifndef CONFIG_PPC_8xx
void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
#ifdef CONFIG_SMP
	preempt_disable();
	smp_call_function(do_flush_tlb_mm_ipi, NULL, 1);
	_tlbil_pid(0);
	preempt_enable();
#else
	_tlbil_pid(0);
#endif
}
EXPORT_SYMBOL(flush_tlb_kernel_range);
#endif

/*
 * Currently, for range flushing, we just do a full mm flush. This should
 * be optimized based on a threshold on the size of the range, since
 * some implementation can stack multiple tlbivax before a tlbsync but
 * for now, we keep it that way
 */
void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)

{
	if (end - start == PAGE_SIZE && !(start & ~PAGE_MASK))
		flush_tlb_page(vma, start);
	else
		flush_tlb_mm(vma->vm_mm);
}
EXPORT_SYMBOL(flush_tlb_range);

void tlb_flush(struct mmu_gather *tlb)
{
	flush_tlb_mm(tlb->mm);
}

#ifndef CONFIG_PPC64
void __init early_init_mmu(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (IS_ENABLED(CONFIG_PPC_47x) && IS_ENABLED(CONFIG_SMP) &&
	    of_get_flat_dt_prop(root, "cooperative-partition", NULL))
		mmu_clear_feature(MMU_FTR_USE_TLBIVAX_BCAST);
}
#endif /* CONFIG_PPC64 */
