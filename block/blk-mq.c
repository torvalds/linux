/*
 * Block multiqueue core code
 *
 * Copyright (C) 2013-2014 Jens Axboe
 * Copyright (C) 2013-2014 Christoph Hellwig
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/smp.h>
#include <linux/llist.h>
#include <linux/list_sort.h>
#include <linux/cpu.h>
#include <linux/cache.h>
#include <linux/sched/sysctl.h>
#include <linux/delay.h>

#include <trace/events/block.h>

#include <linux/blk-mq.h>
#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-tag.h"

static DEFINE_MUTEX(all_q_mutex);
static LIST_HEAD(all_q_list);

static void __blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx);

/*
 * Check if any of the ctx's have pending work in this hardware queue
 */
static bool blk_mq_hctx_has_pending(struct blk_mq_hw_ctx *hctx)
{
	unsigned int i;

	for (i = 0; i < hctx->ctx_map.map_size; i++)
		if (hctx->ctx_map.map[i].word)
			return true;

	return false;
}

static inline struct blk_align_bitmap *get_bm(struct blk_mq_hw_ctx *hctx,
					      struct blk_mq_ctx *ctx)
{
	return &hctx->ctx_map.map[ctx->index_hw / hctx->ctx_map.bits_per_word];
}

#define CTX_TO_BIT(hctx, ctx)	\
	((ctx)->index_hw & ((hctx)->ctx_map.bits_per_word - 1))

/*
 * Mark this ctx as having pending work in this hardware queue
 */
static void blk_mq_hctx_mark_pending(struct blk_mq_hw_ctx *hctx,
				     struct blk_mq_ctx *ctx)
{
	struct blk_align_bitmap *bm = get_bm(hctx, ctx);

	if (!test_bit(CTX_TO_BIT(hctx, ctx), &bm->word))
		set_bit(CTX_TO_BIT(hctx, ctx), &bm->word);
}

static void blk_mq_hctx_clear_pending(struct blk_mq_hw_ctx *hctx,
				      struct blk_mq_ctx *ctx)
{
	struct blk_align_bitmap *bm = get_bm(hctx, ctx);

	clear_bit(CTX_TO_BIT(hctx, ctx), &bm->word);
}

static int blk_mq_queue_enter(struct request_queue *q)
{
	int ret;

	__percpu_counter_add(&q->mq_usage_counter, 1, 1000000);
	smp_mb();

	/* we have problems freezing the queue if it's initializing */
	if (!blk_queue_dying(q) &&
	    (!blk_queue_bypass(q) || !blk_queue_init_done(q)))
		return 0;

	__percpu_counter_add(&q->mq_usage_counter, -1, 1000000);

	spin_lock_irq(q->queue_lock);
	ret = wait_event_interruptible_lock_irq(q->mq_freeze_wq,
		!blk_queue_bypass(q) || blk_queue_dying(q),
		*q->queue_lock);
	/* inc usage with lock hold to avoid freeze_queue runs here */
	if (!ret && !blk_queue_dying(q))
		__percpu_counter_add(&q->mq_usage_counter, 1, 1000000);
	else if (blk_queue_dying(q))
		ret = -ENODEV;
	spin_unlock_irq(q->queue_lock);

	return ret;
}

static void blk_mq_queue_exit(struct request_queue *q)
{
	__percpu_counter_add(&q->mq_usage_counter, -1, 1000000);
}

void blk_mq_drain_queue(struct request_queue *q)
{
	while (true) {
		s64 count;

		spin_lock_irq(q->queue_lock);
		count = percpu_counter_sum(&q->mq_usage_counter);
		spin_unlock_irq(q->queue_lock);

		if (count == 0)
			break;
		blk_mq_start_hw_queues(q);
		msleep(10);
	}
}

/*
 * Guarantee no request is in use, so we can change any data structure of
 * the queue afterward.
 */
static void blk_mq_freeze_queue(struct request_queue *q)
{
	bool drain;

	spin_lock_irq(q->queue_lock);
	drain = !q->bypass_depth++;
	queue_flag_set(QUEUE_FLAG_BYPASS, q);
	spin_unlock_irq(q->queue_lock);

	if (drain)
		blk_mq_drain_queue(q);
}

static void blk_mq_unfreeze_queue(struct request_queue *q)
{
	bool wake = false;

	spin_lock_irq(q->queue_lock);
	if (!--q->bypass_depth) {
		queue_flag_clear(QUEUE_FLAG_BYPASS, q);
		wake = true;
	}
	WARN_ON_ONCE(q->bypass_depth < 0);
	spin_unlock_irq(q->queue_lock);
	if (wake)
		wake_up_all(&q->mq_freeze_wq);
}

bool blk_mq_can_queue(struct blk_mq_hw_ctx *hctx)
{
	return blk_mq_has_free_tags(hctx->tags);
}
EXPORT_SYMBOL(blk_mq_can_queue);

static void blk_mq_rq_ctx_init(struct request_queue *q, struct blk_mq_ctx *ctx,
			       struct request *rq, unsigned int rw_flags)
{
	if (blk_queue_io_stat(q))
		rw_flags |= REQ_IO_STAT;

	INIT_LIST_HEAD(&rq->queuelist);
	/* csd/requeue_work/fifo_time is initialized before use */
	rq->q = q;
	rq->mq_ctx = ctx;
	rq->cmd_flags |= rw_flags;
	/* do not touch atomic flags, it needs atomic ops against the timer */
	rq->cpu = -1;
	INIT_HLIST_NODE(&rq->hash);
	RB_CLEAR_NODE(&rq->rb_node);
	rq->rq_disk = NULL;
	rq->part = NULL;
	rq->start_time = jiffies;
#ifdef CONFIG_BLK_CGROUP
	rq->rl = NULL;
	set_start_time_ns(rq);
	rq->io_start_time_ns = 0;
#endif
	rq->nr_phys_segments = 0;
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	rq->nr_integrity_segments = 0;
#endif
	rq->special = NULL;
	/* tag was already set */
	rq->errors = 0;

	rq->extra_len = 0;
	rq->sense_len = 0;
	rq->resid_len = 0;
	rq->sense = NULL;

	INIT_LIST_HEAD(&rq->timeout_list);
	rq->timeout = 0;

	rq->end_io = NULL;
	rq->end_io_data = NULL;
	rq->next_rq = NULL;

	ctx->rq_dispatched[rw_is_sync(rw_flags)]++;
}

static struct request *
__blk_mq_alloc_request(struct blk_mq_alloc_data *data, int rw)
{
	struct request *rq;
	unsigned int tag;

	tag = blk_mq_get_tag(data);
	if (tag != BLK_MQ_TAG_FAIL) {
		rq = data->hctx->tags->rqs[tag];

		rq->cmd_flags = 0;
		if (blk_mq_tag_busy(data->hctx)) {
			rq->cmd_flags = REQ_MQ_INFLIGHT;
			atomic_inc(&data->hctx->nr_active);
		}

		rq->tag = tag;
		blk_mq_rq_ctx_init(data->q, data->ctx, rq, rw);
		return rq;
	}

	return NULL;
}

struct request *blk_mq_alloc_request(struct request_queue *q, int rw, gfp_t gfp,
		bool reserved)
{
	struct blk_mq_ctx *ctx;
	struct blk_mq_hw_ctx *hctx;
	struct request *rq;
	struct blk_mq_alloc_data alloc_data;

	if (blk_mq_queue_enter(q))
		return NULL;

	ctx = blk_mq_get_ctx(q);
	hctx = q->mq_ops->map_queue(q, ctx->cpu);
	blk_mq_set_alloc_data(&alloc_data, q, gfp & ~__GFP_WAIT,
			reserved, ctx, hctx);

	rq = __blk_mq_alloc_request(&alloc_data, rw);
	if (!rq && (gfp & __GFP_WAIT)) {
		__blk_mq_run_hw_queue(hctx);
		blk_mq_put_ctx(ctx);

		ctx = blk_mq_get_ctx(q);
		hctx = q->mq_ops->map_queue(q, ctx->cpu);
		blk_mq_set_alloc_data(&alloc_data, q, gfp, reserved, ctx,
				hctx);
		rq =  __blk_mq_alloc_request(&alloc_data, rw);
		ctx = alloc_data.ctx;
	}
	blk_mq_put_ctx(ctx);
	return rq;
}
EXPORT_SYMBOL(blk_mq_alloc_request);

