/*
 * Lockless get_user_pages_fast for powerpc
 *
 * Copyright (C) 2008 Nick Piggin
 * Copyright (C) 2008 Novell Inc.
 */
#undef DEBUG

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/vmstat.h>
#include <linux/pagemap.h>
#include <linux/rwsem.h>
#include <asm/pgtable.h>

#ifdef __HAVE_ARCH_PTE_SPECIAL

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

	result = _PAGE_PRESENT|_PAGE_USER;
	if (write)
		result |= _PAGE_RW;
	mask = result | _PAGE_SPECIAL;

	ptep = pte_offset_kernel(&pmd, addr);
	do {
		pte_t pte = *ptep;
		struct page *page;

		if ((pte_val(pte) & mask) != result)
			return 0;
		VM_BUG_ON(!pfn_valid(pte_pfn(pte)));
		page = pte_page(pte);
		if (!page_cache_get_speculative(page))
			return 0;
		if (unlikely(pte_val(pte) != pte_val(*ptep))) {
			put_page(page);
			return 0;
		}
		pages[*nr] = page;
		(*nr)++;

	} while (ptep++, addr += PAGE_SIZE, addr != end);

	return 1;
}

#ifdef CONFIG_HUGETLB_PAGE
static noinline int gup_huge_pte(pte_t *ptep, struct hstate *hstate,
				 unsigned long *addr, unsigned long end,
				 int write, struct page **pages, int *nr)
{
	unsigned long mask;
	unsigned long pte_end;
	struct page *head, *page;
	pte_t pte;
	int refs;

	pte_end = (*addr + huge_page_size(hstate)) & huge_page_mask(hstate);
	if (pte_end < end)
		end = pte_end;

	pte = *ptep;
	mask = _PAGE_PRESENT|_PAGE_USER;
	if (write)
		mask |= _PAGE_RW;
	if ((pte_val(pte) & mask) != mask)
		return 0;
	/* hugepages are never "special" */
	VM_BUG_ON(!pfn_valid(pte_pfn(pte)));

	refs = 0;
	head = pte_page(pte);
	page = head + ((*addr & ~huge_page_mask(hstate)) >> PAGE_SHIFT);
	do {
		VM_BUG_ON(compound_head(page) != head);
		pages[*nr] = page;
		(*nr)++;
		page++;
		refs++;
	} while (*addr += PAGE_SIZE, *addr != end);

	if (!page_cache_add_speculative(head, refs)) {
		*nr -= refs;
		return 0;
	}
	if (unlikely(pte_val(pte) != pte_val(*ptep))) {
		/* Could be optimized better */
		while (*nr) {
			put_page(page);
			(*nr)--;
		}
	}

	return 1;
}
#endif /* CONFIG_HUGETLB_PAGE */

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
		if (!gup_pte_range(pmd, addr, next, write, pages, nr))
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
		if (!gup_pmd_range(pud, addr, next, write, pages, nr))
			return 0;
	} while (pudp++, addr = next, addr != end);

	return 1;
}

int get_user_pages_fast(unsigned long start, int nr_pages, int write,
			struct page **pages)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr, len, end;
	unsigned long next;
	pgd_t *pgdp;
	int nr = 0;
#ifdef CONFIG_PPC64
	unsigned int shift;
	int psize;
#endif

	pr_devel("%s(%lx,%x,%s)\n", __func__, start, nr_pages, write ? "write" : "read");

	start &= PAGE_MASK;
	addr = start;
	len = (unsigned long) nr_pages << PAGE_SHIFT;
	end = start + len;

	if (unlikely(!access_ok(write ? VERIFY_WRITE : VERIFY_READ,
					start, len)))
		goto slow_irqon;

	pr_devel("  aligned: %lx .. %lx\n", start, end);

#ifdef CONFIG_HUGETLB_PAGE
	/* We bail out on slice boundary crossing when hugetlb is
	 * enabled in order to not have to deal with two different
	 * page table formats
	 */
	if (addr < SLICE_LOW_TOP) {
		if (end > SLICE_LOW_TOP)
			goto slow_irqon;

		if (unlikely(GET_LOW_SLICE_INDEX(addr) !=
			     GET_LOW_SLICE_INDEX(end - 1)))
			goto slow_irqon;
	} else {
		if (unlikely(GET_HIGH_SLICE_INDEX(addr) !=
			     GET_HIGH_SLICE_INDEX(end - 1)))
			goto slow_irqon;
	}
#endif /* CONFIG_HUGETLB_PAGE */

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
	 * the pagetables from being freed on powerpc.
	 *
	 * So long as we atomically load page table pointers versus teardown,
	 * we can follow the address down to the the page and take a ref on it.
	 */
	local_irq_disable();

#ifdef CONFIG_PPC64
	/* Those bits are related to hugetlbfs implementation and only exist
	 * on 64-bit for now
	 */
	psize = get_slice_psize(mm, addr);
	shift = mmu_psize_defs[psize].shift;
#endif /* CONFIG_PPC64 */

#ifdef CONFIG_HUGETLB_PAGE
	if (unlikely(mmu_huge_psizes[psize])) {
		pte_t *ptep;
		unsigned long a = addr;
		unsigned long sz = ((1UL) << shift);
		struct hstate *hstate = size_to_hstate(sz);

		BUG_ON(!hstate);
		/*
		 * XXX: could be optimized to avoid hstate
		 * lookup entirely (just use shift)
		 */

		do {
			VM_BUG_ON(shift != mmu_psize_defs[get_slice_psize(mm, a)].shift);
			ptep = huge_pte_offset(mm, a);
			pr_devel(" %016lx: huge ptep %p\n", a, ptep);
			if (!ptep || !gup_huge_pte(ptep, hstate, &a, end, write, pages,
						   &nr))
				goto slow;
		} while (a != end);
	} else
#endif /* CONFIG_HUGETLB_PAGE */
	{
		pgdp = pgd_offset(mm, addr);
		do {
			pgd_t pgd = *pgdp;

#ifdef CONFIG_PPC64
			VM_BUG_ON(shift != mmu_psize_defs[get_slice_psize(mm, addr)].shift);
#endif
			pr_devel("  %016lx: normal pgd %p\n", addr,
				 (void *)pgd_val(pgd));
			next = pgd_addr_end(addr, end);
			if (pgd_none(pgd))
				goto slow;
			if (!gup_pud_range(pgd, addr, next, write, pages, &nr))
				goto slow;
		} while (pgdp++, addr = next, addr != end);
	}
	local_irq_enable();

	VM_BUG_ON(nr != (end - start) >> PAGE_SHIFT);
	return nr;

	{
		int ret;

slow:
		local_irq_enable();
slow_irqon:
		pr_devel("  slow path ! nr = %d\n", nr);

		/* Try to get the remaining pages with get_user_pages */
		start += nr << PAGE_SHIFT;
		pages += nr;

		down_read(&mm->mmap_sem);
		ret = get_user_pages(current, mm, start,
			(end - start) >> PAGE_SHIFT, write, 0, pages, NULL);
		up_read(&mm->mmap_sem);

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

#endif /* __HAVE_ARCH_PTE_SPECIAL */
