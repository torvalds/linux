/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __UM_TLBFLUSH_H
#define __UM_TLBFLUSH_H

#include <linux/mm.h>

// No-op implementation of TLB flushing for LKL arch.
static inline void flush_tlb_mm(struct mm_struct *mm) {}
static inline void flush_tlb_range(struct vm_area_struct *vma,
				unsigned long start, unsigned long end) {}
static inline void flush_tlb_page(struct vm_area_struct *vma,
				unsigned long address) {}
static inline void flush_tlb_kernel_range(unsigned long start,
				unsigned long end) {}

#endif
