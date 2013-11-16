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

DEFINE_PER_CPU(struct llist_head, ipi_lists);

static struct blk_mq_ctx *__blk_mq_get_ctx(struct request_queue *q,
					   unsigned int cpu)
{
	return per_cpu_ptr(q->queue_ctx, cpu);
}

/*
 * This assumes per-cpu software queueing queues. They could be per-node
 * as well, for instance. For now this is hardcoded as-is. Note that we don't
 * care about preemption, since we know the ctx's are persistent. This does
 * mean that we can't rely on ctx always matching the currently running CPU.
 */
static struct blk_mq_ctx *blk_mq_get_ctx(struct request_queue *q)
{
	return __blk_mq_get_ctx(q, get_cpu());
}

static void blk_mq_put_ctx(struct blk_mq_ctx *ctx)
{
	put_cpu();
}

/*
 * Check if any of the ctx's have pending work in this hardware queue
 */
static bool blk_mq_hctx_has_pending(struct blk_mq_hw_ctx *hctx)
{
	unsigned int i;

	for (i = 0; i < hctx->nr_ctx_map; i++)
		if (hctx->ctx_map[i])
			return true;

	return false;
}

/*
 * Mark this ctx as having pending work in this hardware queue
 */
static void blk_mq_hctx_mark_pending(struct blk_mq_hw_ctx *hctx,
				     struct blk_mq_ctx *ctx)
{
	if (!test_bit(ctx->index_hw, hctx->ctx_map))
		set_bit(ctx->index_hw, hctx->ctx_map);
}

static struct request *blk_mq_alloc_rq(struct blk_mq_hw_ctx *hctx, gfp_t gfp,
				       bool reserved)
{
	struct request *rq;
	unsigned int tag;

	tag = blk_mq_get_tag(hctx->tags, gfp, reserved);
	if (tag != BLK_MQ_TAG_FAIL) {
		rq = hctx->rqs[tag];
		rq->tag = tag;

		return rq;
	}

	return NULL;
}

static int blk_mq_queue_enter(struct request_queue *q)
{
	int ret;

	__percpu_counter_add(&q->mq_usage_counter, 1, 1000000);
	smp_wmb();
	/* we have problems to freeze the queue if it's initializing */
	if (!blk_queue_bypass(q) || !blk_queue_init_done(q))
		return 0;

	__percpu_counter_add(&q->mq_usage_counter, -1, 1000000);

	spin_lock_irq(q->queue_lock);
	ret = wait_event_interruptible_lock_irq(q->mq_freeze_wq,
		!blk_queue_bypass(q), *q->queue_lock);
	/* inc usage with lock hold to avoid freeze_queue runs here */
	if (!ret)
		__percpu_counter_add(&q->mq_usage_counter, 1, 1000000);
	spin_unlock_irq(q->queue_lock);

	return ret;
}

