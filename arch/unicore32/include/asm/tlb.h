/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/tlb.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_TLB_H__
#define __UNICORE_TLB_H__

/*
 * unicore32 lacks an efficient flush_tlb_range(), use flush_tlb_mm().
 */

#define __pte_free_tlb(tlb, pte, addr)				\
	do {							\
		pgtable_page_dtor(pte);				\
		tlb_remove_page((tlb), (pte));			\
	} while (0)

#include <asm-generic/tlb.h>

#endif
