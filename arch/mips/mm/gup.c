/*
 * Lockless get_user_pages_fast for MIPS
 *
 * Copyright (C) 2008 Nick Piggin
 * Copyright (C) 2008 Novell Inc.
 * Copyright (C) 2011 Ralf Baechle
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/hugetlb.h>

#include <asm/cpu-features.h>
#include <asm/pgtable.h>

static inline pte_t gup_get_pte(pte_t *ptep)
{
#if defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32)
	pte_t pte;

retry:
	pte.pte_low = ptep->pte_low;
	smp_rmb();
	pte.pte_high = ptep->pte_high;
	smp_rmb();
	if (unlikely(pte.pte_low != ptep->pte_low))
		goto retry;

	return pte;
#else
	return READ_ONCE(*ptep);
#endif
}

static int gup_pte_range(pmd_t pmd, unsigned long addr, unsigned long end,
			int write, struct page **pages, int *nr)
{
	pte_t *ptep = pte_offset_map(&pmd, addr);
	do {
		pte_t pte = gup_get_pte(ptep);
		struct page *page;

		if (!pte_present(pte) ||
		    pte_special(pte) || (write && !pte_write(pte))) {
			pte_unmap(ptep);
			return 0;
		}
		VM_BUG_ON(!pfn_valid(pte_pfn(pte)));
		page = pte_page(pte);
		get_page(page);
		SetPageReferenced(page);
		pages[*nr] = page;
		(*nr)++;

	} while (ptep++, addr += PAGE_SIZE, addr != end);

	pte_unmap(ptep - 1);
	return 1;
}

static inline void get_head_page_multiple(struct page *page, int nr)
{
	VM_BUG_ON(page != compound_head(page));
	VM_BUG_ON(page_count(page) == 0);
	atomic_add(nr, &page->_count);
	SetPageReferenced(page);
}

static int gup_huge_pmd(pmd_t pmd, unsigned long addr, unsigned long end,
			int write, struct page **pages, int *nr)
{
	pte_t pte = *(pte_t *)&pmd;
	struct page *head, *page;
	int refs;

	if (write && !pte_write(pte))
		return 0;
	/* hugepages are never "special" */
	VM_BUG_ON(pte_special(pte));
	VM_BUG_ON(!pfn_valid(pte_pfn(pte)));

	refs = 0;
	head = pte_page(pte);
	page = head + ((addr & ~PMD_MASK) >> PAGE_SHIFT);
	do {
		VM_BUG_ON(compound_head(page) != head);
		pages[*nr] = page;
		if (PageTail(page))
			get_huge_page_tail(page);
		(*nr)++;
		page++;
		refs++;
	} while (addr += PAGE_SIZE, addr != end);

	get_head_page_multiple(head, refs);
	return 1;
}

static int gup_pmd_range(pud_t pud, unsigned long addr, unsigned long end,
			int write, struct page **pages, int *nr)
{
	unsigned long next;
	pmd_t *pmdp;

	pmdp = pmd_offset(&pud, addr);
	do {
		pmd_t pmd = *pmdp;

		next = pmd_addr_end(addr, end);
		/*
		 * The pmd_trans_splitting() check below explains why
		 * pmdp_splitting_flush has to flush the tlb, to stop
		 * this gup-fast code from running while we set the
		 * splitting bit in the pmd. Returning zero will take
		 * the slow path that will call wait_split_huge_page()
		 * if the pmd is still in splitting state. gup-fast
		 * can't because it has irq disabled and
		 * wait_split_huge_page() would never return as the
		 * tlb flush IPI wouldn't run.
		 */
		if (pmd_none(pmd) || pmd_trans_splitting(pmd))
			return 0;
		if (unlikely(pmd_huge(pmd))) {
			if (!gup_huge_pmd(pmd, addr, next, write, pages,nr))
				return 0;
		} else {
			if (!gup_pte_range(pmd, addr, next, write, pages,nr))
				return 0;
		}
	} while (pmdp++, addr = next, addr != end);

	return 1;
}

static int gup_huge_pud(pud_t pud, unsigned long addr, unsigned long end,
			int write, struct page **pages, int *nr)
{
	pte_t pte = *(pte_t *)&pud;
	struct page *head, *page;
	int refs;

	if (write && !pte_write(pte))
		return 0;
	/* hugepages are never "special" */
	VM_BUG_ON(pte_special(pte));
	VM_BUG_ON(!pfn_valid(pte_pfn(pte)));

	refs = 0;
	head = pte_page(pte);
	page = head + ((addr & ~PUD_MASK) >> PAGE_SHIFT);
	do {
		VM_BUG_ON(compound_head(page) != head);
		pages[*nr] = page;
		if (PageTail(page))
			get_huge_page_tail(page);
		(*nr)++;
		page++;
		refs++;
	} while (addr += PAGE_SIZE, addr != end);

	get_head_page_multiple(head, refs);
	return 1;
}

