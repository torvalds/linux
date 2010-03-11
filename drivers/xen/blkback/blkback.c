/******************************************************************************
 * arch/xen/drivers/blkif/backend/main.c
 *
 * Back-end of the driver for virtual block devices. This portion of the
 * driver exports a 'unified' block-device interface that can be accessed
 * by any operating system that implements a compatible front end. A
 * reference front-end implementation can be found in:
 *  arch/xen/drivers/blkif/frontend
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

#include <xen/balloon.h>
#include <xen/events.h>
#include <xen/page.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include "common.h"

/*
 * These are rather arbitrary. They are fairly large because adjacent requests
 * pulled from a communication ring are quite likely to end up being part of
 * the same scatter/gather request at the disc.
 *
 * ** TRY INCREASING 'blkif_reqs' IF WRITE SPEEDS SEEM TOO LOW **
 *
 * This will increase the chances of being able to write whole tracks.
 * 64 should be enough to keep us competitive with Linux.
 */
static int blkif_reqs = 64;
module_param_named(reqs, blkif_reqs, int, 0);
MODULE_PARM_DESC(reqs, "Number of blkback requests to allocate");

/* Run-time switchable: /sys/module/blkback/parameters/ */
static unsigned int log_stats = 0;
static unsigned int debug_lvl = 0;
module_param(log_stats, int, 0644);
module_param(debug_lvl, int, 0644);

/*
 * Each outstanding request that we've passed to the lower device layers has a
 * 'pending_req' allocated to it. Each buffer_head that completes decrements
 * the pendcnt towards zero. When it hits zero, the specified domain has a
 * response queued for it, with the saved 'id' passed back.
 */
typedef struct {
	blkif_t       *blkif;
	u64            id;
	int            nr_pages;
	atomic_t       pendcnt;
	unsigned short operation;
	int            status;
	struct list_head free_list;
} pending_req_t;

static pending_req_t *pending_reqs;
static struct list_head pending_free;
static DEFINE_SPINLOCK(pending_free_lock);
static DECLARE_WAIT_QUEUE_HEAD(pending_free_wq);

#define BLKBACK_INVALID_HANDLE (~0)

static struct page **pending_pages;
static grant_handle_t *pending_grant_handles;

static inline int vaddr_pagenr(pending_req_t *req, int seg)
{
	return (req - pending_reqs) * BLKIF_MAX_SEGMENTS_PER_REQUEST + seg;
}

static inline unsigned long vaddr(pending_req_t *req, int seg)
{
	unsigned long pfn = page_to_pfn(pending_pages[vaddr_pagenr(req, seg)]);
	return (unsigned long)pfn_to_kaddr(pfn);
}

#define pending_handle(_req, _seg) \
	(pending_grant_handles[vaddr_pagenr(_req, _seg)])


static int do_block_io_op(blkif_t *blkif);
static void dispatch_rw_block_io(blkif_t *blkif,
				 struct blkif_request *req,
				 pending_req_t *pending_req);
static void make_response(blkif_t *blkif, u64 id,
			  unsigned short op, int st);

/******************************************************************
 * misc small helpers
 */
static pending_req_t* alloc_req(void)
{
	pending_req_t *req = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pending_free_lock, flags);
	if (!list_empty(&pending_free)) {
		req = list_entry(pending_free.next, pending_req_t, free_list);
		list_del(&req->free_list);
	}
	spin_unlock_irqrestore(&pending_free_lock, flags);
	return req;
}

static void free_req(pending_req_t *req)
{
	unsigned long flags;
	int was_empty;

	spin_lock_irqsave(&pending_free_lock, flags);
	was_empty = list_empty(&pending_free);
	list_add(&req->free_list, &pending_free);
	spin_unlock_irqrestore(&pending_free_lock, flags);
	if (was_empty)
		wake_up(&pending_free_wq);
}

static void unplug_queue(blkif_t *blkif)
{
	if (blkif->plug == NULL)
		return;
	if (blkif->plug->unplug_fn)
		blkif->plug->unplug_fn(blkif->plug);
	blk_put_queue(blkif->plug);
	blkif->plug = NULL;
}

static void plug_queue(blkif_t *blkif, struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	if (q == blkif->plug)
		return;
	unplug_queue(blkif);
	blk_get_queue(q);
	blkif->plug = q;
}

