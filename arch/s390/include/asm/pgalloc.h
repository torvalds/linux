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
#include <linux/string.h>
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

static inline void crst_table_init(unsigned long *crst, unsigned long entry)
{
	memset64((u64 *)crst, entry, _CRST_ENTRIES);
}

int crst_table_upgrade(struct mm_struct *mm, unsigned long limit);

static inline unsigned long check_asce_limit(struct mm_struct *mm, unsigned long addr,
					     unsigned long len)
{
	int rc;

	if (addr + len > mm->context.asce_limit &&
	    addr + len <= TASK_SIZE) {
		rc = crst_table_upgrade(mm, addr + len);
		if (rc)
			return (unsigned long) rc;
	}
	return addr;
}

static inline p4d_t *p4d_alloc_one(struct mm_struct *mm, unsigned long address)
{
	unsigned long *table = crst_table_alloc(mm);

	if (table)
		crst_table_init(table, _REGION2_ENTRY_EMPTY);
	return (p4d_t *) table;
}

static inline void p4d_free(struct mm_struct *mm, p4d_t *p4d)
{
	if (!mm_p4d_folded(mm))
		crst_table_free(mm, (unsigned long *) p4d);
}

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long address)
{
	unsigned long *table = crst_table_alloc(mm);
	if (table)
		crst_table_init(table, _REGION3_ENTRY_EMPTY);
	return (pud_t *) table;
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	if (!mm_pud_folded(mm))
		crst_table_free(mm, (unsigned long *) pud);
}

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long vmaddr)
{
	unsigned long *table = crst_table_alloc(mm);

	if (!table)
		return NULL;
	crst_table_init(table, _SEGMENT_ENTRY_EMPTY);
	if (!pagetable_pmd_ctor(virt_to_ptdesc(table))) {
		crst_table_free(mm, table);
		return NULL;
	}
	return (pmd_t *) table;
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	if (mm_pmd_folded(mm))
		return;
	pagetable_pmd_dtor(virt_to_ptdesc(pmd));
	crst_table_free(mm, (unsigned long *) pmd);
}

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, p4d_t *p4d)
{
	set_pgd(pgd, __pgd(_REGION1_ENTRY | __pa(p4d)));
}

static inline void p4d_populate(struct mm_struct *mm, p4d_t *p4d, pud_t *pud)
{
	set_p4d(p4d, __p4d(_REGION2_ENTRY | __pa(pud)));
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud(_REGION3_ENTRY | __pa(pmd)));
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return (pgd_t *) crst_table_alloc(mm);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	crst_table_free(mm, (unsigned long *) pgd);
}

static inline void pmd_populate(struct mm_struct *mm,
				pmd_t *pmd, pgtable_t pte)
{
	set_pmd(pmd, __pmd(_SEGMENT_ENTRY | __pa(pte)));
}

#define pmd_populate_kernel(mm, pmd, pte) pmd_populate(mm, pmd, pte)

/*
 * page table entry allocation/free routines.
 */
#define pte_alloc_one_kernel(mm) ((pte_t *)page_table_alloc(mm))
#define pte_alloc_one(mm) ((pte_t *)page_table_alloc(mm))

#define pte_free_kernel(mm, pte) page_table_free(mm, (unsigned long *) pte)
#define pte_free(mm, pte) page_table_free(mm, (unsigned long *) pte)

/* arch use pte_free_defer() implementation in arch/s390/mm/pgalloc.c */
#define pte_free_defer pte_free_defer
void pte_free_defer(struct mm_struct *mm, pgtable_t pgtable);

void vmem_map_init(void);
void *vmem_crst_alloc(unsigned long val);
pte_t *vmem_pte_alloc(void);

unsigned long base_asce_alloc(unsigned long addr, unsigned long num_pages);
void base_asce_free(unsigned long asce);

#endif /* _S390_PGALLOC_H */
