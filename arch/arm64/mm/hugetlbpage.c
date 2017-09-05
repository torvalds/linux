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

/*
 * Select all bits except the pfn
 */
static inline pgprot_t pte_pgprot(pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);

	return __pgprot(pte_val(pfn_pte(pfn, __pgprot(0))) ^ pte_val(pte));
}

static int find_num_contig(struct mm_struct *mm, unsigned long addr,
			   pte_t *ptep, size_t *pgsize)
{
	pgd_t *pgd = pgd_offset(mm, addr);
	pud_t *pud;
	pmd_t *pmd;

	*pgsize = PAGE_SIZE;
	pud = pud_offset(pgd, addr);
	pmd = pmd_offset(pud, addr);
	if ((pte_t *)pmd == ptep) {
		*pgsize = PMD_SIZE;
		return CONT_PMDS;
	}
	return CONT_PTES;
}

static inline int num_contig_ptes(unsigned long size, size_t *pgsize)
{
	int contig_ptes = 0;

	*pgsize = size;

	switch (size) {
#ifdef CONFIG_ARM64_4K_PAGES
	case PUD_SIZE:
#endif
	case PMD_SIZE:
		contig_ptes = 1;
		break;
	case CONT_PMD_SIZE:
		*pgsize = PMD_SIZE;
		contig_ptes = CONT_PMDS;
		break;
	case CONT_PTE_SIZE:
		*pgsize = PAGE_SIZE;
		contig_ptes = CONT_PTES;
		break;
	}

	return contig_ptes;
}

/*
 * Changing some bits of contiguous entries requires us to follow a
 * Break-Before-Make approach, breaking the whole contiguous set
 * before we can change any entries. See ARM DDI 0487A.k_iss10775,
 * "Misprogramming of the Contiguous bit", page D4-1762.
 *
 * This helper performs the break step.
 */
static pte_t get_clear_flush(struct mm_struct *mm,
			     unsigned long addr,
			     pte_t *ptep,
			     unsigned long pgsize,
			     unsigned long ncontig)
{
	struct vm_area_struct vma = { .vm_mm = mm };
	pte_t orig_pte = huge_ptep_get(ptep);
	bool valid = pte_valid(orig_pte);
	unsigned long i, saddr = addr;

	for (i = 0; i < ncontig; i++, addr += pgsize, ptep++) {
		pte_t pte = ptep_get_and_clear(mm, addr, ptep);

		/*
		 * If HW_AFDBM is enabled, then the HW could turn on
		 * the dirty bit for any page in the set, so check
		 * them all.  All hugetlb entries are already young.
		 */
		if (pte_dirty(pte))
			orig_pte = pte_mkdirty(orig_pte);
	}

	if (valid)
		flush_tlb_range(&vma, saddr, addr);
	return orig_pte;
}

/*
 * Changing some bits of contiguous entries requires us to follow a
 * Break-Before-Make approach, breaking the whole contiguous set
 * before we can change any entries. See ARM DDI 0487A.k_iss10775,
 * "Misprogramming of the Contiguous bit", page D4-1762.
 *
 * This helper performs the break step for use cases where the
 * original pte is not needed.
 */
static void clear_flush(struct mm_struct *mm,
			     unsigned long addr,
			     pte_t *ptep,
			     unsigned long pgsize,
			     unsigned long ncontig)
{
	struct vm_area_struct vma = { .vm_mm = mm };
	unsigned long i, saddr = addr;

	for (i = 0; i < ncontig; i++, addr += pgsize, ptep++)
		pte_clear(mm, addr, ptep);

	flush_tlb_range(&vma, saddr, addr);
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
			    pte_t *ptep, pte_t pte)
{
	size_t pgsize;
	int i;
	int ncontig;
	unsigned long pfn, dpfn;
	pgprot_t hugeprot;

	/*
	 * Code needs to be expanded to handle huge swap and migration
	 * entries. Needed for HUGETLB and MEMORY_FAILURE.
	 */
	WARN_ON(!pte_present(pte));

	if (!pte_cont(pte)) {
		set_pte_at(mm, addr, ptep, pte);
		return;
	}

	ncontig = find_num_contig(mm, addr, ptep, &pgsize);
	pfn = pte_pfn(pte);
	dpfn = pgsize >> PAGE_SHIFT;
	hugeprot = pte_pgprot(pte);

	clear_flush(mm, addr, ptep, pgsize, ncontig);

	for (i = 0; i < ncontig; i++, ptep++, addr += pgsize, pfn += dpfn) {
		pr_debug("%s: set pte %p to 0x%llx\n", __func__, ptep,
			 pte_val(pfn_pte(pfn, hugeprot)));
		set_pte_at(mm, addr, ptep, pfn_pte(pfn, hugeprot));
	}
}

