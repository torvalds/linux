/* MN10300 Page and page table/directory allocation
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_PGALLOC_H
#define _ASM_PGALLOC_H

#include <asm/page.h>
#include <linux/threads.h>
#include <linux/mm.h>		/* for struct page */

struct mm_struct;
struct page;

/* attach a page table to a PMD entry */
#define pmd_populate_kernel(mm, pmd, pte) \
	set_pmd(pmd, __pmd(__pa(pte) | _PAGE_TABLE))

static inline
void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *pte)
{
	set_pmd(pmd, __pmd((page_to_pfn(pte) << PAGE_SHIFT) | _PAGE_TABLE));
}
#define pmd_pgtable(pmd) pmd_page(pmd)

/*
 * Allocate and free page tables.
 */

extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *, pgd_t *);

extern pte_t *pte_alloc_one_kernel(struct mm_struct *, unsigned long);
extern struct page *pte_alloc_one(struct mm_struct *, unsigned long);

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long) pte);
}

static inline void pte_free(struct mm_struct *mm, struct page *pte)
{
	pgtable_page_dtor(pte);
	__free_page(pte);
}


#define __pte_free_tlb(tlb, pte, addr) tlb_remove_page((tlb), (pte))

#endif /* _ASM_PGALLOC_H */
