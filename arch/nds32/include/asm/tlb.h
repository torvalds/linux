// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __ASMNDS32_TLB_H
#define __ASMNDS32_TLB_H

#define tlb_end_vma(tlb,vma)				\
	do { 						\
		if(!tlb->fullmm)			\
			flush_tlb_range(vma, vma->vm_start, vma->vm_end); \
	} while (0)

#define __tlb_remove_tlb_entry(tlb, pte, addr) do { } while (0)

#define tlb_flush(tlb)	flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>

#define __pte_free_tlb(tlb, pte, addr)	pte_free((tlb)->mm, pte)
#define __pmd_free_tlb(tlb, pmd, addr)	pmd_free((tln)->mm, pmd)

#endif
