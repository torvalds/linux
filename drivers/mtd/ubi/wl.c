/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём), Thomas Gleixner
 */

/*
 * UBI wear-leveling sub-system.
 *
 * This sub-system is responsible for wear-leveling. It works in terms of
 * physical eraseblocks and erase counters and knows nothing about logical
 * eraseblocks, volumes, etc. From this sub-system's perspective all physical
 * eraseblocks are of two types - used and free. Used physical eraseblocks are
 * those that were "get" by the 'ubi_wl_get_peb()' function, and free physical
 * eraseblocks are those that were put by the 'ubi_wl_put_peb()' function.
 *
 * Physical eraseblocks returned by 'ubi_wl_get_peb()' have only erase counter
 * header. The rest of the physical eraseblock contains only %0xFF bytes.
 *
 * When physical eraseblocks are returned to the WL sub-system by means of the
 * 'ubi_wl_put_peb()' function, they are scheduled for erasure. The erasure is
 * done asynchronously in context of the per-UBI device background thread,
 * which is also managed by the WL sub-system.
 *
 * The wear-leveling is ensured by means of moving the contents of used
 * physical eraseblocks with low erase counter to free physical eraseblocks
 * with high erase counter.
 *
 * If the WL sub-system fails to erase a physical eraseblock, it marks it as
 * bad.
 *
 * This sub-system is also responsible for scrubbing. If a bit-flip is detected
 * in a physical eraseblock, it has to be moved. Technically this is the same
 * as moving it for wear-leveling reasons.
 *
 * As it was said, for the UBI sub-system all physical eraseblocks are either
 * "free" or "used". Free eraseblock are kept in the @wl->free RB-tree, while
 * used eraseblocks are kept in @wl->used, @wl->erroneous, or @wl->scrub
 * RB-trees, as well as (temporarily) in the @wl->pq queue.
 *
 * When the WL sub-system returns a physical eraseblock, the physical
 * eraseblock is protected from being moved for some "time". For this reason,
 * the physical eraseblock is not directly moved from the @wl->free tree to the
 * @wl->used tree. There is a protection queue in between where this
 * physical eraseblock is temporarily stored (@wl->pq).
 *
 * All this protection stuff is needed because:
 *  o we don't want to move physical eraseblocks just after we have given them
 *    to the user; instead, we first want to let users fill them up with data;
 *
 *  o there is a chance that the user will put the physical eraseblock very
 *    soon, so it makes sense not to move it for some time, but wait.
 *
 * Physical eraseblocks stay protected only for limited time. But the "time" is
 * measured in erase cycles in this case. This is implemented with help of the
 * protection queue. Eraseblocks are put to the tail of this queue when they
 * are returned by the 'ubi_wl_get_peb()', and eraseblocks are removed from the
 * head of the queue on each erase operation (for any eraseblock). So the
 * length of the queue defines how may (global) erase cycles PEBs are protected.
 *
 * To put it differently, each physical eraseblock has 2 main states: free and
 * used. The former state corresponds to the @wl->free tree. The latter state
 * is split up on several sub-states:
 * o the WL movement is allowed (@wl->used tree);
 * o the WL movement is disallowed (@wl->erroneous) because the PEB is
 *   erroneous - e.g., there was a read error;
 * o the WL movement is temporarily prohibited (@wl->pq queue);
 * o scrubbing is needed (@wl->scrub tree).
 *
 * Depending on the sub-state, wear-leveling entries of the used physical
 * eraseblocks may be kept in one of those structures.
 *
 * Note, in this implementation, we keep a small in-RAM object for each physical
 * eraseblock. This is surely not a scalable solution. But it appears to be good
 * enough for moderately large flashes and it is simple. In future, one may
 * re-work this sub-system and make it more scalable.
 *
 * At the moment this sub-system does not utilize the sequence number, which
 * was introduced relatively recently. But it would be wise to do this because
 * the sequence number of a logical eraseblock characterizes how old is it. For
 * example, when we move a PEB with low erase counter, and we need to pick the
 * target PEB, we pick a PEB with the highest EC if our PEB is "old" and we
 * pick target PEB with an average EC if our PEB is not very "old". This is a
 * room for future re-works of the WL sub-system.
 */

#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include "ubi.h"
#include "wl.h"

/* Number of physical eraseblocks reserved for wear-leveling purposes */
#define WL_RESERVED_PEBS 1

/*
 * Maximum difference between two erase counters. If this threshold is
 * exceeded, the WL sub-system starts moving data from used physical
 * eraseblocks with low erase counter to free physical eraseblocks with high
 * erase counter.
 */
#define UBI_WL_THRESHOLD CONFIG_MTD_UBI_WL_THRESHOLD

/*
 * When a physical eraseblock is moved, the WL sub-system has to pick the target
 * physical eraseblock to move to. The simplest way would be just to pick the
 * one with the highest erase counter. But in certain workloads this could lead
 * to an unlimited wear of one or few physical eraseblock. Indeed, imagine a
 * situation when the picked physical eraseblock is constantly erased after the
 * data is written to it. So, we have a constant which limits the highest erase
 * counter of the free physical eraseblock to pick. Namely, the WL sub-system
 * does not pick eraseblocks with erase counter greater than the lowest erase
 * counter plus %WL_FREE_MAX_DIFF.
 */
#define WL_FREE_MAX_DIFF (2*UBI_WL_THRESHOLD)

/*
 * Maximum number of consecutive background thread failures which is enough to
 * switch to read-only mode.
 */
#define WL_MAX_FAILURES 32

static int self_check_ec(struct ubi_device *ubi, int pnum, int ec);
static int self_check_in_wl_tree(const struct ubi_device *ubi,
				 struct ubi_wl_entry *e, struct rb_root *root);
static int self_check_in_pq(const struct ubi_device *ubi,
			    struct ubi_wl_entry *e);

/**
 * wl_tree_add - add a wear-leveling entry to a WL RB-tree.
 * @e: the wear-leveling entry to add
 * @root: the root of the tree
 *
 * Note, we use (erase counter, physical eraseblock number) pairs as keys in
 * the @ubi->used and @ubi->free RB-trees.
 */
static void wl_tree_add(struct ubi_wl_entry *e, struct rb_root *root)
{
	struct rb_node **p, *parent = NULL;

	p = &root->rb_node;
	while (*p) {
		struct ubi_wl_entry *e1;

		parent = *p;
		e1 = rb_entry(parent, struct ubi_wl_entry, u.rb);

		if (e->ec < e1->ec)
			p = &(*p)->rb_left;
		else if (e->ec > e1->ec)
			p = &(*p)->rb_right;
		else {
			ubi_assert(e->pnum != e1->pnum);
			if (e->pnum < e1->pnum)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;
		}
	}

	rb_link_node(&e->u.rb, parent, p);
	rb_insert_color(&e->u.rb, root);
}

/**
 * wl_tree_destroy - destroy a wear-leveling entry.
 * @ubi: UBI device description object
 * @e: the wear-leveling entry to add
 *
 * This function destroys a wear leveling entry and removes
 * the reference from the lookup table.
 */
static void wl_entry_destroy(struct ubi_device *ubi, struct ubi_wl_entry *e)
{
	ubi->lookuptbl[e->pnum] = NULL;
	kmem_cache_free(ubi_wl_entry_slab, e);
}

