// SPDX-License-Identifier: GPL-2.0
/*
 * Tag allocation using scalable bitmaps. Uses active queue tracking to support
 * fairer distribution of tags between multiple submitters when a shared tag map
 * is used.
 *
 * Copyright (C) 2013-2014 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/delay.h>
#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-sched.h"

/*
 * Recalculate wakeup batch when tag is shared by hctx.
 */
static void blk_mq_update_wake_batch(struct blk_mq_tags *tags,
		unsigned int users)
{
	if (!users)
		return;

	sbitmap_queue_recalculate_wake_batch(&tags->bitmap_tags,
			users);
	sbitmap_queue_recalculate_wake_batch(&tags->breserved_tags,
			users);
}

/*
 * If a previously inactive queue goes active, bump the active user count.
 * We need to do this before try to allocate driver tag, then even if fail
 * to get tag when first time, the other shared-tag users could reserve
 * budget for it.
 */
void __blk_mq_tag_busy(struct blk_mq_hw_ctx *hctx)
{
	unsigned int users;
	unsigned long flags;
	struct blk_mq_tags *tags = hctx->tags;

	/*
	 * calling test_bit() prior to test_and_set_bit() is intentional,
	 * it avoids dirtying the cacheline if the queue is already active.
	 */
	if (blk_mq_is_shared_tags(hctx->flags)) {
		struct request_queue *q = hctx->queue;

		if (test_bit(QUEUE_FLAG_HCTX_ACTIVE, &q->queue_flags) ||
		    test_and_set_bit(QUEUE_FLAG_HCTX_ACTIVE, &q->queue_flags))
			return;
	} else {
		if (test_bit(BLK_MQ_S_TAG_ACTIVE, &hctx->state) ||
		    test_and_set_bit(BLK_MQ_S_TAG_ACTIVE, &hctx->state))
			return;
	}

	spin_lock_irqsave(&tags->lock, flags);
	users = tags->active_queues + 1;
	WRITE_ONCE(tags->active_queues, users);
	blk_mq_update_wake_batch(tags, users);
	spin_unlock_irqrestore(&tags->lock, flags);
}

/*
 * Wakeup all potentially sleeping on tags
 */
void blk_mq_tag_wakeup_all(struct blk_mq_tags *tags, bool include_reserve)
{
	sbitmap_queue_wake_all(&tags->bitmap_tags);
	if (include_reserve)
		sbitmap_queue_wake_all(&tags->breserved_tags);
}

/*
 * If a previously busy queue goes inactive, potential waiters could now
 * be allowed to queue. Wake them up and check.
 */
void __blk_mq_tag_idle(struct blk_mq_hw_ctx *hctx)
{
	struct blk_mq_tags *tags = hctx->tags;
	unsigned int users;

	if (blk_mq_is_shared_tags(hctx->flags)) {
		struct request_queue *q = hctx->queue;

		if (!test_and_clear_bit(QUEUE_FLAG_HCTX_ACTIVE,
					&q->queue_flags))
			return;
	} else {
		if (!test_and_clear_bit(BLK_MQ_S_TAG_ACTIVE, &hctx->state))
			return;
	}

	spin_lock_irq(&tags->lock);
	users = tags->active_queues - 1;
	WRITE_ONCE(tags->active_queues, users);
	blk_mq_update_wake_batch(tags, users);
	spin_unlock_irq(&tags->lock);

	blk_mq_tag_wakeup_all(tags, false);
}

static int __blk_mq_get_tag(struct blk_mq_alloc_data *data,
			    struct sbitmap_queue *bt)
{
	if (!data->q->elevator && !(data->flags & BLK_MQ_REQ_RESERVED) &&
			!hctx_may_queue(data->hctx, bt))
		return BLK_MQ_NO_TAG;

	if (data->shallow_depth)
		return sbitmap_queue_get_shallow(bt, data->shallow_depth);
	else
		return __sbitmap_queue_get(bt);
}

