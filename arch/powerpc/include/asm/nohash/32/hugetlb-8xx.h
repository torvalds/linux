/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_32_HUGETLB_8XX_H
#define _ASM_POWERPC_NOHASH_32_HUGETLB_8XX_H

static inline pte_t *hugepd_page(hugepd_t hpd)
{
	BUG_ON(!hugepd_ok(hpd));

	return (pte_t *)__va(hpd_val(hpd) & ~HUGEPD_SHIFT_MASK);
}

static inline unsigned int hugepd_shift(hugepd_t hpd)
{
	return ((hpd_val(hpd) & _PMD_PAGE_MASK) >> 1) + 17;
}

static inline pte_t *hugepte_offset(hugepd_t hpd, unsigned long addr,
				    unsigned int pdshift)
{
	unsigned long idx = (addr & ((1UL << pdshift) - 1)) >> PAGE_SHIFT;

	return hugepd_page(hpd) + idx;
}

static inline void flush_hugetlb_page(struct vm_area_struct *vma,
				      unsigned long vmaddr)
{
	flush_tlb_page(vma, vmaddr);
}

#endif /* _ASM_POWERPC_NOHASH_32_HUGETLB_8XX_H */
