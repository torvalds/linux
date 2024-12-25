/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_TLB_H
#define _ASM_LKL_TLB_H

#ifndef CONFIG_MMU
#define tlb_start_vma(tlb, vma)				do { } while (0)
#define tlb_end_vma(tlb, vma)				do { } while (0)
#define __tlb_remove_tlb_entry(tlb, pte, address)	do { } while (0)
#define tlb_flush(tlb)					do { } while (0)

#include <asm-generic/tlb.h>

#else
#include <linux/mm.h>
#include <asm/tlbflush.h>
#include <asm-generic/cacheflush.h>
#include <asm-generic/tlb.h>
#endif // CONFIG_MMU

#endif /* _ASM_LKL_TLB_H */
