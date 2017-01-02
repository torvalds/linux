/*
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2006-2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "queue.h"
#include "block.h"

#define MMC_QUEUE_BOUNCESZ	65536

/*
 * Prepare a MMC request. This just filters out odd stuff.
 */
static int mmc_prep_request(struct request_queue *q, struct request *req)
{
	struct mmc_queue *mq = q->queuedata;

	/*
	 * We only like normal block requests and discards.
	 */
	if (req->cmd_type != REQ_TYPE_FS && req_op(req) != REQ_OP_DISCARD &&
	    req_op(req) != REQ_OP_SECURE_ERASE) {
		blk_dump_rq_flags(req, "MMC bad request");
		return BLKPREP_KILL;
	}

	if (mq && (mmc_card_removed(mq->card) || mmc_access_rpmb(mq)))
		return BLKPREP_KILL;

	req->rq_flags |= RQF_DONTPREP;

	return BLKPREP_OK;
}

static int mmc_queue_thread(void *d)
{
	struct mmc_queue *mq = d;
	struct request_queue *q = mq->queue;
	struct mmc_context_info *cntx = &mq->card->host->context_info;

	current->flags |= PF_MEMALLOC;

	down(&mq->thread_sem);
	do {
		struct request *req = NULL;

		spin_lock_irq(q->queue_lock);
		set_current_state(TASK_INTERRUPTIBLE);
		req = blk_fetch_request(q);
		mq->asleep = false;
		cntx->is_waiting_last_req = false;
		cntx->is_new_req = false;
		if (!req) {
			/*
			 * Dispatch queue is empty so set flags for
			 * mmc_request_fn() to wake us up.
			 */
			if (mq->mqrq_prev->req)
				cntx->is_waiting_last_req = true;
			else
				mq->asleep = true;
		}
		mq->mqrq_cur->req = req;
		spin_unlock_irq(q->queue_lock);

		if (req || mq->mqrq_prev->req) {
			bool req_is_special = mmc_req_is_special(req);

			set_current_state(TASK_RUNNING);
			mmc_blk_issue_rq(mq, req);
			cond_resched();
			if (mq->flags & MMC_QUEUE_NEW_REQUEST) {
				mq->flags &= ~MMC_QUEUE_NEW_REQUEST;
				continue; /* fetch again */
			}

			/*
			 * Current request becomes previous request
			 * and vice versa.
			 * In case of special requests, current request
			 * has been finished. Do not assign it to previous
			 * request.
			 */
			if (req_is_special)
				mq->mqrq_cur->req = NULL;

			mq->mqrq_prev->brq.mrq.data = NULL;
			mq->mqrq_prev->req = NULL;
			swap(mq->mqrq_prev, mq->mqrq_cur);
		} else {
			if (kthread_should_stop()) {
				set_current_state(TASK_RUNNING);
				break;
			}
			up(&mq->thread_sem);
			schedule();
			down(&mq->thread_sem);
		}
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
static void mmc_request_fn(struct request_queue *q)
{
	struct mmc_queue *mq = q->queuedata;
	struct request *req;
	struct mmc_context_info *cntx;

	if (!mq) {
		while ((req = blk_fetch_request(q)) != NULL) {
			req->rq_flags |= RQF_QUIET;
			__blk_end_request_all(req, -EIO);
		}
		return;
	}

	cntx = &mq->card->host->context_info;

	if (cntx->is_waiting_last_req) {
		cntx->is_new_req = true;
		wake_up_interruptible(&cntx->wait);
	}

	if (mq->asleep)
		wake_up_process(mq->thread);
}

static struct scatterlist *mmc_alloc_sg(int sg_len, int *err)
{
	struct scatterlist *sg;

	sg = kmalloc(sizeof(struct scatterlist)*sg_len, GFP_KERNEL);
	if (!sg)
		*err = -ENOMEM;
	else {
		*err = 0;
		sg_init_table(sg, sg_len);
	}

	return sg;
}

static void mmc_queue_setup_discard(struct request_queue *q,
				    struct mmc_card *card)
{
	unsigned max_discard;

	max_discard = mmc_calc_max_discard(card);
	if (!max_discard)
		return;

	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, q);
	blk_queue_max_discard_sectors(q, max_discard);
	if (card->erased_byte == 0 && !mmc_can_discard(card))
		q->limits.discard_zeroes_data = 1;
	q->limits.discard_granularity = card->pref_erase << 9;
	/* granularity must not be greater than max. discard */
	if (card->pref_erase > max_discard)
		q->limits.discard_granularity = 0;
	if (mmc_can_secure_erase_trim(card))
		queue_flag_set_unlocked(QUEUE_FLAG_SECERASE, q);
}

#ifdef CONFIG_MMC_BLOCK_BOUNCE
static bool mmc_queue_alloc_bounce_bufs(struct mmc_queue *mq,
					unsigned int bouncesz)
{
	int i;

	for (i = 0; i < mq->qdepth; i++) {
		mq->mqrq[i].bounce_buf = kmalloc(bouncesz, GFP_KERNEL);
		if (!mq->mqrq[i].bounce_buf)
			goto out_err;
	}

	return true;

out_err:
	while (--i >= 0) {
		kfree(mq->mqrq[i].bounce_buf);
		mq->mqrq[i].bounce_buf = NULL;
	}
	pr_warn("%s: unable to allocate bounce buffers\n",
		mmc_card_name(mq->card));
	return false;
}

static int mmc_queue_alloc_bounce_sgs(struct mmc_queue *mq,
				      unsigned int bouncesz)
{
	int i, ret;

	for (i = 0; i < mq->qdepth; i++) {
		mq->mqrq[i].sg = mmc_alloc_sg(1, &ret);
		if (ret)
			return ret;

		mq->mqrq[i].bounce_sg = mmc_alloc_sg(bouncesz / 512, &ret);
		if (ret)
			return ret;
	}

	return 0;
}
#endif

static int mmc_queue_alloc_sgs(struct mmc_queue *mq, int max_segs)
{
	int i, ret;

	for (i = 0; i < mq->qdepth; i++) {
		mq->mqrq[i].sg = mmc_alloc_sg(max_segs, &ret);
		if (ret)
			return ret;
	}

	return 0;
}

static void mmc_queue_req_free_bufs(struct mmc_queue_req *mqrq)
{
	kfree(mqrq->bounce_sg);
	mqrq->bounce_sg = NULL;

	kfree(mqrq->sg);
	mqrq->sg = NULL;

	kfree(mqrq->bounce_buf);
	mqrq->bounce_buf = NULL;
}

static void mmc_queue_reqs_free_bufs(struct mmc_queue *mq)
{
	int i;

	for (i = 0; i < mq->qdepth; i++)
		mmc_queue_req_free_bufs(&mq->mqrq[i]);
}

/**
 * mmc_init_queue - initialise a queue structure.
 * @mq: mmc queue
 * @card: mmc card to attach this queue
 * @lock: queue lock
 * @subname: partition subname
 *
 * Initialise a MMC card request queue.
 */
int mmc_init_queue(struct mmc_queue *mq, struct mmc_card *card,
		   spinlock_t *lock, const char *subname)
{
	struct mmc_host *host = card->host;
	u64 limit = BLK_BOUNCE_HIGH;
	bool bounce = false;
	int ret = -ENOMEM;

	if (mmc_dev(host)->dma_mask && *mmc_dev(host)->dma_mask)
		limit = (u64)dma_max_pfn(mmc_dev(host)) << PAGE_SHIFT;

	mq->card = card;
	mq->queue = blk_init_queue(mmc_request_fn, lock);
	if (!mq->queue)
		return -ENOMEM;

	mq->qdepth = 2;
	mq->mqrq = kcalloc(mq->qdepth, sizeof(struct mmc_queue_req),
			   GFP_KERNEL);
	if (!mq->mqrq)
		goto blk_cleanup;
	mq->mqrq_cur = &mq->mqrq[0];
	mq->mqrq_prev = &mq->mqrq[1];
	mq->queue->queuedata = mq;

	blk_queue_prep_rq(mq->queue, mmc_prep_request);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, mq->queue);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, mq->queue);
	if (mmc_can_erase(card))
		mmc_queue_setup_discard(mq->queue, card);