static void blk_mq_queue_exit(struct request_queue *q)
{
	__percpu_counter_add(&q->mq_usage_counter, -1, 1000000);
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

	if (!drain)
		return;

	while (true) {
		s64 count;

		spin_lock_irq(q->queue_lock);
		count = percpu_counter_sum(&q->mq_usage_counter);
		spin_unlock_irq(q->queue_lock);

		if (count == 0)
			break;
		blk_mq_run_queues(q, false);
		msleep(10);
	}
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

static void blk_mq_rq_ctx_init(struct blk_mq_ctx *ctx, struct request *rq,
			       unsigned int rw_flags)
{
	rq->mq_ctx = ctx;
	rq->cmd_flags = rw_flags;
	ctx->rq_dispatched[rw_is_sync(rw_flags)]++;
}

static struct request *__blk_mq_alloc_request(struct blk_mq_hw_ctx *hctx,
					      gfp_t gfp, bool reserved)
{
	return blk_mq_alloc_rq(hctx, gfp, reserved);
}

static struct request *blk_mq_alloc_request_pinned(struct request_queue *q,
						   int rw, gfp_t gfp,
						   bool reserved)
{
	struct request *rq;

	do {
		struct blk_mq_ctx *ctx = blk_mq_get_ctx(q);
		struct blk_mq_hw_ctx *hctx = q->mq_ops->map_queue(q, ctx->cpu);

		rq = __blk_mq_alloc_request(hctx, gfp & ~__GFP_WAIT, reserved);
		if (rq) {
			blk_mq_rq_ctx_init(ctx, rq, rw);
			break;
		} else if (!(gfp & __GFP_WAIT))
			break;

		blk_mq_put_ctx(ctx);
		__blk_mq_run_hw_queue(hctx);
		blk_mq_wait_for_tags(hctx->tags);
	} while (1);

	return rq;
}

struct request *blk_mq_alloc_request(struct request_queue *q, int rw,
		gfp_t gfp, bool reserved)
{
	struct request *rq;

	if (blk_mq_queue_enter(q))
		return NULL;

	rq = blk_mq_alloc_request_pinned(q, rw, gfp, reserved);
	blk_mq_put_ctx(rq->mq_ctx);
	return rq;
}

struct request *blk_mq_alloc_reserved_request(struct request_queue *q, int rw,
					      gfp_t gfp)
{
	struct request *rq;

	if (blk_mq_queue_enter(q))
		return NULL;

	rq = blk_mq_alloc_request_pinned(q, rw, gfp, true);
	blk_mq_put_ctx(rq->mq_ctx);
	return rq;
}
EXPORT_SYMBOL(blk_mq_alloc_reserved_request);

/*
 * Re-init and set pdu, if we have it
 */
static void blk_mq_rq_init(struct blk_mq_hw_ctx *hctx, struct request *rq)
{
	blk_rq_init(hctx->queue, rq);

	if (hctx->cmd_size)
		rq->special = blk_mq_rq_to_pdu(rq);
}

static void __blk_mq_free_request(struct blk_mq_hw_ctx *hctx,
				  struct blk_mq_ctx *ctx, struct request *rq)
{
	const int tag = rq->tag;
	struct request_queue *q = rq->q;

	blk_mq_rq_init(hctx, rq);
	blk_mq_put_tag(hctx->tags, tag);

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

static void blk_mq_bio_endio(struct request *rq, struct bio *bio, int error)
{
	if (error)
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
	else if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
		error = -EIO;

	if (unlikely(rq->cmd_flags & REQ_QUIET))
		set_bit(BIO_QUIET, &bio->bi_flags);

	/* don't actually finish bio if it's part of flush sequence */
	if (!(rq->cmd_flags & REQ_FLUSH_SEQ))
		bio_endio(bio, error);
}

void blk_mq_complete_request(struct request *rq, int error)
{
	struct bio *bio = rq->bio;
	unsigned int bytes = 0;

	trace_block_rq_complete(rq->q, rq);

	while (bio) {
		struct bio *next = bio->bi_next;

		bio->bi_next = NULL;
		bytes += bio->bi_size;
		blk_mq_bio_endio(rq, bio, error);
		bio = next;
	}

	blk_account_io_completion(rq, bytes);

	if (rq->end_io)
		rq->end_io(rq, error);
	else
		blk_mq_free_request(rq);

	blk_account_io_done(rq);
}

void __blk_mq_end_io(struct request *rq, int error)
{
	if (!blk_mark_rq_complete(rq))
		blk_mq_complete_request(rq, error);
}

#if defined(CONFIG_SMP)

/*
 * Called with interrupts disabled.
 */
static void ipi_end_io(void *data)
{
	struct llist_head *list = &per_cpu(ipi_lists, smp_processor_id());
	struct llist_node *entry, *next;
	struct request *rq;

	entry = llist_del_all(list);

	while (entry) {
		next = entry->next;
		rq = llist_entry(entry, struct request, ll_list);
		__blk_mq_end_io(rq, rq->errors);
		entry = next;
	}
}

static int ipi_remote_cpu(struct blk_mq_ctx *ctx, const int cpu,
			  struct request *rq, const int error)
{
	struct call_single_data *data = &rq->csd;

	rq->errors = error;
	rq->ll_list.next = NULL;

	/*
	 * If the list is non-empty, an existing IPI must already
	 * be "in flight". If that is the case, we need not schedule
	 * a new one.
	 */
	if (llist_add(&rq->ll_list, &per_cpu(ipi_lists, ctx->cpu))) {
		data->func = ipi_end_io;
		data->flags = 0;
		__smp_call_function_single(ctx->cpu, data, 0);
	}

	return true;
}
#else /* CONFIG_SMP */
static int ipi_remote_cpu(struct blk_mq_ctx *ctx, const int cpu,
			  struct request *rq, const int error)
{
	return false;
}
#endif

/*
 * End IO on this request on a multiqueue enabled driver. We'll either do
 * it directly inline, or punt to a local IPI handler on the matching
 * remote CPU.
 */
void blk_mq_end_io(struct request *rq, int error)
{
	struct blk_mq_ctx *ctx = rq->mq_ctx;
	int cpu;

	if (!ctx->ipi_redirect)
		return __blk_mq_end_io(rq, error);

	cpu = get_cpu();

	if (cpu == ctx->cpu || !cpu_online(ctx->cpu) ||
	    !ipi_remote_cpu(ctx, cpu, rq, error))
		__blk_mq_end_io(rq, error);

	put_cpu();
}
EXPORT_SYMBOL(blk_mq_end_io);

static void blk_mq_start_request(struct request *rq)
{
	struct request_queue *q = rq->q;

	trace_block_rq_issue(q, rq);

	/*
	 * Just mark start time and set the started bit. Due to memory
	 * ordering, we know we'll see the correct deadline as long as
	 * REQ_ATOMIC_STARTED is seen.
	 */
	rq->deadline = jiffies + q->rq_timeout;
	set_bit(REQ_ATOM_STARTED, &rq->atomic_flags);
}

static void blk_mq_requeue_request(struct request *rq)
{
	struct request_queue *q = rq->q;

	trace_block_rq_requeue(q, rq);
	clear_bit(REQ_ATOM_STARTED, &rq->atomic_flags);
}

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

		tag = find_next_zero_bit(free_tags, hctx->queue_depth, tag);
		if (tag >= hctx->queue_depth)
			break;

		rq = hctx->rqs[tag++];

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

static void blk_mq_rq_timer(unsigned long data)
{
	struct request_queue *q = (struct request_queue *) data;
	struct blk_mq_hw_ctx *hctx;
	unsigned long next = 0;
	int i, next_set = 0;

	queue_for_each_hw_ctx(q, hctx, i)
		blk_mq_hw_ctx_check_timeout(hctx, &next, &next_set);

	if (next_set)
		mod_timer(&q->timeout, round_jiffies_up(next));
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

void blk_mq_add_timer(struct request *rq)
{
	__blk_add_timer(rq, NULL);
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
	struct blk_mq_ctx *ctx;
	struct request *rq;
	LIST_HEAD(rq_list);
	int bit, queued;

	if (unlikely(test_bit(BLK_MQ_S_STOPPED, &hctx->flags)))
		return;

	hctx->run++;

	/*
	 * Touch any software queue that has pending entries.
	 */
	for_each_set_bit(bit, hctx->ctx_map, hctx->nr_ctx) {
		clear_bit(bit, hctx->ctx_map);
		ctx = hctx->ctxs[bit];
		BUG_ON(bit != ctx->index_hw);

		spin_lock(&ctx->lock);
		list_splice_tail_init(&ctx->rq_list, &rq_list);
		spin_unlock(&ctx->lock);
	}

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
	 * Delete and return all entries from our dispatch list
	 */
	queued = 0;

	/*
	 * Now process all the entries, sending them to the driver.
	 */
	while (!list_empty(&rq_list)) {
		int ret;

		rq = list_first_entry(&rq_list, struct request, queuelist);
		list_del_init(&rq->queuelist);
		blk_mq_start_request(rq);

		/*
		 * Last request in the series. Flag it as such, this
		 * enables drivers to know when IO should be kicked off,
		 * if they don't do it on a per-request basis.
		 *
		 * Note: the flag isn't the only condition drivers
		 * should do kick off. If drive is busy, the last
		 * request might not have the bit set.
		 */
		if (list_empty(&rq_list))
			rq->cmd_flags |= REQ_END;

		ret = q->mq_ops->queue_rq(hctx, rq);
		switch (ret) {
		case BLK_MQ_RQ_QUEUE_OK:
			queued++;
			continue;
		case BLK_MQ_RQ_QUEUE_BUSY:
			/*
			 * FIXME: we should have a mechanism to stop the queue
			 * like blk_stop_queue, otherwise we will waste cpu
			 * time
			 */
			list_add(&rq->queuelist, &rq_list);
			blk_mq_requeue_request(rq);
			break;
		default:
			pr_err("blk-mq: bad return on queue: %d\n", ret);
			rq->errors = -EIO;
		case BLK_MQ_RQ_QUEUE_ERROR:
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

void blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx, bool async)
{
	if (unlikely(test_bit(BLK_MQ_S_STOPPED, &hctx->flags)))
		return;

	if (!async)
		__blk_mq_run_hw_queue(hctx);
	else {
		struct request_queue *q = hctx->queue;

		kblockd_schedule_delayed_work(q, &hctx->delayed_work, 0);
	}
}

void blk_mq_run_queues(struct request_queue *q, bool async)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if ((!blk_mq_hctx_has_pending(hctx) &&
		    list_empty_careful(&hctx->dispatch)) ||
		    test_bit(BLK_MQ_S_STOPPED, &hctx->flags))
			continue;

		blk_mq_run_hw_queue(hctx, async);
	}
}
EXPORT_SYMBOL(blk_mq_run_queues);

void blk_mq_stop_hw_queue(struct blk_mq_hw_ctx *hctx)
{
	cancel_delayed_work(&hctx->delayed_work);
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
	__blk_mq_run_hw_queue(hctx);
}
EXPORT_SYMBOL(blk_mq_start_hw_queue);

void blk_mq_start_stopped_hw_queues(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (!test_bit(BLK_MQ_S_STOPPED, &hctx->state))
			continue;

		clear_bit(BLK_MQ_S_STOPPED, &hctx->state);
		blk_mq_run_hw_queue(hctx, true);
	}
}
EXPORT_SYMBOL(blk_mq_start_stopped_hw_queues);

static void blk_mq_work_fn(struct work_struct *work)
{
	struct blk_mq_hw_ctx *hctx;

	hctx = container_of(work, struct blk_mq_hw_ctx, delayed_work.work);
	__blk_mq_run_hw_queue(hctx);
}

static void __blk_mq_insert_request(struct blk_mq_hw_ctx *hctx,
				    struct request *rq)
{
	struct blk_mq_ctx *ctx = rq->mq_ctx;

	list_add_tail(&rq->queuelist, &ctx->rq_list);
	blk_mq_hctx_mark_pending(hctx, ctx);

	/*
	 * We do this early, to ensure we are on the right CPU.
	 */
	blk_mq_add_timer(rq);
}

void blk_mq_insert_request(struct request_queue *q, struct request *rq,
			   bool run_queue)
{
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx, *current_ctx;

	ctx = rq->mq_ctx;
	hctx = q->mq_ops->map_queue(q, ctx->cpu);

	if (rq->cmd_flags & (REQ_FLUSH | REQ_FUA)) {
		blk_insert_flush(rq);
	} else {
		current_ctx = blk_mq_get_ctx(q);

		if (!cpu_online(ctx->cpu)) {
			ctx = current_ctx;
			hctx = q->mq_ops->map_queue(q, ctx->cpu);
			rq->mq_ctx = ctx;
		}
		spin_lock(&ctx->lock);
		__blk_mq_insert_request(hctx, rq);
		spin_unlock(&ctx->lock);

		blk_mq_put_ctx(current_ctx);
	}

	if (run_queue)
		__blk_mq_run_hw_queue(hctx);
}
EXPORT_SYMBOL(blk_mq_insert_request);

/*
 * This is a special version of blk_mq_insert_request to bypass FLUSH request
 * check. Should only be used internally.
 */
void blk_mq_run_request(struct request *rq, bool run_queue, bool async)
{
	struct request_queue *q = rq->q;
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx, *current_ctx;

	current_ctx = blk_mq_get_ctx(q);

	ctx = rq->mq_ctx;
	if (!cpu_online(ctx->cpu)) {
		ctx = current_ctx;
		rq->mq_ctx = ctx;
	}
	hctx = q->mq_ops->map_queue(q, ctx->cpu);

	/* ctx->cpu might be offline */
	spin_lock(&ctx->lock);
	__blk_mq_insert_request(hctx, rq);
	spin_unlock(&ctx->lock);

	blk_mq_put_ctx(current_ctx);

	if (run_queue)
		blk_mq_run_hw_queue(hctx, async);
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
		__blk_mq_insert_request(hctx, rq);
	}
	spin_unlock(&ctx->lock);

	blk_mq_put_ctx(current_ctx);

	blk_mq_run_hw_queue(hctx, from_schedule);
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
	blk_account_io_start(rq, 1);
}

static void blk_mq_make_request(struct request_queue *q, struct bio *bio)
{
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx;
	const int is_sync = rw_is_sync(bio->bi_rw);
	const int is_flush_fua = bio->bi_rw & (REQ_FLUSH | REQ_FUA);
	int rw = bio_data_dir(bio);
	struct request *rq;
	unsigned int use_plug, request_count = 0;

	/*
	 * If we have multiple hardware queues, just go directly to
	 * one of those for sync IO.
	 */
	use_plug = !is_flush_fua && ((q->nr_hw_queues == 1) || !is_sync);

	blk_queue_bounce(q, &bio);

	if (use_plug && blk_attempt_plug_merge(q, bio, &request_count))
		return;

	if (blk_mq_queue_enter(q)) {
		bio_endio(bio, -EIO);
		return;
	}

	ctx = blk_mq_get_ctx(q);
	hctx = q->mq_ops->map_queue(q, ctx->cpu);

	trace_block_getrq(q, bio, rw);
	rq = __blk_mq_alloc_request(hctx, GFP_ATOMIC, false);
	if (likely(rq))
		blk_mq_rq_ctx_init(ctx, rq, rw);
	else {
		blk_mq_put_ctx(ctx);
		trace_block_sleeprq(q, bio, rw);
		rq = blk_mq_alloc_request_pinned(q, rw, __GFP_WAIT|GFP_ATOMIC,
							false);
		ctx = rq->mq_ctx;
		hctx = q->mq_ops->map_queue(q, ctx->cpu);
	}

	hctx->queued++;

	if (unlikely(is_flush_fua)) {
		blk_mq_bio_to_request(rq, bio);
		blk_mq_put_ctx(ctx);
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
			blk_mq_put_ctx(ctx);
			return;
		}
	}

	spin_lock(&ctx->lock);

	if ((hctx->flags & BLK_MQ_F_SHOULD_MERGE) &&
	    blk_mq_attempt_merge(q, ctx, bio))
		__blk_mq_free_request(hctx, ctx, rq);
	else {
		blk_mq_bio_to_request(rq, bio);
		__blk_mq_insert_request(hctx, rq);
	}

	spin_unlock(&ctx->lock);
	blk_mq_put_ctx(ctx);

	/*
	 * For a SYNC request, send it to the hardware immediately. For an
	 * ASYNC request, just ensure that we run it later on. The latter
	 * allows for merging opportunities and more efficient dispatching.
	 */
run_queue:
	blk_mq_run_hw_queue(hctx, !is_sync || is_flush_fua);
}

