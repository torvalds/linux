// SPDX-License-Identifier: GPL-2.0
/*
 * Alpha huge page support, built on the page table entry granularity hint
 * described in asm/pgtable.h. All PTEs of a block have to agree in bits
 * <15:0>, so every update below rewrites the whole block.
 */

#include <linux/hugetlb.h>
#include <linux/mm.h>
#include <linux/pgtable.h>

#include <asm/tlbflush.h>

/*
 * Every hint size is smaller than PMD_SIZE, so a block always fits inside a
 * single last level page table and its entry count is just its page count.
 */
static inline unsigned long num_contig_ptes(unsigned long sz)
{
	return sz >> PAGE_SHIFT;
}

/* Hint order that maps @sz, or 0 if @sz is not a hint size. */
static unsigned int gh_order_from_size(unsigned long sz)
{
	unsigned int order;

	for_each_gh_order(order)
		if (gh_cont_size(order) == sz)
			return order;

	return 0;
}

pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, unsigned long sz)
{
	unsigned int order = gh_order_from_size(sz);
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	if (!order)
		return NULL;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return NULL;

	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return NULL;

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return NULL;

	return pte_alloc_huge(mm, pmd, addr & gh_cont_mask(order));
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr,
		       unsigned long sz)
{
	unsigned int order = gh_order_from_size(sz);
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	if (!order)
		return NULL;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(pgdp_get(pgd)))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (!p4d_present(p4dp_get(p4d)))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (!pud_present(pudp_get(pud)))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(pmdp_get(pmd)))
		return NULL;

	return pte_offset_huge(pmd, addr & gh_cont_mask(order));
}

/*
 * A block is only as large as its own hint, so the walk can skip to the end of
 * the containing last level page table but no further.
 */
unsigned long hugetlb_mask_last_page(struct hstate *h)
{
	return PMD_SIZE - huge_page_size(h);
}

pte_t huge_ptep_get(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_t orig_pte = ptep_get(ptep);
	unsigned long i, ncontig;

	/* Swap, migration and marker entries carry no hint. */
	if (!pte_huge(orig_pte))
		return orig_pte;

	ncontig = gh_pte_num(pte_gh_order(orig_pte));

	for (i = 0; i < ncontig; i++, ptep++) {
		pte_t pte = ptep_get(ptep);

		if (pte_dirty(pte))
			orig_pte = pte_mkdirty(orig_pte);
		if (pte_young(pte))
			orig_pte = pte_mkyoung(orig_pte);
	}

	return orig_pte;
}

static pte_t get_clear_contig(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, unsigned long ncontig)
{
	pte_t pte = ptep_get_and_clear(mm, addr, ptep);
	bool present = pte_present(pte);

	while (--ncontig) {
		pte_t tmp_pte;

		ptep++;
		addr += PAGE_SIZE;
		tmp_pte = ptep_get_and_clear(mm, addr, ptep);
		if (present) {
			if (pte_dirty(tmp_pte))
				pte = pte_mkdirty(pte);
			if (pte_young(tmp_pte))
				pte = pte_mkyoung(pte);
		}
	}

	return pte;
}

static pte_t get_clear_contig_flush(struct mm_struct *mm, unsigned long addr,
				    pte_t *ptep, unsigned long ncontig)
{
	pte_t orig_pte = get_clear_contig(mm, addr, ptep, ncontig);
	struct vm_area_struct vma = TLB_FLUSH_VMA(mm, 0);

	if (!pte_none(orig_pte))
		flush_tlb_range(&vma, addr, addr + ncontig * PAGE_SIZE);

	return orig_pte;
}

