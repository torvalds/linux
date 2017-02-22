/*
 * blk-mq scheduling framework
 *
 * Copyright (C) 2016 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/blk-mq.h>

#include <trace/events/block.h>

#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-sched.h"
#include "blk-mq-tag.h"
#include "blk-wbt.h"

void blk_mq_sched_free_hctx_data(struct request_queue *q,
				 void (*exit)(struct blk_mq_hw_ctx *))
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (exit && hctx->sched_data)
			exit(hctx);
		kfree(hctx->sched_data);
		hctx->sched_data = NULL;
	}
}
EXPORT_SYMBOL_GPL(blk_mq_sched_free_hctx_data);

int blk_mq_sched_init_hctx_data(struct request_queue *q, size_t size,
				int (*init)(struct blk_mq_hw_ctx *),
				void (*exit)(struct blk_mq_hw_ctx *))
{
	struct blk_mq_hw_ctx *hctx;
	int ret;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		hctx->sched_data = kmalloc_node(size, GFP_KERNEL, hctx->numa_node);
		if (!hctx->sched_data) {
			ret = -ENOMEM;
			goto error;
		}

		if (init) {
			ret = init(hctx);
			if (ret) {
				/*
				 * We don't want to give exit() a partially
				 * initialized sched_data. init() must clean up
				 * if it fails.
				 */
				kfree(hctx->sched_data);
				hctx->sched_data = NULL;
				goto error;
			}
		}
	}

	return 0;
error:
	blk_mq_sched_free_hctx_data(q, exit);
	return ret;
}
EXPORT_SYMBOL_GPL(blk_mq_sched_init_hctx_data);

static void __blk_mq_sched_assign_ioc(struct request_queue *q,
				      struct request *rq,
				      struct bio *bio,
				      struct io_context *ioc)
{
	struct io_cq *icq;

	spin_lock_irq(q->queue_lock);
	icq = ioc_lookup_icq(ioc, q);
	spin_unlock_irq(q->queue_lock);

	if (!icq) {
		icq = ioc_create_icq(ioc, q, GFP_ATOMIC);
		if (!icq)
			return;
	}

	rq->elv.icq = icq;
	if (!blk_mq_sched_get_rq_priv(q, rq, bio)) {
		rq->rq_flags |= RQF_ELVPRIV;
		get_io_context(icq->ioc);
		return;
	}

	rq->elv.icq = NULL;
}

static void blk_mq_sched_assign_ioc(struct request_queue *q,
				    struct request *rq, struct bio *bio)
{
	struct io_context *ioc;

	ioc = rq_ioc(bio);
	if (ioc)
		__blk_mq_sched_assign_ioc(q, rq, bio, ioc);
}

struct request *blk_mq_sched_get_request(struct request_queue *q,
					 struct bio *bio,
					 unsigned int op,
					 struct blk_mq_alloc_data *data)
{
	struct elevator_queue *e = q->elevator;
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx;
	struct request *rq;

	blk_queue_enter_live(q);
	ctx = blk_mq_get_ctx(q);
	hctx = blk_mq_map_queue(q, ctx->cpu);

	blk_mq_set_alloc_data(data, q, data->flags, ctx, hctx);

	if (e) {
		data->flags |= BLK_MQ_REQ_INTERNAL;

		/*
		 * Flush requests are special and go directly to the
		 * dispatch list.
		 */
		if (!op_is_flush(op) && e->type->ops.mq.get_request) {
			rq = e->type->ops.mq.get_request(q, op, data);
			if (rq)
				rq->rq_flags |= RQF_QUEUED;
		} else
			rq = __blk_mq_alloc_request(data, op);
	} else {
		rq = __blk_mq_alloc_request(data, op);
		if (rq)
			data->hctx->tags->rqs[rq->tag] = rq;
	}

	if (rq) {
		if (!op_is_flush(op)) {
			rq->elv.icq = NULL;
			if (e && e->type->icq_cache)
				blk_mq_sched_assign_ioc(q, rq, bio);
		}
		data->hctx->queued++;
		return rq;
	}

