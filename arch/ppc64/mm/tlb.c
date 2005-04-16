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
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <linux/highmem.h>

DEFINE_PER_CPU(struct ppc64_tlb_batch, ppc64_tlb_batch);

/* This is declared as we are using the more or less generic
 * include/asm-ppc64/tlb.h file -- tgall
 */
DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);
DEFINE_PER_CPU(struct pte_freelist_batch *, pte_freelist_cur);
unsigned long pte_freelist_forced_free;

void __pte_free_tlb(struct mmu_gather *tlb, struct page *ptepage)
{
	/* This is safe as we are holding page_table_lock */
        cpumask_t local_cpumask = cpumask_of_cpu(smp_processor_id());
	struct pte_freelist_batch **batchp = &__get_cpu_var(pte_freelist_cur);

	if (atomic_read(&tlb->mm->mm_users) < 2 ||
	    cpus_equal(tlb->mm->cpu_vm_mask, local_cpumask)) {
		pte_free(ptepage);
		return;
	}

	if (*batchp == NULL) {
		*batchp = (struct pte_freelist_batch *)__get_free_page(GFP_ATOMIC);
		if (*batchp == NULL) {
			pte_free_now(ptepage);
			return;
		}
		(*batchp)->index = 0;
	}
	(*batchp)->pages[(*batchp)->index++] = ptepage;
	if ((*batchp)->index == PTE_FREELIST_SIZE) {
		pte_free_submit(*batchp);
		*batchp = NULL;
	}
}

/*
 * Update the MMU hash table to correspond with a change to
 * a Linux PTE.  If wrprot is true, it is permissible to
 * change the existing HPTE to read-only rather than removing it
 * (if we remove it we should clear the _PTE_HPTEFLAGS bits).
 */
void hpte_update(struct mm_struct *mm, unsigned long addr,
		 unsigned long pte, int wrprot)
{
	int i;
	unsigned long context = 0;
	struct ppc64_tlb_batch *batch = &__get_cpu_var(ppc64_tlb_batch);

	if (REGION_ID(addr) == USER_REGION_ID)
		context = mm->context.id;
	i = batch->index;

	/*
	 * This can happen when we are in the middle of a TLB batch and
	 * we encounter memory pressure (eg copy_page_range when it tries
	 * to allocate a new pte). If we have to reclaim memory and end
	 * up scanning and resetting referenced bits then our batch context
	 * will change mid stream.
	 */
	if (unlikely(i != 0 && context != batch->context)) {
		flush_tlb_pending();
		i = 0;
	}

	if (i == 0) {
		batch->context = context;
		batch->mm = mm;
	}
	batch->pte[i] = __pte(pte);
	batch->addr[i] = addr;
	batch->index = ++i;
	if (i >= PPC64_TLB_BATCH_NR)
		flush_tlb_pending();
}

void __flush_tlb_pending(struct ppc64_tlb_batch *batch)
{
	int i;
	int cpu;
	cpumask_t tmp;
	int local = 0;

	BUG_ON(in_interrupt());

	cpu = get_cpu();
	i = batch->index;
	tmp = cpumask_of_cpu(cpu);
	if (cpus_equal(batch->mm->cpu_vm_mask, tmp))
		local = 1;

	if (i == 1)
		flush_hash_page(batch->context, batch->addr[0], batch->pte[0],
				local);
	else
		flush_hash_range(batch->context, i, local);
	batch->index = 0;
	put_cpu();
}

#ifdef CONFIG_SMP
static void pte_free_smp_sync(void *arg)
{
	/* Do nothing, just ensure we sync with all CPUs */
}
#endif

/* This is only called when we are critically out of memory
 * (and fail to get a page in pte_free_tlb).
 */
void pte_free_now(struct page *ptepage)
{
	pte_freelist_forced_free++;

	smp_call_function(pte_free_smp_sync, NULL, 0, 1);

	pte_free(ptepage);
}

static void pte_free_rcu_callback(struct rcu_head *head)
{
	struct pte_freelist_batch *batch =
		container_of(head, struct pte_freelist_batch, rcu);
	unsigned int i;

	for (i = 0; i < batch->index; i++)
		pte_free(batch->pages[i]);
	free_page((unsigned long)batch);
}

void pte_free_submit(struct pte_freelist_batch *batch)
{
	INIT_RCU_HEAD(&batch->rcu);
	call_rcu(&batch->rcu, pte_free_rcu_callback);
}

void pte_free_finish(void)
{
	/* This is safe as we are holding page_table_lock */
	struct pte_freelist_batch **batchp = &__get_cpu_var(pte_freelist_cur);

	if (*batchp == NULL)
		return;
	pte_free_submit(*batchp);
	*batchp = NULL;
}
