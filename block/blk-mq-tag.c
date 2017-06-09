/*
 * Tag allocation using scalable bitmaps. Uses active queue tracking to support
 * fairer distribution of tags between multiple submitters when a shared tag map
 * is used.
 *
 * Copyright (C) 2013-2014 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/blk-mq.h>
#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-tag.h"

bool blk_mq_has_free_tags(struct blk_mq_tags *tags)
{
	if (!tags)
		return true;

	return sbitmap_any_bit_clear(&tags->bitmap_tags.sb);
}

/*
 * If a previously inactive queue goes active, bump the active user count.
 */
bool __blk_mq_tag_busy(struct blk_mq_hw_ctx *hctx)
{
	if (!test_bit(BLK_MQ_S_TAG_ACTIVE, &hctx->state) &&
	    !test_and_set_bit(BLK_MQ_S_TAG_ACTIVE, &hctx->state))
		atomic_inc(&hctx->tags->active_queues);

	return true;
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

	if (!test_and_clear_bit(BLK_MQ_S_TAG_ACTIVE, &hctx->state))
		return;

	atomic_dec(&tags->active_queues);

	blk_mq_tag_wakeup_all(tags, false);
}

/*
 * For shared tag users, we track the number of currently active users
 * and attempt to provide a fair share of the tag depth for each of them.
 */
static inline bool hctx_may_queue(struct blk_mq_hw_ctx *hctx,
				  struct sbitmap_queue *bt)
{
	unsigned int depth, users;

	if (!hctx || !(hctx->flags & BLK_MQ_F_TAG_SHARED))
		return true;
	if (!test_bit(BLK_MQ_S_TAG_ACTIVE, &hctx->state))
		return true;

	/*
	 * Don't try dividing an ant
	 */
	if (bt->sb.depth == 1)
		return true;

	users = atomic_read(&hctx->tags->active_queues);
	if (!users)
		return true;

	/*
	 * Allow at least some tags
	 */
	depth = max((bt->sb.depth + users - 1) / users, 4U);
	return atomic_read(&hctx->nr_active) < depth;
}

static int __blk_mq_get_tag(struct blk_mq_alloc_data *data,
			    struct sbitmap_queue *bt)
{
	if (!(data->flags & BLK_MQ_REQ_INTERNAL) &&
	    !hctx_may_queue(data->hctx, bt))
		return -1;
	if (data->shallow_depth)
		return __sbitmap_queue_get_shallow(bt, data->shallow_depth);
	else
		return __sbitmap_queue_get(bt);
}

unsigned int blk_mq_get_tag(struct blk_mq_alloc_data *data)
{
	struct blk_mq_tags *tags = blk_mq_tags_from_data(data);
	struct sbitmap_queue *bt;
	struct sbq_wait_state *ws;
	DEFINE_WAIT(wait);
	unsigned int tag_offset;
	bool drop_ctx;
	int tag;

	if (data->flags & BLK_MQ_REQ_RESERVED) {
		if (unlikely(!tags->nr_reserved_tags)) {
			WARN_ON_ONCE(1);
			return BLK_MQ_TAG_FAIL;
		}
		bt = &tags->breserved_tags;
		tag_offset = 0;
	} else {
		bt = &tags->bitmap_tags;
		tag_offset = tags->nr_reserved_tags;
	}

	tag = __blk_mq_get_tag(data, bt);
	if (tag != -1)
		goto found_tag;

	if (data->flags & BLK_MQ_REQ_NOWAIT)
		return BLK_MQ_TAG_FAIL;

	ws = bt_wait_ptr(bt, data->hctx);
	drop_ctx = data->ctx == NULL;
	do {
		prepare_to_wait(&ws->wait, &wait, TASK_UNINTERRUPTIBLE);

		tag = __blk_mq_get_tag(data, bt);
		if (tag != -1)
			break;

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
		if (tag != -1)
			break;

		if (data->ctx)
			blk_mq_put_ctx(data->ctx);

		io_schedule();

		data->ctx = blk_mq_get_ctx(data->q);
		data->hctx = blk_mq_map_queue(data->q, data->ctx->cpu);
		tags = blk_mq_tags_from_data(data);
		if (data->flags & BLK_MQ_REQ_RESERVED)
			bt = &tags->breserved_tags;
		else
			bt = &tags->bitmap_tags;

		finish_wait(&ws->wait, &wait);
		ws = bt_wait_ptr(bt, data->hctx);
	} while (1);

	if (drop_ctx && data->ctx)
		blk_mq_put_ctx(data->ctx);

	finish_wait(&ws->wait, &wait);

found_tag:
	return tag + tag_offset;
}