unsigned long blk_mq_get_tags(struct blk_mq_alloc_data *data, int nr_tags,
			      unsigned int *offset)
{
	struct blk_mq_tags *tags = blk_mq_tags_from_data(data);
	struct sbitmap_queue *bt = &tags->bitmap_tags;
	unsigned long ret;

	if (data->shallow_depth ||data->flags & BLK_MQ_REQ_RESERVED ||
	    data->hctx->flags & BLK_MQ_F_TAG_QUEUE_SHARED)
		return 0;
	ret = __sbitmap_queue_get_batch(bt, nr_tags, offset);
	*offset += tags->nr_reserved_tags;
	return ret;
}

unsigned int blk_mq_get_tag(struct blk_mq_alloc_data *data)
{
	struct blk_mq_tags *tags = blk_mq_tags_from_data(data);
	struct sbitmap_queue *bt;
	struct sbq_wait_state *ws;
	DEFINE_SBQ_WAIT(wait);
	unsigned int tag_offset;
	int tag;

	if (data->flags & BLK_MQ_REQ_RESERVED) {
		if (unlikely(!tags->nr_reserved_tags)) {
			WARN_ON_ONCE(1);
			return BLK_MQ_NO_TAG;
		}
		bt = &tags->breserved_tags;
		tag_offset = 0;
	} else {
		bt = &tags->bitmap_tags;
		tag_offset = tags->nr_reserved_tags;
	}

	tag = __blk_mq_get_tag(data, bt);
	if (tag != BLK_MQ_NO_TAG)
		goto found_tag;

	if (data->flags & BLK_MQ_REQ_NOWAIT)
		return BLK_MQ_NO_TAG;

	ws = bt_wait_ptr(bt, data->hctx);
	do {
		struct sbitmap_queue *bt_prev;

		/*
		 * We're out of tags on this hardware queue, kick any
		 * pending IO submits before going to sleep waiting for
		 * some to complete.
		 */
		blk_mq_run_hw_queue(data->hctx, false);

		/*
		 * Retry tag allocation after running the hardware queue,
		 * as running the queue may also have found completions.
		 */
		tag = __blk_mq_get_tag(data, bt);
		if (tag != BLK_MQ_NO_TAG)
			break;

		sbitmap_prepare_to_wait(bt, ws, &wait, TASK_UNINTERRUPTIBLE);

		tag = __blk_mq_get_tag(data, bt);
		if (tag != BLK_MQ_NO_TAG)
			break;

		bt_prev = bt;
		io_schedule();

		sbitmap_finish_wait(bt, ws, &wait);

		data->ctx = blk_mq_get_ctx(data->q);
		data->hctx = blk_mq_map_queue(data->cmd_flags, data->ctx);
		tags = blk_mq_tags_from_data(data);
		if (data->flags & BLK_MQ_REQ_RESERVED)
			bt = &tags->breserved_tags;
		else
			bt = &tags->bitmap_tags;

		/*
		 * If destination hw queue is changed, fake wake up on
		 * previous queue for compensating the wake up miss, so
		 * other allocations on previous queue won't be starved.
		 */
		if (bt != bt_prev)
			sbitmap_queue_wake_up(bt_prev, 1);

		ws = bt_wait_ptr(bt, data->hctx);
	} while (1);

	sbitmap_finish_wait(bt, ws, &wait);

found_tag:
	/*
	 * Give up this allocation if the hctx is inactive.  The caller will
	 * retry on an active hctx.
	 */
	if (unlikely(test_bit(BLK_MQ_S_INACTIVE, &data->hctx->state))) {
		blk_mq_put_tag(tags, data->ctx, tag + tag_offset);
		return BLK_MQ_NO_TAG;
	}
	return tag + tag_offset;
}

void blk_mq_put_tag(struct blk_mq_tags *tags, struct blk_mq_ctx *ctx,
		    unsigned int tag)
{
	if (!blk_mq_tag_is_reserved(tags, tag)) {
		const int real_tag = tag - tags->nr_reserved_tags;

		BUG_ON(real_tag >= tags->nr_tags);
		sbitmap_queue_clear(&tags->bitmap_tags, real_tag, ctx->cpu);
	} else {
		sbitmap_queue_clear(&tags->breserved_tags, tag, ctx->cpu);
	}
}

