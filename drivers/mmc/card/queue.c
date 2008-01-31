/*
 *  linux/drivers/mmc/card/queue.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2006-2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/scatterlist.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include "queue.h"

#define MMC_QUEUE_BOUNCESZ	65536

#define MMC_QUEUE_SUSPENDED	(1 << 0)

/*
 * Prepare a MMC request. This just filters out odd stuff.
 */
static int mmc_prep_request(struct request_queue *q, struct request *req)
{
	/*
	 * We only like normal block requests.
	 */
	if (!blk_fs_request(req) && !blk_pc_request(req)) {
		blk_dump_rq_flags(req, "MMC bad request");
		return BLKPREP_KILL;
	}

	req->cmd_flags |= REQ_DONTPREP;

	return BLKPREP_OK;
}

static int mmc_queue_thread(void *d)
{
	struct mmc_queue *mq = d;
	struct request_queue *q = mq->queue;

	current->flags |= PF_MEMALLOC;

	down(&mq->thread_sem);
	do {
		struct request *req = NULL;

		spin_lock_irq(q->queue_lock);
		set_current_state(TASK_INTERRUPTIBLE);
		if (!blk_queue_plugged(q))
			req = elv_next_request(q);
		mq->req = req;
		spin_unlock_irq(q->queue_lock);

		if (!req) {
			if (kthread_should_stop()) {
				set_current_state(TASK_RUNNING);
				break;
			}
			up(&mq->thread_sem);
			schedule();
			down(&mq->thread_sem);
			continue;
		}
		set_current_state(TASK_RUNNING);

		mq->issue_fn(mq, req);
	} while (1);
	up(&mq->thread_sem);

	return 0;
}

/*
 * Generic MMC request handler.  This is called for any queue on a
 * particular host.  When the host is not busy, we look for a request
 * on any queue on this host, and attempt to issue it.  This may
 * not be the queue we were asked to process.
 */
static void mmc_request(struct request_queue *q)
{
	struct mmc_queue *mq = q->queuedata;
	struct request *req;
	int ret;

	if (!mq) {
		printk(KERN_ERR "MMC: killing requests for dead queue\n");
		while ((req = elv_next_request(q)) != NULL) {
			do {
				ret = __blk_end_request(req, -EIO,
							blk_rq_cur_bytes(req));
			} while (ret);
		}
		return;
	}

	if (!mq->req)
		wake_up_process(mq->thread);
}

/**
 * mmc_init_queue - initialise a queue structure.
 * @mq: mmc queue
 * @card: mmc card to attach this queue
 * @lock: queue lock
 *
 * Initialise a MMC card request queue.
 */
int mmc_init_queue(struct mmc_queue *mq, struct mmc_card *card, spinlock_t *lock)
{
	struct mmc_host *host = card->host;
	u64 limit = BLK_BOUNCE_HIGH;
	int ret;

	if (mmc_dev(host)->dma_mask && *mmc_dev(host)->dma_mask)
		limit = *mmc_dev(host)->dma_mask;

	mq->card = card;
	mq->queue = blk_init_queue(mmc_request, lock);
	if (!mq->queue)
		return -ENOMEM;

	mq->queue->queuedata = mq;
	mq->req = NULL;

	blk_queue_prep_rq(mq->queue, mmc_prep_request);

#ifdef CONFIG_MMC_BLOCK_BOUNCE
	if (host->max_hw_segs == 1) {
		unsigned int bouncesz;

		bouncesz = MMC_QUEUE_BOUNCESZ;

		if (bouncesz > host->max_req_size)
			bouncesz = host->max_req_size;
		if (bouncesz > host->max_seg_size)
			bouncesz = host->max_seg_size;

		mq->bounce_buf = kmalloc(bouncesz, GFP_KERNEL);
		if (!mq->bounce_buf) {
			printk(KERN_WARNING "%s: unable to allocate "
				"bounce buffer\n", mmc_card_name(card));
		} else {
			blk_queue_bounce_limit(mq->queue, BLK_BOUNCE_HIGH);
			blk_queue_max_sectors(mq->queue, bouncesz / 512);
			blk_queue_max_phys_segments(mq->queue, bouncesz / 512);
			blk_queue_max_hw_segments(mq->queue, bouncesz / 512);
			blk_queue_max_segment_size(mq->queue, bouncesz);

			mq->sg = kmalloc(sizeof(struct scatterlist),
				GFP_KERNEL);
			if (!mq->sg) {
				ret = -ENOMEM;
				goto cleanup_queue;
			}
			sg_init_table(mq->sg, 1);

			mq->bounce_sg = kmalloc(sizeof(struct scatterlist) *
				bouncesz / 512, GFP_KERNEL);
			if (!mq->bounce_sg) {
				ret = -ENOMEM;
				goto cleanup_queue;
			}
			sg_init_table(mq->bounce_sg, bouncesz / 512);
		}
	}
#endif

	if (!mq->bounce_buf) {
		blk_queue_bounce_limit(mq->queue, limit);
		blk_queue_max_sectors(mq->queue, host->max_req_size / 512);
		blk_queue_max_phys_segments(mq->queue, host->max_phys_segs);
		blk_queue_max_hw_segments(mq->queue, host->max_hw_segs);
		blk_queue_max_segment_size(mq->queue, host->max_seg_size);

		mq->sg = kmalloc(sizeof(struct scatterlist) *
			host->max_phys_segs, GFP_KERNEL);
		if (!mq->sg) {
			ret = -ENOMEM;
			goto cleanup_queue;
		}
		sg_init_table(mq->sg, host->max_phys_segs);
	}

	init_MUTEX(&mq->thread_sem);

	mq->thread = kthread_run(mmc_queue_thread, mq, "mmcqd");
	if (IS_ERR(mq->thread)) {
		ret = PTR_ERR(mq->thread);
		goto free_bounce_sg;
	}

	return 0;
 free_bounce_sg:
 	if (mq->bounce_sg)
 		kfree(mq->bounce_sg);
 	mq->bounce_sg = NULL;
 cleanup_queue:
 	if (mq->sg)
		kfree(mq->sg);
	mq->sg = NULL;
	if (mq->bounce_buf)
		kfree(mq->bounce_buf);
	mq->bounce_buf = NULL;
	blk_cleanup_queue(mq->queue);
	return ret;
}

