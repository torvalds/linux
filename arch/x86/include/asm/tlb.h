#ifndef _ASM_X86_TLB_H
#define _ASM_X86_TLB_H

#define tlb_start_vma(tlb, vma) do { } while (0)
#define tlb_end_vma(tlb, vma) do { } while (0)
#define __tlb_remove_tlb_entry(tlb, ptep, address) do { } while (0)

#define tlb_flush(tlb)							\
{									\
	if (tlb->fullmm == 0)						\
		flush_tlb_mm_range(tlb->mm, tlb->start, tlb->end, 0UL);	\
	else								\
		flush_tlb_mm_range(tlb->mm, 0UL, TLB_FLUSH_ALL, 0UL);	\
}

#include <asm-generic/tlb.h>

#endif /* _ASM_X86_TLB_H */
