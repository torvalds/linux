// SPDX-License-Identifier: GPL-2.0
/*
 * blk-mq scheduling framework
 *
 * Copyright (C) 2016 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/blk-mq.h>
#include <linux/list_sort.h>

#include <trace/events/block.h>

#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-debugfs.h"
#include "blk-mq-sched.h"
#include "blk-mq-tag.h"
#include "blk-wbt.h"

void blk_mq_sched_assign_ioc(struct request *rq)
{
	struct request_queue *q = rq->q;
	struct io_context *ioc;
	struct io_cq *icq;

	/*
	 * May not have an IO context if it's a passthrough request
	 */
	ioc = current->io_context;
	if (!ioc)
		return;

	spin_lock_irq(&q->queue_lock);
	icq = ioc_lookup_icq(ioc, q);
	spin_unlock_irq(&q->queue_lock);

	if (!icq) {
		icq = ioc_create_icq(ioc, q, GFP_ATOMIC);
		if (!icq)
			return;
	}
	get_io_context(icq->ioc);
	rq->elv.icq = icq;
}

/*
 * Mark a hardware queue as needing a restart. For shared queues, maintain
 * a count of how many hardware queues are marked for restart.
 */
void blk_mq_sched_mark_restart_hctx(struct blk_mq_hw_ctx *hctx)
{
	if (test_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state))
		return;

	set_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state);
}
EXPORT_SYMBOL_GPL(blk_mq_sched_mark_restart_hctx);

void blk_mq_sched_restart(struct blk_mq_hw_ctx *hctx)
{
	if (!test_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state))
		return;
	clear_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state);

	/*
	 * Order clearing SCHED_RESTART and list_empty_careful(&hctx->dispatch)
	 * in blk_mq_run_hw_queue(). Its pair is the barrier in
	 * blk_mq_dispatch_rq_list(). So dispatch code won't see SCHED_RESTART,
	 * meantime new request added to hctx->dispatch is missed to check in
	 * blk_mq_run_hw_queue().
	 */
	smp_mb();

	blk_mq_run_hw_queue(hctx, true);
}

static int sched_rq_cmp(void *priv, const struct list_head *a,
			const struct list_head *b)
{
	struct request *rqa = container_of(a, struct request, queuelist);
	struct request *rqb = container_of(b, struct request, queuelist);

	return rqa->mq_hctx > rqb->mq_hctx;
}

static bool blk_mq_dispatch_hctx_list(struct list_head *rq_list)
{
	struct blk_mq_hw_ctx *hctx =
		list_first_entry(rq_list, struct request, queuelist)->mq_hctx;
	struct request *rq;
	LIST_HEAD(hctx_list);
	unsigned int count = 0;

	list_for_each_entry(rq, rq_list, queuelist) {
		if (rq->mq_hctx != hctx) {
			list_cut_before(&hctx_list, rq_list, &rq->queuelist);
			goto dispatch;
		}
		count++;
	}
	list_splice_tail_init(rq_list, &hctx_list);

dispatch:
	return blk_mq_dispatch_rq_list(hctx, &hctx_list, count);
}

#define BLK_MQ_BUDGET_DELAY	3		/* ms units */

/*
 * Only SCSI implements .get_budget and .put_budget, and SCSI restarts
 * its queue by itself in its completion handler, so we don't need to
 * restart queue if .get_budget() returns BLK_STS_NO_RESOURCE.
 *
 * Returns -EAGAIN if hctx->dispatch was found non-empty and run_work has to
 * be run again.  This is necessary to avoid starving flushes.
 */
