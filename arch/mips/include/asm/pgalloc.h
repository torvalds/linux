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

#define __HAVE_ARCH_PMD_ALLOC_ONE
#define __HAVE_ARCH_PUD_ALLOC_ONE
#include <asm-generic/pgalloc.h>

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

/*
 * Initialize a new pmd table with invalid pointers.
 */
extern void pmd_init(void *addr);

#ifndef __PAGETABLE_PMD_FOLDED

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud((unsigned long)pmd));
}
#endif

/*
 * Initialize a new pgd table with invalid pointers.
 */
extern void pgd_init(void *addr);
extern pgd_t *pgd_alloc(struct mm_struct *mm);

#define __pte_free_tlb(tlb, pte, address)	\
	tlb_remove_ptdesc((tlb), page_ptdesc(pte))

#ifndef __PAGETABLE_PMD_FOLDED

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pmd_t *pmd;
	struct ptdesc *ptdesc;

	ptdesc = pagetable_alloc(GFP_KERNEL_ACCOUNT, PMD_TABLE_ORDER);
	if (!ptdesc)
		return NULL;

	if (!pagetable_pmd_ctor(mm, ptdesc)) {
		pagetable_free(ptdesc);
		return NULL;
	}

	pmd = ptdesc_address(ptdesc);
	pmd_init(pmd);
	return pmd;
}

#define __pmd_free_tlb(tlb, x, addr)	pmd_free((tlb)->mm, x)

#endif

#ifndef __PAGETABLE_PUD_FOLDED

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pud_t *pud;
	struct ptdesc *ptdesc = pagetable_alloc(GFP_KERNEL & ~__GFP_HIGHMEM,
			PUD_TABLE_ORDER);

	if (!ptdesc)
		return NULL;
	pagetable_pud_ctor(ptdesc);
	pud = ptdesc_address(ptdesc);

	pud_init(pud);
	return pud;
}

static inline void p4d_populate(struct mm_struct *mm, p4d_t *p4d, pud_t *pud)
{
	set_p4d(p4d, __p4d((unsigned long)pud));
}

#define __pud_free_tlb(tlb, x, addr)	pud_free((tlb)->mm, x)

#endif /* __PAGETABLE_PUD_FOLDED */

extern void pagetable_init(void);

#endif /* _ASM_PGALLOC_H */
