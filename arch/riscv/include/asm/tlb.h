/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_TLB_H
#define _ASM_RISCV_TLB_H

struct mmu_gather;

static void tlb_flush(struct mmu_gather *tlb);

#define tlb_flush tlb_flush
#include <asm-generic/tlb.h>

static inline void tlb_flush(struct mmu_gather *tlb)
{
	flush_tlb_mm(tlb->mm);
}

#endif /* _ASM_RISCV_TLB_H */
