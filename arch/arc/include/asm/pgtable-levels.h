/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Synopsys, Inc. (www.synopsys.com)
 */

/*
 * Helpers for implemenintg paging levels
 */

#ifndef _ASM_ARC_PGTABLE_LEVELS_H
#define _ASM_ARC_PGTABLE_LEVELS_H

#if CONFIG_PGTABLE_LEVELS == 2

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
 * Default value provides 11:8:13 (8K), 10:10:12 (4K)
 * Limits imposed by pgtable_t only PAGE_SIZE long
 * (so 4K page can only have 1K entries: or 10 bits)
 */
#ifdef CONFIG_ARC_PAGE_SIZE_4K
#define PGDIR_SHIFT		22
#else
#define PGDIR_SHIFT		21
#endif

#endif

#else /* CONFIG_PGTABLE_LEVELS != 2 */

/*
 * A default 3 level paging testing setup in software walked MMU
 *   MMUv4 (8K page): <4> : <7> : <8> : <13>
 */
#define PGDIR_SHIFT		28
#if CONFIG_PGTABLE_LEVELS > 2
#define PMD_SHIFT		21
#endif

#endif /* CONFIG_PGTABLE_LEVELS */

#define PGDIR_SIZE		BIT(PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE - 1))
#define PTRS_PER_PGD		BIT(32 - PGDIR_SHIFT)

#if CONFIG_PGTABLE_LEVELS > 2
#define PMD_SIZE		BIT(PMD_SHIFT)
#define PMD_MASK		(~(PMD_SIZE - 1))
#define PTRS_PER_PMD		BIT(PGDIR_SHIFT - PMD_SHIFT)
#endif

#define PTRS_PER_PTE		BIT(PMD_SHIFT - PAGE_SHIFT)

#ifndef __ASSEMBLY__

#if CONFIG_PGTABLE_LEVELS > 2
#include <asm-generic/pgtable-nopud.h>
#else
#include <asm-generic/pgtable-nopmd.h>
#endif

/*
 * 1st level paging: pgd
 */
#define pgd_index(addr)		((addr) >> PGDIR_SHIFT)
#define pgd_offset(mm, addr)	(((mm)->pgd) + pgd_index(addr))
#define pgd_offset_k(addr)	pgd_offset(&init_mm, addr)
#define pgd_ERROR(e) \
	pr_crit("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

#if CONFIG_PGTABLE_LEVELS > 2

/* In 3 level paging, pud_* macros work on pgd */
#define pud_none(x)		(!pud_val(x))
#define pud_bad(x)		((pud_val(x) & ~PAGE_MASK))
#define pud_present(x)		(pud_val(x))
#define pud_clear(xp)		do { pud_val(*(xp)) = 0; } while (0)
#define pud_pgtable(pud)	((pmd_t *)(pud_val(pud) & PAGE_MASK))
#define pud_page(pud)		virt_to_page(pud_pgtable(pud))
#define set_pud(pudp, pud)	(*(pudp) = pud)

/*
 * 2nd level paging: pmd
 */
#define pmd_ERROR(e) \
	pr_crit("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))

#define pmd_pfn(pmd)		((pmd_val(pmd) & PMD_MASK) >> PAGE_SHIFT)
#define pfn_pmd(pfn,prot)	__pmd(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define mk_pmd(page,prot)	pfn_pmd(page_to_pfn(page),prot)

#endif

/*
 * Due to the strange way generic pgtable level folding works, the pmd_* macros
 *  - are valid even for 2 levels (which supposedly only has pgd - pte)
 *  - behave differently for 2 vs. 3
 * In 2  level paging        (pgd -> pte), pmd_* macros work on pgd
 * In 3+ level paging (pgd -> pmd -> pte), pmd_* macros work on pmd
 */
#define pmd_none(x)		(!pmd_val(x))
#define pmd_bad(x)		((pmd_val(x) & ~PAGE_MASK))
#define pmd_present(x)		(pmd_val(x))
#define pmd_clear(xp)		do { pmd_val(*(xp)) = 0; } while (0)
#define pmd_page_vaddr(pmd)	(pmd_val(pmd) & PAGE_MASK)
#define pmd_page(pmd)		virt_to_page(pmd_page_vaddr(pmd))
#define set_pmd(pmdp, pmd)	(*(pmdp) = pmd)
#define pmd_pgtable(pmd)	((pgtable_t) pmd_page_vaddr(pmd))

/*
 * 3rd level paging: pte
 */
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
