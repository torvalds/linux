/*
 * This file contains the routines for flushing entries from the
 * TLB and MMU hash table.
 *
 *  Derived from arch/ppc64/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Dave Engebretsen <engebret@us.ibm.com>
 *      Rework for PPC64 port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/bug.h>

#include <trace/events/thp.h>

DEFINE_PER_CPU(struct ppc64_tlb_batch, ppc64_tlb_batch);

/*
 * A linux PTE was changed and the corresponding hash table entry
 * neesd to be flushed. This function will either perform the flush
 * immediately or will batch it up if the current CPU has an active
 * batch on it.
 */
void hpte_need_flush(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, unsigned long pte, int huge)
{
	unsigned long vpn;
	struct ppc64_tlb_batch *batch = &get_cpu_var(ppc64_tlb_batch);
	unsigned long vsid;
	unsigned int psize;
	int ssize;
	real_pte_t rpte;
	int i;

	i = batch->index;

	/* Get page size (maybe move back to caller).
	 *
	 * NOTE: when using special 64K mappings in 4K environment like
	 * for SPEs, we obtain the page size from the slice, which thus
	 * must still exist (and thus the VMA not reused) at the time
	 * of this call
	 */
	if (huge) {
#ifdef CONFIG_HUGETLB_PAGE
		psize = get_slice_psize(mm, addr);
		/* Mask the address for the correct page size */
		addr &= ~((1UL << mmu_psize_defs[psize].shift) - 1);
#else
		BUG();
		psize = pte_pagesize_index(mm, addr, pte); /* shutup gcc */
#endif
	} else {
		psize = pte_pagesize_index(mm, addr, pte);
		/* Mask the address for the standard page size.  If we
		 * have a 64k page kernel, but the hardware does not
		 * support 64k pages, this might be different from the
		 * hardware page size encoded in the slice table. */
		addr &= PAGE_MASK;
	}


	/* Build full vaddr */
	if (!is_kernel_addr(addr)) {
		ssize = user_segment_size(addr);
		vsid = get_vsid(mm->context.id, addr, ssize);
	} else {
		vsid = get_kernel_vsid(addr, mmu_kernel_ssize);
		ssize = mmu_kernel_ssize;
	}
	WARN_ON(vsid == 0);
	vpn = hpt_vpn(addr, vsid, ssize);
	rpte = __real_pte(__pte(pte), ptep);

	/*
	 * Check if we have an active batch on this CPU. If not, just
	 * flush now and return. For now, we don global invalidates
	 * in that case, might be worth testing the mm cpu mask though
	 * and decide to use local invalidates instead...
	 */
	if (!batch->active) {
		flush_hash_page(vpn, rpte, psize, ssize, 0);
		put_cpu_var(ppc64_tlb_batch);
		return;
	}

	/*
	 * This can happen when we are in the middle of a TLB batch and
	 * we encounter memory pressure (eg copy_page_range when it tries
	 * to allocate a new pte). If we have to reclaim memory and end
	 * up scanning and resetting referenced bits then our batch context
	 * will change mid stream.
	 *
	 * We also need to ensure only one page size is present in a given
	 * batch
	 */
	if (i != 0 && (mm != batch->mm || batch->psize != psize ||
		       batch->ssize != ssize)) {
		__flush_tlb_pending(batch);
		i = 0;
	}
	if (i == 0) {
		batch->mm = mm;
		batch->psize = psize;
		batch->ssize = ssize;
	}
	batch->pte[i] = rpte;
	batch->vpn[i] = vpn;
	batch->index = ++i;
	if (i >= PPC64_TLB_BATCH_NR)
		__flush_tlb_pending(batch);
	put_cpu_var(ppc64_tlb_batch);
}

/*
 * This function is called when terminating an mmu batch or when a batch
 * is full. It will perform the flush of all the entries currently stored
 * in a batch.
 *
 * Must be called from within some kind of spinlock/non-preempt region...
 */
