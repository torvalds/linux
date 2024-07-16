/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PGALLOC_H
#define _ASM_PGALLOC_H

#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/fixmap.h>

#include <asm/cache.h>

#define __HAVE_ARCH_PMD_ALLOC_ONE
#define __HAVE_ARCH_PMD_FREE
#define __HAVE_ARCH_PGD_FREE
#include <asm-generic/pgalloc.h>

/* Allocate the top level pgd (page directory) */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;

	pgd = (pgd_t *) __get_free_pages(GFP_KERNEL, PGD_TABLE_ORDER);
	if (unlikely(pgd == NULL))
		return NULL;

	memset(pgd, 0, PAGE_SIZE << PGD_TABLE_ORDER);

	return pgd;
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_pages((unsigned long)pgd, PGD_TABLE_ORDER);
}

#if CONFIG_PGTABLE_LEVELS == 3

/* Three Level Page Table Support for pmd's */

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud((PxD_FLAG_PRESENT | PxD_FLAG_VALID) +
			(__u32)(__pa((unsigned long)pmd) >> PxD_VALUE_SHIFT)));
}

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pmd_t *pmd;

	pmd = (pmd_t *)__get_free_pages(GFP_PGTABLE_KERNEL, PMD_TABLE_ORDER);
	if (likely(pmd))
		memset ((void *)pmd, 0, PAGE_SIZE << PMD_TABLE_ORDER);
	return pmd;
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	free_pages((unsigned long)pmd, PMD_TABLE_ORDER);
}
#endif

static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	set_pmd(pmd, __pmd((PxD_FLAG_PRESENT | PxD_FLAG_VALID)
		+ (__u32)(__pa((unsigned long)pte) >> PxD_VALUE_SHIFT)));
}

#define pmd_populate(mm, pmd, pte_page) \
	pmd_populate_kernel(mm, pmd, page_address(pte_page))

#endif
