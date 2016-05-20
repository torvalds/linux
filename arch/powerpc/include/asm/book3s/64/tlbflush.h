#ifndef _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H
#define _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H

#define MMU_NO_CONTEXT	~0UL


#include <asm/book3s/64/tlbflush-hash.h>
#include <asm/book3s/64/tlbflush-radix.h>

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	if (radix_enabled())
		return radix__flush_tlb_range(vma, start, end);
	return hash__flush_tlb_range(vma, start, end);
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	if (radix_enabled())
		return radix__flush_tlb_kernel_range(start, end);
	return hash__flush_tlb_kernel_range(start, end);
}

static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
	if (radix_enabled())
		return radix__local_flush_tlb_mm(mm);
	return hash__local_flush_tlb_mm(mm);
}

static inline void local_flush_tlb_page(struct vm_area_struct *vma,
					unsigned long vmaddr)
{
	if (radix_enabled())
		return radix__local_flush_tlb_page(vma, vmaddr);
	return hash__local_flush_tlb_page(vma, vmaddr);
}

static inline void flush_tlb_page_nohash(struct vm_area_struct *vma,
					 unsigned long vmaddr)
{
	if (radix_enabled())
		return radix__flush_tlb_page(vma, vmaddr);
	return hash__flush_tlb_page_nohash(vma, vmaddr);
}

static inline void tlb_flush(struct mmu_gather *tlb)
{
	if (radix_enabled())
		return radix__tlb_flush(tlb);
	return hash__tlb_flush(tlb);
}

#ifdef CONFIG_SMP
static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (radix_enabled())
		return radix__flush_tlb_mm(mm);
	return hash__flush_tlb_mm(mm);
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long vmaddr)
{
	if (radix_enabled())
		return radix__flush_tlb_page(vma, vmaddr);
	return hash__flush_tlb_page(vma, vmaddr);
}
#else
#define flush_tlb_mm(mm)		local_flush_tlb_mm(mm)
#define flush_tlb_page(vma, addr)	local_flush_tlb_page(vma, addr)
#endif /* CONFIG_SMP */

#endif /*  _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H */
