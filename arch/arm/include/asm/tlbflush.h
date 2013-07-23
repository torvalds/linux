/*
 *  arch/arm/include/asm/tlbflush.h
 *
 *  Copyright (C) 1999-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_TLBFLUSH_H
#define _ASMARM_TLBFLUSH_H

#ifdef CONFIG_MMU

#include <asm/glue.h>

#define TLB_V4_U_PAGE	(1 << 1)
#define TLB_V4_D_PAGE	(1 << 2)
#define TLB_V4_I_PAGE	(1 << 3)
#define TLB_V6_U_PAGE	(1 << 4)
#define TLB_V6_D_PAGE	(1 << 5)
#define TLB_V6_I_PAGE	(1 << 6)

#define TLB_V4_U_FULL	(1 << 9)
#define TLB_V4_D_FULL	(1 << 10)
#define TLB_V4_I_FULL	(1 << 11)
#define TLB_V6_U_FULL	(1 << 12)
#define TLB_V6_D_FULL	(1 << 13)
#define TLB_V6_I_FULL	(1 << 14)

#define TLB_V6_U_ASID	(1 << 16)
#define TLB_V6_D_ASID	(1 << 17)
#define TLB_V6_I_ASID	(1 << 18)

#define TLB_V6_BP	(1 << 19)

/* Unified Inner Shareable TLB operations (ARMv7 MP extensions) */
#define TLB_V7_UIS_PAGE	(1 << 20)
#define TLB_V7_UIS_FULL (1 << 21)
#define TLB_V7_UIS_ASID (1 << 22)
#define TLB_V7_UIS_BP	(1 << 23)

#define TLB_BARRIER	(1 << 28)
#define TLB_L2CLEAN_FR	(1 << 29)		/* Feroceon */
#define TLB_DCLEAN	(1 << 30)
#define TLB_WB		(1 << 31)

/*
 *	MMU TLB Model
 *	=============
 *
 *	We have the following to choose from:
 *	  v4    - ARMv4 without write buffer
 *	  v4wb  - ARMv4 with write buffer without I TLB flush entry instruction
 *	  v4wbi - ARMv4 with write buffer with I TLB flush entry instruction
 *	  fr    - Feroceon (v4wbi with non-outer-cacheable page table walks)
 *	  fa    - Faraday (v4 with write buffer with UTLB)
 *	  v6wbi - ARMv6 with write buffer with I TLB flush entry instruction
 *	  v7wbi - identical to v6wbi
 */
#undef _TLB
#undef MULTI_TLB

#ifdef CONFIG_SMP_ON_UP
#define MULTI_TLB 1
#endif

#define v4_tlb_flags	(TLB_V4_U_FULL | TLB_V4_U_PAGE)

#ifdef CONFIG_CPU_TLB_V4WT
# define v4_possible_flags	v4_tlb_flags
# define v4_always_flags	v4_tlb_flags
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v4
# endif
#else
# define v4_possible_flags	0
# define v4_always_flags	(-1UL)
#endif

#define fa_tlb_flags	(TLB_WB | TLB_DCLEAN | TLB_BARRIER | \
			 TLB_V4_U_FULL | TLB_V4_U_PAGE)

#ifdef CONFIG_CPU_TLB_FA
# define fa_possible_flags	fa_tlb_flags
# define fa_always_flags	fa_tlb_flags
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB fa
# endif
#else
# define fa_possible_flags	0
# define fa_always_flags	(-1UL)
#endif

#define v4wbi_tlb_flags	(TLB_WB | TLB_DCLEAN | \
			 TLB_V4_I_FULL | TLB_V4_D_FULL | \
			 TLB_V4_I_PAGE | TLB_V4_D_PAGE)

#ifdef CONFIG_CPU_TLB_V4WBI
# define v4wbi_possible_flags	v4wbi_tlb_flags
# define v4wbi_always_flags	v4wbi_tlb_flags
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v4wbi
# endif
#else
# define v4wbi_possible_flags	0
# define v4wbi_always_flags	(-1UL)
#endif

#define fr_tlb_flags	(TLB_WB | TLB_DCLEAN | TLB_L2CLEAN_FR | \
			 TLB_V4_I_FULL | TLB_V4_D_FULL | \
			 TLB_V4_I_PAGE | TLB_V4_D_PAGE)

#ifdef CONFIG_CPU_TLB_FEROCEON
# define fr_possible_flags	fr_tlb_flags
# define fr_always_flags	fr_tlb_flags
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v4wbi
# endif
#else
# define fr_possible_flags	0
# define fr_always_flags	(-1UL)
#endif