/**
 * do_work - do one pending work.
 * @ubi: UBI device description object
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int do_work(struct ubi_device *ubi)
{
	int err;
	struct ubi_work *wrk;

	cond_resched();

	/*
	 * @ubi->work_sem is used to synchronize with the workers. Workers take
	 * it in read mode, so many of them may be doing works at a time. But
	 * the queue flush code has to be sure the whole queue of works is
	 * done, and it takes the mutex in write mode.
	 */
	down_read(&ubi->work_sem);
	spin_lock(&ubi->wl_lock);
	if (list_empty(&ubi->works)) {
		spin_unlock(&ubi->wl_lock);
		up_read(&ubi->work_sem);
		return 0;
	}

	wrk = list_entry(ubi->works.next, struct ubi_work, list);
	list_del(&wrk->list);
	ubi->works_count -= 1;
	ubi_assert(ubi->works_count >= 0);
	spin_unlock(&ubi->wl_lock);

	/*
	 * Call the worker function. Do not touch the work structure
	 * after this call as it will have been freed or reused by that
	 * time by the worker function.
	 */
	err = wrk->func(ubi, wrk, 0);
	if (err)
		ubi_err(ubi, "work failed with error code %d", err);
	up_read(&ubi->work_sem);

	return err;
}

/**
 * in_wl_tree - check if wear-leveling entry is present in a WL RB-tree.
 * @e: the wear-leveling entry to check
 * @root: the root of the tree
 *
 * This function returns non-zero if @e is in the @root RB-tree and zero if it
 * is not.
 */
static int in_wl_tree(struct ubi_wl_entry *e, struct rb_root *root)
{
	struct rb_node *p;

	p = root->rb_node;
	while (p) {
		struct ubi_wl_entry *e1;

		e1 = rb_entry(p, struct ubi_wl_entry, u.rb);

		if (e->pnum == e1->pnum) {
			ubi_assert(e == e1);
			return 1;
		}

		if (e->ec < e1->ec)
			p = p->rb_left;
		else if (e->ec > e1->ec)
			p = p->rb_right;
		else {
			ubi_assert(e->pnum != e1->pnum);
			if (e->pnum < e1->pnum)
				p = p->rb_left;
			else
				p = p->rb_right;
		}
	}

	return 0;
}

/**
 * prot_queue_add - add physical eraseblock to the protection queue.
 * @ubi: UBI device description object
 * @e: the physical eraseblock to add
 *
 * This function adds @e to the tail of the protection queue @ubi->pq, where
 * @e will stay for %UBI_PROT_QUEUE_LEN erase operations and will be
 * temporarily protected from the wear-leveling worker. Note, @wl->lock has to
 * be locked.
 */
static void prot_queue_add(struct ubi_device *ubi, struct ubi_wl_entry *e)
{
	int pq_tail = ubi->pq_head - 1;

	if (pq_tail < 0)
		pq_tail = UBI_PROT_QUEUE_LEN - 1;
	ubi_assert(pq_tail >= 0 && pq_tail < UBI_PROT_QUEUE_LEN);
	list_add_tail(&e->u.list, &ubi->pq[pq_tail]);
	dbg_wl("added PEB %d EC %d to the protection queue", e->pnum, e->ec);
}

/**
 * find_wl_entry - find wear-leveling entry closest to certain erase counter.
 * @ubi: UBI device description object
 * @root: the RB-tree where to look for
 * @diff: maximum possible difference from the smallest erase counter
 *
 * This function looks for a wear leveling entry with erase counter closest to
 * min + @diff, where min is the smallest erase counter.
 */
static struct ubi_wl_entry *find_wl_entry(struct ubi_device *ubi,
					  struct rb_root *root, int diff)
{
	struct rb_node *p;
	struct ubi_wl_entry *e, *prev_e = NULL;
	int max;

	e = rb_entry(rb_first(root), struct ubi_wl_entry, u.rb);
	max = e->ec + diff;

	p = root->rb_node;
	while (p) {
		struct ubi_wl_entry *e1;

		e1 = rb_entry(p, struct ubi_wl_entry, u.rb);
		if (e1->ec >= max)
			p = p->rb_left;
		else {
			p = p->rb_right;
			prev_e = e;
			e = e1;
		}
	}

	/* If no fastmap has been written and this WL entry can be used
	 * as anchor PEB, hold it back and return the second best WL entry
	 * such that fastmap can use the anchor PEB later. */
	if (prev_e && !ubi->fm_disabled &&
	    !ubi->fm && e->pnum < UBI_FM_MAX_START)
		return prev_e;

	return e;
}

/**
 * find_mean_wl_entry - find wear-leveling entry with medium erase counter.
 * @ubi: UBI device description object
 * @root: the RB-tree where to look for
 *
 * This function looks for a wear leveling entry with medium erase counter,
 * but not greater or equivalent than the lowest erase counter plus
 * %WL_FREE_MAX_DIFF/2.
 */
static struct ubi_wl_entry *find_mean_wl_entry(struct ubi_device *ubi,
					       struct rb_root *root)
{
	struct ubi_wl_entry *e, *first, *last;

	first = rb_entry(rb_first(root), struct ubi_wl_entry, u.rb);
	last = rb_entry(rb_last(root), struct ubi_wl_entry, u.rb);

	if (last->ec - first->ec < WL_FREE_MAX_DIFF) {
		e = rb_entry(root->rb_node, struct ubi_wl_entry, u.rb);

		/* If no fastmap has been written and this WL entry can be used
		 * as anchor PEB, hold it back and return the second best
		 * WL entry such that fastmap can use the anchor PEB later. */
		e = may_reserve_for_fm(ubi, e, root);
	} else
		e = find_wl_entry(ubi, root, WL_FREE_MAX_DIFF/2);

	return e;
}

/**
 * wl_get_wle - get a mean wl entry to be used by ubi_wl_get_peb() or
 * refill_wl_user_pool().
 * @ubi: UBI device description object
 *
 * This function returns a a wear leveling entry in case of success and
 * NULL in case of failure.
 */
static struct ubi_wl_entry *wl_get_wle(struct ubi_device *ubi)
{
	struct ubi_wl_entry *e;

	e = find_mean_wl_entry(ubi, &ubi->free);
	if (!e) {
		ubi_err(ubi, "no free eraseblocks");
		return NULL;
	}

	self_check_in_wl_tree(ubi, e, &ubi->free);

	/*
	 * Move the physical eraseblock to the protection queue where it will
	 * be protected from being moved for some time.
	 */
	rb_erase(&e->u.rb, &ubi->free);
	ubi->free_count--;
	dbg_wl("PEB %d EC %d", e->pnum, e->ec);

	return e;
}

/**
 * prot_queue_del - remove a physical eraseblock from the protection queue.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock to remove
 *
 * This function deletes PEB @pnum from the protection queue and returns zero
 * in case of success and %-ENODEV if the PEB was not found.
 */
static int prot_queue_del(struct ubi_device *ubi, int pnum)
{
	struct ubi_wl_entry *e;

	e = ubi->lookuptbl[pnum];
	if (!e)
		return -ENODEV;

	if (self_check_in_pq(ubi, e))
		return -ENODEV;

	list_del(&e->u.list);
	dbg_wl("deleted PEB %d from the protection queue", e->pnum);
	return 0;
}

/**
 * sync_erase - synchronously erase a physical eraseblock.
 * @ubi: UBI device description object
 * @e: the the physical eraseblock to erase
 * @torture: if the physical eraseblock has to be tortured
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int sync_erase(struct ubi_device *ubi, struct ubi_wl_entry *e,
		      int torture)
{
	int err;
	struct ubi_ec_hdr *ec_hdr;
	unsigned long long ec = e->ec;

	dbg_wl("erase PEB %d, old EC %llu", e->pnum, ec);

	err = self_check_ec(ubi, e->pnum, e->ec);
	if (err)
		return -EINVAL;

	ec_hdr = kzalloc(ubi->ec_hdr_alsize, GFP_NOFS);
	if (!ec_hdr)
		return -ENOMEM;

	err = ubi_io_sync_erase(ubi, e->pnum, torture);
	if (err < 0)
		goto out_free;

	ec += err;
	if (ec > UBI_MAX_ERASECOUNTER) {
		/*
		 * Erase counter overflow. Upgrade UBI and use 64-bit
		 * erase counters internally.
		 */
		ubi_err(ubi, "erase counter overflow at PEB %d, EC %llu",
			e->pnum, ec);
		err = -EINVAL;
		goto out_free;
	}

	dbg_wl("erased PEB %d, new EC %llu", e->pnum, ec);

	ec_hdr->ec = cpu_to_be64(ec);

	err = ubi_io_write_ec_hdr(ubi, e->pnum, ec_hdr);
	if (err)
		goto out_free;

	e->ec = ec;
	spin_lock(&ubi->wl_lock);
	if (e->ec > ubi->max_ec)
		ubi->max_ec = e->ec;
	spin_unlock(&ubi->wl_lock);

