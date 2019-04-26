/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_PGALLOC_32_H
#define _ASM_POWERPC_PGALLOC_32_H

#include <linux/threads.h>
#include <linux/slab.h>

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(PGT_CACHE(PGD_INDEX_SIZE),
			pgtable_gfp_flags(mm, GFP_KERNEL));
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	kmem_cache_free(PGT_CACHE(PGD_INDEX_SIZE), pgd);
}

/*
 * We don't have any real pmd's, and this code never triggers because
 * the pgd will always be present..
 */
/* #define pmd_alloc_one(mm,address)       ({ BUG(); ((pmd_t *)2); }) */
#define pmd_free(mm, x) 		do { } while (0)
#define __pmd_free_tlb(tlb,x,a)		do { } while (0)
/* #define pgd_populate(mm, pmd, pte)      BUG() */

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp,
				       pte_t *pte)
{
	if (IS_ENABLED(CONFIG_BOOKE))
		*pmdp = __pmd((unsigned long)pte | _PMD_PRESENT);
	else
		*pmdp = __pmd(__pa(pte) | _PMD_PRESENT);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmdp,
				pgtable_t pte_page)
{
	if (IS_ENABLED(CONFIG_BOOKE))
		*pmdp = __pmd((unsigned long)pte_page | _PMD_PRESENT);
	else
		*pmdp = __pmd(__pa(pte_page) | _PMD_USER | _PMD_PRESENT);
}

#define pmd_pgtable(pmd) ((pgtable_t)pmd_page_vaddr(pmd))

#endif /* _ASM_POWERPC_PGALLOC_32_H */
