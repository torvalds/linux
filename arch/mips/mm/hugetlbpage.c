/*
 * MIPS Huge TLB Page Support for Kernel.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 * Copyright 2005, Embedded Alley Solutions, Inc.
 * Matt Porter <mporter@embeddedalley.com>
 * Copyright (C) 2008, 2009 Cavium Networks, Inc.
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/err.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr,
		      unsigned long sz)
{
	pgd_t *pgd;
	pud_t *pud;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	pud = pud_alloc(mm, pgd, addr);
	if (pud)
		pte = (pte_t *)pmd_alloc(mm, pud, addr);

	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_present(*pgd)) {
		pud = pud_offset(pgd, addr);
		if (pud_present(*pud))
			pmd = pmd_offset(pud, addr);
	}
	return (pte_t *) pmd;
}

int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	return 0;
}

/*
 * This function checks for proper alignment of input addr and len parameters.
 */
int is_aligned_hugepage_range(unsigned long addr, unsigned long len)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	return 0;
}

struct page *
follow_huge_addr(struct mm_struct *mm, unsigned long address, int write)
{
	return ERR_PTR(-EINVAL);
}

int pmd_huge(pmd_t pmd)
{
	return (pmd_val(pmd) & _PAGE_HUGE) != 0;
}

int pud_huge(pud_t pud)
{
	return (pud_val(pud) & _PAGE_HUGE) != 0;
}

int pmd_huge_support(void)
{
	return 1;
}

struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	struct page *page;

	page = pte_page(*(pte_t *)pmd);
	if (page)
		page += ((address & ~HPAGE_MASK) >> PAGE_SHIFT);
	return page;
}
