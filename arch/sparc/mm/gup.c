// SPDX-License-Identifier: GPL-2.0
/*
 * Lockless get_user_pages_fast for sparc, cribbed from powerpc
 *
 * Copyright (C) 2008 Nick Piggin
 * Copyright (C) 2008 Novell Inc.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/pagemap.h>
#include <linux/rwsem.h>
#include <asm/pgtable.h>
#include <asm/adi.h>

/*
 * The performance critical leaf functions are made noinline otherwise gcc
 * inlines everything into a single function which results in too much
 * register pressure.
 */
static noinline int gup_pte_range(pmd_t pmd, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	unsigned long mask, result;
	pte_t *ptep;

	if (tlb_type == hypervisor) {
		result = _PAGE_PRESENT_4V|_PAGE_P_4V;
		if (write)
			result |= _PAGE_WRITE_4V;
	} else {
		result = _PAGE_PRESENT_4U|_PAGE_P_4U;
		if (write)
			result |= _PAGE_WRITE_4U;
	}
	mask = result | _PAGE_SPECIAL;

	ptep = pte_offset_kernel(&pmd, addr);
	do {
		struct page *page, *head;
		pte_t pte = *ptep;

		if ((pte_val(pte) & mask) != result)
			return 0;
		VM_BUG_ON(!pfn_valid(pte_pfn(pte)));

		/* The hugepage case is simplified on sparc64 because
		 * we encode the sub-page pfn offsets into the
		 * hugepage PTEs.  We could optimize this in the future
		 * use page_cache_add_speculative() for the hugepage case.
		 */
		page = pte_page(pte);
		head = compound_head(page);
		if (!page_cache_get_speculative(head))
			return 0;
		if (unlikely(pte_val(pte) != pte_val(*ptep))) {
			put_page(head);
			return 0;
		}

		pages[*nr] = page;
		(*nr)++;
	} while (ptep++, addr += PAGE_SIZE, addr != end);

	return 1;
}

static int gup_huge_pmd(pmd_t *pmdp, pmd_t pmd, unsigned long addr,
			unsigned long end, int write, struct page **pages,
			int *nr)
{
	struct page *head, *page;
	int refs;

	if (!(pmd_val(pmd) & _PAGE_VALID))
		return 0;

	if (write && !pmd_write(pmd))
		return 0;

	refs = 0;
	page = pmd_page(pmd) + ((addr & ~PMD_MASK) >> PAGE_SHIFT);
	head = compound_head(page);
	do {
		VM_BUG_ON(compound_head(page) != head);
		pages[*nr] = page;
		(*nr)++;
		page++;
		refs++;
	} while (addr += PAGE_SIZE, addr != end);

	if (!page_cache_add_speculative(head, refs)) {
		*nr -= refs;
		return 0;
	}

	if (unlikely(pmd_val(pmd) != pmd_val(*pmdp))) {
		*nr -= refs;
		while (refs--)
			put_page(head);
		return 0;
	}

	return 1;
}

static int gup_huge_pud(pud_t *pudp, pud_t pud, unsigned long addr,
			unsigned long end, int write, struct page **pages,
			int *nr)
{
	struct page *head, *page;
	int refs;

	if (!(pud_val(pud) & _PAGE_VALID))
		return 0;

	if (write && !pud_write(pud))
		return 0;

	refs = 0;
	page = pud_page(pud) + ((addr & ~PUD_MASK) >> PAGE_SHIFT);
	head = compound_head(page);
	do {
		VM_BUG_ON(compound_head(page) != head);
		pages[*nr] = page;
		(*nr)++;
		page++;
		refs++;
	} while (addr += PAGE_SIZE, addr != end);

	if (!page_cache_add_speculative(head, refs)) {
		*nr -= refs;
		return 0;
	}

	if (unlikely(pud_val(pud) != pud_val(*pudp))) {
		*nr -= refs;
		while (refs--)
			put_page(head);
		return 0;
	}

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
		if (pmd_none(pmd))
			return 0;
		if (unlikely(pmd_large(pmd))) {
			if (!gup_huge_pmd(pmdp, pmd, addr, next,
					  write, pages, nr))
				return 0;
		} else if (!gup_pte_range(pmd, addr, next, write,
					  pages, nr))
			return 0;
	} while (pmdp++, addr = next, addr != end);

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
		if (unlikely(pud_large(pud))) {
			if (!gup_huge_pud(pudp, pud, addr, next,
					  write, pages, nr))
				return 0;
		} else if (!gup_pmd_range(pud, addr, next, write, pages, nr))
			return 0;
	} while (pudp++, addr = next, addr != end);

	return 1;
}

