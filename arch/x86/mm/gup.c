/*
 * Lockless get_user_pages_fast for x86
 *
 * Copyright (C) 2008 Nick Piggin
 * Copyright (C) 2008 Novell Inc.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/memremap.h>

#include <asm/mmu_context.h>
#include <asm/pgtable.h>

static inline pte_t gup_get_pte(pte_t *ptep)
{
#ifndef CONFIG_X86_PAE
	return READ_ONCE(*ptep);
#else
	/*
	 * With get_user_pages_fast, we walk down the pagetables without taking
	 * any locks.  For this we would like to load the pointers atomically,
	 * but that is not possible (without expensive cmpxchg8b) on PAE.  What
	 * we do have is the guarantee that a pte will only either go from not
	 * present to present, or present to not present or both -- it will not
	 * switch to a completely different present page without a TLB flush in
	 * between; something that we are blocking by holding interrupts off.
	 *
	 * Setting ptes from not present to present goes:
	 * ptep->pte_high = h;
	 * smp_wmb();
	 * ptep->pte_low = l;
	 *
	 * And present to not present goes:
	 * ptep->pte_low = 0;
	 * smp_wmb();
	 * ptep->pte_high = 0;
	 *
	 * We must ensure here that the load of pte_low sees l iff pte_high
	 * sees h. We load pte_high *after* loading pte_low, which ensures we
	 * don't see an older value of pte_high.  *Then* we recheck pte_low,
	 * which ensures that we haven't picked up a changed pte high. We might
	 * have got rubbish values from pte_low and pte_high, but we are
	 * guaranteed that pte_low will not have the present bit set *unless*
	 * it is 'l'. And get_user_pages_fast only operates on present ptes, so
	 * we're safe.
	 *
	 * gup_get_pte should not be used or copied outside gup.c without being
	 * very careful -- it does not atomically load the pte or anything that
	 * is likely to be useful for you.
	 */
	pte_t pte;

retry:
	pte.pte_low = ptep->pte_low;
	smp_rmb();
	pte.pte_high = ptep->pte_high;
	smp_rmb();
	if (unlikely(pte.pte_low != ptep->pte_low))
		goto retry;

	return pte;
#endif
}

static void undo_dev_pagemap(int *nr, int nr_start, struct page **pages)
{
	while ((*nr) - nr_start) {
		struct page *page = pages[--(*nr)];

		ClearPageReferenced(page);
		put_page(page);
	}
}

/*
 * 'pteval' can come from a pte, pmd or pud.  We only check
 * _PAGE_PRESENT, _PAGE_USER, and _PAGE_RW in here which are the
 * same value on all 3 types.
 */
static inline int pte_allows_gup(unsigned long pteval, int write)
{
	unsigned long need_pte_bits = _PAGE_PRESENT|_PAGE_USER;

	if (write)
		need_pte_bits |= _PAGE_RW;

	if ((pteval & need_pte_bits) != need_pte_bits)
		return 0;

	/* Check memory protection keys permissions. */
	if (!__pkru_allows_pkey(pte_flags_pkey(pteval), write))
		return 0;

	return 1;
}

/*
 * The performance critical leaf functions are made noinline otherwise gcc
 * inlines everything into a single function which results in too much
 * register pressure.
 */
static noinline int gup_pte_range(pmd_t pmd, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	struct dev_pagemap *pgmap = NULL;
	int nr_start = *nr;
	pte_t *ptep;

	ptep = pte_offset_map(&pmd, addr);
	do {
		pte_t pte = gup_get_pte(ptep);
		struct page *page;

		/* Similar to the PMD case, NUMA hinting must take slow path */
		if (pte_protnone(pte)) {
			pte_unmap(ptep);
			return 0;
		}

		if (pte_devmap(pte)) {
			pgmap = get_dev_pagemap(pte_pfn(pte), pgmap);
			if (unlikely(!pgmap)) {
				undo_dev_pagemap(nr, nr_start, pages);
				pte_unmap(ptep);
				return 0;
			}
		} else if (!pte_allows_gup(pte_val(pte), write) ||
			   pte_special(pte)) {
			pte_unmap(ptep);
			return 0;
		}
		VM_BUG_ON(!pfn_valid(pte_pfn(pte)));
		page = pte_page(pte);
		get_page(page);
		put_dev_pagemap(pgmap);
		SetPageReferenced(page);
		pages[*nr] = page;
		(*nr)++;

	} while (ptep++, addr += PAGE_SIZE, addr != end);
	pte_unmap(ptep - 1);

	return 1;
}

static inline void get_head_page_multiple(struct page *page, int nr)
{
	VM_BUG_ON_PAGE(page != compound_head(page), page);
	VM_BUG_ON_PAGE(page_count(page) == 0, page);
	page_ref_add(page, nr);
	SetPageReferenced(page);
}

