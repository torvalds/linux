/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_TLB_H
#define _ALPHA_TLB_H

#include <asm-generic/tlb.h>

#define __pte_free_tlb(tlb, pte, address)	tlb_remove_ptdesc((tlb), page_ptdesc(pte))
#define __pmd_free_tlb(tlb, pmd, address)	tlb_remove_ptdesc((tlb), virt_to_ptdesc(pmd))

#endif