out_free:
	kfree(ec_hdr);
	return err;
}

/**
 * serve_prot_queue - check if it is time to stop protecting PEBs.
 * @ubi: UBI device description object
 *
 * This function is called after each erase operation and removes PEBs from the
 * tail of the protection queue. These PEBs have been protected for long enough
 * and should be moved to the used tree.
 */
static void serve_prot_queue(struct ubi_device *ubi)
{
	struct ubi_wl_entry *e, *tmp;
	int count;

	/*
	 * There may be several protected physical eraseblock to remove,
	 * process them all.
	 */
repeat:
	count = 0;
	spin_lock(&ubi->wl_lock);
	list_for_each_entry_safe(e, tmp, &ubi->pq[ubi->pq_head], u.list) {
		dbg_wl("PEB %d EC %d protection over, move to used tree",
			e->pnum, e->ec);

		list_del(&e->u.list);
		wl_tree_add(e, &ubi->used);
		if (count++ > 32) {
			/*
			 * Let's be nice and avoid holding the spinlock for
			 * too long.
			 */
			spin_unlock(&ubi->wl_lock);
			cond_resched();
			goto repeat;
		}
	}

	ubi->pq_head += 1;
	if (ubi->pq_head == UBI_PROT_QUEUE_LEN)
		ubi->pq_head = 0;
	ubi_assert(ubi->pq_head >= 0 && ubi->pq_head < UBI_PROT_QUEUE_LEN);
	spin_unlock(&ubi->wl_lock);
}

/**
 * __schedule_ubi_work - schedule a work.
 * @ubi: UBI device description object
 * @wrk: the work to schedule
 *
 * This function adds a work defined by @wrk to the tail of the pending works
 * list. Can only be used if ubi->work_sem is already held in read mode!
 */
static void __schedule_ubi_work(struct ubi_device *ubi, struct ubi_work *wrk)
{
	spin_lock(&ubi->wl_lock);
	list_add_tail(&wrk->list, &ubi->works);
	ubi_assert(ubi->works_count >= 0);
	ubi->works_count += 1;
	if (ubi->thread_enabled && !ubi_dbg_is_bgt_disabled(ubi))
		wake_up_process(ubi->bgt_thread);
	spin_unlock(&ubi->wl_lock);
}

/**
 * schedule_ubi_work - schedule a work.
 * @ubi: UBI device description object
 * @wrk: the work to schedule
 *
 * This function adds a work defined by @wrk to the tail of the pending works
 * list.
 */
static void schedule_ubi_work(struct ubi_device *ubi, struct ubi_work *wrk)
{
	down_read(&ubi->work_sem);
	__schedule_ubi_work(ubi, wrk);
	up_read(&ubi->work_sem);
}

static int erase_worker(struct ubi_device *ubi, struct ubi_work *wl_wrk,
			int shutdown);

/**
 * schedule_erase - schedule an erase work.
 * @ubi: UBI device description object
 * @e: the WL entry of the physical eraseblock to erase
 * @vol_id: the volume ID that last used this PEB
 * @lnum: the last used logical eraseblock number for the PEB
 * @torture: if the physical eraseblock has to be tortured
 *
 * This function returns zero in case of success and a %-ENOMEM in case of
 * failure.
 */
static int schedule_erase(struct ubi_device *ubi, struct ubi_wl_entry *e,
			  int vol_id, int lnum, int torture)
{
	struct ubi_work *wl_wrk;

	ubi_assert(e);

	dbg_wl("schedule erasure of PEB %d, EC %d, torture %d",
	       e->pnum, e->ec, torture);

	wl_wrk = kmalloc(sizeof(struct ubi_work), GFP_NOFS);
	if (!wl_wrk)
		return -ENOMEM;

	wl_wrk->func = &erase_worker;
	wl_wrk->e = e;
	wl_wrk->vol_id = vol_id;
	wl_wrk->lnum = lnum;
	wl_wrk->torture = torture;

	schedule_ubi_work(ubi, wl_wrk);
	return 0;
}

static int __erase_worker(struct ubi_device *ubi, struct ubi_work *wl_wrk);
/**
 * do_sync_erase - run the erase worker synchronously.
 * @ubi: UBI device description object
 * @e: the WL entry of the physical eraseblock to erase
 * @vol_id: the volume ID that last used this PEB
 * @lnum: the last used logical eraseblock number for the PEB
 * @torture: if the physical eraseblock has to be tortured
 *
 */
static int do_sync_erase(struct ubi_device *ubi, struct ubi_wl_entry *e,
			 int vol_id, int lnum, int torture)
{
	struct ubi_work wl_wrk;

	dbg_wl("sync erase of PEB %i", e->pnum);

	wl_wrk.e = e;
	wl_wrk.vol_id = vol_id;
	wl_wrk.lnum = lnum;
	wl_wrk.torture = torture;

	return __erase_worker(ubi, &wl_wrk);
}

static int ensure_wear_leveling(struct ubi_device *ubi, int nested);
/**
 * wear_leveling_worker - wear-leveling worker function.
 * @ubi: UBI device description object
 * @wrk: the work object
 * @shutdown: non-zero if the worker has to free memory and exit
 * because the WL-subsystem is shutting down
 *
 * This function copies a more worn out physical eraseblock to a less worn out
 * one. Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int wear_leveling_worker(struct ubi_device *ubi, struct ubi_work *wrk,
				int shutdown)
{
	int err, scrubbing = 0, torture = 0, protect = 0, erroneous = 0;
	int vol_id = -1, lnum = -1;
#ifdef CONFIG_MTD_UBI_FASTMAP
	int anchor = wrk->anchor;
#endif
	struct ubi_wl_entry *e1, *e2;
	struct ubi_vid_hdr *vid_hdr;
	int dst_leb_clean = 0;

	kfree(wrk);
	if (shutdown)
		return 0;

	vid_hdr = ubi_zalloc_vid_hdr(ubi, GFP_NOFS);
	if (!vid_hdr)
		return -ENOMEM;

	mutex_lock(&ubi->move_mutex);
	spin_lock(&ubi->wl_lock);
	ubi_assert(!ubi->move_from && !ubi->move_to);
	ubi_assert(!ubi->move_to_put);

	if (!ubi->free.rb_node ||
	    (!ubi->used.rb_node && !ubi->scrub.rb_node)) {
		/*
		 * No free physical eraseblocks? Well, they must be waiting in
		 * the queue to be erased. Cancel movement - it will be
		 * triggered again when a free physical eraseblock appears.
		 *
		 * No used physical eraseblocks? They must be temporarily
		 * protected from being moved. They will be moved to the
		 * @ubi->used tree later and the wear-leveling will be
		 * triggered again.
		 */
		dbg_wl("cancel WL, a list is empty: free %d, used %d",
		       !ubi->free.rb_node, !ubi->used.rb_node);
		goto out_cancel;
	}

