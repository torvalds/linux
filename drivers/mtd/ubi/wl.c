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
 * UBI wear-leveling unit.
 *
 * This unit is responsible for wear-leveling. It works in terms of physical
 * eraseblocks and erase counters and knows nothing about logical eraseblocks,
 * volumes, etc. From this unit's perspective all physical eraseblocks are of
 * two types - used and free. Used physical eraseblocks are those that were
 * "get" by the 'ubi_wl_get_peb()' function, and free physical eraseblocks are
 * those that were put by the 'ubi_wl_put_peb()' function.
 *
 * Physical eraseblocks returned by 'ubi_wl_get_peb()' have only erase counter
 * header. The rest of the physical eraseblock contains only 0xFF bytes.
 *
 * When physical eraseblocks are returned to the WL unit by means of the
 * 'ubi_wl_put_peb()' function, they are scheduled for erasure. The erasure is
 * done asynchronously in context of the per-UBI device background thread,
 * which is also managed by the WL unit.
 *
 * The wear-leveling is ensured by means of moving the contents of used
 * physical eraseblocks with low erase counter to free physical eraseblocks
 * with high erase counter.
 *
 * The 'ubi_wl_get_peb()' function accepts data type hints which help to pick
 * an "optimal" physical eraseblock. For example, when it is known that the
 * physical eraseblock will be "put" soon because it contains short-term data,
 * the WL unit may pick a free physical eraseblock with low erase counter, and
 * so forth.
 *
 * If the WL unit fails to erase a physical eraseblock, it marks it as bad.
 *
 * This unit is also responsible for scrubbing. If a bit-flip is detected in a
 * physical eraseblock, it has to be moved. Technically this is the same as
 * moving it for wear-leveling reasons.
 *
 * As it was said, for the UBI unit all physical eraseblocks are either "free"
 * or "used". Free eraseblock are kept in the @wl->free RB-tree, while used
 * eraseblocks are kept in a set of different RB-trees: @wl->used,
 * @wl->prot.pnum, @wl->prot.aec, and @wl->scrub.
 *
 * Note, in this implementation, we keep a small in-RAM object for each physical
 * eraseblock. This is surely not a scalable solution. But it appears to be good
 * enough for moderately large flashes and it is simple. In future, one may
 * re-work this unit and make it more scalable.
 *
 * At the moment this unit does not utilize the sequence number, which was
 * introduced relatively recently. But it would be wise to do this because the
 * sequence number of a logical eraseblock characterizes how old is it. For
 * example, when we move a PEB with low erase counter, and we need to pick the
 * target PEB, we pick a PEB with the highest EC if our PEB is "old" and we
 * pick target PEB with an average EC if our PEB is not very "old". This is a
 * room for future re-works of the WL unit.
 *
 * FIXME: looks too complex, should be simplified (later).
 */

#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include "ubi.h"

/* Number of physical eraseblocks reserved for wear-leveling purposes */
#define WL_RESERVED_PEBS 1

/*
 * How many erase cycles are short term, unknown, and long term physical
 * eraseblocks protected.
 */
#define ST_PROTECTION 16
#define U_PROTECTION  10
#define LT_PROTECTION 4

/*
 * Maximum difference between two erase counters. If this threshold is
 * exceeded, the WL unit starts moving data from used physical eraseblocks with
 * low erase counter to free physical eraseblocks with high erase counter.
 */
#define UBI_WL_THRESHOLD CONFIG_MTD_UBI_WL_THRESHOLD

/*
 * When a physical eraseblock is moved, the WL unit has to pick the target
 * physical eraseblock to move to. The simplest way would be just to pick the
 * one with the highest erase counter. But in certain workloads this could lead
 * to an unlimited wear of one or few physical eraseblock. Indeed, imagine a
 * situation when the picked physical eraseblock is constantly erased after the
 * data is written to it. So, we have a constant which limits the highest erase
 * counter of the free physical eraseblock to pick. Namely, the WL unit does
 * not pick eraseblocks with erase counter greater then the lowest erase
 * counter plus %WL_FREE_MAX_DIFF.
 */
#define WL_FREE_MAX_DIFF (2*UBI_WL_THRESHOLD)

/*
 * Maximum number of consecutive background thread failures which is enough to
 * switch to read-only mode.
 */
#define WL_MAX_FAILURES 32

/**
 * struct ubi_wl_entry - wear-leveling entry.
 * @rb: link in the corresponding RB-tree
 * @ec: erase counter
 * @pnum: physical eraseblock number
 *
 * Each physical eraseblock has a corresponding &struct wl_entry object which
 * may be kept in different RB-trees.
 */
struct ubi_wl_entry {
	struct rb_node rb;
	int ec;
	int pnum;
};

/**
 * struct ubi_wl_prot_entry - PEB protection entry.
 * @rb_pnum: link in the @wl->prot.pnum RB-tree
 * @rb_aec: link in the @wl->prot.aec RB-tree
 * @abs_ec: the absolute erase counter value when the protection ends
 * @e: the wear-leveling entry of the physical eraseblock under protection
 *
 * When the WL unit returns a physical eraseblock, the physical eraseblock is
 * protected from being moved for some "time". For this reason, the physical
 * eraseblock is not directly moved from the @wl->free tree to the @wl->used
 * tree. There is one more tree in between where this physical eraseblock is
 * temporarily stored (@wl->prot).
 *
 * All this protection stuff is needed because:
 *  o we don't want to move physical eraseblocks just after we have given them
 *    to the user; instead, we first want to let users fill them up with data;
 *
 *  o there is a chance that the user will put the physical eraseblock very
 *    soon, so it makes sense not to move it for some time, but wait; this is
 *    especially important in case of "short term" physical eraseblocks.
 *
 * Physical eraseblocks stay protected only for limited time. But the "time" is
 * measured in erase cycles in this case. This is implemented with help of the
 * absolute erase counter (@wl->abs_ec). When it reaches certain value, the
 * physical eraseblocks are moved from the protection trees (@wl->prot.*) to
 * the @wl->used tree.
 *
 * Protected physical eraseblocks are searched by physical eraseblock number
 * (when they are put) and by the absolute erase counter (to check if it is
 * time to move them to the @wl->used tree). So there are actually 2 RB-trees
 * storing the protected physical eraseblocks: @wl->prot.pnum and
 * @wl->prot.aec. They are referred to as the "protection" trees. The
 * first one is indexed by the physical eraseblock number. The second one is
 * indexed by the absolute erase counter. Both trees store
 * &struct ubi_wl_prot_entry objects.
 *
 * Each physical eraseblock has 2 main states: free and used. The former state
 * corresponds to the @wl->free tree. The latter state is split up on several
 * sub-states:
 * o the WL movement is allowed (@wl->used tree);
 * o the WL movement is temporarily prohibited (@wl->prot.pnum and
 * @wl->prot.aec trees);
 * o scrubbing is needed (@wl->scrub tree).
 *
 * Depending on the sub-state, wear-leveling entries of the used physical
 * eraseblocks may be kept in one of those trees.
 */