#define v4wb_tlb_flags	(TLB_WB | TLB_DCLEAN | \
			 TLB_V4_I_FULL | TLB_V4_D_FULL | \
			 TLB_V4_D_PAGE)

#ifdef CONFIG_CPU_TLB_V4WB
# define v4wb_possible_flags	v4wb_tlb_flags
# define v4wb_always_flags	v4wb_tlb_flags
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v4wb
# endif
#else
# define v4wb_possible_flags	0
# define v4wb_always_flags	(-1UL)
#endif

#define v6wbi_tlb_flags (TLB_WB | TLB_DCLEAN | TLB_BARRIER | \
			 TLB_V6_I_FULL | TLB_V6_D_FULL | \
			 TLB_V6_I_PAGE | TLB_V6_D_PAGE | \
			 TLB_V6_I_ASID | TLB_V6_D_ASID | \
			 TLB_V6_BP)

#ifdef CONFIG_CPU_TLB_V6
# define v6wbi_possible_flags	v6wbi_tlb_flags
# define v6wbi_always_flags	v6wbi_tlb_flags
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v6wbi
# endif
#else
# define v6wbi_possible_flags	0
# define v6wbi_always_flags	(-1UL)
#endif

#define v7wbi_tlb_flags_smp	(TLB_WB | TLB_BARRIER | \
				 TLB_V7_UIS_FULL | TLB_V7_UIS_PAGE | \
				 TLB_V7_UIS_ASID | TLB_V7_UIS_BP)
#define v7wbi_tlb_flags_up	(TLB_WB | TLB_DCLEAN | TLB_BARRIER | \
				 TLB_V6_U_FULL | TLB_V6_U_PAGE | \
				 TLB_V6_U_ASID | TLB_V6_BP)

#ifdef CONFIG_CPU_TLB_V7

# ifdef CONFIG_SMP_ON_UP
#  define v7wbi_possible_flags	(v7wbi_tlb_flags_smp | v7wbi_tlb_flags_up)
#  define v7wbi_always_flags	(v7wbi_tlb_flags_smp & v7wbi_tlb_flags_up)
# elif defined(CONFIG_SMP)
#  define v7wbi_possible_flags	v7wbi_tlb_flags_smp
#  define v7wbi_always_flags	v7wbi_tlb_flags_smp
# else
#  define v7wbi_possible_flags	v7wbi_tlb_flags_up
#  define v7wbi_always_flags	v7wbi_tlb_flags_up
# endif
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v7wbi
# endif
#else
# define v7wbi_possible_flags	0
# define v7wbi_always_flags	(-1UL)
#endif

#ifndef _TLB
#error Unknown TLB model
#endif

#ifndef __ASSEMBLY__

#include <linux/sched.h>

struct cpu_tlb_fns {
	void (*flush_user_range)(unsigned long, unsigned long, struct vm_area_struct *);
	void (*flush_kern_range)(unsigned long, unsigned long);
	unsigned long tlb_flags;
};

/*
 * Select the calling method
 */
#ifdef MULTI_TLB

#define __cpu_flush_user_tlb_range	cpu_tlb.flush_user_range
#define __cpu_flush_kern_tlb_range	cpu_tlb.flush_kern_range

#else

#define __cpu_flush_user_tlb_range	__glue(_TLB,_flush_user_tlb_range)
#define __cpu_flush_kern_tlb_range	__glue(_TLB,_flush_kern_tlb_range)

extern void __cpu_flush_user_tlb_range(unsigned long, unsigned long, struct vm_area_struct *);
extern void __cpu_flush_kern_tlb_range(unsigned long, unsigned long);

#endif

extern struct cpu_tlb_fns cpu_tlb;

#define __cpu_tlb_flags			cpu_tlb.tlb_flags

/*
 *	TLB Management
 *	==============
 *
 *	The arch/arm/mm/tlb-*.S files implement these methods.
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

/*
 * We optimise the code below by:
 *  - building a set of TLB flags that might be set in __cpu_tlb_flags
 *  - building a set of TLB flags that will always be set in __cpu_tlb_flags
 *  - if we're going to need __cpu_tlb_flags, access it once and only once
 *
 * This allows us to build optimal assembly for the single-CPU type case,
 * and as close to optimal given the compiler constrants for multi-CPU
 * case.  We could do better for the multi-CPU case if the compiler
 * implemented the "%?" method, but this has been discontinued due to too
 * many people getting it wrong.
 */
#define possible_tlb_flags	(v4_possible_flags | \
				 v4wbi_possible_flags | \
				 fr_possible_flags | \
				 v4wb_possible_flags | \
				 fa_possible_flags | \
				 v6wbi_possible_flags | \
				 v7wbi_possible_flags)

