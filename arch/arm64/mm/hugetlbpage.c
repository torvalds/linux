/*
 * arch/arm64/mm/hugetlbpage.c
 *
 * Copyright (C) 2013 Linaro Ltd.
 *
 * Based on arch/x86/mm/hugetlbpage.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
#include <asm/pgalloc.h>

int pmd_huge(pmd_t pmd)
{
	return pmd_val(pmd) && !(pmd_val(pmd) & PMD_TABLE_BIT);
}

int pud_huge(pud_t pud)
{
#ifndef __PAGETABLE_PMD_FOLDED
	return pud_val(pud) && !(pud_val(pud) & PUD_TABLE_BIT);
#else
	return 0;
#endif
}

static int find_num_contig(struct mm_struct *mm, unsigned long addr,
			   pte_t *ptep, pte_t pte, size_t *pgsize)
{
	pgd_t *pgd = pgd_offset(mm, addr);
	pud_t *pud;
	pmd_t *pmd;

	*pgsize = PAGE_SIZE;
	if (!pte_cont(pte))
		return 1;
	pud = pud_offset(pgd, addr);
	pmd = pmd_offset(pud, addr);
	if ((pte_t *)pmd == ptep) {
		*pgsize = PMD_SIZE;
		return CONT_PMDS;
	}
	return CONT_PTES;
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
			    pte_t *ptep, pte_t pte)
{
	size_t pgsize;
	int i;
	int ncontig = find_num_contig(mm, addr, ptep, pte, &pgsize);
	unsigned long pfn;
	pgprot_t hugeprot;

	if (ncontig == 1) {
		set_pte_at(mm, addr, ptep, pte);
		return;
	}

	pfn = pte_pfn(pte);
	hugeprot = __pgprot(pte_val(pfn_pte(pfn, __pgprot(0))) ^ pte_val(pte));
	for (i = 0; i < ncontig; i++) {
		pr_debug("%s: set pte %p to 0x%llx\n", __func__, ptep,
			 pte_val(pfn_pte(pfn, hugeprot)));
		set_pte_at(mm, addr, ptep, pfn_pte(pfn, hugeprot));
		ptep++;
		pfn += pgsize >> PAGE_SHIFT;
		addr += pgsize;
	}
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
		      unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	pud_t *pud;
	pte_t *pte = NULL;

	pr_debug("%s: addr:0x%lx sz:0x%lx\n", __func__, addr, sz);
	pgd = pgd_offset(mm, addr);
	pud = pud_alloc(mm, pgd, addr);
	if (!pud)
		return NULL;

	if (sz == PUD_SIZE) {
		pte = (pte_t *)pud;
	} else if (sz == (PAGE_SIZE * CONT_PTES)) {
		pmd_t *pmd = pmd_alloc(mm, pud, addr);

		WARN_ON(addr & (sz - 1));
		/*
		 * Note that if this code were ever ported to the
		 * 32-bit arm platform then it will cause trouble in
		 * the case where CONFIG_HIGHPTE is set, since there
		 * will be no pte_unmap() to correspond with this
		 * pte_alloc_map().
		 */
		pte = pte_alloc_map(mm, NULL, pmd, addr);
	} else if (sz == PMD_SIZE) {
		if (IS_ENABLED(CONFIG_ARCH_WANT_HUGE_PMD_SHARE) &&
		    pud_none(*pud))
			pte = huge_pmd_share(mm, addr, pud);
		else
			pte = (pte_t *)pmd_alloc(mm, pud, addr);
	} else if (sz == (PMD_SIZE * CONT_PMDS)) {
		pmd_t *pmd;

		pmd = pmd_alloc(mm, pud, addr);
		WARN_ON(addr & (sz - 1));
		return (pte_t *)pmd;
	}

	pr_debug("%s: addr:0x%lx sz:0x%lx ret pte=%p/0x%llx\n", __func__, addr,
	       sz, pte, pte_val(*pte));
	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd = NULL;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	pr_debug("%s: addr:0x%lx pgd:%p\n", __func__, addr, pgd);
	if (!pgd_present(*pgd))
		return NULL;
	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
		return NULL;

	if (pud_huge(*pud))
		return (pte_t *)pud;
	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		return NULL;

	if (pte_cont(pmd_pte(*pmd))) {
		pmd = pmd_offset(
			pud, (addr & CONT_PMD_MASK));
		return (pte_t *)pmd;
	}
	if (pmd_huge(*pmd))
		return (pte_t *)pmd;
	pte = pte_offset_kernel(pmd, addr);
	if (pte_present(*pte) && pte_cont(*pte)) {
		pte = pte_offset_kernel(
			pmd, (addr & CONT_PTE_MASK));
		return pte;
	}
	return NULL;
}

