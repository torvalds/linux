/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_HUGETLB_E500_H
#define _ASM_POWERPC_NOHASH_HUGETLB_E500_H

void flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr);

static inline int check_and_get_huge_psize(int shift)
{
	if (shift & 1)	/* Not a power of 4 */
		return -EINVAL;

	return shift_to_mmu_psize(shift);
}

static inline pte_t arch_make_huge_pte(pte_t entry, unsigned int shift, vm_flags_t flags)
{
	unsigned int tsize = shift - _PAGE_PSIZE_SHIFT_OFFSET;
	pte_basic_t val = (tsize << _PAGE_PSIZE_SHIFT) & _PAGE_PSIZE_MSK;

	return __pte((pte_val(entry) & ~(pte_basic_t)_PAGE_PSIZE_MSK) | val);
}
#define arch_make_huge_pte arch_make_huge_pte

#endif /* _ASM_POWERPC_NOHASH_HUGETLB_E500_H */