/*
 * Default mapping to a software queue, since we use one per CPU.
 */
struct blk_mq_hw_ctx *blk_mq_map_queue(struct request_queue *q, const int cpu)
{
	return q->queue_hw_ctx[q->mq_map[cpu]];
}
EXPORT_SYMBOL(blk_mq_map_queue);

struct blk_mq_hw_ctx *blk_mq_alloc_single_hw_queue(struct blk_mq_reg *reg,
						   unsigned int hctx_index)
{
	return kmalloc_node(sizeof(struct blk_mq_hw_ctx),
				GFP_KERNEL | __GFP_ZERO, reg->numa_node);
}
EXPORT_SYMBOL(blk_mq_alloc_single_hw_queue);

void blk_mq_free_single_hw_queue(struct blk_mq_hw_ctx *hctx,
				 unsigned int hctx_index)
{
	kfree(hctx);
}
EXPORT_SYMBOL(blk_mq_free_single_hw_queue);

static void blk_mq_hctx_notify(void *data, unsigned long action,
			       unsigned int cpu)
{
	struct blk_mq_hw_ctx *hctx = data;
	struct blk_mq_ctx *ctx;
	LIST_HEAD(tmp);

	if (action != CPU_DEAD && action != CPU_DEAD_FROZEN)
		return;

	/*
	 * Move ctx entries to new CPU, if this one is going away.
	 */
	ctx = __blk_mq_get_ctx(hctx->queue, cpu);

	spin_lock(&ctx->lock);
	if (!list_empty(&ctx->rq_list)) {
		list_splice_init(&ctx->rq_list, &tmp);
		clear_bit(ctx->index_hw, hctx->ctx_map);
	}
	spin_unlock(&ctx->lock);

	if (list_empty(&tmp))
		return;

	ctx = blk_mq_get_ctx(hctx->queue);
	spin_lock(&ctx->lock);

	while (!list_empty(&tmp)) {
		struct request *rq;

		rq = list_first_entry(&tmp, struct request, queuelist);
		rq->mq_ctx = ctx;
		list_move_tail(&rq->queuelist, &ctx->rq_list);
	}

	blk_mq_hctx_mark_pending(hctx, ctx);

	spin_unlock(&ctx->lock);
	blk_mq_put_ctx(ctx);
}

