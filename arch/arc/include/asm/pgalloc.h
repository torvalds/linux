/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * vineetg: June 2011
 *  -"/proc/meminfo | grep PageTables" kept on increasing
 *   Recently added pgtable dtor was not getting called.
 *
 * vineetg: May 2011
 *  -Variable pg-sz means that Page Tables could be variable sized themselves
 *    So calculate it based on addr traversal split [pgd-bits:pte-bits:xxx]
 *  -Page Table size capped to max 1 to save memory - hence verified.
 *  -Since these deal with constants, gcc compile-time optimizes them.
 *
 * vineetg: Nov 2010
 *  -Added pgtable ctor/dtor used for pgtable mem accounting
 *
 * vineetg: April 2010
 *  -Switched pgtable_t from being struct page * to unsigned long
 *      =Needed so that Page Table allocator (pte_alloc_one) is not forced to
 *       to deal with struct page. Thay way in future we can make it allocate
 *       multiple PG Tbls in one Page Frame
 *      =sweet side effect is avoiding calls to ugly page_address( ) from the
 *       pg-tlb allocator sub-sys (pte_alloc_one, ptr_free, pmd_populate
 *
 *  Amit Bhor, Sameer Dhavale: Codito Technologies 2004
 */

#ifndef _ASM_ARC_PGALLOC_H
#define _ASM_ARC_PGALLOC_H

#include <linux/mm.h>
#include <linux/log2.h>

static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	pmd_set(pmd, pte);
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmd, pgtable_t ptep)
{
	pmd_set(pmd, (pte_t *) ptep);
}

static inline int __get_order_pgd(void)
{
	return get_order(PTRS_PER_PGD * sizeof(pgd_t));
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	int num, num2;
	pgd_t *ret = (pgd_t *) __get_free_pages(GFP_KERNEL, __get_order_pgd());

	if (ret) {
		num = USER_PTRS_PER_PGD + USER_KERNEL_GUTTER / PGDIR_SIZE;
		memzero(ret, num * sizeof(pgd_t));

		num2 = VMALLOC_SIZE / PGDIR_SIZE;
		memcpy(ret + num, swapper_pg_dir + num, num2 * sizeof(pgd_t));

		memzero(ret + num + num2,
			       (PTRS_PER_PGD - num - num2) * sizeof(pgd_t));

	}
	return ret;
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_pages((unsigned long)pgd, __get_order_pgd());
}


/*
 * With software-only page-tables, addr-split for traversal is tweakable and
 * that directly governs how big tables would be at each level.
 * Further, the MMU page size is configurable.
 * Thus we need to programatically assert the size constraint
 * All of this is const math, allowing gcc to do constant folding/propagation.
 */

static inline int __get_order_pte(void)
{
	return get_order(PTRS_PER_PTE * sizeof(pte_t));
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_pages(GFP_KERNEL | __GFP_ZERO,
					 __get_order_pte());

	return pte;
}

static inline pgtable_t
pte_alloc_one(struct mm_struct *mm)
{
	pgtable_t pte_pg;
	struct page *page;

	pte_pg = (pgtable_t)__get_free_pages(GFP_KERNEL, __get_order_pte());
	if (!pte_pg)
		return 0;
	memzero((void *)pte_pg, PTRS_PER_PTE * sizeof(pte_t));
	page = virt_to_page(pte_pg);
	if (!pgtable_pte_page_ctor(page)) {
		__free_page(page);
		return 0;
	}

	return pte_pg;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_pages((unsigned long)pte, __get_order_pte()); /* takes phy addr */
}

static inline void pte_free(struct mm_struct *mm, pgtable_t ptep)
{
	pgtable_pte_page_dtor(virt_to_page(ptep));
	free_pages((unsigned long)ptep, __get_order_pte());
}

#define __pte_free_tlb(tlb, pte, addr)  pte_free((tlb)->mm, pte)

#define pmd_pgtable(pmd)	((pgtable_t) pmd_page_vaddr(pmd))

#endif /* _ASM_ARC_PGALLOC_H */
