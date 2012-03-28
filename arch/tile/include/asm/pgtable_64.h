/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
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

#ifndef _ASM_TILE_PGTABLE_64_H
#define _ASM_TILE_PGTABLE_64_H

/* The level-0 page table breaks the address space into 32-bit chunks. */
#define PGDIR_SHIFT	HV_LOG2_L1_SPAN
#define PGDIR_SIZE	HV_L1_SPAN
#define PGDIR_MASK	(~(PGDIR_SIZE-1))
#define PTRS_PER_PGD	HV_L0_ENTRIES
#define SIZEOF_PGD	(PTRS_PER_PGD * sizeof(pgd_t))

/*
 * The level-1 index is defined by the huge page size.  A PMD is composed
 * of PTRS_PER_PMD pgd_t's and is the middle level of the page table.
 */
#define PMD_SHIFT	HV_LOG2_PAGE_SIZE_LARGE
#define PMD_SIZE	HV_PAGE_SIZE_LARGE
#define PMD_MASK	(~(PMD_SIZE-1))
#define PTRS_PER_PMD	(1 << (PGDIR_SHIFT - PMD_SHIFT))
#define SIZEOF_PMD	(PTRS_PER_PMD * sizeof(pmd_t))

/*
 * The level-2 index is defined by the difference between the huge
 * page size and the normal page size.  A PTE is composed of
 * PTRS_PER_PTE pte_t's and is the bottom level of the page table.
 * Note that the hypervisor docs use PTE for what we call pte_t, so
 * this nomenclature is somewhat confusing.
 */
#define PTRS_PER_PTE (1 << (HV_LOG2_PAGE_SIZE_LARGE - HV_LOG2_PAGE_SIZE_SMALL))
#define SIZEOF_PTE	(PTRS_PER_PTE * sizeof(pte_t))

/*
 * Align the vmalloc area to an L2 page table, and leave a guard page
 * at the beginning and end.  The vmalloc code also puts in an internal
 * guard page between each allocation.
 */
#define _VMALLOC_END	HUGE_VMAP_BASE
#define VMALLOC_END	(_VMALLOC_END - PAGE_SIZE)
#define VMALLOC_START	(_VMALLOC_START + PAGE_SIZE)

#define HUGE_VMAP_END	(HUGE_VMAP_BASE + PGDIR_SIZE)

#ifndef __ASSEMBLY__

/* We have no pud since we are a three-level page table. */
#include <asm-generic/pgtable-nopud.h>

static inline int pud_none(pud_t pud)
{
	return pud_val(pud) == 0;
}

static inline int pud_present(pud_t pud)
{
	return pud_val(pud) & _PAGE_PRESENT;
}

#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd 0x%016llx.\n", __FILE__, __LINE__, pmd_val(e))

static inline void pud_clear(pud_t *pudp)
{
	__pte_clear(&pudp->pgd);
}

static inline int pud_bad(pud_t pud)
{
	return ((pud_val(pud) & _PAGE_ALL) != _PAGE_TABLE);
}

/* Return the page-table frame number (ptfn) that a pud_t points at. */
#define pud_ptfn(pud) hv_pte_get_ptfn((pud).pgd)

/*
 * A given kernel pud_t maps to a kernel pmd_t table at a specific
 * virtual address.  Since kernel pmd_t tables can be aligned at
 * sub-page granularity, this macro can return non-page-aligned
 * pointers, despite its name.
 */
#define pud_page_vaddr(pud) \
	(__va((phys_addr_t)pud_ptfn(pud) << HV_LOG2_PAGE_TABLE_ALIGN))

/*
 * A pud_t points to a pmd_t array.  Since we can have multiple per
 * page, we don't have a one-to-one mapping of pud_t's to pages.
 */
#define pud_page(pud) pfn_to_page(HV_PTFN_TO_PFN(pud_ptfn(pud)))

static inline unsigned long pud_index(unsigned long address)
{
	return (address >> PUD_SHIFT) & (PTRS_PER_PUD - 1);
}

#define pmd_offset(pud, address) \
	((pmd_t *)pud_page_vaddr(*(pud)) + pmd_index(address))

/* Normalize an address to having the correct high bits set. */
#define pgd_addr_normalize pgd_addr_normalize
static inline unsigned long pgd_addr_normalize(unsigned long addr)
{
	return ((long)addr << (CHIP_WORD_SIZE() - CHIP_VA_WIDTH())) >>
		(CHIP_WORD_SIZE() - CHIP_VA_WIDTH());
}

/* We don't define any pgds for these addresses. */
static inline int pgd_addr_invalid(unsigned long addr)
{
	return addr >= MEM_HV_START ||
		(addr > MEM_LOW_END && addr < MEM_HIGH_START);
}

/*
 * Use atomic instructions to provide atomicity against the hypervisor.
 */
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static inline int ptep_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long addr, pte_t *ptep)
{
	return (__insn_fetchand(&ptep->val, ~HV_PTE_ACCESSED) >>
		HV_PTE_INDEX_ACCESSED) & 0x1;
}

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm,
				      unsigned long addr, pte_t *ptep)
{
	__insn_fetchand(&ptep->val, ~HV_PTE_WRITABLE);
}

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long addr, pte_t *ptep)
{
	return hv_pte(__insn_exch(&ptep->val, 0UL));
}

/*
 * pmds are the same as pgds and ptes, so converting is a no-op.
 */
#define pmd_pte(pmd) (pmd)
#define pmdp_ptep(pmdp) (pmdp)
#define pte_pmd(pte) (pte)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_TILE_PGTABLE_64_H */