#define always_tlb_flags	(v4_always_flags & \
				 v4wbi_always_flags & \
				 fr_always_flags & \
				 v4wb_always_flags & \
				 fa_always_flags & \
				 v6wbi_always_flags & \
				 v7wbi_always_flags)

#define tlb_flag(f)	((always_tlb_flags & (f)) || (__tlb_flag & possible_tlb_flags & (f)))

#define __tlb_op(f, insnarg, arg)					\
	do {								\
		if (always_tlb_flags & (f))				\
			asm("mcr " insnarg				\
			    : : "r" (arg) : "cc");			\
		else if (possible_tlb_flags & (f))			\
			asm("tst %1, %2\n\t"				\
			    "mcrne " insnarg				\
			    : : "r" (arg), "r" (__tlb_flag), "Ir" (f)	\
			    : "cc");					\
	} while (0)

#define tlb_op(f, regs, arg)	__tlb_op(f, "p15, 0, %0, " regs, arg)
#define tlb_l2_op(f, regs, arg)	__tlb_op(f, "p15, 1, %0, " regs, arg)

static inline void local_flush_tlb_all(void)
{
	const int zero = 0;
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	if (tlb_flag(TLB_WB))
		dsb();

	tlb_op(TLB_V4_U_FULL | TLB_V6_U_FULL, "c8, c7, 0", zero);
	tlb_op(TLB_V4_D_FULL | TLB_V6_D_FULL, "c8, c6, 0", zero);
	tlb_op(TLB_V4_I_FULL | TLB_V6_I_FULL, "c8, c5, 0", zero);
	tlb_op(TLB_V7_UIS_FULL, "c8, c3, 0", zero);

	if (tlb_flag(TLB_BARRIER)) {
		dsb();
		isb();
	}
}

static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
	const int zero = 0;
	const int asid = ASID(mm);
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	if (tlb_flag(TLB_WB))
		dsb();

	if (possible_tlb_flags & (TLB_V4_U_FULL|TLB_V4_D_FULL|TLB_V4_I_FULL)) {
		if (cpumask_test_cpu(get_cpu(), mm_cpumask(mm))) {
			tlb_op(TLB_V4_U_FULL, "c8, c7, 0", zero);
			tlb_op(TLB_V4_D_FULL, "c8, c6, 0", zero);
			tlb_op(TLB_V4_I_FULL, "c8, c5, 0", zero);
		}
		put_cpu();
	}

	tlb_op(TLB_V6_U_ASID, "c8, c7, 2", asid);
	tlb_op(TLB_V6_D_ASID, "c8, c6, 2", asid);
	tlb_op(TLB_V6_I_ASID, "c8, c5, 2", asid);
#ifdef CONFIG_ARM_ERRATA_720789
	tlb_op(TLB_V7_UIS_ASID, "c8, c3, 0", zero);
#else
	tlb_op(TLB_V7_UIS_ASID, "c8, c3, 2", asid);
#endif

	if (tlb_flag(TLB_BARRIER))
		dsb();
}

static inline void
local_flush_tlb_page(struct vm_area_struct *vma, unsigned long uaddr)
{
	const int zero = 0;
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	uaddr = (uaddr & PAGE_MASK) | ASID(vma->vm_mm);

	if (tlb_flag(TLB_WB))
		dsb();

	if (possible_tlb_flags & (TLB_V4_U_PAGE|TLB_V4_D_PAGE|TLB_V4_I_PAGE|TLB_V4_I_FULL) &&
	    cpumask_test_cpu(smp_processor_id(), mm_cpumask(vma->vm_mm))) {
		tlb_op(TLB_V4_U_PAGE, "c8, c7, 1", uaddr);
		tlb_op(TLB_V4_D_PAGE, "c8, c6, 1", uaddr);
		tlb_op(TLB_V4_I_PAGE, "c8, c5, 1", uaddr);
		if (!tlb_flag(TLB_V4_I_PAGE) && tlb_flag(TLB_V4_I_FULL))
			asm("mcr p15, 0, %0, c8, c5, 0" : : "r" (zero) : "cc");
	}

	tlb_op(TLB_V6_U_PAGE, "c8, c7, 1", uaddr);
	tlb_op(TLB_V6_D_PAGE, "c8, c6, 1", uaddr);
	tlb_op(TLB_V6_I_PAGE, "c8, c5, 1", uaddr);
#ifdef CONFIG_ARM_ERRATA_720789
	tlb_op(TLB_V7_UIS_PAGE, "c8, c3, 3", uaddr & PAGE_MASK);
#else
	tlb_op(TLB_V7_UIS_PAGE, "c8, c3, 1", uaddr);
#endif

	if (tlb_flag(TLB_BARRIER))
		dsb();
}

