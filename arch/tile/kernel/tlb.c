/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 */

#include <linux/cpumask.h>
#include <linux/module.h>
#include <asm/tlbflush.h>
#include <asm/homecache.h>
#include <hv/hypervisor.h>

/* From tlbflush.h */
DEFINE_PER_CPU(int, current_asid);
int min_asid, max_asid;

/*
 * Note that we flush the L1I (for VM_EXEC pages) as well as the TLB
 * so that when we are unmapping an executable page, we also flush it.
 * Combined with flushing the L1I at context switch time, this means
 * we don't have to do any other icache flushes.
 */

void flush_tlb_mm(struct mm_struct *mm)
{
	HV_Remote_ASID asids[NR_CPUS];
	int i = 0, cpu;
	for_each_cpu(cpu, mm_cpumask(mm)) {
		HV_Remote_ASID *asid = &asids[i++];
		asid->y = cpu / smp_topology.width;
		asid->x = cpu % smp_topology.width;
		asid->asid = per_cpu(current_asid, cpu);
	}
	flush_remote(0, HV_FLUSH_EVICT_L1I, mm_cpumask(mm),
		     0, 0, 0, NULL, asids, i);
}

void flush_tlb_current_task(void)
{
	flush_tlb_mm(current->mm);
}

void flush_tlb_page_mm(const struct vm_area_struct *vma, struct mm_struct *mm,
		       unsigned long va)
{
	unsigned long size = hv_page_size(vma);
	int cache = (vma->vm_flags & VM_EXEC) ? HV_FLUSH_EVICT_L1I : 0;
	flush_remote(0, cache, mm_cpumask(mm),
		     va, size, size, mm_cpumask(mm), NULL, 0);
}

void flush_tlb_page(const struct vm_area_struct *vma, unsigned long va)
{
	flush_tlb_page_mm(vma, vma->vm_mm, va);
}
EXPORT_SYMBOL(flush_tlb_page);

void flush_tlb_range(const struct vm_area_struct *vma,
		     unsigned long start, unsigned long end)
{
	unsigned long size = hv_page_size(vma);
	struct mm_struct *mm = vma->vm_mm;
	int cache = (vma->vm_flags & VM_EXEC) ? HV_FLUSH_EVICT_L1I : 0;
	flush_remote(0, cache, mm_cpumask(mm), start, end - start, size,
		     mm_cpumask(mm), NULL, 0);
}

void flush_tlb_all(void)
{
	int i;
	for (i = 0; ; ++i) {
		HV_VirtAddrRange r = hv_inquire_virtual(i);
		if (r.size == 0)
			break;
		flush_remote(0, HV_FLUSH_EVICT_L1I, cpu_online_mask,
			     r.start, r.size, PAGE_SIZE, cpu_online_mask,
			     NULL, 0);
		flush_remote(0, 0, NULL,
			     r.start, r.size, HPAGE_SIZE, cpu_online_mask,
			     NULL, 0);
	}
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	flush_remote(0, HV_FLUSH_EVICT_L1I, cpu_online_mask,
		     start, end - start, PAGE_SIZE, cpu_online_mask, NULL, 0);
}
