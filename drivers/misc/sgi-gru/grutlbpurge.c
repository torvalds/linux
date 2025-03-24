// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SN Platform GRU Driver
 *
 * 		MMUOPS callbacks  + TLB flushing
 *
 * This file handles emu notifier callbacks from the core kernel. The callbacks
 * are used to update the TLB in the GRU as a result of changes in the
 * state of a process address space. This file also handles TLB invalidates
 * from the GRU driver.
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/hugetlb.h>
#include <linux/delay.h>
#include <linux/timex.h>
#include <linux/srcu.h>
#include <asm/processor.h>
#include "gru.h"
#include "grutables.h"
#include <asm/uv/uv_hub.h>

#define gru_random()	get_cycles()

/* ---------------------------------- TLB Invalidation functions --------
 * get_tgh_handle
 *
 * Find a TGH to use for issuing a TLB invalidate. For GRUs that are on the
 * local blade, use a fixed TGH that is a function of the blade-local cpu
 * number. Normally, this TGH is private to the cpu & no contention occurs for
 * the TGH. For offblade GRUs, select a random TGH in the range above the
 * private TGHs. A spinlock is required to access this TGH & the lock must be
 * released when the invalidate is completes. This sucks, but it is the best we
 * can do.
 *
 * Note that the spinlock is IN the TGH handle so locking does not involve
 * additional cache lines.
 *
 */
static inline int get_off_blade_tgh(struct gru_state *gru)
{
	int n;

	n = GRU_NUM_TGH - gru->gs_tgh_first_remote;
	n = gru_random() % n;
	n += gru->gs_tgh_first_remote;
	return n;
}

static inline int get_on_blade_tgh(struct gru_state *gru)
{
	return uv_blade_processor_id() >> gru->gs_tgh_local_shift;
}

static struct gru_tlb_global_handle *get_lock_tgh_handle(struct gru_state
							 *gru)
{
	struct gru_tlb_global_handle *tgh;
	int n;

	if (uv_numa_blade_id() == gru->gs_blade_id)
		n = get_on_blade_tgh(gru);
	else
		n = get_off_blade_tgh(gru);
	tgh = get_tgh_by_index(gru, n);
	lock_tgh_handle(tgh);

	return tgh;
}

static void get_unlock_tgh_handle(struct gru_tlb_global_handle *tgh)
{
	unlock_tgh_handle(tgh);
}

/*
 * gru_flush_tlb_range
 *
 * General purpose TLB invalidation function. This function scans every GRU in
 * the ENTIRE system (partition) looking for GRUs where the specified MM has
 * been accessed by the GRU. For each GRU found, the TLB must be invalidated OR
 * the ASID invalidated. Invalidating an ASID causes a new ASID to be assigned
 * on the next fault. This effectively flushes the ENTIRE TLB for the MM at the
 * cost of (possibly) a large number of future TLBmisses.
 *
 * The current algorithm is optimized based on the following (somewhat true)
 * assumptions:
 * 	- GRU contexts are not loaded into a GRU unless a reference is made to
 * 	  the data segment or control block (this is true, not an assumption).
 * 	  If a DS/CB is referenced, the user will also issue instructions that
 * 	  cause TLBmisses. It is not necessary to optimize for the case where
 * 	  contexts are loaded but no instructions cause TLB misses. (I know
 * 	  this will happen but I'm not optimizing for it).
 * 	- GRU instructions to invalidate TLB entries are SLOOOOWWW - normally
 * 	  a few usec but in unusual cases, it could be longer. Avoid if
 * 	  possible.
 * 	- intrablade process migration between cpus is not frequent but is
 * 	  common.
 * 	- a GRU context is not typically migrated to a different GRU on the
 * 	  blade because of intrablade migration
 *	- interblade migration is rare. Processes migrate their GRU context to
 *	  the new blade.
 *	- if interblade migration occurs, migration back to the original blade
 *	  is very very rare (ie., no optimization for this case)
 *	- most GRU instruction operate on a subset of the user REGIONS. Code
 *	  & shared library regions are not likely targets of GRU instructions.
 *
 * To help improve the efficiency of TLB invalidation, the GMS data
 * structure is maintained for EACH address space (MM struct). The GMS is
 * also the structure that contains the pointer to the mmu callout
 * functions. This structure is linked to the mm_struct for the address space
 * using the mmu "register" function. The mmu interfaces are used to
 * provide the callbacks for TLB invalidation. The GMS contains:
 *
 * 	- asid[maxgrus] array. ASIDs are assigned to a GRU when a context is
 * 	  loaded into the GRU.
 * 	- asidmap[maxgrus]. bitmap to make it easier to find non-zero asids in
 * 	  the above array
 *	- ctxbitmap[maxgrus]. Indicates the contexts that are currently active
 *	  in the GRU for the address space. This bitmap must be passed to the
 *	  GRU to do an invalidate.
 *
 * The current algorithm for invalidating TLBs is:
 * 	- scan the asidmap for GRUs where the context has been loaded, ie,
 * 	  asid is non-zero.
 * 	- for each gru found:
 * 		- if the ctxtmap is non-zero, there are active contexts in the
 * 		  GRU. TLB invalidate instructions must be issued to the GRU.
 *		- if the ctxtmap is zero, no context is active. Set the ASID to
 *		  zero to force a full TLB invalidation. This is fast but will
 *		  cause a lot of TLB misses if the context is reloaded onto the
 *		  GRU
 *
 */

