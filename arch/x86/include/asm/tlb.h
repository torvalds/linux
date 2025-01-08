/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TLB_H
#define _ASM_X86_TLB_H

#define tlb_flush tlb_flush
static inline void tlb_flush(struct mmu_gather *tlb);

#include <asm-generic/tlb.h>

static inline void tlb_flush(struct mmu_gather *tlb)
{
	unsigned long start = 0UL, end = TLB_FLUSH_ALL;
	unsigned int stride_shift = tlb_get_unmap_shift(tlb);

	if (!tlb->fullmm && !tlb->need_flush_all) {
		start = tlb->start;
		end = tlb->end;
	}

	flush_tlb_mm_range(tlb->mm, start, end, stride_shift, tlb->freed_tables);
}

static inline void invlpg(unsigned long addr)
{
	asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
}

#endif /* _ASM_X86_TLB_H */