struct ubi_wl_prot_entry {
	struct rb_node rb_pnum;
	struct rb_node rb_aec;
	unsigned long long abs_ec;
	struct ubi_wl_entry *e;
};

/**
 * struct ubi_work - UBI work description data structure.
 * @list: a link in the list of pending works
 * @func: worker function
 * @priv: private data of the worker function
 *
 * @e: physical eraseblock to erase
 * @torture: if the physical eraseblock has to be tortured
 *
 * The @func pointer points to the worker function. If the @cancel argument is
 * not zero, the worker has to free the resources and exit immediately. The
 * worker has to return zero in case of success and a negative error code in
 * case of failure.
 */
struct ubi_work {
	struct list_head list;
	int (*func)(struct ubi_device *ubi, struct ubi_work *wrk, int cancel);
	/* The below fields are only relevant to erasure works */
	struct ubi_wl_entry *e;
	int torture;
};

#ifdef CONFIG_MTD_UBI_DEBUG_PARANOID
static int paranoid_check_ec(const struct ubi_device *ubi, int pnum, int ec);
static int paranoid_check_in_wl_tree(struct ubi_wl_entry *e,
				     struct rb_root *root);
#else
#define paranoid_check_ec(ubi, pnum, ec) 0
#define paranoid_check_in_wl_tree(e, root)
#endif

/* Slab cache for wear-leveling entries */
static struct kmem_cache *wl_entries_slab;

/**
 * tree_empty - a helper function to check if an RB-tree is empty.
 * @root: the root of the tree
 *
 * This function returns non-zero if the RB-tree is empty and zero if not.
 */
static inline int tree_empty(struct rb_root *root)
{
	return root->rb_node == NULL;
}

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
		e1 = rb_entry(parent, struct ubi_wl_entry, rb);

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

	rb_link_node(&e->rb, parent, p);
	rb_insert_color(&e->rb, root);
}


/*
 * Helper functions to add and delete wear-leveling entries from different
 * trees.
 */

static void free_tree_add(struct ubi_device *ubi, struct ubi_wl_entry *e)
{
	wl_tree_add(e, &ubi->free);
}
static inline void used_tree_add(struct ubi_device *ubi,
				 struct ubi_wl_entry *e)
{
	wl_tree_add(e, &ubi->used);
}
static inline void scrub_tree_add(struct ubi_device *ubi,
				  struct ubi_wl_entry *e)
{
	wl_tree_add(e, &ubi->scrub);
}
static inline void free_tree_del(struct ubi_device *ubi,
				 struct ubi_wl_entry *e)
{
	paranoid_check_in_wl_tree(e, &ubi->free);
	rb_erase(&e->rb, &ubi->free);
}
static inline void used_tree_del(struct ubi_device *ubi,
				 struct ubi_wl_entry *e)
{
	paranoid_check_in_wl_tree(e, &ubi->used);
	rb_erase(&e->rb, &ubi->used);
}
static inline void scrub_tree_del(struct ubi_device *ubi,
				  struct ubi_wl_entry *e)
{
	paranoid_check_in_wl_tree(e, &ubi->scrub);
	rb_erase(&e->rb, &ubi->scrub);
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

	spin_lock(&ubi->wl_lock);

	if (list_empty(&ubi->works)) {
		spin_unlock(&ubi->wl_lock);
		return 0;
	}

	wrk = list_entry(ubi->works.next, struct ubi_work, list);
	list_del(&wrk->list);
	spin_unlock(&ubi->wl_lock);

	/*
	 * Call the worker function. Do not touch the work structure
	 * after this call as it will have been freed or reused by that
	 * time by the worker function.
	 */
	err = wrk->func(ubi, wrk, 0);
	if (err)
		ubi_err("work failed with error code %d", err);

	spin_lock(&ubi->wl_lock);
	ubi->works_count -= 1;
	ubi_assert(ubi->works_count >= 0);
	spin_unlock(&ubi->wl_lock);
	return err;
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

	spin_lock(&ubi->wl_lock);
	while (tree_empty(&ubi->free)) {
		spin_unlock(&ubi->wl_lock);

		dbg_wl("do one work synchronously");
		err = do_work(ubi);
		if (err)
			return err;

		spin_lock(&ubi->wl_lock);
	}
	spin_unlock(&ubi->wl_lock);

	return 0;
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

		e1 = rb_entry(p, struct ubi_wl_entry, rb);

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
 * prot_tree_add - add physical eraseblock to protection trees.
 * @ubi: UBI device description object
 * @e: the physical eraseblock to add
 * @pe: protection entry object to use
 * @abs_ec: absolute erase counter value when this physical eraseblock has
 * to be removed from the protection trees.
 *
 * @wl->lock has to be locked.
 */
static void prot_tree_add(struct ubi_device *ubi, struct ubi_wl_entry *e,
			  struct ubi_wl_prot_entry *pe, int abs_ec)
{
	struct rb_node **p, *parent = NULL;
	struct ubi_wl_prot_entry *pe1;

