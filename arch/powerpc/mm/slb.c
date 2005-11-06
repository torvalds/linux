/*
 * PowerPC64 SLB support.
 *
 * Copyright (C) 2004 David Gibson <dwg@au.ibm.com>, IBM
 * Based on earlier code writteh by:
 * Dave Engebretsen and Mike Corrigan {engebret|mikejc}@us.ibm.com
 *    Copyright (c) 2001 Dave Engebretsen
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/paca.h>
#include <asm/cputable.h>

extern void slb_allocate(unsigned long ea);

static inline unsigned long mk_esid_data(unsigned long ea, unsigned long slot)
{
	return (ea & ESID_MASK) | SLB_ESID_V | slot;
}

static inline unsigned long mk_vsid_data(unsigned long ea, unsigned long flags)
{
	return (get_kernel_vsid(ea) << SLB_VSID_SHIFT) | flags;
}

static inline void create_slbe(unsigned long ea, unsigned long flags,
			       unsigned long entry)
{
	asm volatile("slbmte  %0,%1" :
		     : "r" (mk_vsid_data(ea, flags)),
		       "r" (mk_esid_data(ea, entry))
		     : "memory" );
}

static void slb_flush_and_rebolt(void)
{
	/* If you change this make sure you change SLB_NUM_BOLTED
	 * appropriately too. */
	unsigned long ksp_flags = SLB_VSID_KERNEL;
	unsigned long ksp_esid_data;

	WARN_ON(!irqs_disabled());

	if (cpu_has_feature(CPU_FTR_16M_PAGE))
		ksp_flags |= SLB_VSID_L;

	ksp_esid_data = mk_esid_data(get_paca()->kstack, 2);
	if ((ksp_esid_data & ESID_MASK) == KERNELBASE)
		ksp_esid_data &= ~SLB_ESID_V;

	/* We need to do this all in asm, so we're sure we don't touch
	 * the stack between the slbia and rebolting it. */
	asm volatile("isync\n"
		     "slbia\n"
		     /* Slot 1 - first VMALLOC segment */
		     "slbmte	%0,%1\n"
		     /* Slot 2 - kernel stack */
		     "slbmte	%2,%3\n"
		     "isync"
		     :: "r"(mk_vsid_data(VMALLOCBASE, SLB_VSID_KERNEL)),
		        "r"(mk_esid_data(VMALLOCBASE, 1)),
		        "r"(mk_vsid_data(ksp_esid_data, ksp_flags)),
		        "r"(ksp_esid_data)
		     : "memory");
}

/* Flush all user entries from the segment table of the current processor. */
void switch_slb(struct task_struct *tsk, struct mm_struct *mm)
{
	unsigned long offset = get_paca()->slb_cache_ptr;
	unsigned long esid_data = 0;
	unsigned long pc = KSTK_EIP(tsk);
	unsigned long stack = KSTK_ESP(tsk);
	unsigned long unmapped_base;

	if (offset <= SLB_CACHE_ENTRIES) {
		int i;
		asm volatile("isync" : : : "memory");
		for (i = 0; i < offset; i++) {
			esid_data = ((unsigned long)get_paca()->slb_cache[i]
				<< SID_SHIFT) | SLBIE_C;
			asm volatile("slbie %0" : : "r" (esid_data));
		}
		asm volatile("isync" : : : "memory");
	} else {
		slb_flush_and_rebolt();
	}

	/* Workaround POWER5 < DD2.1 issue */
	if (offset == 1 || offset > SLB_CACHE_ENTRIES)
		asm volatile("slbie %0" : : "r" (esid_data));

	get_paca()->slb_cache_ptr = 0;
	get_paca()->context = mm->context;

	/*
	 * preload some userspace segments into the SLB.
	 */
	if (test_tsk_thread_flag(tsk, TIF_32BIT))
		unmapped_base = TASK_UNMAPPED_BASE_USER32;
	else
		unmapped_base = TASK_UNMAPPED_BASE_USER64;

	if (pc >= KERNELBASE)
		return;
	slb_allocate(pc);

	if (GET_ESID(pc) == GET_ESID(stack))
		return;

	if (stack >= KERNELBASE)
		return;
	slb_allocate(stack);

	if ((GET_ESID(pc) == GET_ESID(unmapped_base))
	    || (GET_ESID(stack) == GET_ESID(unmapped_base)))
		return;

	if (unmapped_base >= KERNELBASE)
		return;
	slb_allocate(unmapped_base);
}

void slb_initialize(void)
{
	/* On iSeries the bolted entries have already been set up by
	 * the hypervisor from the lparMap data in head.S */
#ifndef CONFIG_PPC_ISERIES
	unsigned long flags = SLB_VSID_KERNEL;

 	/* Invalidate the entire SLB (even slot 0) & all the ERATS */
 	if (cpu_has_feature(CPU_FTR_16M_PAGE))
 		flags |= SLB_VSID_L;

 	asm volatile("isync":::"memory");
 	asm volatile("slbmte  %0,%0"::"r" (0) : "memory");
	asm volatile("isync; slbia; isync":::"memory");
	create_slbe(KERNELBASE, flags, 0);
	create_slbe(VMALLOCBASE, SLB_VSID_KERNEL, 1);
	/* We don't bolt the stack for the time being - we're in boot,
	 * so the stack is in the bolted segment.  By the time it goes
	 * elsewhere, we'll call _switch() which will bolt in the new
	 * one. */
	asm volatile("isync":::"memory");
#endif

	get_paca()->stab_rr = SLB_NUM_BOLTED;
}
