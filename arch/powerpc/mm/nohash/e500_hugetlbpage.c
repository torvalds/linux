// SPDX-License-Identifier: GPL-2.0
/*
 * PPC Huge TLB Page Support for Book3E MMU
 *
 * Copyright (C) 2009 David Gibson, IBM Corporation.
 * Copyright (C) 2011 Becky Bruce, Freescale Semiconductor
 *
 */
#include <linux/mm.h>
#include <linux/hugetlb.h>

#include <asm/mmu.h>

#ifdef CONFIG_PPC64
#include <asm/paca.h>

static inline int tlb1_next(void)
{
	struct paca_struct *paca = get_paca();
	struct tlb_core_data *tcd;
	int this, next;

	tcd = paca->tcd_ptr;
	this = tcd->esel_next;

	next = this + 1;
	if (next >= tcd->esel_max)
		next = tcd->esel_first;

	tcd->esel_next = next;
	return this;
}

static inline void book3e_tlb_lock(void)
{
	struct paca_struct *paca = get_paca();
	unsigned long tmp;
	int token = smp_processor_id() + 1;

	/*
	 * Besides being unnecessary in the absence of SMT, this
	 * check prevents trying to do lbarx/stbcx. on e5500 which
	 * doesn't implement either feature.
	 */
	if (!cpu_has_feature(CPU_FTR_SMT))
		return;

	asm volatile(".machine push;"
		     ".machine e6500;"
		     "1: lbarx %0, 0, %1;"
		     "cmpwi %0, 0;"
		     "bne 2f;"
		     "stbcx. %2, 0, %1;"
		     "bne 1b;"
		     "b 3f;"
		     "2: lbzx %0, 0, %1;"
		     "cmpwi %0, 0;"
		     "bne 2b;"
		     "b 1b;"
		     "3:"
		     ".machine pop;"
		     : "=&r" (tmp)
		     : "r" (&paca->tcd_ptr->lock), "r" (token)
		     : "memory");
}

static inline void book3e_tlb_unlock(void)
{
	struct paca_struct *paca = get_paca();

	if (!cpu_has_feature(CPU_FTR_SMT))
		return;

	isync();
	paca->tcd_ptr->lock = 0;
}
#else
static inline int tlb1_next(void)
{
	int index, ncams;

	ncams = mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY;

	index = this_cpu_read(next_tlbcam_idx);

	/* Just round-robin the entries and wrap when we hit the end */
	if (unlikely(index == ncams - 1))
		__this_cpu_write(next_tlbcam_idx, tlbcam_index);
	else
		__this_cpu_inc(next_tlbcam_idx);

	return index;
}

static inline void book3e_tlb_lock(void)
{
}

static inline void book3e_tlb_unlock(void)
{
}
#endif

static inline int book3e_tlb_exists(unsigned long ea, unsigned long pid)
{
	int found = 0;

	mtspr(SPRN_MAS6, pid << 16);
	asm volatile(
		"tlbsx	0,%1\n"
		"mfspr	%0,0x271\n"
		"srwi	%0,%0,31\n"
		: "=&r"(found) : "r"(ea));

	return found;
}

static void
book3e_hugetlb_preload(struct vm_area_struct *vma, unsigned long ea, pte_t pte)
{
	unsigned long mas1, mas2;
	u64 mas7_3;
	unsigned long psize, tsize, shift;
	unsigned long flags;
	struct mm_struct *mm;
	int index;

	if (unlikely(is_kernel_addr(ea)))
		return;

	mm = vma->vm_mm;

	psize = vma_mmu_pagesize(vma);
	shift = __ilog2(psize);
	tsize = shift - 10;
	/*
	 * We can't be interrupted while we're setting up the MAS
	 * registers or after we've confirmed that no tlb exists.
	 */
	local_irq_save(flags);

	book3e_tlb_lock();

	if (unlikely(book3e_tlb_exists(ea, mm->context.id))) {
		book3e_tlb_unlock();
		local_irq_restore(flags);
		return;
	}

	/* We have to use the CAM(TLB1) on FSL parts for hugepages */
	index = tlb1_next();
	mtspr(SPRN_MAS0, MAS0_ESEL(index) | MAS0_TLBSEL(1));

	mas1 = MAS1_VALID | MAS1_TID(mm->context.id) | MAS1_TSIZE(tsize);
	mas2 = ea & ~((1UL << shift) - 1);
	mas2 |= (pte_val(pte) >> PTE_WIMGE_SHIFT) & MAS2_WIMGE_MASK;
	mas7_3 = (u64)pte_pfn(pte) << PAGE_SHIFT;
	mas7_3 |= (pte_val(pte) >> PTE_BAP_SHIFT) & MAS3_BAP_MASK;
	if (!pte_dirty(pte))
		mas7_3 &= ~(MAS3_SW|MAS3_UW);

	mtspr(SPRN_MAS1, mas1);
	mtspr(SPRN_MAS2, mas2);

	if (mmu_has_feature(MMU_FTR_BIG_PHYS))
		mtspr(SPRN_MAS7, upper_32_bits(mas7_3));
	mtspr(SPRN_MAS3, lower_32_bits(mas7_3));

	asm volatile ("tlbwe");

	book3e_tlb_unlock();
	local_irq_restore(flags);
}

/*
 * This is called at the end of handling a user page fault, when the
 * fault has been handled by updating a PTE in the linux page tables.
 *
 * This must always be called with the pte lock held.
 */
void update_mmu_cache_range(struct vm_fault *vmf, struct vm_area_struct *vma,
		unsigned long address, pte_t *ptep, unsigned int nr)
{
	if (is_vm_hugetlb_page(vma))
		book3e_hugetlb_preload(vma, address, *ptep);
}

void flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	struct hstate *hstate = hstate_file(vma->vm_file);
	unsigned long tsize = huge_page_shift(hstate) - 10;

	__flush_tlb_page(vma->vm_mm, vmaddr, tsize, 0);
}