	pe->e = e;
	pe->abs_ec = ubi->abs_ec + abs_ec;

	p = &ubi->prot.pnum.rb_node;
	while (*p) {
		parent = *p;
		pe1 = rb_entry(parent, struct ubi_wl_prot_entry, rb_pnum);

		if (e->pnum < pe1->e->pnum)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&pe->rb_pnum, parent, p);
	rb_insert_color(&pe->rb_pnum, &ubi->prot.pnum);

	p = &ubi->prot.aec.rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;
		pe1 = rb_entry(parent, struct ubi_wl_prot_entry, rb_aec);

		if (pe->abs_ec < pe1->abs_ec)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&pe->rb_aec, parent, p);
	rb_insert_color(&pe->rb_aec, &ubi->prot.aec);
}

/**
 * find_wl_entry - find wear-leveling entry closest to certain erase counter.
 * @root: the RB-tree where to look for
 * @max: highest possible erase counter
 *
 * This function looks for a wear leveling entry with erase counter closest to
 * @max and less then @max.
 */
static struct ubi_wl_entry *find_wl_entry(struct rb_root *root, int max)
{
	struct rb_node *p;
	struct ubi_wl_entry *e;

	e = rb_entry(rb_first(root), struct ubi_wl_entry, rb);
	max += e->ec;

	p = root->rb_node;
	while (p) {
		struct ubi_wl_entry *e1;

		e1 = rb_entry(p, struct ubi_wl_entry, rb);
		if (e1->ec >= max)
			p = p->rb_left;
		else {
			p = p->rb_right;
			e = e1;
		}
	}

	return e;
}

/**
 * ubi_wl_get_peb - get a physical eraseblock.
 * @ubi: UBI device description object
 * @dtype: type of data which will be stored in this physical eraseblock
 *
 * This function returns a physical eraseblock in case of success and a
 * negative error code in case of failure. Might sleep.
 */
int ubi_wl_get_peb(struct ubi_device *ubi, int dtype)
{
	int err, protect, medium_ec;
	struct ubi_wl_entry *e, *first, *last;
	struct ubi_wl_prot_entry *pe;

	ubi_assert(dtype == UBI_LONGTERM || dtype == UBI_SHORTTERM ||
		   dtype == UBI_UNKNOWN);

	pe = kmalloc(sizeof(struct ubi_wl_prot_entry), GFP_KERNEL);
	if (!pe)
		return -ENOMEM;

retry:
	spin_lock(&ubi->wl_lock);
	if (tree_empty(&ubi->free)) {
		if (ubi->works_count == 0) {
			ubi_assert(list_empty(&ubi->works));
			ubi_err("no free eraseblocks");
			spin_unlock(&ubi->wl_lock);
			kfree(pe);
			return -ENOSPC;
		}
		spin_unlock(&ubi->wl_lock);

		err = produce_free_peb(ubi);
		if (err < 0) {
			kfree(pe);
			return err;
		}
		goto retry;
	}

	switch (dtype) {
		case UBI_LONGTERM:
			/*
			 * For long term data we pick a physical eraseblock
			 * with high erase counter. But the highest erase
			 * counter we can pick is bounded by the the lowest
			 * erase counter plus %WL_FREE_MAX_DIFF.
			 */
			e = find_wl_entry(&ubi->free, WL_FREE_MAX_DIFF);
			protect = LT_PROTECTION;
			break;
		case UBI_UNKNOWN:
			/*
			 * For unknown data we pick a physical eraseblock with
			 * medium erase counter. But we by no means can pick a
			 * physical eraseblock with erase counter greater or
			 * equivalent than the lowest erase counter plus
			 * %WL_FREE_MAX_DIFF.
			 */
			first = rb_entry(rb_first(&ubi->free),
					 struct ubi_wl_entry, rb);
			last = rb_entry(rb_last(&ubi->free),
					struct ubi_wl_entry, rb);

			if (last->ec - first->ec < WL_FREE_MAX_DIFF)
				e = rb_entry(ubi->free.rb_node,
						struct ubi_wl_entry, rb);
			else {
				medium_ec = (first->ec + WL_FREE_MAX_DIFF)/2;
				e = find_wl_entry(&ubi->free, medium_ec);
			}
			protect = U_PROTECTION;
			break;
		case UBI_SHORTTERM:
			/*
			 * For short term data we pick a physical eraseblock
			 * with the lowest erase counter as we expect it will
			 * be erased soon.
			 */
			e = rb_entry(rb_first(&ubi->free),
				     struct ubi_wl_entry, rb);
			protect = ST_PROTECTION;
			break;
		default:
			protect = 0;
			e = NULL;
			BUG();
	}

	/*
	 * Move the physical eraseblock to the protection trees where it will
	 * be protected from being moved for some time.
	 */
	free_tree_del(ubi, e);
	prot_tree_add(ubi, e, pe, protect);

	dbg_wl("PEB %d EC %d, protection %d", e->pnum, e->ec, protect);
	spin_unlock(&ubi->wl_lock);

	return e->pnum;
}

/**
 * prot_tree_del - remove a physical eraseblock from the protection trees
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock to remove
 */
