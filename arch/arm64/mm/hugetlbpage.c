// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm64/mm/hugetlbpage.c
 *
 * Copyright (C) 2013 Linaro Ltd.
 *
 * Based on arch/x86/mm/hugetlbpage.c.
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

/*
 * HugeTLB Support Matrix
 *
 * ---------------------------------------------------
 * | Page Size | CONT PTE |  PMD  | CONT PMD |  PUD  |
 * ---------------------------------------------------
 * |     4K    |   64K    |   2M  |    32M   |   1G  |
 * |    16K    |    2M    |  32M  |     1G   |       |
 * |    64K    |    2M    | 512M  |    16G   |       |
 * ---------------------------------------------------
 */

/*
 * Reserve CMA areas for the largest supported gigantic
 * huge page when requested. Any other smaller gigantic
 * huge pages could still be served from those areas.
 */
#ifdef CONFIG_CMA
void __init arm64_hugetlb_cma_reserve(void)
{
	int order;

#ifdef CONFIG_ARM64_4K_PAGES
	order = PUD_SHIFT - PAGE_SHIFT;
#else
	order = CONT_PMD_SHIFT - PAGE_SHIFT;
#endif
	/*
	 * HugeTLB CMA reservation is required for gigantic
	 * huge pages which could not be allocated via the
	 * page allocator. Just warn if there is any change
	 * breaking this assumption.
	 */
	WARN_ON(order <= MAX_ORDER);
	hugetlb_cma_reserve(order);
}
#endif /* CONFIG_CMA */

#ifdef CONFIG_ARCH_ENABLE_HUGEPAGE_MIGRATION
bool arch_hugetlb_migration_supported(struct hstate *h)
{
	size_t pagesize = huge_page_size(h);

	switch (pagesize) {
#ifdef CONFIG_ARM64_4K_PAGES
	case PUD_SIZE:
#endif
	case PMD_SIZE:
	case CONT_PMD_SIZE:
	case CONT_PTE_SIZE:
		return true;
	}
	pr_warn("%s: unrecognized huge page size 0x%lx\n",
			__func__, pagesize);
	return false;
}
#endif

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
	pgd_t *pgdp = pgd_offset(mm, addr);
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;

	*pgsize = PAGE_SIZE;
	p4dp = p4d_offset(pgdp, addr);
	pudp = pud_offset(p4dp, addr);
	pmdp = pmd_offset(pudp, addr);
	if ((pte_t *)pmdp == ptep) {
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
	pte_t orig_pte = huge_ptep_get(ptep);
	bool valid = pte_valid(orig_pte);
	unsigned long i, saddr = addr;

	for (i = 0; i < ncontig; i++, addr += pgsize, ptep++) {
		pte_t pte = ptep_get_and_clear(mm, addr, ptep);

		/*
		 * If HW_AFDBM is enabled, then the HW could turn on
		 * the dirty or accessed bit for any page in the set,
		 * so check them all.
		 */
		if (pte_dirty(pte))
			orig_pte = pte_mkdirty(orig_pte);

		if (pte_young(pte))
			orig_pte = pte_mkyoung(orig_pte);
	}

	if (valid) {
		struct vm_area_struct vma = TLB_FLUSH_VMA(mm, 0);
		flush_tlb_range(&vma, saddr, addr);
	}
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
	struct vm_area_struct vma = TLB_FLUSH_VMA(mm, 0);
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

	for (i = 0; i < ncontig; i++, ptep++, addr += pgsize, pfn += dpfn)
		set_pte_at(mm, addr, ptep, pfn_pte(pfn, hugeprot));
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

pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, unsigned long sz)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep = NULL;

	pgdp = pgd_offset(mm, addr);
	p4dp = p4d_offset(pgdp, addr);
	pudp = pud_alloc(mm, p4dp, addr);
	if (!pudp)
		return NULL;

	if (sz == PUD_SIZE) {
		ptep = (pte_t *)pudp;
	} else if (sz == (CONT_PTE_SIZE)) {
		pmdp = pmd_alloc(mm, pudp, addr);
		if (!pmdp)
			return NULL;

		WARN_ON(addr & (sz - 1));
		/*
		 * Note that if this code were ever ported to the
		 * 32-bit arm platform then it will cause trouble in
		 * the case where CONFIG_HIGHPTE is set, since there
		 * will be no pte_unmap() to correspond with this
		 * pte_alloc_map().
		 */
		ptep = pte_alloc_map(mm, pmdp, addr);
	} else if (sz == PMD_SIZE) {
		if (want_pmd_share(vma, addr) && pud_none(READ_ONCE(*pudp)))
			ptep = huge_pmd_share(mm, vma, addr, pudp);
		else
			ptep = (pte_t *)pmd_alloc(mm, pudp, addr);
	} else if (sz == (CONT_PMD_SIZE)) {
		pmdp = pmd_alloc(mm, pudp, addr);
		WARN_ON(addr & (sz - 1));
		return (pte_t *)pmdp;
	}

	return ptep;
}

pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr, unsigned long sz)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp, pud;
	pmd_t *pmdp, pmd;

	pgdp = pgd_offset(mm, addr);
	if (!pgd_present(READ_ONCE(*pgdp)))
		return NULL;

	p4dp = p4d_offset(pgdp, addr);
	if (!p4d_present(READ_ONCE(*p4dp)))
		return NULL;

	pudp = pud_offset(p4dp, addr);
	pud = READ_ONCE(*pudp);
	if (sz != PUD_SIZE && pud_none(pud))
		return NULL;
	/* hugepage or swap? */
	if (pud_huge(pud) || !pud_present(pud))
		return (pte_t *)pudp;
	/* table; check the next level */

	if (sz == CONT_PMD_SIZE)
		addr &= CONT_PMD_MASK;

	pmdp = pmd_offset(pudp, addr);
	pmd = READ_ONCE(*pmdp);
	if (!(sz == PMD_SIZE || sz == CONT_PMD_SIZE) &&
	    pmd_none(pmd))
		return NULL;
	if (pmd_huge(pmd) || !pmd_present(pmd))
		return (pte_t *)pmdp;

	if (sz == CONT_PTE_SIZE)
		return pte_offset_kernel(pmdp, (addr & CONT_PTE_MASK));

	return NULL;
}

pte_t arch_make_huge_pte(pte_t entry, unsigned int shift, vm_flags_t flags)
{
	size_t pagesize = 1UL << shift;

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

/*
 * huge_ptep_set_access_flags will update access flags (dirty, accesssed)
 * and write permission.
 *
 * For a contiguous huge pte range we need to check whether or not write
 * permission has to change only on the first pte in the set. Then for
 * all the contiguous ptes we need to check whether or not there is a
 * discrepancy between dirty or young.
 */
static int __cont_access_flags_changed(pte_t *ptep, pte_t pte, int ncontig)
{
	int i;

	if (pte_write(pte) != pte_write(huge_ptep_get(ptep)))
		return 1;

	for (i = 0; i < ncontig; i++) {
		pte_t orig_pte = huge_ptep_get(ptep + i);

		if (pte_dirty(pte) != pte_dirty(orig_pte))
			return 1;

		if (pte_young(pte) != pte_young(orig_pte))
			return 1;
	}

	return 0;
}

int huge_ptep_set_access_flags(struct vm_area_struct *vma,
			       unsigned long addr, pte_t *ptep,
			       pte_t pte, int dirty)
{
	int ncontig, i;
	size_t pgsize = 0;
	unsigned long pfn = pte_pfn(pte), dpfn;
	pgprot_t hugeprot;
	pte_t orig_pte;

	if (!pte_cont(pte))
		return ptep_set_access_flags(vma, addr, ptep, pte, dirty);

	ncontig = find_num_contig(vma->vm_mm, addr, ptep, &pgsize);
	dpfn = pgsize >> PAGE_SHIFT;

	if (!__cont_access_flags_changed(ptep, pte, ncontig))
		return 0;

	orig_pte = get_clear_flush(vma->vm_mm, addr, ptep, pgsize, ncontig);

	/* Make sure we don't lose the dirty or young state */
	if (pte_dirty(orig_pte))
		pte = pte_mkdirty(pte);

	if (pte_young(orig_pte))
		pte = pte_mkyoung(pte);

	hugeprot = pte_pgprot(pte);
	for (i = 0; i < ncontig; i++, ptep++, addr += pgsize, pfn += dpfn)
		set_pte_at(vma->vm_mm, addr, ptep, pfn_pte(pfn, hugeprot));

	return 1;
}

void huge_ptep_set_wrprotect(struct mm_struct *mm,
			     unsigned long addr, pte_t *ptep)
{
	unsigned long pfn, dpfn;
	pgprot_t hugeprot;
	int ncontig, i;
	size_t pgsize;
	pte_t pte;

	if (!pte_cont(READ_ONCE(*ptep))) {
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

	if (!pte_cont(READ_ONCE(*ptep))) {
		ptep_clear_flush(vma, addr, ptep);
		return;
	}

	ncontig = find_num_contig(vma->vm_mm, addr, ptep, &pgsize);
	clear_flush(vma->vm_mm, addr, ptep, pgsize, ncontig);
}

static int __init hugetlbpage_init(void)
{
#ifdef CONFIG_ARM64_4K_PAGES
	hugetlb_add_hstate(PUD_SHIFT - PAGE_SHIFT);
#endif
	hugetlb_add_hstate(CONT_PMD_SHIFT - PAGE_SHIFT);
	hugetlb_add_hstate(PMD_SHIFT - PAGE_SHIFT);
	hugetlb_add_hstate(CONT_PTE_SHIFT - PAGE_SHIFT);

	return 0;
}
arch_initcall(hugetlbpage_init);

bool __init arch_hugetlb_valid_size(unsigned long size)
{
	switch (size) {
#ifdef CONFIG_ARM64_4K_PAGES
	case PUD_SIZE:
#endif
	case CONT_PMD_SIZE:
	case PMD_SIZE:
	case CONT_PTE_SIZE:
		return true;
	}

	return false;
}
