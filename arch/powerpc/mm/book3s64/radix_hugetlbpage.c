// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/security.h>
#include <asm/cacheflush.h>
#include <asm/machdep.h>
#include <asm/mman.h>
#include <asm/tlb.h>

void radix__flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	int psize;
	struct hstate *hstate = hstate_file(vma->vm_file);

	psize = hstate_get_psize(hstate);
	radix__flush_tlb_page_psize(vma->vm_mm, vmaddr, psize);
}

void radix__local_flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	int psize;
	struct hstate *hstate = hstate_file(vma->vm_file);

	psize = hstate_get_psize(hstate);
	radix__local_flush_tlb_page_psize(vma->vm_mm, vmaddr, psize);
}

void radix__flush_hugetlb_tlb_range(struct vm_area_struct *vma, unsigned long start,
				   unsigned long end)
{
	int psize;
	struct hstate *hstate = hstate_file(vma->vm_file);

	psize = hstate_get_psize(hstate);
	/*
	 * Flush PWC even if we get PUD_SIZE hugetlb invalidate to keep this simpler.
	 */
	if (end - start >= PUD_SIZE)
		radix__flush_tlb_pwc_range_psize(vma->vm_mm, start, end, psize);
	else
		radix__flush_tlb_range_psize(vma->vm_mm, start, end, psize);
	mmu_notifier_invalidate_range(vma->vm_mm, start, end);
}

void radix__huge_ptep_modify_prot_commit(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep,
					 pte_t old_pte, pte_t pte)
{
	struct mm_struct *mm = vma->vm_mm;

	/*
	 * POWER9 NMMU must flush the TLB after clearing the PTE before
	 * installing a PTE with more relaxed access permissions, see
	 * radix__ptep_set_access_flags.
	 */
	if (!cpu_has_feature(CPU_FTR_ARCH_31) &&
	    is_pte_rw_upgrade(pte_val(old_pte), pte_val(pte)) &&
	    atomic_read(&mm->context.copros) > 0)
		radix__flush_hugetlb_page(vma, addr);

	set_huge_pte_at(vma->vm_mm, addr, ptep, pte);
}