static void blk_mq_init_hw_commands(struct blk_mq_hw_ctx *hctx,
				    void (*init)(void *, struct blk_mq_hw_ctx *,
					struct request *, unsigned int),
				    void *data)
{
	unsigned int i;

	for (i = 0; i < hctx->queue_depth; i++) {
		struct request *rq = hctx->rqs[i];

		init(data, hctx, rq, i);
	}
}

void blk_mq_init_commands(struct request_queue *q,
			  void (*init)(void *, struct blk_mq_hw_ctx *,
					struct request *, unsigned int),
			  void *data)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned int i;

	queue_for_each_hw_ctx(q, hctx, i)
		blk_mq_init_hw_commands(hctx, init, data);
}
EXPORT_SYMBOL(blk_mq_init_commands);

static void blk_mq_free_rq_map(struct blk_mq_hw_ctx *hctx)
{
	struct page *page;

	while (!list_empty(&hctx->page_list)) {
		page = list_first_entry(&hctx->page_list, struct page, list);
		list_del_init(&page->list);
		__free_pages(page, page->private);
	}

	kfree(hctx->rqs);

	if (hctx->tags)
		blk_mq_free_tags(hctx->tags);
}

static size_t order_to_size(unsigned int order)
{
	size_t ret = PAGE_SIZE;

	while (order--)
		ret *= 2;

	return ret;
}

