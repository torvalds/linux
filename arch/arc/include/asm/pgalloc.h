/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * vineetg: June 2011
 *  -"/proc/meminfo | grep PageTables" kept on increasing
 *   Recently added pgtable dtor was not getting called.
 *
 * vineetg: May 2011
 *  -Variable pg-sz means that Page Tables could be variable sized themselves
 *    So calculate it based on addr traversal split [pgd-bits:pte-bits:xxx]
 *  -Page Table size capped to max 1 to save memory - hence verified.
 *  -Since these deal with constants, gcc compile-time optimizes them.
 *
 * vineetg: Nov 2010
 *  -Added pgtable ctor/dtor used for pgtable mem accounting
 *
 * vineetg: April 2010
 *  -Switched pgtable_t from being struct page * to unsigned long
 *      =Needed so that Page Table allocator (pte_alloc_one) is not forced to
 *       deal with struct page. That way in future we can make it allocate
 *       multiple PG Tbls in one Page Frame
 *      =sweet side effect is avoiding calls to ugly page_address( ) from the
 *       pg-tlb allocator sub-sys (pte_alloc_one, ptr_free, pmd_populate)
 *
 *  Amit Bhor, Sameer Dhavale: Codito Technologies 2004
 */

#ifndef _ASM_ARC_PGALLOC_H
#define _ASM_ARC_PGALLOC_H

#include <linux/mm.h>
#include <linux/log2.h>
#include <asm-generic/pgalloc.h>

static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	/*
	 * The cast to long below is OK in 32-bit PAE40 regime with long long pte
	 * Despite "wider" pte, the pte table needs to be in non-PAE low memory
	 * as all higher levels can only hold long pointers.
	 *
	 * The cast itself is needed given simplistic definition of set_pmd()
	 */
	set_pmd(pmd, __pmd((unsigned long)pte));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, pgtable_t pte_page)
{
	set_pmd(pmd, __pmd((unsigned long)page_address(pte_page)));
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret = __pgd_alloc(mm, 0);

	if (ret) {
		int num, num2;

		num = USER_PTRS_PER_PGD + USER_KERNEL_GUTTER / PGDIR_SIZE;
		num2 = VMALLOC_SIZE / PGDIR_SIZE;
		memcpy(ret + num, swapper_pg_dir + num, num2 * sizeof(pgd_t));
	}
	return ret;
}

#if CONFIG_PGTABLE_LEVELS > 3

static inline void p4d_populate(struct mm_struct *mm, p4d_t *p4dp, pud_t *pudp)
{
	set_p4d(p4dp, __p4d((unsigned long)pudp));
}

#define __pud_free_tlb(tlb, pmd, addr)  pud_free((tlb)->mm, pmd)

#endif

#if CONFIG_PGTABLE_LEVELS > 2

static inline void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmdp)
{
	set_pud(pudp, __pud((unsigned long)pmdp));
}

#define __pmd_free_tlb(tlb, pmd, addr)  pmd_free((tlb)->mm, pmd)

#endif

#define __pte_free_tlb(tlb, pte, addr)  pte_free((tlb)->mm, pte)

#endif /* _ASM_ARC_PGALLOC_H */
