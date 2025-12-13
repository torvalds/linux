// SPDX-License-Identifier: GPL-2.0
/*
 * blk-mq scheduling framework
 *
 * Copyright (C) 2016 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list_sort.h>

#include <trace/events/block.h>

#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-debugfs.h"
#include "blk-mq-sched.h"
#include "blk-wbt.h"

/*
 * Mark a hardware queue as needing a restart.
 */
void blk_mq_sched_mark_restart_hctx(struct blk_mq_hw_ctx *hctx)
{
	if (test_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state))
		return;

	set_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state);
}
EXPORT_SYMBOL_GPL(blk_mq_sched_mark_restart_hctx);

void __blk_mq_sched_restart(struct blk_mq_hw_ctx *hctx)
{
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

	list_for_each_entry(rq, rq_list, queuelist) {
		if (rq->mq_hctx != hctx) {
			list_cut_before(&hctx_list, rq_list, &rq->queuelist);
			goto dispatch;
		}
	}
	list_splice_tail_init(rq_list, &hctx_list);

dispatch:
	return blk_mq_dispatch_rq_list(hctx, &hctx_list, false);
}

#define BLK_MQ_BUDGET_DELAY	3		/* ms units */

/*
 * Only SCSI implements .get_budget and .put_budget, and SCSI restarts
 * its queue by itself in its completion handler, so we don't need to
 * restart queue if .get_budget() fails to get the budget.
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
		count++;
		if (rq->mq_hctx != hctx)
			multi_hctxs = true;

		/*
		 * If we cannot get tag for the request, stop dequeueing
		 * requests from the IO scheduler. We are unlikely to be able
		 * to submit them anyway and it creates false impression for
		 * scheduling heuristics that the device can take more IO.
		 */
		if (!blk_mq_get_driver_tag(rq))
			break;
	} while (count < max_dispatch);

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
		dispatched = blk_mq_dispatch_rq_list(hctx, &rq_list, false);
	}

	if (busy)
		return -EAGAIN;
	return !!dispatched;
}

static int blk_mq_do_dispatch_sched(struct blk_mq_hw_ctx *hctx)
{
	unsigned long end = jiffies + HZ;
	int ret;

	do {
		ret = __blk_mq_do_dispatch_sched(hctx);
		if (ret != 1)
			break;
		if (need_resched() || time_is_before_jiffies(end)) {
			blk_mq_delay_run_hw_queue(hctx, 0);
			break;
		}
	} while (1);

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
 * restart queue if .get_budget() fails to get the budget.
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

	} while (blk_mq_dispatch_rq_list(rq->mq_hctx, &rq_list, false));

	WRITE_ONCE(hctx->dispatch_from, ctx);
	return ret;
}

static int __blk_mq_sched_dispatch_requests(struct blk_mq_hw_ctx *hctx)
{
	bool need_dispatch = false;
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
		if (!blk_mq_dispatch_rq_list(hctx, &rq_list, true))
			return 0;
		need_dispatch = true;
	} else {
		need_dispatch = hctx->dispatch_busy;
	}

	if (hctx->queue->elevator)
		return blk_mq_do_dispatch_sched(hctx);

	/* dequeue request one by one from sw queue if queue is busy */
	if (need_dispatch)
		return blk_mq_do_dispatch_ctx(hctx);
	blk_mq_flush_busy_ctxs(hctx, &rq_list);
	blk_mq_dispatch_rq_list(hctx, &rq_list, true);
	return 0;
}

void blk_mq_sched_dispatch_requests(struct blk_mq_hw_ctx *hctx)
{
	struct request_queue *q = hctx->queue;

	/* RCU or SRCU read lock is needed before checking quiesced flag */
	if (unlikely(blk_mq_hctx_stopped(hctx) || blk_queue_quiesced(q)))
		return;

	/*
	 * A return of -EAGAIN is an indication that hctx->dispatch is not
	 * empty and we must run again in order to avoid starving flushes.
	 */
	if (__blk_mq_sched_dispatch_requests(hctx) == -EAGAIN) {
		if (__blk_mq_sched_dispatch_requests(hctx) == -EAGAIN)
			blk_mq_run_hw_queue(hctx, true);
	}
}