pte_t arch_make_huge_pte(pte_t entry, struct vm_area_struct *vma,
			 struct page *page, int writable)
{
	size_t pagesize = huge_page_size(hstate_vma(vma));

	if (pagesize == CONT_PTE_SIZE) {
		entry = pte_mkcont(entry);
	} else if (pagesize == CONT_PMD_SIZE) {
		entry = pmd_pte(pmd_mkcont(pte_pmd(entry)));
	} else if (pagesize != PUD_SIZE && pagesize != PMD_SIZE) {
		pr_warn("%s: unrecognized huge page size 0x%lx\n",
			__func__, pagesize);
	}
	return entry;
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
			      unsigned long addr, pte_t *ptep)
{
	pte_t pte;

	if (pte_cont(*ptep)) {
		int ncontig, i;
		size_t pgsize;
		pte_t *cpte;
		bool is_dirty = false;

		cpte = huge_pte_offset(mm, addr);
		ncontig = find_num_contig(mm, addr, cpte, *cpte, &pgsize);
		/* save the 1st pte to return */
		pte = ptep_get_and_clear(mm, addr, cpte);
		for (i = 1, addr += pgsize; i < ncontig; ++i, addr += pgsize) {
			/*
			 * If HW_AFDBM is enabled, then the HW could
			 * turn on the dirty bit for any of the page
			 * in the set, so check them all.
			 */
			++cpte;
			if (pte_dirty(ptep_get_and_clear(mm, addr, cpte)))
				is_dirty = true;
		}
		if (is_dirty)
			return pte_mkdirty(pte);
		else
			return pte;
	} else {
		return ptep_get_and_clear(mm, addr, ptep);
	}
}

int huge_ptep_set_access_flags(struct vm_area_struct *vma,
			       unsigned long addr, pte_t *ptep,
			       pte_t pte, int dirty)
{
	pte_t *cpte;

	if (pte_cont(pte)) {
		int ncontig, i, changed = 0;
		size_t pgsize = 0;
		unsigned long pfn = pte_pfn(pte);
		/* Select all bits except the pfn */
		pgprot_t hugeprot =
			__pgprot(pte_val(pfn_pte(pfn, __pgprot(0))) ^
				 pte_val(pte));

		cpte = huge_pte_offset(vma->vm_mm, addr);
		pfn = pte_pfn(*cpte);
		ncontig = find_num_contig(vma->vm_mm, addr, cpte,
					  *cpte, &pgsize);
		for (i = 0; i < ncontig; ++i, ++cpte, addr += pgsize) {
			changed |= ptep_set_access_flags(vma, addr, cpte,
							pfn_pte(pfn,
								hugeprot),
							dirty);
			pfn += pgsize >> PAGE_SHIFT;
		}
		return changed;
	} else {
		return ptep_set_access_flags(vma, addr, ptep, pte, dirty);
	}
}

void huge_ptep_set_wrprotect(struct mm_struct *mm,
			     unsigned long addr, pte_t *ptep)
{
	if (pte_cont(*ptep)) {
		int ncontig, i;
		pte_t *cpte;
		size_t pgsize = 0;

		cpte = huge_pte_offset(mm, addr);
		ncontig = find_num_contig(mm, addr, cpte, *cpte, &pgsize);
		for (i = 0; i < ncontig; ++i, ++cpte, addr += pgsize)
			ptep_set_wrprotect(mm, addr, cpte);
	} else {
		ptep_set_wrprotect(mm, addr, ptep);
	}
}

void huge_ptep_clear_flush(struct vm_area_struct *vma,
			   unsigned long addr, pte_t *ptep)
{
	if (pte_cont(*ptep)) {
		int ncontig, i;
		pte_t *cpte;
		size_t pgsize = 0;

		cpte = huge_pte_offset(vma->vm_mm, addr);
		ncontig = find_num_contig(vma->vm_mm, addr, cpte,
					  *cpte, &pgsize);
		for (i = 0; i < ncontig; ++i, ++cpte, addr += pgsize)
			ptep_clear_flush(vma, addr, cpte);
	} else {
		ptep_clear_flush(vma, addr, ptep);
	}
}

static __init int setup_hugepagesz(char *opt)
{
	unsigned long ps = memparse(opt, &opt);

	if (ps == PMD_SIZE) {
		hugetlb_add_hstate(PMD_SHIFT - PAGE_SHIFT);
	} else if (ps == PUD_SIZE) {
		hugetlb_add_hstate(PUD_SHIFT - PAGE_SHIFT);
	} else {
		pr_err("hugepagesz: Unsupported page size %lu K\n", ps >> 10);
		return 0;
	}
	return 1;
}
__setup("hugepagesz=", setup_hugepagesz);