static int __blk_mq_do_dispatch_sched(struct blk_mq_hw_ctx *hctx)
{
	struct request_queue *q = hctx->queue;
	struct elevator_queue *e = q->elevator;
	bool multi_hctxs = false, run_queue = false;
	bool dispatched = false, busy = false;
	unsigned int max_dispatch;
	LIST_HEAD(rq_list);
	int count = 0;

	if (hctx->dispatch_busy)
		max_dispatch = 1;
	else
		max_dispatch = hctx->queue->nr_requests;

	do {
		struct request *rq;
		int budget_token;

		if (e->type->ops.has_work && !e->type->ops.has_work(hctx))
			break;

		if (!list_empty_careful(&hctx->dispatch)) {
			busy = true;
			break;
		}

		budget_token = blk_mq_get_dispatch_budget(q);
		if (budget_token < 0)
			break;

		rq = e->type->ops.dispatch_request(hctx);
		if (!rq) {
			blk_mq_put_dispatch_budget(q, budget_token);
			/*
			 * We're releasing without dispatching. Holding the
			 * budget could have blocked any "hctx"s with the
			 * same queue and if we didn't dispatch then there's
			 * no guarantee anyone will kick the queue.  Kick it
			 * ourselves.
			 */
			run_queue = true;
			break;
		}

		blk_mq_set_rq_budget_token(rq, budget_token);

		/*
		 * Now this rq owns the budget which has to be released
		 * if this rq won't be queued to driver via .queue_rq()
		 * in blk_mq_dispatch_rq_list().
		 */
		list_add_tail(&rq->queuelist, &rq_list);
		if (rq->mq_hctx != hctx)
			multi_hctxs = true;
	} while (++count < max_dispatch);

	if (!count) {
		if (run_queue)
			blk_mq_delay_run_hw_queues(q, BLK_MQ_BUDGET_DELAY);
	} else if (multi_hctxs) {
		/*
		 * Requests from different hctx may be dequeued from some
		 * schedulers, such as bfq and deadline.
		 *
		 * Sort the requests in the list according to their hctx,
		 * dispatch batching requests from same hctx at a time.
		 */
		list_sort(NULL, &rq_list, sched_rq_cmp);
		do {
			dispatched |= blk_mq_dispatch_hctx_list(&rq_list);
		} while (!list_empty(&rq_list));
	} else {
		dispatched = blk_mq_dispatch_rq_list(hctx, &rq_list, count);
	}

	if (busy)
		return -EAGAIN;
	return !!dispatched;
}

static int blk_mq_do_dispatch_sched(struct blk_mq_hw_ctx *hctx)
{
	int ret;

	do {
		ret = __blk_mq_do_dispatch_sched(hctx);
	} while (ret == 1);

	return ret;
}

static struct blk_mq_ctx *blk_mq_next_ctx(struct blk_mq_hw_ctx *hctx,
					  struct blk_mq_ctx *ctx)
{
	unsigned short idx = ctx->index_hw[hctx->type];

	if (++idx == hctx->nr_ctx)
		idx = 0;

	return hctx->ctxs[idx];
}

/*
 * Only SCSI implements .get_budget and .put_budget, and SCSI restarts
 * its queue by itself in its completion handler, so we don't need to
 * restart queue if .get_budget() returns BLK_STS_NO_RESOURCE.
 *
 * Returns -EAGAIN if hctx->dispatch was found non-empty and run_work has to
 * be run again.  This is necessary to avoid starving flushes.
 */
static int blk_mq_do_dispatch_ctx(struct blk_mq_hw_ctx *hctx)
{
	struct request_queue *q = hctx->queue;
	LIST_HEAD(rq_list);
	struct blk_mq_ctx *ctx = READ_ONCE(hctx->dispatch_from);
	int ret = 0;
	struct request *rq;

	do {
		int budget_token;

		if (!list_empty_careful(&hctx->dispatch)) {
			ret = -EAGAIN;
			break;
		}

		if (!sbitmap_any_bit_set(&hctx->ctx_map))
			break;

		budget_token = blk_mq_get_dispatch_budget(q);
		if (budget_token < 0)
			break;

		rq = blk_mq_dequeue_from_ctx(hctx, ctx);
		if (!rq) {
			blk_mq_put_dispatch_budget(q, budget_token);
			/*
			 * We're releasing without dispatching. Holding the
			 * budget could have blocked any "hctx"s with the
			 * same queue and if we didn't dispatch then there's
			 * no guarantee anyone will kick the queue.  Kick it
			 * ourselves.
			 */
			blk_mq_delay_run_hw_queues(q, BLK_MQ_BUDGET_DELAY);
			break;
		}

		blk_mq_set_rq_budget_token(rq, budget_token);

		/*
		 * Now this rq owns the budget which has to be released
		 * if this rq won't be queued to driver via .queue_rq()
		 * in blk_mq_dispatch_rq_list().
		 */
		list_add(&rq->queuelist, &rq_list);

		/* round robin for fair dispatch */
		ctx = blk_mq_next_ctx(hctx, rq->mq_ctx);

	} while (blk_mq_dispatch_rq_list(rq->mq_hctx, &rq_list, 1));

	WRITE_ONCE(hctx->dispatch_from, ctx);
	return ret;
}