bool blk_mq_sched_bio_merge(struct request_queue *q, struct bio *bio,
		unsigned int nr_segs)
{
	struct elevator_queue *e = q->elevator;
	struct blk_mq_ctx *ctx;
	struct blk_mq_hw_ctx *hctx;
	bool ret = false;
	enum hctx_type type;

	if (e && e->type->ops.bio_merge) {
		ret = e->type->ops.bio_merge(q, bio, nr_segs);
		goto out_put;
	}

	ctx = blk_mq_get_ctx(q);
	hctx = blk_mq_map_queue(bio->bi_opf, ctx);
	type = hctx->type;
	if (list_empty_careful(&ctx->rq_lists[type]))
		goto out_put;

	/* default per sw-queue merge */
	spin_lock(&ctx->lock);
	/*
	 * Reverse check our software queue for entries that we could
	 * potentially merge with. Currently includes a hand-wavy stop
	 * count of 8, to not spend too much time checking for merges.
	 */
	if (blk_bio_list_merge(q, &ctx->rq_lists[type], bio, nr_segs))
		ret = true;

	spin_unlock(&ctx->lock);
out_put:
	return ret;
}

bool blk_mq_sched_try_insert_merge(struct request_queue *q, struct request *rq,
				   struct list_head *free)
{
	return rq_mergeable(rq) && elv_attempt_insert_merge(q, rq, free);
}
EXPORT_SYMBOL_GPL(blk_mq_sched_try_insert_merge);

/* called in queue's release handler, tagset has gone away */
static void blk_mq_sched_tags_teardown(struct request_queue *q, unsigned int flags)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned long i;

	queue_for_each_hw_ctx(q, hctx, i)
		hctx->sched_tags = NULL;

	if (blk_mq_is_shared_tags(flags))
		q->sched_shared_tags = NULL;
}

void blk_mq_sched_reg_debugfs(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned long i;

	mutex_lock(&q->debugfs_mutex);
	blk_mq_debugfs_register_sched(q);
	queue_for_each_hw_ctx(q, hctx, i)
		blk_mq_debugfs_register_sched_hctx(q, hctx);
	mutex_unlock(&q->debugfs_mutex);
}

void blk_mq_sched_unreg_debugfs(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned long i;

	mutex_lock(&q->debugfs_mutex);
	queue_for_each_hw_ctx(q, hctx, i)
		blk_mq_debugfs_unregister_sched_hctx(hctx);
	blk_mq_debugfs_unregister_sched(q);
	mutex_unlock(&q->debugfs_mutex);
}

void blk_mq_free_sched_tags(struct elevator_tags *et,
		struct blk_mq_tag_set *set)
{
	unsigned long i;

	/* Shared tags are stored at index 0 in @tags. */
	if (blk_mq_is_shared_tags(set->flags))
		blk_mq_free_map_and_rqs(set, et->tags[0], BLK_MQ_NO_HCTX_IDX);
	else {
		for (i = 0; i < et->nr_hw_queues; i++)
			blk_mq_free_map_and_rqs(set, et->tags[i], i);
	}

	kfree(et);
}

void blk_mq_free_sched_tags_batch(struct xarray *et_table,
		struct blk_mq_tag_set *set)
{
	struct request_queue *q;
	struct elevator_tags *et;

	lockdep_assert_held_write(&set->update_nr_hwq_lock);

	list_for_each_entry(q, &set->tag_list, tag_set_list) {
		/*
		 * Accessing q->elevator without holding q->elevator_lock is
		 * safe because we're holding here set->update_nr_hwq_lock in
		 * the writer context. So, scheduler update/switch code (which
		 * acquires the same lock but in the reader context) can't run
		 * concurrently.
		 */
		if (q->elevator) {
			et = xa_load(et_table, q->id);
			if (unlikely(!et))
				WARN_ON_ONCE(1);
			else
				blk_mq_free_sched_tags(et, set);
		}
	}
}

struct elevator_tags *blk_mq_alloc_sched_tags(struct blk_mq_tag_set *set,
		unsigned int nr_hw_queues, unsigned int nr_requests)
{
	unsigned int nr_tags;
	int i;
	struct elevator_tags *et;
	gfp_t gfp = GFP_NOIO | __GFP_ZERO | __GFP_NOWARN | __GFP_NORETRY;

	if (blk_mq_is_shared_tags(set->flags))
		nr_tags = 1;
	else
		nr_tags = nr_hw_queues;

	et = kmalloc(sizeof(struct elevator_tags) +
			nr_tags * sizeof(struct blk_mq_tags *), gfp);
	if (!et)
		return NULL;

	et->nr_requests = nr_requests;
	et->nr_hw_queues = nr_hw_queues;

	if (blk_mq_is_shared_tags(set->flags)) {
		/* Shared tags are stored at index 0 in @tags. */
		et->tags[0] = blk_mq_alloc_map_and_rqs(set, BLK_MQ_NO_HCTX_IDX,
					MAX_SCHED_RQ);
		if (!et->tags[0])
			goto out;
	} else {
		for (i = 0; i < et->nr_hw_queues; i++) {
			et->tags[i] = blk_mq_alloc_map_and_rqs(set, i,
					et->nr_requests);
			if (!et->tags[i])
				goto out_unwind;
		}
	}

	return et;
out_unwind:
	while (--i >= 0)
		blk_mq_free_map_and_rqs(set, et->tags[i], i);
out:
	kfree(et);
	return NULL;
}

