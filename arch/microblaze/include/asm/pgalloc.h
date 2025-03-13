/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_PGALLOC_H
#define _ASM_MICROBLAZE_PGALLOC_H

#include <linux/kernel.h>	/* For min/max macros */
#include <linux/highmem.h>
#include <linux/pgtable.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/cache.h>

#define __HAVE_ARCH_PTE_ALLOC_ONE_KERNEL
#include <asm-generic/pgalloc.h>

extern void __bad_pte(pmd_t *pmd);

#define pgd_alloc(mm)		__pgd_alloc(mm, 0)

extern pte_t *pte_alloc_one_kernel(struct mm_struct *mm);

#define __pte_free_tlb(tlb, pte, addr)	pte_free((tlb)->mm, (pte))

#define pmd_populate(mm, pmd, pte) \
			(pmd_val(*(pmd)) = (unsigned long)page_address(pte))

#define pmd_populate_kernel(mm, pmd, pte) \
		(pmd_val(*(pmd)) = (unsigned long) (pte))

#endif /* _ASM_MICROBLAZE_PGALLOC_H */