static int __blk_mq_sched_dispatch_requests(struct blk_mq_hw_ctx *hctx)
{
	struct request_queue *q = hctx->queue;
	struct elevator_queue *e = q->elevator;
	const bool has_sched_dispatch = e && e->type->ops.dispatch_request;
	int ret = 0;
	LIST_HEAD(rq_list);

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
	 *
	 * We want to dispatch from the scheduler if there was nothing
	 * on the dispatch list or we were able to dispatch from the
	 * dispatch list.
	 */
	if (!list_empty(&rq_list)) {
		blk_mq_sched_mark_restart_hctx(hctx);
		if (blk_mq_dispatch_rq_list(hctx, &rq_list, 0)) {
			if (has_sched_dispatch)
				ret = blk_mq_do_dispatch_sched(hctx);
			else
				ret = blk_mq_do_dispatch_ctx(hctx);
		}
	} else if (has_sched_dispatch) {
		ret = blk_mq_do_dispatch_sched(hctx);
	} else if (hctx->dispatch_busy) {
		/* dequeue request one by one from sw queue if queue is busy */
		ret = blk_mq_do_dispatch_ctx(hctx);
	} else {
		blk_mq_flush_busy_ctxs(hctx, &rq_list);
		blk_mq_dispatch_rq_list(hctx, &rq_list, 0);
	}

	return ret;
}

void blk_mq_sched_dispatch_requests(struct blk_mq_hw_ctx *hctx)
{
	struct request_queue *q = hctx->queue;

	/* RCU or SRCU read lock is needed before checking quiesced flag */
	if (unlikely(blk_mq_hctx_stopped(hctx) || blk_queue_quiesced(q)))
		return;

	hctx->run++;

	/*
	 * A return of -EAGAIN is an indication that hctx->dispatch is not
	 * empty and we must run again in order to avoid starving flushes.
	 */
	if (__blk_mq_sched_dispatch_requests(hctx) == -EAGAIN) {
		if (__blk_mq_sched_dispatch_requests(hctx) == -EAGAIN)
			blk_mq_run_hw_queue(hctx, true);
	}
}

bool __blk_mq_sched_bio_merge(struct request_queue *q, struct bio *bio,
		unsigned int nr_segs)
{
	struct elevator_queue *e = q->elevator;
	struct blk_mq_ctx *ctx;
	struct blk_mq_hw_ctx *hctx;
	bool ret = false;
	enum hctx_type type;

	if (e && e->type->ops.bio_merge)
		return e->type->ops.bio_merge(q, bio, nr_segs);

	ctx = blk_mq_get_ctx(q);
	hctx = blk_mq_map_queue(q, bio->bi_opf, ctx);
	type = hctx->type;
	if (!(hctx->flags & BLK_MQ_F_SHOULD_MERGE) ||
	    list_empty_careful(&ctx->rq_lists[type]))
		return false;

	/* default per sw-queue merge */
	spin_lock(&ctx->lock);
	/*
	 * Reverse check our software queue for entries that we could
	 * potentially merge with. Currently includes a hand-wavy stop
	 * count of 8, to not spend too much time checking for merges.
	 */
	if (blk_bio_list_merge(q, &ctx->rq_lists[type], bio, nr_segs)) {
		ctx->rq_merged++;
		ret = true;
	}

	spin_unlock(&ctx->lock);

	return ret;
}

bool blk_mq_sched_try_insert_merge(struct request_queue *q, struct request *rq)
{
	return rq_mergeable(rq) && elv_attempt_insert_merge(q, rq);
}
EXPORT_SYMBOL_GPL(blk_mq_sched_try_insert_merge);

static bool blk_mq_sched_bypass_insert(struct blk_mq_hw_ctx *hctx,
				       struct request *rq)
{
	/*
	 * dispatch flush and passthrough rq directly
	 *
	 * passthrough request has to be added to hctx->dispatch directly.
	 * For some reason, device may be in one situation which can't
	 * handle FS request, so STS_RESOURCE is always returned and the
	 * FS request will be added to hctx->dispatch. However passthrough
	 * request may be required at that time for fixing the problem. If
	 * passthrough request is added to scheduler queue, there isn't any
	 * chance to dispatch it given we prioritize requests in hctx->dispatch.
	 */
	if ((rq->rq_flags & RQF_FLUSH_SEQ) || blk_rq_is_passthrough(rq))
		return true;

	return false;
}

