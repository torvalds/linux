/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/tlbflush.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_TLBFLUSH_H__
#define __UNICORE_TLBFLUSH_H__

#ifndef __ASSEMBLY__

#include <linux/sched.h>

extern void __cpu_flush_user_tlb_range(unsigned long, unsigned long,
					struct vm_area_struct *);
extern void __cpu_flush_kern_tlb_range(unsigned long, unsigned long);

/*
 *	TLB Management
 *	==============
 *
 *	The arch/unicore/mm/tlb-*.S files implement these methods.
 *
 *	The TLB specific code is expected to perform whatever tests it
 *	needs to determine if it should invalidate the TLB for each
 *	call.  Start addresses are inclusive and end addresses are
 *	exclusive; it is safe to round these addresses down.
 *
 *	flush_tlb_all()
 *
 *		Invalidate the entire TLB.
 *
 *	flush_tlb_mm(mm)
 *
 *		Invalidate all TLB entries in a particular address
 *		space.
 *		- mm	- mm_struct describing address space
 *
 *	flush_tlb_range(mm,start,end)
 *
 *		Invalidate a range of TLB entries in the specified
 *		address space.
 *		- mm	- mm_struct describing address space
 *		- start - start address (may not be aligned)
 *		- end	- end address (exclusive, may not be aligned)
 *
 *	flush_tlb_page(vaddr,vma)
 *
 *		Invalidate the specified page in the specified address range.
 *		- vaddr - virtual address (may not be aligned)
 *		- vma	- vma_struct describing address range
 *
 *	flush_kern_tlb_page(kaddr)
 *
 *		Invalidate the TLB entry for the specified page.  The address
 *		will be in the kernels virtual memory space.  Current uses
 *		only require the D-TLB to be invalidated.
 *		- kaddr - Kernel virtual memory address
 */

static inline void local_flush_tlb_all(void)
{
	const int zero = 0;

	/* TLB invalidate all */
	asm("movc p0.c6, %0, #6; nop; nop; nop; nop; nop; nop; nop; nop"
		: : "r" (zero) : "cc");
}

static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
	const int zero = 0;

	if (cpumask_test_cpu(get_cpu(), mm_cpumask(mm))) {
		/* TLB invalidate all */
		asm("movc p0.c6, %0, #6; nop; nop; nop; nop; nop; nop; nop; nop"
			: : "r" (zero) : "cc");
	}
	put_cpu();
}

static inline void
local_flush_tlb_page(struct vm_area_struct *vma, unsigned long uaddr)
{
	if (cpumask_test_cpu(smp_processor_id(), mm_cpumask(vma->vm_mm))) {
#ifndef CONFIG_CPU_TLB_SINGLE_ENTRY_DISABLE
		/* iTLB invalidate page */
		asm("movc p0.c6, %0, #5; nop; nop; nop; nop; nop; nop; nop; nop"
			: : "r" (uaddr & PAGE_MASK) : "cc");
		/* dTLB invalidate page */
		asm("movc p0.c6, %0, #3; nop; nop; nop; nop; nop; nop; nop; nop"
			: : "r" (uaddr & PAGE_MASK) : "cc");
#else
		/* TLB invalidate all */
		asm("movc p0.c6, %0, #6; nop; nop; nop; nop; nop; nop; nop; nop"
			: : "r" (uaddr & PAGE_MASK) : "cc");
#endif
	}
}

static inline void local_flush_tlb_kernel_page(unsigned long kaddr)
{
#ifndef CONFIG_CPU_TLB_SINGLE_ENTRY_DISABLE
	/* iTLB invalidate page */
	asm("movc p0.c6, %0, #5; nop; nop; nop; nop; nop; nop; nop; nop"
		: : "r" (kaddr & PAGE_MASK) : "cc");
	/* dTLB invalidate page */
	asm("movc p0.c6, %0, #3; nop; nop; nop; nop; nop; nop; nop; nop"
		: : "r" (kaddr & PAGE_MASK) : "cc");
#else
	/* TLB invalidate all */
	asm("movc p0.c6, %0, #6; nop; nop; nop; nop; nop; nop; nop; nop"
		: : "r" (kaddr & PAGE_MASK) : "cc");
#endif
}

/*
 *	flush_pmd_entry
 *
 *	Flush a PMD entry (word aligned, or double-word aligned) to
 *	RAM if the TLB for the CPU we are running on requires this.
 *	This is typically used when we are creating PMD entries.
 *
 *	clean_pmd_entry
 *
 *	Clean (but don't drain the write buffer) if the CPU requires
 *	these operations.  This is typically used when we are removing
 *	PMD entries.
 */
static inline void flush_pmd_entry(pmd_t *pmd)
{
#ifndef CONFIG_CPU_DCACHE_LINE_DISABLE
	/* flush dcache line, see dcacheline_flush in proc-macros.S */
	asm("mov	r1, %0 << #20\n"
		"ldw	r2, =_stext\n"
		"add	r2, r2, r1 >> #20\n"
		"ldw	r1, [r2+], #0x0000\n"
		"ldw	r1, [r2+], #0x1000\n"
		"ldw	r1, [r2+], #0x2000\n"
		"ldw	r1, [r2+], #0x3000\n"
		: : "r" (pmd) : "r1", "r2");
#else
	/* flush dcache all */
	asm("movc p0.c5, %0, #14; nop; nop; nop; nop; nop; nop; nop; nop"
		: : "r" (pmd) : "cc");
#endif
}

static inline void clean_pmd_entry(pmd_t *pmd)
{
#ifndef CONFIG_CPU_DCACHE_LINE_DISABLE
	/* clean dcache line */
	asm("movc p0.c5, %0, #11; nop; nop; nop; nop; nop; nop; nop; nop"
		: : "r" (__pa(pmd) & ~(L1_CACHE_BYTES - 1)) : "cc");
#else
	/* clean dcache all */
	asm("movc p0.c5, %0, #10; nop; nop; nop; nop; nop; nop; nop; nop"
		: : "r" (pmd) : "cc");
#endif
}

/*
 * Convert calls to our calling convention.
 */
#define local_flush_tlb_range(vma, start, end)	\
	__cpu_flush_user_tlb_range(start, end, vma)
#define local_flush_tlb_kernel_range(s, e)	\
	__cpu_flush_kern_tlb_range(s, e)

#define flush_tlb_all		local_flush_tlb_all
#define flush_tlb_mm		local_flush_tlb_mm
#define flush_tlb_page		local_flush_tlb_page
#define flush_tlb_kernel_page	local_flush_tlb_kernel_page
#define flush_tlb_range		local_flush_tlb_range
#define flush_tlb_kernel_range	local_flush_tlb_kernel_range

/*
 * if PG_dcache_clean is not set for the page, we need to ensure that any
 * cache entries for the kernels virtual memory range are written
 * back to the page.
 */
extern void update_mmu_cache(struct vm_area_struct *vma,
		unsigned long addr, pte_t *ptep);

extern void do_bad_area(unsigned long addr, unsigned int fsr,
		struct pt_regs *regs);

#endif

#endif
