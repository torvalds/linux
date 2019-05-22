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
#include "host.h"

static inline bool mmc_cqe_dcmd_busy(struct mmc_queue *mq)
{
	/* Allow only 1 DCMD at a time */
	return mq->in_flight[MMC_ISSUE_DCMD];
}

void mmc_cqe_check_busy(struct mmc_queue *mq)
{
	if ((mq->cqe_busy & MMC_CQE_DCMD_BUSY) && !mmc_cqe_dcmd_busy(mq))
		mq->cqe_busy &= ~MMC_CQE_DCMD_BUSY;

	mq->cqe_busy &= ~MMC_CQE_QUEUE_FULL;
}

static inline bool mmc_cqe_can_dcmd(struct mmc_host *host)
{
	return host->caps2 & MMC_CAP2_CQE_DCMD;
}

static enum mmc_issue_type mmc_cqe_issue_type(struct mmc_host *host,
					      struct request *req)
{
	switch (req_op(req)) {
	case REQ_OP_DRV_IN:
	case REQ_OP_DRV_OUT:
	case REQ_OP_DISCARD:
	case REQ_OP_SECURE_ERASE:
		return MMC_ISSUE_SYNC;
	case REQ_OP_FLUSH:
		return mmc_cqe_can_dcmd(host) ? MMC_ISSUE_DCMD : MMC_ISSUE_SYNC;
	default:
		return MMC_ISSUE_ASYNC;
	}
}

enum mmc_issue_type mmc_issue_type(struct mmc_queue *mq, struct request *req)
{
	struct mmc_host *host = mq->card->host;

	if (mq->use_cqe)
		return mmc_cqe_issue_type(host, req);

	if (req_op(req) == REQ_OP_READ || req_op(req) == REQ_OP_WRITE)
		return MMC_ISSUE_ASYNC;

	return MMC_ISSUE_SYNC;
}

static void __mmc_cqe_recovery_notifier(struct mmc_queue *mq)
{
	if (!mq->recovery_needed) {
		mq->recovery_needed = true;
		schedule_work(&mq->recovery_work);
	}
}

