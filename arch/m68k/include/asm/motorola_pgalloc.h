/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MOTOROLA_PGALLOC_H
#define _MOTOROLA_PGALLOC_H

#include <asm/tlb.h>
#include <asm/tlbflush.h>

extern void mmu_page_ctor(void *page);
extern void mmu_page_dtor(void *page);

extern pmd_t *get_pointer_table(void);
extern int free_pointer_table(pmd_t *);

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	return (pte_t *)get_pointer_table();
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_pointer_table((void *)pte);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	return (pte_t *)get_pointer_table();
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pgtable)
{
	free_pointer_table((void *)pgtable);
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t pgtable,
				  unsigned long address)
{
	free_pointer_table((void *)pgtable);
}


static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	return get_pointer_table();
}

static inline int pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	return free_pointer_table(pmd);
}

static inline int __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd,
				 unsigned long address)
{
	return free_pointer_table(pmd);
}


static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	pmd_free(mm, (pmd_t *)pgd);
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return (pgd_t *)get_pointer_table();
}


static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	pmd_set(pmd, pte);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, pgtable_t page)
{
	pmd_set(pmd, page);
}
#define pmd_pgtable(pmd) ((pgtable_t)__pmd_page(pmd))

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	pud_set(pud, pmd);
}

#endif /* _MOTOROLA_PGALLOC_H */