void blk_mq_sched_insert_request(struct request *rq, bool at_head,
				 bool run_queue, bool async)
{
	struct request_queue *q = rq->q;
	struct elevator_queue *e = q->elevator;
	struct blk_mq_ctx *ctx = rq->mq_ctx;
	struct blk_mq_hw_ctx *hctx = rq->mq_hctx;

	WARN_ON(e && (rq->tag != BLK_MQ_NO_TAG));

	if (blk_mq_sched_bypass_insert(hctx, rq)) {
		/*
		 * Firstly normal IO request is inserted to scheduler queue or
		 * sw queue, meantime we add flush request to dispatch queue(
		 * hctx->dispatch) directly and there is at most one in-flight
		 * flush request for each hw queue, so it doesn't matter to add
		 * flush request to tail or front of the dispatch queue.
		 *
		 * Secondly in case of NCQ, flush request belongs to non-NCQ
		 * command, and queueing it will fail when there is any
		 * in-flight normal IO request(NCQ command). When adding flush
		 * rq to the front of hctx->dispatch, it is easier to introduce
		 * extra time to flush rq's latency because of S_SCHED_RESTART
		 * compared with adding to the tail of dispatch queue, then
		 * chance of flush merge is increased, and less flush requests
		 * will be issued to controller. It is observed that ~10% time
		 * is saved in blktests block/004 on disk attached to AHCI/NCQ
		 * drive when adding flush rq to the front of hctx->dispatch.
		 *
		 * Simply queue flush rq to the front of hctx->dispatch so that
		 * intensive flush workloads can benefit in case of NCQ HW.
		 */
		at_head = (rq->rq_flags & RQF_FLUSH_SEQ) ? true : at_head;
		blk_mq_request_bypass_insert(rq, at_head, false);
		goto run;
	}

	if (e && e->type->ops.insert_requests) {
		LIST_HEAD(list);

		list_add(&rq->queuelist, &list);
		e->type->ops.insert_requests(hctx, &list, at_head);
	} else {
		spin_lock(&ctx->lock);
		__blk_mq_insert_request(hctx, rq, at_head);
		spin_unlock(&ctx->lock);
	}

run:
	if (run_queue)
		blk_mq_run_hw_queue(hctx, async);
}

void blk_mq_sched_insert_requests(struct blk_mq_hw_ctx *hctx,
				  struct blk_mq_ctx *ctx,
				  struct list_head *list, bool run_queue_async)
{
	struct elevator_queue *e;
	struct request_queue *q = hctx->queue;

	/*
	 * blk_mq_sched_insert_requests() is called from flush plug
	 * context only, and hold one usage counter to prevent queue
	 * from being released.
	 */
	percpu_ref_get(&q->q_usage_counter);

	e = hctx->queue->elevator;
	if (e && e->type->ops.insert_requests)
		e->type->ops.insert_requests(hctx, list, false);
	else {
		/*
		 * try to issue requests directly if the hw queue isn't
		 * busy in case of 'none' scheduler, and this way may save
		 * us one extra enqueue & dequeue to sw queue.
		 */
		if (!hctx->dispatch_busy && !e && !run_queue_async) {
			blk_mq_try_issue_list_directly(hctx, list);
			if (list_empty(list))
				goto out;
		}
		blk_mq_insert_requests(hctx, ctx, list);
	}

	blk_mq_run_hw_queue(hctx, run_queue_async);
 out:
	percpu_ref_put(&q->q_usage_counter);
}

static void blk_mq_sched_free_tags(struct blk_mq_tag_set *set,
				   struct blk_mq_hw_ctx *hctx,
				   unsigned int hctx_idx)
{
	if (hctx->sched_tags) {
		blk_mq_free_rqs(set, hctx->sched_tags, hctx_idx);
		blk_mq_free_rq_map(hctx->sched_tags, set->flags);
		hctx->sched_tags = NULL;
	}
}

static int blk_mq_sched_alloc_tags(struct request_queue *q,
				   struct blk_mq_hw_ctx *hctx,
				   unsigned int hctx_idx)
{
	struct blk_mq_tag_set *set = q->tag_set;
	int ret;

	hctx->sched_tags = blk_mq_alloc_rq_map(set, hctx_idx, q->nr_requests,
					       set->reserved_tags, set->flags);
	if (!hctx->sched_tags)
		return -ENOMEM;

	ret = blk_mq_alloc_rqs(set, hctx->sched_tags, hctx_idx, q->nr_requests);
	if (ret)
		blk_mq_sched_free_tags(set, hctx, hctx_idx);

	return ret;
}