static void __blk_mq_free_request(struct blk_mq_hw_ctx *hctx,
				  struct blk_mq_ctx *ctx, struct request *rq)
{
	const int tag = rq->tag;
	struct request_queue *q = rq->q;

	if (rq->cmd_flags & REQ_MQ_INFLIGHT)
		atomic_dec(&hctx->nr_active);

	clear_bit(REQ_ATOM_STARTED, &rq->atomic_flags);
	blk_mq_put_tag(hctx, tag, &ctx->last_tag);
	blk_mq_queue_exit(q);
}

void blk_mq_free_request(struct request *rq)
{
	struct blk_mq_ctx *ctx = rq->mq_ctx;
	struct blk_mq_hw_ctx *hctx;
	struct request_queue *q = rq->q;

	ctx->rq_completed[rq_is_sync(rq)]++;

	hctx = q->mq_ops->map_queue(q, ctx->cpu);
	__blk_mq_free_request(hctx, ctx, rq);
}

/*
 * Clone all relevant state from a request that has been put on hold in
 * the flush state machine into the preallocated flush request that hangs
 * off the request queue.
 *
 * For a driver the flush request should be invisible, that's why we are
 * impersonating the original request here.
 */
void blk_mq_clone_flush_request(struct request *flush_rq,
		struct request *orig_rq)
{
	struct blk_mq_hw_ctx *hctx =
		orig_rq->q->mq_ops->map_queue(orig_rq->q, orig_rq->mq_ctx->cpu);

	flush_rq->mq_ctx = orig_rq->mq_ctx;
	flush_rq->tag = orig_rq->tag;
	memcpy(blk_mq_rq_to_pdu(flush_rq), blk_mq_rq_to_pdu(orig_rq),
		hctx->cmd_size);
}

inline void __blk_mq_end_io(struct request *rq, int error)
{
	blk_account_io_done(rq);

	if (rq->end_io) {
		rq->end_io(rq, error);
	} else {
		if (unlikely(blk_bidi_rq(rq)))
			blk_mq_free_request(rq->next_rq);
		blk_mq_free_request(rq);
	}
}
EXPORT_SYMBOL(__blk_mq_end_io);

void blk_mq_end_io(struct request *rq, int error)
{
	if (blk_update_request(rq, error, blk_rq_bytes(rq)))
		BUG();
	__blk_mq_end_io(rq, error);
}
EXPORT_SYMBOL(blk_mq_end_io);

static void __blk_mq_complete_request_remote(void *data)
{
	struct request *rq = data;

	rq->q->softirq_done_fn(rq);
}

static void blk_mq_ipi_complete_request(struct request *rq)
{
	struct blk_mq_ctx *ctx = rq->mq_ctx;
	bool shared = false;
	int cpu;

	if (!test_bit(QUEUE_FLAG_SAME_COMP, &rq->q->queue_flags)) {
		rq->q->softirq_done_fn(rq);
		return;
	}

	cpu = get_cpu();
	if (!test_bit(QUEUE_FLAG_SAME_FORCE, &rq->q->queue_flags))
		shared = cpus_share_cache(cpu, ctx->cpu);

	if (cpu != ctx->cpu && !shared && cpu_online(ctx->cpu)) {
		rq->csd.func = __blk_mq_complete_request_remote;
		rq->csd.info = rq;
		rq->csd.flags = 0;
		smp_call_function_single_async(ctx->cpu, &rq->csd);
	} else {
		rq->q->softirq_done_fn(rq);
	}
	put_cpu();
}

void __blk_mq_complete_request(struct request *rq)
{
	struct request_queue *q = rq->q;

	if (!q->softirq_done_fn)
		blk_mq_end_io(rq, rq->errors);
	else
		blk_mq_ipi_complete_request(rq);
}

/**
 * blk_mq_complete_request - end I/O on a request
 * @rq:		the request being processed
 *
 * Description:
 *	Ends all I/O on a request. It does not handle partial completions.
 *	The actual completion happens out-of-order, through a IPI handler.
 **/
void blk_mq_complete_request(struct request *rq)
{
	struct request_queue *q = rq->q;

	if (unlikely(blk_should_fake_timeout(q)))
		return;
	if (!blk_mark_rq_complete(rq))
		__blk_mq_complete_request(rq);
}
EXPORT_SYMBOL(blk_mq_complete_request);

static void blk_mq_start_request(struct request *rq, bool last)
{
	struct request_queue *q = rq->q;

	trace_block_rq_issue(q, rq);

	rq->resid_len = blk_rq_bytes(rq);
	if (unlikely(blk_bidi_rq(rq)))
		rq->next_rq->resid_len = blk_rq_bytes(rq->next_rq);

	blk_add_timer(rq);

	/*
	 * Mark us as started and clear complete. Complete might have been
	 * set if requeue raced with timeout, which then marked it as
	 * complete. So be sure to clear complete again when we start
	 * the request, otherwise we'll ignore the completion event.
	 */
	if (!test_bit(REQ_ATOM_STARTED, &rq->atomic_flags))
		set_bit(REQ_ATOM_STARTED, &rq->atomic_flags);
	if (test_bit(REQ_ATOM_COMPLETE, &rq->atomic_flags))
		clear_bit(REQ_ATOM_COMPLETE, &rq->atomic_flags);

	if (q->dma_drain_size && blk_rq_bytes(rq)) {
		/*
		 * Make sure space for the drain appears.  We know we can do
		 * this because max_hw_segments has been adjusted to be one
		 * fewer than the device can handle.
		 */
		rq->nr_phys_segments++;
	}

	/*
	 * Flag the last request in the series so that drivers know when IO
	 * should be kicked off, if they don't do it on a per-request basis.
	 *
	 * Note: the flag isn't the only condition drivers should do kick off.
	 * If drive is busy, the last request might not have the bit set.
	 */
	if (last)
		rq->cmd_flags |= REQ_END;
}

static void __blk_mq_requeue_request(struct request *rq)
{
	struct request_queue *q = rq->q;

	trace_block_rq_requeue(q, rq);
	clear_bit(REQ_ATOM_STARTED, &rq->atomic_flags);

	rq->cmd_flags &= ~REQ_END;

	if (q->dma_drain_size && blk_rq_bytes(rq))
		rq->nr_phys_segments--;
}

void blk_mq_requeue_request(struct request *rq)
{
	__blk_mq_requeue_request(rq);
	blk_clear_rq_complete(rq);

	BUG_ON(blk_queued_rq(rq));
	blk_mq_add_to_requeue_list(rq, true);
}
EXPORT_SYMBOL(blk_mq_requeue_request);

static void blk_mq_requeue_work(struct work_struct *work)
{
	struct request_queue *q =
		container_of(work, struct request_queue, requeue_work);
	LIST_HEAD(rq_list);
	struct request *rq, *next;
	unsigned long flags;

	spin_lock_irqsave(&q->requeue_lock, flags);
	list_splice_init(&q->requeue_list, &rq_list);
	spin_unlock_irqrestore(&q->requeue_lock, flags);

	list_for_each_entry_safe(rq, next, &rq_list, queuelist) {
		if (!(rq->cmd_flags & REQ_SOFTBARRIER))
			continue;

		rq->cmd_flags &= ~REQ_SOFTBARRIER;
		list_del_init(&rq->queuelist);
		blk_mq_insert_request(rq, true, false, false);
	}

	while (!list_empty(&rq_list)) {
		rq = list_entry(rq_list.next, struct request, queuelist);
		list_del_init(&rq->queuelist);
		blk_mq_insert_request(rq, false, false, false);
	}

	blk_mq_run_queues(q, false);
}