static int __gup_device_huge_pmd(pmd_t pmd, unsigned long addr,
		unsigned long end, struct page **pages, int *nr)
{
	int nr_start = *nr;
	unsigned long pfn = pmd_pfn(pmd);
	struct dev_pagemap *pgmap = NULL;

	pfn += (addr & ~PMD_MASK) >> PAGE_SHIFT;
	do {
		struct page *page = pfn_to_page(pfn);

		pgmap = get_dev_pagemap(pfn, pgmap);
		if (unlikely(!pgmap)) {
			undo_dev_pagemap(nr, nr_start, pages);
			return 0;
		}
		SetPageReferenced(page);
		pages[*nr] = page;
		get_page(page);
		put_dev_pagemap(pgmap);
		(*nr)++;
		pfn++;
	} while (addr += PAGE_SIZE, addr != end);
	return 1;
}

static noinline int gup_huge_pmd(pmd_t pmd, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	struct page *head, *page;
	int refs;

	if (!pte_allows_gup(pmd_val(pmd), write))
		return 0;

	VM_BUG_ON(!pfn_valid(pmd_pfn(pmd)));
	if (pmd_devmap(pmd))
		return __gup_device_huge_pmd(pmd, addr, end, pages, nr);

	/* hugepages are never "special" */
	VM_BUG_ON(pmd_flags(pmd) & _PAGE_SPECIAL);

	refs = 0;
	head = pmd_page(pmd);
	page = head + ((addr & ~PMD_MASK) >> PAGE_SHIFT);
	do {
		VM_BUG_ON_PAGE(compound_head(page) != head, page);
		pages[*nr] = page;
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
		if (pmd_none(pmd))
			return 0;
		if (unlikely(pmd_large(pmd) || !pmd_present(pmd))) {
			/*
			 * NUMA hinting faults need to be handled in the GUP
			 * slowpath for accounting purposes and so that they
			 * can be serialised against THP migration.
			 */
			if (pmd_protnone(pmd))
				return 0;
			if (!gup_huge_pmd(pmd, addr, next, write, pages, nr))
				return 0;
		} else {
			if (!gup_pte_range(pmd, addr, next, write, pages, nr))
				return 0;
		}
	} while (pmdp++, addr = next, addr != end);

	return 1;
}

static noinline int gup_huge_pud(pud_t pud, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	struct page *head, *page;
	int refs;

	if (!pte_allows_gup(pud_val(pud), write))
		return 0;
	/* hugepages are never "special" */
	VM_BUG_ON(pud_flags(pud) & _PAGE_SPECIAL);
	VM_BUG_ON(!pfn_valid(pud_pfn(pud)));

	refs = 0;
	head = pud_page(pud);
	page = head + ((addr & ~PUD_MASK) >> PAGE_SHIFT);
	do {
		VM_BUG_ON_PAGE(compound_head(page) != head, page);
		pages[*nr] = page;
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
		if (unlikely(pud_large(pud))) {
			if (!gup_huge_pud(pud, addr, next, write, pages, nr))
				return 0;
		} else {
			if (!gup_pmd_range(pud, addr, next, write, pages, nr))
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
	 * important workloads (eg. DB2), and whether limiting the batch size
	 * will decrease performance.
	 *
	 * It seems like we're in the clear for the moment. Direct-IO is
	 * the main guy that batches up lots of get_user_pages, and even
	 * they are limited to 64-at-a-time which is not so many.
	 */
	/*
	 * This doesn't prevent pagetable teardown, but does prevent
	 * the pagetables and pages from being freed on x86.
	 *
	 * So long as we atomically load page table pointers versus teardown
	 * (which we do on x86, with the above PAE exception), we can follow the
	 * address down to the the page and take a ref on it.
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
 * 		Should be at least nr_pages long.
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
	int nr = 0;

	start &= PAGE_MASK;
	addr = start;
	len = (unsigned long) nr_pages << PAGE_SHIFT;

	end = start + len;
	if (end < start)
		goto slow_irqon;

#ifdef CONFIG_X86_64
	if (end >> __VIRTUAL_MASK_SHIFT)
		goto slow_irqon;
#endif

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
	 * the pagetables and pages from being freed on x86.
	 *
	 * So long as we atomically load page table pointers versus teardown
	 * (which we do on x86, with the above PAE exception), we can follow the
	 * address down to the the page and take a ref on it.
	 */
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

	{
		int ret;

slow:
		local_irq_enable();
slow_irqon:
		/* Try to get the remaining pages with get_user_pages */
		start += nr << PAGE_SHIFT;
		pages += nr;

		ret = get_user_pages_unlocked(start,
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
}