void set_huge_swap_pte_at(struct mm_struct *mm, unsigned long addr,
			  pte_t *ptep, pte_t pte, unsigned long sz)
{
	int i, ncontig;
	size_t pgsize;

	ncontig = num_contig_ptes(sz, &pgsize);

	for (i = 0; i < ncontig; i++, ptep++)
		set_pte(ptep, pte);
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
		pte = pte_alloc_map(mm, pmd, addr);
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

pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	pr_debug("%s: addr:0x%lx pgd:%p\n", __func__, addr, pgd);
	if (!pgd_present(*pgd))
		return NULL;

	pud = pud_offset(pgd, addr);
	if (sz != PUD_SIZE && pud_none(*pud))
		return NULL;
	/* hugepage or swap? */
	if (pud_huge(*pud) || !pud_present(*pud))
		return (pte_t *)pud;
	/* table; check the next level */

	if (sz == CONT_PMD_SIZE)
		addr &= CONT_PMD_MASK;

	pmd = pmd_offset(pud, addr);
	if (!(sz == PMD_SIZE || sz == CONT_PMD_SIZE) &&
	    pmd_none(*pmd))
		return NULL;
	if (pmd_huge(*pmd) || !pmd_present(*pmd))
		return (pte_t *)pmd;

	if (sz == CONT_PTE_SIZE) {
		pte_t *pte = pte_offset_kernel(pmd, (addr & CONT_PTE_MASK));
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

void huge_pte_clear(struct mm_struct *mm, unsigned long addr,
		    pte_t *ptep, unsigned long sz)
{
	int i, ncontig;
	size_t pgsize;

	ncontig = num_contig_ptes(sz, &pgsize);

	for (i = 0; i < ncontig; i++, addr += pgsize, ptep++)
		pte_clear(mm, addr, ptep);
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
			      unsigned long addr, pte_t *ptep)
{
	int ncontig;
	size_t pgsize;
	pte_t orig_pte = huge_ptep_get(ptep);

	if (!pte_cont(orig_pte))
		return ptep_get_and_clear(mm, addr, ptep);

	ncontig = find_num_contig(mm, addr, ptep, &pgsize);

	return get_clear_flush(mm, addr, ptep, pgsize, ncontig);
}

int huge_ptep_set_access_flags(struct vm_area_struct *vma,
			       unsigned long addr, pte_t *ptep,
			       pte_t pte, int dirty)
{
	int ncontig, i, changed = 0;
	size_t pgsize = 0;
	unsigned long pfn = pte_pfn(pte), dpfn;
	pgprot_t hugeprot;
	pte_t orig_pte;

	if (!pte_cont(pte))
		return ptep_set_access_flags(vma, addr, ptep, pte, dirty);

	ncontig = find_num_contig(vma->vm_mm, addr, ptep, &pgsize);
	dpfn = pgsize >> PAGE_SHIFT;

	orig_pte = get_clear_flush(vma->vm_mm, addr, ptep, pgsize, ncontig);
	if (!pte_same(orig_pte, pte))
		changed = 1;

	/* Make sure we don't lose the dirty state */
	if (pte_dirty(orig_pte))
		pte = pte_mkdirty(pte);

	hugeprot = pte_pgprot(pte);
	for (i = 0; i < ncontig; i++, ptep++, addr += pgsize, pfn += dpfn)
		set_pte_at(vma->vm_mm, addr, ptep, pfn_pte(pfn, hugeprot));

	return changed;
}

void huge_ptep_set_wrprotect(struct mm_struct *mm,
			     unsigned long addr, pte_t *ptep)
{
	unsigned long pfn, dpfn;
	pgprot_t hugeprot;
	int ncontig, i;
	size_t pgsize;
	pte_t pte;

	if (!pte_cont(*ptep)) {
		ptep_set_wrprotect(mm, addr, ptep);
		return;
	}

	ncontig = find_num_contig(mm, addr, ptep, &pgsize);
	dpfn = pgsize >> PAGE_SHIFT;

	pte = get_clear_flush(mm, addr, ptep, pgsize, ncontig);
	pte = pte_wrprotect(pte);

	hugeprot = pte_pgprot(pte);
	pfn = pte_pfn(pte);

	for (i = 0; i < ncontig; i++, ptep++, addr += pgsize, pfn += dpfn)
		set_pte_at(mm, addr, ptep, pfn_pte(pfn, hugeprot));
}

void huge_ptep_clear_flush(struct vm_area_struct *vma,
			   unsigned long addr, pte_t *ptep)
{
	size_t pgsize;
	int ncontig;

	if (!pte_cont(*ptep)) {
		ptep_clear_flush(vma, addr, ptep);
		return;
	}

	ncontig = find_num_contig(vma->vm_mm, addr, ptep, &pgsize);
	clear_flush(vma->vm_mm, addr, ptep, pgsize, ncontig);
}

static __init int setup_hugepagesz(char *opt)
{
	unsigned long ps = memparse(opt, &opt);

	switch (ps) {
#ifdef CONFIG_ARM64_4K_PAGES
	case PUD_SIZE:
#endif
	case PMD_SIZE * CONT_PMDS:
	case PMD_SIZE:
	case PAGE_SIZE * CONT_PTES:
		hugetlb_add_hstate(ilog2(ps) - PAGE_SHIFT);
		return 1;
	}

	hugetlb_bad_size();
	pr_err("hugepagesz: Unsupported page size %lu K\n", ps >> 10);
	return 0;
}
__setup("hugepagesz=", setup_hugepagesz);

#ifdef CONFIG_ARM64_64K_PAGES
static __init int add_default_hugepagesz(void)
{
	if (size_to_hstate(CONT_PTES * PAGE_SIZE) == NULL)
		hugetlb_add_hstate(CONT_PTE_SHIFT);
	return 0;
}
arch_initcall(add_default_hugepagesz);
#endif