void blk_mq_add_to_requeue_list(struct request *rq, bool at_head)
{
	struct request_queue *q = rq->q;
	unsigned long flags;

	/*
	 * We abuse this flag that is otherwise used by the I/O scheduler to
	 * request head insertation from the workqueue.
	 */
	BUG_ON(rq->cmd_flags & REQ_SOFTBARRIER);

	spin_lock_irqsave(&q->requeue_lock, flags);
	if (at_head) {
		rq->cmd_flags |= REQ_SOFTBARRIER;
		list_add(&rq->queuelist, &q->requeue_list);
	} else {
		list_add_tail(&rq->queuelist, &q->requeue_list);
	}
	spin_unlock_irqrestore(&q->requeue_lock, flags);
}
EXPORT_SYMBOL(blk_mq_add_to_requeue_list);

void blk_mq_kick_requeue_list(struct request_queue *q)
{
	kblockd_schedule_work(&q->requeue_work);
}
EXPORT_SYMBOL(blk_mq_kick_requeue_list);

static inline bool is_flush_request(struct request *rq, unsigned int tag)
{
	return ((rq->cmd_flags & REQ_FLUSH_SEQ) &&
			rq->q->flush_rq->tag == tag);
}

struct request *blk_mq_tag_to_rq(struct blk_mq_tags *tags, unsigned int tag)
{
	struct request *rq = tags->rqs[tag];

	if (!is_flush_request(rq, tag))
		return rq;

	return rq->q->flush_rq;
}
EXPORT_SYMBOL(blk_mq_tag_to_rq);

struct blk_mq_timeout_data {
	struct blk_mq_hw_ctx *hctx;
	unsigned long *next;
	unsigned int *next_set;
};

static void blk_mq_timeout_check(void *__data, unsigned long *free_tags)
{
	struct blk_mq_timeout_data *data = __data;
	struct blk_mq_hw_ctx *hctx = data->hctx;
	unsigned int tag;

	 /* It may not be in flight yet (this is where
	 * the REQ_ATOMIC_STARTED flag comes in). The requests are
	 * statically allocated, so we know it's always safe to access the
	 * memory associated with a bit offset into ->rqs[].
	 */
	tag = 0;
	do {
		struct request *rq;

		tag = find_next_zero_bit(free_tags, hctx->tags->nr_tags, tag);
		if (tag >= hctx->tags->nr_tags)
			break;

		rq = blk_mq_tag_to_rq(hctx->tags, tag++);
		if (rq->q != hctx->queue)
			continue;
		if (!test_bit(REQ_ATOM_STARTED, &rq->atomic_flags))
			continue;

		blk_rq_check_expired(rq, data->next, data->next_set);
	} while (1);
}

static void blk_mq_hw_ctx_check_timeout(struct blk_mq_hw_ctx *hctx,
					unsigned long *next,
					unsigned int *next_set)
{
	struct blk_mq_timeout_data data = {
		.hctx		= hctx,
		.next		= next,
		.next_set	= next_set,
	};

	/*
	 * Ask the tagging code to iterate busy requests, so we can
	 * check them for timeout.
	 */
	blk_mq_tag_busy_iter(hctx->tags, blk_mq_timeout_check, &data);
}

static enum blk_eh_timer_return blk_mq_rq_timed_out(struct request *rq)
{
	struct request_queue *q = rq->q;

	/*
	 * We know that complete is set at this point. If STARTED isn't set
	 * anymore, then the request isn't active and the "timeout" should
	 * just be ignored. This can happen due to the bitflag ordering.
	 * Timeout first checks if STARTED is set, and if it is, assumes
	 * the request is active. But if we race with completion, then
	 * we both flags will get cleared. So check here again, and ignore
	 * a timeout event with a request that isn't active.
	 */
	if (!test_bit(REQ_ATOM_STARTED, &rq->atomic_flags))
		return BLK_EH_NOT_HANDLED;

	if (!q->mq_ops->timeout)
		return BLK_EH_RESET_TIMER;

	return q->mq_ops->timeout(rq);
}

static void blk_mq_rq_timer(unsigned long data)
{
	struct request_queue *q = (struct request_queue *) data;
	struct blk_mq_hw_ctx *hctx;
	unsigned long next = 0;
	int i, next_set = 0;

	queue_for_each_hw_ctx(q, hctx, i) {
		/*
		 * If not software queues are currently mapped to this
		 * hardware queue, there's nothing to check
		 */
		if (!hctx->nr_ctx || !hctx->tags)
			continue;

		blk_mq_hw_ctx_check_timeout(hctx, &next, &next_set);
	}

	if (next_set) {
		next = blk_rq_timeout(round_jiffies_up(next));
		mod_timer(&q->timeout, next);
	} else {
		queue_for_each_hw_ctx(q, hctx, i)
			blk_mq_tag_idle(hctx);
	}
}

/*
 * Reverse check our software queue for entries that we could potentially
 * merge with. Currently includes a hand-wavy stop count of 8, to not spend
 * too much time checking for merges.
 */
static bool blk_mq_attempt_merge(struct request_queue *q,
				 struct blk_mq_ctx *ctx, struct bio *bio)
{
	struct request *rq;
	int checked = 8;

	list_for_each_entry_reverse(rq, &ctx->rq_list, queuelist) {
		int el_ret;

		if (!checked--)
			break;

		if (!blk_rq_merge_ok(rq, bio))
			continue;

		el_ret = blk_try_merge(rq, bio);
		if (el_ret == ELEVATOR_BACK_MERGE) {
			if (bio_attempt_back_merge(q, rq, bio)) {
				ctx->rq_merged++;
				return true;
			}
			break;
		} else if (el_ret == ELEVATOR_FRONT_MERGE) {
			if (bio_attempt_front_merge(q, rq, bio)) {
				ctx->rq_merged++;
				return true;
			}
			break;
		}
	}

	return false;
}

/*
 * Process software queues that have been marked busy, splicing them
 * to the for-dispatch
 */
static void flush_busy_ctxs(struct blk_mq_hw_ctx *hctx, struct list_head *list)
{
	struct blk_mq_ctx *ctx;
	int i;

	for (i = 0; i < hctx->ctx_map.map_size; i++) {
		struct blk_align_bitmap *bm = &hctx->ctx_map.map[i];
		unsigned int off, bit;

		if (!bm->word)
			continue;

		bit = 0;
		off = i * hctx->ctx_map.bits_per_word;
		do {
			bit = find_next_bit(&bm->word, bm->depth, bit);
			if (bit >= bm->depth)
				break;

			ctx = hctx->ctxs[bit + off];
			clear_bit(bit, &bm->word);
			spin_lock(&ctx->lock);
			list_splice_tail_init(&ctx->rq_list, list);
			spin_unlock(&ctx->lock);

			bit++;
		} while (1);
	}
}

/*
 * Run this hardware queue, pulling any software queues mapped to it in.
 * Note that this function currently has various problems around ordering
 * of IO. In particular, we'd like FIFO behaviour on handling existing
 * items on the hctx->dispatch list. Ignore that for now.
 */
