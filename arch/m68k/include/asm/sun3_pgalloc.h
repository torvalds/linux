/* SPDX-License-Identifier: GPL-2.0 */
/* sun3_pgalloc.h --
 * reorganization around 2.3.39, routines moved from sun3_pgtable.h
 *
 *
 * 02/27/2002 -- Modified to support "highpte" implementation in 2.5.5 (Sam)
 *
 * moved 1/26/2000 Sam Creasey
 */

#ifndef _SUN3_PGALLOC_H
#define _SUN3_PGALLOC_H

#include <asm/tlb.h>

#include <asm-generic/pgalloc.h>

extern const char bad_pmd_string[];

#define __pte_free_tlb(tlb, pte, addr)	\
	tlb_remove_ptdesc((tlb), page_ptdesc(pte))

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	pmd_val(*pmd) = __pa((unsigned long)pte);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, pgtable_t page)
{
	pmd_val(*pmd) = __pa((unsigned long)page_address(page));
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
#define pmd_free(mm, x)			do { } while (0)

static inline pgd_t * pgd_alloc(struct mm_struct *mm)
{
	pgd_t *new_pgd;

	new_pgd = __pgd_alloc(mm, 0);
	if (likely(new_pgd != NULL)) {
		memcpy(new_pgd, swapper_pg_dir, PAGE_SIZE);
		memset(new_pgd, 0, (PAGE_OFFSET >> PGDIR_SHIFT));
	}
	return new_pgd;
}

#endif /* SUN3_PGALLOC_H */