#ifdef CONFIG_MTD_UBI_FASTMAP
	/* Check whether we need to produce an anchor PEB */
	if (!anchor)
		anchor = !anchor_pebs_avalible(&ubi->free);

	if (anchor) {
		e1 = find_anchor_wl_entry(&ubi->used);
		if (!e1)
			goto out_cancel;
		e2 = get_peb_for_wl(ubi);
		if (!e2)
			goto out_cancel;

		self_check_in_wl_tree(ubi, e1, &ubi->used);
		rb_erase(&e1->u.rb, &ubi->used);
		dbg_wl("anchor-move PEB %d to PEB %d", e1->pnum, e2->pnum);
	} else if (!ubi->scrub.rb_node) {
#else
	if (!ubi->scrub.rb_node) {
#endif
		/*
		 * Now pick the least worn-out used physical eraseblock and a
		 * highly worn-out free physical eraseblock. If the erase
		 * counters differ much enough, start wear-leveling.
		 */
		e1 = rb_entry(rb_first(&ubi->used), struct ubi_wl_entry, u.rb);
		e2 = get_peb_for_wl(ubi);
		if (!e2)
			goto out_cancel;

		if (!(e2->ec - e1->ec >= UBI_WL_THRESHOLD)) {
			dbg_wl("no WL needed: min used EC %d, max free EC %d",
			       e1->ec, e2->ec);

			/* Give the unused PEB back */
			wl_tree_add(e2, &ubi->free);
			ubi->free_count++;
			goto out_cancel;
		}
		self_check_in_wl_tree(ubi, e1, &ubi->used);
		rb_erase(&e1->u.rb, &ubi->used);
		dbg_wl("move PEB %d EC %d to PEB %d EC %d",
		       e1->pnum, e1->ec, e2->pnum, e2->ec);
	} else {
		/* Perform scrubbing */
		scrubbing = 1;
		e1 = rb_entry(rb_first(&ubi->scrub), struct ubi_wl_entry, u.rb);
		e2 = get_peb_for_wl(ubi);
		if (!e2)
			goto out_cancel;

		self_check_in_wl_tree(ubi, e1, &ubi->scrub);
		rb_erase(&e1->u.rb, &ubi->scrub);
		dbg_wl("scrub PEB %d to PEB %d", e1->pnum, e2->pnum);
	}

	ubi->move_from = e1;
	ubi->move_to = e2;
	spin_unlock(&ubi->wl_lock);

	/*
	 * Now we are going to copy physical eraseblock @e1->pnum to @e2->pnum.
	 * We so far do not know which logical eraseblock our physical
	 * eraseblock (@e1) belongs to. We have to read the volume identifier
	 * header first.
	 *
	 * Note, we are protected from this PEB being unmapped and erased. The
	 * 'ubi_wl_put_peb()' would wait for moving to be finished if the PEB
	 * which is being moved was unmapped.
	 */

	err = ubi_io_read_vid_hdr(ubi, e1->pnum, vid_hdr, 0);
	if (err && err != UBI_IO_BITFLIPS) {
		dst_leb_clean = 1;
		if (err == UBI_IO_FF) {
			/*
			 * We are trying to move PEB without a VID header. UBI
			 * always write VID headers shortly after the PEB was
			 * given, so we have a situation when it has not yet
			 * had a chance to write it, because it was preempted.
			 * So add this PEB to the protection queue so far,
			 * because presumably more data will be written there
			 * (including the missing VID header), and then we'll
			 * move it.
			 */
			dbg_wl("PEB %d has no VID header", e1->pnum);
			protect = 1;
			goto out_not_moved;
		} else if (err == UBI_IO_FF_BITFLIPS) {
			/*
			 * The same situation as %UBI_IO_FF, but bit-flips were
			 * detected. It is better to schedule this PEB for
			 * scrubbing.
			 */
			dbg_wl("PEB %d has no VID header but has bit-flips",
			       e1->pnum);
			scrubbing = 1;
			goto out_not_moved;
		}

		ubi_err(ubi, "error %d while reading VID header from PEB %d",
			err, e1->pnum);
		goto out_error;
	}

	vol_id = be32_to_cpu(vid_hdr->vol_id);
	lnum = be32_to_cpu(vid_hdr->lnum);

	err = ubi_eba_copy_leb(ubi, e1->pnum, e2->pnum, vid_hdr);
	if (err) {
		if (err == MOVE_CANCEL_RACE) {
			/*
			 * The LEB has not been moved because the volume is
			 * being deleted or the PEB has been put meanwhile. We
			 * should prevent this PEB from being selected for
			 * wear-leveling movement again, so put it to the
			 * protection queue.
			 */
			protect = 1;
			dst_leb_clean = 1;
			goto out_not_moved;
		}
		if (err == MOVE_RETRY) {
			scrubbing = 1;
			dst_leb_clean = 1;
			goto out_not_moved;
		}
		if (err == MOVE_TARGET_BITFLIPS || err == MOVE_TARGET_WR_ERR ||
		    err == MOVE_TARGET_RD_ERR) {
			/*
			 * Target PEB had bit-flips or write error - torture it.
			 */
			torture = 1;
			goto out_not_moved;
		}

		if (err == MOVE_SOURCE_RD_ERR) {
			/*
			 * An error happened while reading the source PEB. Do
			 * not switch to R/O mode in this case, and give the
			 * upper layers a possibility to recover from this,
			 * e.g. by unmapping corresponding LEB. Instead, just
			 * put this PEB to the @ubi->erroneous list to prevent
			 * UBI from trying to move it over and over again.
			 */
			if (ubi->erroneous_peb_count > ubi->max_erroneous) {
				ubi_err(ubi, "too many erroneous eraseblocks (%d)",
					ubi->erroneous_peb_count);
				goto out_error;
			}
			dst_leb_clean = 1;
			erroneous = 1;
			goto out_not_moved;
		}

		if (err < 0)
			goto out_error;

		ubi_assert(0);
	}

	/* The PEB has been successfully moved */
	if (scrubbing)
		ubi_msg(ubi, "scrubbed PEB %d (LEB %d:%d), data moved to PEB %d",
			e1->pnum, vol_id, lnum, e2->pnum);
	ubi_free_vid_hdr(ubi, vid_hdr);

	spin_lock(&ubi->wl_lock);
	if (!ubi->move_to_put) {
		wl_tree_add(e2, &ubi->used);
		e2 = NULL;
	}
	ubi->move_from = ubi->move_to = NULL;
	ubi->move_to_put = ubi->wl_scheduled = 0;
	spin_unlock(&ubi->wl_lock);

	err = do_sync_erase(ubi, e1, vol_id, lnum, 0);
	if (err) {
		if (e2)
			wl_entry_destroy(ubi, e2);
		goto out_ro;
	}

	if (e2) {
		/*
		 * Well, the target PEB was put meanwhile, schedule it for
		 * erasure.
		 */
		dbg_wl("PEB %d (LEB %d:%d) was put meanwhile, erase",
		       e2->pnum, vol_id, lnum);
		err = do_sync_erase(ubi, e2, vol_id, lnum, 0);
		if (err)
			goto out_ro;
	}

	dbg_wl("done");
	mutex_unlock(&ubi->move_mutex);
	return 0;

	/*
	 * For some reasons the LEB was not moved, might be an error, might be
	 * something else. @e1 was not changed, so return it back. @e2 might
	 * have been changed, schedule it for erasure.
	 */
out_not_moved:
	if (vol_id != -1)
		dbg_wl("cancel moving PEB %d (LEB %d:%d) to PEB %d (%d)",
		       e1->pnum, vol_id, lnum, e2->pnum, err);
	else
		dbg_wl("cancel moving PEB %d to PEB %d (%d)",
		       e1->pnum, e2->pnum, err);
	spin_lock(&ubi->wl_lock);
	if (protect)
		prot_queue_add(ubi, e1);
	else if (erroneous) {
		wl_tree_add(e1, &ubi->erroneous);
		ubi->erroneous_peb_count += 1;
	} else if (scrubbing)
		wl_tree_add(e1, &ubi->scrub);
	else
		wl_tree_add(e1, &ubi->used);
	if (dst_leb_clean) {
		wl_tree_add(e2, &ubi->free);
		ubi->free_count++;
	}

	ubi_assert(!ubi->move_to_put);
	ubi->move_from = ubi->move_to = NULL;
	ubi->wl_scheduled = 0;
	spin_unlock(&ubi->wl_lock);

	ubi_free_vid_hdr(ubi, vid_hdr);
	if (dst_leb_clean) {
		ensure_wear_leveling(ubi, 1);
	} else {
		err = do_sync_erase(ubi, e2, vol_id, lnum, torture);
		if (err)
			goto out_ro;
	}

	mutex_unlock(&ubi->move_mutex);
	return 0;

out_error:
	if (vol_id != -1)
		ubi_err(ubi, "error %d while moving PEB %d to PEB %d",
			err, e1->pnum, e2->pnum);
	else
		ubi_err(ubi, "error %d while moving PEB %d (LEB %d:%d) to PEB %d",
			err, e1->pnum, vol_id, lnum, e2->pnum);
	spin_lock(&ubi->wl_lock);
	ubi->move_from = ubi->move_to = NULL;
	ubi->move_to_put = ubi->wl_scheduled = 0;
	spin_unlock(&ubi->wl_lock);

	ubi_free_vid_hdr(ubi, vid_hdr);
	wl_entry_destroy(ubi, e1);
	wl_entry_destroy(ubi, e2);

out_ro:
	ubi_ro_mode(ubi);
	mutex_unlock(&ubi->move_mutex);
	ubi_assert(err != 0);
	return err < 0 ? err : -EIO;

out_cancel:
	ubi->wl_scheduled = 0;
	spin_unlock(&ubi->wl_lock);
	mutex_unlock(&ubi->move_mutex);
	ubi_free_vid_hdr(ubi, vid_hdr);
	return 0;
}

