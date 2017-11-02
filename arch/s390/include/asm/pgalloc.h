/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/pgalloc.h"
 *    Copyright (C) 1994  Linus Torvalds
 */

#ifndef _S390_PGALLOC_H
#define _S390_PGALLOC_H

#include <linux/threads.h>
#include <linux/gfp.h>
#include <linux/mm.h>

#define CRST_ALLOC_ORDER 2

unsigned long *crst_table_alloc(struct mm_struct *);
void crst_table_free(struct mm_struct *, unsigned long *);

unsigned long *page_table_alloc(struct mm_struct *);
struct page *page_table_alloc_pgste(struct mm_struct *mm);
void page_table_free(struct mm_struct *, unsigned long *);
void page_table_free_rcu(struct mmu_gather *, unsigned long *, unsigned long);
void page_table_free_pgste(struct page *page);
extern int page_table_allocate_pgste;

static inline void clear_table(unsigned long *s, unsigned long val, size_t n)
{
	struct addrtype { char _[256]; };
	int i;

	for (i = 0; i < n; i += 256) {
		*s = val;
		asm volatile(
			"mvc	8(248,%[s]),0(%[s])\n"
			: "+m" (*(struct addrtype *) s)
			: [s] "a" (s));
		s += 256 / sizeof(long);
	}
}

static inline void crst_table_init(unsigned long *crst, unsigned long entry)
{
	clear_table(crst, entry, _CRST_TABLE_SIZE);
}

static inline unsigned long pgd_entry_type(struct mm_struct *mm)
{
	if (mm->context.asce_limit <= _REGION3_SIZE)
		return _SEGMENT_ENTRY_EMPTY;
	if (mm->context.asce_limit <= _REGION2_SIZE)
		return _REGION3_ENTRY_EMPTY;
	if (mm->context.asce_limit <= _REGION1_SIZE)
		return _REGION2_ENTRY_EMPTY;
	return _REGION1_ENTRY_EMPTY;
}

int crst_table_upgrade(struct mm_struct *mm, unsigned long limit);
void crst_table_downgrade(struct mm_struct *);

static inline p4d_t *p4d_alloc_one(struct mm_struct *mm, unsigned long address)
{
	unsigned long *table = crst_table_alloc(mm);

	if (table)
		crst_table_init(table, _REGION2_ENTRY_EMPTY);
	return (p4d_t *) table;
}
#define p4d_free(mm, p4d) crst_table_free(mm, (unsigned long *) p4d)

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long address)
{
	unsigned long *table = crst_table_alloc(mm);
	if (table)
		crst_table_init(table, _REGION3_ENTRY_EMPTY);
	return (pud_t *) table;
}
#define pud_free(mm, pud) crst_table_free(mm, (unsigned long *) pud)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long vmaddr)
{
	unsigned long *table = crst_table_alloc(mm);

	if (!table)
		return NULL;
	crst_table_init(table, _SEGMENT_ENTRY_EMPTY);
	if (!pgtable_pmd_page_ctor(virt_to_page(table))) {
		crst_table_free(mm, table);
		return NULL;
	}
	return (pmd_t *) table;
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	pgtable_pmd_page_dtor(virt_to_page(pmd));
	crst_table_free(mm, (unsigned long *) pmd);
}

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, p4d_t *p4d)
{
	pgd_val(*pgd) = _REGION1_ENTRY | __pa(p4d);
}

static inline void p4d_populate(struct mm_struct *mm, p4d_t *p4d, pud_t *pud)
{
	p4d_val(*p4d) = _REGION2_ENTRY | __pa(pud);
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	pud_val(*pud) = _REGION3_ENTRY | __pa(pmd);
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	unsigned long *table = crst_table_alloc(mm);

	if (!table)
		return NULL;
	if (mm->context.asce_limit == _REGION3_SIZE) {
		/* Forking a compat process with 2 page table levels */
		if (!pgtable_pmd_page_ctor(virt_to_page(table))) {
			crst_table_free(mm, table);
			return NULL;
		}
	}
	return (pgd_t *) table;
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	if (mm->context.asce_limit == _REGION3_SIZE)
		pgtable_pmd_page_dtor(virt_to_page(pgd));
	crst_table_free(mm, (unsigned long *) pgd);
}

static inline void pmd_populate(struct mm_struct *mm,
				pmd_t *pmd, pgtable_t pte)
{
	pmd_val(*pmd) = _SEGMENT_ENTRY + __pa(pte);
}

#define pmd_populate_kernel(mm, pmd, pte) pmd_populate(mm, pmd, pte)

#define pmd_pgtable(pmd) \
	(pgtable_t)(pmd_val(pmd) & -sizeof(pte_t)*PTRS_PER_PTE)

/*
 * page table entry allocation/free routines.
 */
#define pte_alloc_one_kernel(mm, vmaddr) ((pte_t *) page_table_alloc(mm))
#define pte_alloc_one(mm, vmaddr) ((pte_t *) page_table_alloc(mm))

#define pte_free_kernel(mm, pte) page_table_free(mm, (unsigned long *) pte)
#define pte_free(mm, pte) page_table_free(mm, (unsigned long *) pte)

extern void rcu_table_freelist_finish(void);

void vmem_map_init(void);
void *vmem_crst_alloc(unsigned long val);
pte_t *vmem_pte_alloc(void);

#endif /* _S390_PGALLOC_H */
