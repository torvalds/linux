#ifndef _ASM_X86_TLB_H
#define _ASM_X86_TLB_H

#define tlb_start_vma(tlb, vma) do { } while (0)
#define tlb_end_vma(tlb, vma) do { } while (0)
#define __tlb_remove_tlb_entry(tlb, ptep, address) do { } while (0)

#define tlb_flush(tlb)							\
{									\
	if (!tlb->fullmm && !tlb->need_flush_all) 			\
		flush_tlb_mm_range(tlb->mm, tlb->start, tlb->end, 0UL);	\
	else								\
		flush_tlb_mm_range(tlb->mm, 0UL, TLB_FLUSH_ALL, 0UL);	\
}

#include <asm-generic/tlb.h>

/*
 * While x86 architecture in general requires an IPI to perform TLB
 * shootdown, enablement code for several hypervisors overrides
 * .flush_tlb_others hook in pv_mmu_ops and implements it by issuing
 * a hypercall. To keep software pagetable walkers safe in this case we
 * switch to RCU based table free (HAVE_RCU_TABLE_FREE). See the comment
 * below 'ifdef CONFIG_HAVE_RCU_TABLE_FREE' in include/asm-generic/tlb.h
 * for more details.
 */
static inline void __tlb_remove_table(void *table)
{
	free_page_and_swap_cache(table);
}

#endif /* _ASM_X86_TLB_H */