static inline void local_flush_tlb_kernel_page(unsigned long kaddr)
{
	const int zero = 0;
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	kaddr &= PAGE_MASK;

	if (tlb_flag(TLB_WB))
		dsb();

	tlb_op(TLB_V4_U_PAGE, "c8, c7, 1", kaddr);
	tlb_op(TLB_V4_D_PAGE, "c8, c6, 1", kaddr);
	tlb_op(TLB_V4_I_PAGE, "c8, c5, 1", kaddr);
	if (!tlb_flag(TLB_V4_I_PAGE) && tlb_flag(TLB_V4_I_FULL))
		asm("mcr p15, 0, %0, c8, c5, 0" : : "r" (zero) : "cc");

	tlb_op(TLB_V6_U_PAGE, "c8, c7, 1", kaddr);
	tlb_op(TLB_V6_D_PAGE, "c8, c6, 1", kaddr);
	tlb_op(TLB_V6_I_PAGE, "c8, c5, 1", kaddr);
	tlb_op(TLB_V7_UIS_PAGE, "c8, c3, 1", kaddr);

	if (tlb_flag(TLB_BARRIER)) {
		dsb();
		isb();
	}
}

static inline void local_flush_bp_all(void)
{
	const int zero = 0;
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	if (tlb_flag(TLB_V7_UIS_BP))
		asm("mcr p15, 0, %0, c7, c1, 6" : : "r" (zero));
	else if (tlb_flag(TLB_V6_BP))
		asm("mcr p15, 0, %0, c7, c5, 6" : : "r" (zero));

	if (tlb_flag(TLB_BARRIER))
		isb();
}

#include <asm/cputype.h>
#ifdef CONFIG_ARM_ERRATA_798181
static inline int erratum_a15_798181(void)
{
	unsigned int midr = read_cpuid_id();

	/* Cortex-A15 r0p0..r3p2 affected */
	if ((midr & 0xff0ffff0) != 0x410fc0f0 || midr > 0x413fc0f2)
		return 0;
	return 1;
}

static inline void dummy_flush_tlb_a15_erratum(void)
{
	/*
	 * Dummy TLBIMVAIS. Using the unmapped address 0 and ASID 0.
	 */
	asm("mcr p15, 0, %0, c8, c3, 1" : : "r" (0));
	dsb();
}
#else
static inline int erratum_a15_798181(void)
{
	return 0;
}

static inline void dummy_flush_tlb_a15_erratum(void)
{
}
#endif

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
static inline void flush_pmd_entry(void *pmd)
{
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	tlb_op(TLB_DCLEAN, "c7, c10, 1	@ flush_pmd", pmd);
	tlb_l2_op(TLB_L2CLEAN_FR, "c15, c9, 1  @ L2 flush_pmd", pmd);

	if (tlb_flag(TLB_WB))
		dsb();
}

static inline void clean_pmd_entry(void *pmd)
{
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	tlb_op(TLB_DCLEAN, "c7, c10, 1	@ flush_pmd", pmd);
	tlb_l2_op(TLB_L2CLEAN_FR, "c15, c9, 1  @ L2 flush_pmd", pmd);
}

#undef tlb_op
#undef tlb_flag
#undef always_tlb_flags
#undef possible_tlb_flags

/*
 * Convert calls to our calling convention.
 */
#define local_flush_tlb_range(vma,start,end)	__cpu_flush_user_tlb_range(start,end,vma)
#define local_flush_tlb_kernel_range(s,e)	__cpu_flush_kern_tlb_range(s,e)

#ifndef CONFIG_SMP
#define flush_tlb_all		local_flush_tlb_all
#define flush_tlb_mm		local_flush_tlb_mm
#define flush_tlb_page		local_flush_tlb_page
#define flush_tlb_kernel_page	local_flush_tlb_kernel_page
#define flush_tlb_range		local_flush_tlb_range
#define flush_tlb_kernel_range	local_flush_tlb_kernel_range
#define flush_bp_all		local_flush_bp_all
#else
extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long uaddr);
extern void flush_tlb_kernel_page(unsigned long kaddr);
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start, unsigned long end);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);
extern void flush_bp_all(void);
#endif

/*
 * If PG_dcache_clean is not set for the page, we need to ensure that any
 * cache entries for the kernels virtual memory range are written
 * back to the page. On ARMv6 and later, the cache coherency is handled via
 * the set_pte_at() function.
 */
#if __LINUX_ARM_ARCH__ < 6
extern void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr,
	pte_t *ptep);
#else
static inline void update_mmu_cache(struct vm_area_struct *vma,
				    unsigned long addr, pte_t *ptep)
{
}
#endif

#endif

#endif /* CONFIG_MMU */

#endif
