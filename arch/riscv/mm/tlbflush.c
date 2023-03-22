// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <asm/sbi.h>
#include <asm/mmu_context.h>

static inline void local_flush_tlb_all_asid(unsigned long asid)
{
	__asm__ __volatile__ ("sfence.vma x0, %0"
			:
			: "r" (asid)
			: "memory");
}

static inline void local_flush_tlb_page_asid(unsigned long addr,
		unsigned long asid)
{
	__asm__ __volatile__ ("sfence.vma %0, %1"
			:
			: "r" (addr), "r" (asid)
			: "memory");
}

void flush_tlb_all(void)
{
	sbi_remote_sfence_vma(NULL, 0, -1);
}

static void __sbi_tlb_flush_range(struct mm_struct *mm, unsigned long start,
				  unsigned long size, unsigned long stride)
{
	struct cpumask *cmask = mm_cpumask(mm);
	unsigned int cpuid;
	bool broadcast;

	if (cpumask_empty(cmask))
		return;

	cpuid = get_cpu();
	/* check if the tlbflush needs to be sent to other CPUs */
	broadcast = cpumask_any_but(cmask, cpuid) < nr_cpu_ids;
	if (static_branch_unlikely(&use_asid_allocator)) {
		unsigned long asid = atomic_long_read(&mm->context.id);

		if (broadcast) {
			sbi_remote_sfence_vma_asid(cmask, start, size, asid);
		} else if (size <= stride) {
			local_flush_tlb_page_asid(start, asid);
		} else {
			local_flush_tlb_all_asid(asid);
		}
	} else {
		if (broadcast) {
			sbi_remote_sfence_vma(cmask, start, size);
		} else if (size <= stride) {
			local_flush_tlb_page(start);
		} else {
			local_flush_tlb_all();
		}
	}

	put_cpu();
}

void flush_tlb_mm(struct mm_struct *mm)
{
	__sbi_tlb_flush_range(mm, 0, -1, PAGE_SIZE);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	__sbi_tlb_flush_range(vma->vm_mm, addr, PAGE_SIZE, PAGE_SIZE);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	__sbi_tlb_flush_range(vma->vm_mm, start, end - start, PAGE_SIZE);
}
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void flush_pmd_tlb_range(struct vm_area_struct *vma, unsigned long start,
			unsigned long end)
{
	__sbi_tlb_flush_range(vma->vm_mm, start, end - start, PMD_SIZE);
}
#endif