static void __blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx)
{
	struct request_queue *q = hctx->queue;
	struct request *rq;
	LIST_HEAD(rq_list);
	int queued;

	WARN_ON(!cpumask_test_cpu(raw_smp_processor_id(), hctx->cpumask));

	if (unlikely(test_bit(BLK_MQ_S_STOPPED, &hctx->state)))
		return;

	hctx->run++;

	/*
	 * Touch any software queue that has pending entries.
	 */
	flush_busy_ctxs(hctx, &rq_list);

	/*
	 * If we have previous entries on our dispatch list, grab them
	 * and stuff them at the front for more fair dispatch.
	 */
	if (!list_empty_careful(&hctx->dispatch)) {
		spin_lock(&hctx->lock);
		if (!list_empty(&hctx->dispatch))
			list_splice_init(&hctx->dispatch, &rq_list);
		spin_unlock(&hctx->lock);
	}

	/*
	 * Now process all the entries, sending them to the driver.
	 */
	queued = 0;
	while (!list_empty(&rq_list)) {
		int ret;

		rq = list_first_entry(&rq_list, struct request, queuelist);
		list_del_init(&rq->queuelist);

		blk_mq_start_request(rq, list_empty(&rq_list));

		ret = q->mq_ops->queue_rq(hctx, rq);
		switch (ret) {
		case BLK_MQ_RQ_QUEUE_OK:
			queued++;
			continue;
		case BLK_MQ_RQ_QUEUE_BUSY:
			list_add(&rq->queuelist, &rq_list);
			__blk_mq_requeue_request(rq);
			break;
		default:
			pr_err("blk-mq: bad return on queue: %d\n", ret);
		case BLK_MQ_RQ_QUEUE_ERROR:
			rq->errors = -EIO;
			blk_mq_end_io(rq, rq->errors);
			break;
		}

		if (ret == BLK_MQ_RQ_QUEUE_BUSY)
			break;
	}

	if (!queued)
		hctx->dispatched[0]++;
	else if (queued < (1 << (BLK_MQ_MAX_DISPATCH_ORDER - 1)))
		hctx->dispatched[ilog2(queued) + 1]++;

	/*
	 * Any items that need requeuing? Stuff them into hctx->dispatch,
	 * that is where we will continue on next queue run.
	 */
	if (!list_empty(&rq_list)) {
		spin_lock(&hctx->lock);
		list_splice(&rq_list, &hctx->dispatch);
		spin_unlock(&hctx->lock);
	}
}

/*
 * It'd be great if the workqueue API had a way to pass
 * in a mask and had some smarts for more clever placement.
 * For now we just round-robin here, switching for every
 * BLK_MQ_CPU_WORK_BATCH queued items.
 */
static int blk_mq_hctx_next_cpu(struct blk_mq_hw_ctx *hctx)
{
	int cpu = hctx->next_cpu;

	if (--hctx->next_cpu_batch <= 0) {
		int next_cpu;

		next_cpu = cpumask_next(hctx->next_cpu, hctx->cpumask);
		if (next_cpu >= nr_cpu_ids)
			next_cpu = cpumask_first(hctx->cpumask);

		hctx->next_cpu = next_cpu;
		hctx->next_cpu_batch = BLK_MQ_CPU_WORK_BATCH;
	}

	return cpu;
}

void blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx, bool async)
{
	if (unlikely(test_bit(BLK_MQ_S_STOPPED, &hctx->state)))
		return;

	if (!async && cpumask_test_cpu(smp_processor_id(), hctx->cpumask))
		__blk_mq_run_hw_queue(hctx);
	else if (hctx->queue->nr_hw_queues == 1)
		kblockd_schedule_delayed_work(&hctx->run_work, 0);
	else {
		unsigned int cpu;

		cpu = blk_mq_hctx_next_cpu(hctx);
		kblockd_schedule_delayed_work_on(cpu, &hctx->run_work, 0);
	}
}

void blk_mq_run_queues(struct request_queue *q, bool async)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if ((!blk_mq_hctx_has_pending(hctx) &&
		    list_empty_careful(&hctx->dispatch)) ||
		    test_bit(BLK_MQ_S_STOPPED, &hctx->state))
			continue;

		preempt_disable();
		blk_mq_run_hw_queue(hctx, async);
		preempt_enable();
	}
}
EXPORT_SYMBOL(blk_mq_run_queues);

void blk_mq_stop_hw_queue(struct blk_mq_hw_ctx *hctx)
{
	cancel_delayed_work(&hctx->run_work);
	cancel_delayed_work(&hctx->delay_work);
	set_bit(BLK_MQ_S_STOPPED, &hctx->state);
}
EXPORT_SYMBOL(blk_mq_stop_hw_queue);

void blk_mq_stop_hw_queues(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i)
		blk_mq_stop_hw_queue(hctx);
}
EXPORT_SYMBOL(blk_mq_stop_hw_queues);

void blk_mq_start_hw_queue(struct blk_mq_hw_ctx *hctx)
{
	clear_bit(BLK_MQ_S_STOPPED, &hctx->state);

	preempt_disable();
	blk_mq_run_hw_queue(hctx, false);
	preempt_enable();
}
EXPORT_SYMBOL(blk_mq_start_hw_queue);

void blk_mq_start_hw_queues(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i)
		blk_mq_start_hw_queue(hctx);
}
EXPORT_SYMBOL(blk_mq_start_hw_queues);


void blk_mq_start_stopped_hw_queues(struct request_queue *q, bool async)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (!test_bit(BLK_MQ_S_STOPPED, &hctx->state))
			continue;

		clear_bit(BLK_MQ_S_STOPPED, &hctx->state);
		preempt_disable();
		blk_mq_run_hw_queue(hctx, async);
		preempt_enable();
	}
}
EXPORT_SYMBOL(blk_mq_start_stopped_hw_queues);

static void blk_mq_run_work_fn(struct work_struct *work)
{
	struct blk_mq_hw_ctx *hctx;

	hctx = container_of(work, struct blk_mq_hw_ctx, run_work.work);

	__blk_mq_run_hw_queue(hctx);
}

static void blk_mq_delay_work_fn(struct work_struct *work)
{
	struct blk_mq_hw_ctx *hctx;

	hctx = container_of(work, struct blk_mq_hw_ctx, delay_work.work);

	if (test_and_clear_bit(BLK_MQ_S_STOPPED, &hctx->state))
		__blk_mq_run_hw_queue(hctx);
}

void blk_mq_delay_queue(struct blk_mq_hw_ctx *hctx, unsigned long msecs)
{
	unsigned long tmo = msecs_to_jiffies(msecs);

	if (hctx->queue->nr_hw_queues == 1)
		kblockd_schedule_delayed_work(&hctx->delay_work, tmo);
	else {
		unsigned int cpu;

		cpu = blk_mq_hctx_next_cpu(hctx);
		kblockd_schedule_delayed_work_on(cpu, &hctx->delay_work, tmo);
	}
}
EXPORT_SYMBOL(blk_mq_delay_queue);

static void __blk_mq_insert_request(struct blk_mq_hw_ctx *hctx,
				    struct request *rq, bool at_head)
{
	struct blk_mq_ctx *ctx = rq->mq_ctx;

	trace_block_rq_insert(hctx->queue, rq);

	if (at_head)
		list_add(&rq->queuelist, &ctx->rq_list);
	else
		list_add_tail(&rq->queuelist, &ctx->rq_list);

	blk_mq_hctx_mark_pending(hctx, ctx);
}

void blk_mq_insert_request(struct request *rq, bool at_head, bool run_queue,
		bool async)
{
	struct request_queue *q = rq->q;
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx = rq->mq_ctx, *current_ctx;

	current_ctx = blk_mq_get_ctx(q);
	if (!cpu_online(ctx->cpu))
		rq->mq_ctx = ctx = current_ctx;

	hctx = q->mq_ops->map_queue(q, ctx->cpu);

	if (rq->cmd_flags & (REQ_FLUSH | REQ_FUA) &&
	    !(rq->cmd_flags & (REQ_FLUSH_SEQ))) {
		blk_insert_flush(rq);
	} else {
		spin_lock(&ctx->lock);
		__blk_mq_insert_request(hctx, rq, at_head);
		spin_unlock(&ctx->lock);
	}

	if (run_queue)
		blk_mq_run_hw_queue(hctx, async);

	blk_mq_put_ctx(current_ctx);
}

static void blk_mq_insert_requests(struct request_queue *q,
				     struct blk_mq_ctx *ctx,
				     struct list_head *list,
				     int depth,
				     bool from_schedule)

