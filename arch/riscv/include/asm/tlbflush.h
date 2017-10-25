/*
 * Copyright (C) 2009 Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#ifndef _ASM_RISCV_TLBFLUSH_H
#define _ASM_RISCV_TLBFLUSH_H

#ifdef CONFIG_MMU

#include <linux/mm_types.h>

/* Flush entire local TLB */
static inline void local_flush_tlb_all(void)
{
	__asm__ __volatile__ ("sfence.vma" : : : "memory");
}

/* Flush one page from local TLB */
static inline void local_flush_tlb_page(unsigned long addr)
{
	__asm__ __volatile__ ("sfence.vma %0" : : "r" (addr) : "memory");
}

#ifndef CONFIG_SMP

#define flush_tlb_all() local_flush_tlb_all()
#define flush_tlb_page(vma, addr) local_flush_tlb_page(addr)
#define flush_tlb_range(vma, start, end) local_flush_tlb_all()

#else /* CONFIG_SMP */

#include <asm/sbi.h>

#define flush_tlb_all() sbi_remote_sfence_vma(0, 0, -1)
#define flush_tlb_page(vma, addr) flush_tlb_range(vma, addr, 0)
#define flush_tlb_range(vma, start, end) \
	sbi_remote_sfence_vma(0, start, (end) - (start))

#endif /* CONFIG_SMP */

/* Flush the TLB entries of the specified mm context */
static inline void flush_tlb_mm(struct mm_struct *mm)
{
	flush_tlb_all();
}

/* Flush a range of kernel pages */
static inline void flush_tlb_kernel_range(unsigned long start,
	unsigned long end)
{
	flush_tlb_all();
}

#endif /* CONFIG_MMU */

#endif /* _ASM_RISCV_TLBFLUSH_H */
