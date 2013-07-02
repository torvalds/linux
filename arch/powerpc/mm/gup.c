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
		if (pmd_huge(pmd)) {
			if (!gup_hugepte((pte_t *)pmdp, PMD_SIZE, addr, next,
					 write, pages, nr))
				return 0;
		} else if (is_hugepd(pmdp)) {
			if (!gup_hugepd((hugepd_t *)pmdp, PMD_SHIFT,
					addr, next, write, pages, nr))
				return 0;
		} else if (!gup_pte_range(pmd, addr, next, write, pages, nr))
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
		if (pud_huge(pud)) {
			if (!gup_hugepte((pte_t *)pudp, PUD_SIZE, addr, next,
					 write, pages, nr))
				return 0;
		} else if (is_hugepd(pudp)) {
			if (!gup_hugepd((hugepd_t *)pudp, PUD_SHIFT,
					addr, next, write, pages, nr))
				return 0;
		} else if (!gup_pmd_range(pud, addr, next, write, pages, nr))
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

	pr_devel("%s(%lx,%x,%s)\n", __func__, start, nr_pages, write ? "write" : "read");

	start &= PAGE_MASK;
	addr = start;
	len = (unsigned long) nr_pages << PAGE_SHIFT;
	end = start + len;

	if (unlikely(!access_ok(write ? VERIFY_WRITE : VERIFY_READ,
					start, len)))
		goto slow_irqon;

	pr_devel("  aligned: %lx .. %lx\n", start, end);

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

	pgdp = pgd_offset(mm, addr);
	do {
		pgd_t pgd = *pgdp;

		pr_devel("  %016lx: normal pgd %p\n", addr,
			 (void *)pgd_val(pgd));
		next = pgd_addr_end(addr, end);
		if (pgd_none(pgd))
			goto slow;
		if (pgd_huge(pgd)) {
			if (!gup_hugepte((pte_t *)pgdp, PGDIR_SIZE, addr, next,
					 write, pages, &nr))
				goto slow;
		} else if (is_hugepd(pgdp)) {
			if (!gup_hugepd((hugepd_t *)pgdp, PGDIR_SHIFT,
					addr, next, write, pages, &nr))
				goto slow;
		} else if (!gup_pud_range(pgd, addr, next, write, pages, &nr))
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
