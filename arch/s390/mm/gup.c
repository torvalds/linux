/*
 *  Lockless get_user_pages_fast for s390
 *
 *  Copyright IBM Corp. 2010
 *  Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/vmstat.h>
#include <linux/pagemap.h>
#include <linux/rwsem.h>
#include <asm/pgtable.h>

/*
 * The performance critical leaf functions are made noinline otherwise gcc
 * inlines everything into a single function which results in too much
 * register pressure.
 */
static inline int gup_pte_range(pmd_t *pmdp, pmd_t pmd, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	unsigned long mask;
	pte_t *ptep, pte;
	struct page *page;

	mask = (write ? _PAGE_RO : 0) | _PAGE_INVALID | _PAGE_SPECIAL;

	ptep = ((pte_t *) pmd_deref(pmd)) + pte_index(addr);
	do {
		pte = *ptep;
		barrier();
		if ((pte_val(pte) & mask) != 0)
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

static inline void get_huge_page_tail(struct page *page)
{
	/*
	 * __split_huge_page_refcount() cannot run
	 * from under us.
	 */
	VM_BUG_ON(page_mapcount(page) < 0);
	VM_BUG_ON(atomic_read(&page->_count) != 0);
	atomic_inc(&page->_mapcount);
}

static inline int gup_huge_pmd(pmd_t *pmdp, pmd_t pmd, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	unsigned long mask, result;
	struct page *head, *page, *tail;
	int refs;

	result = write ? 0 : _SEGMENT_ENTRY_RO;
	mask = result | _SEGMENT_ENTRY_INV;
	if ((pmd_val(pmd) & mask) != result)
		return 0;
	VM_BUG_ON(!pfn_valid(pmd_val(pmd) >> PAGE_SHIFT));

	refs = 0;
	head = pmd_page(pmd);
	page = head + ((addr & ~PMD_MASK) >> PAGE_SHIFT);
	tail = page;
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
	} else {
		/*
		 * Any tail page need their mapcount reference taken
		 * before we return.
		 */
		while (refs--) {
			if (PageTail(tail))
				get_huge_page_tail(tail);
			tail++;
		}
	}

	return 1;
}


static inline int gup_pmd_range(pud_t *pudp, pud_t pud, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	unsigned long next;
	pmd_t *pmdp, pmd;

	pmdp = (pmd_t *) pudp;
#ifdef CONFIG_64BIT
	if ((pud_val(pud) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3)
		pmdp = (pmd_t *) pud_deref(pud);
	pmdp += pmd_index(addr);
#endif
	do {
		pmd = *pmdp;
		barrier();
		next = pmd_addr_end(addr, end);
		if (pmd_none(pmd))
			return 0;
		if (unlikely(pmd_huge(pmd))) {
			if (!gup_huge_pmd(pmdp, pmd, addr, next,
					  write, pages, nr))
				return 0;
		} else if (!gup_pte_range(pmdp, pmd, addr, next,
					  write, pages, nr))
			return 0;
	} while (pmdp++, addr = next, addr != end);

	return 1;
}

static inline int gup_pud_range(pgd_t *pgdp, pgd_t pgd, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	unsigned long next;
	pud_t *pudp, pud;

	pudp = (pud_t *) pgdp;
#ifdef CONFIG_64BIT
	if ((pgd_val(pgd) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R2)
		pudp = (pud_t *) pgd_deref(pgd);
	pudp += pud_index(addr);
#endif
	do {
		pud = *pudp;
		barrier();
		next = pud_addr_end(addr, end);
		if (pud_none(pud))
			return 0;
		if (!gup_pmd_range(pudp, pud, addr, next, write, pages, nr))
			return 0;
	} while (pudp++, addr = next, addr != end);

	return 1;
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
	pgd_t *pgdp, pgd;
	int nr = 0;

	start &= PAGE_MASK;
	addr = start;
	len = (unsigned long) nr_pages << PAGE_SHIFT;
	end = start + len;
	if (end < start)
		goto slow_irqon;

	/*
	 * local_irq_disable() doesn't prevent pagetable teardown, but does
	 * prevent the pagetables from being freed on s390.
	 *
	 * So long as we atomically load page table pointers versus teardown,
	 * we can follow the address down to the the page and take a ref on it.
	 */
	local_irq_disable();
	pgdp = pgd_offset(mm, addr);
	do {
		pgd = *pgdp;
		barrier();
		next = pgd_addr_end(addr, end);
		if (pgd_none(pgd))
			goto slow;
		if (!gup_pud_range(pgdp, pgd, addr, next, write, pages, &nr))
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
