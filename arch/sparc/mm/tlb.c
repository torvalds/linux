// SPDX-License-Identifier: GPL-2.0
/* arch/sparc64/mm/tlb.c
 *
 * Copyright (C) 2004 David S. Miller <davem@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/preempt.h>
#include <linux/pagemap.h>

#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>

/* Heavily inspired by the ppc64 code.  */

static DEFINE_PER_CPU(struct tlb_batch, tlb_batch);

void flush_tlb_pending(void)
{
	struct tlb_batch *tb = &get_cpu_var(tlb_batch);
	struct mm_struct *mm = tb->mm;

	if (!tb->tlb_nr)
		goto out;

	flush_tsb_user(tb);

	if (CTX_VALID(mm->context)) {
		if (tb->tlb_nr == 1) {
			global_flush_tlb_page(mm, tb->vaddrs[0]);
		} else {
#ifdef CONFIG_SMP
			smp_flush_tlb_pending(tb->mm, tb->tlb_nr,
					      &tb->vaddrs[0]);
#else
			__flush_tlb_pending(CTX_HWBITS(tb->mm->context),
					    tb->tlb_nr, &tb->vaddrs[0]);
#endif
		}
	}

	tb->tlb_nr = 0;

out:
	put_cpu_var(tlb_batch);
}

void arch_enter_lazy_mmu_mode(void)
{
	struct tlb_batch *tb;

	preempt_disable();
	tb = this_cpu_ptr(&tlb_batch);
	tb->active = 1;
}

void arch_leave_lazy_mmu_mode(void)
{
	struct tlb_batch *tb = this_cpu_ptr(&tlb_batch);

	if (tb->tlb_nr)
		flush_tlb_pending();
	tb->active = 0;
	preempt_enable();
}

static void tlb_batch_add_one(struct mm_struct *mm, unsigned long vaddr,
			      bool exec, unsigned int hugepage_shift)
{
	struct tlb_batch *tb = &get_cpu_var(tlb_batch);
	unsigned long nr;

	vaddr &= PAGE_MASK;
	if (exec)
		vaddr |= 0x1UL;

	nr = tb->tlb_nr;

	if (unlikely(nr != 0 && mm != tb->mm)) {
		flush_tlb_pending();
		nr = 0;
	}

	if (!tb->active) {
		flush_tsb_user_page(mm, vaddr, hugepage_shift);
		global_flush_tlb_page(mm, vaddr);
		goto out;
	}

	if (nr == 0) {
		tb->mm = mm;
		tb->hugepage_shift = hugepage_shift;
	}

	if (tb->hugepage_shift != hugepage_shift) {
		flush_tlb_pending();
		tb->hugepage_shift = hugepage_shift;
		nr = 0;
	}

	tb->vaddrs[nr] = vaddr;
	tb->tlb_nr = ++nr;
	if (nr >= TLB_BATCH_NR)
		flush_tlb_pending();

out:
	put_cpu_var(tlb_batch);
}