void mmc_cleanup_queue(struct mmc_queue *mq)
{
	struct request_queue *q = mq->queue;
	unsigned long flags;

	/* Mark that we should start throwing out stragglers */
	spin_lock_irqsave(q->queue_lock, flags);
	q->queuedata = NULL;
	spin_unlock_irqrestore(q->queue_lock, flags);

	/* Make sure the queue isn't suspended, as that will deadlock */
	mmc_queue_resume(mq);

	/* Then terminate our worker thread */
	kthread_stop(mq->thread);

 	if (mq->bounce_sg)
 		kfree(mq->bounce_sg);
 	mq->bounce_sg = NULL;

	kfree(mq->sg);
	mq->sg = NULL;

	if (mq->bounce_buf)
		kfree(mq->bounce_buf);
	mq->bounce_buf = NULL;

	blk_cleanup_queue(mq->queue);

	mq->card = NULL;
}
EXPORT_SYMBOL(mmc_cleanup_queue);

/**
 * mmc_queue_suspend - suspend a MMC request queue
 * @mq: MMC queue to suspend
 *
 * Stop the block request queue, and wait for our thread to
 * complete any outstanding requests.  This ensures that we
 * won't suspend while a request is being processed.
 */
void mmc_queue_suspend(struct mmc_queue *mq)
{
	struct request_queue *q = mq->queue;
	unsigned long flags;

	if (!(mq->flags & MMC_QUEUE_SUSPENDED)) {
		mq->flags |= MMC_QUEUE_SUSPENDED;

		spin_lock_irqsave(q->queue_lock, flags);
		blk_stop_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);

		down(&mq->thread_sem);
	}
}

/**
 * mmc_queue_resume - resume a previously suspended MMC request queue
 * @mq: MMC queue to resume
 */
void mmc_queue_resume(struct mmc_queue *mq)
{
	struct request_queue *q = mq->queue;
	unsigned long flags;

	if (mq->flags & MMC_QUEUE_SUSPENDED) {
		mq->flags &= ~MMC_QUEUE_SUSPENDED;

		up(&mq->thread_sem);

		spin_lock_irqsave(q->queue_lock, flags);
		blk_start_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}

static void copy_sg(struct scatterlist *dst, unsigned int dst_len,
	struct scatterlist *src, unsigned int src_len)
{
	unsigned int chunk;
	char *dst_buf, *src_buf;
	unsigned int dst_size, src_size;

	dst_buf = NULL;
	src_buf = NULL;
	dst_size = 0;
	src_size = 0;

	while (src_len) {
		BUG_ON(dst_len == 0);

		if (dst_size == 0) {
			dst_buf = sg_virt(dst);
			dst_size = dst->length;
		}

		if (src_size == 0) {
			src_buf = sg_virt(src);
			src_size = src->length;
		}

		chunk = min(dst_size, src_size);

		memcpy(dst_buf, src_buf, chunk);

		dst_buf += chunk;
		src_buf += chunk;
		dst_size -= chunk;
		src_size -= chunk;

		if (dst_size == 0) {
			dst++;
			dst_len--;
		}

		if (src_size == 0) {
			src++;
			src_len--;
		}
	}
}

unsigned int mmc_queue_map_sg(struct mmc_queue *mq)
{
	unsigned int sg_len;

	if (!mq->bounce_buf)
		return blk_rq_map_sg(mq->queue, mq->req, mq->sg);

	BUG_ON(!mq->bounce_sg);

	sg_len = blk_rq_map_sg(mq->queue, mq->req, mq->bounce_sg);

	mq->bounce_sg_len = sg_len;

	/*
	 * Shortcut in the event we only get a single entry.
	 */
	if (sg_len == 1) {
		memcpy(mq->sg, mq->bounce_sg, sizeof(struct scatterlist));
		return 1;
	}

	sg_init_one(mq->sg, mq->bounce_buf, 0);

	while (sg_len) {
		mq->sg[0].length += mq->bounce_sg[sg_len - 1].length;
		sg_len--;
	}

	return 1;
}

void mmc_queue_bounce_pre(struct mmc_queue *mq)
{
	if (!mq->bounce_buf)
		return;

	if (mq->bounce_sg_len == 1)
		return;
	if (rq_data_dir(mq->req) != WRITE)
		return;

	copy_sg(mq->sg, 1, mq->bounce_sg, mq->bounce_sg_len);
}

void mmc_queue_bounce_post(struct mmc_queue *mq)
{
	if (!mq->bounce_buf)
		return;

	if (mq->bounce_sg_len == 1)
		return;
	if (rq_data_dir(mq->req) != READ)
		return;

	copy_sg(mq->bounce_sg, mq->bounce_sg_len, mq->sg, 1);
}

