/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_TLB_H
#define _ASM_RISCV_TLB_H

struct mmu_gather;

static void tlb_flush(struct mmu_gather *tlb);

#ifdef CONFIG_MMU

static inline void __tlb_remove_table(void *table)
{
	struct ptdesc *ptdesc = (struct ptdesc *)table;

	pagetable_dtor(ptdesc);
	pagetable_free(ptdesc);
}

#endif /* CONFIG_MMU */

#define tlb_flush tlb_flush
#include <asm-generic/tlb.h>

static inline void tlb_flush(struct mmu_gather *tlb)
{
#ifdef CONFIG_MMU
	if (tlb->fullmm || tlb->need_flush_all || tlb->freed_tables)
		flush_tlb_mm(tlb->mm);
	else
		flush_tlb_mm_range(tlb->mm, tlb->start, tlb->end,
				   tlb_get_unmap_size(tlb));
#endif
}

#endif /* _ASM_RISCV_TLB_H */
