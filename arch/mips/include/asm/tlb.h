#ifndef __ASM_TLB_H
#define __ASM_TLB_H

#include <asm/cpu-features.h>
#include <asm/mipsregs.h>

/*
 * MIPS doesn't need any special per-pte or per-vma handling, except
 * we need to flush cache for area to be unmapped.
 */
#define tlb_start_vma(tlb, vma)					\
	do {							\
		if (!tlb->fullmm)				\
			flush_cache_range(vma, vma->vm_start, vma->vm_end); \
	}  while (0)
#define tlb_end_vma(tlb, vma) do { } while (0)
#define __tlb_remove_tlb_entry(tlb, ptep, address) do { } while (0)

/*
 * .. because we flush the whole mm when it fills up.
 */
#define tlb_flush(tlb) flush_tlb_mm((tlb)->mm)

#define _UNIQUE_ENTRYHI(base, idx)					\
		(((base) + ((idx) << (PAGE_SHIFT + 1))) |		\
		 (cpu_has_tlbinv ? MIPS_ENTRYHI_EHINV : 0))
#define UNIQUE_ENTRYHI(idx)		_UNIQUE_ENTRYHI(CKSEG0, idx)
#define UNIQUE_GUEST_ENTRYHI(idx)	_UNIQUE_ENTRYHI(CKSEG1, idx)

static inline unsigned int num_wired_entries(void)
{
	unsigned int wired = read_c0_wired();

	if (cpu_has_mips_r6)
		wired &= MIPSR6_WIRED_WIRED;

	return wired;
}

#include <asm-generic/tlb.h>

#endif /* __ASM_TLB_H */
