/******************************************************************************
 *
 * Back-end of the driver for virtual block devices. This portion of the
 * driver exports a 'unified' block-device interface that can be accessed
 * by any operating system that implements a compatible front end. A
 * reference front-end implementation can be found in:
 *  drivers/block/xen-blkfront.c
 *
 * Copyright (c) 2003-2004, Keir Fraser & Steve Hand
 * Copyright (c) 2005, Christopher Clark
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/bitmap.h>

#include <xen/events.h>
#include <xen/page.h>
#include <xen/xen.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include <xen/balloon.h>
#include "common.h"

/*
 * These are rather arbitrary. They are fairly large because adjacent requests
 * pulled from a communication ring are quite likely to end up being part of
 * the same scatter/gather request at the disc.
 *
 * ** TRY INCREASING 'xen_blkif_reqs' IF WRITE SPEEDS SEEM TOO LOW **
 *
 * This will increase the chances of being able to write whole tracks.
 * 64 should be enough to keep us competitive with Linux.
 */
static int xen_blkif_reqs = 64;
module_param_named(reqs, xen_blkif_reqs, int, 0);
MODULE_PARM_DESC(reqs, "Number of blkback requests to allocate");

/*
 * Maximum number of unused free pages to keep in the internal buffer.
 * Setting this to a value too low will reduce memory used in each backend,
 * but can have a performance penalty.
 *
 * A sane value is xen_blkif_reqs * BLKIF_MAX_SEGMENTS_PER_REQUEST, but can
 * be set to a lower value that might degrade performance on some intensive
 * IO workloads.
 */

static int xen_blkif_max_buffer_pages = 704;
module_param_named(max_buffer_pages, xen_blkif_max_buffer_pages, int, 0644);
MODULE_PARM_DESC(max_buffer_pages,
"Maximum number of free pages to keep in each block backend buffer");

/*
 * Maximum number of grants to map persistently in blkback. For maximum
 * performance this should be the total numbers of grants that can be used
 * to fill the ring, but since this might become too high, specially with
 * the use of indirect descriptors, we set it to a value that provides good
 * performance without using too much memory.
 *
 * When the list of persistent grants is full we clean it up using a LRU
 * algorithm.
 */

static int xen_blkif_max_pgrants = 352;
module_param_named(max_persistent_grants, xen_blkif_max_pgrants, int, 0644);
MODULE_PARM_DESC(max_persistent_grants,
                 "Maximum number of grants to map persistently");

/*
 * The LRU mechanism to clean the lists of persistent grants needs to
 * be executed periodically. The time interval between consecutive executions
 * of the purge mechanism is set in ms.
 */
#define LRU_INTERVAL 100

/*
 * When the persistent grants list is full we will remove unused grants
 * from the list. The percent number of grants to be removed at each LRU
 * execution.
 */
#define LRU_PERCENT_CLEAN 5

/* Run-time switchable: /sys/module/blkback/parameters/ */
static unsigned int log_stats;
module_param(log_stats, int, 0644);

/*
 * Each outstanding request that we've passed to the lower device layers has a
 * 'pending_req' allocated to it. Each buffer_head that completes decrements
 * the pendcnt towards zero. When it hits zero, the specified domain has a
 * response queued for it, with the saved 'id' passed back.
 */
struct pending_req {
	struct xen_blkif	*blkif;
	u64			id;
	int			nr_pages;
	atomic_t		pendcnt;
	unsigned short		operation;
	int			status;
	struct list_head	free_list;
	struct page		*pages[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct persistent_gnt	*persistent_gnts[BLKIF_MAX_SEGMENTS_PER_REQUEST];
};

#define BLKBACK_INVALID_HANDLE (~0)

/* Number of free pages to remove on each call to free_xenballooned_pages */
#define NUM_BATCH_FREE_PAGES 10

struct xen_blkbk {
	struct pending_req	*pending_reqs;
	/* List of all 'pending_req' available */
	struct list_head	pending_free;
	/* And its spinlock. */
	spinlock_t		pending_free_lock;
	wait_queue_head_t	pending_free_wq;
	/* And the grant handles that are available. */
	grant_handle_t		*pending_grant_handles;
};

static struct xen_blkbk *blkbk;

/*
 * Little helpful macro to figure out the index and virtual address of the
 * pending_pages[..]. For each 'pending_req' we have have up to
 * BLKIF_MAX_SEGMENTS_PER_REQUEST (11) pages. The seg would be from 0 through
 * 10 and would index in the pending_pages[..].
 */
static inline int vaddr_pagenr(struct pending_req *req, int seg)
{
	return (req - blkbk->pending_reqs) *
		BLKIF_MAX_SEGMENTS_PER_REQUEST + seg;
}

static inline int get_free_page(struct xen_blkif *blkif, struct page **page)
{
	unsigned long flags;

	spin_lock_irqsave(&blkif->free_pages_lock, flags);
	if (list_empty(&blkif->free_pages)) {
		BUG_ON(blkif->free_pages_num != 0);
		spin_unlock_irqrestore(&blkif->free_pages_lock, flags);
		return alloc_xenballooned_pages(1, page, false);
	}
	BUG_ON(blkif->free_pages_num == 0);
	page[0] = list_first_entry(&blkif->free_pages, struct page, lru);
	list_del(&page[0]->lru);
	blkif->free_pages_num--;
	spin_unlock_irqrestore(&blkif->free_pages_lock, flags);

	return 0;
}

static inline void put_free_pages(struct xen_blkif *blkif, struct page **page,
                                  int num)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&blkif->free_pages_lock, flags);
	for (i = 0; i < num; i++)
		list_add(&page[i]->lru, &blkif->free_pages);
	blkif->free_pages_num += num;
	spin_unlock_irqrestore(&blkif->free_pages_lock, flags);
}

