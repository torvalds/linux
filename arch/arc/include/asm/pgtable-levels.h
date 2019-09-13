/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Synopsys, Inc. (www.synopsys.com)
 */

/*
 * Helpers for implemenintg paging levels
 */

#ifndef _ASM_ARC_PGTABLE_LEVELS_H
#define _ASM_ARC_PGTABLE_LEVELS_H

/*
 * 2 level paging setup for software walked MMUv3 (ARC700) and MMUv4 (HS)
 *
 * [31]            32 bit virtual address              [0]
 * -------------------------------------------------------
 * |               | <---------- PGDIR_SHIFT ----------> |
 * |               |                | <-- PAGE_SHIFT --> |
 * -------------------------------------------------------
 *       |                  |                |
 *       |                  |                --> off in page frame
 *       |                  ---> index into Page Table
 *       ----> index into Page Directory
 *
 * Given software walk, the vaddr split is arbitrary set to 11:8:13
 * However enabling of super page in a 2 level regime pegs PGDIR_SHIFT to
 * super page size.
 */

#if defined(CONFIG_ARC_HUGEPAGE_16M)
#define PGDIR_SHIFT		24
#elif defined(CONFIG_ARC_HUGEPAGE_2M)
#define PGDIR_SHIFT		21
#else
/*
 * No Super page case
 * Default value provides 11:8:13 (8K), 11:9:12 (4K)
 */
#define PGDIR_SHIFT		21

#endif

#define PGDIR_SIZE		BIT(PGDIR_SHIFT)	/* vaddr span, not PDG sz */
#define PGDIR_MASK		(~(PGDIR_SIZE - 1))

#define PTRS_PER_PGD		BIT(32 - PGDIR_SHIFT)

#define PTRS_PER_PTE		BIT(PGDIR_SHIFT - PAGE_SHIFT)

#ifndef __ASSEMBLY__

#include <asm-generic/pgtable-nopmd.h>

/*
 * 1st level paging: pgd
 */
#define pgd_index(addr)		((addr) >> PGDIR_SHIFT)
#define pgd_offset(mm, addr)	(((mm)->pgd) + pgd_index(addr))
#define pgd_offset_k(addr)	pgd_offset(&init_mm, addr)
#define pgd_ERROR(e) \
	pr_crit("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Due to the strange way generic pgtable level folding works, in a 2 level
 * setup, pmd_val() returns pgd, so these pmd_* macros actually work on pgd
 */
#define pmd_none(x)		(!pmd_val(x))
#define pmd_bad(x)		((pmd_val(x) & ~PAGE_MASK))
#define pmd_present(x)		(pmd_val(x))
#define pmd_clear(xp)		do { pmd_val(*(xp)) = 0; } while (0)
#define pmd_page_vaddr(pmd)	(pmd_val(pmd) & PAGE_MASK)
#define pmd_page(pmd)		virt_to_page(pmd_page_vaddr(pmd))
#define set_pmd(pmdp, pmd)	(*(pmdp) = pmd)
#define pmd_pgtable(pmd)	((pgtable_t) pmd_page_vaddr(pmd))

#define pte_ERROR(e) \
	pr_crit("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))

#define pte_none(x)		(!pte_val(x))
#define pte_present(x)		(pte_val(x) & _PAGE_PRESENT)
#define pte_clear(mm,addr,ptep)	set_pte_at(mm, addr, ptep, __pte(0))
#define pte_page(pte)		pfn_to_page(pte_pfn(pte))
#define set_pte(ptep, pte)	((*(ptep)) = (pte))
#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)
#define pfn_pte(pfn, prot)	__pte(__pfn_to_phys(pfn) | pgprot_val(prot))
#define mk_pte(page, prot)	pfn_pte(page_to_pfn(page), prot)

#ifdef CONFIG_ISA_ARCV2
#define pmd_leaf(x)		(pmd_val(x) & _PAGE_HW_SZ)
#endif

#endif	/* !__ASSEMBLY__ */

#endif