void blk_mq_put_tags(struct blk_mq_tags *tags, int *tag_array, int nr_tags)
{
	sbitmap_queue_clear_batch(&tags->bitmap_tags, tags->nr_reserved_tags,
					tag_array, nr_tags);
}

struct bt_iter_data {
	struct blk_mq_hw_ctx *hctx;
	struct request_queue *q;
	busy_tag_iter_fn *fn;
	void *data;
	bool reserved;
};

static struct request *blk_mq_find_and_get_req(struct blk_mq_tags *tags,
		unsigned int bitnr)
{
	struct request *rq;
	unsigned long flags;

	spin_lock_irqsave(&tags->lock, flags);
	rq = tags->rqs[bitnr];
	if (!rq || rq->tag != bitnr || !req_ref_inc_not_zero(rq))
		rq = NULL;
	spin_unlock_irqrestore(&tags->lock, flags);
	return rq;
}

static bool bt_iter(struct sbitmap *bitmap, unsigned int bitnr, void *data)
{
	struct bt_iter_data *iter_data = data;
	struct blk_mq_hw_ctx *hctx = iter_data->hctx;
	struct request_queue *q = iter_data->q;
	struct blk_mq_tag_set *set = q->tag_set;
	struct blk_mq_tags *tags;
	struct request *rq;
	bool ret = true;

	if (blk_mq_is_shared_tags(set->flags))
		tags = set->shared_tags;
	else
		tags = hctx->tags;

	if (!iter_data->reserved)
		bitnr += tags->nr_reserved_tags;
	/*
	 * We can hit rq == NULL here, because the tagging functions
	 * test and set the bit before assigning ->rqs[].
	 */
	rq = blk_mq_find_and_get_req(tags, bitnr);
	if (!rq)
		return true;

	if (rq->q == q && (!hctx || rq->mq_hctx == hctx))
		ret = iter_data->fn(rq, iter_data->data);
	blk_mq_put_rq_ref(rq);
	return ret;
}

/**
 * bt_for_each - iterate over the requests associated with a hardware queue
 * @hctx:	Hardware queue to examine.
 * @q:		Request queue to examine.
 * @bt:		sbitmap to examine. This is either the breserved_tags member
 *		or the bitmap_tags member of struct blk_mq_tags.
 * @fn:		Pointer to the function that will be called for each request
 *		associated with @hctx that has been assigned a driver tag.
 *		@fn will be called as follows: @fn(@hctx, rq, @data, @reserved)
 *		where rq is a pointer to a request. Return true to continue
 *		iterating tags, false to stop.
 * @data:	Will be passed as third argument to @fn.
 * @reserved:	Indicates whether @bt is the breserved_tags member or the
 *		bitmap_tags member of struct blk_mq_tags.
 */
static void bt_for_each(struct blk_mq_hw_ctx *hctx, struct request_queue *q,
			struct sbitmap_queue *bt, busy_tag_iter_fn *fn,
			void *data, bool reserved)
{
	struct bt_iter_data iter_data = {
		.hctx = hctx,
		.fn = fn,
		.data = data,
		.reserved = reserved,
		.q = q,
	};

	sbitmap_for_each_set(&bt->sb, bt_iter, &iter_data);
}

struct bt_tags_iter_data {
	struct blk_mq_tags *tags;
	busy_tag_iter_fn *fn;
	void *data;
	unsigned int flags;
};

#define BT_TAG_ITER_RESERVED		(1 << 0)
#define BT_TAG_ITER_STARTED		(1 << 1)
#define BT_TAG_ITER_STATIC_RQS		(1 << 2)

