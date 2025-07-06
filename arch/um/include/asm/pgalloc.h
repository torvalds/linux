/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Copyright 2003 PathScale, Inc.
 * Derived from include/asm-i386/pgalloc.h and include/asm-i386/pgtable.h
 */

#ifndef __UM_PGALLOC_H
#define __UM_PGALLOC_H

#include <linux/mm.h>

#include <asm-generic/pgalloc.h>

#define pmd_populate_kernel(mm, pmd, pte) \
	set_pmd(pmd, __pmd(_PAGE_TABLE + (unsigned long) __pa(pte)))

#define pmd_populate(mm, pmd, pte) 				\
	set_pmd(pmd, __pmd(_PAGE_TABLE +			\
		((unsigned long long)page_to_pfn(pte) <<	\
			(unsigned long long) PAGE_SHIFT)))

/*
 * Allocate and free page tables.
 */
extern pgd_t *pgd_alloc(struct mm_struct *);

#define __pte_free_tlb(tlb, pte, address)	\
	tlb_remove_ptdesc((tlb), page_ptdesc(pte))

#if CONFIG_PGTABLE_LEVELS > 2

#define __pmd_free_tlb(tlb, pmd, address)	\
	tlb_remove_ptdesc((tlb), virt_to_ptdesc(pmd))

#if CONFIG_PGTABLE_LEVELS > 3

#define __pud_free_tlb(tlb, pud, address)	\
	tlb_remove_ptdesc((tlb), virt_to_ptdesc(pud))

#endif
#endif

#endif

