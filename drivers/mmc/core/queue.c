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
#include "core.h"
#include "card.h"

#define MMC_QUEUE_BOUNCESZ	65536

/*
 * Prepare a MMC request. This just filters out odd stuff.
 */
static int mmc_prep_request(struct request_queue *q, struct request *req)
{
	struct mmc_queue *mq = q->queuedata;

	if (mq && (mmc_card_removed(mq->card) || mmc_access_rpmb(mq)))
		return BLKPREP_KILL;

	req->rq_flags |= RQF_DONTPREP;

	return BLKPREP_OK;
}

struct mmc_queue_req *mmc_queue_req_find(struct mmc_queue *mq,
					 struct request *req)
{
	struct mmc_queue_req *mqrq;
	int i = ffz(mq->qslots);

	if (i >= mq->qdepth)
		return NULL;

	mqrq = &mq->mqrq[i];
	WARN_ON(mqrq->req || mq->qcnt >= mq->qdepth ||
		test_bit(mqrq->task_id, &mq->qslots));
	mqrq->req = req;
	mq->qcnt += 1;
	__set_bit(mqrq->task_id, &mq->qslots);

	return mqrq;
}

void mmc_queue_req_free(struct mmc_queue *mq,
			struct mmc_queue_req *mqrq)
{
	WARN_ON(!mqrq->req || mq->qcnt < 1 ||
		!test_bit(mqrq->task_id, &mq->qslots));
	mqrq->req = NULL;
	mq->qcnt -= 1;
	__clear_bit(mqrq->task_id, &mq->qslots);
}