static inline void shrink_free_pagepool(struct xen_blkif *blkif, int num)
{
	/* Remove requested pages in batches of NUM_BATCH_FREE_PAGES */
	struct page *page[NUM_BATCH_FREE_PAGES];
	unsigned int num_pages = 0;
	unsigned long flags;

	spin_lock_irqsave(&blkif->free_pages_lock, flags);
	while (blkif->free_pages_num > num) {
		BUG_ON(list_empty(&blkif->free_pages));
		page[num_pages] = list_first_entry(&blkif->free_pages,
		                                   struct page, lru);
		list_del(&page[num_pages]->lru);
		blkif->free_pages_num--;
		if (++num_pages == NUM_BATCH_FREE_PAGES) {
			spin_unlock_irqrestore(&blkif->free_pages_lock, flags);
			free_xenballooned_pages(num_pages, page);
			spin_lock_irqsave(&blkif->free_pages_lock, flags);
			num_pages = 0;
		}
	}
	spin_unlock_irqrestore(&blkif->free_pages_lock, flags);
	if (num_pages != 0)
		free_xenballooned_pages(num_pages, page);
}

#define vaddr(page) ((unsigned long)pfn_to_kaddr(page_to_pfn(page)))

#define pending_handle(_req, _seg) \
	(blkbk->pending_grant_handles[vaddr_pagenr(_req, _seg)])


static int do_block_io_op(struct xen_blkif *blkif);
static int dispatch_rw_block_io(struct xen_blkif *blkif,
				struct blkif_request *req,
				struct pending_req *pending_req);
static void make_response(struct xen_blkif *blkif, u64 id,
			  unsigned short op, int st);

#define foreach_grant_safe(pos, n, rbtree, node) \
	for ((pos) = container_of(rb_first((rbtree)), typeof(*(pos)), node), \
	     (n) = (&(pos)->node != NULL) ? rb_next(&(pos)->node) : NULL; \
	     &(pos)->node != NULL; \
	     (pos) = container_of(n, typeof(*(pos)), node), \
	     (n) = (&(pos)->node != NULL) ? rb_next(&(pos)->node) : NULL)


/*
 * We don't need locking around the persistent grant helpers
 * because blkback uses a single-thread for each backed, so we
 * can be sure that this functions will never be called recursively.
 *
 * The only exception to that is put_persistent_grant, that can be called
 * from interrupt context (by xen_blkbk_unmap), so we have to use atomic
 * bit operations to modify the flags of a persistent grant and to count
 * the number of used grants.
 */
static int add_persistent_gnt(struct xen_blkif *blkif,
			       struct persistent_gnt *persistent_gnt)
{
	struct rb_node **new = NULL, *parent = NULL;
	struct persistent_gnt *this;

	if (blkif->persistent_gnt_c >= xen_blkif_max_pgrants) {
		if (!blkif->vbd.overflow_max_grants)
			blkif->vbd.overflow_max_grants = 1;
		return -EBUSY;
	}
	/* Figure out where to put new node */
	new = &blkif->persistent_gnts.rb_node;
	while (*new) {
		this = container_of(*new, struct persistent_gnt, node);

		parent = *new;
		if (persistent_gnt->gnt < this->gnt)
			new = &((*new)->rb_left);
		else if (persistent_gnt->gnt > this->gnt)
			new = &((*new)->rb_right);
		else {
			pr_alert_ratelimited(DRV_PFX " trying to add a gref that's already in the tree\n");
			return -EINVAL;
		}
	}

	bitmap_zero(persistent_gnt->flags, PERSISTENT_GNT_FLAGS_SIZE);
	set_bit(PERSISTENT_GNT_ACTIVE, persistent_gnt->flags);
	/* Add new node and rebalance tree. */
	rb_link_node(&(persistent_gnt->node), parent, new);
	rb_insert_color(&(persistent_gnt->node), &blkif->persistent_gnts);
	blkif->persistent_gnt_c++;
	atomic_inc(&blkif->persistent_gnt_in_use);
	return 0;
}

static struct persistent_gnt *get_persistent_gnt(struct xen_blkif *blkif,
						 grant_ref_t gref)
{
	struct persistent_gnt *data;
	struct rb_node *node = NULL;

	node = blkif->persistent_gnts.rb_node;
	while (node) {
		data = container_of(node, struct persistent_gnt, node);

		if (gref < data->gnt)
			node = node->rb_left;
		else if (gref > data->gnt)
			node = node->rb_right;
		else {
			if(test_bit(PERSISTENT_GNT_ACTIVE, data->flags)) {
				pr_alert_ratelimited(DRV_PFX " requesting a grant already in use\n");
				return NULL;
			}
			set_bit(PERSISTENT_GNT_ACTIVE, data->flags);
			atomic_inc(&blkif->persistent_gnt_in_use);
			return data;
		}
	}
	return NULL;
}

static void put_persistent_gnt(struct xen_blkif *blkif,
                               struct persistent_gnt *persistent_gnt)
{
	if(!test_bit(PERSISTENT_GNT_ACTIVE, persistent_gnt->flags))
	          pr_alert_ratelimited(DRV_PFX " freeing a grant already unused");
	set_bit(PERSISTENT_GNT_WAS_ACTIVE, persistent_gnt->flags);
	clear_bit(PERSISTENT_GNT_ACTIVE, persistent_gnt->flags);
	atomic_dec(&blkif->persistent_gnt_in_use);
}

