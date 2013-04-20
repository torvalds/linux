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
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
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

	preempt_disable();
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
	preempt_enable();
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
static void gru_invalidate_range_start(struct mmu_notifier *mn,
				       struct mm_struct *mm,
				       unsigned long start, unsigned long end)
{
	struct gru_mm_struct *gms = container_of(mn, struct gru_mm_struct,
						 ms_notifier);

	STAT(mmu_invalidate_range);
	atomic_inc(&gms->ms_range_active);
	gru_dbg(grudev, "gms %p, start 0x%lx, end 0x%lx, act %d\n", gms,
		start, end, atomic_read(&gms->ms_range_active));
	gru_flush_tlb_range(gms, start, end - start);
}

static void gru_invalidate_range_end(struct mmu_notifier *mn,
				     struct mm_struct *mm, unsigned long start,
				     unsigned long end)
{
	struct gru_mm_struct *gms = container_of(mn, struct gru_mm_struct,
						 ms_notifier);

	/* ..._and_test() provides needed barrier */
	(void)atomic_dec_and_test(&gms->ms_range_active);

	wake_up_all(&gms->ms_wait_queue);
	gru_dbg(grudev, "gms %p, start 0x%lx, end 0x%lx\n", gms, start, end);
}

static void gru_invalidate_page(struct mmu_notifier *mn, struct mm_struct *mm,
				unsigned long address)
{
	struct gru_mm_struct *gms = container_of(mn, struct gru_mm_struct,
						 ms_notifier);

	STAT(mmu_invalidate_page);
	gru_flush_tlb_range(gms, address, PAGE_SIZE);
	gru_dbg(grudev, "gms %p, address 0x%lx\n", gms, address);
}

static void gru_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct gru_mm_struct *gms = container_of(mn, struct gru_mm_struct,
						 ms_notifier);

	gms->ms_released = 1;
	gru_dbg(grudev, "gms %p\n", gms);
}


static const struct mmu_notifier_ops gru_mmuops = {
	.invalidate_page	= gru_invalidate_page,
	.invalidate_range_start	= gru_invalidate_range_start,
	.invalidate_range_end	= gru_invalidate_range_end,
	.release		= gru_release,
};

/* Move this to the basic mmu_notifier file. But for now... */
static struct mmu_notifier *mmu_find_ops(struct mm_struct *mm,
			const struct mmu_notifier_ops *ops)
{
	struct mmu_notifier *mn, *gru_mn = NULL;

	if (mm->mmu_notifier_mm) {
		rcu_read_lock();
		hlist_for_each_entry_rcu(mn, &mm->mmu_notifier_mm->list,
					 hlist)
		    if (mn->ops == ops) {
			gru_mn = mn;
			break;
		}
		rcu_read_unlock();
	}
	return gru_mn;
}

struct gru_mm_struct *gru_register_mmu_notifier(void)
{
	struct gru_mm_struct *gms;
	struct mmu_notifier *mn;
	int err;

	mn = mmu_find_ops(current->mm, &gru_mmuops);
	if (mn) {
		gms = container_of(mn, struct gru_mm_struct, ms_notifier);
		atomic_inc(&gms->ms_refcnt);
	} else {
		gms = kzalloc(sizeof(*gms), GFP_KERNEL);
		if (gms) {
			STAT(gms_alloc);
			spin_lock_init(&gms->ms_asid_lock);
			gms->ms_notifier.ops = &gru_mmuops;
			atomic_set(&gms->ms_refcnt, 1);
			init_waitqueue_head(&gms->ms_wait_queue);
			err = __mmu_notifier_register(&gms->ms_notifier, current->mm);
			if (err)
				goto error;
		}
	}
	gru_dbg(grudev, "gms %p, refcnt %d\n", gms,
		atomic_read(&gms->ms_refcnt));
	return gms;
error:
	kfree(gms);
	return ERR_PTR(err);
}

void gru_drop_mmu_notifier(struct gru_mm_struct *gms)
{
	gru_dbg(grudev, "gms %p, refcnt %d, released %d\n", gms,
		atomic_read(&gms->ms_refcnt), gms->ms_released);
	if (atomic_dec_return(&gms->ms_refcnt) == 0) {
		if (!gms->ms_released)
			mmu_notifier_unregister(&gms->ms_notifier, current->mm);
		kfree(gms);
		STAT(gms_free);
	}
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
