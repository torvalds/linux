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
 *       deal with struct page. That way in future we can make it allocate
 *       multiple PG Tbls in one Page Frame
 *      =sweet side effect is avoiding calls to ugly page_address( ) from the
 *       pg-tlb allocator sub-sys (pte_alloc_one, ptr_free, pmd_populate)
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
	/*
	 * The cast to long below is OK in 32-bit PAE40 regime with long long pte
	 * Despite "wider" pte, the pte table needs to be in non-PAE low memory
	 * as all higher levels can only hold long pointers.
	 *
	 * The cast itself is needed given simplistic definition of set_pmd()
	 */
	set_pmd(pmd, __pmd((unsigned long)pte));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, pgtable_t pte_page)
{
	set_pmd(pmd, __pmd((unsigned long)page_address(pte_page)));
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret = (pgd_t *) __get_free_page(GFP_KERNEL);

	if (ret) {
		int num, num2;
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
	free_page((unsigned long)pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL | __GFP_ZERO);

	return pte;
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	struct page *page;

	page = (pgtable_t)alloc_page(GFP_KERNEL | __GFP_ZERO | __GFP_ACCOUNT);
	if (!page)
		return NULL;

	if (!pgtable_pte_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}

	return page;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte_page)
{
	pgtable_pte_page_dtor(pte_page);
	__free_page(pte_page);
}

#define __pte_free_tlb(tlb, pte, addr)  pte_free((tlb)->mm, pte)

#endif /* _ASM_ARC_PGALLOC_H */