void __flush_tlb_pending(struct ppc64_tlb_batch *batch)
{
	const struct cpumask *tmp;
	int i, local = 0;

	i = batch->index;
	tmp = cpumask_of(smp_processor_id());
	if (cpumask_equal(mm_cpumask(batch->mm), tmp))
		local = 1;
	if (i == 1)
		flush_hash_page(batch->vpn[0], batch->pte[0],
				batch->psize, batch->ssize, local);
	else
		flush_hash_range(i, local);
	batch->index = 0;
}

void tlb_flush(struct mmu_gather *tlb)
{
	struct ppc64_tlb_batch *tlbbatch = &get_cpu_var(ppc64_tlb_batch);

	/* If there's a TLB batch pending, then we must flush it because the
	 * pages are going to be freed and we really don't want to have a CPU
	 * access a freed page because it has a stale TLB
	 */
	if (tlbbatch->index)
		__flush_tlb_pending(tlbbatch);

	put_cpu_var(ppc64_tlb_batch);
}

/**
 * __flush_hash_table_range - Flush all HPTEs for a given address range
 *                            from the hash table (and the TLB). But keeps
 *                            the linux PTEs intact.
 *
 * @mm		: mm_struct of the target address space (generally init_mm)
 * @start	: starting address
 * @end         : ending address (not included in the flush)
 *
 * This function is mostly to be used by some IO hotplug code in order
 * to remove all hash entries from a given address range used to map IO
 * space on a removed PCI-PCI bidge without tearing down the full mapping
 * since 64K pages may overlap with other bridges when using 64K pages
 * with 4K HW pages on IO space.
 *
 * Because of that usage pattern, it is implemented for small size rather
 * than speed.
 */
void __flush_hash_table_range(struct mm_struct *mm, unsigned long start,
			      unsigned long end)
{
	bool is_thp;
	int hugepage_shift;
	unsigned long flags;

	start = _ALIGN_DOWN(start, PAGE_SIZE);
	end = _ALIGN_UP(end, PAGE_SIZE);

	BUG_ON(!mm->pgd);

	/* Note: Normally, we should only ever use a batch within a
	 * PTE locked section. This violates the rule, but will work
	 * since we don't actually modify the PTEs, we just flush the
	 * hash while leaving the PTEs intact (including their reference
	 * to being hashed). This is not the most performance oriented
	 * way to do things but is fine for our needs here.
	 */
	local_irq_save(flags);
	arch_enter_lazy_mmu_mode();
	for (; start < end; start += PAGE_SIZE) {
		pte_t *ptep = find_linux_pte_or_hugepte(mm->pgd, start, &is_thp,
							&hugepage_shift);
		unsigned long pte;

		if (ptep == NULL)
			continue;
		pte = pte_val(*ptep);
		if (is_thp)
			trace_hugepage_invalidate(start, pte);
		if (!(pte & _PAGE_HASHPTE))
			continue;
		if (unlikely(is_thp))
			hpte_do_hugepage_flush(mm, start, (pmd_t *)ptep, pte);
		else
			hpte_need_flush(mm, start, ptep, pte, hugepage_shift);
	}
	arch_leave_lazy_mmu_mode();
	local_irq_restore(flags);
}

void flush_tlb_pmd_range(struct mm_struct *mm, pmd_t *pmd, unsigned long addr)
{
	pte_t *pte;
	pte_t *start_pte;
	unsigned long flags;

	addr = _ALIGN_DOWN(addr, PMD_SIZE);
	/* Note: Normally, we should only ever use a batch within a
	 * PTE locked section. This violates the rule, but will work
	 * since we don't actually modify the PTEs, we just flush the
	 * hash while leaving the PTEs intact (including their reference
	 * to being hashed). This is not the most performance oriented
	 * way to do things but is fine for our needs here.
	 */
	local_irq_save(flags);
	arch_enter_lazy_mmu_mode();
	start_pte = pte_offset_map(pmd, addr);
	for (pte = start_pte; pte < start_pte + PTRS_PER_PTE; pte++) {
		unsigned long pteval = pte_val(*pte);
		if (pteval & _PAGE_HASHPTE)
			hpte_need_flush(mm, addr, pte, pteval, 0);
		addr += PAGE_SIZE;
	}
	arch_leave_lazy_mmu_mode();
	local_irq_restore(flags);
}