void gru_flush_tlb_range(struct gru_mm_struct *gms, unsigned long start,
			 unsigned long len)
{
	struct gru_state *gru;
	struct gru_mm_tracker *asids;
	struct gru_tlb_global_handle *tgh;
	unsigned long num;
	int grupagesize, pagesize, pageshift, gid, asid;

	/* ZZZ TODO - handle huge pages */
	pageshift = PAGE_SHIFT;
	pagesize = (1UL << pageshift);
	grupagesize = GRU_PAGESIZE(pageshift);
	num = min(((len + pagesize - 1) >> pageshift), GRUMAXINVAL);

	STAT(flush_tlb);
	gru_dbg(grudev, "gms %p, start 0x%lx, len 0x%lx, asidmap 0x%lx\n", gms,
		start, len, gms->ms_asidmap[0]);

	spin_lock(&gms->ms_asid_lock);
	for_each_gru_in_bitmap(gid, gms->ms_asidmap) {
		STAT(flush_tlb_gru);
		gru = GID_TO_GRU(gid);
		asids = gms->ms_asids + gid;
		asid = asids->mt_asid;
		if (asids->mt_ctxbitmap && asid) {
			STAT(flush_tlb_gru_tgh);
			asid = GRUASID(asid, start);
			gru_dbg(grudev,
	"  FLUSH gruid %d, asid 0x%x, vaddr 0x%lx, vamask 0x%x, num %ld, cbmap 0x%x\n",
			      gid, asid, start, grupagesize, num, asids->mt_ctxbitmap);
			tgh = get_lock_tgh_handle(gru);
			tgh_invalidate(tgh, start, ~0, asid, grupagesize, 0,
				       num - 1, asids->mt_ctxbitmap);
			get_unlock_tgh_handle(tgh);
		} else {
			STAT(flush_tlb_gru_zero_asid);
			asids->mt_asid = 0;
			__clear_bit(gru->gs_gid, gms->ms_asidmap);
			gru_dbg(grudev,
	"  CLEARASID gruid %d, asid 0x%x, cbtmap 0x%x, asidmap 0x%lx\n",
				gid, asid, asids->mt_ctxbitmap,
				gms->ms_asidmap[0]);
		}
	}
	spin_unlock(&gms->ms_asid_lock);
}

/*
 * Flush the entire TLB on a chiplet.
 */
void gru_flush_all_tlb(struct gru_state *gru)
{
	struct gru_tlb_global_handle *tgh;

	gru_dbg(grudev, "gid %d\n", gru->gs_gid);
	tgh = get_lock_tgh_handle(gru);
	tgh_invalidate(tgh, 0, ~0, 0, 1, 1, GRUMAXINVAL - 1, 0xffff);
	get_unlock_tgh_handle(tgh);
}