#ifdef CONFIG_MMC_BLOCK_BOUNCE
	if (host->max_segs == 1) {
		unsigned int bouncesz;

		bouncesz = MMC_QUEUE_BOUNCESZ;

		if (bouncesz > host->max_req_size)
			bouncesz = host->max_req_size;
		if (bouncesz > host->max_seg_size)
			bouncesz = host->max_seg_size;
		if (bouncesz > (host->max_blk_count * 512))
			bouncesz = host->max_blk_count * 512;

		if (bouncesz > 512 &&
		    mmc_queue_alloc_bounce_bufs(mq, bouncesz)) {
			blk_queue_bounce_limit(mq->queue, BLK_BOUNCE_ANY);
			blk_queue_max_hw_sectors(mq->queue, bouncesz / 512);
			blk_queue_max_segments(mq->queue, bouncesz / 512);
			blk_queue_max_segment_size(mq->queue, bouncesz);

			ret = mmc_queue_alloc_bounce_sgs(mq, bouncesz);
			if (ret)
				goto cleanup_queue;
			bounce = true;
		}
	}
#endif

	if (!bounce) {
		blk_queue_bounce_limit(mq->queue, limit);
		blk_queue_max_hw_sectors(mq->queue,
			min(host->max_blk_count, host->max_req_size / 512));
		blk_queue_max_segments(mq->queue, host->max_segs);
		blk_queue_max_segment_size(mq->queue, host->max_seg_size);

		ret = mmc_queue_alloc_sgs(mq, host->max_segs);
		if (ret)
			goto cleanup_queue;
	}

	sema_init(&mq->thread_sem, 1);

	mq->thread = kthread_run(mmc_queue_thread, mq, "mmcqd/%d%s",
		host->index, subname ? subname : "");

	if (IS_ERR(mq->thread)) {
		ret = PTR_ERR(mq->thread);
		goto cleanup_queue;
	}

	return 0;

 cleanup_queue:
	mmc_queue_reqs_free_bufs(mq);
	kfree(mq->mqrq);
	mq->mqrq = NULL;