/**
 * ensure_wear_leveling - schedule wear-leveling if it is needed.
 * @ubi: UBI device description object
 * @nested: set to non-zero if this function is called from UBI worker
 *
 * This function checks if it is time to start wear-leveling and schedules it
 * if yes. This function returns zero in case of success and a negative error
 * code in case of failure.
 */
static int ensure_wear_leveling(struct ubi_device *ubi, int nested)
{
	int err = 0;
	struct ubi_wl_entry *e1;
	struct ubi_wl_entry *e2;
	struct ubi_work *wrk;

	spin_lock(&ubi->wl_lock);
	if (ubi->wl_scheduled)
		/* Wear-leveling is already in the work queue */
		goto out_unlock;

	/*
	 * If the ubi->scrub tree is not empty, scrubbing is needed, and the
	 * the WL worker has to be scheduled anyway.
	 */
	if (!ubi->scrub.rb_node) {
		if (!ubi->used.rb_node || !ubi->free.rb_node)
			/* No physical eraseblocks - no deal */
			goto out_unlock;

		/*
		 * We schedule wear-leveling only if the difference between the
		 * lowest erase counter of used physical eraseblocks and a high
		 * erase counter of free physical eraseblocks is greater than
		 * %UBI_WL_THRESHOLD.
		 */
		e1 = rb_entry(rb_first(&ubi->used), struct ubi_wl_entry, u.rb);
		e2 = find_wl_entry(ubi, &ubi->free, WL_FREE_MAX_DIFF);

		if (!(e2->ec - e1->ec >= UBI_WL_THRESHOLD))
			goto out_unlock;
		dbg_wl("schedule wear-leveling");
	} else
		dbg_wl("schedule scrubbing");

	ubi->wl_scheduled = 1;
	spin_unlock(&ubi->wl_lock);

	wrk = kmalloc(sizeof(struct ubi_work), GFP_NOFS);
	if (!wrk) {
		err = -ENOMEM;
		goto out_cancel;
	}

	wrk->anchor = 0;
	wrk->func = &wear_leveling_worker;
	if (nested)
		__schedule_ubi_work(ubi, wrk);
	else
		schedule_ubi_work(ubi, wrk);
	return err;

out_cancel:
	spin_lock(&ubi->wl_lock);
	ubi->wl_scheduled = 0;
out_unlock:
	spin_unlock(&ubi->wl_lock);
	return err;
}

/**
 * __erase_worker - physical eraseblock erase worker function.
 * @ubi: UBI device description object
 * @wl_wrk: the work object
 * @shutdown: non-zero if the worker has to free memory and exit
 * because the WL sub-system is shutting down
 *
 * This function erases a physical eraseblock and perform torture testing if
 * needed. It also takes care about marking the physical eraseblock bad if
 * needed. Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int __erase_worker(struct ubi_device *ubi, struct ubi_work *wl_wrk)
{
	struct ubi_wl_entry *e = wl_wrk->e;
	int pnum = e->pnum;
	int vol_id = wl_wrk->vol_id;
	int lnum = wl_wrk->lnum;
	int err, available_consumed = 0;

	dbg_wl("erase PEB %d EC %d LEB %d:%d",
	       pnum, e->ec, wl_wrk->vol_id, wl_wrk->lnum);

	err = sync_erase(ubi, e, wl_wrk->torture);
	if (!err) {
		spin_lock(&ubi->wl_lock);
		wl_tree_add(e, &ubi->free);
		ubi->free_count++;
		spin_unlock(&ubi->wl_lock);

		/*
		 * One more erase operation has happened, take care about
		 * protected physical eraseblocks.
		 */
		serve_prot_queue(ubi);

		/* And take care about wear-leveling */
		err = ensure_wear_leveling(ubi, 1);
		return err;
	}

	ubi_err(ubi, "failed to erase PEB %d, error %d", pnum, err);

	if (err == -EINTR || err == -ENOMEM || err == -EAGAIN ||
	    err == -EBUSY) {
		int err1;

		/* Re-schedule the LEB for erasure */
		err1 = schedule_erase(ubi, e, vol_id, lnum, 0);
		if (err1) {
			wl_entry_destroy(ubi, e);
			err = err1;
			goto out_ro;
		}
		return err;
	}

	wl_entry_destroy(ubi, e);
	if (err != -EIO)
		/*
		 * If this is not %-EIO, we have no idea what to do. Scheduling
		 * this physical eraseblock for erasure again would cause
		 * errors again and again. Well, lets switch to R/O mode.
		 */
		goto out_ro;

	/* It is %-EIO, the PEB went bad */

	if (!ubi->bad_allowed) {
		ubi_err(ubi, "bad physical eraseblock %d detected", pnum);
		goto out_ro;
	}

	spin_lock(&ubi->volumes_lock);
	if (ubi->beb_rsvd_pebs == 0) {
		if (ubi->avail_pebs == 0) {
			spin_unlock(&ubi->volumes_lock);
			ubi_err(ubi, "no reserved/available physical eraseblocks");
			goto out_ro;
		}
		ubi->avail_pebs -= 1;
		available_consumed = 1;
	}
	spin_unlock(&ubi->volumes_lock);

	ubi_msg(ubi, "mark PEB %d as bad", pnum);
	err = ubi_io_mark_bad(ubi, pnum);
	if (err)
		goto out_ro;

	spin_lock(&ubi->volumes_lock);
	if (ubi->beb_rsvd_pebs > 0) {
		if (available_consumed) {
			/*
			 * The amount of reserved PEBs increased since we last
			 * checked.
			 */
			ubi->avail_pebs += 1;
			available_consumed = 0;
		}
		ubi->beb_rsvd_pebs -= 1;
	}
	ubi->bad_peb_count += 1;
	ubi->good_peb_count -= 1;
	ubi_calculate_reserved(ubi);
	if (available_consumed)
		ubi_warn(ubi, "no PEBs in the reserved pool, used an available PEB");
	else if (ubi->beb_rsvd_pebs)
		ubi_msg(ubi, "%d PEBs left in the reserve",
			ubi->beb_rsvd_pebs);
	else
		ubi_warn(ubi, "last PEB from the reserve was used");
	spin_unlock(&ubi->volumes_lock);

	return err;

