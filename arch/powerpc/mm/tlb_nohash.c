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
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/lmb.h>

#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/code-patching.h>

#include "mmu_decl.h"

#ifdef CONFIG_PPC_BOOK3E
struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT] = {
	[MMU_PAGE_4K] = {
		.shift	= 12,
		.enc	= BOOK3E_PAGESZ_4K,
	},
	[MMU_PAGE_16K] = {
		.shift	= 14,
		.enc	= BOOK3E_PAGESZ_16K,
	},
	[MMU_PAGE_64K] = {
		.shift	= 16,
		.enc	= BOOK3E_PAGESZ_64K,
	},
	[MMU_PAGE_1M] = {
		.shift	= 20,
		.enc	= BOOK3E_PAGESZ_1M,
	},
	[MMU_PAGE_16M] = {
		.shift	= 24,
		.enc	= BOOK3E_PAGESZ_16M,
	},
	[MMU_PAGE_256M] = {
		.shift	= 28,
		.enc	= BOOK3E_PAGESZ_256M,
	},
	[MMU_PAGE_1G] = {
		.shift	= 30,
		.enc	= BOOK3E_PAGESZ_1GB,
	},
};
static inline int mmu_get_tsize(int psize)
{
	return mmu_psize_defs[psize].enc;
}
#else
static inline int mmu_get_tsize(int psize)
{
	/* This isn't used on !Book3E for now */
	return 0;
}
#endif

/* The variables below are currently only used on 64-bit Book3E
 * though this will probably be made common with other nohash
 * implementations at some point
 */
#ifdef CONFIG_PPC64

int mmu_linear_psize;		/* Page size used for the linear mapping */
int mmu_pte_psize;		/* Page size used for PTE pages */
int mmu_vmemmap_psize;		/* Page size used for the virtual mem map */
int book3e_htw_enabled;		/* Is HW tablewalk enabled ? */
unsigned long linear_map_top;	/* Top of linear mapping */

#endif /* CONFIG_PPC64 */

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

/*
 * And here are the SMP non-local implementations
 */
#ifdef CONFIG_SMP

static DEFINE_SPINLOCK(tlbivax_lock);

static int mm_is_core_local(struct mm_struct *mm)
{
	return cpumask_subset(mm_cpumask(mm),
			      topology_thread_cpumask(smp_processor_id()));
}

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

	preempt_disable();
	pid = mm ? mm->context.id : 0;
	if (unlikely(pid == MMU_NO_CONTEXT))
		goto bail;
	cpu_mask = mm_cpumask(mm);
	if (!mm_is_core_local(mm)) {
		/* If broadcast tlbivax is supported, use it */
		if (mmu_has_feature(MMU_FTR_USE_TLBIVAX_BCAST)) {
			int lock = mmu_has_feature(MMU_FTR_LOCK_BCAST_INVAL);
			if (lock)
				spin_lock(&tlbivax_lock);
			_tlbivax_bcast(vmaddr, pid, tsize, ind);
			if (lock)
				spin_unlock(&tlbivax_lock);
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
	__flush_tlb_page(vma ? vma->vm_mm : NULL, vmaddr,
			 mmu_get_tsize(mmu_virtual_psize), 0);
}
EXPORT_SYMBOL(flush_tlb_page);

#endif /* CONFIG_SMP */

/*
 * Flush kernel TLB entries in the given range
 */
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

/*
 * Currently, for range flushing, we just do a full mm flush. This should
 * be optimized based on a threshold on the size of the range, since
 * some implementation can stack multiple tlbivax before a tlbsync but
 * for now, we keep it that way
 */
void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)

{
	flush_tlb_mm(vma->vm_mm);
}
EXPORT_SYMBOL(flush_tlb_range);

void tlb_flush(struct mmu_gather *tlb)
{
	flush_tlb_mm(tlb->mm);

	/* Push out batch of freed page tables */
	pte_free_finish();
}

/*
 * Below are functions specific to the 64-bit variant of Book3E though that
 * may change in the future
 */

#ifdef CONFIG_PPC64

/*
 * Handling of virtual linear page tables or indirect TLB entries
 * flushing when PTE pages are freed
 */