int blk_mq_alloc_sched_tags_batch(struct xarray *et_table,
		struct blk_mq_tag_set *set, unsigned int nr_hw_queues)
{
	struct request_queue *q;
	struct elevator_tags *et;
	gfp_t gfp = GFP_NOIO | __GFP_ZERO | __GFP_NOWARN | __GFP_NORETRY;

	lockdep_assert_held_write(&set->update_nr_hwq_lock);

	list_for_each_entry(q, &set->tag_list, tag_set_list) {
		/*
		 * Accessing q->elevator without holding q->elevator_lock is
		 * safe because we're holding here set->update_nr_hwq_lock in
		 * the writer context. So, scheduler update/switch code (which
		 * acquires the same lock but in the reader context) can't run
		 * concurrently.
		 */
		if (q->elevator) {
			et = blk_mq_alloc_sched_tags(set, nr_hw_queues,
					blk_mq_default_nr_requests(set));
			if (!et)
				goto out_unwind;
			if (xa_insert(et_table, q->id, et, gfp))
				goto out_free_tags;
		}
	}
	return 0;
out_free_tags:
	blk_mq_free_sched_tags(et, set);
out_unwind:
	list_for_each_entry_continue_reverse(q, &set->tag_list, tag_set_list) {
		if (q->elevator) {
			et = xa_load(et_table, q->id);
			if (et)
				blk_mq_free_sched_tags(et, set);
		}
	}
	return -ENOMEM;
}

/* caller must have a reference to @e, will grab another one if successful */
int blk_mq_init_sched(struct request_queue *q, struct elevator_type *e,
		struct elevator_tags *et)
{
	unsigned int flags = q->tag_set->flags;
	struct blk_mq_hw_ctx *hctx;
	struct elevator_queue *eq;
	unsigned long i;
	int ret;

	eq = elevator_alloc(q, e, et);
	if (!eq)
		return -ENOMEM;

	q->nr_requests = et->nr_requests;

	if (blk_mq_is_shared_tags(flags)) {
		/* Shared tags are stored at index 0 in @et->tags. */
		q->sched_shared_tags = et->tags[0];
		blk_mq_tag_update_sched_shared_tags(q, et->nr_requests);
	}

	queue_for_each_hw_ctx(q, hctx, i) {
		if (blk_mq_is_shared_tags(flags))
			hctx->sched_tags = q->sched_shared_tags;
		else
			hctx->sched_tags = et->tags[i];
	}

	ret = e->ops.init_sched(q, eq);
	if (ret)
		goto out;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (e->ops.init_hctx) {
			ret = e->ops.init_hctx(hctx, i);
			if (ret) {
				blk_mq_exit_sched(q, eq);
				kobject_put(&eq->kobj);
				return ret;
			}
		}
	}
	return 0;

out:
	blk_mq_sched_tags_teardown(q, flags);
	kobject_put(&eq->kobj);
	q->elevator = NULL;
	return ret;
}

/*
 * called in either blk_queue_cleanup or elevator_switch, tagset
 * is required for freeing requests
 */
void blk_mq_sched_free_rqs(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned long i;

	if (blk_mq_is_shared_tags(q->tag_set->flags)) {
		blk_mq_free_rqs(q->tag_set, q->sched_shared_tags,
				BLK_MQ_NO_HCTX_IDX);
	} else {
		queue_for_each_hw_ctx(q, hctx, i) {
			if (hctx->sched_tags)
				blk_mq_free_rqs(q->tag_set,
						hctx->sched_tags, i);
		}
	}
}

void blk_mq_exit_sched(struct request_queue *q, struct elevator_queue *e)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned long i;
	unsigned int flags = 0;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (e->type->ops.exit_hctx && hctx->sched_data) {
			e->type->ops.exit_hctx(hctx, i);
			hctx->sched_data = NULL;
		}
		flags = hctx->flags;
	}

	if (e->type->ops.exit_sched)
		e->type->ops.exit_sched(e);
	blk_mq_sched_tags_teardown(q, flags);
	set_bit(ELEVATOR_FLAG_DYING, &q->elevator->flags);
	q->elevator = NULL;
}