static void free_persistent_gnts(struct xen_blkif *blkif, struct rb_root *root,
                                 unsigned int num)
{
	struct gnttab_unmap_grant_ref unmap[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct page *pages[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct persistent_gnt *persistent_gnt;
	struct rb_node *n;
	int ret = 0;
	int segs_to_unmap = 0;

	foreach_grant_safe(persistent_gnt, n, root, node) {
		BUG_ON(persistent_gnt->handle ==
			BLKBACK_INVALID_HANDLE);
		gnttab_set_unmap_op(&unmap[segs_to_unmap],
			(unsigned long) pfn_to_kaddr(page_to_pfn(
				persistent_gnt->page)),
			GNTMAP_host_map,
			persistent_gnt->handle);

		pages[segs_to_unmap] = persistent_gnt->page;

		if (++segs_to_unmap == BLKIF_MAX_SEGMENTS_PER_REQUEST ||
			!rb_next(&persistent_gnt->node)) {
			ret = gnttab_unmap_refs(unmap, NULL, pages,
				segs_to_unmap);
			BUG_ON(ret);
			put_free_pages(blkif, pages, segs_to_unmap);
			segs_to_unmap = 0;
		}

		rb_erase(&persistent_gnt->node, root);
		kfree(persistent_gnt);
		num--;
	}
	BUG_ON(num != 0);
}

static void unmap_purged_grants(struct work_struct *work)
{
	struct gnttab_unmap_grant_ref unmap[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct page *pages[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct persistent_gnt *persistent_gnt;
	int ret, segs_to_unmap = 0;
	struct xen_blkif *blkif = container_of(work, typeof(*blkif), persistent_purge_work);

	while(!list_empty(&blkif->persistent_purge_list)) {
		persistent_gnt = list_first_entry(&blkif->persistent_purge_list,
		                                  struct persistent_gnt,
		                                  remove_node);
		list_del(&persistent_gnt->remove_node);

		gnttab_set_unmap_op(&unmap[segs_to_unmap],
			vaddr(persistent_gnt->page),
			GNTMAP_host_map,
			persistent_gnt->handle);

		pages[segs_to_unmap] = persistent_gnt->page;

		if (++segs_to_unmap == BLKIF_MAX_SEGMENTS_PER_REQUEST) {
			ret = gnttab_unmap_refs(unmap, NULL, pages,
				segs_to_unmap);
			BUG_ON(ret);
			put_free_pages(blkif, pages, segs_to_unmap);
			segs_to_unmap = 0;
		}
		kfree(persistent_gnt);
	}
	if (segs_to_unmap > 0) {
		ret = gnttab_unmap_refs(unmap, NULL, pages, segs_to_unmap);
		BUG_ON(ret);
		put_free_pages(blkif, pages, segs_to_unmap);
	}
}

static void purge_persistent_gnt(struct xen_blkif *blkif)
{
	struct persistent_gnt *persistent_gnt;
	struct rb_node *n;
	unsigned int num_clean, total;
	bool scan_used = false;
	struct rb_root *root;

	if (blkif->persistent_gnt_c < xen_blkif_max_pgrants ||
	    (blkif->persistent_gnt_c == xen_blkif_max_pgrants &&
	    !blkif->vbd.overflow_max_grants)) {
		return;
	}

	if (work_pending(&blkif->persistent_purge_work)) {
		pr_alert_ratelimited(DRV_PFX "Scheduled work from previous purge is still pending, cannot purge list\n");
		return;
	}

	num_clean = (xen_blkif_max_pgrants / 100) * LRU_PERCENT_CLEAN;
	num_clean = blkif->persistent_gnt_c - xen_blkif_max_pgrants + num_clean;
	num_clean = min(blkif->persistent_gnt_c, num_clean);
	if (num_clean >
	    (blkif->persistent_gnt_c -
	    atomic_read(&blkif->persistent_gnt_in_use)))
		return;

	/*
	 * At this point, we can assure that there will be no calls
         * to get_persistent_grant (because we are executing this code from
         * xen_blkif_schedule), there can only be calls to put_persistent_gnt,
         * which means that the number of currently used grants will go down,
         * but never up, so we will always be able to remove the requested
         * number of grants.
	 */

	total = num_clean;

	pr_debug(DRV_PFX "Going to purge %u persistent grants\n", num_clean);

	INIT_LIST_HEAD(&blkif->persistent_purge_list);
	root = &blkif->persistent_gnts;
purge_list:
	foreach_grant_safe(persistent_gnt, n, root, node) {
		BUG_ON(persistent_gnt->handle ==
			BLKBACK_INVALID_HANDLE);

		if (test_bit(PERSISTENT_GNT_ACTIVE, persistent_gnt->flags))
			continue;
		if (!scan_used &&
		    (test_bit(PERSISTENT_GNT_WAS_ACTIVE, persistent_gnt->flags)))
			continue;

		rb_erase(&persistent_gnt->node, root);
		list_add(&persistent_gnt->remove_node,
		         &blkif->persistent_purge_list);
		if (--num_clean == 0)
			goto finished;
	}
	/*
	 * If we get here it means we also need to start cleaning
	 * grants that were used since last purge in order to cope
	 * with the requested num
	 */
	if (!scan_used) {
		pr_debug(DRV_PFX "Still missing %u purged frames\n", num_clean);
		scan_used = true;
		goto purge_list;
	}
finished:
	/* Remove the "used" flag from all the persistent grants */
	foreach_grant_safe(persistent_gnt, n, root, node) {
		BUG_ON(persistent_gnt->handle ==
			BLKBACK_INVALID_HANDLE);
		clear_bit(PERSISTENT_GNT_WAS_ACTIVE, persistent_gnt->flags);
	}
	blkif->persistent_gnt_c -= (total - num_clean);
	blkif->vbd.overflow_max_grants = 0;

	/* We can defer this work */
	INIT_WORK(&blkif->persistent_purge_work, unmap_purged_grants);
	schedule_work(&blkif->persistent_purge_work);
	pr_debug(DRV_PFX "Purged %u/%u\n", (total - num_clean), total);
	return;
}

/*
 * Retrieve from the 'pending_reqs' a free pending_req structure to be used.
 */
static struct pending_req *alloc_req(void)
{
	struct pending_req *req = NULL;
	unsigned long flags;

	spin_lock_irqsave(&blkbk->pending_free_lock, flags);
	if (!list_empty(&blkbk->pending_free)) {
		req = list_entry(blkbk->pending_free.next, struct pending_req,
				 free_list);
		list_del(&req->free_list);
	}
	spin_unlock_irqrestore(&blkbk->pending_free_lock, flags);
	return req;
}

/*
 * Return the 'pending_req' structure back to the freepool. We also
 * wake up the thread if it was waiting for a free page.
 */
static void free_req(struct pending_req *req)
{
	unsigned long flags;
	int was_empty;

	spin_lock_irqsave(&blkbk->pending_free_lock, flags);
	was_empty = list_empty(&blkbk->pending_free);
	list_add(&req->free_list, &blkbk->pending_free);
	spin_unlock_irqrestore(&blkbk->pending_free_lock, flags);
	if (was_empty)
		wake_up(&blkbk->pending_free_wq);
}

/*
 * Routines for managing virtual block devices (vbds).
 */
static int xen_vbd_translate(struct phys_req *req, struct xen_blkif *blkif,
			     int operation)
{
	struct xen_vbd *vbd = &blkif->vbd;
	int rc = -EACCES;

	if ((operation != READ) && vbd->readonly)
		goto out;

	if (likely(req->nr_sects)) {
		blkif_sector_t end = req->sector_number + req->nr_sects;

		if (unlikely(end < req->sector_number))
			goto out;
		if (unlikely(end > vbd_sz(vbd)))
			goto out;
	}

	req->dev  = vbd->pdevice;
	req->bdev = vbd->bdev;
	rc = 0;

 out:
	return rc;
}

static void xen_vbd_resize(struct xen_blkif *blkif)
{
	struct xen_vbd *vbd = &blkif->vbd;
	struct xenbus_transaction xbt;
	int err;
	struct xenbus_device *dev = xen_blkbk_xenbus(blkif->be);
	unsigned long long new_size = vbd_sz(vbd);

	pr_info(DRV_PFX "VBD Resize: Domid: %d, Device: (%d, %d)\n",
		blkif->domid, MAJOR(vbd->pdevice), MINOR(vbd->pdevice));
	pr_info(DRV_PFX "VBD Resize: new size %llu\n", new_size);
	vbd->size = new_size;
again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		pr_warn(DRV_PFX "Error starting transaction");
		return;
	}
	err = xenbus_printf(xbt, dev->nodename, "sectors", "%llu",
			    (unsigned long long)vbd_sz(vbd));
	if (err) {
		pr_warn(DRV_PFX "Error writing new size");
		goto abort;
	}
	/*
	 * Write the current state; we will use this to synchronize
	 * the front-end. If the current state is "connected" the
	 * front-end will get the new size information online.
	 */
	err = xenbus_printf(xbt, dev->nodename, "state", "%d", dev->state);
	if (err) {
		pr_warn(DRV_PFX "Error writing the state");
		goto abort;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN)
		goto again;
	if (err)
		pr_warn(DRV_PFX "Error ending transaction");
	return;
abort:
	xenbus_transaction_end(xbt, 1);
}

/*
 * Notification from the guest OS.
 */
static void blkif_notify_work(struct xen_blkif *blkif)
{
	blkif->waiting_reqs = 1;
	wake_up(&blkif->wq);
}

irqreturn_t xen_blkif_be_int(int irq, void *dev_id)
{
	blkif_notify_work(dev_id);
	return IRQ_HANDLED;
}

/*
 * SCHEDULER FUNCTIONS
 */

static void print_stats(struct xen_blkif *blkif)
{
	pr_info("xen-blkback (%s): oo %3llu  |  rd %4llu  |  wr %4llu  |  f %4llu"
		 "  |  ds %4llu | pg: %4u/%4d\n",
		 current->comm, blkif->st_oo_req,
		 blkif->st_rd_req, blkif->st_wr_req,
		 blkif->st_f_req, blkif->st_ds_req,
		 blkif->persistent_gnt_c,
		 xen_blkif_max_pgrants);
	blkif->st_print = jiffies + msecs_to_jiffies(10 * 1000);
	blkif->st_rd_req = 0;
	blkif->st_wr_req = 0;
	blkif->st_oo_req = 0;
	blkif->st_ds_req = 0;
}

int xen_blkif_schedule(void *arg)
{
	struct xen_blkif *blkif = arg;
	struct xen_vbd *vbd = &blkif->vbd;
	unsigned long timeout;

	xen_blkif_get(blkif);

	while (!kthread_should_stop()) {
		if (try_to_freeze())
			continue;
		if (unlikely(vbd->size != vbd_sz(vbd)))
			xen_vbd_resize(blkif);

		timeout = msecs_to_jiffies(LRU_INTERVAL);

		timeout = wait_event_interruptible_timeout(
			blkif->wq,
			blkif->waiting_reqs || kthread_should_stop(),
			timeout);
		if (timeout == 0)
			goto purge_gnt_list;
		timeout = wait_event_interruptible_timeout(
			blkbk->pending_free_wq,
			!list_empty(&blkbk->pending_free) ||
			kthread_should_stop(),
			timeout);
		if (timeout == 0)
			goto purge_gnt_list;

		blkif->waiting_reqs = 0;
		smp_mb(); /* clear flag *before* checking for work */

		if (do_block_io_op(blkif))
			blkif->waiting_reqs = 1;

purge_gnt_list:
		if (blkif->vbd.feature_gnt_persistent &&
		    time_after(jiffies, blkif->next_lru)) {
			purge_persistent_gnt(blkif);
			blkif->next_lru = jiffies + msecs_to_jiffies(LRU_INTERVAL);
		}

		/* Shrink if we have more than xen_blkif_max_buffer_pages */
		shrink_free_pagepool(blkif, xen_blkif_max_buffer_pages);

		if (log_stats && time_after(jiffies, blkif->st_print))
			print_stats(blkif);
	}

	/* Since we are shutting down remove all pages from the buffer */
	shrink_free_pagepool(blkif, 0 /* All */);

	/* Free all persistent grant pages */
	if (!RB_EMPTY_ROOT(&blkif->persistent_gnts))
		free_persistent_gnts(blkif, &blkif->persistent_gnts,
			blkif->persistent_gnt_c);

	BUG_ON(!RB_EMPTY_ROOT(&blkif->persistent_gnts));
	blkif->persistent_gnt_c = 0;

	if (log_stats)
		print_stats(blkif);

	blkif->xenblkd = NULL;
	xen_blkif_put(blkif);

	return 0;
}

struct seg_buf {
	unsigned int offset;
	unsigned int nsec;
};
/*
 * Unmap the grant references, and also remove the M2P over-rides
 * used in the 'pending_req'.
 */
static void xen_blkbk_unmap(struct pending_req *req)
{
	struct gnttab_unmap_grant_ref unmap[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct page *pages[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	unsigned int i, invcount = 0;
	grant_handle_t handle;
	struct xen_blkif *blkif = req->blkif;
	int ret;

	for (i = 0; i < req->nr_pages; i++) {
		if (req->persistent_gnts[i] != NULL) {
			put_persistent_gnt(blkif, req->persistent_gnts[i]);
			continue;
		}
		handle = pending_handle(req, i);
		pages[invcount] = req->pages[i];
		if (handle == BLKBACK_INVALID_HANDLE)
			continue;
		gnttab_set_unmap_op(&unmap[invcount], vaddr(pages[invcount]),
				    GNTMAP_host_map, handle);
		pending_handle(req, i) = BLKBACK_INVALID_HANDLE;
		invcount++;
	}

	ret = gnttab_unmap_refs(unmap, NULL, pages, invcount);
	BUG_ON(ret);
	put_free_pages(blkif, pages, invcount);
}

static int xen_blkbk_map(struct blkif_request *req,
			 struct pending_req *pending_req,
			 struct seg_buf seg[],
			 struct page *pages[])
{
	struct gnttab_map_grant_ref map[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct page *pages_to_gnt[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct persistent_gnt **persistent_gnts = pending_req->persistent_gnts;
	struct persistent_gnt *persistent_gnt = NULL;
	struct xen_blkif *blkif = pending_req->blkif;
	phys_addr_t addr = 0;
	int i, seg_idx, new_map_idx;
	int nseg = req->u.rw.nr_segments;
	int segs_to_map = 0;
	int ret = 0;
	int use_persistent_gnts;

	use_persistent_gnts = (blkif->vbd.feature_gnt_persistent);

	/*
	 * Fill out preq.nr_sects with proper amount of sectors, and setup
	 * assign map[..] with the PFN of the page in our domain with the
	 * corresponding grant reference for each page.
	 */
	for (i = 0; i < nseg; i++) {
		uint32_t flags;

		if (use_persistent_gnts)
			persistent_gnt = get_persistent_gnt(
				blkif,
				req->u.rw.seg[i].gref);

		if (persistent_gnt) {
			/*
			 * We are using persistent grants and
			 * the grant is already mapped
			 */
			pages[i] = persistent_gnt->page;
			persistent_gnts[i] = persistent_gnt;
		} else {
			if (get_free_page(blkif, &pages[i]))
				goto out_of_memory;
			addr = vaddr(pages[i]);
			pages_to_gnt[segs_to_map] = pages[i];
			persistent_gnts[i] = NULL;
			flags = GNTMAP_host_map;
			if (!use_persistent_gnts &&
			    (pending_req->operation != BLKIF_OP_READ))
				flags |= GNTMAP_readonly;
			gnttab_set_map_op(&map[segs_to_map++], addr,
					  flags, req->u.rw.seg[i].gref,
					  blkif->domid);
		}
	}

	if (segs_to_map) {
		ret = gnttab_map_refs(map, NULL, pages_to_gnt, segs_to_map);
		BUG_ON(ret);
	}

	/*
	 * Now swizzle the MFN in our domain with the MFN from the other domain
	 * so that when we access vaddr(pending_req,i) it has the contents of
	 * the page from the other domain.
	 */
	for (seg_idx = 0, new_map_idx = 0; seg_idx < nseg; seg_idx++) {
		if (!persistent_gnts[seg_idx]) {
			/* This is a newly mapped grant */
			BUG_ON(new_map_idx >= segs_to_map);
			if (unlikely(map[new_map_idx].status != 0)) {
				pr_debug(DRV_PFX "invalid buffer -- could not remap it\n");
				pending_handle(pending_req, seg_idx) = BLKBACK_INVALID_HANDLE;
				ret |= 1;
				new_map_idx++;
				/*
				 * No need to set unmap_seg bit, since
				 * we can not unmap this grant because
				 * the handle is invalid.
				 */
				continue;
			}
			pending_handle(pending_req, seg_idx) = map[new_map_idx].handle;
		} else {
			/* This grant is persistent and already mapped */
			goto next;
		}
		if (use_persistent_gnts &&
		    blkif->persistent_gnt_c < xen_blkif_max_pgrants) {
			/*
			 * We are using persistent grants, the grant is
			 * not mapped but we might have room for it.
			 */
			persistent_gnt = kmalloc(sizeof(struct persistent_gnt),
				                 GFP_KERNEL);
			if (!persistent_gnt) {
				/*
				 * If we don't have enough memory to
				 * allocate the persistent_gnt struct
				 * map this grant non-persistenly
				 */
				goto next_unmap;
			}
			persistent_gnt->gnt = map[new_map_idx].ref;
			persistent_gnt->handle = map[new_map_idx].handle;
			persistent_gnt->page = pages[seg_idx];
			if (add_persistent_gnt(blkif,
			                       persistent_gnt)) {
				kfree(persistent_gnt);
				persistent_gnt = NULL;
				goto next_unmap;
			}
			persistent_gnts[seg_idx] = persistent_gnt;
			pr_debug(DRV_PFX " grant %u added to the tree of persistent grants, using %u/%u\n",
				 persistent_gnt->gnt, blkif->persistent_gnt_c,
				 xen_blkif_max_pgrants);
			new_map_idx++;
			goto next;
		}
		if (use_persistent_gnts && !blkif->vbd.overflow_max_grants) {
			blkif->vbd.overflow_max_grants = 1;
			pr_debug(DRV_PFX " domain %u, device %#x is using maximum number of persistent grants\n",
			         blkif->domid, blkif->vbd.handle);
		}
next_unmap:
		/*
		 * We could not map this grant persistently, so use it as
		 * a non-persistent grant.
		 */
		new_map_idx++;
next:
		seg[seg_idx].offset = (req->u.rw.seg[seg_idx].first_sect << 9);
	}
	return ret;

out_of_memory:
	pr_alert(DRV_PFX "%s: out of memory\n", __func__);
	put_free_pages(blkif, pages_to_gnt, segs_to_map);
	return -ENOMEM;
}

static int dispatch_discard_io(struct xen_blkif *blkif,
				struct blkif_request *req)
{
	int err = 0;
	int status = BLKIF_RSP_OKAY;
	struct block_device *bdev = blkif->vbd.bdev;
	unsigned long secure;

	blkif->st_ds_req++;

	xen_blkif_get(blkif);
	secure = (blkif->vbd.discard_secure &&
		 (req->u.discard.flag & BLKIF_DISCARD_SECURE)) ?
		 BLKDEV_DISCARD_SECURE : 0;

	err = blkdev_issue_discard(bdev, req->u.discard.sector_number,
				   req->u.discard.nr_sectors,
				   GFP_KERNEL, secure);

	if (err == -EOPNOTSUPP) {
		pr_debug(DRV_PFX "discard op failed, not supported\n");
		status = BLKIF_RSP_EOPNOTSUPP;
	} else if (err)
		status = BLKIF_RSP_ERROR;

	make_response(blkif, req->u.discard.id, req->operation, status);
	xen_blkif_put(blkif);
	return err;
}

static int dispatch_other_io(struct xen_blkif *blkif,
			     struct blkif_request *req,
			     struct pending_req *pending_req)
{
	free_req(pending_req);
	make_response(blkif, req->u.other.id, req->operation,
		      BLKIF_RSP_EOPNOTSUPP);
	return -EIO;
}

static void xen_blk_drain_io(struct xen_blkif *blkif)
{
	atomic_set(&blkif->drain, 1);
	do {
		/* The initial value is one, and one refcnt taken at the
		 * start of the xen_blkif_schedule thread. */
		if (atomic_read(&blkif->refcnt) <= 2)
			break;
		wait_for_completion_interruptible_timeout(
				&blkif->drain_complete, HZ);

		if (!atomic_read(&blkif->drain))
			break;
	} while (!kthread_should_stop());
	atomic_set(&blkif->drain, 0);
}

/*
 * Completion callback on the bio's. Called as bh->b_end_io()
 */

static void __end_block_io_op(struct pending_req *pending_req, int error)
{
	/* An error fails the entire request. */
	if ((pending_req->operation == BLKIF_OP_FLUSH_DISKCACHE) &&
	    (error == -EOPNOTSUPP)) {
		pr_debug(DRV_PFX "flush diskcache op failed, not supported\n");
		xen_blkbk_flush_diskcache(XBT_NIL, pending_req->blkif->be, 0);
		pending_req->status = BLKIF_RSP_EOPNOTSUPP;
	} else if ((pending_req->operation == BLKIF_OP_WRITE_BARRIER) &&
		    (error == -EOPNOTSUPP)) {
		pr_debug(DRV_PFX "write barrier op failed, not supported\n");
		xen_blkbk_barrier(XBT_NIL, pending_req->blkif->be, 0);
		pending_req->status = BLKIF_RSP_EOPNOTSUPP;
	} else if (error) {
		pr_debug(DRV_PFX "Buffer not up-to-date at end of operation,"
			 " error=%d\n", error);
		pending_req->status = BLKIF_RSP_ERROR;
	}

	/*
	 * If all of the bio's have completed it is time to unmap
	 * the grant references associated with 'request' and provide
	 * the proper response on the ring.
	 */
	if (atomic_dec_and_test(&pending_req->pendcnt)) {
		xen_blkbk_unmap(pending_req);
		make_response(pending_req->blkif, pending_req->id,
			      pending_req->operation, pending_req->status);
		xen_blkif_put(pending_req->blkif);
		if (atomic_read(&pending_req->blkif->refcnt) <= 2) {
			if (atomic_read(&pending_req->blkif->drain))
				complete(&pending_req->blkif->drain_complete);
		}
		free_req(pending_req);
	}
}

/*
 * bio callback.
 */
static void end_block_io_op(struct bio *bio, int error)
{
	__end_block_io_op(bio->bi_private, error);
	bio_put(bio);
}



/*
 * Function to copy the from the ring buffer the 'struct blkif_request'
 * (which has the sectors we want, number of them, grant references, etc),
 * and transmute  it to the block API to hand it over to the proper block disk.
 */
static int
__do_block_io_op(struct xen_blkif *blkif)
{
	union blkif_back_rings *blk_rings = &blkif->blk_rings;
	struct blkif_request req;
	struct pending_req *pending_req;
	RING_IDX rc, rp;
	int more_to_do = 0;

	rc = blk_rings->common.req_cons;
	rp = blk_rings->common.sring->req_prod;
	rmb(); /* Ensure we see queued requests up to 'rp'. */

	while (rc != rp) {

		if (RING_REQUEST_CONS_OVERFLOW(&blk_rings->common, rc))
			break;

		if (kthread_should_stop()) {
			more_to_do = 1;
			break;
		}

		pending_req = alloc_req();
		if (NULL == pending_req) {
			blkif->st_oo_req++;
			more_to_do = 1;
			break;
		}

		switch (blkif->blk_protocol) {
		case BLKIF_PROTOCOL_NATIVE:
			memcpy(&req, RING_GET_REQUEST(&blk_rings->native, rc), sizeof(req));
			break;
		case BLKIF_PROTOCOL_X86_32:
			blkif_get_x86_32_req(&req, RING_GET_REQUEST(&blk_rings->x86_32, rc));
			break;
		case BLKIF_PROTOCOL_X86_64:
			blkif_get_x86_64_req(&req, RING_GET_REQUEST(&blk_rings->x86_64, rc));
			break;
		default:
			BUG();
		}
		blk_rings->common.req_cons = ++rc; /* before make_response() */

		/* Apply all sanity checks to /private copy/ of request. */
		barrier();

		switch (req.operation) {
		case BLKIF_OP_READ:
		case BLKIF_OP_WRITE:
		case BLKIF_OP_WRITE_BARRIER:
		case BLKIF_OP_FLUSH_DISKCACHE:
			if (dispatch_rw_block_io(blkif, &req, pending_req))
				goto done;
			break;
		case BLKIF_OP_DISCARD:
			free_req(pending_req);
			if (dispatch_discard_io(blkif, &req))
				goto done;
			break;
		default:
			if (dispatch_other_io(blkif, &req, pending_req))
				goto done;
			break;
		}

		/* Yield point for this unbounded loop. */
		cond_resched();
	}
done:
	return more_to_do;
}

static int
do_block_io_op(struct xen_blkif *blkif)
{
	union blkif_back_rings *blk_rings = &blkif->blk_rings;
	int more_to_do;

	do {
		more_to_do = __do_block_io_op(blkif);
		if (more_to_do)
			break;

		RING_FINAL_CHECK_FOR_REQUESTS(&blk_rings->common, more_to_do);
	} while (more_to_do);

	return more_to_do;
}
/*
 * Transmutation of the 'struct blkif_request' to a proper 'struct bio'
 * and call the 'submit_bio' to pass it to the underlying storage.
 */
static int dispatch_rw_block_io(struct xen_blkif *blkif,
				struct blkif_request *req,
				struct pending_req *pending_req)
{
	struct phys_req preq;
	struct seg_buf seg[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	unsigned int nseg;
	struct bio *bio = NULL;
	struct bio *biolist[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	int i, nbio = 0;
	int operation;
	struct blk_plug plug;
	bool drain = false;
	struct page **pages = pending_req->pages;

	switch (req->operation) {
	case BLKIF_OP_READ:
		blkif->st_rd_req++;
		operation = READ;
		break;
	case BLKIF_OP_WRITE:
		blkif->st_wr_req++;
		operation = WRITE_ODIRECT;
		break;
	case BLKIF_OP_WRITE_BARRIER:
		drain = true;
	case BLKIF_OP_FLUSH_DISKCACHE:
		blkif->st_f_req++;
		operation = WRITE_FLUSH;
		break;
	default:
		operation = 0; /* make gcc happy */
		goto fail_response;
		break;
	}

	/* Check that the number of segments is sane. */
	nseg = req->u.rw.nr_segments;

	if (unlikely(nseg == 0 && operation != WRITE_FLUSH) ||
	    unlikely(nseg > BLKIF_MAX_SEGMENTS_PER_REQUEST)) {
		pr_debug(DRV_PFX "Bad number of segments in request (%d)\n",
			 nseg);
		/* Haven't submitted any bio's yet. */
		goto fail_response;
	}

	preq.sector_number = req->u.rw.sector_number;
	preq.nr_sects      = 0;

	pending_req->blkif     = blkif;
	pending_req->id        = req->u.rw.id;
	pending_req->operation = req->operation;
	pending_req->status    = BLKIF_RSP_OKAY;
	pending_req->nr_pages  = nseg;

	for (i = 0; i < nseg; i++) {
		seg[i].nsec = req->u.rw.seg[i].last_sect -
			req->u.rw.seg[i].first_sect + 1;
		if ((req->u.rw.seg[i].last_sect >= (PAGE_SIZE >> 9)) ||
		    (req->u.rw.seg[i].last_sect < req->u.rw.seg[i].first_sect))
			goto fail_response;
		preq.nr_sects += seg[i].nsec;

	}

	if (xen_vbd_translate(&preq, blkif, operation) != 0) {
		pr_debug(DRV_PFX "access denied: %s of [%llu,%llu] on dev=%04x\n",
			 operation == READ ? "read" : "write",
			 preq.sector_number,
			 preq.sector_number + preq.nr_sects,
			 blkif->vbd.pdevice);
		goto fail_response;
	}

	/*
	 * This check _MUST_ be done after xen_vbd_translate as the preq.bdev
	 * is set there.
	 */
	for (i = 0; i < nseg; i++) {
		if (((int)preq.sector_number|(int)seg[i].nsec) &
		    ((bdev_logical_block_size(preq.bdev) >> 9) - 1)) {
			pr_debug(DRV_PFX "Misaligned I/O request from domain %d",
				 blkif->domid);
			goto fail_response;
		}
	}

	/* Wait on all outstanding I/O's and once that has been completed
	 * issue the WRITE_FLUSH.
	 */
	if (drain)
		xen_blk_drain_io(pending_req->blkif);

	/*
	 * If we have failed at this point, we need to undo the M2P override,
	 * set gnttab_set_unmap_op on all of the grant references and perform
	 * the hypercall to unmap the grants - that is all done in
	 * xen_blkbk_unmap.
	 */
	if (xen_blkbk_map(req, pending_req, seg, pages))
		goto fail_flush;

	/*
	 * This corresponding xen_blkif_put is done in __end_block_io_op, or
	 * below (in "!bio") if we are handling a BLKIF_OP_DISCARD.
	 */
	xen_blkif_get(blkif);

	for (i = 0; i < nseg; i++) {
		while ((bio == NULL) ||
		       (bio_add_page(bio,
				     pages[i],
				     seg[i].nsec << 9,
				     seg[i].offset) == 0)) {

			bio = bio_alloc(GFP_KERNEL, nseg-i);
			if (unlikely(bio == NULL))
				goto fail_put_bio;

			biolist[nbio++] = bio;
			bio->bi_bdev    = preq.bdev;
			bio->bi_private = pending_req;
			bio->bi_end_io  = end_block_io_op;
			bio->bi_sector  = preq.sector_number;
		}

		preq.sector_number += seg[i].nsec;
	}

	/* This will be hit if the operation was a flush or discard. */
	if (!bio) {
		BUG_ON(operation != WRITE_FLUSH);

		bio = bio_alloc(GFP_KERNEL, 0);
		if (unlikely(bio == NULL))
			goto fail_put_bio;

		biolist[nbio++] = bio;
		bio->bi_bdev    = preq.bdev;
		bio->bi_private = pending_req;
		bio->bi_end_io  = end_block_io_op;
	}

	atomic_set(&pending_req->pendcnt, nbio);
	blk_start_plug(&plug);

	for (i = 0; i < nbio; i++)
		submit_bio(operation, biolist[i]);

	/* Let the I/Os go.. */
	blk_finish_plug(&plug);

	if (operation == READ)
		blkif->st_rd_sect += preq.nr_sects;
	else if (operation & WRITE)
		blkif->st_wr_sect += preq.nr_sects;

	return 0;

 fail_flush:
	xen_blkbk_unmap(pending_req);
 fail_response:
	/* Haven't submitted any bio's yet. */
	make_response(blkif, req->u.rw.id, req->operation, BLKIF_RSP_ERROR);
	free_req(pending_req);
	msleep(1); /* back off a bit */
	return -EIO;

 fail_put_bio:
	for (i = 0; i < nbio; i++)
		bio_put(biolist[i]);
	atomic_set(&pending_req->pendcnt, 1);
	__end_block_io_op(pending_req, -EINVAL);
	msleep(1); /* back off a bit */
	return -EIO;
}



/*
 * Put a response on the ring on how the operation fared.
 */
static void make_response(struct xen_blkif *blkif, u64 id,
			  unsigned short op, int st)
{
	struct blkif_response  resp;
	unsigned long     flags;
	union blkif_back_rings *blk_rings = &blkif->blk_rings;
	int notify;

	resp.id        = id;
	resp.operation = op;
	resp.status    = st;

	spin_lock_irqsave(&blkif->blk_ring_lock, flags);
	/* Place on the response ring for the relevant domain. */
	switch (blkif->blk_protocol) {
	case BLKIF_PROTOCOL_NATIVE:
		memcpy(RING_GET_RESPONSE(&blk_rings->native, blk_rings->native.rsp_prod_pvt),
		       &resp, sizeof(resp));
		break;
	case BLKIF_PROTOCOL_X86_32:
		memcpy(RING_GET_RESPONSE(&blk_rings->x86_32, blk_rings->x86_32.rsp_prod_pvt),
		       &resp, sizeof(resp));
		break;
	case BLKIF_PROTOCOL_X86_64:
		memcpy(RING_GET_RESPONSE(&blk_rings->x86_64, blk_rings->x86_64.rsp_prod_pvt),
		       &resp, sizeof(resp));
		break;
	default:
		BUG();
	}
	blk_rings->common.rsp_prod_pvt++;
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&blk_rings->common, notify);
	spin_unlock_irqrestore(&blkif->blk_ring_lock, flags);
	if (notify)
		notify_remote_via_irq(blkif->irq);
}

static int __init xen_blkif_init(void)
{
	int i, mmap_pages;
	int rc = 0;

	if (!xen_domain())
		return -ENODEV;

	blkbk = kzalloc(sizeof(struct xen_blkbk), GFP_KERNEL);
	if (!blkbk) {
		pr_alert(DRV_PFX "%s: out of memory!\n", __func__);
		return -ENOMEM;
	}

	mmap_pages = xen_blkif_reqs * BLKIF_MAX_SEGMENTS_PER_REQUEST;

	blkbk->pending_reqs          = kzalloc(sizeof(blkbk->pending_reqs[0]) *
					xen_blkif_reqs, GFP_KERNEL);
	blkbk->pending_grant_handles = kmalloc(sizeof(blkbk->pending_grant_handles[0]) *
					mmap_pages, GFP_KERNEL);

	if (!blkbk->pending_reqs || !blkbk->pending_grant_handles) {
		rc = -ENOMEM;
		goto out_of_memory;
	}

	for (i = 0; i < mmap_pages; i++) {
		blkbk->pending_grant_handles[i] = BLKBACK_INVALID_HANDLE;
	}
	rc = xen_blkif_interface_init();
	if (rc)
		goto failed_init;

	INIT_LIST_HEAD(&blkbk->pending_free);
	spin_lock_init(&blkbk->pending_free_lock);
	init_waitqueue_head(&blkbk->pending_free_wq);

	for (i = 0; i < xen_blkif_reqs; i++)
		list_add_tail(&blkbk->pending_reqs[i].free_list,
			      &blkbk->pending_free);

	rc = xen_blkif_xenbus_init();
	if (rc)
		goto failed_init;

	return 0;

 out_of_memory:
	pr_alert(DRV_PFX "%s: out of memory\n", __func__);
 failed_init:
	kfree(blkbk->pending_reqs);
	kfree(blkbk->pending_grant_handles);
	kfree(blkbk);
	blkbk = NULL;
	return rc;
}

module_init(xen_blkif_init);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("xen-backend:vbd");