void tlb_batch_add(struct mm_struct *mm, unsigned long vaddr,
		   pte_t *ptep, pte_t orig, int fullmm,
		   unsigned int hugepage_shift)
{
	if (tlb_type != hypervisor &&
	    pte_dirty(orig)) {
		unsigned long paddr, pfn = pte_pfn(orig);
		struct address_space *mapping;
		struct page *page;
		struct folio *folio;

		if (!pfn_valid(pfn))
			goto no_cache_flush;

		page = pfn_to_page(pfn);
		if (PageReserved(page))
			goto no_cache_flush;

		/* A real file page? */
		folio = page_folio(page);
		mapping = folio_flush_mapping(folio);
		if (!mapping)
			goto no_cache_flush;

		paddr = (unsigned long) page_address(page);
		if ((paddr ^ vaddr) & (1 << 13))
			flush_dcache_folio_all(mm, folio);
	}

no_cache_flush:
	if (!fullmm)
		tlb_batch_add_one(mm, vaddr, pte_exec(orig), hugepage_shift);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void tlb_batch_pmd_scan(struct mm_struct *mm, unsigned long vaddr,
			       pmd_t pmd)
{
	unsigned long end;
	pte_t *pte;

	pte = pte_offset_map(&pmd, vaddr);
	if (!pte)
		return;
	end = vaddr + HPAGE_SIZE;
	while (vaddr < end) {
		if (pte_val(*pte) & _PAGE_VALID) {
			bool exec = pte_exec(*pte);

			tlb_batch_add_one(mm, vaddr, exec, PAGE_SHIFT);
		}
		pte++;
		vaddr += PAGE_SIZE;
	}
	pte_unmap(pte);
}


static void __set_pmd_acct(struct mm_struct *mm, unsigned long addr,
			   pmd_t orig, pmd_t pmd)
{
	if (mm == &init_mm)
		return;

	if ((pmd_val(pmd) ^ pmd_val(orig)) & _PAGE_PMD_HUGE) {
		/*
		 * Note that this routine only sets pmds for THP pages.
		 * Hugetlb pages are handled elsewhere.  We need to check
		 * for huge zero page.  Huge zero pages are like hugetlb
		 * pages in that there is no RSS, but there is the need
		 * for TSB entries.  So, huge zero page counts go into
		 * hugetlb_pte_count.
		 */
		if (pmd_val(pmd) & _PAGE_PMD_HUGE) {
			if (is_huge_zero_pmd(pmd))
				mm->context.hugetlb_pte_count++;
			else
				mm->context.thp_pte_count++;
		} else {
			if (is_huge_zero_pmd(orig))
				mm->context.hugetlb_pte_count--;
			else
				mm->context.thp_pte_count--;
		}

		/* Do not try to allocate the TSB hash table if we
		 * don't have one already.  We have various locks held
		 * and thus we'll end up doing a GFP_KERNEL allocation
		 * in an atomic context.
		 *
		 * Instead, we let the first TLB miss on a hugepage
		 * take care of this.
		 */
	}

	if (!pmd_none(orig)) {
		addr &= HPAGE_MASK;
		if (pmd_trans_huge(orig)) {
			pte_t orig_pte = __pte(pmd_val(orig));
			bool exec = pte_exec(orig_pte);

			tlb_batch_add_one(mm, addr, exec, REAL_HPAGE_SHIFT);
			tlb_batch_add_one(mm, addr + REAL_HPAGE_SIZE, exec,
					  REAL_HPAGE_SHIFT);
		} else {
			tlb_batch_pmd_scan(mm, addr, orig);
		}
	}
}

void set_pmd_at(struct mm_struct *mm, unsigned long addr,
		pmd_t *pmdp, pmd_t pmd)
{
	pmd_t orig = *pmdp;

	*pmdp = pmd;
	__set_pmd_acct(mm, addr, orig, pmd);
}

static inline pmd_t pmdp_establish(struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmdp, pmd_t pmd)
{
	pmd_t old;

	do {
		old = *pmdp;
	} while (cmpxchg64(&pmdp->pmd, old.pmd, pmd.pmd) != old.pmd);
	__set_pmd_acct(vma->vm_mm, address, old, pmd);

	return old;
}

/*
 * This routine is only called when splitting a THP
 */
pmd_t pmdp_invalidate(struct vm_area_struct *vma, unsigned long address,
		     pmd_t *pmdp)
{
	pmd_t old, entry;

	VM_WARN_ON_ONCE(!pmd_present(*pmdp));
	entry = __pmd(pmd_val(*pmdp) & ~_PAGE_VALID);
	old = pmdp_establish(vma, address, pmdp, entry);
	flush_tlb_range(vma, address, address + HPAGE_PMD_SIZE);

	/*
	 * set_pmd_at() will not be called in a way to decrement
	 * thp_pte_count when splitting a THP, so do it now.
	 * Sanity check pmd before doing the actual decrement.
	 */
	if ((pmd_val(entry) & _PAGE_PMD_HUGE) &&
	    !is_huge_zero_pmd(entry))
		(vma->vm_mm)->context.thp_pte_count--;

	return old;
}

void pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				pgtable_t pgtable)
{
	struct list_head *lh = (struct list_head *) pgtable;

	assert_spin_locked(&mm->page_table_lock);

	/* FIFO */
	if (!pmd_huge_pte(mm, pmdp))
		INIT_LIST_HEAD(lh);
	else
		list_add(lh, (struct list_head *) pmd_huge_pte(mm, pmdp));
	pmd_huge_pte(mm, pmdp) = pgtable;
}

pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp)
{
	struct list_head *lh;
	pgtable_t pgtable;

	assert_spin_locked(&mm->page_table_lock);

	/* FIFO */
	pgtable = pmd_huge_pte(mm, pmdp);
	lh = (struct list_head *) pgtable;
	if (list_empty(lh))
		pmd_huge_pte(mm, pmdp) = NULL;
	else {
		pmd_huge_pte(mm, pmdp) = (pgtable_t) lh->next;
		list_del(lh);
	}
	pte_val(pgtable[0]) = 0;
	pte_val(pgtable[1]) = 0;

	return pgtable;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