pte_t arch_make_huge_pte(pte_t entry, unsigned int shift, vm_flags_t flags)
{
	unsigned int order = gh_order_from_size(1UL << shift);

	/*
	 * Not every caller asks for a size Alpha can hint; mm/debug_vm_pgtable.c
	 * probes with PMD_SHIFT. Leave the entry alone rather than complaining.
	 */
	if (order)
		entry = pte_mkgh(entry, order);

	return entry;
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
		     pte_t pte, unsigned long sz)
{
	unsigned long i, ncontig = num_contig_ptes(sz);

	/* The hint requires the block to be naturally aligned in the VA too. */
	VM_WARN_ON_ONCE(addr & (sz - 1));

	if (!pte_present(pte)) {
		/*
		 * Swap, migration and marker entries hold no frame number, so
		 * every slot gets the same value.
		 */
		for (i = 0; i < ncontig; i++, ptep++, addr += PAGE_SIZE)
			set_ptes(mm, addr, ptep, pte, 1);
		return;
	}

	/*
	 * All PTEs of a block must agree in bits <15:0> (ARM Table 22-3, note
	 * 2), which rewriting a valid block in place would violate part way
	 * through. Invalidate it everywhere first, following ARM 11.6.1: the
	 * invalidate has to assume the hint is zero, so it must cover every
	 * page of the block.
	 */
	if (pte_present(ptep_get(ptep)))
		get_clear_contig_flush(mm, addr, ptep, ncontig);

	/* set_ptes() advances the frame number for each entry. */
	set_ptes(mm, addr, ptep, pte, ncontig);
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, unsigned long sz)
{
	return get_clear_contig(mm, addr, ptep, num_contig_ptes(sz));
}

/*
 * The block size comes from the hstate rather than from the entry, because
 * this is also reached for non-present entries, which carry no hint.
 */
pte_t huge_ptep_clear_flush(struct vm_area_struct *vma, unsigned long addr,
			    pte_t *ptep)
{
	unsigned long sz = huge_page_size(hstate_vma(vma));

	return get_clear_contig_flush(vma->vm_mm, addr, ptep,
				      num_contig_ptes(sz));
}

pte_t huge_ptep_modify_prot_start(struct vm_area_struct *vma, unsigned long addr,
				  pte_t *ptep)
{
	return huge_ptep_clear_flush(vma, addr, ptep);
}

/* True if any entry of the block differs from @pte outside the frame number. */
static bool gh_access_flags_changed(pte_t *ptep, pte_t pte,
				    unsigned long ncontig)
{
	unsigned long i;

	for (i = 0; i < ncontig; i++)
		if ((pte_val(ptep_get(ptep + i)) ^ pte_val(pte)) & ~_PFN_MASK)
			return true;

	return false;
}

int huge_ptep_set_access_flags(struct vm_area_struct *vma, unsigned long addr,
			       pte_t *ptep, pte_t pte, int dirty)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long sz = huge_page_size(hstate_vma(vma));
	unsigned long ncontig = num_contig_ptes(sz);
	pte_t orig_pte = huge_ptep_get(mm, addr, ptep);

	/* Keep the young and dirty state the block already has. */
	if (pte_dirty(orig_pte))
		pte = pte_mkdirty(pte);
	if (pte_young(orig_pte))
		pte = pte_mkyoung(pte);

	/* Breaking an unchanged block only makes concurrent touchers refault. */
	if (!gh_access_flags_changed(ptep, pte, ncontig))
		return 0;

	get_clear_contig_flush(mm, addr, ptep, ncontig);
	set_ptes(mm, addr, ptep, pte, ncontig);

	return 1;
}

void huge_ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr,
			     pte_t *ptep)
{
	pte_t pte = ptep_get(ptep);
	unsigned long ncontig = gh_pte_num(pte_gh_order(pte));

	VM_WARN_ON_ONCE(!pte_huge(pte));

	/*
	 * pte_wrprotect() sets _PAGE_FOW, which lives in the bits the whole
	 * block must agree on, so this takes the break before make path.
	 */
	pte = pte_wrprotect(get_clear_contig_flush(mm, addr, ptep, ncontig));
	set_ptes(mm, addr, ptep, pte, ncontig);
}

void huge_pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
		    unsigned long sz)
{
	unsigned long i, ncontig = num_contig_ptes(sz);

	for (i = 0; i < ncontig; i++, addr += PAGE_SIZE, ptep++)
		pte_clear(mm, addr, ptep);
}

bool __init arch_hugetlb_valid_size(unsigned long size)
{
	return gh_order_from_size(size) != 0;
}

static __init int alpha_hugetlbpage_init(void)
{
	unsigned int order;

	for_each_gh_order(order)
		hugetlb_add_hstate(gh_cont_shift(order) - PAGE_SHIFT);

	return 0;
}
arch_initcall(alpha_hugetlbpage_init);