static int gup_pud_range(pgd_t pgd, unsigned long addr, unsigned long end,
			int write, struct page **pages, int *nr)
{
	unsigned long next;
	pud_t *pudp;

	pudp = pud_offset(&pgd, addr);
	do {
		pud_t pud = *pudp;

		next = pud_addr_end(addr, end);
		if (pud_none(pud))
			return 0;
		if (unlikely(pud_huge(pud))) {
			if (!gup_huge_pud(pud, addr, next, write, pages,nr))
				return 0;
		} else {
			if (!gup_pmd_range(pud, addr, next, write, pages,nr))
				return 0;
		}
	} while (pudp++, addr = next, addr != end);

	return 1;
}

/*
 * Like get_user_pages_fast() except its IRQ-safe in that it won't fall
 * back to the regular GUP.
 */
int __get_user_pages_fast(unsigned long start, int nr_pages, int write,
			  struct page **pages)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr, len, end;
	unsigned long next;
	unsigned long flags;
	pgd_t *pgdp;
	int nr = 0;

	start &= PAGE_MASK;
	addr = start;
	len = (unsigned long) nr_pages << PAGE_SHIFT;
	end = start + len;
	if (unlikely(!access_ok(write ? VERIFY_WRITE : VERIFY_READ,
					(void __user *)start, len)))
		return 0;

	/*
	 * XXX: batch / limit 'nr', to avoid large irq off latency
	 * needs some instrumenting to determine the common sizes used by
	 * important workloads (eg. DB2), and whether limiting the batch
	 * size will decrease performance.
	 *
	 * It seems like we're in the clear for the moment. Direct-IO is
	 * the main guy that batches up lots of get_user_pages, and even
	 * they are limited to 64-at-a-time which is not so many.
	 */
	/*
	 * This doesn't prevent pagetable teardown, but does prevent
	 * the pagetables and pages from being freed.
	 *
	 * So long as we atomically load page table pointers versus teardown,
	 * we can follow the address down to the page and take a ref on it.
	 */
	local_irq_save(flags);
	pgdp = pgd_offset(mm, addr);
	do {
		pgd_t pgd = *pgdp;

		next = pgd_addr_end(addr, end);
		if (pgd_none(pgd))
			break;
		if (!gup_pud_range(pgd, addr, next, write, pages, &nr))
			break;
	} while (pgdp++, addr = next, addr != end);
	local_irq_restore(flags);

	return nr;
}

/**
 * get_user_pages_fast() - pin user pages in memory
 * @start:	starting user address
 * @nr_pages:	number of pages from start to pin
 * @write:	whether pages will be written to
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_pages long.
 *
 * Attempt to pin user pages in memory without taking mm->mmap_sem.
 * If not successful, it will fall back to taking the lock and
 * calling get_user_pages().
 *
 * Returns number of pages pinned. This may be fewer than the number
 * requested. If nr_pages is 0 or negative, returns 0. If no pages
 * were pinned, returns -errno.
 */
int get_user_pages_fast(unsigned long start, int nr_pages, int write,
			struct page **pages)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr, len, end;
	unsigned long next;
	pgd_t *pgdp;
	int ret, nr = 0;

	start &= PAGE_MASK;
	addr = start;
	len = (unsigned long) nr_pages << PAGE_SHIFT;

	end = start + len;
	if (end < start || cpu_has_dc_aliases)
		goto slow_irqon;

	/* XXX: batch / limit 'nr' */
	local_irq_disable();
	pgdp = pgd_offset(mm, addr);
	do {
		pgd_t pgd = *pgdp;

		next = pgd_addr_end(addr, end);
		if (pgd_none(pgd))
			goto slow;
		if (!gup_pud_range(pgd, addr, next, write, pages, &nr))
			goto slow;
	} while (pgdp++, addr = next, addr != end);
	local_irq_enable();

	VM_BUG_ON(nr != (end - start) >> PAGE_SHIFT);
	return nr;
slow:
	local_irq_enable();

slow_irqon:
	/* Try to get the remaining pages with get_user_pages */
	start += nr << PAGE_SHIFT;
	pages += nr;

	ret = get_user_pages_unlocked(current, mm, start,
				      (end - start) >> PAGE_SHIFT,
				      write, 0, pages);

	/* Have to be a bit careful with return values */
	if (nr > 0) {
		if (ret < 0)
			ret = nr;
		else
			ret += nr;
	}
	return ret;
}