void blk_mq_put_tag(struct blk_mq_hw_ctx *hctx, struct blk_mq_tags *tags,
		    struct blk_mq_ctx *ctx, unsigned int tag)
{
	if (!blk_mq_tag_is_reserved(tags, tag)) {
		const int real_tag = tag - tags->nr_reserved_tags;

		BUG_ON(real_tag >= tags->nr_tags);
		sbitmap_queue_clear(&tags->bitmap_tags, real_tag, ctx->cpu);
	} else {
		BUG_ON(tag >= tags->nr_reserved_tags);
		sbitmap_queue_clear(&tags->breserved_tags, tag, ctx->cpu);
	}
}

struct bt_iter_data {
	struct blk_mq_hw_ctx *hctx;
	busy_iter_fn *fn;
	void *data;
	bool reserved;
};

static bool bt_iter(struct sbitmap *bitmap, unsigned int bitnr, void *data)
{
	struct bt_iter_data *iter_data = data;
	struct blk_mq_hw_ctx *hctx = iter_data->hctx;
	struct blk_mq_tags *tags = hctx->tags;
	bool reserved = iter_data->reserved;
	struct request *rq;

	if (!reserved)
		bitnr += tags->nr_reserved_tags;
	rq = tags->rqs[bitnr];

	if (rq->q == hctx->queue)
		iter_data->fn(hctx, rq, iter_data->data, reserved);
	return true;
}

static void bt_for_each(struct blk_mq_hw_ctx *hctx, struct sbitmap_queue *bt,
			busy_iter_fn *fn, void *data, bool reserved)
{
	struct bt_iter_data iter_data = {
		.hctx = hctx,
		.fn = fn,
		.data = data,
		.reserved = reserved,
	};

	sbitmap_for_each_set(&bt->sb, bt_iter, &iter_data);
}

struct bt_tags_iter_data {
	struct blk_mq_tags *tags;
	busy_tag_iter_fn *fn;
	void *data;
	bool reserved;
};

static bool bt_tags_iter(struct sbitmap *bitmap, unsigned int bitnr, void *data)
{
	struct bt_tags_iter_data *iter_data = data;
	struct blk_mq_tags *tags = iter_data->tags;
	bool reserved = iter_data->reserved;
	struct request *rq;

	if (!reserved)
		bitnr += tags->nr_reserved_tags;
	rq = tags->rqs[bitnr];

	iter_data->fn(rq, iter_data->data, reserved);
	return true;
}

static void bt_tags_for_each(struct blk_mq_tags *tags, struct sbitmap_queue *bt,
			     busy_tag_iter_fn *fn, void *data, bool reserved)
{
	struct bt_tags_iter_data iter_data = {
		.tags = tags,
		.fn = fn,
		.data = data,
		.reserved = reserved,
	};

	if (tags->rqs)
		sbitmap_for_each_set(&bt->sb, bt_tags_iter, &iter_data);
}

static void blk_mq_all_tag_busy_iter(struct blk_mq_tags *tags,
		busy_tag_iter_fn *fn, void *priv)
{
	if (tags->nr_reserved_tags)
		bt_tags_for_each(tags, &tags->breserved_tags, fn, priv, true);
	bt_tags_for_each(tags, &tags->bitmap_tags, fn, priv, false);
}

void blk_mq_tagset_busy_iter(struct blk_mq_tag_set *tagset,
		busy_tag_iter_fn *fn, void *priv)
{
	int i;

	for (i = 0; i < tagset->nr_hw_queues; i++) {
		if (tagset->tags && tagset->tags[i])
			blk_mq_all_tag_busy_iter(tagset->tags[i], fn, priv);
	}
}
EXPORT_SYMBOL(blk_mq_tagset_busy_iter);

