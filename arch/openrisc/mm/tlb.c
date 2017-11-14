/*
 * OpenRISC tlb.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Julius Baxter <julius.baxter@orsoc.se>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/segment.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/spr_defs.h>

#define NO_CONTEXT -1

#define NUM_DTLB_SETS (1 << ((mfspr(SPR_IMMUCFGR) & SPR_IMMUCFGR_NTS) >> \
			    SPR_DMMUCFGR_NTS_OFF))
#define NUM_ITLB_SETS (1 << ((mfspr(SPR_IMMUCFGR) & SPR_IMMUCFGR_NTS) >> \
			    SPR_IMMUCFGR_NTS_OFF))
#define DTLB_OFFSET(addr) (((addr) >> PAGE_SHIFT) & (NUM_DTLB_SETS-1))
#define ITLB_OFFSET(addr) (((addr) >> PAGE_SHIFT) & (NUM_ITLB_SETS-1))
/*
 * Invalidate all TLB entries.
 *
 * This comes down to setting the 'valid' bit for all xTLBMR registers to 0.
 * Easiest way to accomplish this is to just zero out the xTLBMR register
 * completely.
 *
 */

void local_flush_tlb_all(void)
{
	int i;
	unsigned long num_tlb_sets;

	/* Determine number of sets for IMMU. */
	/* FIXME: Assumption is I & D nsets equal. */
	num_tlb_sets = NUM_ITLB_SETS;

	for (i = 0; i < num_tlb_sets; i++) {
		mtspr_off(SPR_DTLBMR_BASE(0), i, 0);
		mtspr_off(SPR_ITLBMR_BASE(0), i, 0);
	}
}

#define have_dtlbeir (mfspr(SPR_DMMUCFGR) & SPR_DMMUCFGR_TEIRI)
#define have_itlbeir (mfspr(SPR_IMMUCFGR) & SPR_IMMUCFGR_TEIRI)

/*
 * Invalidate a single page.  This is what the xTLBEIR register is for.
 *
 * There's no point in checking the vma for PAGE_EXEC to determine whether it's
 * the data or instruction TLB that should be flushed... that would take more
 * than the few instructions that the following compiles down to!
 *
 * The case where we don't have the xTLBEIR register really only works for
 * MMU's with a single way and is hard-coded that way.
 */

#define flush_dtlb_page_eir(addr) mtspr(SPR_DTLBEIR, addr)
#define flush_dtlb_page_no_eir(addr) \
	mtspr_off(SPR_DTLBMR_BASE(0), DTLB_OFFSET(addr), 0);

#define flush_itlb_page_eir(addr) mtspr(SPR_ITLBEIR, addr)
#define flush_itlb_page_no_eir(addr) \
	mtspr_off(SPR_ITLBMR_BASE(0), ITLB_OFFSET(addr), 0);

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	if (have_dtlbeir)
		flush_dtlb_page_eir(addr);
	else
		flush_dtlb_page_no_eir(addr);

	if (have_itlbeir)
		flush_itlb_page_eir(addr);
	else
		flush_itlb_page_no_eir(addr);
}

void local_flush_tlb_range(struct vm_area_struct *vma,
			   unsigned long start, unsigned long end)
{
	int addr;
	bool dtlbeir;
	bool itlbeir;

	dtlbeir = have_dtlbeir;
	itlbeir = have_itlbeir;

	for (addr = start; addr < end; addr += PAGE_SIZE) {
		if (dtlbeir)
			flush_dtlb_page_eir(addr);
		else
			flush_dtlb_page_no_eir(addr);

		if (itlbeir)
			flush_itlb_page_eir(addr);
		else
			flush_itlb_page_no_eir(addr);
	}
}

/*
 * Invalidate the selected mm context only.
 *
 * FIXME: Due to some bug here, we're flushing everything for now.
 * This should be changed to loop over over mm and call flush_tlb_range.
 */

void local_flush_tlb_mm(struct mm_struct *mm)
{

	/* Was seeing bugs with the mm struct passed to us. Scrapped most of
	   this function. */
	/* Several architctures do this */
	local_flush_tlb_all();
}

/* called in schedule() just before actually doing the switch_to */

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *next_tsk)
{
	/* remember the pgd for the fault handlers
	 * this is similar to the pgd register in some other CPU's.
	 * we need our own copy of it because current and active_mm
	 * might be invalid at points where we still need to derefer
	 * the pgd.
	 */
	current_pgd[smp_processor_id()] = next->pgd;

	/* We don't have context support implemented, so flush all
	 * entries belonging to previous map
	 */

	if (prev != next)
		local_flush_tlb_mm(prev);

}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context = NO_CONTEXT;
	return 0;
}

/* called by __exit_mm to destroy the used MMU context if any before
 * destroying the mm itself. this is only called when the last user of the mm
 * drops it.
 */

void destroy_context(struct mm_struct *mm)
{
	flush_tlb_mm(mm);

}

/* called once during VM initialization, from init.c */

void __init tlb_init(void)
{
	/* Do nothing... */
	/* invalidate the entire TLB */
	/* flush_tlb_all(); */
}