static void prot_tree_del(struct ubi_device *ubi, int pnum)
{
	struct rb_node *p;
	struct ubi_wl_prot_entry *pe = NULL;

	p = ubi->prot.pnum.rb_node;
	while (p) {

		pe = rb_entry(p, struct ubi_wl_prot_entry, rb_pnum);

		if (pnum == pe->e->pnum)
			break;

		if (pnum < pe->e->pnum)
			p = p->rb_left;
		else
			p = p->rb_right;
	}

	ubi_assert(pe->e->pnum == pnum);
	rb_erase(&pe->rb_aec, &ubi->prot.aec);
	rb_erase(&pe->rb_pnum, &ubi->prot.pnum);
	kfree(pe);
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
static int sync_erase(struct ubi_device *ubi, struct ubi_wl_entry *e, int torture)
{
	int err;
	struct ubi_ec_hdr *ec_hdr;
	unsigned long long ec = e->ec;

	dbg_wl("erase PEB %d, old EC %llu", e->pnum, ec);

	err = paranoid_check_ec(ubi, e->pnum, e->ec);
	if (err > 0)
		return -EINVAL;

	ec_hdr = kzalloc(ubi->ec_hdr_alsize, GFP_KERNEL);
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
		ubi_err("erase counter overflow at PEB %d, EC %llu",
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
 * check_protection_over - check if it is time to stop protecting some
 * physical eraseblocks.
 * @ubi: UBI device description object
 *
 * This function is called after each erase operation, when the absolute erase
 * counter is incremented, to check if some physical eraseblock  have not to be
 * protected any longer. These physical eraseblocks are moved from the
 * protection trees to the used tree.
 */
static void check_protection_over(struct ubi_device *ubi)
{
	struct ubi_wl_prot_entry *pe;

	/*
	 * There may be several protected physical eraseblock to remove,
	 * process them all.
	 */
	while (1) {
		spin_lock(&ubi->wl_lock);
		if (tree_empty(&ubi->prot.aec)) {
			spin_unlock(&ubi->wl_lock);
			break;
		}

		pe = rb_entry(rb_first(&ubi->prot.aec),
			      struct ubi_wl_prot_entry, rb_aec);

		if (pe->abs_ec > ubi->abs_ec) {
			spin_unlock(&ubi->wl_lock);
			break;
		}

		dbg_wl("PEB %d protection over, abs_ec %llu, PEB abs_ec %llu",
		       pe->e->pnum, ubi->abs_ec, pe->abs_ec);
		rb_erase(&pe->rb_aec, &ubi->prot.aec);
		rb_erase(&pe->rb_pnum, &ubi->prot.pnum);
		used_tree_add(ubi, pe->e);
		spin_unlock(&ubi->wl_lock);

		kfree(pe);
		cond_resched();
	}
}

/**
 * schedule_ubi_work - schedule a work.
 * @ubi: UBI device description object
 * @wrk: the work to schedule
 *
 * This function enqueues a work defined by @wrk to the tail of the pending
 * works list.
 */
static void schedule_ubi_work(struct ubi_device *ubi, struct ubi_work *wrk)
{
	spin_lock(&ubi->wl_lock);
	list_add_tail(&wrk->list, &ubi->works);
	ubi_assert(ubi->works_count >= 0);
	ubi->works_count += 1;
	if (ubi->thread_enabled)
		wake_up_process(ubi->bgt_thread);
	spin_unlock(&ubi->wl_lock);
}

static int erase_worker(struct ubi_device *ubi, struct ubi_work *wl_wrk,
			int cancel);

/**
 * schedule_erase - schedule an erase work.
 * @ubi: UBI device description object
 * @e: the WL entry of the physical eraseblock to erase
 * @torture: if the physical eraseblock has to be tortured
 *
 * This function returns zero in case of success and a %-ENOMEM in case of
 * failure.
 */
static int schedule_erase(struct ubi_device *ubi, struct ubi_wl_entry *e,
			  int torture)
{
	struct ubi_work *wl_wrk;

	dbg_wl("schedule erasure of PEB %d, EC %d, torture %d",
	       e->pnum, e->ec, torture);

	wl_wrk = kmalloc(sizeof(struct ubi_work), GFP_KERNEL);
	if (!wl_wrk)
		return -ENOMEM;

	wl_wrk->func = &erase_worker;
	wl_wrk->e = e;
	wl_wrk->torture = torture;

	schedule_ubi_work(ubi, wl_wrk);
	return 0;
}

/**
 * wear_leveling_worker - wear-leveling worker function.
 * @ubi: UBI device description object
 * @wrk: the work object
 * @cancel: non-zero if the worker has to free memory and exit
 *
 * This function copies a more worn out physical eraseblock to a less worn out
 * one. Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int wear_leveling_worker(struct ubi_device *ubi, struct ubi_work *wrk,
				int cancel)
{
	int err, put = 0;
	struct ubi_wl_entry *e1, *e2;
	struct ubi_vid_hdr *vid_hdr;

	kfree(wrk);

	if (cancel)
		return 0;

	vid_hdr = ubi_zalloc_vid_hdr(ubi);
	if (!vid_hdr)
		return -ENOMEM;

	spin_lock(&ubi->wl_lock);

	/*
	 * Only one WL worker at a time is supported at this implementation, so
	 * make sure a PEB is not being moved already.
	 */
	if (ubi->move_to || tree_empty(&ubi->free) ||
	    (tree_empty(&ubi->used) && tree_empty(&ubi->scrub))) {
		/*
		 * Only one WL worker at a time is supported at this
		 * implementation, so if a LEB is already being moved, cancel.
		 *
		 * No free physical eraseblocks? Well, we cancel wear-leveling
		 * then. It will be triggered again when a free physical
		 * eraseblock appears.
		 *
		 * No used physical eraseblocks? They must be temporarily
		 * protected from being moved. They will be moved to the
		 * @ubi->used tree later and the wear-leveling will be
		 * triggered again.
		 */
		dbg_wl("cancel WL, a list is empty: free %d, used %d",
		       tree_empty(&ubi->free), tree_empty(&ubi->used));
		ubi->wl_scheduled = 0;
		spin_unlock(&ubi->wl_lock);
		ubi_free_vid_hdr(ubi, vid_hdr);
		return 0;
	}

	if (tree_empty(&ubi->scrub)) {
		/*
		 * Now pick the least worn-out used physical eraseblock and a
		 * highly worn-out free physical eraseblock. If the erase
		 * counters differ much enough, start wear-leveling.
		 */
		e1 = rb_entry(rb_first(&ubi->used), struct ubi_wl_entry, rb);
		e2 = find_wl_entry(&ubi->free, WL_FREE_MAX_DIFF);

		if (!(e2->ec - e1->ec >= UBI_WL_THRESHOLD)) {
			dbg_wl("no WL needed: min used EC %d, max free EC %d",
			       e1->ec, e2->ec);
			ubi->wl_scheduled = 0;
			spin_unlock(&ubi->wl_lock);
			ubi_free_vid_hdr(ubi, vid_hdr);
			return 0;
		}
		used_tree_del(ubi, e1);
		dbg_wl("move PEB %d EC %d to PEB %d EC %d",
		       e1->pnum, e1->ec, e2->pnum, e2->ec);
	} else {
		e1 = rb_entry(rb_first(&ubi->scrub), struct ubi_wl_entry, rb);
		e2 = find_wl_entry(&ubi->free, WL_FREE_MAX_DIFF);
		scrub_tree_del(ubi, e1);
		dbg_wl("scrub PEB %d to PEB %d", e1->pnum, e2->pnum);
	}

	free_tree_del(ubi, e2);
	ubi_assert(!ubi->move_from && !ubi->move_to);
	ubi_assert(!ubi->move_to_put && !ubi->move_from_put);
	ubi->move_from = e1;
	ubi->move_to = e2;
	spin_unlock(&ubi->wl_lock);

	/*
	 * Now we are going to copy physical eraseblock @e1->pnum to @e2->pnum.
	 * We so far do not know which logical eraseblock our physical
	 * eraseblock (@e1) belongs to. We have to read the volume identifier
	 * header first.
	 */

	err = ubi_io_read_vid_hdr(ubi, e1->pnum, vid_hdr, 0);
	if (err && err != UBI_IO_BITFLIPS) {
		if (err == UBI_IO_PEB_FREE) {
			/*
			 * We are trying to move PEB without a VID header. UBI
			 * always write VID headers shortly after the PEB was
			 * given, so we have a situation when it did not have
			 * chance to write it down because it was preempted.
			 * Just re-schedule the work, so that next time it will
			 * likely have the VID header in place.
			 */
			dbg_wl("PEB %d has no VID header", e1->pnum);
			err = 0;
		} else {
			ubi_err("error %d while reading VID header from PEB %d",
				err, e1->pnum);
			if (err > 0)
				err = -EIO;
		}
		goto error;
	}

	err = ubi_eba_copy_leb(ubi, e1->pnum, e2->pnum, vid_hdr);
	if (err) {
		if (err == UBI_IO_BITFLIPS)
			err = 0;
		goto error;
	}

	ubi_free_vid_hdr(ubi, vid_hdr);
	spin_lock(&ubi->wl_lock);
	if (!ubi->move_to_put)
		used_tree_add(ubi, e2);
	else
		put = 1;
	ubi->move_from = ubi->move_to = NULL;
	ubi->move_from_put = ubi->move_to_put = 0;
	ubi->wl_scheduled = 0;
	spin_unlock(&ubi->wl_lock);

	if (put) {
		/*
		 * Well, the target PEB was put meanwhile, schedule it for
		 * erasure.
		 */
		dbg_wl("PEB %d was put meanwhile, erase", e2->pnum);
		err = schedule_erase(ubi, e2, 0);
		if (err) {
			kmem_cache_free(wl_entries_slab, e2);
			ubi_ro_mode(ubi);
		}
	}

	err = schedule_erase(ubi, e1, 0);
	if (err) {
		kmem_cache_free(wl_entries_slab, e1);
		ubi_ro_mode(ubi);
	}

	dbg_wl("done");
	return err;

	/*
	 * Some error occurred. @e1 was not changed, so return it back. @e2
	 * might be changed, schedule it for erasure.
	 */
error:
	if (err)
		dbg_wl("error %d occurred, cancel operation", err);
	ubi_assert(err <= 0);

	ubi_free_vid_hdr(ubi, vid_hdr);
	spin_lock(&ubi->wl_lock);
	ubi->wl_scheduled = 0;
	if (ubi->move_from_put)
		put = 1;
	else
		used_tree_add(ubi, e1);
	ubi->move_from = ubi->move_to = NULL;
	ubi->move_from_put = ubi->move_to_put = 0;
	spin_unlock(&ubi->wl_lock);

	if (put) {
		/*
		 * Well, the target PEB was put meanwhile, schedule it for
		 * erasure.
		 */
		dbg_wl("PEB %d was put meanwhile, erase", e1->pnum);
		err = schedule_erase(ubi, e1, 0);
		if (err) {
			kmem_cache_free(wl_entries_slab, e1);
			ubi_ro_mode(ubi);
		}
	}

	err = schedule_erase(ubi, e2, 0);
	if (err) {
		kmem_cache_free(wl_entries_slab, e2);
		ubi_ro_mode(ubi);
	}

	yield();
	return err;
}

/**
 * ensure_wear_leveling - schedule wear-leveling if it is needed.
 * @ubi: UBI device description object
 *
 * This function checks if it is time to start wear-leveling and schedules it
 * if yes. This function returns zero in case of success and a negative error
 * code in case of failure.
 */
static int ensure_wear_leveling(struct ubi_device *ubi)
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
	if (tree_empty(&ubi->scrub)) {
		if (tree_empty(&ubi->used) || tree_empty(&ubi->free))
			/* No physical eraseblocks - no deal */
			goto out_unlock;

		/*
		 * We schedule wear-leveling only if the difference between the
		 * lowest erase counter of used physical eraseblocks and a high
		 * erase counter of free physical eraseblocks is greater then
		 * %UBI_WL_THRESHOLD.
		 */
		e1 = rb_entry(rb_first(&ubi->used), struct ubi_wl_entry, rb);
		e2 = find_wl_entry(&ubi->free, WL_FREE_MAX_DIFF);

		if (!(e2->ec - e1->ec >= UBI_WL_THRESHOLD))
			goto out_unlock;
		dbg_wl("schedule wear-leveling");
	} else
		dbg_wl("schedule scrubbing");

	ubi->wl_scheduled = 1;
	spin_unlock(&ubi->wl_lock);

	wrk = kmalloc(sizeof(struct ubi_work), GFP_KERNEL);
	if (!wrk) {
		err = -ENOMEM;
		goto out_cancel;
	}

	wrk->func = &wear_leveling_worker;
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
 * erase_worker - physical eraseblock erase worker function.
 * @ubi: UBI device description object
 * @wl_wrk: the work object
 * @cancel: non-zero if the worker has to free memory and exit
 *
 * This function erases a physical eraseblock and perform torture testing if
 * needed. It also takes care about marking the physical eraseblock bad if
 * needed. Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int erase_worker(struct ubi_device *ubi, struct ubi_work *wl_wrk,
			int cancel)
{
	struct ubi_wl_entry *e = wl_wrk->e;
	int pnum = e->pnum, err, need;

	if (cancel) {
		dbg_wl("cancel erasure of PEB %d EC %d", pnum, e->ec);
		kfree(wl_wrk);
		kmem_cache_free(wl_entries_slab, e);
		return 0;
	}

	dbg_wl("erase PEB %d EC %d", pnum, e->ec);

	err = sync_erase(ubi, e, wl_wrk->torture);
	if (!err) {
		/* Fine, we've erased it successfully */
		kfree(wl_wrk);

		spin_lock(&ubi->wl_lock);
		ubi->abs_ec += 1;
		free_tree_add(ubi, e);
		spin_unlock(&ubi->wl_lock);

		/*
		 * One more erase operation has happened, take care about protected
		 * physical eraseblocks.
		 */
		check_protection_over(ubi);

		/* And take care about wear-leveling */
		err = ensure_wear_leveling(ubi);
		return err;
	}

	kfree(wl_wrk);
	kmem_cache_free(wl_entries_slab, e);

	if (err == -EINTR || err == -ENOMEM || err == -EAGAIN ||
	    err == -EBUSY) {
		int err1;

		/* Re-schedule the LEB for erasure */
		err1 = schedule_erase(ubi, e, 0);
		if (err1) {
			err = err1;
			goto out_ro;
		}
		return err;
	} else if (err != -EIO) {
		/*
		 * If this is not %-EIO, we have no idea what to do. Scheduling
		 * this physical eraseblock for erasure again would cause
		 * errors again and again. Well, lets switch to RO mode.
		 */
		goto out_ro;
	}

	/* It is %-EIO, the PEB went bad */

	if (!ubi->bad_allowed) {
		ubi_err("bad physical eraseblock %d detected", pnum);
		goto out_ro;
	}

	spin_lock(&ubi->volumes_lock);
	need = ubi->beb_rsvd_level - ubi->beb_rsvd_pebs + 1;
	if (need > 0) {
		need = ubi->avail_pebs >= need ? need : ubi->avail_pebs;
		ubi->avail_pebs -= need;
		ubi->rsvd_pebs += need;
		ubi->beb_rsvd_pebs += need;
		if (need > 0)
			ubi_msg("reserve more %d PEBs", need);
	}

	if (ubi->beb_rsvd_pebs == 0) {
		spin_unlock(&ubi->volumes_lock);
		ubi_err("no reserved physical eraseblocks");
		goto out_ro;
	}

	spin_unlock(&ubi->volumes_lock);
	ubi_msg("mark PEB %d as bad", pnum);

	err = ubi_io_mark_bad(ubi, pnum);
	if (err)
		goto out_ro;

	spin_lock(&ubi->volumes_lock);
	ubi->beb_rsvd_pebs -= 1;
	ubi->bad_peb_count += 1;
	ubi->good_peb_count -= 1;
	ubi_calculate_reserved(ubi);
	if (ubi->beb_rsvd_pebs == 0)
		ubi_warn("last PEB from the reserved pool was used");
	spin_unlock(&ubi->volumes_lock);

	return err;

out_ro:
	ubi_ro_mode(ubi);
	return err;
}

/**
 * ubi_wl_put_peb - return a physical eraseblock to the wear-leveling
 * unit.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock to return
 * @torture: if this physical eraseblock has to be tortured
 *
 * This function is called to return physical eraseblock @pnum to the pool of
 * free physical eraseblocks. The @torture flag has to be set if an I/O error
 * occurred to this @pnum and it has to be tested. This function returns zero
 * in case of success and a negative error code in case of failure.
 */
int ubi_wl_put_peb(struct ubi_device *ubi, int pnum, int torture)
{
	int err;
	struct ubi_wl_entry *e;

	dbg_wl("PEB %d", pnum);
	ubi_assert(pnum >= 0);
	ubi_assert(pnum < ubi->peb_count);

	spin_lock(&ubi->wl_lock);

	e = ubi->lookuptbl[pnum];
	if (e == ubi->move_from) {
		/*
		 * User is putting the physical eraseblock which was selected to
		 * be moved. It will be scheduled for erasure in the
		 * wear-leveling worker.
		 */
		dbg_wl("PEB %d is being moved", pnum);
		ubi_assert(!ubi->move_from_put);
		ubi->move_from_put = 1;
		spin_unlock(&ubi->wl_lock);
		return 0;
	} else if (e == ubi->move_to) {
		/*
		 * User is putting the physical eraseblock which was selected
		 * as the target the data is moved to. It may happen if the EBA
		 * unit already re-mapped the LEB but the WL unit did has not
		 * put the PEB to the "used" tree.
		 */
		dbg_wl("PEB %d is the target of data moving", pnum);
		ubi_assert(!ubi->move_to_put);
		ubi->move_to_put = 1;
		spin_unlock(&ubi->wl_lock);
		return 0;
	} else {
		if (in_wl_tree(e, &ubi->used))
			used_tree_del(ubi, e);
		else if (in_wl_tree(e, &ubi->scrub))
			scrub_tree_del(ubi, e);
		else
			prot_tree_del(ubi, e->pnum);
	}
	spin_unlock(&ubi->wl_lock);

	err = schedule_erase(ubi, e, torture);
	if (err) {
		spin_lock(&ubi->wl_lock);
		used_tree_add(ubi, e);
		spin_unlock(&ubi->wl_lock);
	}

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

	ubi_msg("schedule PEB %d for scrubbing", pnum);

retry:
	spin_lock(&ubi->wl_lock);
	e = ubi->lookuptbl[pnum];
	if (e == ubi->move_from || in_wl_tree(e, &ubi->scrub)) {
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

	if (in_wl_tree(e, &ubi->used))
		used_tree_del(ubi, e);
	else
		prot_tree_del(ubi, pnum);

	scrub_tree_add(ubi, e);
	spin_unlock(&ubi->wl_lock);

	/*
	 * Technically scrubbing is the same as wear-leveling, so it is done
	 * by the WL worker.
	 */
	return ensure_wear_leveling(ubi);
}

/**
 * ubi_wl_flush - flush all pending works.
 * @ubi: UBI device description object
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
int ubi_wl_flush(struct ubi_device *ubi)
{
	int err, pending_count;

	pending_count = ubi->works_count;

	dbg_wl("flush (%d pending works)", pending_count);

	/*
	 * Erase while the pending works queue is not empty, but not more then
	 * the number of currently pending works.
	 */
	while (pending_count-- > 0) {
		err = do_work(ubi);
		if (err)
			return err;
	}

	return 0;
}

/**
 * tree_destroy - destroy an RB-tree.
 * @root: the root of the tree to destroy
 */
static void tree_destroy(struct rb_root *root)
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
			e = rb_entry(rb, struct ubi_wl_entry, rb);

			rb = rb_parent(rb);
			if (rb) {
				if (rb->rb_left == &e->rb)
					rb->rb_left = NULL;
				else
					rb->rb_right = NULL;
			}

			kmem_cache_free(wl_entries_slab, e);
		}
	}
}