static void fast_flush_area(pending_req_t *req)
{
	struct gnttab_unmap_grant_ref unmap[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	unsigned int i, invcount = 0;
	grant_handle_t handle;
	int ret;

	for (i = 0; i < req->nr_pages; i++) {
		handle = pending_handle(req, i);
		if (handle == BLKBACK_INVALID_HANDLE)
			continue;
		gnttab_set_unmap_op(&unmap[invcount], vaddr(req, i),
				    GNTMAP_host_map, handle);
		pending_handle(req, i) = BLKBACK_INVALID_HANDLE;
		invcount++;
	}

	ret = HYPERVISOR_grant_table_op(
		GNTTABOP_unmap_grant_ref, unmap, invcount);
	BUG_ON(ret);
}

/******************************************************************
 * SCHEDULER FUNCTIONS
 */

static void print_stats(blkif_t *blkif)
{
	printk(KERN_DEBUG "%s: oo %3d  |  rd %4d  |  wr %4d  |  br %4d\n",
	       current->comm, blkif->st_oo_req,
	       blkif->st_rd_req, blkif->st_wr_req, blkif->st_br_req);
	blkif->st_print = jiffies + msecs_to_jiffies(10 * 1000);
	blkif->st_rd_req = 0;
	blkif->st_wr_req = 0;
	blkif->st_oo_req = 0;
}

int blkif_schedule(void *arg)
{
	blkif_t *blkif = arg;
	struct vbd *vbd = &blkif->vbd;

	blkif_get(blkif);

	if (debug_lvl)
		printk(KERN_DEBUG "%s: started\n", current->comm);

	while (!kthread_should_stop()) {
		if (try_to_freeze())
			continue;
		if (unlikely(vbd->size != vbd_size(vbd)))
			vbd_resize(blkif);

		wait_event_interruptible(
			blkif->wq,
			blkif->waiting_reqs || kthread_should_stop());
		wait_event_interruptible(
			pending_free_wq,
			!list_empty(&pending_free) || kthread_should_stop());

		blkif->waiting_reqs = 0;
		smp_mb(); /* clear flag *before* checking for work */

		if (do_block_io_op(blkif))
			blkif->waiting_reqs = 1;
		unplug_queue(blkif);

		if (log_stats && time_after(jiffies, blkif->st_print))
			print_stats(blkif);
	}

	if (log_stats)
		print_stats(blkif);
	if (debug_lvl)
		printk(KERN_DEBUG "%s: exiting\n", current->comm);

	blkif->xenblkd = NULL;
	blkif_put(blkif);

	return 0;
}

/******************************************************************
 * COMPLETION CALLBACK -- Called as bh->b_end_io()
 */

static void __end_block_io_op(pending_req_t *pending_req, int error)
{
	/* An error fails the entire request. */
	if ((pending_req->operation == BLKIF_OP_WRITE_BARRIER) &&
	    (error == -EOPNOTSUPP)) {
		DPRINTK("blkback: write barrier op failed, not supported\n");
		blkback_barrier(XBT_NIL, pending_req->blkif->be, 0);
		pending_req->status = BLKIF_RSP_EOPNOTSUPP;
	} else if (error) {
		DPRINTK("Buffer not up-to-date at end of operation, "
			"error=%d\n", error);
		pending_req->status = BLKIF_RSP_ERROR;
	}

	if (atomic_dec_and_test(&pending_req->pendcnt)) {
		fast_flush_area(pending_req);
		make_response(pending_req->blkif, pending_req->id,
			      pending_req->operation, pending_req->status);
		blkif_put(pending_req->blkif);
		free_req(pending_req);
	}
}

static void end_block_io_op(struct bio *bio, int error)
{
	__end_block_io_op(bio->bi_private, error);
	bio_put(bio);
}


/******************************************************************************
 * NOTIFICATION FROM GUEST OS.
 */

static void blkif_notify_work(blkif_t *blkif)
{
	blkif->waiting_reqs = 1;
	wake_up(&blkif->wq);
}

irqreturn_t blkif_be_int(int irq, void *dev_id)
{
	blkif_notify_work(dev_id);
	return IRQ_HANDLED;
}



/******************************************************************
 * DOWNWARD CALLS -- These interface with the block-device layer proper.
 */

static int do_block_io_op(blkif_t *blkif)
{
	union blkif_back_rings *blk_rings = &blkif->blk_rings;
	struct blkif_request req;
	pending_req_t *pending_req;
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
			blkif->st_rd_req++;
			dispatch_rw_block_io(blkif, &req, pending_req);
			break;
		case BLKIF_OP_WRITE_BARRIER:
			blkif->st_br_req++;
			/* fall through */
		case BLKIF_OP_WRITE:
			blkif->st_wr_req++;
			dispatch_rw_block_io(blkif, &req, pending_req);
			break;
		default:
			/* A good sign something is wrong: sleep for a while to
			 * avoid excessive CPU consumption by a bad guest. */
			msleep(1);
			DPRINTK("error: unknown block io operation [%d]\n",
				req.operation);
			make_response(blkif, req.id, req.operation,
				      BLKIF_RSP_ERROR);
			free_req(pending_req);
			break;
		}

		/* Yield point for this unbounded loop. */
		cond_resched();
	}

	return more_to_do;
}

