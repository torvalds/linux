/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_TLB_H
#define _ASM_RISCV_TLB_H

struct mmu_gather;

static void tlb_flush(struct mmu_gather *tlb);

#ifdef CONFIG_MMU
#include <linux/swap.h>

/*
 * While riscv platforms with riscv_ipi_for_rfence as true require an IPI to
 * perform TLB shootdown, some platforms with riscv_ipi_for_rfence as false use
 * SBI to perform TLB shootdown. To keep software pagetable walkers safe in this
 * case we switch to RCU based table free (MMU_GATHER_RCU_TABLE_FREE). See the
 * comment below 'ifdef CONFIG_MMU_GATHER_RCU_TABLE_FREE' in include/asm-generic/tlb.h
 * for more details.
 */
static inline void __tlb_remove_table(void *table)
{
	free_page_and_swap_cache(table);
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
