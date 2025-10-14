// SPDX-License-Identifier: GPL-2.0
/*
 * SPARC64 Huge TLB page support.
 *
 * Copyright (C) 2002, 2003, 2006 David S. Miller (davem@davemloft.net)
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>


static pte_t sun4u_hugepage_shift_to_tte(pte_t entry, unsigned int shift)
{
	unsigned long hugepage_size = _PAGE_SZ4MB_4U;

	pte_val(entry) = pte_val(entry) & ~_PAGE_SZALL_4U;

	switch (shift) {
	case HPAGE_256MB_SHIFT:
		hugepage_size = _PAGE_SZ256MB_4U;
		pte_val(entry) |= _PAGE_PMD_HUGE;
		break;
	case HPAGE_SHIFT:
		pte_val(entry) |= _PAGE_PMD_HUGE;
		break;
	case HPAGE_64K_SHIFT:
		hugepage_size = _PAGE_SZ64K_4U;
		break;
	default:
		WARN_ONCE(1, "unsupported hugepage shift=%u\n", shift);
	}

	pte_val(entry) = pte_val(entry) | hugepage_size;
	return entry;
}

static pte_t sun4v_hugepage_shift_to_tte(pte_t entry, unsigned int shift)
{
	unsigned long hugepage_size = _PAGE_SZ4MB_4V;

	pte_val(entry) = pte_val(entry) & ~_PAGE_SZALL_4V;

	switch (shift) {
	case HPAGE_16GB_SHIFT:
		hugepage_size = _PAGE_SZ16GB_4V;
		pte_val(entry) |= _PAGE_PUD_HUGE;
		break;
	case HPAGE_2GB_SHIFT:
		hugepage_size = _PAGE_SZ2GB_4V;
		pte_val(entry) |= _PAGE_PMD_HUGE;
		break;
	case HPAGE_256MB_SHIFT:
		hugepage_size = _PAGE_SZ256MB_4V;
		pte_val(entry) |= _PAGE_PMD_HUGE;
		break;
	case HPAGE_SHIFT:
		pte_val(entry) |= _PAGE_PMD_HUGE;
		break;
	case HPAGE_64K_SHIFT:
		hugepage_size = _PAGE_SZ64K_4V;
		break;
	default:
		WARN_ONCE(1, "unsupported hugepage shift=%u\n", shift);
	}

	pte_val(entry) = pte_val(entry) | hugepage_size;
	return entry;
}

static pte_t hugepage_shift_to_tte(pte_t entry, unsigned int shift)
{
	if (tlb_type == hypervisor)
		return sun4v_hugepage_shift_to_tte(entry, shift);
	else
		return sun4u_hugepage_shift_to_tte(entry, shift);
}

pte_t arch_make_huge_pte(pte_t entry, unsigned int shift, vm_flags_t flags)
{
	pte_t pte;

	entry = pte_mkhuge(entry);
	pte = hugepage_shift_to_tte(entry, shift);

#ifdef CONFIG_SPARC64
	/* If this vma has ADI enabled on it, turn on TTE.mcd
	 */
	if (flags & VM_SPARC_ADI)
		return pte_mkmcd(pte);
	else
		return pte_mknotmcd(pte);
#else
	return pte;
#endif
}

static unsigned int sun4v_huge_tte_to_shift(pte_t entry)
{
	unsigned long tte_szbits = pte_val(entry) & _PAGE_SZALL_4V;
	unsigned int shift;

	switch (tte_szbits) {
	case _PAGE_SZ16GB_4V:
		shift = HPAGE_16GB_SHIFT;
		break;
	case _PAGE_SZ2GB_4V:
		shift = HPAGE_2GB_SHIFT;
		break;
	case _PAGE_SZ256MB_4V:
		shift = HPAGE_256MB_SHIFT;
		break;
	case _PAGE_SZ4MB_4V:
		shift = REAL_HPAGE_SHIFT;
		break;
	case _PAGE_SZ64K_4V:
		shift = HPAGE_64K_SHIFT;
		break;
	default:
		shift = PAGE_SHIFT;
		break;
	}
	return shift;
}

static unsigned int sun4u_huge_tte_to_shift(pte_t entry)
{
	unsigned long tte_szbits = pte_val(entry) & _PAGE_SZALL_4U;
	unsigned int shift;

	switch (tte_szbits) {
	case _PAGE_SZ256MB_4U:
		shift = HPAGE_256MB_SHIFT;
		break;
	case _PAGE_SZ4MB_4U:
		shift = REAL_HPAGE_SHIFT;
		break;
	case _PAGE_SZ64K_4U:
		shift = HPAGE_64K_SHIFT;
		break;
	default:
		shift = PAGE_SHIFT;
		break;
	}
	return shift;
}

