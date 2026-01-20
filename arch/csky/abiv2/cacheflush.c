// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/cache.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <asm/cache.h>
#include <asm/tlbflush.h>

void update_mmu_cache_range(struct vm_fault *vmf, struct vm_area_struct *vma,
		unsigned long address, pte_t *pte, unsigned int nr)
{
	unsigned long pfn = pte_pfn(*pte);
	struct folio *folio;
	unsigned int i;

	flush_tlb_page(vma, address);

	if (!pfn_valid(pfn))
		return;

	folio = page_folio(pfn_to_page(pfn));

	if (test_and_set_bit(PG_dcache_clean, &folio->flags.f))
		return;

	icache_inv_range(address, address + nr*PAGE_SIZE);
	for (i = 0; i < folio_nr_pages(folio); i++) {
		unsigned long addr = (unsigned long) kmap_local_folio(folio,
								i * PAGE_SIZE);

		dcache_wb_range(addr, addr + PAGE_SIZE);
		if (vma->vm_flags & VM_EXEC)
			icache_inv_range(addr, addr + PAGE_SIZE);
		kunmap_local((void *) addr);
	}
}

void flush_icache_deferred(struct mm_struct *mm)
{
	unsigned int cpu = smp_processor_id();
	cpumask_t *mask = &mm->context.icache_stale_mask;

	if (cpumask_test_cpu(cpu, mask)) {
		cpumask_clear_cpu(cpu, mask);
		/*
		 * Ensure the remote hart's writes are visible to this hart.
		 * This pairs with a barrier in flush_icache_mm.
		 */
		smp_mb();
		local_icache_inv_all(NULL);
	}
}

void flush_icache_mm_range(struct mm_struct *mm,
		unsigned long start, unsigned long end)
{
	unsigned int cpu;
	cpumask_t others, *mask;

	preempt_disable();

#ifdef CONFIG_CPU_HAS_ICACHE_INS
	if (mm == current->mm) {
		icache_inv_range(start, end);
		preempt_enable();
		return;
	}
#endif

	/* Mark every hart's icache as needing a flush for this MM. */
	mask = &mm->context.icache_stale_mask;
	cpumask_setall(mask);

	/* Flush this hart's I$ now, and mark it as flushed. */
	cpu = smp_processor_id();
	cpumask_clear_cpu(cpu, mask);
	local_icache_inv_all(NULL);

	/*
	 * Flush the I$ of other harts concurrently executing, and mark them as
	 * flushed.
	 */
	cpumask_andnot(&others, mm_cpumask(mm), cpumask_of(cpu));

	if (mm != current->active_mm || !cpumask_empty(&others)) {
		on_each_cpu_mask(&others, local_icache_inv_all, NULL, 1);
		cpumask_clear(mask);
	}

	preempt_enable();
}