blk_cleanup:
	blk_cleanup_queue(mq->queue);
	return ret;
}

void mmc_cleanup_queue(struct mmc_queue *mq)
{
	struct request_queue *q = mq->queue;
	unsigned long flags;

	/* Make sure the queue isn't suspended, as that will deadlock */
	mmc_queue_resume(mq);

	/* Then terminate our worker thread */
	kthread_stop(mq->thread);

	/* Empty the queue */
	spin_lock_irqsave(q->queue_lock, flags);
	q->queuedata = NULL;
	blk_start_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);

	mmc_queue_reqs_free_bufs(mq);
	kfree(mq->mqrq);
	mq->mqrq = NULL;

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

/*
 * Prepare the sg list(s) to be handed of to the host driver
 */
unsigned int mmc_queue_map_sg(struct mmc_queue *mq, struct mmc_queue_req *mqrq)
{
	unsigned int sg_len;
	size_t buflen;
	struct scatterlist *sg;
	int i;

	if (!mqrq->bounce_buf)
		return blk_rq_map_sg(mq->queue, mqrq->req, mqrq->sg);

	sg_len = blk_rq_map_sg(mq->queue, mqrq->req, mqrq->bounce_sg);

	mqrq->bounce_sg_len = sg_len;

	buflen = 0;
	for_each_sg(mqrq->bounce_sg, sg, sg_len, i)
		buflen += sg->length;

	sg_init_one(mqrq->sg, mqrq->bounce_buf, buflen);

	return 1;
}

/*
 * If writing, bounce the data to the buffer before the request
 * is sent to the host driver
 */
void mmc_queue_bounce_pre(struct mmc_queue_req *mqrq)
{
	if (!mqrq->bounce_buf)
		return;

	if (rq_data_dir(mqrq->req) != WRITE)
		return;

	sg_copy_to_buffer(mqrq->bounce_sg, mqrq->bounce_sg_len,
		mqrq->bounce_buf, mqrq->sg[0].length);
}

/*
 * If reading, bounce the data from the buffer after the request
 * has been handled by the host driver
 */
void mmc_queue_bounce_post(struct mmc_queue_req *mqrq)
{
	if (!mqrq->bounce_buf)
		return;

	if (rq_data_dir(mqrq->req) != READ)
		return;

	sg_copy_from_buffer(mqrq->bounce_sg, mqrq->bounce_sg_len,
		mqrq->bounce_buf, mqrq->sg[0].length);
}