/**
 * ubi_thread - UBI background thread.
 * @u: the UBI device description object pointer
 */
static int ubi_thread(void *u)
{
	int failures = 0;
	struct ubi_device *ubi = u;

	ubi_msg("background thread \"%s\" started, PID %d",
		ubi->bgt_name, current->pid);

	set_freezable();
	for (;;) {
		int err;

		if (kthread_should_stop())
			goto out;

		if (try_to_freeze())
			continue;

		spin_lock(&ubi->wl_lock);
		if (list_empty(&ubi->works) || ubi->ro_mode ||
			       !ubi->thread_enabled) {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock(&ubi->wl_lock);
			schedule();
			continue;
		}
		spin_unlock(&ubi->wl_lock);

		err = do_work(ubi);
		if (err) {
			ubi_err("%s: work failed with error code %d",
				ubi->bgt_name, err);
			if (failures++ > WL_MAX_FAILURES) {
				/*
				 * Too many failures, disable the thread and
				 * switch to read-only mode.
				 */
				ubi_msg("%s: %d consecutive failures",
					ubi->bgt_name, WL_MAX_FAILURES);
				ubi_ro_mode(ubi);
				break;
			}
		} else
			failures = 0;

		cond_resched();
	}

out:
	dbg_wl("background thread \"%s\" is killed", ubi->bgt_name);
	return 0;
}