static bool bt_tags_iter(struct sbitmap *bitmap, unsigned int bitnr, void *data)
{
	struct bt_tags_iter_data *iter_data = data;
	struct blk_mq_tags *tags = iter_data->tags;
	struct request *rq;
	bool ret = true;
	bool iter_static_rqs = !!(iter_data->flags & BT_TAG_ITER_STATIC_RQS);

	if (!(iter_data->flags & BT_TAG_ITER_RESERVED))
		bitnr += tags->nr_reserved_tags;

	/*
	 * We can hit rq == NULL here, because the tagging functions
	 * test and set the bit before assigning ->rqs[].
	 */
	if (iter_static_rqs)
		rq = tags->static_rqs[bitnr];
	else
		rq = blk_mq_find_and_get_req(tags, bitnr);
	if (!rq)
		return true;

	if (!(iter_data->flags & BT_TAG_ITER_STARTED) ||
	    blk_mq_request_started(rq))
		ret = iter_data->fn(rq, iter_data->data);
	if (!iter_static_rqs)
		blk_mq_put_rq_ref(rq);
	return ret;
}

/**
 * bt_tags_for_each - iterate over the requests in a tag map
 * @tags:	Tag map to iterate over.
 * @bt:		sbitmap to examine. This is either the breserved_tags member
 *		or the bitmap_tags member of struct blk_mq_tags.
 * @fn:		Pointer to the function that will be called for each started
 *		request. @fn will be called as follows: @fn(rq, @data,
 *		@reserved) where rq is a pointer to a request. Return true
 *		to continue iterating tags, false to stop.
 * @data:	Will be passed as second argument to @fn.
 * @flags:	BT_TAG_ITER_*
 */
static void bt_tags_for_each(struct blk_mq_tags *tags, struct sbitmap_queue *bt,
			     busy_tag_iter_fn *fn, void *data, unsigned int flags)
{
	struct bt_tags_iter_data iter_data = {
		.tags = tags,
		.fn = fn,
		.data = data,
		.flags = flags,
	};

	if (tags->rqs)
		sbitmap_for_each_set(&bt->sb, bt_tags_iter, &iter_data);
}

static void __blk_mq_all_tag_iter(struct blk_mq_tags *tags,
		busy_tag_iter_fn *fn, void *priv, unsigned int flags)
{
	WARN_ON_ONCE(flags & BT_TAG_ITER_RESERVED);

	if (tags->nr_reserved_tags)
		bt_tags_for_each(tags, &tags->breserved_tags, fn, priv,
				 flags | BT_TAG_ITER_RESERVED);
	bt_tags_for_each(tags, &tags->bitmap_tags, fn, priv, flags);
}

/**
 * blk_mq_all_tag_iter - iterate over all requests in a tag map
 * @tags:	Tag map to iterate over.
 * @fn:		Pointer to the function that will be called for each
 *		request. @fn will be called as follows: @fn(rq, @priv,
 *		reserved) where rq is a pointer to a request. 'reserved'
 *		indicates whether or not @rq is a reserved request. Return
 *		true to continue iterating tags, false to stop.
 * @priv:	Will be passed as second argument to @fn.
 *
 * Caller has to pass the tag map from which requests are allocated.
 */
void blk_mq_all_tag_iter(struct blk_mq_tags *tags, busy_tag_iter_fn *fn,
		void *priv)
{
	__blk_mq_all_tag_iter(tags, fn, priv, BT_TAG_ITER_STATIC_RQS);
}

/**
 * blk_mq_tagset_busy_iter - iterate over all started requests in a tag set
 * @tagset:	Tag set to iterate over.
 * @fn:		Pointer to the function that will be called for each started
 *		request. @fn will be called as follows: @fn(rq, @priv,
 *		reserved) where rq is a pointer to a request. 'reserved'
 *		indicates whether or not @rq is a reserved request. Return
 *		true to continue iterating tags, false to stop.
 * @priv:	Will be passed as second argument to @fn.
 *
 * We grab one request reference before calling @fn and release it after
 * @fn returns.
 */