{
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *current_ctx;

	trace_block_unplug(q, depth, !from_schedule);

	current_ctx = blk_mq_get_ctx(q);

	if (!cpu_online(ctx->cpu))
		ctx = current_ctx;
	hctx = q->mq_ops->map_queue(q, ctx->cpu);

	/*
	 * preemption doesn't flush plug list, so it's possible ctx->cpu is
	 * offline now
	 */
	spin_lock(&ctx->lock);
	while (!list_empty(list)) {
		struct request *rq;

		rq = list_first_entry(list, struct request, queuelist);
		list_del_init(&rq->queuelist);
		rq->mq_ctx = ctx;
		__blk_mq_insert_request(hctx, rq, false);
	}
	spin_unlock(&ctx->lock);

	blk_mq_run_hw_queue(hctx, from_schedule);
	blk_mq_put_ctx(current_ctx);
}

static int plug_ctx_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct request *rqa = container_of(a, struct request, queuelist);
	struct request *rqb = container_of(b, struct request, queuelist);

	return !(rqa->mq_ctx < rqb->mq_ctx ||
		 (rqa->mq_ctx == rqb->mq_ctx &&
		  blk_rq_pos(rqa) < blk_rq_pos(rqb)));
}

void blk_mq_flush_plug_list(struct blk_plug *plug, bool from_schedule)
{
	struct blk_mq_ctx *this_ctx;
	struct request_queue *this_q;
	struct request *rq;
	LIST_HEAD(list);
	LIST_HEAD(ctx_list);
	unsigned int depth;

	list_splice_init(&plug->mq_list, &list);

	list_sort(NULL, &list, plug_ctx_cmp);

	this_q = NULL;
	this_ctx = NULL;
	depth = 0;

	while (!list_empty(&list)) {
		rq = list_entry_rq(list.next);
		list_del_init(&rq->queuelist);
		BUG_ON(!rq->q);
		if (rq->mq_ctx != this_ctx) {
			if (this_ctx) {
				blk_mq_insert_requests(this_q, this_ctx,
							&ctx_list, depth,
							from_schedule);
			}

			this_ctx = rq->mq_ctx;
			this_q = rq->q;
			depth = 0;
		}

		depth++;
		list_add_tail(&rq->queuelist, &ctx_list);
	}

	/*
	 * If 'this_ctx' is set, we know we have entries to complete
	 * on 'ctx_list'. Do those.
	 */
	if (this_ctx) {
		blk_mq_insert_requests(this_q, this_ctx, &ctx_list, depth,
				       from_schedule);
	}
}

static void blk_mq_bio_to_request(struct request *rq, struct bio *bio)
{
	init_request_from_bio(rq, bio);

	if (blk_do_io_stat(rq))
		blk_account_io_start(rq, 1);
}

static inline bool blk_mq_merge_queue_io(struct blk_mq_hw_ctx *hctx,
					 struct blk_mq_ctx *ctx,
					 struct request *rq, struct bio *bio)
{
	struct request_queue *q = hctx->queue;

	if (!(hctx->flags & BLK_MQ_F_SHOULD_MERGE)) {
		blk_mq_bio_to_request(rq, bio);
		spin_lock(&ctx->lock);
insert_rq:
		__blk_mq_insert_request(hctx, rq, false);
		spin_unlock(&ctx->lock);
		return false;
	} else {
		spin_lock(&ctx->lock);
		if (!blk_mq_attempt_merge(q, ctx, bio)) {
			blk_mq_bio_to_request(rq, bio);
			goto insert_rq;
		}

		spin_unlock(&ctx->lock);
		__blk_mq_free_request(hctx, ctx, rq);
		return true;
	}
}

struct blk_map_ctx {
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx;
};

static struct request *blk_mq_map_request(struct request_queue *q,
					  struct bio *bio,
					  struct blk_map_ctx *data)
{
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx;
	struct request *rq;
	int rw = bio_data_dir(bio);
	struct blk_mq_alloc_data alloc_data;

	if (unlikely(blk_mq_queue_enter(q))) {
		bio_endio(bio, -EIO);
		return NULL;
	}

	ctx = blk_mq_get_ctx(q);
	hctx = q->mq_ops->map_queue(q, ctx->cpu);

	if (rw_is_sync(bio->bi_rw))
		rw |= REQ_SYNC;

	trace_block_getrq(q, bio, rw);
	blk_mq_set_alloc_data(&alloc_data, q, GFP_ATOMIC, false, ctx,
			hctx);
	rq = __blk_mq_alloc_request(&alloc_data, rw);
	if (unlikely(!rq)) {
		__blk_mq_run_hw_queue(hctx);
		blk_mq_put_ctx(ctx);
		trace_block_sleeprq(q, bio, rw);

		ctx = blk_mq_get_ctx(q);
		hctx = q->mq_ops->map_queue(q, ctx->cpu);
		blk_mq_set_alloc_data(&alloc_data, q,
				__GFP_WAIT|GFP_ATOMIC, false, ctx, hctx);
		rq = __blk_mq_alloc_request(&alloc_data, rw);
		ctx = alloc_data.ctx;
		hctx = alloc_data.hctx;
	}

	hctx->queued++;
	data->hctx = hctx;
	data->ctx = ctx;
	return rq;
}

/*
 * Multiple hardware queue variant. This will not use per-process plugs,
 * but will attempt to bypass the hctx queueing if we can go straight to
 * hardware for SYNC IO.
 */
static void blk_mq_make_request(struct request_queue *q, struct bio *bio)
{
	const int is_sync = rw_is_sync(bio->bi_rw);
	const int is_flush_fua = bio->bi_rw & (REQ_FLUSH | REQ_FUA);
	struct blk_map_ctx data;
	struct request *rq;

	blk_queue_bounce(q, &bio);

	if (bio_integrity_enabled(bio) && bio_integrity_prep(bio)) {
		bio_endio(bio, -EIO);
		return;
	}

	rq = blk_mq_map_request(q, bio, &data);
	if (unlikely(!rq))
		return;

	if (unlikely(is_flush_fua)) {
		blk_mq_bio_to_request(rq, bio);
		blk_insert_flush(rq);
		goto run_queue;
	}

	if (is_sync) {
		int ret;

		blk_mq_bio_to_request(rq, bio);
		blk_mq_start_request(rq, true);

		/*
		 * For OK queue, we are done. For error, kill it. Any other
		 * error (busy), just add it to our list as we previously
		 * would have done
		 */
		ret = q->mq_ops->queue_rq(data.hctx, rq);
		if (ret == BLK_MQ_RQ_QUEUE_OK)
			goto done;
		else {
			__blk_mq_requeue_request(rq);

			if (ret == BLK_MQ_RQ_QUEUE_ERROR) {
				rq->errors = -EIO;
				blk_mq_end_io(rq, rq->errors);
				goto done;
			}
		}
	}

	if (!blk_mq_merge_queue_io(data.hctx, data.ctx, rq, bio)) {
		/*
		 * For a SYNC request, send it to the hardware immediately. For
		 * an ASYNC request, just ensure that we run it later on. The
		 * latter allows for merging opportunities and more efficient
		 * dispatching.
		 */
run_queue:
		blk_mq_run_hw_queue(data.hctx, !is_sync || is_flush_fua);
	}
done:
	blk_mq_put_ctx(data.ctx);
}

/*
 * Single hardware queue variant. This will attempt to use any per-process
 * plug for merging and IO deferral.
 */