static int blk_mq_init_rq_map(struct blk_mq_hw_ctx *hctx,
			      unsigned int reserved_tags, int node)
{
	unsigned int i, j, entries_per_page, max_order = 4;
	size_t rq_size, left;

	INIT_LIST_HEAD(&hctx->page_list);

	hctx->rqs = kmalloc_node(hctx->queue_depth * sizeof(struct request *),
					GFP_KERNEL, node);
	if (!hctx->rqs)
		return -ENOMEM;

	/*
	 * rq_size is the size of the request plus driver payload, rounded
	 * to the cacheline size
	 */
	rq_size = round_up(sizeof(struct request) + hctx->cmd_size,
				cache_line_size());
	left = rq_size * hctx->queue_depth;

	for (i = 0; i < hctx->queue_depth;) {
		int this_order = max_order;
		struct page *page;
		int to_do;
		void *p;

		while (left < order_to_size(this_order - 1) && this_order)
			this_order--;

		do {
			page = alloc_pages_node(node, GFP_KERNEL, this_order);
			if (page)
				break;
			if (!this_order--)
				break;
			if (order_to_size(this_order) < rq_size)
				break;
		} while (1);

		if (!page)
			break;

		page->private = this_order;
		list_add_tail(&page->list, &hctx->page_list);

		p = page_address(page);
		entries_per_page = order_to_size(this_order) / rq_size;
		to_do = min(entries_per_page, hctx->queue_depth - i);
		left -= to_do * rq_size;
		for (j = 0; j < to_do; j++) {
			hctx->rqs[i] = p;
			blk_mq_rq_init(hctx, hctx->rqs[i]);
			p += rq_size;
			i++;
		}
	}

	if (i < (reserved_tags + BLK_MQ_TAG_MIN))
		goto err_rq_map;
	else if (i != hctx->queue_depth) {
		hctx->queue_depth = i;
		pr_warn("%s: queue depth set to %u because of low memory\n",
					__func__, i);
	}

	hctx->tags = blk_mq_init_tags(hctx->queue_depth, reserved_tags, node);
	if (!hctx->tags) {
err_rq_map:
		blk_mq_free_rq_map(hctx);
		return -ENOMEM;
	}

	return 0;
}

