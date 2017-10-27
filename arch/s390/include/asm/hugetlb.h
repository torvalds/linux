/*
 *  IBM System z Huge TLB Page Support for Kernel.
 *
 *    Copyright IBM Corp. 2008
 *    Author(s): Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#ifndef _ASM_S390_HUGETLB_H
#define _ASM_S390_HUGETLB_H

#include <asm/page.h>
#include <asm/pgtable.h>


#define is_hugepage_only_range(mm, addr, len)	0
#define hugetlb_free_pgd_range			free_pgd_range
#define hugepages_supported()			(MACHINE_HAS_EDAT1)

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte);
pte_t huge_ptep_get(pte_t *ptep);
pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
			      unsigned long addr, pte_t *ptep);

/*
 * If the arch doesn't supply something else, assume that hugepage
 * size aligned regions are ok without further preparation.
 */
static inline int prepare_hugepage_range(struct file *file,
			unsigned long addr, unsigned long len)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	return 0;
}

#define arch_clear_hugepage_flags(page)		do { } while (0)

static inline void huge_pte_clear(struct mm_struct *mm, unsigned long addr,
				  pte_t *ptep, unsigned long sz)
{
	if ((pte_val(*ptep) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3)
		pte_val(*ptep) = _REGION3_ENTRY_EMPTY;
	else
		pte_val(*ptep) = _SEGMENT_ENTRY_EMPTY;
}

static inline void huge_ptep_clear_flush(struct vm_area_struct *vma,
					 unsigned long address, pte_t *ptep)
{
	huge_ptep_get_and_clear(vma->vm_mm, address, ptep);
}

static inline int huge_ptep_set_access_flags(struct vm_area_struct *vma,
					     unsigned long addr, pte_t *ptep,
					     pte_t pte, int dirty)
{
	int changed = !pte_same(huge_ptep_get(ptep), pte);
	if (changed) {
		huge_ptep_get_and_clear(vma->vm_mm, addr, ptep);
		set_huge_pte_at(vma->vm_mm, addr, ptep, pte);
	}
	return changed;
}

static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	pte_t pte = huge_ptep_get_and_clear(mm, addr, ptep);
	set_huge_pte_at(mm, addr, ptep, pte_wrprotect(pte));
}

static inline pte_t mk_huge_pte(struct page *page, pgprot_t pgprot)
{
	return mk_pte(page, pgprot);
}

static inline int huge_pte_none(pte_t pte)
{
	return pte_none(pte);
}

static inline int huge_pte_write(pte_t pte)
{
	return pte_write(pte);
}

static inline int huge_pte_dirty(pte_t pte)
{
	return pte_dirty(pte);
}

static inline pte_t huge_pte_mkwrite(pte_t pte)
{
	return pte_mkwrite(pte);
}

static inline pte_t huge_pte_mkdirty(pte_t pte)
{
	return pte_mkdirty(pte);
}

static inline pte_t huge_pte_wrprotect(pte_t pte)
{
	return pte_wrprotect(pte);
}

static inline pte_t huge_pte_modify(pte_t pte, pgprot_t newprot)
{
	return pte_modify(pte, newprot);
}

#ifdef CONFIG_ARCH_HAS_GIGANTIC_PAGE
static inline bool gigantic_page_supported(void) { return true; }
#endif
#endif /* _ASM_S390_HUGETLB_H */