	blk_queue_exit(q);
	return NULL;
}

void blk_mq_sched_put_request(struct request *rq)
{
	struct request_queue *q = rq->q;
	struct elevator_queue *e = q->elevator;

	if (rq->rq_flags & RQF_ELVPRIV) {
		blk_mq_sched_put_rq_priv(rq->q, rq);
		if (rq->elv.icq) {
			put_io_context(rq->elv.icq->ioc);
			rq->elv.icq = NULL;
		}
	}

	if ((rq->rq_flags & RQF_QUEUED) && e && e->type->ops.mq.put_request)
		e->type->ops.mq.put_request(rq);
	else
		blk_mq_finish_request(rq);
}

void blk_mq_sched_dispatch_requests(struct blk_mq_hw_ctx *hctx)
{
	struct elevator_queue *e = hctx->queue->elevator;
	const bool has_sched_dispatch = e && e->type->ops.mq.dispatch_request;
	bool did_work = false;
	LIST_HEAD(rq_list);

	if (unlikely(blk_mq_hctx_stopped(hctx)))
		return;

	hctx->run++;

	/*
	 * If we have previous entries on our dispatch list, grab them first for
	 * more fair dispatch.
	 */
	if (!list_empty_careful(&hctx->dispatch)) {
		spin_lock(&hctx->lock);
		if (!list_empty(&hctx->dispatch))
			list_splice_init(&hctx->dispatch, &rq_list);
		spin_unlock(&hctx->lock);
	}

	/*
	 * Only ask the scheduler for requests, if we didn't have residual
	 * requests from the dispatch list. This is to avoid the case where
	 * we only ever dispatch a fraction of the requests available because
	 * of low device queue depth. Once we pull requests out of the IO
	 * scheduler, we can no longer merge or sort them. So it's best to
	 * leave them there for as long as we can. Mark the hw queue as
	 * needing a restart in that case.
	 */
	if (!list_empty(&rq_list)) {
		blk_mq_sched_mark_restart(hctx);
		did_work = blk_mq_dispatch_rq_list(hctx, &rq_list);
	} else if (!has_sched_dispatch) {
		blk_mq_flush_busy_ctxs(hctx, &rq_list);
		blk_mq_dispatch_rq_list(hctx, &rq_list);
	}

	/*
	 * We want to dispatch from the scheduler if we had no work left
	 * on the dispatch list, OR if we did have work but weren't able
	 * to make progress.
	 */
	if (!did_work && has_sched_dispatch) {
		do {
			struct request *rq;

			rq = e->type->ops.mq.dispatch_request(hctx);
			if (!rq)
				break;
			list_add(&rq->queuelist, &rq_list);
		} while (blk_mq_dispatch_rq_list(hctx, &rq_list));
	}
}

void blk_mq_sched_move_to_dispatch(struct blk_mq_hw_ctx *hctx,
				   struct list_head *rq_list,
				   struct request *(*get_rq)(struct blk_mq_hw_ctx *))
{
	do {
		struct request *rq;

		rq = get_rq(hctx);
		if (!rq)
			break;

		list_add_tail(&rq->queuelist, rq_list);
	} while (1);
}
EXPORT_SYMBOL_GPL(blk_mq_sched_move_to_dispatch);

bool blk_mq_sched_try_merge(struct request_queue *q, struct bio *bio,
			    struct request **merged_request)
{
	struct request *rq;

