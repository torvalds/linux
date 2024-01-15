/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_32_HUGETLB_8XX_H
#define _ASM_POWERPC_NOHASH_32_HUGETLB_8XX_H

#define PAGE_SHIFT_8M		23

static inline pte_t *hugepd_page(hugepd_t hpd)
{
	BUG_ON(!hugepd_ok(hpd));

	return (pte_t *)__va(hpd_val(hpd) & ~HUGEPD_SHIFT_MASK);
}

static inline unsigned int hugepd_shift(hugepd_t hpd)
{
	return PAGE_SHIFT_8M;
}

static inline pte_t *hugepte_offset(hugepd_t hpd, unsigned long addr,
				    unsigned int pdshift)
{
	unsigned long idx = (addr & (SZ_4M - 1)) >> PAGE_SHIFT;

	return hugepd_page(hpd) + idx;
}

static inline void flush_hugetlb_page(struct vm_area_struct *vma,
				      unsigned long vmaddr)
{
	flush_tlb_page(vma, vmaddr);
}

static inline void hugepd_populate(hugepd_t *hpdp, pte_t *new, unsigned int pshift)
{
	*hpdp = __hugepd(__pa(new) | _PMD_USER | _PMD_PRESENT | _PMD_PAGE_8M);
}

static inline void hugepd_populate_kernel(hugepd_t *hpdp, pte_t *new, unsigned int pshift)
{
	*hpdp = __hugepd(__pa(new) | _PMD_PRESENT | _PMD_PAGE_8M);
}

static inline int check_and_get_huge_psize(int shift)
{
	return shift_to_mmu_psize(shift);
}

#define __HAVE_ARCH_HUGE_SET_HUGE_PTE_AT
void set_huge_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
		     pte_t pte, unsigned long sz);

#define __HAVE_ARCH_HUGE_PTE_CLEAR
static inline void huge_pte_clear(struct mm_struct *mm, unsigned long addr,
				  pte_t *ptep, unsigned long sz)
{
	pte_update(mm, addr, ptep, ~0UL, 0, 1);
}

#define __HAVE_ARCH_HUGE_PTEP_SET_WRPROTECT
static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	unsigned long clr = ~pte_val(pte_wrprotect(__pte(~0)));
	unsigned long set = pte_val(pte_wrprotect(__pte(0)));

	pte_update(mm, addr, ptep, clr, set, 1);
}

#ifdef CONFIG_PPC_4K_PAGES
static inline pte_t arch_make_huge_pte(pte_t entry, unsigned int shift, vm_flags_t flags)
{
	size_t size = 1UL << shift;

	if (size == SZ_16K)
		return __pte(pte_val(entry) | _PAGE_SPS);
	else
		return __pte(pte_val(entry) | _PAGE_SPS | _PAGE_HUGE);
}
#define arch_make_huge_pte arch_make_huge_pte
#endif

#endif /* _ASM_POWERPC_NOHASH_32_HUGETLB_8XX_H */