out_ro:
	if (available_consumed) {
		spin_lock(&ubi->volumes_lock);
		ubi->avail_pebs += 1;
		spin_unlock(&ubi->volumes_lock);
	}
	ubi_ro_mode(ubi);
	return err;
}

static int erase_worker(struct ubi_device *ubi, struct ubi_work *wl_wrk,
			  int shutdown)
{
	int ret;

	if (shutdown) {
		struct ubi_wl_entry *e = wl_wrk->e;

		dbg_wl("cancel erasure of PEB %d EC %d", e->pnum, e->ec);
		kfree(wl_wrk);
		wl_entry_destroy(ubi, e);
		return 0;
	}

	ret = __erase_worker(ubi, wl_wrk);
	kfree(wl_wrk);
	return ret;
}

/**
 * ubi_wl_put_peb - return a PEB to the wear-leveling sub-system.
 * @ubi: UBI device description object
 * @vol_id: the volume ID that last used this PEB
 * @lnum: the last used logical eraseblock number for the PEB
 * @pnum: physical eraseblock to return
 * @torture: if this physical eraseblock has to be tortured
 *
 * This function is called to return physical eraseblock @pnum to the pool of
 * free physical eraseblocks. The @torture flag has to be set if an I/O error
 * occurred to this @pnum and it has to be tested. This function returns zero
 * in case of success, and a negative error code in case of failure.
 */
int ubi_wl_put_peb(struct ubi_device *ubi, int vol_id, int lnum,
		   int pnum, int torture)
{
	int err;
	struct ubi_wl_entry *e;

	dbg_wl("PEB %d", pnum);
	ubi_assert(pnum >= 0);
	ubi_assert(pnum < ubi->peb_count);

	down_read(&ubi->fm_protect);

retry:
	spin_lock(&ubi->wl_lock);
	e = ubi->lookuptbl[pnum];
	if (e == ubi->move_from) {
		/*
		 * User is putting the physical eraseblock which was selected to
		 * be moved. It will be scheduled for erasure in the
		 * wear-leveling worker.
		 */
		dbg_wl("PEB %d is being moved, wait", pnum);
		spin_unlock(&ubi->wl_lock);

		/* Wait for the WL worker by taking the @ubi->move_mutex */
		mutex_lock(&ubi->move_mutex);
		mutex_unlock(&ubi->move_mutex);
		goto retry;
	} else if (e == ubi->move_to) {
		/*
		 * User is putting the physical eraseblock which was selected
		 * as the target the data is moved to. It may happen if the EBA
		 * sub-system already re-mapped the LEB in 'ubi_eba_copy_leb()'
		 * but the WL sub-system has not put the PEB to the "used" tree
		 * yet, but it is about to do this. So we just set a flag which
		 * will tell the WL worker that the PEB is not needed anymore
		 * and should be scheduled for erasure.
		 */
		dbg_wl("PEB %d is the target of data moving", pnum);
		ubi_assert(!ubi->move_to_put);
		ubi->move_to_put = 1;
		spin_unlock(&ubi->wl_lock);
		up_read(&ubi->fm_protect);
		return 0;
	} else {
		if (in_wl_tree(e, &ubi->used)) {
			self_check_in_wl_tree(ubi, e, &ubi->used);
			rb_erase(&e->u.rb, &ubi->used);
		} else if (in_wl_tree(e, &ubi->scrub)) {
			self_check_in_wl_tree(ubi, e, &ubi->scrub);
			rb_erase(&e->u.rb, &ubi->scrub);
		} else if (in_wl_tree(e, &ubi->erroneous)) {
			self_check_in_wl_tree(ubi, e, &ubi->erroneous);
			rb_erase(&e->u.rb, &ubi->erroneous);
			ubi->erroneous_peb_count -= 1;
			ubi_assert(ubi->erroneous_peb_count >= 0);
			/* Erroneous PEBs should be tortured */
			torture = 1;
		} else {
			err = prot_queue_del(ubi, e->pnum);
			if (err) {
				ubi_err(ubi, "PEB %d not found", pnum);
				ubi_ro_mode(ubi);
				spin_unlock(&ubi->wl_lock);
				up_read(&ubi->fm_protect);
				return err;
			}
		}
	}
	spin_unlock(&ubi->wl_lock);

	err = schedule_erase(ubi, e, vol_id, lnum, torture);
	if (err) {
		spin_lock(&ubi->wl_lock);
		wl_tree_add(e, &ubi->used);
		spin_unlock(&ubi->wl_lock);
	}

	up_read(&ubi->fm_protect);
	return err;
}

/**
 * ubi_wl_scrub_peb - schedule a physical eraseblock for scrubbing.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock to schedule
 *
 * If a bit-flip in a physical eraseblock is detected, this physical eraseblock
 * needs scrubbing. This function schedules a physical eraseblock for
 * scrubbing which is done in background. This function returns zero in case of
 * success and a negative error code in case of failure.
 */
int ubi_wl_scrub_peb(struct ubi_device *ubi, int pnum)
{
	struct ubi_wl_entry *e;

	ubi_msg(ubi, "schedule PEB %d for scrubbing", pnum);

retry:
	spin_lock(&ubi->wl_lock);
	e = ubi->lookuptbl[pnum];
	if (e == ubi->move_from || in_wl_tree(e, &ubi->scrub) ||
				   in_wl_tree(e, &ubi->erroneous)) {
		spin_unlock(&ubi->wl_lock);
		return 0;
	}

	if (e == ubi->move_to) {
		/*
		 * This physical eraseblock was used to move data to. The data
		 * was moved but the PEB was not yet inserted to the proper
		 * tree. We should just wait a little and let the WL worker
		 * proceed.
		 */
		spin_unlock(&ubi->wl_lock);
		dbg_wl("the PEB %d is not in proper tree, retry", pnum);
		yield();
		goto retry;
	}

	if (in_wl_tree(e, &ubi->used)) {
		self_check_in_wl_tree(ubi, e, &ubi->used);
		rb_erase(&e->u.rb, &ubi->used);
	} else {
		int err;

		err = prot_queue_del(ubi, e->pnum);
		if (err) {
			ubi_err(ubi, "PEB %d not found", pnum);
			ubi_ro_mode(ubi);
			spin_unlock(&ubi->wl_lock);
			return err;
		}
	}

	wl_tree_add(e, &ubi->scrub);
	spin_unlock(&ubi->wl_lock);

	/*
	 * Technically scrubbing is the same as wear-leveling, so it is done
	 * by the WL worker.
	 */
	return ensure_wear_leveling(ubi, 0);
}

/**
 * ubi_wl_flush - flush all pending works.
 * @ubi: UBI device description object
 * @vol_id: the volume id to flush for
 * @lnum: the logical eraseblock number to flush for
 *
 * This function executes all pending works for a particular volume id /
 * logical eraseblock number pair. If either value is set to %UBI_ALL, then it
 * acts as a wildcard for all of the corresponding volume numbers or logical
 * eraseblock numbers. It returns zero in case of success and a negative error
 * code in case of failure.
 */
int ubi_wl_flush(struct ubi_device *ubi, int vol_id, int lnum)
{
	int err = 0;
	int found = 1;

	/*
	 * Erase while the pending works queue is not empty, but not more than
	 * the number of currently pending works.
	 */
	dbg_wl("flush pending work for LEB %d:%d (%d pending works)",
	       vol_id, lnum, ubi->works_count);

	while (found) {
		struct ubi_work *wrk, *tmp;
		found = 0;

		down_read(&ubi->work_sem);
		spin_lock(&ubi->wl_lock);
		list_for_each_entry_safe(wrk, tmp, &ubi->works, list) {
			if ((vol_id == UBI_ALL || wrk->vol_id == vol_id) &&
			    (lnum == UBI_ALL || wrk->lnum == lnum)) {
				list_del(&wrk->list);
				ubi->works_count -= 1;
				ubi_assert(ubi->works_count >= 0);
				spin_unlock(&ubi->wl_lock);

				err = wrk->func(ubi, wrk, 0);
				if (err) {
					up_read(&ubi->work_sem);
					return err;
				}

				spin_lock(&ubi->wl_lock);
				found = 1;
				break;
			}
		}
		spin_unlock(&ubi->wl_lock);
		up_read(&ubi->work_sem);
	}

	/*
	 * Make sure all the works which have been done in parallel are
	 * finished.
	 */
	down_write(&ubi->work_sem);
	up_write(&ubi->work_sem);

	return err;
}

