// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __ASMNDS32_TLB_H
#define __ASMNDS32_TLB_H

#include <asm-generic/tlb.h>

#define __pte_free_tlb(tlb, pte, addr)	pte_free((tlb)->mm, pte)
#define __pmd_free_tlb(tlb, pmd, addr)	pmd_free((tln)->mm, pmd)

#endif