	switch (elv_merge(q, &rq, bio)) {
	case ELEVATOR_BACK_MERGE:
		if (!blk_mq_sched_allow_merge(q, rq, bio))
			return false;
		if (!bio_attempt_back_merge(q, rq, bio))
			return false;
		*merged_request = attempt_back_merge(q, rq);
		if (!*merged_request)
			elv_merged_request(q, rq, ELEVATOR_BACK_MERGE);
		return true;
	case ELEVATOR_FRONT_MERGE:
		if (!blk_mq_sched_allow_merge(q, rq, bio))
			return false;
		if (!bio_attempt_front_merge(q, rq, bio))
			return false;
		*merged_request = attempt_front_merge(q, rq);
		if (!*merged_request)
			elv_merged_request(q, rq, ELEVATOR_FRONT_MERGE);
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(blk_mq_sched_try_merge);

bool __blk_mq_sched_bio_merge(struct request_queue *q, struct bio *bio)
{
	struct elevator_queue *e = q->elevator;

	if (e->type->ops.mq.bio_merge) {
		struct blk_mq_ctx *ctx = blk_mq_get_ctx(q);
		struct blk_mq_hw_ctx *hctx = blk_mq_map_queue(q, ctx->cpu);

		blk_mq_put_ctx(ctx);
		return e->type->ops.mq.bio_merge(hctx, bio);
	}

	return false;
}

bool blk_mq_sched_try_insert_merge(struct request_queue *q, struct request *rq)
{
	return rq_mergeable(rq) && elv_attempt_insert_merge(q, rq);
}
EXPORT_SYMBOL_GPL(blk_mq_sched_try_insert_merge);

void blk_mq_sched_request_inserted(struct request *rq)
{
	trace_block_rq_insert(rq->q, rq);
}
EXPORT_SYMBOL_GPL(blk_mq_sched_request_inserted);

static bool blk_mq_sched_bypass_insert(struct blk_mq_hw_ctx *hctx,
				       struct request *rq)
{
	if (rq->tag == -1) {
		rq->rq_flags |= RQF_SORTED;
		return false;
	}

	/*
	 * If we already have a real request tag, send directly to
	 * the dispatch list.
	 */
	spin_lock(&hctx->lock);
	list_add(&rq->queuelist, &hctx->dispatch);
	spin_unlock(&hctx->lock);
	return true;
}

static void blk_mq_sched_restart_hctx(struct blk_mq_hw_ctx *hctx)
{
	if (test_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state)) {
		clear_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state);
		if (blk_mq_hctx_has_pending(hctx))
			blk_mq_run_hw_queue(hctx, true);
	}
}

void blk_mq_sched_restart_queues(struct blk_mq_hw_ctx *hctx)
{
	unsigned int i;

	if (!(hctx->flags & BLK_MQ_F_TAG_SHARED))
		blk_mq_sched_restart_hctx(hctx);
	else {
		struct request_queue *q = hctx->queue;

		if (!test_bit(QUEUE_FLAG_RESTART, &q->queue_flags))
			return;

		clear_bit(QUEUE_FLAG_RESTART, &q->queue_flags);

		queue_for_each_hw_ctx(q, hctx, i)
			blk_mq_sched_restart_hctx(hctx);
	}
}

/*
 * Add flush/fua to the queue. If we fail getting a driver tag, then
 * punt to the requeue list. Requeue will re-invoke us from a context
 * that's safe to block from.
 */
static void blk_mq_sched_insert_flush(struct blk_mq_hw_ctx *hctx,
				      struct request *rq, bool can_block)
{
	if (blk_mq_get_driver_tag(rq, &hctx, can_block)) {
		blk_insert_flush(rq);
		blk_mq_run_hw_queue(hctx, true);
	} else
		blk_mq_add_to_requeue_list(rq, false, true);
}

void blk_mq_sched_insert_request(struct request *rq, bool at_head,
				 bool run_queue, bool async, bool can_block)
{
	struct request_queue *q = rq->q;
	struct elevator_queue *e = q->elevator;
	struct blk_mq_ctx *ctx = rq->mq_ctx;
	struct blk_mq_hw_ctx *hctx = blk_mq_map_queue(q, ctx->cpu);

	if (rq->tag == -1 && op_is_flush(rq->cmd_flags)) {
		blk_mq_sched_insert_flush(hctx, rq, can_block);
		return;
	}

	if (e && blk_mq_sched_bypass_insert(hctx, rq))
		goto run;

	if (e && e->type->ops.mq.insert_requests) {
		LIST_HEAD(list);

		list_add(&rq->queuelist, &list);
		e->type->ops.mq.insert_requests(hctx, &list, at_head);
	} else {
		spin_lock(&ctx->lock);
		__blk_mq_insert_request(hctx, rq, at_head);
		spin_unlock(&ctx->lock);
	}

run:
	if (run_queue)
		blk_mq_run_hw_queue(hctx, async);
}

