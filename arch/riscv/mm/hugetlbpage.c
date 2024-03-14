// SPDX-License-Identifier: GPL-2.0
#include <linux/hugetlb.h>
#include <linux/err.h>

#ifdef CONFIG_RISCV_ISA_SVNAPOT
pte_t huge_ptep_get(pte_t *ptep)
{
	unsigned long pte_num;
	int i;
	pte_t orig_pte = ptep_get(ptep);

	if (!pte_present(orig_pte) || !pte_napot(orig_pte))
		return orig_pte;

	pte_num = napot_pte_num(napot_cont_order(orig_pte));

	for (i = 0; i < pte_num; i++, ptep++) {
		pte_t pte = ptep_get(ptep);

		if (pte_dirty(pte))
			orig_pte = pte_mkdirty(orig_pte);

		if (pte_young(pte))
			orig_pte = pte_mkyoung(orig_pte);
	}

	return orig_pte;
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
		      struct vm_area_struct *vma,
		      unsigned long addr,
		      unsigned long sz)
{
	unsigned long order;
	pte_t *pte = NULL;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return NULL;

	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return NULL;

	if (sz == PUD_SIZE) {
		pte = (pte_t *)pud;
		goto out;
	}

	if (sz == PMD_SIZE) {
		if (want_pmd_share(vma, addr) && pud_none(*pud))
			pte = huge_pmd_share(mm, vma, addr, pud);
		else
			pte = (pte_t *)pmd_alloc(mm, pud, addr);
		goto out;
	}

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return NULL;

	for_each_napot_order(order) {
		if (napot_cont_size(order) == sz) {
			pte = pte_alloc_huge(mm, pmd, addr & napot_cont_mask(order));
			break;
		}
	}

out:
	if (pte) {
		pte_t pteval = ptep_get_lockless(pte);

		WARN_ON_ONCE(pte_present(pteval) && !pte_huge(pteval));
	}
	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr,
		       unsigned long sz)
{
	unsigned long order;
	pte_t *pte = NULL;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (!p4d_present(*p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (sz == PUD_SIZE)
		/* must be pud huge, non-present or none */
		return (pte_t *)pud;

	if (!pud_present(*pud))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (sz == PMD_SIZE)
		/* must be pmd huge, non-present or none */
		return (pte_t *)pmd;

	if (!pmd_present(*pmd))
		return NULL;

	for_each_napot_order(order) {
		if (napot_cont_size(order) == sz) {
			pte = pte_offset_huge(pmd, addr & napot_cont_mask(order));
			break;
		}
	}
	return pte;
}

unsigned long hugetlb_mask_last_page(struct hstate *h)
{
	unsigned long hp_size = huge_page_size(h);

	switch (hp_size) {
#ifndef __PAGETABLE_PMD_FOLDED
	case PUD_SIZE:
		return P4D_SIZE - PUD_SIZE;
#endif
	case PMD_SIZE:
		return PUD_SIZE - PMD_SIZE;
	case napot_cont_size(NAPOT_CONT64KB_ORDER):
		return PMD_SIZE - napot_cont_size(NAPOT_CONT64KB_ORDER);
	default:
		break;
	}

	return 0UL;
}

static pte_t get_clear_contig(struct mm_struct *mm,
			      unsigned long addr,
			      pte_t *ptep,
			      unsigned long pte_num)
{
	pte_t orig_pte = ptep_get(ptep);
	unsigned long i;

	for (i = 0; i < pte_num; i++, addr += PAGE_SIZE, ptep++) {
		pte_t pte = ptep_get_and_clear(mm, addr, ptep);

		if (pte_dirty(pte))
			orig_pte = pte_mkdirty(orig_pte);

		if (pte_young(pte))
			orig_pte = pte_mkyoung(orig_pte);
	}

	return orig_pte;
}

static pte_t get_clear_contig_flush(struct mm_struct *mm,
				    unsigned long addr,
				    pte_t *ptep,
				    unsigned long pte_num)
{
	pte_t orig_pte = get_clear_contig(mm, addr, ptep, pte_num);
	struct vm_area_struct vma = TLB_FLUSH_VMA(mm, 0);
	bool valid = !pte_none(orig_pte);

	if (valid)
		flush_tlb_range(&vma, addr, addr + (PAGE_SIZE * pte_num));

	return orig_pte;
}

pte_t arch_make_huge_pte(pte_t entry, unsigned int shift, vm_flags_t flags)
{
	unsigned long order;

	for_each_napot_order(order) {
		if (shift == napot_cont_shift(order)) {
			entry = pte_mknapot(entry, order);
			break;
		}
	}
	if (order == NAPOT_ORDER_MAX)
		entry = pte_mkhuge(entry);

	return entry;
}

static void clear_flush(struct mm_struct *mm,
			unsigned long addr,
			pte_t *ptep,
			unsigned long pgsize,
			unsigned long ncontig)
{
	struct vm_area_struct vma = TLB_FLUSH_VMA(mm, 0);
	unsigned long i, saddr = addr;

	for (i = 0; i < ncontig; i++, addr += pgsize, ptep++)
		ptep_get_and_clear(mm, addr, ptep);

	flush_tlb_range(&vma, saddr, addr);
}

/*
 * When dealing with NAPOT mappings, the privileged specification indicates that
 * "if an update needs to be made, the OS generally should first mark all of the
 * PTEs invalid, then issue SFENCE.VMA instruction(s) covering all 4 KiB regions
 * within the range, [...] then update the PTE(s), as described in Section
 * 4.2.1.". That's the equivalent of the Break-Before-Make approach used by
 * arm64.
 */
void set_huge_pte_at(struct mm_struct *mm,
		     unsigned long addr,
		     pte_t *ptep,
		     pte_t pte,
		     unsigned long sz)
{
	unsigned long hugepage_shift, pgsize;
	int i, pte_num;

	if (sz >= PGDIR_SIZE)
		hugepage_shift = PGDIR_SHIFT;
	else if (sz >= P4D_SIZE)
		hugepage_shift = P4D_SHIFT;
	else if (sz >= PUD_SIZE)
		hugepage_shift = PUD_SHIFT;
	else if (sz >= PMD_SIZE)
		hugepage_shift = PMD_SHIFT;
	else
		hugepage_shift = PAGE_SHIFT;

	pte_num = sz >> hugepage_shift;
	pgsize = 1 << hugepage_shift;

	if (!pte_present(pte)) {
		for (i = 0; i < pte_num; i++, ptep++, addr += pgsize)
			set_ptes(mm, addr, ptep, pte, 1);
		return;
	}

	if (!pte_napot(pte)) {
		set_ptes(mm, addr, ptep, pte, 1);
		return;
	}

	clear_flush(mm, addr, ptep, pgsize, pte_num);

	for (i = 0; i < pte_num; i++, ptep++, addr += pgsize)
		set_pte_at(mm, addr, ptep, pte);
}

int huge_ptep_set_access_flags(struct vm_area_struct *vma,
			       unsigned long addr,
			       pte_t *ptep,
			       pte_t pte,
			       int dirty)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long order;
	pte_t orig_pte;
	int i, pte_num;

	if (!pte_napot(pte))
		return ptep_set_access_flags(vma, addr, ptep, pte, dirty);

	order = napot_cont_order(pte);
	pte_num = napot_pte_num(order);
	ptep = huge_pte_offset(mm, addr, napot_cont_size(order));
	orig_pte = get_clear_contig_flush(mm, addr, ptep, pte_num);

	if (pte_dirty(orig_pte))
		pte = pte_mkdirty(pte);

	if (pte_young(orig_pte))
		pte = pte_mkyoung(pte);

	for (i = 0; i < pte_num; i++, addr += PAGE_SIZE, ptep++)
		set_pte_at(mm, addr, ptep, pte);

	return true;
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
			      unsigned long addr,
			      pte_t *ptep)
{
	pte_t orig_pte = ptep_get(ptep);
	int pte_num;

	if (!pte_napot(orig_pte))
		return ptep_get_and_clear(mm, addr, ptep);

	pte_num = napot_pte_num(napot_cont_order(orig_pte));

	return get_clear_contig(mm, addr, ptep, pte_num);
}

void huge_ptep_set_wrprotect(struct mm_struct *mm,
			     unsigned long addr,
			     pte_t *ptep)
{
	pte_t pte = ptep_get(ptep);
	unsigned long order;
	pte_t orig_pte;
	int i, pte_num;

	if (!pte_napot(pte)) {
		ptep_set_wrprotect(mm, addr, ptep);
		return;
	}

	order = napot_cont_order(pte);
	pte_num = napot_pte_num(order);
	ptep = huge_pte_offset(mm, addr, napot_cont_size(order));
	orig_pte = get_clear_contig_flush(mm, addr, ptep, pte_num);

	orig_pte = pte_wrprotect(orig_pte);

	for (i = 0; i < pte_num; i++, addr += PAGE_SIZE, ptep++)
		set_pte_at(mm, addr, ptep, orig_pte);
}

pte_t huge_ptep_clear_flush(struct vm_area_struct *vma,
			    unsigned long addr,
			    pte_t *ptep)
{
	pte_t pte = ptep_get(ptep);
	int pte_num;

	if (!pte_napot(pte))
		return ptep_clear_flush(vma, addr, ptep);

	pte_num = napot_pte_num(napot_cont_order(pte));

	return get_clear_contig_flush(vma->vm_mm, addr, ptep, pte_num);
}

void huge_pte_clear(struct mm_struct *mm,
		    unsigned long addr,
		    pte_t *ptep,
		    unsigned long sz)
{
	pte_t pte = READ_ONCE(*ptep);
	int i, pte_num;

	if (!pte_napot(pte)) {
		pte_clear(mm, addr, ptep);
		return;
	}

	pte_num = napot_pte_num(napot_cont_order(pte));
	for (i = 0; i < pte_num; i++, addr += PAGE_SIZE, ptep++)
		pte_clear(mm, addr, ptep);
}

static bool is_napot_size(unsigned long size)
{
	unsigned long order;

	if (!has_svnapot())
		return false;

	for_each_napot_order(order) {
		if (size == napot_cont_size(order))
			return true;
	}
	return false;
}

static __init int napot_hugetlbpages_init(void)
{
	if (has_svnapot()) {
		unsigned long order;

		for_each_napot_order(order)
			hugetlb_add_hstate(order);
	}
	return 0;
}
arch_initcall(napot_hugetlbpages_init);

#else

static bool is_napot_size(unsigned long size)
{
	return false;
}

#endif /*CONFIG_RISCV_ISA_SVNAPOT*/

int pud_huge(pud_t pud)
{
	return pud_leaf(pud);
}

int pmd_huge(pmd_t pmd)
{
	return pmd_leaf(pmd);
}

static bool __hugetlb_valid_size(unsigned long size)
{
	if (size == HPAGE_SIZE)
		return true;
	else if (IS_ENABLED(CONFIG_64BIT) && size == PUD_SIZE)
		return true;
	else if (is_napot_size(size))
		return true;
	else
		return false;
}

bool __init arch_hugetlb_valid_size(unsigned long size)
{
	return __hugetlb_valid_size(size);
}

bool arch_hugetlb_migration_supported(struct hstate *h)
{
	return __hugetlb_valid_size(huge_page_size(h));
}

#ifdef CONFIG_CONTIG_ALLOC
static __init int gigantic_pages_init(void)
{
	/* With CONTIG_ALLOC, we can allocate gigantic pages at runtime */
	if (IS_ENABLED(CONFIG_64BIT))
		hugetlb_add_hstate(PUD_SHIFT - PAGE_SHIFT);
	return 0;
}
arch_initcall(gigantic_pages_init);
#endif
