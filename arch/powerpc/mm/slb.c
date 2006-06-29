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

#undef DEBUG

#include <linux/config.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/paca.h>
#include <asm/cputable.h>
#include <asm/cacheflush.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

extern void slb_allocate_realmode(unsigned long ea);
extern void slb_allocate_user(unsigned long ea);

static void slb_allocate(unsigned long ea)
{
	/* Currently, we do real mode for all SLBs including user, but
	 * that will change if we bring back dynamic VSIDs
	 */
	slb_allocate_realmode(ea);
}

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

void slb_flush_and_rebolt(void)
{
	/* If you change this make sure you change SLB_NUM_BOLTED
	 * appropriately too. */
	unsigned long linear_llp, vmalloc_llp, lflags, vflags;
	unsigned long ksp_esid_data;

	WARN_ON(!irqs_disabled());

	linear_llp = mmu_psize_defs[mmu_linear_psize].sllp;
	vmalloc_llp = mmu_psize_defs[mmu_vmalloc_psize].sllp;
	lflags = SLB_VSID_KERNEL | linear_llp;
	vflags = SLB_VSID_KERNEL | vmalloc_llp;

	ksp_esid_data = mk_esid_data(get_paca()->kstack, 2);
	if ((ksp_esid_data & ESID_MASK) == PAGE_OFFSET)
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
		     :: "r"(mk_vsid_data(VMALLOC_START, vflags)),
		        "r"(mk_esid_data(VMALLOC_START, 1)),
		        "r"(mk_vsid_data(ksp_esid_data, lflags)),
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

	if (is_kernel_addr(pc))
		return;
	slb_allocate(pc);

	if (GET_ESID(pc) == GET_ESID(stack))
		return;

	if (is_kernel_addr(stack))
		return;
	slb_allocate(stack);

	if ((GET_ESID(pc) == GET_ESID(unmapped_base))
	    || (GET_ESID(stack) == GET_ESID(unmapped_base)))
		return;

	if (is_kernel_addr(unmapped_base))
		return;
	slb_allocate(unmapped_base);
}

static inline void patch_slb_encoding(unsigned int *insn_addr,
				      unsigned int immed)
{
	/* Assume the instruction had a "0" immediate value, just
	 * "or" in the new value
	 */
	*insn_addr |= immed;
	flush_icache_range((unsigned long)insn_addr, 4+
			   (unsigned long)insn_addr);
}

void slb_initialize(void)
{
	unsigned long linear_llp, vmalloc_llp, io_llp;
	static int slb_encoding_inited;
	extern unsigned int *slb_miss_kernel_load_linear;
	extern unsigned int *slb_miss_kernel_load_io;
#ifdef CONFIG_HUGETLB_PAGE
	extern unsigned int *slb_miss_user_load_huge;
	unsigned long huge_llp;

	huge_llp = mmu_psize_defs[mmu_huge_psize].sllp;
#endif

	/* Prepare our SLB miss handler based on our page size */
	linear_llp = mmu_psize_defs[mmu_linear_psize].sllp;
	io_llp = mmu_psize_defs[mmu_io_psize].sllp;
	vmalloc_llp = mmu_psize_defs[mmu_vmalloc_psize].sllp;
	get_paca()->vmalloc_sllp = SLB_VSID_KERNEL | vmalloc_llp;

	if (!slb_encoding_inited) {
		slb_encoding_inited = 1;
		patch_slb_encoding(slb_miss_kernel_load_linear,
				   SLB_VSID_KERNEL | linear_llp);
		patch_slb_encoding(slb_miss_kernel_load_io,
				   SLB_VSID_KERNEL | io_llp);

		DBG("SLB: linear  LLP = %04x\n", linear_llp);
		DBG("SLB: io      LLP = %04x\n", io_llp);
#ifdef CONFIG_HUGETLB_PAGE
		patch_slb_encoding(slb_miss_user_load_huge,
				   SLB_VSID_USER | huge_llp);
		DBG("SLB: huge    LLP = %04x\n", huge_llp);
#endif
	}

	/* On iSeries the bolted entries have already been set up by
	 * the hypervisor from the lparMap data in head.S */
#ifndef CONFIG_PPC_ISERIES
 {
	unsigned long lflags, vflags;

	lflags = SLB_VSID_KERNEL | linear_llp;
	vflags = SLB_VSID_KERNEL | vmalloc_llp;

	/* Invalidate the entire SLB (even slot 0) & all the ERATS */
	asm volatile("isync":::"memory");
	asm volatile("slbmte  %0,%0"::"r" (0) : "memory");
	asm volatile("isync; slbia; isync":::"memory");
	create_slbe(PAGE_OFFSET, lflags, 0);

	create_slbe(VMALLOC_START, vflags, 1);

	/* We don't bolt the stack for the time being - we're in boot,
	 * so the stack is in the bolted segment.  By the time it goes
	 * elsewhere, we'll call _switch() which will bolt in the new
	 * one. */
	asm volatile("isync":::"memory");
 }
#endif /* CONFIG_PPC_ISERIES */

	get_paca()->stab_rr = SLB_NUM_BOLTED;
}
