// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/smp.h>
#include <asm/sbi.h>

void flush_tlb_all(void)
{
	sbi_remote_sfence_vma(NULL, 0, -1);
}

static void __sbi_tlb_flush_range(struct cpumask *cmask, unsigned long start,
				  unsigned long size)
{
	struct cpumask hmask;

	riscv_cpuid_to_hartid_mask(cmask, &hmask);
	sbi_remote_sfence_vma(hmask.bits, start, size);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	__sbi_tlb_flush_range(mm_cpumask(mm), 0, -1);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	__sbi_tlb_flush_range(mm_cpumask(vma->vm_mm), addr, PAGE_SIZE);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	__sbi_tlb_flush_range(mm_cpumask(vma->vm_mm), start, end - start);
}