/**
 * tree_destroy - destroy an RB-tree.
 * @ubi: UBI device description object
 * @root: the root of the tree to destroy
 */
static void tree_destroy(struct ubi_device *ubi, struct rb_root *root)
{
	struct rb_node *rb;
	struct ubi_wl_entry *e;

	rb = root->rb_node;
	while (rb) {
		if (rb->rb_left)
			rb = rb->rb_left;
		else if (rb->rb_right)
			rb = rb->rb_right;
		else {
			e = rb_entry(rb, struct ubi_wl_entry, u.rb);

			rb = rb_parent(rb);
			if (rb) {
				if (rb->rb_left == &e->u.rb)
					rb->rb_left = NULL;
				else
					rb->rb_right = NULL;
			}

			wl_entry_destroy(ubi, e);
		}
	}
}

/**
 * ubi_thread - UBI background thread.
 * @u: the UBI device description object pointer
 */
int ubi_thread(void *u)
{
	int failures = 0;
	struct ubi_device *ubi = u;

	ubi_msg(ubi, "background thread \"%s\" started, PID %d",
		ubi->bgt_name, task_pid_nr(current));

	set_freezable();
	for (;;) {
		int err;

		if (kthread_should_stop())
			break;

		if (try_to_freeze())
			continue;

		spin_lock(&ubi->wl_lock);
		if (list_empty(&ubi->works) || ubi->ro_mode ||
		    !ubi->thread_enabled || ubi_dbg_is_bgt_disabled(ubi)) {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock(&ubi->wl_lock);
			schedule();
			continue;
		}
		spin_unlock(&ubi->wl_lock);

		err = do_work(ubi);
		if (err) {
			ubi_err(ubi, "%s: work failed with error code %d",
				ubi->bgt_name, err);
			if (failures++ > WL_MAX_FAILURES) {
				/*
				 * Too many failures, disable the thread and
				 * switch to read-only mode.
				 */
				ubi_msg(ubi, "%s: %d consecutive failures",
					ubi->bgt_name, WL_MAX_FAILURES);
				ubi_ro_mode(ubi);
				ubi->thread_enabled = 0;
				continue;
			}
		} else
			failures = 0;

		cond_resched();
	}

	dbg_wl("background thread \"%s\" is killed", ubi->bgt_name);
	return 0;
}

/**
 * shutdown_work - shutdown all pending works.
 * @ubi: UBI device description object
 */
static void shutdown_work(struct ubi_device *ubi)
{
#ifdef CONFIG_MTD_UBI_FASTMAP
	flush_work(&ubi->fm_work);
#endif
	while (!list_empty(&ubi->works)) {
		struct ubi_work *wrk;

		wrk = list_entry(ubi->works.next, struct ubi_work, list);
		list_del(&wrk->list);
		wrk->func(ubi, wrk, 1);
		ubi->works_count -= 1;
		ubi_assert(ubi->works_count >= 0);
	}
}

/**
 * ubi_wl_init - initialize the WL sub-system using attaching information.
 * @ubi: UBI device description object
 * @ai: attaching information
 *
 * This function returns zero in case of success, and a negative error code in
 * case of failure.
 */
int ubi_wl_init(struct ubi_device *ubi, struct ubi_attach_info *ai)
{
	int err, i, reserved_pebs, found_pebs = 0;
	struct rb_node *rb1, *rb2;
	struct ubi_ainf_volume *av;
	struct ubi_ainf_peb *aeb, *tmp;
	struct ubi_wl_entry *e;

	ubi->used = ubi->erroneous = ubi->free = ubi->scrub = RB_ROOT;
	spin_lock_init(&ubi->wl_lock);
	mutex_init(&ubi->move_mutex);
	init_rwsem(&ubi->work_sem);
	ubi->max_ec = ai->max_ec;
	INIT_LIST_HEAD(&ubi->works);

	sprintf(ubi->bgt_name, UBI_BGT_NAME_PATTERN, ubi->ubi_num);

	err = -ENOMEM;
	ubi->lookuptbl = kzalloc(ubi->peb_count * sizeof(void *), GFP_KERNEL);
	if (!ubi->lookuptbl)
		return err;

	for (i = 0; i < UBI_PROT_QUEUE_LEN; i++)
		INIT_LIST_HEAD(&ubi->pq[i]);
	ubi->pq_head = 0;

	ubi->free_count = 0;
	list_for_each_entry_safe(aeb, tmp, &ai->erase, u.list) {
		cond_resched();

		e = kmem_cache_alloc(ubi_wl_entry_slab, GFP_KERNEL);
		if (!e)
			goto out_free;

		e->pnum = aeb->pnum;
		e->ec = aeb->ec;
		ubi->lookuptbl[e->pnum] = e;
		if (schedule_erase(ubi, e, aeb->vol_id, aeb->lnum, 0)) {
			wl_entry_destroy(ubi, e);
			goto out_free;
		}

		found_pebs++;
	}

	list_for_each_entry(aeb, &ai->free, u.list) {
		cond_resched();

		e = kmem_cache_alloc(ubi_wl_entry_slab, GFP_KERNEL);
		if (!e)
			goto out_free;

		e->pnum = aeb->pnum;
		e->ec = aeb->ec;
		ubi_assert(e->ec >= 0);

		wl_tree_add(e, &ubi->free);
		ubi->free_count++;

		ubi->lookuptbl[e->pnum] = e;

		found_pebs++;
	}

	ubi_rb_for_each_entry(rb1, av, &ai->volumes, rb) {
		ubi_rb_for_each_entry(rb2, aeb, &av->root, u.rb) {
			cond_resched();

			e = kmem_cache_alloc(ubi_wl_entry_slab, GFP_KERNEL);
			if (!e)
				goto out_free;

			e->pnum = aeb->pnum;
			e->ec = aeb->ec;
			ubi->lookuptbl[e->pnum] = e;

			if (!aeb->scrub) {
				dbg_wl("add PEB %d EC %d to the used tree",
				       e->pnum, e->ec);
				wl_tree_add(e, &ubi->used);
			} else {
				dbg_wl("add PEB %d EC %d to the scrub tree",
				       e->pnum, e->ec);
				wl_tree_add(e, &ubi->scrub);
			}

			found_pebs++;
		}
	}

	list_for_each_entry(aeb, &ai->fastmap, u.list) {
		cond_resched();

		e = ubi_find_fm_block(ubi, aeb->pnum);

		if (e) {
			ubi_assert(!ubi->lookuptbl[e->pnum]);
			ubi->lookuptbl[e->pnum] = e;
		} else {
			/*
			 * Usually old Fastmap PEBs are scheduled for erasure
			 * and we don't have to care about them but if we face
			 * an power cut before scheduling them we need to
			 * take care of them here.
			 */
			if (ubi->lookuptbl[aeb->pnum])
				continue;

			e = kmem_cache_alloc(ubi_wl_entry_slab, GFP_KERNEL);
			if (!e)
				goto out_free;

			e->pnum = aeb->pnum;
			e->ec = aeb->ec;
			ubi_assert(!ubi->lookuptbl[e->pnum]);
			ubi->lookuptbl[e->pnum] = e;
			if (schedule_erase(ubi, e, aeb->vol_id, aeb->lnum, 0)) {
				wl_entry_destroy(ubi, e);
				goto out_free;
			}
		}

		found_pebs++;
	}

	dbg_wl("found %i PEBs", found_pebs);

	ubi_assert(ubi->good_peb_count == found_pebs);

	reserved_pebs = WL_RESERVED_PEBS;
	ubi_fastmap_init(ubi, &reserved_pebs);

	if (ubi->avail_pebs < reserved_pebs) {
		ubi_err(ubi, "no enough physical eraseblocks (%d, need %d)",
			ubi->avail_pebs, reserved_pebs);
		if (ubi->corr_peb_count)
			ubi_err(ubi, "%d PEBs are corrupted and not used",
				ubi->corr_peb_count);
		err = -ENOSPC;
		goto out_free;
	}
	ubi->avail_pebs -= reserved_pebs;
	ubi->rsvd_pebs += reserved_pebs;

	/* Schedule wear-leveling if needed */
	err = ensure_wear_leveling(ubi, 0);
	if (err)
		goto out_free;

	return 0;

out_free:
	shutdown_work(ubi);
	tree_destroy(ubi, &ubi->used);
	tree_destroy(ubi, &ubi->free);
	tree_destroy(ubi, &ubi->scrub);
	kfree(ubi->lookuptbl);
	return err;
}