void tlb_flush_pgtable(struct mmu_gather *tlb, unsigned long address)
{
	int tsize = mmu_psize_defs[mmu_pte_psize].enc;

	if (book3e_htw_enabled) {
		unsigned long start = address & PMD_MASK;
		unsigned long end = address + PMD_SIZE;
		unsigned long size = 1UL << mmu_psize_defs[mmu_pte_psize].shift;

		/* This isn't the most optimal, ideally we would factor out the
		 * while preempt & CPU mask mucking around, or even the IPI but
		 * it will do for now
		 */
		while (start < end) {
			__flush_tlb_page(tlb->mm, start, tsize, 1);
			start += size;
		}
	} else {
		unsigned long rmask = 0xf000000000000000ul;
		unsigned long rid = (address & rmask) | 0x1000000000000000ul;
		unsigned long vpte = address & ~rmask;

#ifdef CONFIG_PPC_64K_PAGES
		vpte = (vpte >> (PAGE_SHIFT - 4)) & ~0xfffful;
#else
		vpte = (vpte >> (PAGE_SHIFT - 3)) & ~0xffful;
#endif
		vpte |= rid;
		__flush_tlb_page(tlb->mm, vpte, tsize, 0);
	}
}

/*
 * Early initialization of the MMU TLB code
 */
static void __early_init_mmu(int boot_cpu)
{
	extern unsigned int interrupt_base_book3e;
	extern unsigned int exc_data_tlb_miss_htw_book3e;
	extern unsigned int exc_instruction_tlb_miss_htw_book3e;

	unsigned int *ibase = &interrupt_base_book3e;
	unsigned int mas4;

	/* XXX This will have to be decided at runtime, but right
	 * now our boot and TLB miss code hard wires it. Ideally
	 * we should find out a suitable page size and patch the
	 * TLB miss code (either that or use the PACA to store
	 * the value we want)
	 */
	mmu_linear_psize = MMU_PAGE_1G;

	/* XXX This should be decided at runtime based on supported
	 * page sizes in the TLB, but for now let's assume 16M is
	 * always there and a good fit (which it probably is)
	 */
	mmu_vmemmap_psize = MMU_PAGE_16M;

	/* Check if HW tablewalk is present, and if yes, enable it by:
	 *
	 * - patching the TLB miss handlers to branch to the
	 *   one dedicates to it
	 *
	 * - setting the global book3e_htw_enabled
	 *
	 * - Set MAS4:INDD and default page size
	 */

	/* XXX This code only checks for TLB 0 capabilities and doesn't
	 *     check what page size combos are supported by the HW. It
	 *     also doesn't handle the case where a separate array holds
	 *     the IND entries from the array loaded by the PT.
	 */
	if (boot_cpu) {
		unsigned int tlb0cfg = mfspr(SPRN_TLB0CFG);

		/* Check if HW loader is supported */
		if ((tlb0cfg & TLBnCFG_IND) &&
		    (tlb0cfg & TLBnCFG_PT)) {
			patch_branch(ibase + (0x1c0 / 4),
			     (unsigned long)&exc_data_tlb_miss_htw_book3e, 0);
			patch_branch(ibase + (0x1e0 / 4),
			     (unsigned long)&exc_instruction_tlb_miss_htw_book3e, 0);
			book3e_htw_enabled = 1;
		}
		pr_info("MMU: Book3E Page Tables %s\n",
			book3e_htw_enabled ? "Enabled" : "Disabled");
	}

	/* Set MAS4 based on page table setting */

	mas4 = 0x4 << MAS4_WIMGED_SHIFT;
	if (book3e_htw_enabled) {
		mas4 |= mas4 | MAS4_INDD;
#ifdef CONFIG_PPC_64K_PAGES
		mas4 |=	BOOK3E_PAGESZ_256M << MAS4_TSIZED_SHIFT;
		mmu_pte_psize = MMU_PAGE_256M;
#else
		mas4 |=	BOOK3E_PAGESZ_1M << MAS4_TSIZED_SHIFT;
		mmu_pte_psize = MMU_PAGE_1M;
#endif
	} else {
#ifdef CONFIG_PPC_64K_PAGES
		mas4 |=	BOOK3E_PAGESZ_64K << MAS4_TSIZED_SHIFT;
#else
		mas4 |=	BOOK3E_PAGESZ_4K << MAS4_TSIZED_SHIFT;
#endif
		mmu_pte_psize = mmu_virtual_psize;
	}
	mtspr(SPRN_MAS4, mas4);

	/* Set the global containing the top of the linear mapping
	 * for use by the TLB miss code
	 */
	linear_map_top = lmb_end_of_DRAM();

	/* A sync won't hurt us after mucking around with
	 * the MMU configuration
	 */
	mb();
}

void __init early_init_mmu(void)
{
	__early_init_mmu(1);
}

void __cpuinit early_init_mmu_secondary(void)
{
	__early_init_mmu(0);
}

#endif /* CONFIG_PPC64 */