/* called in queue's release handler, tagset has gone away */
static void blk_mq_sched_tags_teardown(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (hctx->sched_tags) {
			blk_mq_free_rq_map(hctx->sched_tags, hctx->flags);
			hctx->sched_tags = NULL;
		}
	}
}

static int blk_mq_init_sched_shared_sbitmap(struct request_queue *queue)
{
	struct blk_mq_tag_set *set = queue->tag_set;
	int alloc_policy = BLK_MQ_FLAG_TO_ALLOC_POLICY(set->flags);
	struct blk_mq_hw_ctx *hctx;
	int ret, i;

	/*
	 * Set initial depth at max so that we don't need to reallocate for
	 * updating nr_requests.
	 */
	ret = blk_mq_init_bitmaps(&queue->sched_bitmap_tags,
				  &queue->sched_breserved_tags,
				  MAX_SCHED_RQ, set->reserved_tags,
				  set->numa_node, alloc_policy);
	if (ret)
		return ret;

	queue_for_each_hw_ctx(queue, hctx, i) {
		hctx->sched_tags->bitmap_tags =
					&queue->sched_bitmap_tags;
		hctx->sched_tags->breserved_tags =
					&queue->sched_breserved_tags;
	}

	sbitmap_queue_resize(&queue->sched_bitmap_tags,
			     queue->nr_requests - set->reserved_tags);

	return 0;
}

static void blk_mq_exit_sched_shared_sbitmap(struct request_queue *queue)
{
	sbitmap_queue_free(&queue->sched_bitmap_tags);
	sbitmap_queue_free(&queue->sched_breserved_tags);
}

int blk_mq_init_sched(struct request_queue *q, struct elevator_type *e)
{
	struct blk_mq_hw_ctx *hctx;
	struct elevator_queue *eq;
	unsigned int i;
	int ret;

	if (!e) {
		q->elevator = NULL;
		q->nr_requests = q->tag_set->queue_depth;
		return 0;
	}

	/*
	 * Default to double of smaller one between hw queue_depth and 128,
	 * since we don't split into sync/async like the old code did.
	 * Additionally, this is a per-hw queue depth.
	 */
	q->nr_requests = 2 * min_t(unsigned int, q->tag_set->queue_depth,
				   BLKDEV_MAX_RQ);

	queue_for_each_hw_ctx(q, hctx, i) {
		ret = blk_mq_sched_alloc_tags(q, hctx, i);
		if (ret)
			goto err_free_tags;
	}

	if (blk_mq_is_sbitmap_shared(q->tag_set->flags)) {
		ret = blk_mq_init_sched_shared_sbitmap(q);
		if (ret)
			goto err_free_tags;
	}

	ret = e->ops.init_sched(q, e);
	if (ret)
		goto err_free_sbitmap;

	blk_mq_debugfs_register_sched(q);

	queue_for_each_hw_ctx(q, hctx, i) {
		if (e->ops.init_hctx) {
			ret = e->ops.init_hctx(hctx, i);
			if (ret) {
				eq = q->elevator;
				blk_mq_sched_free_requests(q);
				blk_mq_exit_sched(q, eq);
				kobject_put(&eq->kobj);
				return ret;
			}
		}
		blk_mq_debugfs_register_sched_hctx(q, hctx);
	}

	return 0;

err_free_sbitmap:
	if (blk_mq_is_sbitmap_shared(q->tag_set->flags))
		blk_mq_exit_sched_shared_sbitmap(q);
err_free_tags:
	blk_mq_sched_free_requests(q);
	blk_mq_sched_tags_teardown(q);
	q->elevator = NULL;
	return ret;
}

/*
 * called in either blk_queue_cleanup or elevator_switch, tagset
 * is required for freeing requests
 */
void blk_mq_sched_free_requests(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (hctx->sched_tags)
			blk_mq_free_rqs(q->tag_set, hctx->sched_tags, i);
	}
}

void blk_mq_exit_sched(struct request_queue *q, struct elevator_queue *e)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		blk_mq_debugfs_unregister_sched_hctx(hctx);
		if (e->type->ops.exit_hctx && hctx->sched_data) {
			e->type->ops.exit_hctx(hctx, i);
			hctx->sched_data = NULL;
		}
	}
	blk_mq_debugfs_unregister_sched(q);
	if (e->type->ops.exit_sched)
		e->type->ops.exit_sched(e);
	blk_mq_sched_tags_teardown(q);
	if (blk_mq_is_sbitmap_shared(q->tag_set->flags))
		blk_mq_exit_sched_shared_sbitmap(q);
	q->elevator = NULL;
}
