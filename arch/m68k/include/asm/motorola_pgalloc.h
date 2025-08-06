/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MOTOROLA_PGALLOC_H
#define _MOTOROLA_PGALLOC_H

#include <asm/tlb.h>
#include <asm/tlbflush.h>

extern void mmu_page_ctor(void *page);
extern void mmu_page_dtor(void *page);

enum m68k_table_types {
	TABLE_PGD,
	TABLE_PMD,
	TABLE_PTE,
};

extern void init_pointer_table(void *table, int type);
extern void *get_pointer_table(struct mm_struct *mm, int type);
extern int free_pointer_table(void *table, int type);

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	return get_pointer_table(mm, TABLE_PTE);
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_pointer_table(pte, TABLE_PTE);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	return get_pointer_table(mm, TABLE_PTE);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pgtable)
{
	free_pointer_table(pgtable, TABLE_PTE);
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t pgtable,
				  unsigned long address)
{
	free_pointer_table(pgtable, TABLE_PTE);
}


static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	return get_pointer_table(mm, TABLE_PMD);
}

static inline int pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	return free_pointer_table(pmd, TABLE_PMD);
}

static inline int __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd,
				 unsigned long address)
{
	return free_pointer_table(pmd, TABLE_PMD);
}


static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_pointer_table(pgd, TABLE_PGD);
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return get_pointer_table(mm, TABLE_PGD);
}


static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	pmd_set(pmd, pte);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, pgtable_t page)
{
	pmd_set(pmd, page);
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	pud_set(pud, pmd);
}

#endif /* _MOTOROLA_PGALLOC_H */
