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

unsigned long *crst_table_alloc_noprof(struct mm_struct *);
#define crst_table_alloc(...)	alloc_hooks(crst_table_alloc_noprof(__VA_ARGS__))
void crst_table_free(struct mm_struct *, unsigned long *);

unsigned long *page_table_alloc_noprof(struct mm_struct *);
#define page_table_alloc(...)	alloc_hooks(page_table_alloc_noprof(__VA_ARGS__))
void page_table_free(struct mm_struct *, unsigned long *);

struct ptdesc *page_table_alloc_pgste_noprof(struct mm_struct *mm);
#define page_table_alloc_pgste(...)	alloc_hooks(page_table_alloc_pgste_noprof(__VA_ARGS__))
void page_table_free_pgste(struct ptdesc *ptdesc);

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

static inline p4d_t *p4d_alloc_one_noprof(struct mm_struct *mm, unsigned long address)
{
	unsigned long *table = crst_table_alloc_noprof(mm);

	if (!table)
		return NULL;
	crst_table_init(table, _REGION2_ENTRY_EMPTY);
	pagetable_p4d_ctor(virt_to_ptdesc(table));

	return (p4d_t *) table;
}
#define p4d_alloc_one(...)	alloc_hooks(p4d_alloc_one_noprof(__VA_ARGS__))

static inline void p4d_free(struct mm_struct *mm, p4d_t *p4d)
{
	if (mm_p4d_folded(mm))
		return;

	pagetable_dtor(virt_to_ptdesc(p4d));
	crst_table_free(mm, (unsigned long *) p4d);
}

static inline pud_t *pud_alloc_one_noprof(struct mm_struct *mm, unsigned long address)
{
	unsigned long *table = crst_table_alloc_noprof(mm);

	if (!table)
		return NULL;
	crst_table_init(table, _REGION3_ENTRY_EMPTY);
	pagetable_pud_ctor(virt_to_ptdesc(table));

	return (pud_t *) table;
}
#define pud_alloc_one(...)	alloc_hooks(pud_alloc_one_noprof(__VA_ARGS__))

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	if (mm_pud_folded(mm))
		return;

	pagetable_dtor(virt_to_ptdesc(pud));
	crst_table_free(mm, (unsigned long *) pud);
}

static inline pmd_t *pmd_alloc_one_noprof(struct mm_struct *mm, unsigned long vmaddr)
{
	unsigned long *table = crst_table_alloc_noprof(mm);

	if (!table)
		return NULL;
	crst_table_init(table, _SEGMENT_ENTRY_EMPTY);
	if (!pagetable_pmd_ctor(mm, virt_to_ptdesc(table))) {
		crst_table_free(mm, table);
		return NULL;
	}
	return (pmd_t *) table;
}
#define pmd_alloc_one(...)	alloc_hooks(pmd_alloc_one_noprof(__VA_ARGS__))

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	if (mm_pmd_folded(mm))
		return;
	pagetable_dtor(virt_to_ptdesc(pmd));
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

static inline pgd_t *pgd_alloc_noprof(struct mm_struct *mm)
{
	unsigned long *table = crst_table_alloc_noprof(mm);

	if (!table)
		return NULL;
	pagetable_pgd_ctor(virt_to_ptdesc(table));

	return (pgd_t *) table;
}
#define pgd_alloc(...)	alloc_hooks(pgd_alloc_noprof(__VA_ARGS__))

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	pagetable_dtor(virt_to_ptdesc(pgd));
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
