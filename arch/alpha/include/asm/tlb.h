/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_TLB_H
#define _ALPHA_TLB_H

#include <asm-generic/tlb.h>

#define __pte_free_tlb(tlb, pte, address)		pte_free((tlb)->mm, pte)
#define __pmd_free_tlb(tlb, pmd, address)		pmd_free((tlb)->mm, pmd)
 
#endif
