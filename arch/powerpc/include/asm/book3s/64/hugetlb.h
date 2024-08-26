/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_HUGETLB_H
#define _ASM_POWERPC_BOOK3S_64_HUGETLB_H

#include <asm/firmware.h>

/*
 * For radix we want generic code to handle hugetlb. But then if we want
 * both hash and radix to be enabled together we need to workaround the
 * limitations.
 */
void radix__flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
void radix__local_flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr);

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

#define huge_ptep_modify_prot_start huge_ptep_modify_prot_start
extern pte_t huge_ptep_modify_prot_start(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep);

#define huge_ptep_modify_prot_commit huge_ptep_modify_prot_commit
extern void huge_ptep_modify_prot_commit(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep,
					 pte_t old_pte, pte_t new_pte);

static inline void flush_hugetlb_page(struct vm_area_struct *vma,
				      unsigned long vmaddr)
{
	if (radix_enabled())
		return radix__flush_hugetlb_page(vma, vmaddr);
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
