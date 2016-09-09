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

unsigned long *crst_table_alloc(struct mm_struct *);
void crst_table_free(struct mm_struct *, unsigned long *);

unsigned long *page_table_alloc(struct mm_struct *);
void page_table_free(struct mm_struct *, unsigned long *);
void page_table_free_rcu(struct mmu_gather *, unsigned long *, unsigned long);
extern int page_table_allocate_pgste;

static inline void clear_table(unsigned long *s, unsigned long val, size_t n)
{
	typedef struct { char _[n]; } addrtype;

	*s = val;
	n = (n / 256) - 1;
	asm volatile(
		"	mvc	8(248,%0),0(%0)\n"
		"0:	mvc	256(256,%0),0(%0)\n"
		"	la	%0,256(%0)\n"
		"	brct	%1,0b\n"
		: "+a" (s), "+d" (n), "=m" (*(addrtype *) s)
		: "m" (*(addrtype *) s));
}

static inline void crst_table_init(unsigned long *crst, unsigned long entry)
{
	clear_table(crst, entry, sizeof(unsigned long)*2048);
}

static inline unsigned long pgd_entry_type(struct mm_struct *mm)
{
	if (mm->context.asce_limit <= (1UL << 31))
		return _SEGMENT_ENTRY_EMPTY;
	if (mm->context.asce_limit <= (1UL << 42))
		return _REGION3_ENTRY_EMPTY;
	return _REGION2_ENTRY_EMPTY;
}

int crst_table_upgrade(struct mm_struct *);
void crst_table_downgrade(struct mm_struct *);

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

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pud_t *pud)
{
	pgd_val(*pgd) = _REGION2_ENTRY | __pa(pud);
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
	if (mm->context.asce_limit == (1UL << 31)) {
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
	if (mm->context.asce_limit == (1UL << 31))
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

#endif /* _S390_PGALLOC_H */
