/*
 * PPC Huge TLB Page Support for Book3E MMU
 *
 * Copyright (C) 2009 David Gibson, IBM Corporation.
 * Copyright (C) 2011 Becky Bruce, Freescale Semiconductor
 *
 */
#include <linux/mm.h>
#include <linux/hugetlb.h>

static inline int mmu_get_tsize(int psize)
{
	return mmu_psize_defs[psize].enc;
}

static inline int book3e_tlb_exists(unsigned long ea, unsigned long pid)
{
	int found = 0;

	mtspr(SPRN_MAS6, pid << 16);
	if (mmu_has_feature(MMU_FTR_USE_TLBRSRV)) {
		asm volatile(
			"li	%0,0\n"
			"tlbsx.	0,%1\n"
			"bne	1f\n"
			"li	%0,1\n"
			"1:\n"
			: "=&r"(found) : "r"(ea));
	} else {
		asm volatile(
			"tlbsx	0,%1\n"
			"mfspr	%0,0x271\n"
			"srwi	%0,%0,31\n"
			: "=&r"(found) : "r"(ea));
	}

	return found;
}

void book3e_hugetlb_preload(struct vm_area_struct *vma, unsigned long ea,
			    pte_t pte)
{
	unsigned long mas1, mas2;
	u64 mas7_3;
	unsigned long psize, tsize, shift;
	unsigned long flags;
	struct mm_struct *mm;

#ifdef CONFIG_PPC_FSL_BOOK3E
	int index, ncams;
#endif

	if (unlikely(is_kernel_addr(ea)))
		return;

	mm = vma->vm_mm;

#ifdef CONFIG_PPC_MM_SLICES
	psize = get_slice_psize(mm, ea);
	tsize = mmu_get_tsize(psize);
	shift = mmu_psize_defs[psize].shift;
#else
	psize = vma_mmu_pagesize(vma);
	shift = __ilog2(psize);
	tsize = shift - 10;
#endif

	/*
	 * We can't be interrupted while we're setting up the MAS
	 * regusters or after we've confirmed that no tlb exists.
	 */
	local_irq_save(flags);

	if (unlikely(book3e_tlb_exists(ea, mm->context.id))) {
		local_irq_restore(flags);
		return;
	}

#ifdef CONFIG_PPC_FSL_BOOK3E
	ncams = mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY;

	/* We have to use the CAM(TLB1) on FSL parts for hugepages */
	index = __get_cpu_var(next_tlbcam_idx);
	mtspr(SPRN_MAS0, MAS0_ESEL(index) | MAS0_TLBSEL(1));

	/* Just round-robin the entries and wrap when we hit the end */
	if (unlikely(index == ncams - 1))
		__get_cpu_var(next_tlbcam_idx) = tlbcam_index;
	else
		__get_cpu_var(next_tlbcam_idx)++;
#endif
	mas1 = MAS1_VALID | MAS1_TID(mm->context.id) | MAS1_TSIZE(tsize);
	mas2 = ea & ~((1UL << shift) - 1);
	mas2 |= (pte_val(pte) >> PTE_WIMGE_SHIFT) & MAS2_WIMGE_MASK;
	mas7_3 = (u64)pte_pfn(pte) << PAGE_SHIFT;
	mas7_3 |= (pte_val(pte) >> PTE_BAP_SHIFT) & MAS3_BAP_MASK;
	if (!pte_dirty(pte))
		mas7_3 &= ~(MAS3_SW|MAS3_UW);

	mtspr(SPRN_MAS1, mas1);
	mtspr(SPRN_MAS2, mas2);

	if (mmu_has_feature(MMU_FTR_USE_PAIRED_MAS)) {
		mtspr(SPRN_MAS7_MAS3, mas7_3);
	} else {
		mtspr(SPRN_MAS7, upper_32_bits(mas7_3));
		mtspr(SPRN_MAS3, lower_32_bits(mas7_3));
	}

	asm volatile ("tlbwe");

	local_irq_restore(flags);
}

void flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	struct hstate *hstate = hstate_file(vma->vm_file);
	unsigned long tsize = huge_page_shift(hstate) - 10;

	__flush_tlb_page(vma ? vma->vm_mm : NULL, vmaddr, tsize, 0);

}