/*
 * Note a difference with get_user_pages_fast: this always returns the
 * number of pages pinned, 0 if no pages were pinned.
 */
int __get_user_pages_fast(unsigned long start, int nr_pages, int write,
			  struct page **pages)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr, len, end;
	unsigned long next, flags;
	pgd_t *pgdp;
	int nr = 0;

#ifdef CONFIG_SPARC64
	if (adi_capable()) {
		long addr = start;

		/* If userspace has passed a versioned address, kernel
		 * will not find it in the VMAs since it does not store
		 * the version tags in the list of VMAs. Storing version
		 * tags in list of VMAs is impractical since they can be
		 * changed any time from userspace without dropping into
		 * kernel. Any address search in VMAs will be done with
		 * non-versioned addresses. Ensure the ADI version bits
		 * are dropped here by sign extending the last bit before
		 * ADI bits. IOMMU does not implement version tags.
		 */
		addr = (addr << (long)adi_nbits()) >> (long)adi_nbits();
		start = addr;
	}
#endif
	start &= PAGE_MASK;
	addr = start;
	len = (unsigned long) nr_pages << PAGE_SHIFT;
	end = start + len;

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

int get_user_pages_fast(unsigned long start, int nr_pages,
			unsigned int gup_flags, struct page **pages)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr, len, end;
	unsigned long next;
	pgd_t *pgdp;
	int nr = 0;

#ifdef CONFIG_SPARC64
	if (adi_capable()) {
		long addr = start;

		/* If userspace has passed a versioned address, kernel
		 * will not find it in the VMAs since it does not store
		 * the version tags in the list of VMAs. Storing version
		 * tags in list of VMAs is impractical since they can be
		 * changed any time from userspace without dropping into
		 * kernel. Any address search in VMAs will be done with
		 * non-versioned addresses. Ensure the ADI version bits
		 * are dropped here by sign extending the last bit before
		 * ADI bits. IOMMU does not implements version tags,
		 */
		addr = (addr << (long)adi_nbits()) >> (long)adi_nbits();
		start = addr;
	}
#endif
	start &= PAGE_MASK;
	addr = start;
	len = (unsigned long) nr_pages << PAGE_SHIFT;
	end = start + len;

	/*
	 * XXX: batch / limit 'nr', to avoid large irq off latency
	 * needs some instrumenting to determine the common sizes used by
	 * important workloads (eg. DB2), and whether limiting the batch size
	 * will decrease performance.
	 *
	 * It seems like we're in the clear for the moment. Direct-IO is
	 * the main guy that batches up lots of get_user_pages, and even
	 * they are limited to 64-at-a-time which is not so many.
	 */
	/*
	 * This doesn't prevent pagetable teardown, but does prevent
	 * the pagetables from being freed on sparc.
	 *
	 * So long as we atomically load page table pointers versus teardown,
	 * we can follow the address down to the the page and take a ref on it.
	 */
	local_irq_disable();

	pgdp = pgd_offset(mm, addr);
	do {
		pgd_t pgd = *pgdp;

		next = pgd_addr_end(addr, end);
		if (pgd_none(pgd))
			goto slow;
		if (!gup_pud_range(pgd, addr, next, gup_flags & FOLL_WRITE,
				   pages, &nr))
			goto slow;
	} while (pgdp++, addr = next, addr != end);

	local_irq_enable();

	VM_BUG_ON(nr != (end - start) >> PAGE_SHIFT);
	return nr;

	{
		int ret;

slow:
		local_irq_enable();

		/* Try to get the remaining pages with get_user_pages */
		start += nr << PAGE_SHIFT;
		pages += nr;

		ret = get_user_pages_unlocked(start,
			(end - start) >> PAGE_SHIFT, pages,
			gup_flags);

		/* Have to be a bit careful with return values */
		if (nr > 0) {
			if (ret < 0)
				ret = nr;
			else
				ret += nr;
		}

		return ret;
	}
}
