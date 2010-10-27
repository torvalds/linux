/* MN10300 TLB flushing functions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_TLBFLUSH_H
#define _ASM_TLBFLUSH_H

#include <asm/processor.h>

/**
 * local_flush_tlb - Flush the current MM's entries from the local CPU's TLBs
 */
static inline void local_flush_tlb(void)
{
	int w;
	asm volatile(
		"	mov	%1,%0		\n"
		"	or	%2,%0		\n"
		"	mov	%0,%1		\n"
		: "=d"(w)
		: "m"(MMUCTR), "i"(MMUCTR_IIV|MMUCTR_DIV)
		: "cc", "memory");
}

/**
 * local_flush_tlb_all - Flush all entries from the local CPU's TLBs
 */
#define local_flush_tlb_all()		local_flush_tlb()

/**
 * local_flush_tlb_one - Flush one entry from the local CPU's TLBs
 */
#define local_flush_tlb_one(addr)	local_flush_tlb()

/**
 * local_flush_tlb_page - Flush a page's entry from the local CPU's TLBs
 * @mm: The MM to flush for
 * @addr: The address of the target page in RAM (not its page struct)
 */
extern void local_flush_tlb_page(struct mm_struct *mm, unsigned long addr);


/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 */
#define flush_tlb_all()				\
do {						\
	preempt_disable();			\
	local_flush_tlb_all();			\
	preempt_enable();			\
} while (0)

#define flush_tlb_mm(mm)			\
do {						\
	preempt_disable();			\
	local_flush_tlb_all();			\
	preempt_enable();			\
} while (0)

#define flush_tlb_range(vma, start, end)			\
do {								\
	unsigned long __s __attribute__((unused)) = (start);	\
	unsigned long __e __attribute__((unused)) = (end);	\
	preempt_disable();					\
	local_flush_tlb_all();					\
	preempt_enable();					\
} while (0)

#define flush_tlb_page(vma, addr)	local_flush_tlb_page((vma)->vm_mm, addr)
#define flush_tlb()			flush_tlb_all()

#define flush_tlb_kernel_range(start, end)			\
do {								\
	unsigned long __s __attribute__((unused)) = (start);	\
	unsigned long __e __attribute__((unused)) = (end);	\
	flush_tlb_all();					\
} while (0)

#define flush_tlb_pgtables(mm, start, end)	do {} while (0)

#endif /* _ASM_TLBFLUSH_H */