static int blk_mq_init_hw_queues(struct request_queue *q,
				 struct blk_mq_reg *reg, void *driver_data)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned int i, j;

	/*
	 * Initialize hardware queues
	 */
	queue_for_each_hw_ctx(q, hctx, i) {
		unsigned int num_maps;
		int node;

		node = hctx->numa_node;
		if (node == NUMA_NO_NODE)
			node = hctx->numa_node = reg->numa_node;

		INIT_DELAYED_WORK(&hctx->delayed_work, blk_mq_work_fn);
		spin_lock_init(&hctx->lock);
		INIT_LIST_HEAD(&hctx->dispatch);
		hctx->queue = q;
		hctx->queue_num = i;
		hctx->flags = reg->flags;
		hctx->queue_depth = reg->queue_depth;
		hctx->cmd_size = reg->cmd_size;

		blk_mq_init_cpu_notifier(&hctx->cpu_notifier,
						blk_mq_hctx_notify, hctx);
		blk_mq_register_cpu_notifier(&hctx->cpu_notifier);

		if (blk_mq_init_rq_map(hctx, reg->reserved_tags, node))
			break;

		/*
		 * Allocate space for all possible cpus to avoid allocation in
		 * runtime
		 */
		hctx->ctxs = kmalloc_node(nr_cpu_ids * sizeof(void *),
						GFP_KERNEL, node);
		if (!hctx->ctxs)
			break;

		num_maps = ALIGN(nr_cpu_ids, BITS_PER_LONG) / BITS_PER_LONG;
		hctx->ctx_map = kzalloc_node(num_maps * sizeof(unsigned long),
						GFP_KERNEL, node);
		if (!hctx->ctx_map)
			break;

		hctx->nr_ctx_map = num_maps;
		hctx->nr_ctx = 0;

		if (reg->ops->init_hctx &&
		    reg->ops->init_hctx(hctx, driver_data, i))
			break;
	}

	if (i == q->nr_hw_queues)
		return 0;

	/*
	 * Init failed
	 */
	queue_for_each_hw_ctx(q, hctx, j) {
		if (i == j)
			break;

		if (reg->ops->exit_hctx)
			reg->ops->exit_hctx(hctx, j);

		blk_mq_unregister_cpu_notifier(&hctx->cpu_notifier);
		blk_mq_free_rq_map(hctx);
		kfree(hctx->ctxs);
	}

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
		hctx = q->mq_ops->map_queue(q, i);
		hctx->nr_ctx++;

		if (!cpu_online(i))
			continue;

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
		hctx->nr_ctx = 0;
	}

	/*
	 * Map software to hardware queues
	 */
	queue_for_each_ctx(q, ctx, i) {
		/* If the cpu isn't online, the cpu is mapped to first hctx */
		hctx = q->mq_ops->map_queue(q, i);
		ctx->index_hw = hctx->nr_ctx;
		hctx->ctxs[hctx->nr_ctx++] = ctx;
	}
}

