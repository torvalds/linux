// SPDX-License-Identifier: GPL-2.0
/*
 * Lockless get_user_pages_fast for SuperH
 *
 * Copyright (C) 2009 - 2010  Paul Mundt
 *
 * Cloned from the x86 and PowerPC versions, by:
 *
 *	Copyright (C) 2008 Nick Piggin
 *	Copyright (C) 2008 Novell Inc.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/highmem.h>
#include <asm/pgtable.h>

static inline pte_t gup_get_pte(pte_t *ptep)
{
#ifndef CONFIG_X2TLB
	return READ_ONCE(*ptep);
#else
	/*
	 * With get_user_pages_fast, we walk down the pagetables without
	 * taking any locks.  For this we would like to load the pointers
	 * atomically, but that is not possible with 64-bit PTEs.  What
	 * we do have is the guarantee that a pte will only either go
	 * from not present to present, or present to not present or both
	 * -- it will not switch to a completely different present page
	 * without a TLB flush in between; something that we are blocking
	 * by holding interrupts off.
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

/*
 * The performance critical leaf functions are made noinline otherwise gcc
 * inlines everything into a single function which results in too much
 * register pressure.
 */
static noinline int gup_pte_range(pmd_t pmd, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	u64 mask, result;
	pte_t *ptep;

#ifdef CONFIG_X2TLB
	result = _PAGE_PRESENT | _PAGE_EXT(_PAGE_EXT_KERN_READ | _PAGE_EXT_USER_READ);
	if (write)
		result |= _PAGE_EXT(_PAGE_EXT_KERN_WRITE | _PAGE_EXT_USER_WRITE);
#elif defined(CONFIG_SUPERH64)
	result = _PAGE_PRESENT | _PAGE_USER | _PAGE_READ;
	if (write)
		result |= _PAGE_WRITE;
#else
	result = _PAGE_PRESENT | _PAGE_USER;
	if (write)
		result |= _PAGE_RW;
#endif

	mask = result | _PAGE_SPECIAL;

	ptep = pte_offset_map(&pmd, addr);
	do {
		pte_t pte = gup_get_pte(ptep);
		struct page *page;

		if ((pte_val(pte) & mask) != result) {
			pte_unmap(ptep);
			return 0;
		}
		VM_BUG_ON(!pfn_valid(pte_pfn(pte)));
		page = pte_page(pte);
		get_page(page);
		__flush_anon_page(page, addr);
		flush_dcache_page(page);
		pages[*nr] = page;
		(*nr)++;

	} while (ptep++, addr += PAGE_SIZE, addr != end);
	pte_unmap(ptep - 1);

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

/*
 * Like get_user_pages_fast() except its IRQ-safe in that it won't fall
 * back to the regular GUP.
 * Note a difference with get_user_pages_fast: this always returns the
 * number of pages pinned, 0 if no pages were pinned.
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
	if (unlikely(!access_ok((void __user *)start, len)))
		return 0;

	/*
	 * This doesn't prevent pagetable teardown, but does prevent
	 * the pagetables and pages from being freed.
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
	int nr = 0;

	start &= PAGE_MASK;
	addr = start;
	len = (unsigned long) nr_pages << PAGE_SHIFT;

	end = start + len;
	if (end < start)
		goto slow_irqon;

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
			(end - start) >> PAGE_SHIFT, pages,
			write ? FOLL_WRITE : 0);

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