static int mmc_queue_thread(void *d)
{
	struct mmc_queue *mq = d;
	struct request_queue *q = mq->queue;
	struct mmc_context_info *cntx = &mq->card->host->context_info;

	current->flags |= PF_MEMALLOC;

	down(&mq->thread_sem);
	do {
		struct request *req;

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
			if (mq->qcnt)
				cntx->is_waiting_last_req = true;
			else
				mq->asleep = true;
		}
		spin_unlock_irq(q->queue_lock);

		if (req || mq->qcnt) {
			set_current_state(TASK_RUNNING);
			mmc_blk_issue_rq(mq, req);
			cond_resched();
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

static struct scatterlist *mmc_alloc_sg(int sg_len)
{
	struct scatterlist *sg;

	sg = kmalloc_array(sg_len, sizeof(*sg), GFP_KERNEL);
	if (sg)
		sg_init_table(sg, sg_len);

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
	q->limits.discard_granularity = card->pref_erase << 9;
	/* granularity must not be greater than max. discard */
	if (card->pref_erase > max_discard)
		q->limits.discard_granularity = 0;
	if (mmc_can_secure_erase_trim(card))
		queue_flag_set_unlocked(QUEUE_FLAG_SECERASE, q);
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

static void mmc_queue_reqs_free_bufs(struct mmc_queue_req *mqrq, int qdepth)
{
	int i;

	for (i = 0; i < qdepth; i++)
		mmc_queue_req_free_bufs(&mqrq[i]);
}

static void mmc_queue_free_mqrqs(struct mmc_queue_req *mqrq, int qdepth)
{
	mmc_queue_reqs_free_bufs(mqrq, qdepth);
	kfree(mqrq);
}

static struct mmc_queue_req *mmc_queue_alloc_mqrqs(int qdepth)
{
	struct mmc_queue_req *mqrq;
	int i;

	mqrq = kcalloc(qdepth, sizeof(*mqrq), GFP_KERNEL);
	if (mqrq) {
		for (i = 0; i < qdepth; i++)
			mqrq[i].task_id = i;
	}

	return mqrq;
}

#ifdef CONFIG_MMC_BLOCK_BOUNCE
static int mmc_queue_alloc_bounce_bufs(struct mmc_queue_req *mqrq, int qdepth,
				       unsigned int bouncesz)
{
	int i;

	for (i = 0; i < qdepth; i++) {
		mqrq[i].bounce_buf = kmalloc(bouncesz, GFP_KERNEL);
		if (!mqrq[i].bounce_buf)
			return -ENOMEM;

		mqrq[i].sg = mmc_alloc_sg(1);
		if (!mqrq[i].sg)
			return -ENOMEM;

		mqrq[i].bounce_sg = mmc_alloc_sg(bouncesz / 512);
		if (!mqrq[i].bounce_sg)
			return -ENOMEM;
	}

	return 0;
}

static bool mmc_queue_alloc_bounce(struct mmc_queue_req *mqrq, int qdepth,
				   unsigned int bouncesz)
{
	int ret;

	ret = mmc_queue_alloc_bounce_bufs(mqrq, qdepth, bouncesz);
	if (ret)
		mmc_queue_reqs_free_bufs(mqrq, qdepth);

	return !ret;
}

static unsigned int mmc_queue_calc_bouncesz(struct mmc_host *host)
{
	unsigned int bouncesz = MMC_QUEUE_BOUNCESZ;

	if (host->max_segs != 1)
		return 0;

	if (bouncesz > host->max_req_size)
		bouncesz = host->max_req_size;
	if (bouncesz > host->max_seg_size)
		bouncesz = host->max_seg_size;
	if (bouncesz > host->max_blk_count * 512)
		bouncesz = host->max_blk_count * 512;

	if (bouncesz <= 512)
		return 0;

	return bouncesz;
}
#else
static inline bool mmc_queue_alloc_bounce(struct mmc_queue_req *mqrq,
					  int qdepth, unsigned int bouncesz)
{
	return false;
}

static unsigned int mmc_queue_calc_bouncesz(struct mmc_host *host)
{
	return 0;
}
#endif

static int mmc_queue_alloc_sgs(struct mmc_queue_req *mqrq, int qdepth,
			       int max_segs)
{
	int i;

	for (i = 0; i < qdepth; i++) {
		mqrq[i].sg = mmc_alloc_sg(max_segs);
		if (!mqrq[i].sg)
			return -ENOMEM;
	}

	return 0;
}

void mmc_queue_free_shared_queue(struct mmc_card *card)
{
	if (card->mqrq) {
		mmc_queue_free_mqrqs(card->mqrq, card->qdepth);
		card->mqrq = NULL;
	}
}

static int __mmc_queue_alloc_shared_queue(struct mmc_card *card, int qdepth)
{
	struct mmc_host *host = card->host;
	struct mmc_queue_req *mqrq;
	unsigned int bouncesz;
	int ret = 0;

	if (card->mqrq)
		return -EINVAL;

	mqrq = mmc_queue_alloc_mqrqs(qdepth);
	if (!mqrq)
		return -ENOMEM;

	card->mqrq = mqrq;
	card->qdepth = qdepth;

	bouncesz = mmc_queue_calc_bouncesz(host);

	if (bouncesz && !mmc_queue_alloc_bounce(mqrq, qdepth, bouncesz)) {
		bouncesz = 0;
		pr_warn("%s: unable to allocate bounce buffers\n",
			mmc_card_name(card));
	}

	card->bouncesz = bouncesz;

	if (!bouncesz) {
		ret = mmc_queue_alloc_sgs(mqrq, qdepth, host->max_segs);
		if (ret)
			goto out_err;
	}

	return ret;

out_err:
	mmc_queue_free_shared_queue(card);
	return ret;
}

int mmc_queue_alloc_shared_queue(struct mmc_card *card)
{
	return __mmc_queue_alloc_shared_queue(card, 2);
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
	int ret = -ENOMEM;

	if (mmc_dev(host)->dma_mask && *mmc_dev(host)->dma_mask)
		limit = (u64)dma_max_pfn(mmc_dev(host)) << PAGE_SHIFT;

	mq->card = card;
	mq->queue = blk_init_queue(mmc_request_fn, lock);
	if (!mq->queue)
		return -ENOMEM;

	mq->mqrq = card->mqrq;
	mq->qdepth = card->qdepth;
	mq->queue->queuedata = mq;

	blk_queue_prep_rq(mq->queue, mmc_prep_request);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, mq->queue);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, mq->queue);
	if (mmc_can_erase(card))
		mmc_queue_setup_discard(mq->queue, card);

	if (card->bouncesz) {
		blk_queue_bounce_limit(mq->queue, BLK_BOUNCE_ANY);
		blk_queue_max_hw_sectors(mq->queue, card->bouncesz / 512);
		blk_queue_max_segments(mq->queue, card->bouncesz / 512);
		blk_queue_max_segment_size(mq->queue, card->bouncesz);
	} else {
		blk_queue_bounce_limit(mq->queue, limit);
		blk_queue_max_hw_sectors(mq->queue,
			min(host->max_blk_count, host->max_req_size / 512));
		blk_queue_max_segments(mq->queue, host->max_segs);
		blk_queue_max_segment_size(mq->queue, host->max_seg_size);
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
	mq->mqrq = NULL;
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

	if (!mq->suspended) {
		mq->suspended |= true;

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

	if (mq->suspended) {
		mq->suspended = false;

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