struct request_queue *blk_mq_init_queue(struct blk_mq_reg *reg,
					void *driver_data)
{
	struct blk_mq_hw_ctx **hctxs;
	struct blk_mq_ctx *ctx;
	struct request_queue *q;
	int i;

	if (!reg->nr_hw_queues ||
	    !reg->ops->queue_rq || !reg->ops->map_queue ||
	    !reg->ops->alloc_hctx || !reg->ops->free_hctx)
		return ERR_PTR(-EINVAL);

	if (!reg->queue_depth)
		reg->queue_depth = BLK_MQ_MAX_DEPTH;
	else if (reg->queue_depth > BLK_MQ_MAX_DEPTH) {
		pr_err("blk-mq: queuedepth too large (%u)\n", reg->queue_depth);
		reg->queue_depth = BLK_MQ_MAX_DEPTH;
	}

	/*
	 * Set aside a tag for flush requests.  It will only be used while
	 * another flush request is in progress but outside the driver.
	 *
	 * TODO: only allocate if flushes are supported
	 */
	reg->queue_depth++;
	reg->reserved_tags++;

	if (reg->queue_depth < (reg->reserved_tags + BLK_MQ_TAG_MIN))
		return ERR_PTR(-EINVAL);

	ctx = alloc_percpu(struct blk_mq_ctx);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	hctxs = kmalloc_node(reg->nr_hw_queues * sizeof(*hctxs), GFP_KERNEL,
			reg->numa_node);

	if (!hctxs)
		goto err_percpu;

	for (i = 0; i < reg->nr_hw_queues; i++) {
		hctxs[i] = reg->ops->alloc_hctx(reg, i);
		if (!hctxs[i])
			goto err_hctxs;

		hctxs[i]->numa_node = NUMA_NO_NODE;
		hctxs[i]->queue_num = i;
	}

	q = blk_alloc_queue_node(GFP_KERNEL, reg->numa_node);
	if (!q)
		goto err_hctxs;

	q->mq_map = blk_mq_make_queue_map(reg);
	if (!q->mq_map)
		goto err_map;

	setup_timer(&q->timeout, blk_mq_rq_timer, (unsigned long) q);
	blk_queue_rq_timeout(q, 30000);

	q->nr_queues = nr_cpu_ids;
	q->nr_hw_queues = reg->nr_hw_queues;

	q->queue_ctx = ctx;
	q->queue_hw_ctx = hctxs;

	q->mq_ops = reg->ops;

	blk_queue_make_request(q, blk_mq_make_request);
	blk_queue_rq_timed_out(q, reg->ops->timeout);
	if (reg->timeout)
		blk_queue_rq_timeout(q, reg->timeout);

	blk_mq_init_flush(q);
	blk_mq_init_cpu_queues(q, reg->nr_hw_queues);

	if (blk_mq_init_hw_queues(q, reg, driver_data))
		goto err_hw;

	blk_mq_map_swqueue(q);

	mutex_lock(&all_q_mutex);
	list_add_tail(&q->all_q_node, &all_q_list);
	mutex_unlock(&all_q_mutex);

	return q;
err_hw:
	kfree(q->mq_map);
err_map:
	blk_cleanup_queue(q);
err_hctxs:
	for (i = 0; i < reg->nr_hw_queues; i++) {
		if (!hctxs[i])
			break;
		reg->ops->free_hctx(hctxs[i], i);
	}
	kfree(hctxs);
err_percpu:
	free_percpu(ctx);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(blk_mq_init_queue);

void blk_mq_free_queue(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		cancel_delayed_work_sync(&hctx->delayed_work);
		kfree(hctx->ctx_map);
		kfree(hctx->ctxs);
		blk_mq_free_rq_map(hctx);
		blk_mq_unregister_cpu_notifier(&hctx->cpu_notifier);
		if (q->mq_ops->exit_hctx)
			q->mq_ops->exit_hctx(hctx, i);
		q->mq_ops->free_hctx(hctx, i);
	}

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
EXPORT_SYMBOL(blk_mq_free_queue);

/* Basically redo blk_mq_init_queue with queue frozen */
static void blk_mq_queue_reinit(struct request_queue *q)
{
	blk_mq_freeze_queue(q);

	blk_mq_update_queue_map(q->mq_map, q->nr_hw_queues);

	/*
	 * redo blk_mq_init_cpu_queues and blk_mq_init_hw_queues. FIXME: maybe
	 * we should change hctx numa_node according to new topology (this
	 * involves free and re-allocate memory, worthy doing?)
	 */

	blk_mq_map_swqueue(q);

	blk_mq_unfreeze_queue(q);
}

static int blk_mq_queue_reinit_notify(struct notifier_block *nb,
				      unsigned long action, void *hcpu)
{
	struct request_queue *q;

	/*
	 * Before new mapping is established, hotadded cpu might already start
	 * handling requests. This doesn't break anything as we map offline
	 * CPUs to first hardware queue. We will re-init queue below to get
	 * optimal settings.
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

static int __init blk_mq_init(void)
{
	unsigned int i;

	for_each_possible_cpu(i)
		init_llist_head(&per_cpu(ipi_lists, i));

	blk_mq_cpu_init();

	/* Must be called after percpu_counter_hotcpu_callback() */
	hotcpu_notifier(blk_mq_queue_reinit_notify, -10);

	return 0;
}
subsys_initcall(blk_mq_init);
