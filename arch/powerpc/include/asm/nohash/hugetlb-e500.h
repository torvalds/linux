/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_HUGETLB_E500_H
#define _ASM_POWERPC_NOHASH_HUGETLB_E500_H

static inline pte_t *hugepd_page(hugepd_t hpd)
{
	if (WARN_ON(!hugepd_ok(hpd)))
		return NULL;

	return (pte_t *)((hpd_val(hpd) & ~HUGEPD_SHIFT_MASK) | PD_HUGE);
}

static inline unsigned int hugepd_shift(hugepd_t hpd)
{
	return hpd_val(hpd) & HUGEPD_SHIFT_MASK;
}

static inline pte_t *hugepte_offset(hugepd_t hpd, unsigned long addr,
				    unsigned int pdshift)
{
	/*
	 * On FSL BookE, we have multiple higher-level table entries that
	 * point to the same hugepte.  Just use the first one since they're all
	 * identical.  So for that case, idx=0.
	 */
	return hugepd_page(hpd);
}

void flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr);

static inline void hugepd_populate(hugepd_t *hpdp, pte_t *new, unsigned int pshift)
{
	/* We use the old format for PPC_E500 */
	*hpdp = __hugepd(((unsigned long)new & ~PD_HUGE) | pshift);
}

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
