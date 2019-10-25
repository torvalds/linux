/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Copyright 2003 PathScale, Inc.
 * Derived from include/asm-i386/pgtable.h
 */

#ifndef __UM_PGTABLE_2LEVEL_H
#define __UM_PGTABLE_2LEVEL_H

#define __ARCH_USE_5LEVEL_HACK
#include <asm-generic/pgtable-nopmd.h>

/* PGDIR_SHIFT determines what a third-level page table entry can map */

#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the i386 is two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE	1024
#define USER_PTRS_PER_PGD ((TASK_SIZE + (PGDIR_SIZE - 1)) / PGDIR_SIZE)
#define PTRS_PER_PGD	1024
#define FIRST_USER_ADDRESS	0UL

#define pte_ERROR(e) \
        printk("%s:%d: bad pte %p(%08lx).\n", __FILE__, __LINE__, &(e), \
	       pte_val(e))
#define pgd_ERROR(e) \
        printk("%s:%d: bad pgd %p(%08lx).\n", __FILE__, __LINE__, &(e), \
	       pgd_val(e))

static inline int pgd_newpage(pgd_t pgd)	{ return 0; }
static inline void pgd_mkuptodate(pgd_t pgd)	{ }

#define set_pmd(pmdptr, pmdval) (*(pmdptr) = (pmdval))

#define pte_pfn(x) phys_to_pfn(pte_val(x))
#define pfn_pte(pfn, prot) __pte(pfn_to_phys(pfn) | pgprot_val(prot))
#define pfn_pmd(pfn, prot) __pmd(pfn_to_phys(pfn) | pgprot_val(prot))

#endif