static void blk_sq_make_request(struct request_queue *q, struct bio *bio)
{
	const int is_sync = rw_is_sync(bio->bi_rw);
	const int is_flush_fua = bio->bi_rw & (REQ_FLUSH | REQ_FUA);
	unsigned int use_plug, request_count = 0;
	struct blk_map_ctx data;
	struct request *rq;

	/*
	 * If we have multiple hardware queues, just go directly to
	 * one of those for sync IO.
	 */
	use_plug = !is_flush_fua && !is_sync;

	blk_queue_bounce(q, &bio);

	if (bio_integrity_enabled(bio) && bio_integrity_prep(bio)) {
		bio_endio(bio, -EIO);
		return;
	}

	if (use_plug && !blk_queue_nomerges(q) &&
	    blk_attempt_plug_merge(q, bio, &request_count))
		return;

	rq = blk_mq_map_request(q, bio, &data);
	if (unlikely(!rq))
		return;

	if (unlikely(is_flush_fua)) {
		blk_mq_bio_to_request(rq, bio);
		blk_insert_flush(rq);
		goto run_queue;
	}

	/*
	 * A task plug currently exists. Since this is completely lockless,
	 * utilize that to temporarily store requests until the task is
	 * either done or scheduled away.
	 */
	if (use_plug) {
		struct blk_plug *plug = current->plug;

		if (plug) {
			blk_mq_bio_to_request(rq, bio);
			if (list_empty(&plug->mq_list))
				trace_block_plug(q);
			else if (request_count >= BLK_MAX_REQUEST_COUNT) {
				blk_flush_plug_list(plug, false);
				trace_block_plug(q);
			}
			list_add_tail(&rq->queuelist, &plug->mq_list);
			blk_mq_put_ctx(data.ctx);
			return;
		}
	}

	if (!blk_mq_merge_queue_io(data.hctx, data.ctx, rq, bio)) {
		/*
		 * For a SYNC request, send it to the hardware immediately. For
		 * an ASYNC request, just ensure that we run it later on. The
		 * latter allows for merging opportunities and more efficient
		 * dispatching.
		 */
run_queue:
		blk_mq_run_hw_queue(data.hctx, !is_sync || is_flush_fua);
	}

	blk_mq_put_ctx(data.ctx);
}

/*
 * Default mapping to a software queue, since we use one per CPU.
 */
struct blk_mq_hw_ctx *blk_mq_map_queue(struct request_queue *q, const int cpu)
{
	return q->queue_hw_ctx[q->mq_map[cpu]];
}
EXPORT_SYMBOL(blk_mq_map_queue);

static void blk_mq_free_rq_map(struct blk_mq_tag_set *set,
		struct blk_mq_tags *tags, unsigned int hctx_idx)
{
	struct page *page;

	if (tags->rqs && set->ops->exit_request) {
		int i;

		for (i = 0; i < tags->nr_tags; i++) {
			if (!tags->rqs[i])
				continue;
			set->ops->exit_request(set->driver_data, tags->rqs[i],
						hctx_idx, i);
		}
	}

	while (!list_empty(&tags->page_list)) {
		page = list_first_entry(&tags->page_list, struct page, lru);
		list_del_init(&page->lru);
		__free_pages(page, page->private);
	}

	kfree(tags->rqs);

	blk_mq_free_tags(tags);
}

static size_t order_to_size(unsigned int order)
{
	return (size_t)PAGE_SIZE << order;
}

static struct blk_mq_tags *blk_mq_init_rq_map(struct blk_mq_tag_set *set,
		unsigned int hctx_idx)
{
	struct blk_mq_tags *tags;
	unsigned int i, j, entries_per_page, max_order = 4;
	size_t rq_size, left;

	tags = blk_mq_init_tags(set->queue_depth, set->reserved_tags,
				set->numa_node);
	if (!tags)
		return NULL;

	INIT_LIST_HEAD(&tags->page_list);

	tags->rqs = kmalloc_node(set->queue_depth * sizeof(struct request *),
					GFP_KERNEL, set->numa_node);
	if (!tags->rqs) {
		blk_mq_free_tags(tags);
		return NULL;
	}

	/*
	 * rq_size is the size of the request plus driver payload, rounded
	 * to the cacheline size
	 */
	rq_size = round_up(sizeof(struct request) + set->cmd_size,
				cache_line_size());
	left = rq_size * set->queue_depth;

	for (i = 0; i < set->queue_depth; ) {
		int this_order = max_order;
		struct page *page;
		int to_do;
		void *p;

		while (left < order_to_size(this_order - 1) && this_order)
			this_order--;

		do {
			page = alloc_pages_node(set->numa_node, GFP_KERNEL,
						this_order);
			if (page)
				break;
			if (!this_order--)
				break;
			if (order_to_size(this_order) < rq_size)
				break;
		} while (1);

		if (!page)
			goto fail;

		page->private = this_order;
		list_add_tail(&page->lru, &tags->page_list);

		p = page_address(page);
		entries_per_page = order_to_size(this_order) / rq_size;
		to_do = min(entries_per_page, set->queue_depth - i);
		left -= to_do * rq_size;
		for (j = 0; j < to_do; j++) {
			tags->rqs[i] = p;
			if (set->ops->init_request) {
				if (set->ops->init_request(set->driver_data,
						tags->rqs[i], hctx_idx, i,
						set->numa_node))
					goto fail;
			}

			p += rq_size;
			i++;
		}
	}

	return tags;

fail:
	pr_warn("%s: failed to allocate requests\n", __func__);
	blk_mq_free_rq_map(set, tags, hctx_idx);
	return NULL;
}

static void blk_mq_free_bitmap(struct blk_mq_ctxmap *bitmap)
{
	kfree(bitmap->map);
}

static int blk_mq_alloc_bitmap(struct blk_mq_ctxmap *bitmap, int node)
{
	unsigned int bpw = 8, total, num_maps, i;

	bitmap->bits_per_word = bpw;

	num_maps = ALIGN(nr_cpu_ids, bpw) / bpw;
	bitmap->map = kzalloc_node(num_maps * sizeof(struct blk_align_bitmap),
					GFP_KERNEL, node);
	if (!bitmap->map)
		return -ENOMEM;

	bitmap->map_size = num_maps;

	total = nr_cpu_ids;
	for (i = 0; i < num_maps; i++) {
		bitmap->map[i].depth = min(total, bitmap->bits_per_word);
		total -= bitmap->map[i].depth;
	}

	return 0;
}

static int blk_mq_hctx_cpu_offline(struct blk_mq_hw_ctx *hctx, int cpu)
{
	struct request_queue *q = hctx->queue;
	struct blk_mq_ctx *ctx;
	LIST_HEAD(tmp);

	/*
	 * Move ctx entries to new CPU, if this one is going away.
	 */
	ctx = __blk_mq_get_ctx(q, cpu);

	spin_lock(&ctx->lock);
	if (!list_empty(&ctx->rq_list)) {
		list_splice_init(&ctx->rq_list, &tmp);
		blk_mq_hctx_clear_pending(hctx, ctx);
	}
	spin_unlock(&ctx->lock);

	if (list_empty(&tmp))
		return NOTIFY_OK;

	ctx = blk_mq_get_ctx(q);
	spin_lock(&ctx->lock);

	while (!list_empty(&tmp)) {
		struct request *rq;

		rq = list_first_entry(&tmp, struct request, queuelist);
		rq->mq_ctx = ctx;
		list_move_tail(&rq->queuelist, &ctx->rq_list);
	}

	hctx = q->mq_ops->map_queue(q, ctx->cpu);
	blk_mq_hctx_mark_pending(hctx, ctx);

	spin_unlock(&ctx->lock);

	blk_mq_run_hw_queue(hctx, true);
	blk_mq_put_ctx(ctx);
	return NOTIFY_OK;
}

static int blk_mq_hctx_cpu_online(struct blk_mq_hw_ctx *hctx, int cpu)
{
	struct request_queue *q = hctx->queue;
	struct blk_mq_tag_set *set = q->tag_set;

	if (set->tags[hctx->queue_num])
		return NOTIFY_OK;

	set->tags[hctx->queue_num] = blk_mq_init_rq_map(set, hctx->queue_num);
	if (!set->tags[hctx->queue_num])
		return NOTIFY_STOP;

	hctx->tags = set->tags[hctx->queue_num];
	return NOTIFY_OK;
}

static int blk_mq_hctx_notify(void *data, unsigned long action,
			      unsigned int cpu)
{
	struct blk_mq_hw_ctx *hctx = data;

	if (action == CPU_DEAD || action == CPU_DEAD_FROZEN)
		return blk_mq_hctx_cpu_offline(hctx, cpu);
	else if (action == CPU_ONLINE || action == CPU_ONLINE_FROZEN)
		return blk_mq_hctx_cpu_online(hctx, cpu);

	return NOTIFY_OK;
}

