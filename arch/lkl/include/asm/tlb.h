/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_TLB_H
#define _ASM_LKL_TLB_H

#define tlb_start_vma(tlb, vma)				do { } while (0)
#define tlb_end_vma(tlb, vma)				do { } while (0)
#define __tlb_remove_tlb_entry(tlb, pte, address)	do { } while (0)
#define tlb_flush(tlb)					do { } while (0)

#include <asm-generic/tlb.h>

#endif /* _ASM_LKL_TLB_H */
