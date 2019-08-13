/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009 Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_TLBFLUSH_H
#define _ASM_RISCV_TLBFLUSH_H

#include <linux/mm_types.h>
#include <asm/smp.h>

/*
 * Flush entire local TLB.  'sfence.vma' implicitly fences with the instruction
 * cache as well, so a 'fence.i' is not necessary.
 */
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

static inline void flush_tlb_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	local_flush_tlb_all();
}

#define flush_tlb_mm(mm) flush_tlb_all()

#else /* CONFIG_SMP */

#include <asm/sbi.h>

static inline void remote_sfence_vma(struct cpumask *cmask, unsigned long start,
				     unsigned long size)
{
	struct cpumask hmask;

	cpumask_clear(&hmask);
	riscv_cpuid_to_hartid_mask(cmask, &hmask);
	sbi_remote_sfence_vma(hmask.bits, start, size);
}

#define flush_tlb_all() sbi_remote_sfence_vma(NULL, 0, -1)
#define flush_tlb_page(vma, addr) flush_tlb_range(vma, addr, 0)
#define flush_tlb_range(vma, start, end) \
	remote_sfence_vma(mm_cpumask((vma)->vm_mm), start, (end) - (start))
#define flush_tlb_mm(mm) \
	remote_sfence_vma(mm_cpumask(mm), 0, -1)

#endif /* CONFIG_SMP */

/* Flush a range of kernel pages */
static inline void flush_tlb_kernel_range(unsigned long start,
	unsigned long end)
{
	flush_tlb_all();
}

#endif /* _ASM_RISCV_TLBFLUSH_H */
