/*
 *  linux/drivers/mmc/mmc_queue.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/blkdev.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include "mmc_queue.h"

#define MMC_QUEUE_EXIT		(1 << 0)
#define MMC_QUEUE_SUSPENDED	(1 << 1)

/*
 * Prepare a MMC request.  Essentially, this means passing the
 * preparation off to the media driver.  The media driver will
 * create a mmc_io_request in req->special.
 */
static int mmc_prep_request(struct request_queue *q, struct request *req)
{
	struct mmc_queue *mq = q->queuedata;
	int ret = BLKPREP_KILL;

	if (req->flags & REQ_SPECIAL) {
		/*
		 * Special commands already have the command
		 * blocks already setup in req->special.
		 */
		BUG_ON(!req->special);

		ret = BLKPREP_OK;
	} else if (req->flags & (REQ_CMD | REQ_BLOCK_PC)) {
		/*
		 * Block I/O requests need translating according
		 * to the protocol.
		 */
		ret = mq->prep_fn(mq, req);
	} else {
		/*
		 * Everything else is invalid.
		 */
		blk_dump_rq_flags(req, "MMC bad request");
	}

	if (ret == BLKPREP_OK)
		req->flags |= REQ_DONTPREP;

	return ret;
}

static int mmc_queue_thread(void *d)
{
	struct mmc_queue *mq = d;
	struct request_queue *q = mq->queue;
	DECLARE_WAITQUEUE(wait, current);

	/*
	 * Set iothread to ensure that we aren't put to sleep by
	 * the process freezing.  We handle suspension ourselves.
	 */
	current->flags |= PF_MEMALLOC|PF_NOFREEZE;

	daemonize("mmcqd");

	complete(&mq->thread_complete);

	down(&mq->thread_sem);
	add_wait_queue(&mq->thread_wq, &wait);
	do {
		struct request *req = NULL;

		spin_lock_irq(q->queue_lock);
		set_current_state(TASK_INTERRUPTIBLE);
		if (!blk_queue_plugged(q))
			mq->req = req = elv_next_request(q);
		spin_unlock_irq(q->queue_lock);

		if (!req) {
			if (mq->flags & MMC_QUEUE_EXIT)
				break;
			up(&mq->thread_sem);
			schedule();
			down(&mq->thread_sem);
			continue;
		}
		set_current_state(TASK_RUNNING);

		mq->issue_fn(mq, req);
	} while (1);
	remove_wait_queue(&mq->thread_wq, &wait);
	up(&mq->thread_sem);

	complete_and_exit(&mq->thread_complete, 0);
	return 0;
}

/*
 * Generic MMC request handler.  This is called for any queue on a
 * particular host.  When the host is not busy, we look for a request
 * on any queue on this host, and attempt to issue it.  This may
 * not be the queue we were asked to process.
 */
static void mmc_request(request_queue_t *q)
{
	struct mmc_queue *mq = q->queuedata;

	if (!mq->req)
		wake_up(&mq->thread_wq);
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

	if (host->dev->dma_mask && *host->dev->dma_mask)
		limit = *host->dev->dma_mask;

	mq->card = card;
	mq->queue = blk_init_queue(mmc_request, lock);
	if (!mq->queue)
		return -ENOMEM;

	blk_queue_prep_rq(mq->queue, mmc_prep_request);
	blk_queue_bounce_limit(mq->queue, limit);
	blk_queue_max_sectors(mq->queue, host->max_sectors);
	blk_queue_max_phys_segments(mq->queue, host->max_phys_segs);
	blk_queue_max_hw_segments(mq->queue, host->max_hw_segs);
	blk_queue_max_segment_size(mq->queue, host->max_seg_size);

	mq->queue->queuedata = mq;
	mq->req = NULL;

	mq->sg = kmalloc(sizeof(struct scatterlist) * host->max_phys_segs,
			 GFP_KERNEL);
	if (!mq->sg) {
		ret = -ENOMEM;
		goto cleanup;
	}

	init_completion(&mq->thread_complete);
	init_waitqueue_head(&mq->thread_wq);
	init_MUTEX(&mq->thread_sem);

	ret = kernel_thread(mmc_queue_thread, mq, CLONE_KERNEL);
	if (ret >= 0) {
		wait_for_completion(&mq->thread_complete);
		init_completion(&mq->thread_complete);
		ret = 0;
		goto out;
	}

 cleanup:
	kfree(mq->sg);
	mq->sg = NULL;

	blk_cleanup_queue(mq->queue);
 out:
	return ret;
}
EXPORT_SYMBOL(mmc_init_queue);

void mmc_cleanup_queue(struct mmc_queue *mq)
{
	mq->flags |= MMC_QUEUE_EXIT;
	wake_up(&mq->thread_wq);
	wait_for_completion(&mq->thread_complete);

	kfree(mq->sg);
	mq->sg = NULL;

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
	request_queue_t *q = mq->queue;
	unsigned long flags;

	if (!(mq->flags & MMC_QUEUE_SUSPENDED)) {
		mq->flags |= MMC_QUEUE_SUSPENDED;

		spin_lock_irqsave(q->queue_lock, flags);
		blk_stop_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);

		down(&mq->thread_sem);
	}
}
EXPORT_SYMBOL(mmc_queue_suspend);

/**
 * mmc_queue_resume - resume a previously suspended MMC request queue
 * @mq: MMC queue to resume
 */
void mmc_queue_resume(struct mmc_queue *mq)
{
	request_queue_t *q = mq->queue;
	unsigned long flags;

	if (mq->flags & MMC_QUEUE_SUSPENDED) {
		mq->flags &= ~MMC_QUEUE_SUSPENDED;

		up(&mq->thread_sem);

		spin_lock_irqsave(q->queue_lock, flags);
		blk_start_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}
EXPORT_SYMBOL(mmc_queue_resume);
