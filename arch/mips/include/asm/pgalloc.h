/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2001, 2003 by Ralf Baechle
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_PGALLOC_H
#define _ASM_PGALLOC_H

#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/sched.h>

#include <asm-generic/pgalloc.h>	/* for pte_{alloc,free}_one */

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
	pte_t *pte)
{
	set_pmd(pmd, __pmd((unsigned long)pte));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
	pgtable_t pte)
{
	set_pmd(pmd, __pmd((unsigned long)page_address(pte)));
}
#define pmd_pgtable(pmd) pmd_page(pmd)

/*
 * Initialize a new pmd table with invalid pointers.
 */
extern void pmd_init(unsigned long page, unsigned long pagetable);

#ifndef __PAGETABLE_PMD_FOLDED

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud((unsigned long)pmd));
}
#endif

/*
 * Initialize a new pgd / pmd table with invalid pointers.
 */
extern void pgd_init(unsigned long page);
extern pgd_t *pgd_alloc(struct mm_struct *mm);

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_pages((unsigned long)pgd, PGD_ORDER);
}

#define __pte_free_tlb(tlb,pte,address)			\
do {							\
	pgtable_page_dtor(pte);				\
	tlb_remove_page((tlb), pte);			\
} while (0)

#ifndef __PAGETABLE_PMD_FOLDED

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pmd_t *pmd;

	pmd = (pmd_t *) __get_free_pages(GFP_KERNEL, PMD_ORDER);
	if (pmd)
		pmd_init((unsigned long)pmd, (unsigned long)invalid_pte_table);
	return pmd;
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	free_pages((unsigned long)pmd, PMD_ORDER);
}

#define __pmd_free_tlb(tlb, x, addr)	pmd_free((tlb)->mm, x)

#endif

#ifndef __PAGETABLE_PUD_FOLDED

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pud_t *pud;

	pud = (pud_t *) __get_free_pages(GFP_KERNEL, PUD_ORDER);
	if (pud)
		pud_init((unsigned long)pud, (unsigned long)invalid_pmd_table);
	return pud;
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	free_pages((unsigned long)pud, PUD_ORDER);
}

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pud_t *pud)
{
	set_pgd(pgd, __pgd((unsigned long)pud));
}

#define __pud_free_tlb(tlb, x, addr)	pud_free((tlb)->mm, x)

#endif /* __PAGETABLE_PUD_FOLDED */

#define check_pgt_cache()	do { } while (0)

extern void pagetable_init(void);

#endif /* _ASM_PGALLOC_H */