static void blk_mq_exit_hw_queues(struct request_queue *q,
		struct blk_mq_tag_set *set, int nr_queue)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (i == nr_queue)
			break;

		blk_mq_tag_idle(hctx);

		if (set->ops->exit_hctx)
			set->ops->exit_hctx(hctx, i);

		blk_mq_unregister_cpu_notifier(&hctx->cpu_notifier);
		kfree(hctx->ctxs);
		blk_mq_free_bitmap(&hctx->ctx_map);
	}

}

static void blk_mq_free_hw_queues(struct request_queue *q,
		struct blk_mq_tag_set *set)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		free_cpumask_var(hctx->cpumask);
		kfree(hctx);
	}
}

static int blk_mq_init_hw_queues(struct request_queue *q,
		struct blk_mq_tag_set *set)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned int i;

	/*
	 * Initialize hardware queues
	 */
	queue_for_each_hw_ctx(q, hctx, i) {
		int node;

		node = hctx->numa_node;
		if (node == NUMA_NO_NODE)
			node = hctx->numa_node = set->numa_node;

		INIT_DELAYED_WORK(&hctx->run_work, blk_mq_run_work_fn);
		INIT_DELAYED_WORK(&hctx->delay_work, blk_mq_delay_work_fn);
		spin_lock_init(&hctx->lock);
		INIT_LIST_HEAD(&hctx->dispatch);
		hctx->queue = q;
		hctx->queue_num = i;
		hctx->flags = set->flags;
		hctx->cmd_size = set->cmd_size;

		blk_mq_init_cpu_notifier(&hctx->cpu_notifier,
						blk_mq_hctx_notify, hctx);
		blk_mq_register_cpu_notifier(&hctx->cpu_notifier);

		hctx->tags = set->tags[i];

		/*
		 * Allocate space for all possible cpus to avoid allocation in
		 * runtime
		 */
		hctx->ctxs = kmalloc_node(nr_cpu_ids * sizeof(void *),
						GFP_KERNEL, node);
		if (!hctx->ctxs)
			break;

		if (blk_mq_alloc_bitmap(&hctx->ctx_map, node))
			break;

		hctx->nr_ctx = 0;

		if (set->ops->init_hctx &&
		    set->ops->init_hctx(hctx, set->driver_data, i))
			break;
	}

	if (i == q->nr_hw_queues)
		return 0;

	/*
	 * Init failed
	 */
	blk_mq_exit_hw_queues(q, set, i);

	return 1;
}

static void blk_mq_init_cpu_queues(struct request_queue *q,
				   unsigned int nr_hw_queues)
{
	unsigned int i;

	for_each_possible_cpu(i) {
		struct blk_mq_ctx *__ctx = per_cpu_ptr(q->queue_ctx, i);
		struct blk_mq_hw_ctx *hctx;

		memset(__ctx, 0, sizeof(*__ctx));
		__ctx->cpu = i;
		spin_lock_init(&__ctx->lock);
		INIT_LIST_HEAD(&__ctx->rq_list);
		__ctx->queue = q;

		/* If the cpu isn't online, the cpu is mapped to first hctx */
		if (!cpu_online(i))
			continue;

		hctx = q->mq_ops->map_queue(q, i);
		cpumask_set_cpu(i, hctx->cpumask);
		hctx->nr_ctx++;

		/*
		 * Set local node, IFF we have more than one hw queue. If
		 * not, we remain on the home node of the device
		 */
		if (nr_hw_queues > 1 && hctx->numa_node == NUMA_NO_NODE)
			hctx->numa_node = cpu_to_node(i);
	}
}

static void blk_mq_map_swqueue(struct request_queue *q)
{
	unsigned int i;
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx;

	queue_for_each_hw_ctx(q, hctx, i) {
		cpumask_clear(hctx->cpumask);
		hctx->nr_ctx = 0;
	}

	/*
	 * Map software to hardware queues
	 */
	queue_for_each_ctx(q, ctx, i) {
		/* If the cpu isn't online, the cpu is mapped to first hctx */
		if (!cpu_online(i))
			continue;

		hctx = q->mq_ops->map_queue(q, i);
		cpumask_set_cpu(i, hctx->cpumask);
		ctx->index_hw = hctx->nr_ctx;
		hctx->ctxs[hctx->nr_ctx++] = ctx;
	}

	queue_for_each_hw_ctx(q, hctx, i) {
		/*
		 * If not software queues are mapped to this hardware queue,
		 * disable it and free the request entries
		 */
		if (!hctx->nr_ctx) {
			struct blk_mq_tag_set *set = q->tag_set;

			if (set->tags[i]) {
				blk_mq_free_rq_map(set, set->tags[i], i);
				set->tags[i] = NULL;
				hctx->tags = NULL;
			}
			continue;
		}

		/*
		 * Initialize batch roundrobin counts
		 */
		hctx->next_cpu = cpumask_first(hctx->cpumask);
		hctx->next_cpu_batch = BLK_MQ_CPU_WORK_BATCH;
	}
}

static void blk_mq_update_tag_set_depth(struct blk_mq_tag_set *set)
{
	struct blk_mq_hw_ctx *hctx;
	struct request_queue *q;
	bool shared;
	int i;

	if (set->tag_list.next == set->tag_list.prev)
		shared = false;
	else
		shared = true;

	list_for_each_entry(q, &set->tag_list, tag_set_list) {
		blk_mq_freeze_queue(q);

		queue_for_each_hw_ctx(q, hctx, i) {
			if (shared)
				hctx->flags |= BLK_MQ_F_TAG_SHARED;
			else
				hctx->flags &= ~BLK_MQ_F_TAG_SHARED;
		}
		blk_mq_unfreeze_queue(q);
	}
}

static void blk_mq_del_queue_tag_set(struct request_queue *q)
{
	struct blk_mq_tag_set *set = q->tag_set;

	blk_mq_freeze_queue(q);

	mutex_lock(&set->tag_list_lock);
	list_del_init(&q->tag_set_list);
	blk_mq_update_tag_set_depth(set);
	mutex_unlock(&set->tag_list_lock);

	blk_mq_unfreeze_queue(q);
}

static void blk_mq_add_queue_tag_set(struct blk_mq_tag_set *set,
				     struct request_queue *q)
{
	q->tag_set = set;

	mutex_lock(&set->tag_list_lock);
	list_add_tail(&q->tag_set_list, &set->tag_list);
	blk_mq_update_tag_set_depth(set);
	mutex_unlock(&set->tag_list_lock);
}

struct request_queue *blk_mq_init_queue(struct blk_mq_tag_set *set)
{
	struct blk_mq_hw_ctx **hctxs;
	struct blk_mq_ctx __percpu *ctx;
	struct request_queue *q;
	unsigned int *map;
	int i;

	ctx = alloc_percpu(struct blk_mq_ctx);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	hctxs = kmalloc_node(set->nr_hw_queues * sizeof(*hctxs), GFP_KERNEL,
			set->numa_node);

	if (!hctxs)
		goto err_percpu;

	map = blk_mq_make_queue_map(set);
	if (!map)
		goto err_map;

	for (i = 0; i < set->nr_hw_queues; i++) {
		int node = blk_mq_hw_queue_to_node(map, i);

		hctxs[i] = kzalloc_node(sizeof(struct blk_mq_hw_ctx),
					GFP_KERNEL, node);
		if (!hctxs[i])
			goto err_hctxs;

		if (!zalloc_cpumask_var(&hctxs[i]->cpumask, GFP_KERNEL))
			goto err_hctxs;

		atomic_set(&hctxs[i]->nr_active, 0);
		hctxs[i]->numa_node = node;
		hctxs[i]->queue_num = i;
	}

	q = blk_alloc_queue_node(GFP_KERNEL, set->numa_node);
	if (!q)
		goto err_hctxs;

