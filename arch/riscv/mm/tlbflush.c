// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <asm/sbi.h>

void flush_tlb_all(void)
{
	sbi_remote_sfence_vma(NULL, 0, -1);
}

/*
 * This function must not be called with cmask being null.
 * Kernel may panic if cmask is NULL.
 */
static void __sbi_tlb_flush_range(struct cpumask *cmask, unsigned long start,
				  unsigned long size)
{
	struct cpumask hmask;
	unsigned int cpuid;

	if (cpumask_empty(cmask))
		return;

	cpuid = get_cpu();

	if (cpumask_any_but(cmask, cpuid) >= nr_cpu_ids) {
		/* local cpu is the only cpu present in cpumask */
		local_flush_tlb_all();
	} else {
		riscv_cpuid_to_hartid_mask(cmask, &hmask);
		sbi_remote_sfence_vma(cpumask_bits(&hmask), start, size);
	}

	put_cpu();
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