static void dispatch_rw_block_io(blkif_t *blkif,
				 struct blkif_request *req,
				 pending_req_t *pending_req)
{
	struct gnttab_map_grant_ref map[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct phys_req preq;
	struct {
		unsigned long buf; unsigned int nsec;
	} seg[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	unsigned int nseg;
	struct bio *bio = NULL;
	int ret, i;
	int operation;

	switch (req->operation) {
	case BLKIF_OP_READ:
		operation = READ;
		break;
	case BLKIF_OP_WRITE:
		operation = WRITE;
		break;
	case BLKIF_OP_WRITE_BARRIER:
		operation = WRITE_BARRIER;
		break;
	default:
		operation = 0; /* make gcc happy */
		BUG();
	}

	/* Check that number of segments is sane. */
	nseg = req->nr_segments;
	if (unlikely(nseg == 0 && operation != WRITE_BARRIER) ||
	    unlikely(nseg > BLKIF_MAX_SEGMENTS_PER_REQUEST)) {
		DPRINTK("Bad number of segments in request (%d)\n", nseg);
		goto fail_response;
	}

	preq.dev           = req->handle;
	preq.sector_number = req->sector_number;
	preq.nr_sects      = 0;

	pending_req->blkif     = blkif;
	pending_req->id        = req->id;
	pending_req->operation = req->operation;
	pending_req->status    = BLKIF_RSP_OKAY;
	pending_req->nr_pages  = nseg;

	for (i = 0; i < nseg; i++) {
		uint32_t flags;

		seg[i].nsec = req->seg[i].last_sect -
			req->seg[i].first_sect + 1;

		if ((req->seg[i].last_sect >= (PAGE_SIZE >> 9)) ||
		    (req->seg[i].last_sect < req->seg[i].first_sect))
			goto fail_response;
		preq.nr_sects += seg[i].nsec;

		flags = GNTMAP_host_map;
		if (operation != READ)
			flags |= GNTMAP_readonly;
		gnttab_set_map_op(&map[i], vaddr(pending_req, i), flags,
				  req->seg[i].gref, blkif->domid);
	}

	ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, map, nseg);
	BUG_ON(ret);

	for (i = 0; i < nseg; i++) {
		if (unlikely(map[i].status != 0)) {
			DPRINTK("invalid buffer -- could not remap it\n");
			map[i].handle = BLKBACK_INVALID_HANDLE;
			ret |= 1;
		}

		pending_handle(pending_req, i) = map[i].handle;

		if (ret)
			continue;

		set_phys_to_machine(__pa(vaddr(
			pending_req, i)) >> PAGE_SHIFT,
			FOREIGN_FRAME(map[i].dev_bus_addr >> PAGE_SHIFT));
		seg[i].buf  = map[i].dev_bus_addr |
			(req->seg[i].first_sect << 9);
	}

	if (ret)
		goto fail_flush;

	if (vbd_translate(&preq, blkif, operation) != 0) {
		DPRINTK("access denied: %s of [%llu,%llu] on dev=%04x\n",
			operation == READ ? "read" : "write",
			preq.sector_number,
			preq.sector_number + preq.nr_sects, preq.dev);
		goto fail_flush;
	}

	plug_queue(blkif, preq.bdev);
	atomic_set(&pending_req->pendcnt, 1);
	blkif_get(blkif);

	for (i = 0; i < nseg; i++) {
		if (((int)preq.sector_number|(int)seg[i].nsec) &
		    ((bdev_logical_block_size(preq.bdev) >> 9) - 1)) {
			DPRINTK("Misaligned I/O request from domain %d",
				blkif->domid);
			goto fail_put_bio;
		}

		while ((bio == NULL) ||
		       (bio_add_page(bio,
				     virt_to_page(vaddr(pending_req, i)),
				     seg[i].nsec << 9,
				     seg[i].buf & ~PAGE_MASK) == 0)) {
			if (bio) {
				atomic_inc(&pending_req->pendcnt);
				submit_bio(operation, bio);
			}

			bio = bio_alloc(GFP_KERNEL, nseg-i);
			if (unlikely(bio == NULL))
				goto fail_put_bio;

			bio->bi_bdev    = preq.bdev;
			bio->bi_private = pending_req;
			bio->bi_end_io  = end_block_io_op;
			bio->bi_sector  = preq.sector_number;
		}

		preq.sector_number += seg[i].nsec;
	}

	if (!bio) {
		BUG_ON(operation != WRITE_BARRIER);
		bio = bio_alloc(GFP_KERNEL, 0);
		if (unlikely(bio == NULL))
			goto fail_put_bio;

		bio->bi_bdev    = preq.bdev;
		bio->bi_private = pending_req;
		bio->bi_end_io  = end_block_io_op;
		bio->bi_sector  = -1;
	}

	submit_bio(operation, bio);

	if (operation == READ)
		blkif->st_rd_sect += preq.nr_sects;
	else if (operation == WRITE || operation == WRITE_BARRIER)
		blkif->st_wr_sect += preq.nr_sects;

	return;

 fail_flush:
	fast_flush_area(pending_req);
 fail_response:
	make_response(blkif, req->id, req->operation, BLKIF_RSP_ERROR);
	free_req(pending_req);
	msleep(1); /* back off a bit */
	return;

 fail_put_bio:
	__end_block_io_op(pending_req, -EINVAL);
	if (bio)
		bio_put(bio);
	unplug_queue(blkif);
	msleep(1); /* back off a bit */
	return;
}



/******************************************************************
 * MISCELLANEOUS SETUP / TEARDOWN / DEBUGGING
 */


static void make_response(blkif_t *blkif, u64 id,
			  unsigned short op, int st)
{
	struct blkif_response  resp;
	unsigned long     flags;
	union blkif_back_rings *blk_rings = &blkif->blk_rings;
	int more_to_do = 0;
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
	if (blk_rings->common.rsp_prod_pvt == blk_rings->common.req_cons) {
		/*
		 * Tail check for pending requests. Allows frontend to avoid
		 * notifications if requests are already in flight (lower
		 * overheads and promotes batching).
		 */
		RING_FINAL_CHECK_FOR_REQUESTS(&blk_rings->common, more_to_do);

	} else if (RING_HAS_UNCONSUMED_REQUESTS(&blk_rings->common)) {
		more_to_do = 1;
	}

	spin_unlock_irqrestore(&blkif->blk_ring_lock, flags);

	if (more_to_do)
		blkif_notify_work(blkif);
	if (notify)
		notify_remote_via_irq(blkif->irq);
}

static int __init blkif_init(void)
{
	int i, mmap_pages;
	int rc = 0;

	if (!xen_pv_domain())
		return -ENODEV;

	mmap_pages = blkif_reqs * BLKIF_MAX_SEGMENTS_PER_REQUEST;

	pending_reqs          = kmalloc(sizeof(pending_reqs[0]) *
					blkif_reqs, GFP_KERNEL);
	pending_grant_handles = kmalloc(sizeof(pending_grant_handles[0]) *
					mmap_pages, GFP_KERNEL);
	pending_pages         = alloc_empty_pages_and_pagevec(mmap_pages);

	if (!pending_reqs || !pending_grant_handles || !pending_pages) {
		rc = -ENOMEM;
		goto out_of_memory;
	}

	for (i = 0; i < mmap_pages; i++)
		pending_grant_handles[i] = BLKBACK_INVALID_HANDLE;

	rc = blkif_interface_init();
	if (rc)
		goto failed_init;

	memset(pending_reqs, 0, sizeof(pending_reqs));
	INIT_LIST_HEAD(&pending_free);

	for (i = 0; i < blkif_reqs; i++)
		list_add_tail(&pending_reqs[i].free_list, &pending_free);

	rc = blkif_xenbus_init();
	if (rc)
		goto failed_init;

	return 0;

 out_of_memory:
	printk(KERN_ERR "%s: out of memory\n", __func__);
 failed_init:
	kfree(pending_reqs);
	kfree(pending_grant_handles);
	free_empty_pages_and_pagevec(pending_pages, mmap_pages);
	return rc;
}

module_init(blkif_init);

MODULE_LICENSE("Dual BSD/GPL");