/**
 * cancel_pending - cancel all pending works.
 * @ubi: UBI device description object
 */
static void cancel_pending(struct ubi_device *ubi)
{
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
 * ubi_wl_init_scan - initialize the wear-leveling unit using scanning
 * information.
 * @ubi: UBI device description object
 * @si: scanning information
 *
 * This function returns zero in case of success, and a negative error code in
 * case of failure.
 */
int ubi_wl_init_scan(struct ubi_device *ubi, struct ubi_scan_info *si)
{
	int err;
	struct rb_node *rb1, *rb2;
	struct ubi_scan_volume *sv;
	struct ubi_scan_leb *seb, *tmp;
	struct ubi_wl_entry *e;


	ubi->used = ubi->free = ubi->scrub = RB_ROOT;
	ubi->prot.pnum = ubi->prot.aec = RB_ROOT;
	spin_lock_init(&ubi->wl_lock);
	ubi->max_ec = si->max_ec;
	INIT_LIST_HEAD(&ubi->works);

	sprintf(ubi->bgt_name, UBI_BGT_NAME_PATTERN, ubi->ubi_num);

	ubi->bgt_thread = kthread_create(ubi_thread, ubi, ubi->bgt_name);
	if (IS_ERR(ubi->bgt_thread)) {
		err = PTR_ERR(ubi->bgt_thread);
		ubi_err("cannot spawn \"%s\", error %d", ubi->bgt_name,
			err);
		return err;
	}

	if (ubi_devices_cnt == 0) {
		wl_entries_slab = kmem_cache_create("ubi_wl_entry_slab",
						    sizeof(struct ubi_wl_entry),
						    0, 0, NULL, NULL);
		if (!wl_entries_slab)
			return -ENOMEM;
	}

	err = -ENOMEM;
	ubi->lookuptbl = kzalloc(ubi->peb_count * sizeof(void *), GFP_KERNEL);
	if (!ubi->lookuptbl)
		goto out_free;

	list_for_each_entry_safe(seb, tmp, &si->erase, u.list) {
		cond_resched();

		e = kmem_cache_alloc(wl_entries_slab, GFP_KERNEL);
		if (!e)
			goto out_free;

		e->pnum = seb->pnum;
		e->ec = seb->ec;
		ubi->lookuptbl[e->pnum] = e;
		if (schedule_erase(ubi, e, 0)) {
			kmem_cache_free(wl_entries_slab, e);
			goto out_free;
		}
	}

	list_for_each_entry(seb, &si->free, u.list) {
		cond_resched();

		e = kmem_cache_alloc(wl_entries_slab, GFP_KERNEL);
		if (!e)
			goto out_free;

		e->pnum = seb->pnum;
		e->ec = seb->ec;
		ubi_assert(e->ec >= 0);
		free_tree_add(ubi, e);
		ubi->lookuptbl[e->pnum] = e;
	}

	list_for_each_entry(seb, &si->corr, u.list) {
		cond_resched();

		e = kmem_cache_alloc(wl_entries_slab, GFP_KERNEL);
		if (!e)
			goto out_free;

		e->pnum = seb->pnum;
		e->ec = seb->ec;
		ubi->lookuptbl[e->pnum] = e;
		if (schedule_erase(ubi, e, 0)) {
			kmem_cache_free(wl_entries_slab, e);
			goto out_free;
		}
	}

	ubi_rb_for_each_entry(rb1, sv, &si->volumes, rb) {
		ubi_rb_for_each_entry(rb2, seb, &sv->root, u.rb) {
			cond_resched();

			e = kmem_cache_alloc(wl_entries_slab, GFP_KERNEL);
			if (!e)
				goto out_free;

			e->pnum = seb->pnum;
			e->ec = seb->ec;
			ubi->lookuptbl[e->pnum] = e;
			if (!seb->scrub) {
				dbg_wl("add PEB %d EC %d to the used tree",
				       e->pnum, e->ec);
				used_tree_add(ubi, e);
			} else {
				dbg_wl("add PEB %d EC %d to the scrub tree",
				       e->pnum, e->ec);
				scrub_tree_add(ubi, e);
			}
		}
	}

	if (WL_RESERVED_PEBS > ubi->avail_pebs) {
		ubi_err("no enough physical eraseblocks (%d, need %d)",
			ubi->avail_pebs, WL_RESERVED_PEBS);
		goto out_free;
	}
	ubi->avail_pebs -= WL_RESERVED_PEBS;
	ubi->rsvd_pebs += WL_RESERVED_PEBS;

	/* Schedule wear-leveling if needed */
	err = ensure_wear_leveling(ubi);
	if (err)
		goto out_free;

	return 0;

out_free:
	cancel_pending(ubi);
	tree_destroy(&ubi->used);
	tree_destroy(&ubi->free);
	tree_destroy(&ubi->scrub);
	kfree(ubi->lookuptbl);
	if (ubi_devices_cnt == 0)
		kmem_cache_destroy(wl_entries_slab);
	return err;
}

/**
 * protection_trees_destroy - destroy the protection RB-trees.
 * @ubi: UBI device description object
 */
static void protection_trees_destroy(struct ubi_device *ubi)
{
	struct rb_node *rb;
	struct ubi_wl_prot_entry *pe;

	rb = ubi->prot.aec.rb_node;
	while (rb) {
		if (rb->rb_left)
			rb = rb->rb_left;
		else if (rb->rb_right)
			rb = rb->rb_right;
		else {
			pe = rb_entry(rb, struct ubi_wl_prot_entry, rb_aec);

			rb = rb_parent(rb);
			if (rb) {
				if (rb->rb_left == &pe->rb_aec)
					rb->rb_left = NULL;
				else
					rb->rb_right = NULL;
			}

			kmem_cache_free(wl_entries_slab, pe->e);
			kfree(pe);
		}
	}
}

/**
 * ubi_wl_close - close the wear-leveling unit.
 * @ubi: UBI device description object
 */
void ubi_wl_close(struct ubi_device *ubi)
{
	dbg_wl("disable \"%s\"", ubi->bgt_name);
	if (ubi->bgt_thread)
		kthread_stop(ubi->bgt_thread);

	dbg_wl("close the UBI wear-leveling unit");

	cancel_pending(ubi);
	protection_trees_destroy(ubi);
	tree_destroy(&ubi->used);
	tree_destroy(&ubi->free);
	tree_destroy(&ubi->scrub);
	kfree(ubi->lookuptbl);
	if (ubi_devices_cnt == 1)
		kmem_cache_destroy(wl_entries_slab);
}

#ifdef CONFIG_MTD_UBI_DEBUG_PARANOID

/**
 * paranoid_check_ec - make sure that the erase counter of a physical eraseblock
 * is correct.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to check
 * @ec: the erase counter to check
 *
 * This function returns zero if the erase counter of physical eraseblock @pnum
 * is equivalent to @ec, %1 if not, and a negative error code if an error
 * occurred.
 */
static int paranoid_check_ec(const struct ubi_device *ubi, int pnum, int ec)
{
	int err;
	long long read_ec;
	struct ubi_ec_hdr *ec_hdr;

	ec_hdr = kzalloc(ubi->ec_hdr_alsize, GFP_KERNEL);
	if (!ec_hdr)
		return -ENOMEM;

	err = ubi_io_read_ec_hdr(ubi, pnum, ec_hdr, 0);
	if (err && err != UBI_IO_BITFLIPS) {
		/* The header does not have to exist */
		err = 0;
		goto out_free;
	}

	read_ec = be64_to_cpu(ec_hdr->ec);
	if (ec != read_ec) {
		ubi_err("paranoid check failed for PEB %d", pnum);
		ubi_err("read EC is %lld, should be %d", read_ec, ec);
		ubi_dbg_dump_stack();
		err = 1;
	} else
		err = 0;

out_free:
	kfree(ec_hdr);
	return err;
}

/**
 * paranoid_check_in_wl_tree - make sure that a wear-leveling entry is present
 * in a WL RB-tree.
 * @e: the wear-leveling entry to check
 * @root: the root of the tree
 *
 * This function returns zero if @e is in the @root RB-tree and %1 if it
 * is not.
 */
static int paranoid_check_in_wl_tree(struct ubi_wl_entry *e,
				     struct rb_root *root)
{
	if (in_wl_tree(e, root))
		return 0;

	ubi_err("paranoid check failed for PEB %d, EC %d, RB-tree %p ",
		e->pnum, e->ec, root);
	ubi_dbg_dump_stack();
	return 1;
}

#endif /* CONFIG_MTD_UBI_DEBUG_PARANOID */