	if (percpu_counter_init(&q->mq_usage_counter, 0))
		goto err_map;

	setup_timer(&q->timeout, blk_mq_rq_timer, (unsigned long) q);
	blk_queue_rq_timeout(q, 30000);

	q->nr_queues = nr_cpu_ids;
	q->nr_hw_queues = set->nr_hw_queues;
	q->mq_map = map;

	q->queue_ctx = ctx;
	q->queue_hw_ctx = hctxs;

	q->mq_ops = set->ops;
	q->queue_flags |= QUEUE_FLAG_MQ_DEFAULT;

	if (!(set->flags & BLK_MQ_F_SG_MERGE))
		q->queue_flags |= 1 << QUEUE_FLAG_NO_SG_MERGE;

	q->sg_reserved_size = INT_MAX;

	INIT_WORK(&q->requeue_work, blk_mq_requeue_work);
	INIT_LIST_HEAD(&q->requeue_list);
	spin_lock_init(&q->requeue_lock);

	if (q->nr_hw_queues > 1)
		blk_queue_make_request(q, blk_mq_make_request);
	else
		blk_queue_make_request(q, blk_sq_make_request);

	blk_queue_rq_timed_out(q, blk_mq_rq_timed_out);
	if (set->timeout)
		blk_queue_rq_timeout(q, set->timeout);

	/*
	 * Do this after blk_queue_make_request() overrides it...
	 */
	q->nr_requests = set->queue_depth;

	if (set->ops->complete)
		blk_queue_softirq_done(q, set->ops->complete);

	blk_mq_init_flush(q);
	blk_mq_init_cpu_queues(q, set->nr_hw_queues);

	q->flush_rq = kzalloc(round_up(sizeof(struct request) +
				set->cmd_size, cache_line_size()),
				GFP_KERNEL);
	if (!q->flush_rq)
		goto err_hw;

	if (blk_mq_init_hw_queues(q, set))
		goto err_flush_rq;

	mutex_lock(&all_q_mutex);
	list_add_tail(&q->all_q_node, &all_q_list);
	mutex_unlock(&all_q_mutex);

	blk_mq_add_queue_tag_set(set, q);

	blk_mq_map_swqueue(q);

	return q;

err_flush_rq:
	kfree(q->flush_rq);
err_hw:
	blk_cleanup_queue(q);
err_hctxs:
	kfree(map);
	for (i = 0; i < set->nr_hw_queues; i++) {
		if (!hctxs[i])
			break;
		free_cpumask_var(hctxs[i]->cpumask);
		kfree(hctxs[i]);
	}
err_map:
	kfree(hctxs);
err_percpu:
	free_percpu(ctx);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(blk_mq_init_queue);

void blk_mq_free_queue(struct request_queue *q)
{
	struct blk_mq_tag_set	*set = q->tag_set;

	blk_mq_del_queue_tag_set(q);

	blk_mq_exit_hw_queues(q, set, set->nr_hw_queues);
	blk_mq_free_hw_queues(q, set);

	percpu_counter_destroy(&q->mq_usage_counter);

	free_percpu(q->queue_ctx);
	kfree(q->queue_hw_ctx);
	kfree(q->mq_map);

	q->queue_ctx = NULL;
	q->queue_hw_ctx = NULL;
	q->mq_map = NULL;

	mutex_lock(&all_q_mutex);
	list_del_init(&q->all_q_node);
	mutex_unlock(&all_q_mutex);
}

/* Basically redo blk_mq_init_queue with queue frozen */
static void blk_mq_queue_reinit(struct request_queue *q)
{
	blk_mq_freeze_queue(q);

	blk_mq_sysfs_unregister(q);

	blk_mq_update_queue_map(q->mq_map, q->nr_hw_queues);

	/*
	 * redo blk_mq_init_cpu_queues and blk_mq_init_hw_queues. FIXME: maybe
	 * we should change hctx numa_node according to new topology (this
	 * involves free and re-allocate memory, worthy doing?)
	 */

	blk_mq_map_swqueue(q);

	blk_mq_sysfs_register(q);

	blk_mq_unfreeze_queue(q);
}

static int blk_mq_queue_reinit_notify(struct notifier_block *nb,
				      unsigned long action, void *hcpu)
{
	struct request_queue *q;

	/*
	 * Before new mappings are established, hotadded cpu might already
	 * start handling requests. This doesn't break anything as we map
	 * offline CPUs to first hardware queue. We will re-init the queue
	 * below to get optimal settings.
	 */
	if (action != CPU_DEAD && action != CPU_DEAD_FROZEN &&
	    action != CPU_ONLINE && action != CPU_ONLINE_FROZEN)
		return NOTIFY_OK;

	mutex_lock(&all_q_mutex);
	list_for_each_entry(q, &all_q_list, all_q_node)
		blk_mq_queue_reinit(q);
	mutex_unlock(&all_q_mutex);
	return NOTIFY_OK;
}

/*
 * Alloc a tag set to be associated with one or more request queues.
 * May fail with EINVAL for various error conditions. May adjust the
 * requested depth down, if if it too large. In that case, the set
 * value will be stored in set->queue_depth.
 */
int blk_mq_alloc_tag_set(struct blk_mq_tag_set *set)
{
	int i;

	if (!set->nr_hw_queues)
		return -EINVAL;
	if (!set->queue_depth)
		return -EINVAL;
	if (set->queue_depth < set->reserved_tags + BLK_MQ_TAG_MIN)
		return -EINVAL;

	if (!set->nr_hw_queues || !set->ops->queue_rq || !set->ops->map_queue)
		return -EINVAL;

	if (set->queue_depth > BLK_MQ_MAX_DEPTH) {
		pr_info("blk-mq: reduced tag depth to %u\n",
			BLK_MQ_MAX_DEPTH);
		set->queue_depth = BLK_MQ_MAX_DEPTH;
	}

	set->tags = kmalloc_node(set->nr_hw_queues *
				 sizeof(struct blk_mq_tags *),
				 GFP_KERNEL, set->numa_node);
	if (!set->tags)
		goto out;

	for (i = 0; i < set->nr_hw_queues; i++) {
		set->tags[i] = blk_mq_init_rq_map(set, i);
		if (!set->tags[i])
			goto out_unwind;
	}

	mutex_init(&set->tag_list_lock);
	INIT_LIST_HEAD(&set->tag_list);

	return 0;

out_unwind:
	while (--i >= 0)
		blk_mq_free_rq_map(set, set->tags[i], i);
out:
	return -ENOMEM;
}
EXPORT_SYMBOL(blk_mq_alloc_tag_set);

void blk_mq_free_tag_set(struct blk_mq_tag_set *set)
{
	int i;

	for (i = 0; i < set->nr_hw_queues; i++) {
		if (set->tags[i])
			blk_mq_free_rq_map(set, set->tags[i], i);
	}

	kfree(set->tags);
}
EXPORT_SYMBOL(blk_mq_free_tag_set);

int blk_mq_update_nr_requests(struct request_queue *q, unsigned int nr)
{
	struct blk_mq_tag_set *set = q->tag_set;
	struct blk_mq_hw_ctx *hctx;
	int i, ret;

	if (!set || nr > set->queue_depth)
		return -EINVAL;

	ret = 0;
	queue_for_each_hw_ctx(q, hctx, i) {
		ret = blk_mq_tag_update_depth(hctx->tags, nr);
		if (ret)
			break;
	}

	if (!ret)
		q->nr_requests = nr;

	return ret;
}

void blk_mq_disable_hotplug(void)
{
	mutex_lock(&all_q_mutex);
}

void blk_mq_enable_hotplug(void)
{
	mutex_unlock(&all_q_mutex);
}

static int __init blk_mq_init(void)
{
	blk_mq_cpu_init();

	/* Must be called after percpu_counter_hotcpu_callback() */
	hotcpu_notifier(blk_mq_queue_reinit_notify, -10);

	return 0;
}
subsys_initcall(blk_mq_init);