void blk_mq_tagset_busy_iter(struct blk_mq_tag_set *tagset,
		busy_tag_iter_fn *fn, void *priv)
{
	unsigned int flags = tagset->flags;
	int i, nr_tags;

	nr_tags = blk_mq_is_shared_tags(flags) ? 1 : tagset->nr_hw_queues;

	for (i = 0; i < nr_tags; i++) {
		if (tagset->tags && tagset->tags[i])
			__blk_mq_all_tag_iter(tagset->tags[i], fn, priv,
					      BT_TAG_ITER_STARTED);
	}
}
EXPORT_SYMBOL(blk_mq_tagset_busy_iter);

static bool blk_mq_tagset_count_completed_rqs(struct request *rq, void *data)
{
	unsigned *count = data;

	if (blk_mq_request_completed(rq))
		(*count)++;
	return true;
}

/**
 * blk_mq_tagset_wait_completed_request - Wait until all scheduled request
 * completions have finished.
 * @tagset:	Tag set to drain completed request
 *
 * Note: This function has to be run after all IO queues are shutdown
 */
void blk_mq_tagset_wait_completed_request(struct blk_mq_tag_set *tagset)
{
	while (true) {
		unsigned count = 0;

		blk_mq_tagset_busy_iter(tagset,
				blk_mq_tagset_count_completed_rqs, &count);
		if (!count)
			break;
		msleep(5);
	}
}
EXPORT_SYMBOL(blk_mq_tagset_wait_completed_request);

/**
 * blk_mq_queue_tag_busy_iter - iterate over all requests with a driver tag
 * @q:		Request queue to examine.
 * @fn:		Pointer to the function that will be called for each request
 *		on @q. @fn will be called as follows: @fn(hctx, rq, @priv,
 *		reserved) where rq is a pointer to a request and hctx points
 *		to the hardware queue associated with the request. 'reserved'
 *		indicates whether or not @rq is a reserved request.
 * @priv:	Will be passed as third argument to @fn.
 *
 * Note: if @q->tag_set is shared with other request queues then @fn will be
 * called for all requests on all queues that share that tag set and not only
 * for requests associated with @q.
 */
void blk_mq_queue_tag_busy_iter(struct request_queue *q, busy_tag_iter_fn *fn,
		void *priv)
{
	/*
	 * __blk_mq_update_nr_hw_queues() updates nr_hw_queues and hctx_table
	 * while the queue is frozen. So we can use q_usage_counter to avoid
	 * racing with it.
	 */
	if (!percpu_ref_tryget(&q->q_usage_counter))
		return;

	if (blk_mq_is_shared_tags(q->tag_set->flags)) {
		struct blk_mq_tags *tags = q->tag_set->shared_tags;
		struct sbitmap_queue *bresv = &tags->breserved_tags;
		struct sbitmap_queue *btags = &tags->bitmap_tags;

		if (tags->nr_reserved_tags)
			bt_for_each(NULL, q, bresv, fn, priv, true);
		bt_for_each(NULL, q, btags, fn, priv, false);
	} else {
		struct blk_mq_hw_ctx *hctx;
		unsigned long i;

		queue_for_each_hw_ctx(q, hctx, i) {
			struct blk_mq_tags *tags = hctx->tags;
			struct sbitmap_queue *bresv = &tags->breserved_tags;
			struct sbitmap_queue *btags = &tags->bitmap_tags;

			/*
			 * If no software queues are currently mapped to this
			 * hardware queue, there's nothing to check
			 */
			if (!blk_mq_hw_queue_mapped(hctx))
				continue;

			if (tags->nr_reserved_tags)
				bt_for_each(hctx, q, bresv, fn, priv, true);
			bt_for_each(hctx, q, btags, fn, priv, false);
		}
	}
	blk_queue_exit(q);
}

static int bt_alloc(struct sbitmap_queue *bt, unsigned int depth,
		    bool round_robin, int node)
{
	return sbitmap_queue_init_node(bt, depth, -1, round_robin, GFP_KERNEL,
				       node);
}

