/*
 * This file contains common routines for dealing with free of page tables
 * Along with common page table handling code
 *
 *  Derived from arch/powerpc/mm/tlb_64.c:
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
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>

static DEFINE_PER_CPU(struct pte_freelist_batch *, pte_freelist_cur);
static unsigned long pte_freelist_forced_free;

struct pte_freelist_batch
{
	struct rcu_head	rcu;
	unsigned int	index;
	pgtable_free_t	tables[0];
};

#define PTE_FREELIST_SIZE \
	((PAGE_SIZE - sizeof(struct pte_freelist_batch)) \
	  / sizeof(pgtable_free_t))

static void pte_free_smp_sync(void *arg)
{
	/* Do nothing, just ensure we sync with all CPUs */
}

/* This is only called when we are critically out of memory
 * (and fail to get a page in pte_free_tlb).
 */
static void pgtable_free_now(pgtable_free_t pgf)
{
	pte_freelist_forced_free++;

	smp_call_function(pte_free_smp_sync, NULL, 1);

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
	struct pte_freelist_batch **batchp = &__get_cpu_var(pte_freelist_cur);

	if (atomic_read(&tlb->mm->mm_users) < 2 ||
	    cpumask_equal(mm_cpumask(tlb->mm), cpumask_of(smp_processor_id()))){
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

void pte_free_finish(void)
{
	/* This is safe since tlb_gather_mmu has disabled preemption */
	struct pte_freelist_batch **batchp = &__get_cpu_var(pte_freelist_cur);

	if (*batchp == NULL)
		return;
	pte_free_submit(*batchp);
	*batchp = NULL;
}

/*
 * Handle i/d cache flushing, called from set_pte_at() or ptep_set_access_flags()
 */
static pte_t do_dcache_icache_coherency(pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);
	struct page *page;

	if (unlikely(!pfn_valid(pfn)))
		return pte;
	page = pfn_to_page(pfn);

	if (!PageReserved(page) && !test_bit(PG_arch_1, &page->flags)) {
		pr_debug("do_dcache_icache_coherency... flushing\n");
		flush_dcache_icache_page(page);
		set_bit(PG_arch_1, &page->flags);
	}
	else
		pr_debug("do_dcache_icache_coherency... already clean\n");
	return __pte(pte_val(pte) | _PAGE_HWEXEC);
}

static inline int is_exec_fault(void)
{
	return current->thread.regs && TRAP(current->thread.regs) == 0x400;
}

/* We only try to do i/d cache coherency on stuff that looks like
 * reasonably "normal" PTEs. We currently require a PTE to be present
 * and we avoid _PAGE_SPECIAL and _PAGE_NO_CACHE
 */
static inline int pte_looks_normal(pte_t pte)
{
	return (pte_val(pte) &
		(_PAGE_PRESENT | _PAGE_SPECIAL | _PAGE_NO_CACHE)) ==
		(_PAGE_PRESENT);
}

#if defined(CONFIG_PPC_STD_MMU)
/* Server-style MMU handles coherency when hashing if HW exec permission
 * is supposed per page (currently 64-bit only). Else, we always flush
 * valid PTEs in set_pte.
 */
static inline int pte_need_exec_flush(pte_t pte, int set_pte)
{
	return set_pte && pte_looks_normal(pte) &&
		!(cpu_has_feature(CPU_FTR_COHERENT_ICACHE) ||
		  cpu_has_feature(CPU_FTR_NOEXECUTE));
}
#elif _PAGE_HWEXEC == 0
/* Embedded type MMU without HW exec support (8xx only so far), we flush
 * the cache for any present PTE
 */
static inline int pte_need_exec_flush(pte_t pte, int set_pte)
{
	return set_pte && pte_looks_normal(pte);
}
#else
/* Other embedded CPUs with HW exec support per-page, we flush on exec
 * fault if HWEXEC is not set
 */
static inline int pte_need_exec_flush(pte_t pte, int set_pte)
{
	return pte_looks_normal(pte) && is_exec_fault() &&
		!(pte_val(pte) & _PAGE_HWEXEC);
}
#endif

/*
 * set_pte stores a linux PTE into the linux page table.
 */
void set_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep, pte_t pte)
{
#ifdef CONFIG_DEBUG_VM
	WARN_ON(pte_present(*ptep));
#endif
	/* Note: mm->context.id might not yet have been assigned as
	 * this context might not have been activated yet when this
	 * is called.
	 */
	pte = __pte(pte_val(pte) & ~_PAGE_HPTEFLAGS);
	if (pte_need_exec_flush(pte, 1))
		pte = do_dcache_icache_coherency(pte);

	/* Perform the setting of the PTE */
	__set_pte_at(mm, addr, ptep, pte, 0);
}

/*
 * This is called when relaxing access to a PTE. It's also called in the page
 * fault path when we don't hit any of the major fault cases, ie, a minor
 * update of _PAGE_ACCESSED, _PAGE_DIRTY, etc... The generic code will have
 * handled those two for us, we additionally deal with missing execute
 * permission here on some processors
 */
int ptep_set_access_flags(struct vm_area_struct *vma, unsigned long address,
			  pte_t *ptep, pte_t entry, int dirty)
{
	int changed;
	if (!dirty && pte_need_exec_flush(entry, 0))
		entry = do_dcache_icache_coherency(entry);
	changed = !pte_same(*(ptep), entry);
	if (changed) {
		if (!(vma->vm_flags & VM_HUGETLB))
			assert_pte_locked(vma->vm_mm, address);
		__ptep_set_access_flags(ptep, entry);
		flush_tlb_page_nohash(vma, address);
	}
	return changed;
}

#ifdef CONFIG_DEBUG_VM
void assert_pte_locked(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	if (mm == &init_mm)
		return;
	pgd = mm->pgd + pgd_index(addr);
	BUG_ON(pgd_none(*pgd));
	pud = pud_offset(pgd, addr);
	BUG_ON(pud_none(*pud));
	pmd = pmd_offset(pud, addr);
	BUG_ON(!pmd_present(*pmd));
	BUG_ON(!spin_is_locked(pte_lockptr(mm, pmd)));
}
#endif /* CONFIG_DEBUG_VM */

