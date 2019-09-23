/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_PGALLOC_H
#define _ASM_MICROBLAZE_PGALLOC_H

#ifdef CONFIG_MMU

#include <linux/kernel.h>	/* For min/max macros */
#include <linux/highmem.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/pgtable.h>

extern void __bad_pte(pmd_t *pmd);

static inline pgd_t *get_pgd(void)
{
	return (pgd_t *)__get_free_pages(GFP_KERNEL|__GFP_ZERO, 0);
}

static inline void free_pgd(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

#define pgd_free(mm, pgd)	free_pgd(pgd)
#define pgd_alloc(mm)		get_pgd()

#define pmd_pgtable(pmd)	pmd_page(pmd)

/*
 * We don't have any real pmd's, and this code never triggers because
 * the pgd will always be present..
 */
#define pmd_alloc_one_fast(mm, address)	({ BUG(); ((pmd_t *)1); })
#define pmd_alloc_one(mm, address)	({ BUG(); ((pmd_t *)2); })

extern pte_t *pte_alloc_one_kernel(struct mm_struct *mm);

static inline struct page *pte_alloc_one(struct mm_struct *mm)
{
	struct page *ptepage;

#ifdef CONFIG_HIGHPTE
	int flags = GFP_KERNEL | __GFP_ZERO | __GFP_HIGHMEM;
#else
	int flags = GFP_KERNEL | __GFP_ZERO;
#endif

	ptepage = alloc_pages(flags, 0);
	if (!ptepage)
		return NULL;
	if (!pgtable_page_ctor(ptepage)) {
		__free_page(ptepage);
		return NULL;
	}
	return ptepage;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free_slow(struct page *ptepage)
{
	__free_page(ptepage);
}

static inline void pte_free(struct mm_struct *mm, struct page *ptepage)
{
	pgtable_page_dtor(ptepage);
	__free_page(ptepage);
}

#define __pte_free_tlb(tlb, pte, addr)	pte_free((tlb)->mm, (pte))

#define pmd_populate(mm, pmd, pte) \
			(pmd_val(*(pmd)) = (unsigned long)page_address(pte))

#define pmd_populate_kernel(mm, pmd, pte) \
		(pmd_val(*(pmd)) = (unsigned long) (pte))

/*
 * We don't have any real pmd's, and this code never triggers because
 * the pgd will always be present..
 */
#define pmd_alloc_one(mm, address)	({ BUG(); ((pmd_t *)2); })
#define pmd_free(mm, x)			do { } while (0)
#define __pmd_free_tlb(tlb, x, addr)	pmd_free((tlb)->mm, x)
#define pgd_populate(mm, pmd, pte)	BUG()

#endif /* CONFIG_MMU */

#endif /* _ASM_MICROBLAZE_PGALLOC_H */
