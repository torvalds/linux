// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/hugetlb.h>
#include <linux/mmu_notifier.h>
#include <asm/sbi.h>
#include <asm/mmu_context.h>

/*
 * Flush entire TLB if number of entries to be flushed is greater
 * than the threshold below.
 */
unsigned long tlb_flush_all_threshold __read_mostly = 64;

static void local_flush_tlb_range_threshold_asid(unsigned long start,
						 unsigned long size,
						 unsigned long stride,
						 unsigned long asid)
{
	unsigned long nr_ptes_in_range = DIV_ROUND_UP(size, stride);
	int i;

	if (nr_ptes_in_range > tlb_flush_all_threshold) {
		local_flush_tlb_all_asid(asid);
		return;
	}

	for (i = 0; i < nr_ptes_in_range; ++i) {
		local_flush_tlb_page_asid(start, asid);
		start += stride;
	}
}

static inline void local_flush_tlb_range_asid(unsigned long start,
		unsigned long size, unsigned long stride, unsigned long asid)
{
	if (size <= stride)
		local_flush_tlb_page_asid(start, asid);
	else if (size == FLUSH_TLB_MAX_SIZE)
		local_flush_tlb_all_asid(asid);
	else
		local_flush_tlb_range_threshold_asid(start, size, stride, asid);
}

/* Flush a range of kernel pages without broadcasting */
void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	local_flush_tlb_range_asid(start, end - start, PAGE_SIZE, FLUSH_TLB_NO_ASID);
}

static void __ipi_flush_tlb_all(void *info)
{
	local_flush_tlb_all();
}

void flush_tlb_all(void)
{
	if (num_online_cpus() < 2)
		local_flush_tlb_all();
	else if (riscv_use_sbi_for_rfence())
		sbi_remote_sfence_vma_asid(NULL, 0, FLUSH_TLB_MAX_SIZE, FLUSH_TLB_NO_ASID);
	else
		on_each_cpu(__ipi_flush_tlb_all, NULL, 1);
}

struct flush_tlb_range_data {
	unsigned long asid;
	unsigned long start;
	unsigned long size;
	unsigned long stride;
};

static void __ipi_flush_tlb_range_asid(void *info)
{
	struct flush_tlb_range_data *d = info;

	local_flush_tlb_range_asid(d->start, d->size, d->stride, d->asid);
}

static inline unsigned long get_mm_asid(struct mm_struct *mm)
{
	return mm ? cntx2asid(atomic_long_read(&mm->context.id)) : FLUSH_TLB_NO_ASID;
}

static void __flush_tlb_range(struct mm_struct *mm,
			      const struct cpumask *cmask,
			      unsigned long start, unsigned long size,
			      unsigned long stride)
{
	unsigned long asid = get_mm_asid(mm);
	unsigned int cpu;

	if (cpumask_empty(cmask))
		return;

	cpu = get_cpu();

	/* Check if the TLB flush needs to be sent to other CPUs. */
	if (cpumask_any_but(cmask, cpu) >= nr_cpu_ids) {
		local_flush_tlb_range_asid(start, size, stride, asid);
	} else if (riscv_use_sbi_for_rfence()) {
		sbi_remote_sfence_vma_asid(cmask, start, size, asid);
	} else {
		struct flush_tlb_range_data ftd;

		ftd.asid = asid;
		ftd.start = start;
		ftd.size = size;
		ftd.stride = stride;
		on_each_cpu_mask(cmask, __ipi_flush_tlb_range_asid, &ftd, 1);
	}

	put_cpu();

	if (mm)
		mmu_notifier_arch_invalidate_secondary_tlbs(mm, start, start + size);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	__flush_tlb_range(mm, mm_cpumask(mm), 0, FLUSH_TLB_MAX_SIZE, PAGE_SIZE);
}

void flush_tlb_mm_range(struct mm_struct *mm,
			unsigned long start, unsigned long end,
			unsigned int page_size)
{
	__flush_tlb_range(mm, mm_cpumask(mm), start, end - start, page_size);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	__flush_tlb_range(vma->vm_mm, mm_cpumask(vma->vm_mm),
			  addr, PAGE_SIZE, PAGE_SIZE);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	unsigned long stride_size;

	if (!is_vm_hugetlb_page(vma)) {
		stride_size = PAGE_SIZE;
	} else {
		stride_size = huge_page_size(hstate_vma(vma));

		/*
		 * As stated in the privileged specification, every PTE in a
		 * NAPOT region must be invalidated, so reset the stride in that
		 * case.
		 */
		if (has_svnapot()) {
			if (stride_size >= PGDIR_SIZE)
				stride_size = PGDIR_SIZE;
			else if (stride_size >= P4D_SIZE)
				stride_size = P4D_SIZE;
			else if (stride_size >= PUD_SIZE)
				stride_size = PUD_SIZE;
			else if (stride_size >= PMD_SIZE)
				stride_size = PMD_SIZE;
			else
				stride_size = PAGE_SIZE;
		}
	}

	__flush_tlb_range(vma->vm_mm, mm_cpumask(vma->vm_mm),
			  start, end - start, stride_size);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	__flush_tlb_range(NULL, cpu_online_mask,
			  start, end - start, PAGE_SIZE);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void flush_pmd_tlb_range(struct vm_area_struct *vma, unsigned long start,
			unsigned long end)
{
	__flush_tlb_range(vma->vm_mm, mm_cpumask(vma->vm_mm),
			  start, end - start, PMD_SIZE);
}
#endif

bool arch_tlbbatch_should_defer(struct mm_struct *mm)
{
	return true;
}

void arch_tlbbatch_add_pending(struct arch_tlbflush_unmap_batch *batch,
		struct mm_struct *mm, unsigned long start, unsigned long end)
{
	cpumask_or(&batch->cpumask, &batch->cpumask, mm_cpumask(mm));
	mmu_notifier_arch_invalidate_secondary_tlbs(mm, start, end);
}

void arch_flush_tlb_batched_pending(struct mm_struct *mm)
{
	flush_tlb_mm(mm);
}

void arch_tlbbatch_flush(struct arch_tlbflush_unmap_batch *batch)
{
	__flush_tlb_range(NULL, &batch->cpumask,
			  0, FLUSH_TLB_MAX_SIZE, PAGE_SIZE);
	cpumask_clear(&batch->cpumask);
}
