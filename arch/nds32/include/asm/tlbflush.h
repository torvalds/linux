// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASMNDS32_TLBFLUSH_H
#define _ASMNDS32_TLBFLUSH_H

#include <linux/spinlock.h>
#include <linux/mm.h>
#include <nds32_intrinsic.h>

static inline void local_flush_tlb_all(void)
{
	__nds32__tlbop_flua();
	__nds32__isb();
}

static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
	__nds32__tlbop_flua();
	__nds32__isb();
}

static inline void local_flush_tlb_kernel_range(unsigned long start,
						unsigned long end)
{
	while (start < end) {
		__nds32__tlbop_inv(start);
		__nds32__isb();
		start += PAGE_SIZE;
	}
}

void local_flush_tlb_range(struct vm_area_struct *vma,
			   unsigned long start, unsigned long end);
void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long addr);

#define flush_tlb_all		local_flush_tlb_all
#define flush_tlb_mm		local_flush_tlb_mm
#define flush_tlb_range		local_flush_tlb_range
#define flush_tlb_page		local_flush_tlb_page
#define flush_tlb_kernel_range	local_flush_tlb_kernel_range

void update_mmu_cache(struct vm_area_struct *vma,
		      unsigned long address, pte_t * pte);
void tlb_migrate_finish(struct mm_struct *mm);

#endif
