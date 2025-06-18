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
#include <asm-generic/pgalloc.h>

/* Allocate the top level pgd (page directory) */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return __pgd_alloc(mm, PGD_TABLE_ORDER);
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
	struct ptdesc *ptdesc;
	gfp_t gfp = GFP_PGTABLE_USER;

	if (mm == &init_mm)
		gfp = GFP_PGTABLE_KERNEL;
	ptdesc = pagetable_alloc(gfp, PMD_TABLE_ORDER);
	if (!ptdesc)
		return NULL;
	if (!pagetable_pmd_ctor(mm, ptdesc)) {
		pagetable_free(ptdesc);
		return NULL;
	}
	return ptdesc_address(ptdesc);
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