void blk_mq_sched_insert_requests(struct request_queue *q,
				  struct blk_mq_ctx *ctx,
				  struct list_head *list, bool run_queue_async)
{
	struct blk_mq_hw_ctx *hctx = blk_mq_map_queue(q, ctx->cpu);
	struct elevator_queue *e = hctx->queue->elevator;

	if (e) {
		struct request *rq, *next;

		/*
		 * We bypass requests that already have a driver tag assigned,
		 * which should only be flushes. Flushes are only ever inserted
		 * as single requests, so we shouldn't ever hit the
		 * WARN_ON_ONCE() below (but let's handle it just in case).
		 */
		list_for_each_entry_safe(rq, next, list, queuelist) {
			if (WARN_ON_ONCE(rq->tag != -1)) {
				list_del_init(&rq->queuelist);
				blk_mq_sched_bypass_insert(hctx, rq);
			}
		}
	}

	if (e && e->type->ops.mq.insert_requests)
		e->type->ops.mq.insert_requests(hctx, list, false);
	else
		blk_mq_insert_requests(hctx, ctx, list);

	blk_mq_run_hw_queue(hctx, run_queue_async);
}

static void blk_mq_sched_free_tags(struct blk_mq_tag_set *set,
				   struct blk_mq_hw_ctx *hctx,
				   unsigned int hctx_idx)
{
	if (hctx->sched_tags) {
		blk_mq_free_rqs(set, hctx->sched_tags, hctx_idx);
		blk_mq_free_rq_map(hctx->sched_tags);
		hctx->sched_tags = NULL;
	}
}

int blk_mq_sched_setup(struct request_queue *q)
{
	struct blk_mq_tag_set *set = q->tag_set;
	struct blk_mq_hw_ctx *hctx;
	int ret, i;

	/*
	 * Default to 256, since we don't split into sync/async like the
	 * old code did. Additionally, this is a per-hw queue depth.
	 */
	q->nr_requests = 2 * BLKDEV_MAX_RQ;

	/*
	 * We're switching to using an IO scheduler, so setup the hctx
	 * scheduler tags and switch the request map from the regular
	 * tags to scheduler tags. First allocate what we need, so we
	 * can safely fail and fallback, if needed.
	 */
	ret = 0;
	queue_for_each_hw_ctx(q, hctx, i) {
		hctx->sched_tags = blk_mq_alloc_rq_map(set, i, q->nr_requests, 0);
		if (!hctx->sched_tags) {
			ret = -ENOMEM;
			break;
		}
		ret = blk_mq_alloc_rqs(set, hctx->sched_tags, i, q->nr_requests);
		if (ret)
			break;
	}

	/*
	 * If we failed, free what we did allocate
	 */
	if (ret) {
		queue_for_each_hw_ctx(q, hctx, i) {
			if (!hctx->sched_tags)
				continue;
			blk_mq_sched_free_tags(set, hctx, i);
		}

		return ret;
	}

	return 0;
}

void blk_mq_sched_teardown(struct request_queue *q)
{
	struct blk_mq_tag_set *set = q->tag_set;
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i)
		blk_mq_sched_free_tags(set, hctx, i);
}

int blk_mq_sched_init(struct request_queue *q)
{
	int ret;

	mutex_lock(&q->sysfs_lock);
	ret = elevator_init(q, NULL);
	mutex_unlock(&q->sysfs_lock);

	return ret;
}