static unsigned long tte_to_shift(pte_t entry)
{
	if (tlb_type == hypervisor)
		return sun4v_huge_tte_to_shift(entry);

	return sun4u_huge_tte_to_shift(entry);
}

static unsigned int huge_tte_to_shift(pte_t entry)
{
	unsigned long shift = tte_to_shift(entry);

	if (shift == PAGE_SHIFT)
		WARN_ONCE(1, "tto_to_shift: invalid hugepage tte=0x%lx\n",
			  pte_val(entry));

	return shift;
}

static unsigned long huge_tte_to_size(pte_t pte)
{
	unsigned long size = 1UL << huge_tte_to_shift(pte);

	if (size == REAL_HPAGE_SIZE)
		size = HPAGE_SIZE;
	return size;
}

unsigned long pud_leaf_size(pud_t pud) { return 1UL << tte_to_shift(*(pte_t *)&pud); }
unsigned long pmd_leaf_size(pmd_t pmd) { return 1UL << tte_to_shift(*(pte_t *)&pmd); }
unsigned long pte_leaf_size(pte_t pte) { return 1UL << tte_to_shift(pte); }

pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_offset(pgd, addr);
	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return NULL;
	if (sz >= PUD_SIZE)
		return (pte_t *)pud;
	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return NULL;
	if (sz >= PMD_SIZE)
		return (pte_t *)pmd;
	return pte_alloc_huge(mm, pmd, addr);
}

pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		return NULL;
	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d))
		return NULL;
	pud = pud_offset(p4d, addr);
	if (pud_none(*pud))
		return NULL;
	if (is_hugetlb_pud(*pud))
		return (pte_t *)pud;
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return NULL;
	if (is_hugetlb_pmd(*pmd))
		return (pte_t *)pmd;
	return pte_offset_huge(pmd, addr);
}

void __set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t entry)
{
	unsigned int nptes, orig_shift, shift;
	unsigned long i, size;
	pte_t orig;

	size = huge_tte_to_size(entry);

	shift = PAGE_SHIFT;
	if (size >= PUD_SIZE)
		shift = PUD_SHIFT;
	else if (size >= PMD_SIZE)
		shift = PMD_SHIFT;
	else
		shift = PAGE_SHIFT;

	nptes = size >> shift;

	if (!pte_present(*ptep) && pte_present(entry))
		mm->context.hugetlb_pte_count += nptes;

	addr &= ~(size - 1);
	orig = *ptep;
	orig_shift = pte_none(orig) ? PAGE_SHIFT : huge_tte_to_shift(orig);

	for (i = 0; i < nptes; i++)
		ptep[i] = __pte(pte_val(entry) + (i << shift));

	maybe_tlb_batch_add(mm, addr, ptep, orig, 0, orig_shift);
	/* An HPAGE_SIZE'ed page is composed of two REAL_HPAGE_SIZE'ed pages */
	if (size == HPAGE_SIZE)
		maybe_tlb_batch_add(mm, addr + REAL_HPAGE_SIZE, ptep, orig, 0,
				    orig_shift);
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t entry, unsigned long sz)
{
	__set_huge_pte_at(mm, addr, ptep, entry);
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, unsigned long sz)
{
	unsigned int i, nptes, orig_shift, shift;
	unsigned long size;
	pte_t entry;

	entry = *ptep;
	size = huge_tte_to_size(entry);

	shift = PAGE_SHIFT;
	if (size >= PUD_SIZE)
		shift = PUD_SHIFT;
	else if (size >= PMD_SIZE)
		shift = PMD_SHIFT;
	else
		shift = PAGE_SHIFT;

	nptes = size >> shift;
	orig_shift = pte_none(entry) ? PAGE_SHIFT : huge_tte_to_shift(entry);

	if (pte_present(entry))
		mm->context.hugetlb_pte_count -= nptes;

	addr &= ~(size - 1);
	for (i = 0; i < nptes; i++)
		ptep[i] = __pte(0UL);

	maybe_tlb_batch_add(mm, addr, ptep, entry, 0, orig_shift);
	/* An HPAGE_SIZE'ed page is composed of two REAL_HPAGE_SIZE'ed pages */
	if (size == HPAGE_SIZE)
		maybe_tlb_batch_add(mm, addr + REAL_HPAGE_SIZE, ptep, entry, 0,
				    orig_shift);

	return entry;
}