/**
 * protection_queue_destroy - destroy the protection queue.
 * @ubi: UBI device description object
 */
static void protection_queue_destroy(struct ubi_device *ubi)
{
	int i;
	struct ubi_wl_entry *e, *tmp;

	for (i = 0; i < UBI_PROT_QUEUE_LEN; ++i) {
		list_for_each_entry_safe(e, tmp, &ubi->pq[i], u.list) {
			list_del(&e->u.list);
			wl_entry_destroy(ubi, e);
		}
	}
}

/**
 * ubi_wl_close - close the wear-leveling sub-system.
 * @ubi: UBI device description object
 */
void ubi_wl_close(struct ubi_device *ubi)
{
	dbg_wl("close the WL sub-system");
	ubi_fastmap_close(ubi);
	shutdown_work(ubi);
	protection_queue_destroy(ubi);
	tree_destroy(ubi, &ubi->used);
	tree_destroy(ubi, &ubi->erroneous);
	tree_destroy(ubi, &ubi->free);
	tree_destroy(ubi, &ubi->scrub);
	kfree(ubi->lookuptbl);
}

/**
 * self_check_ec - make sure that the erase counter of a PEB is correct.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to check
 * @ec: the erase counter to check
 *
 * This function returns zero if the erase counter of physical eraseblock @pnum
 * is equivalent to @ec, and a negative error code if not or if an error
 * occurred.
 */
static int self_check_ec(struct ubi_device *ubi, int pnum, int ec)
{
	int err;
	long long read_ec;
	struct ubi_ec_hdr *ec_hdr;

	if (!ubi_dbg_chk_gen(ubi))
		return 0;

	ec_hdr = kzalloc(ubi->ec_hdr_alsize, GFP_NOFS);
	if (!ec_hdr)
		return -ENOMEM;

	err = ubi_io_read_ec_hdr(ubi, pnum, ec_hdr, 0);
	if (err && err != UBI_IO_BITFLIPS) {
		/* The header does not have to exist */
		err = 0;
		goto out_free;
	}

	read_ec = be64_to_cpu(ec_hdr->ec);
	if (ec != read_ec && read_ec - ec > 1) {
		ubi_err(ubi, "self-check failed for PEB %d", pnum);
		ubi_err(ubi, "read EC is %lld, should be %d", read_ec, ec);
		dump_stack();
		err = 1;
	} else
		err = 0;

out_free:
	kfree(ec_hdr);
	return err;
}

/**
 * self_check_in_wl_tree - check that wear-leveling entry is in WL RB-tree.
 * @ubi: UBI device description object
 * @e: the wear-leveling entry to check
 * @root: the root of the tree
 *
 * This function returns zero if @e is in the @root RB-tree and %-EINVAL if it
 * is not.
 */
static int self_check_in_wl_tree(const struct ubi_device *ubi,
				 struct ubi_wl_entry *e, struct rb_root *root)
{
	if (!ubi_dbg_chk_gen(ubi))
		return 0;

	if (in_wl_tree(e, root))
		return 0;

	ubi_err(ubi, "self-check failed for PEB %d, EC %d, RB-tree %p ",
		e->pnum, e->ec, root);
	dump_stack();
	return -EINVAL;
}

/**
 * self_check_in_pq - check if wear-leveling entry is in the protection
 *                        queue.
 * @ubi: UBI device description object
 * @e: the wear-leveling entry to check
 *
 * This function returns zero if @e is in @ubi->pq and %-EINVAL if it is not.
 */
static int self_check_in_pq(const struct ubi_device *ubi,
			    struct ubi_wl_entry *e)
{
	struct ubi_wl_entry *p;
	int i;

	if (!ubi_dbg_chk_gen(ubi))
		return 0;

	for (i = 0; i < UBI_PROT_QUEUE_LEN; ++i)
		list_for_each_entry(p, &ubi->pq[i], u.list)
			if (p == e)
				return 0;

	ubi_err(ubi, "self-check failed for PEB %d, EC %d, Protect queue",
		e->pnum, e->ec);
	dump_stack();
	return -EINVAL;
}
#ifndef CONFIG_MTD_UBI_FASTMAP
static struct ubi_wl_entry *get_peb_for_wl(struct ubi_device *ubi)
{
	struct ubi_wl_entry *e;

	e = find_wl_entry(ubi, &ubi->free, WL_FREE_MAX_DIFF);
	self_check_in_wl_tree(ubi, e, &ubi->free);
	ubi->free_count--;
	ubi_assert(ubi->free_count >= 0);
	rb_erase(&e->u.rb, &ubi->free);

	return e;
}

/**
 * produce_free_peb - produce a free physical eraseblock.
 * @ubi: UBI device description object
 *
 * This function tries to make a free PEB by means of synchronous execution of
 * pending works. This may be needed if, for example the background thread is
 * disabled. Returns zero in case of success and a negative error code in case
 * of failure.
 */
static int produce_free_peb(struct ubi_device *ubi)
{
	int err;

	while (!ubi->free.rb_node && ubi->works_count) {
		spin_unlock(&ubi->wl_lock);

		dbg_wl("do one work synchronously");
		err = do_work(ubi);

		spin_lock(&ubi->wl_lock);
		if (err)
			return err;
	}

	return 0;
}

/**
 * ubi_wl_get_peb - get a physical eraseblock.
 * @ubi: UBI device description object
 *
 * This function returns a physical eraseblock in case of success and a
 * negative error code in case of failure.
 * Returns with ubi->fm_eba_sem held in read mode!
 */
int ubi_wl_get_peb(struct ubi_device *ubi)
{
	int err;
	struct ubi_wl_entry *e;

retry:
	down_read(&ubi->fm_eba_sem);
	spin_lock(&ubi->wl_lock);
	if (!ubi->free.rb_node) {
		if (ubi->works_count == 0) {
			ubi_err(ubi, "no free eraseblocks");
			ubi_assert(list_empty(&ubi->works));
			spin_unlock(&ubi->wl_lock);
			return -ENOSPC;
		}

		err = produce_free_peb(ubi);
		if (err < 0) {
			spin_unlock(&ubi->wl_lock);
			return err;
		}
		spin_unlock(&ubi->wl_lock);
		up_read(&ubi->fm_eba_sem);
		goto retry;

	}
	e = wl_get_wle(ubi);
	prot_queue_add(ubi, e);
	spin_unlock(&ubi->wl_lock);

	err = ubi_self_check_all_ff(ubi, e->pnum, ubi->vid_hdr_aloffset,
				    ubi->peb_size - ubi->vid_hdr_aloffset);
	if (err) {
		ubi_err(ubi, "new PEB %d does not contain all 0xFF bytes", e->pnum);
		return err;
	}

	return e->pnum;
}
#else
#include "fastmap-wl.c"
#endif