/*
 * MMUOPS notifier callout functions
 */
static int gru_invalidate_range_start(struct mmu_notifier *mn,
			const struct mmu_notifier_range *range)
{
	struct gru_mm_struct *gms = container_of(mn, struct gru_mm_struct,
						 ms_notifier);

	STAT(mmu_invalidate_range);
	atomic_inc(&gms->ms_range_active);
	gru_dbg(grudev, "gms %p, start 0x%lx, end 0x%lx, act %d\n", gms,
		range->start, range->end, atomic_read(&gms->ms_range_active));
	gru_flush_tlb_range(gms, range->start, range->end - range->start);

	return 0;
}

static void gru_invalidate_range_end(struct mmu_notifier *mn,
			const struct mmu_notifier_range *range)
{
	struct gru_mm_struct *gms = container_of(mn, struct gru_mm_struct,
						 ms_notifier);

	/* ..._and_test() provides needed barrier */
	(void)atomic_dec_and_test(&gms->ms_range_active);

	wake_up_all(&gms->ms_wait_queue);
	gru_dbg(grudev, "gms %p, start 0x%lx, end 0x%lx\n",
		gms, range->start, range->end);
}

static struct mmu_notifier *gru_alloc_notifier(struct mm_struct *mm)
{
	struct gru_mm_struct *gms;

	gms = kzalloc(sizeof(*gms), GFP_KERNEL);
	if (!gms)
		return ERR_PTR(-ENOMEM);
	STAT(gms_alloc);
	spin_lock_init(&gms->ms_asid_lock);
	init_waitqueue_head(&gms->ms_wait_queue);

	return &gms->ms_notifier;
}

static void gru_free_notifier(struct mmu_notifier *mn)
{
	kfree(container_of(mn, struct gru_mm_struct, ms_notifier));
	STAT(gms_free);
}

static const struct mmu_notifier_ops gru_mmuops = {
	.invalidate_range_start	= gru_invalidate_range_start,
	.invalidate_range_end	= gru_invalidate_range_end,
	.alloc_notifier		= gru_alloc_notifier,
	.free_notifier		= gru_free_notifier,
};

struct gru_mm_struct *gru_register_mmu_notifier(void)
{
	struct mmu_notifier *mn;

	mn = mmu_notifier_get_locked(&gru_mmuops, current->mm);
	if (IS_ERR(mn))
		return ERR_CAST(mn);

	return container_of(mn, struct gru_mm_struct, ms_notifier);
}

void gru_drop_mmu_notifier(struct gru_mm_struct *gms)
{
	mmu_notifier_put(&gms->ms_notifier);
}

/*
 * Setup TGH parameters. There are:
 * 	- 24 TGH handles per GRU chiplet
 * 	- a portion (MAX_LOCAL_TGH) of the handles are reserved for
 * 	  use by blade-local cpus
 * 	- the rest are used by off-blade cpus. This usage is
 * 	  less frequent than blade-local usage.
 *
 * For now, use 16 handles for local flushes, 8 for remote flushes. If the blade
 * has less tan or equal to 16 cpus, each cpu has a unique handle that it can
 * use.
 */
#define MAX_LOCAL_TGH	16

void gru_tgh_flush_init(struct gru_state *gru)
{
	int cpus, shift = 0, n;

	cpus = uv_blade_nr_possible_cpus(gru->gs_blade_id);

	/* n = cpus rounded up to next power of 2 */
	if (cpus) {
		n = 1 << fls(cpus - 1);

		/*
		 * shift count for converting local cpu# to TGH index
		 *      0 if cpus <= MAX_LOCAL_TGH,
		 *      1 if cpus <= 2*MAX_LOCAL_TGH,
		 *      etc
		 */
		shift = max(0, fls(n - 1) - fls(MAX_LOCAL_TGH - 1));
	}
	gru->gs_tgh_local_shift = shift;

	/* first starting TGH index to use for remote purges */
	gru->gs_tgh_first_remote = (cpus + (1 << shift) - 1) >> shift;

}