int blk_mq_reinit_tagset(struct blk_mq_tag_set *set)
{
	int i, j, ret = 0;

	if (!set->ops->reinit_request)
		goto out;

	for (i = 0; i < set->nr_hw_queues; i++) {
		struct blk_mq_tags *tags = set->tags[i];

		if (!tags)
			continue;

		for (j = 0; j < tags->nr_tags; j++) {
			if (!tags->static_rqs[j])
				continue;

			ret = set->ops->reinit_request(set->driver_data,
						tags->static_rqs[j]);
			if (ret)
				goto out;
		}
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(blk_mq_reinit_tagset);

void blk_mq_queue_tag_busy_iter(struct request_queue *q, busy_iter_fn *fn,
		void *priv)
{
	struct blk_mq_hw_ctx *hctx;
	int i;


	queue_for_each_hw_ctx(q, hctx, i) {
		struct blk_mq_tags *tags = hctx->tags;

		/*
		 * If not software queues are currently mapped to this
		 * hardware queue, there's nothing to check
		 */
		if (!blk_mq_hw_queue_mapped(hctx))
			continue;

		if (tags->nr_reserved_tags)
			bt_for_each(hctx, &tags->breserved_tags, fn, priv, true);
		bt_for_each(hctx, &tags->bitmap_tags, fn, priv, false);
	}

}

static int bt_alloc(struct sbitmap_queue *bt, unsigned int depth,
		    bool round_robin, int node)
{
	return sbitmap_queue_init_node(bt, depth, -1, round_robin, GFP_KERNEL,
				       node);
}

static struct blk_mq_tags *blk_mq_init_bitmap_tags(struct blk_mq_tags *tags,
						   int node, int alloc_policy)
{
	unsigned int depth = tags->nr_tags - tags->nr_reserved_tags;
	bool round_robin = alloc_policy == BLK_TAG_ALLOC_RR;

	if (bt_alloc(&tags->bitmap_tags, depth, round_robin, node))
		goto free_tags;
	if (bt_alloc(&tags->breserved_tags, tags->nr_reserved_tags, round_robin,
		     node))
		goto free_bitmap_tags;

	return tags;
free_bitmap_tags:
	sbitmap_queue_free(&tags->bitmap_tags);
free_tags:
	kfree(tags);
	return NULL;
}

struct blk_mq_tags *blk_mq_init_tags(unsigned int total_tags,
				     unsigned int reserved_tags,
				     int node, int alloc_policy)
{
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

	return blk_mq_init_bitmap_tags(tags, node, alloc_policy);
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

	tdepth -= tags->nr_reserved_tags;

	/*
	 * If we are allowed to grow beyond the original size, allocate
	 * a new set of tags before freeing the old one.
	 */
	if (tdepth > tags->nr_tags) {
		struct blk_mq_tag_set *set = hctx->queue->tag_set;
		struct blk_mq_tags *new;
		bool ret;

		if (!can_grow)
			return -EINVAL;

		/*
		 * We need some sort of upper limit, set it high enough that
		 * no valid use cases should require more.
		 */
		if (tdepth > 16 * BLKDEV_MAX_RQ)
			return -EINVAL;

		new = blk_mq_alloc_rq_map(set, hctx->queue_num, tdepth, 0);
		if (!new)
			return -ENOMEM;
		ret = blk_mq_alloc_rqs(set, new, hctx->queue_num, tdepth);
		if (ret) {
			blk_mq_free_rq_map(new);
			return -ENOMEM;
		}

		blk_mq_free_rqs(set, *tagsptr, hctx->queue_num);
		blk_mq_free_rq_map(*tagsptr);
		*tagsptr = new;
	} else {
		/*
		 * Don't need (or can't) update reserved tags here, they
		 * remain static and should never need resizing.
		 */
		sbitmap_queue_resize(&tags->bitmap_tags, tdepth);
	}

	return 0;
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
	struct request_queue *q = rq->q;
	struct blk_mq_hw_ctx *hctx;
	int hwq = 0;

	if (q->mq_ops) {
		hctx = blk_mq_map_queue(q, rq->mq_ctx->cpu);
		hwq = hctx->queue_num;
	}

	return (hwq << BLK_MQ_UNIQUE_TAG_BITS) |
		(rq->tag & BLK_MQ_UNIQUE_TAG_MASK);
}
EXPORT_SYMBOL(blk_mq_unique_tag);
