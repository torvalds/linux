// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/err.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	pud = pud_alloc(mm, p4d, addr);
	if (pud)
		pte = (pte_t *)pmd_alloc(mm, pud, addr);

	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr,
		       unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_present(pgdp_get(pgd))) {
		p4d = p4d_offset(pgd, addr);
		if (p4d_present(p4dp_get(p4d))) {
			pud = pud_offset(p4d, addr);
			if (pud_present(pudp_get(pud)))
				pmd = pmd_offset(pud, addr);
		}
	}
	return (pte_t *) pmd;
}

uint64_t pmd_to_entrylo(unsigned long pmd_val)
{
	uint64_t val;
	/* PMD as PTE. Must be huge page */
	if (!pmd_leaf(__pmd(pmd_val)))
		panic("%s", __func__);

	val = pmd_val ^ _PAGE_HUGE;
	val |= ((val & _PAGE_HGLOBAL) >>
		(_PAGE_HGLOBAL_SHIFT - _PAGE_GLOBAL_SHIFT));

	return val;
}
