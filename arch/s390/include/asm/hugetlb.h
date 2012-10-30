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

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte);

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

#define hugetlb_prefault_arch_hook(mm)		do { } while (0)
#define arch_clear_hugepage_flags(page)		do { } while (0)

int arch_prepare_hugepage(struct page *page);
void arch_release_hugepage(struct page *page);

static inline pte_t huge_pte_wrprotect(pte_t pte)
{
	pte_val(pte) |= _PAGE_RO;
	return pte;
}

static inline int huge_pte_none(pte_t pte)
{
	return (pte_val(pte) & _SEGMENT_ENTRY_INV) &&
		!(pte_val(pte) & _SEGMENT_ENTRY_RO);
}

static inline pte_t huge_ptep_get(pte_t *ptep)
{
	pte_t pte = *ptep;
	unsigned long mask;

	if (!MACHINE_HAS_HPAGE) {
		ptep = (pte_t *) (pte_val(pte) & _SEGMENT_ENTRY_ORIGIN);
		if (ptep) {
			mask = pte_val(pte) &
				(_SEGMENT_ENTRY_INV | _SEGMENT_ENTRY_RO);
			pte = pte_mkhuge(*ptep);
			pte_val(pte) |= mask;
		}
	}
	return pte;
}

static inline void __pmd_csp(pmd_t *pmdp)
{
	register unsigned long reg2 asm("2") = pmd_val(*pmdp);
	register unsigned long reg3 asm("3") = pmd_val(*pmdp) |
					       _SEGMENT_ENTRY_INV;
	register unsigned long reg4 asm("4") = ((unsigned long) pmdp) + 5;

	asm volatile(
		"	csp %1,%3"
		: "=m" (*pmdp)
		: "d" (reg2), "d" (reg3), "d" (reg4), "m" (*pmdp) : "cc");
}

static inline void huge_ptep_invalidate(struct mm_struct *mm,
					unsigned long address, pte_t *ptep)
{
	pmd_t *pmdp = (pmd_t *) ptep;

	if (MACHINE_HAS_IDTE)
		__pmd_idte(address, pmdp);
	else
		__pmd_csp(pmdp);
	pmd_val(*pmdp) = _SEGMENT_ENTRY_INV | _SEGMENT_ENTRY;
}

static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep)
{
	pte_t pte = huge_ptep_get(ptep);

	huge_ptep_invalidate(mm, addr, ptep);
	return pte;
}

#define huge_ptep_set_access_flags(__vma, __addr, __ptep, __entry, __dirty) \
({									    \
	int __changed = !pte_same(huge_ptep_get(__ptep), __entry);	    \
	if (__changed) {						    \
		huge_ptep_invalidate((__vma)->vm_mm, __addr, __ptep);	    \
		set_huge_pte_at((__vma)->vm_mm, __addr, __ptep, __entry);   \
	}								    \
	__changed;							    \
})

#define huge_ptep_set_wrprotect(__mm, __addr, __ptep)			\
({									\
	pte_t __pte = huge_ptep_get(__ptep);				\
	if (pte_write(__pte)) {						\
		huge_ptep_invalidate(__mm, __addr, __ptep);		\
		set_huge_pte_at(__mm, __addr, __ptep,			\
				huge_pte_wrprotect(__pte));		\
	}								\
})

static inline void huge_ptep_clear_flush(struct vm_area_struct *vma,
					 unsigned long address, pte_t *ptep)
{
	huge_ptep_invalidate(vma->vm_mm, address, ptep);
}

#endif /* _ASM_S390_HUGETLB_H */
