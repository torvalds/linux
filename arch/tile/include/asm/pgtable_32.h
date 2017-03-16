/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 */

#ifndef _ASM_TILE_PGTABLE_32_H
#define _ASM_TILE_PGTABLE_32_H

/*
 * The level-1 index is defined by the huge page size.  A PGD is composed
 * of PTRS_PER_PGD pgd_t's and is the top level of the page table.
 */
#define PGDIR_SHIFT	HPAGE_SHIFT
#define PGDIR_SIZE	HPAGE_SIZE
#define PGDIR_MASK	(~(PGDIR_SIZE-1))
#define PTRS_PER_PGD	_HV_L1_ENTRIES(HPAGE_SHIFT)
#define PGD_INDEX(va)	_HV_L1_INDEX(va, HPAGE_SHIFT)
#define SIZEOF_PGD	_HV_L1_SIZE(HPAGE_SHIFT)

/*
 * The level-2 index is defined by the difference between the huge
 * page size and the normal page size.  A PTE is composed of
 * PTRS_PER_PTE pte_t's and is the bottom level of the page table.
 * Note that the hypervisor docs use PTE for what we call pte_t, so
 * this nomenclature is somewhat confusing.
 */
#define PTRS_PER_PTE	_HV_L2_ENTRIES(HPAGE_SHIFT, PAGE_SHIFT)
#define PTE_INDEX(va)	_HV_L2_INDEX(va, HPAGE_SHIFT, PAGE_SHIFT)
#define SIZEOF_PTE	_HV_L2_SIZE(HPAGE_SHIFT, PAGE_SHIFT)

#ifndef __ASSEMBLY__

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 *
 * HOWEVER, if we are using an allocation scheme with slop after the
 * end of the page table (e.g. where our L2 page tables are 2KB but
 * our pages are 64KB and we are allocating via the page allocator)
 * we can't extend it easily.
 */
#define LAST_PKMAP PTRS_PER_PTE

#define PKMAP_BASE   ((FIXADDR_BOOT_START - PAGE_SIZE*LAST_PKMAP) & PGDIR_MASK)

#ifdef CONFIG_HIGHMEM
# define _VMALLOC_END	(PKMAP_BASE & ~(HPAGE_SIZE-1))
#else
# define _VMALLOC_END	(FIXADDR_START & ~(HPAGE_SIZE-1))
#endif

/*
 * Align the vmalloc area to an L2 page table, and leave a guard page
 * at the beginning and end.  The vmalloc code also puts in an internal
 * guard page between each allocation.
 */
#define VMALLOC_END	(_VMALLOC_END - PAGE_SIZE)
extern unsigned long VMALLOC_RESERVE /* = CONFIG_VMALLOC_RESERVE */;
#define _VMALLOC_START	(_VMALLOC_END - VMALLOC_RESERVE)
#define VMALLOC_START	(_VMALLOC_START + PAGE_SIZE)

/* This is the maximum possible amount of lowmem. */
#define MAXMEM		(_VMALLOC_START - PAGE_OFFSET)

/* We have no pmd or pud since we are strictly a two-level page table */
#define __ARCH_USE_5LEVEL_HACK
#include <asm-generic/pgtable-nopmd.h>

static inline int pud_huge_page(pud_t pud)	{ return 0; }

/* We don't define any pgds for these addresses. */
static inline int pgd_addr_invalid(unsigned long addr)
{
	return addr >= MEM_HV_START;
}

/*
 * Provide versions of these routines that can be used safely when
 * the hypervisor may be asynchronously modifying dirty/accessed bits.
 * ptep_get_and_clear() matches the generic one but we provide it to
 * be parallel with the 64-bit code.
 */
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define __HAVE_ARCH_PTEP_SET_WRPROTECT

extern int ptep_test_and_clear_young(struct vm_area_struct *,
				     unsigned long addr, pte_t *);
extern void ptep_set_wrprotect(struct mm_struct *,
			       unsigned long addr, pte_t *);

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;
	pte_clear(_mm, addr, ptep);
	return pte;
}

/*
 * pmds are wrappers around pgds, which are the same as ptes.
 * It's often convenient to "cast" back and forth and use the pte methods,
 * which are the methods supplied by the hypervisor.
 */
#define pmd_pte(pmd) ((pmd).pud.pgd)
#define pmdp_ptep(pmdp) (&(pmdp)->pud.pgd)
#define pte_pmd(pte) ((pmd_t){ { (pte) } })

#endif /* __ASSEMBLY__ */

#endif /* _ASM_TILE_PGTABLE_32_H */