void mmc_cqe_recovery_notifier(struct mmc_request *mrq)
{
	struct mmc_queue_req *mqrq = container_of(mrq, struct mmc_queue_req,
						  brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct request_queue *q = req->q;
	struct mmc_queue *mq = q->queuedata;
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	__mmc_cqe_recovery_notifier(mq);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static enum blk_eh_timer_return mmc_cqe_timed_out(struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_request *mrq = &mqrq->brq.mrq;
	struct mmc_queue *mq = req->q->queuedata;
	struct mmc_host *host = mq->card->host;
	enum mmc_issue_type issue_type = mmc_issue_type(mq, req);
	bool recovery_needed = false;

	switch (issue_type) {
	case MMC_ISSUE_ASYNC:
	case MMC_ISSUE_DCMD:
		if (host->cqe_ops->cqe_timeout(host, mrq, &recovery_needed)) {
			if (recovery_needed)
				__mmc_cqe_recovery_notifier(mq);
			return BLK_EH_RESET_TIMER;
		}
		/* No timeout (XXX: huh? comment doesn't make much sense) */
		blk_mq_complete_request(req);
		return BLK_EH_DONE;
	default:
		/* Timeout is handled by mmc core */
		return BLK_EH_RESET_TIMER;
	}
}

static enum blk_eh_timer_return mmc_mq_timed_out(struct request *req,
						 bool reserved)
{
	struct request_queue *q = req->q;
	struct mmc_queue *mq = q->queuedata;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(q->queue_lock, flags);

	if (mq->recovery_needed || !mq->use_cqe)
		ret = BLK_EH_RESET_TIMER;
	else
		ret = mmc_cqe_timed_out(req);

	spin_unlock_irqrestore(q->queue_lock, flags);

	return ret;
}

static void mmc_mq_recovery_handler(struct work_struct *work)
{
	struct mmc_queue *mq = container_of(work, struct mmc_queue,
					    recovery_work);
	struct request_queue *q = mq->queue;

	mmc_get_card(mq->card, &mq->ctx);

	mq->in_recovery = true;

	if (mq->use_cqe)
		mmc_blk_cqe_recovery(mq);
	else
		mmc_blk_mq_recovery(mq);

	mq->in_recovery = false;

	spin_lock_irq(q->queue_lock);
	mq->recovery_needed = false;
	spin_unlock_irq(q->queue_lock);

	mmc_put_card(mq->card, &mq->ctx);

	blk_mq_run_hw_queues(q, true);
}

static struct scatterlist *mmc_alloc_sg(int sg_len, gfp_t gfp)
{
	struct scatterlist *sg;

	sg = kmalloc_array(sg_len, sizeof(*sg), gfp);
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

	blk_queue_flag_set(QUEUE_FLAG_DISCARD, q);
	blk_queue_max_discard_sectors(q, max_discard);
	q->limits.discard_granularity = card->pref_erase << 9;
	/* granularity must not be greater than max. discard */
	if (card->pref_erase > max_discard)
		q->limits.discard_granularity = 0;
	if (mmc_can_secure_erase_trim(card))
		blk_queue_flag_set(QUEUE_FLAG_SECERASE, q);
}

/**
 * mmc_init_request() - initialize the MMC-specific per-request data
 * @q: the request queue
 * @req: the request
 * @gfp: memory allocation policy
 */
static int __mmc_init_request(struct mmc_queue *mq, struct request *req,
			      gfp_t gfp)
{
	struct mmc_queue_req *mq_rq = req_to_mmc_queue_req(req);
	struct mmc_card *card = mq->card;
	struct mmc_host *host = card->host;

	mq_rq->sg = mmc_alloc_sg(host->max_segs, gfp);
	if (!mq_rq->sg)
		return -ENOMEM;

	return 0;
}

static void mmc_exit_request(struct request_queue *q, struct request *req)
{
	struct mmc_queue_req *mq_rq = req_to_mmc_queue_req(req);

	kfree(mq_rq->sg);
	mq_rq->sg = NULL;
}

static int mmc_mq_init_request(struct blk_mq_tag_set *set, struct request *req,
			       unsigned int hctx_idx, unsigned int numa_node)
{
	return __mmc_init_request(set->driver_data, req, GFP_KERNEL);
}

static void mmc_mq_exit_request(struct blk_mq_tag_set *set, struct request *req,
				unsigned int hctx_idx)
{
	struct mmc_queue *mq = set->driver_data;

	mmc_exit_request(mq->queue, req);
}

static blk_status_t mmc_mq_queue_rq(struct blk_mq_hw_ctx *hctx,
				    const struct blk_mq_queue_data *bd)
{
	struct request *req = bd->rq;
	struct request_queue *q = req->q;
	struct mmc_queue *mq = q->queuedata;
	struct mmc_card *card = mq->card;
	struct mmc_host *host = card->host;
	enum mmc_issue_type issue_type;
	enum mmc_issued issued;
	bool get_card, cqe_retune_ok;
	int ret;

	if (mmc_card_removed(mq->card)) {
		req->rq_flags |= RQF_QUIET;
		return BLK_STS_IOERR;
	}

	issue_type = mmc_issue_type(mq, req);

	spin_lock_irq(q->queue_lock);

	if (mq->recovery_needed || mq->busy) {
		spin_unlock_irq(q->queue_lock);
		return BLK_STS_RESOURCE;
	}

	switch (issue_type) {
	case MMC_ISSUE_DCMD:
		if (mmc_cqe_dcmd_busy(mq)) {
			mq->cqe_busy |= MMC_CQE_DCMD_BUSY;
			spin_unlock_irq(q->queue_lock);
			return BLK_STS_RESOURCE;
		}
		break;
	case MMC_ISSUE_ASYNC:
		break;
	default:
		/*
		 * Timeouts are handled by mmc core, and we don't have a host
		 * API to abort requests, so we can't handle the timeout anyway.
		 * However, when the timeout happens, blk_mq_complete_request()
		 * no longer works (to stop the request disappearing under us).
		 * To avoid racing with that, set a large timeout.
		 */
		req->timeout = 600 * HZ;
		break;
	}

	/* Parallel dispatch of requests is not supported at the moment */
	mq->busy = true;

	mq->in_flight[issue_type] += 1;
	get_card = (mmc_tot_in_flight(mq) == 1);
	cqe_retune_ok = (mmc_cqe_qcnt(mq) == 1);

	spin_unlock_irq(q->queue_lock);

	if (!(req->rq_flags & RQF_DONTPREP)) {
		req_to_mmc_queue_req(req)->retries = 0;
		req->rq_flags |= RQF_DONTPREP;
	}

	if (get_card)
		mmc_get_card(card, &mq->ctx);

	if (mq->use_cqe) {
		host->retune_now = host->need_retune && cqe_retune_ok &&
				   !host->hold_retune;
	}

	blk_mq_start_request(req);

	issued = mmc_blk_mq_issue_rq(mq, req);

	switch (issued) {
	case MMC_REQ_BUSY:
		ret = BLK_STS_RESOURCE;
		break;
	case MMC_REQ_FAILED_TO_START:
		ret = BLK_STS_IOERR;
		break;
	default:
		ret = BLK_STS_OK;
		break;
	}

	if (issued != MMC_REQ_STARTED) {
		bool put_card = false;

		spin_lock_irq(q->queue_lock);
		mq->in_flight[issue_type] -= 1;
		if (mmc_tot_in_flight(mq) == 0)
			put_card = true;
		mq->busy = false;
		spin_unlock_irq(q->queue_lock);
		if (put_card)
			mmc_put_card(card, &mq->ctx);
	} else {
		WRITE_ONCE(mq->busy, false);
	}

	return ret;
}

static const struct blk_mq_ops mmc_mq_ops = {
	.queue_rq	= mmc_mq_queue_rq,
	.init_request	= mmc_mq_init_request,
	.exit_request	= mmc_mq_exit_request,
	.complete	= mmc_blk_mq_complete,
	.timeout	= mmc_mq_timed_out,
};

static void mmc_setup_queue(struct mmc_queue *mq, struct mmc_card *card)
{
	struct mmc_host *host = card->host;
	u64 limit = BLK_BOUNCE_HIGH;

	if (mmc_dev(host)->dma_mask && *mmc_dev(host)->dma_mask)
		limit = (u64)dma_max_pfn(mmc_dev(host)) << PAGE_SHIFT;

	blk_queue_flag_set(QUEUE_FLAG_NONROT, mq->queue);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, mq->queue);
	if (mmc_can_erase(card))
		mmc_queue_setup_discard(mq->queue, card);

	blk_queue_bounce_limit(mq->queue, limit);
	blk_queue_max_hw_sectors(mq->queue,
		min(host->max_blk_count, host->max_req_size / 512));
	blk_queue_max_segments(mq->queue, host->max_segs);
	blk_queue_max_segment_size(mq->queue, host->max_seg_size);

	INIT_WORK(&mq->recovery_work, mmc_mq_recovery_handler);
	INIT_WORK(&mq->complete_work, mmc_blk_mq_complete_work);

	mutex_init(&mq->complete_lock);

	init_waitqueue_head(&mq->wait);
}

static int mmc_mq_init_queue(struct mmc_queue *mq, int q_depth,
			     const struct blk_mq_ops *mq_ops, spinlock_t *lock)
{
	int ret;

	memset(&mq->tag_set, 0, sizeof(mq->tag_set));
	mq->tag_set.ops = mq_ops;
	mq->tag_set.queue_depth = q_depth;
	mq->tag_set.numa_node = NUMA_NO_NODE;
	mq->tag_set.flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_SG_MERGE |
			    BLK_MQ_F_BLOCKING;
	mq->tag_set.nr_hw_queues = 1;
	mq->tag_set.cmd_size = sizeof(struct mmc_queue_req);
	mq->tag_set.driver_data = mq;

	ret = blk_mq_alloc_tag_set(&mq->tag_set);
	if (ret)
		return ret;

	mq->queue = blk_mq_init_queue(&mq->tag_set);
	if (IS_ERR(mq->queue)) {
		ret = PTR_ERR(mq->queue);
		goto free_tag_set;
	}

	mq->queue->queue_lock = lock;
	mq->queue->queuedata = mq;

	return 0;

free_tag_set:
	blk_mq_free_tag_set(&mq->tag_set);

	return ret;
}

/* Set queue depth to get a reasonable value for q->nr_requests */
#define MMC_QUEUE_DEPTH 64

static int mmc_mq_init(struct mmc_queue *mq, struct mmc_card *card,
			 spinlock_t *lock)
{
	struct mmc_host *host = card->host;
	int q_depth;
	int ret;

	/*
	 * The queue depth for CQE must match the hardware because the request
	 * tag is used to index the hardware queue.
	 */
	if (mq->use_cqe)
		q_depth = min_t(int, card->ext_csd.cmdq_depth, host->cqe_qdepth);
	else
		q_depth = MMC_QUEUE_DEPTH;

	ret = mmc_mq_init_queue(mq, q_depth, &mmc_mq_ops, lock);
	if (ret)
		return ret;

	blk_queue_rq_timeout(mq->queue, 60 * HZ);

	mmc_setup_queue(mq, card);

	return 0;
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

	mq->card = card;

	mq->use_cqe = host->cqe_enabled;

	return mmc_mq_init(mq, card, lock);
}

void mmc_queue_suspend(struct mmc_queue *mq)
{
	blk_mq_quiesce_queue(mq->queue);

	/*
	 * The host remains claimed while there are outstanding requests, so
	 * simply claiming and releasing here ensures there are none.
	 */
	mmc_claim_host(mq->card->host);
	mmc_release_host(mq->card->host);
}

void mmc_queue_resume(struct mmc_queue *mq)
{
	blk_mq_unquiesce_queue(mq->queue);
}

void mmc_cleanup_queue(struct mmc_queue *mq)
{
	struct request_queue *q = mq->queue;

	/*
	 * The legacy code handled the possibility of being suspended,
	 * so do that here too.
	 */
	if (blk_queue_quiesced(q))
		blk_mq_unquiesce_queue(q);

	blk_cleanup_queue(q);
	blk_mq_free_tag_set(&mq->tag_set);

	/*
	 * A request can be completed before the next request, potentially
	 * leaving a complete_work with nothing to do. Such a work item might
	 * still be queued at this point. Flush it.
	 */
	flush_work(&mq->complete_work);

	mq->card = NULL;
}

/*
 * Prepare the sg list(s) to be handed of to the host driver
 */
unsigned int mmc_queue_map_sg(struct mmc_queue *mq, struct mmc_queue_req *mqrq)
{
	struct request *req = mmc_queue_req_to_req(mqrq);

	return blk_rq_map_sg(mq->queue, req, mqrq->sg);
}
