/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_HUGETLB_H
#define _ASM_POWERPC_BOOK3S_64_HUGETLB_H
/*
 * For radix we want generic code to handle hugetlb. But then if we want
 * both hash and radix to be enabled together we need to workaround the
 * limitations.
 */
void radix__flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
void radix__local_flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern unsigned long
radix__hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
				unsigned long len, unsigned long pgoff,
				unsigned long flags);

extern void radix__huge_ptep_modify_prot_commit(struct vm_area_struct *vma,
						unsigned long addr, pte_t *ptep,
						pte_t old_pte, pte_t pte);

static inline int hstate_get_psize(struct hstate *hstate)
{
	unsigned long shift;

	shift = huge_page_shift(hstate);
	if (shift == mmu_psize_defs[MMU_PAGE_2M].shift)
		return MMU_PAGE_2M;
	else if (shift == mmu_psize_defs[MMU_PAGE_1G].shift)
		return MMU_PAGE_1G;
	else if (shift == mmu_psize_defs[MMU_PAGE_16M].shift)
		return MMU_PAGE_16M;
	else if (shift == mmu_psize_defs[MMU_PAGE_16G].shift)
		return MMU_PAGE_16G;
	else {
		WARN(1, "Wrong huge page shift\n");
		return mmu_virtual_psize;
	}
}

#define __HAVE_ARCH_GIGANTIC_PAGE_RUNTIME_SUPPORTED
static inline bool gigantic_page_runtime_supported(void)
{
	/*
	 * We used gigantic page reservation with hypervisor assist in some case.
	 * We cannot use runtime allocation of gigantic pages in those platforms
	 * This is hash translation mode LPARs.
	 */
	if (firmware_has_feature(FW_FEATURE_LPAR) && !radix_enabled())
		return false;

	return true;
}

/* hugepd entry valid bit */
#define HUGEPD_VAL_BITS		(0x8000000000000000UL)

#define huge_ptep_modify_prot_start huge_ptep_modify_prot_start
extern pte_t huge_ptep_modify_prot_start(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep);

#define huge_ptep_modify_prot_commit huge_ptep_modify_prot_commit
extern void huge_ptep_modify_prot_commit(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep,
					 pte_t old_pte, pte_t new_pte);
/*
 * This should work for other subarchs too. But right now we use the
 * new format only for 64bit book3s
 */
static inline pte_t *hugepd_page(hugepd_t hpd)
{
	BUG_ON(!hugepd_ok(hpd));
	/*
	 * We have only four bits to encode, MMU page size
	 */
	BUILD_BUG_ON((MMU_PAGE_COUNT - 1) > 0xf);
	return __va(hpd_val(hpd) & HUGEPD_ADDR_MASK);
}

static inline unsigned int hugepd_mmu_psize(hugepd_t hpd)
{
	return (hpd_val(hpd) & HUGEPD_SHIFT_MASK) >> 2;
}

static inline unsigned int hugepd_shift(hugepd_t hpd)
{
	return mmu_psize_to_shift(hugepd_mmu_psize(hpd));
}
static inline void flush_hugetlb_page(struct vm_area_struct *vma,
				      unsigned long vmaddr)
{
	if (radix_enabled())
		return radix__flush_hugetlb_page(vma, vmaddr);
}

static inline pte_t *hugepte_offset(hugepd_t hpd, unsigned long addr,
				    unsigned int pdshift)
{
	unsigned long idx = (addr & ((1UL << pdshift) - 1)) >> hugepd_shift(hpd);

	return hugepd_page(hpd) + idx;
}

static inline void hugepd_populate(hugepd_t *hpdp, pte_t *new, unsigned int pshift)
{
	*hpdp = __hugepd(__pa(new) | HUGEPD_VAL_BITS | (shift_to_mmu_psize(pshift) << 2));
}

void flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr);

static inline int check_and_get_huge_psize(int shift)
{
	int mmu_psize;

	if (shift > SLICE_HIGH_SHIFT)
		return -EINVAL;

	mmu_psize = shift_to_mmu_psize(shift);

	/*
	 * We need to make sure that for different page sizes reported by
	 * firmware we only add hugetlb support for page sizes that can be
	 * supported by linux page table layout.
	 * For now we have
	 * Radix: 2M and 1G
	 * Hash: 16M and 16G
	 */
	if (radix_enabled()) {
		if (mmu_psize != MMU_PAGE_2M && mmu_psize != MMU_PAGE_1G)
			return -EINVAL;
	} else {
		if (mmu_psize != MMU_PAGE_16M && mmu_psize != MMU_PAGE_16G)
			return -EINVAL;
	}
	return mmu_psize;
}

#endif