struct blk_mq_tags *blk_mq_init_tags(unsigned int total_tags,
		unsigned int reserved_tags, unsigned int flags, int node)
{
	unsigned int depth = total_tags - reserved_tags;
	bool round_robin = flags & BLK_MQ_F_TAG_RR;
	struct blk_mq_tags *tags;

	if (total_tags > BLK_MQ_TAG_MAX) {
		pr_err("blk-mq: tag depth too large\n");
		return NULL;
	}

	tags = kzalloc_node(sizeof(*tags), GFP_KERNEL, node);
	if (!tags)
		return NULL;

	tags->nr_tags = total_tags;
	tags->nr_reserved_tags = reserved_tags;
	spin_lock_init(&tags->lock);
	if (bt_alloc(&tags->bitmap_tags, depth, round_robin, node))
		goto out_free_tags;
	if (bt_alloc(&tags->breserved_tags, reserved_tags, round_robin, node))
		goto out_free_bitmap_tags;

	return tags;

out_free_bitmap_tags:
	sbitmap_queue_free(&tags->bitmap_tags);
out_free_tags:
	kfree(tags);
	return NULL;
}

void blk_mq_free_tags(struct blk_mq_tags *tags)
{
	sbitmap_queue_free(&tags->bitmap_tags);
	sbitmap_queue_free(&tags->breserved_tags);
	kfree(tags);
}

int blk_mq_tag_update_depth(struct blk_mq_hw_ctx *hctx,
			    struct blk_mq_tags **tagsptr, unsigned int tdepth,
			    bool can_grow)
{
	struct blk_mq_tags *tags = *tagsptr;

	if (tdepth <= tags->nr_reserved_tags)
		return -EINVAL;

	/*
	 * If we are allowed to grow beyond the original size, allocate
	 * a new set of tags before freeing the old one.
	 */
	if (tdepth > tags->nr_tags) {
		struct blk_mq_tag_set *set = hctx->queue->tag_set;
		struct blk_mq_tags *new;

		if (!can_grow)
			return -EINVAL;

		/*
		 * We need some sort of upper limit, set it high enough that
		 * no valid use cases should require more.
		 */
		if (tdepth > MAX_SCHED_RQ)
			return -EINVAL;

		/*
		 * Only the sbitmap needs resizing since we allocated the max
		 * initially.
		 */
		if (blk_mq_is_shared_tags(set->flags))
			return 0;

		new = blk_mq_alloc_map_and_rqs(set, hctx->queue_num, tdepth);
		if (!new)
			return -ENOMEM;

		blk_mq_free_map_and_rqs(set, *tagsptr, hctx->queue_num);
		*tagsptr = new;
	} else {
		/*
		 * Don't need (or can't) update reserved tags here, they
		 * remain static and should never need resizing.
		 */
		sbitmap_queue_resize(&tags->bitmap_tags,
				tdepth - tags->nr_reserved_tags);
	}

	return 0;
}

void blk_mq_tag_resize_shared_tags(struct blk_mq_tag_set *set, unsigned int size)
{
	struct blk_mq_tags *tags = set->shared_tags;

	sbitmap_queue_resize(&tags->bitmap_tags, size - set->reserved_tags);
}

void blk_mq_tag_update_sched_shared_tags(struct request_queue *q)
{
	sbitmap_queue_resize(&q->sched_shared_tags->bitmap_tags,
			     q->nr_requests - q->tag_set->reserved_tags);
}

/**
 * blk_mq_unique_tag() - return a tag that is unique queue-wide
 * @rq: request for which to compute a unique tag
 *
 * The tag field in struct request is unique per hardware queue but not over
 * all hardware queues. Hence this function that returns a tag with the
 * hardware context index in the upper bits and the per hardware queue tag in
 * the lower bits.
 *
 * Note: When called for a request that is queued on a non-multiqueue request
 * queue, the hardware context index is set to zero.
 */
u32 blk_mq_unique_tag(struct request *rq)
{
	return (rq->mq_hctx->queue_num << BLK_MQ_UNIQUE_TAG_BITS) |
		(rq->tag & BLK_MQ_UNIQUE_TAG_MASK);
}
EXPORT_SYMBOL(blk_mq_unique_tag);
