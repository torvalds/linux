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

#define arch_make_huge_pte arch_make_huge_pte
static inline pte_t arch_make_huge_pte(pte_t entry, struct vm_area_struct *vma,
				       struct page *page, int writable)
{
	unsigned long page_shift;

	if (!cpu_has_feature(CPU_FTR_POWER9_DD1))
		return entry;

	page_shift = huge_page_shift(hstate_vma(vma));
	/*
	 * We don't support 1G hugetlb pages yet.
	 */
	VM_WARN_ON(page_shift == mmu_psize_defs[MMU_PAGE_1G].shift);
	if (page_shift == mmu_psize_defs[MMU_PAGE_2M].shift)
		return __pte(pte_val(entry) | R_PAGE_LARGE);
	else
		return entry;
}
#endif
