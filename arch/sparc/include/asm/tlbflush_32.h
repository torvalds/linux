/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC_TLBFLUSH_H
#define _SPARC_TLBFLUSH_H

#include <asm/cachetlb_32.h>

#define flush_tlb_all() \
	sparc32_cachetlb_ops->tlb_all()
#define flush_tlb_mm(mm) \
	sparc32_cachetlb_ops->tlb_mm(mm)
#define flush_tlb_range(vma, start, end) \
	sparc32_cachetlb_ops->tlb_range(vma, start, end)
#define flush_tlb_page(vma, addr) \
	sparc32_cachetlb_ops->tlb_page(vma, addr)

/*
 * This is a kludge, until I know better. --zaitcev XXX
 */
static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	flush_tlb_all();
}

#endif /* _SPARC_TLBFLUSH_H */
