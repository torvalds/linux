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
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
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
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/bug.h>

DEFINE_PER_CPU(struct ppc64_tlb_batch, ppc64_tlb_batch);

/* This is declared as we are using the more or less generic
 * include/asm-powerpc/tlb.h file -- tgall
 */
DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);
DEFINE_PER_CPU(struct pte_freelist_batch *, pte_freelist_cur);
unsigned long pte_freelist_forced_free;

struct pte_freelist_batch
{
	struct rcu_head	rcu;
	unsigned int	index;
	pgtable_free_t	tables[0];
};

DEFINE_PER_CPU(struct pte_freelist_batch *, pte_freelist_cur);
unsigned long pte_freelist_forced_free;

#define PTE_FREELIST_SIZE \
	((PAGE_SIZE - sizeof(struct pte_freelist_batch)) \
	  / sizeof(pgtable_free_t))

#ifdef CONFIG_SMP
static void pte_free_smp_sync(void *arg)
{
	/* Do nothing, just ensure we sync with all CPUs */
}
#endif

/* This is only called when we are critically out of memory
 * (and fail to get a page in pte_free_tlb).
 */
static void pgtable_free_now(pgtable_free_t pgf)
{
	pte_freelist_forced_free++;

	smp_call_function(pte_free_smp_sync, NULL, 0, 1);

	pgtable_free(pgf);
}

static void pte_free_rcu_callback(struct rcu_head *head)
{
	struct pte_freelist_batch *batch =
		container_of(head, struct pte_freelist_batch, rcu);
	unsigned int i;

	for (i = 0; i < batch->index; i++)
		pgtable_free(batch->tables[i]);

	free_page((unsigned long)batch);
}

static void pte_free_submit(struct pte_freelist_batch *batch)
{
	INIT_RCU_HEAD(&batch->rcu);
	call_rcu(&batch->rcu, pte_free_rcu_callback);
}

void pgtable_free_tlb(struct mmu_gather *tlb, pgtable_free_t pgf)
{
	/* This is safe since tlb_gather_mmu has disabled preemption */
        cpumask_t local_cpumask = cpumask_of_cpu(smp_processor_id());
	struct pte_freelist_batch **batchp = &__get_cpu_var(pte_freelist_cur);

	if (atomic_read(&tlb->mm->mm_users) < 2 ||
	    cpus_equal(tlb->mm->cpu_vm_mask, local_cpumask)) {
		pgtable_free(pgf);
		return;
	}

	if (*batchp == NULL) {
		*batchp = (struct pte_freelist_batch *)__get_free_page(GFP_ATOMIC);
		if (*batchp == NULL) {
			pgtable_free_now(pgf);
			return;
		}
		(*batchp)->index = 0;
	}
	(*batchp)->tables[(*batchp)->index++] = pgf;
	if ((*batchp)->index == PTE_FREELIST_SIZE) {
		pte_free_submit(*batchp);
		*batchp = NULL;
	}
}

/*
 * A linux PTE was changed and the corresponding hash table entry
 * neesd to be flushed. This function will either perform the flush
 * immediately or will batch it up if the current CPU has an active
 * batch on it.
 *
 * Must be called from within some kind of spinlock/non-preempt region...
 */
void hpte_need_flush(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, unsigned long pte, int huge)
{
	struct ppc64_tlb_batch *batch = &__get_cpu_var(ppc64_tlb_batch);
	unsigned long vsid, vaddr;
	unsigned int psize;
	real_pte_t rpte;
	int i;

	i = batch->index;

	/* We mask the address for the base page size. Huge pages will
	 * have applied their own masking already
	 */
	addr &= PAGE_MASK;

	/* Get page size (maybe move back to caller) */
	if (huge) {
#ifdef CONFIG_HUGETLB_PAGE
		psize = mmu_huge_psize;
#else
		BUG();
		psize = pte_pagesize_index(pte); /* shutup gcc */
#endif
	} else
		psize = pte_pagesize_index(pte);

	/* Build full vaddr */
	if (!is_kernel_addr(addr)) {
		vsid = get_vsid(mm->context.id, addr);
		WARN_ON(vsid == 0);
	} else
		vsid = get_kernel_vsid(addr);
	vaddr = (vsid << 28 ) | (addr & 0x0fffffff);
	rpte = __real_pte(__pte(pte), ptep);

	/*
	 * Check if we have an active batch on this CPU. If not, just
	 * flush now and return. For now, we don global invalidates
	 * in that case, might be worth testing the mm cpu mask though
	 * and decide to use local invalidates instead...
	 */
	if (!batch->active) {
		flush_hash_page(vaddr, rpte, psize, 0);
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
	if (i != 0 && (mm != batch->mm || batch->psize != psize)) {
		__flush_tlb_pending(batch);
		i = 0;
	}
	if (i == 0) {
		batch->mm = mm;
		batch->psize = psize;
	}
	batch->pte[i] = rpte;
	batch->vaddr[i] = vaddr;
	batch->index = ++i;
	if (i >= PPC64_TLB_BATCH_NR)
		__flush_tlb_pending(batch);
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
	cpumask_t tmp;
	int i, local = 0;

	i = batch->index;
	tmp = cpumask_of_cpu(smp_processor_id());
	if (cpus_equal(batch->mm->cpu_vm_mask, tmp))
		local = 1;
	if (i == 1)
		flush_hash_page(batch->vaddr[0], batch->pte[0],
				batch->psize, local);
	else
		flush_hash_range(i, local);
	batch->index = 0;
}

void pte_free_finish(void)
{
	/* This is safe since tlb_gather_mmu has disabled preemption */
	struct pte_freelist_batch **batchp = &__get_cpu_var(pte_freelist_cur);

	if (*batchp == NULL)
		return;
	pte_free_submit(*batchp);
	*batchp = NULL;
}
