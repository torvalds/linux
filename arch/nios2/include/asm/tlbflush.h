/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 */

#ifndef _ASM_NIOS2_TLBFLUSH_H
#define _ASM_NIOS2_TLBFLUSH_H

struct mm_struct;

/*
 * TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLB entries
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB entries
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_page(vma, address) flushes a page
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_kernel_page(address) flushes a kernel page
 *
 *  - reload_tlb_page(vma, address, pte) flushes the TLB for address like
 *    flush_tlb_page, then replaces it with a TLB for pte.
 */
extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long address)
{
	flush_tlb_range(vma, address, address + PAGE_SIZE);
}

static inline void flush_tlb_kernel_page(unsigned long address)
{
	flush_tlb_kernel_range(address, address + PAGE_SIZE);
}

extern void reload_tlb_page(struct vm_area_struct *vma, unsigned long addr,
			    pte_t pte);

#endif /* _ASM_NIOS2_TLBFLUSH_H */
