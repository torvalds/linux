/*
 * Fast and scalable bitmap tagging variant. Uses sparser bitmaps spread
 * over multiple cachelines to avoid ping-pong between multiple submitters
 * or submitter and completer. Uses rolling wakeups to avoid falling of
 * the scaling cliff when we run out of tags and have to start putting
 * submitters to sleep.
 *
 * Uses active queue tracking to support fairer distribution of tags
 * between multiple submitters when a shared tag map is used.
 *
 * Copyright (C) 2013-2014 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>

#include <linux/blk-mq.h>
#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-tag.h"

static bool bt_has_free_tags(struct blk_mq_bitmap_tags *bt)
{
	int i;

	for (i = 0; i < bt->map_nr; i++) {
		struct blk_align_bitmap *bm = &bt->map[i];
		int ret;

		ret = find_first_zero_bit(&bm->word, bm->depth);
		if (ret < bm->depth)
			return true;
	}

	return false;
}

bool blk_mq_has_free_tags(struct blk_mq_tags *tags)
{
	if (!tags)
		return true;

	return bt_has_free_tags(&tags->bitmap_tags);
}

static inline int bt_index_inc(int index)
{
	return (index + 1) & (BT_WAIT_QUEUES - 1);
}

static inline void bt_index_atomic_inc(atomic_t *index)
{
	int old = atomic_read(index);
	int new = bt_index_inc(old);
	atomic_cmpxchg(index, old, new);
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
 * Wakeup all potentially sleeping on normal (non-reserved) tags
 */
static void blk_mq_tag_wakeup_all(struct blk_mq_tags *tags)
{
	struct blk_mq_bitmap_tags *bt;
	int i, wake_index;

	bt = &tags->bitmap_tags;
	wake_index = atomic_read(&bt->wake_index);
	for (i = 0; i < BT_WAIT_QUEUES; i++) {
		struct bt_wait_state *bs = &bt->bs[wake_index];

		if (waitqueue_active(&bs->wait))
			wake_up(&bs->wait);

		wake_index = bt_index_inc(wake_index);
	}
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

	blk_mq_tag_wakeup_all(tags);
}

/*
 * For shared tag users, we track the number of currently active users
 * and attempt to provide a fair share of the tag depth for each of them.
 */
static inline bool hctx_may_queue(struct blk_mq_hw_ctx *hctx,
				  struct blk_mq_bitmap_tags *bt)
{
	unsigned int depth, users;

	if (!hctx || !(hctx->flags & BLK_MQ_F_TAG_SHARED))
		return true;
	if (!test_bit(BLK_MQ_S_TAG_ACTIVE, &hctx->state))
		return true;

	/*
	 * Don't try dividing an ant
	 */
	if (bt->depth == 1)
		return true;

	users = atomic_read(&hctx->tags->active_queues);
	if (!users)
		return true;

	/*
	 * Allow at least some tags
	 */
	depth = max((bt->depth + users - 1) / users, 4U);
	return atomic_read(&hctx->nr_active) < depth;
}

static int __bt_get_word(struct blk_align_bitmap *bm, unsigned int last_tag)
{
	int tag, org_last_tag, end;

	org_last_tag = last_tag;
	end = bm->depth;
	do {
restart:
		tag = find_next_zero_bit(&bm->word, end, last_tag);
		if (unlikely(tag >= end)) {
			/*
			 * We started with an offset, start from 0 to
			 * exhaust the map.
			 */
			if (org_last_tag && last_tag) {
				end = last_tag;
				last_tag = 0;
				goto restart;
			}
			return -1;
		}
		last_tag = tag + 1;
	} while (test_and_set_bit_lock(tag, &bm->word));

	return tag;
}

/*
 * Straight forward bitmap tag implementation, where each bit is a tag
 * (cleared == free, and set == busy). The small twist is using per-cpu
 * last_tag caches, which blk-mq stores in the blk_mq_ctx software queue
 * contexts. This enables us to drastically limit the space searched,
 * without dirtying an extra shared cacheline like we would if we stored
 * the cache value inside the shared blk_mq_bitmap_tags structure. On top
 * of that, each word of tags is in a separate cacheline. This means that
 * multiple users will tend to stick to different cachelines, at least
 * until the map is exhausted.
 */
static int __bt_get(struct blk_mq_hw_ctx *hctx, struct blk_mq_bitmap_tags *bt,
		    unsigned int *tag_cache)
{
	unsigned int last_tag, org_last_tag;
	int index, i, tag;

	if (!hctx_may_queue(hctx, bt))
		return -1;

	last_tag = org_last_tag = *tag_cache;
	index = TAG_TO_INDEX(bt, last_tag);

	for (i = 0; i < bt->map_nr; i++) {
		tag = __bt_get_word(&bt->map[index], TAG_TO_BIT(bt, last_tag));
		if (tag != -1) {
			tag += (index << bt->bits_per_word);
			goto done;
		}

		last_tag = 0;
		if (++index >= bt->map_nr)
			index = 0;
	}

	*tag_cache = 0;
	return -1;

	/*
	 * Only update the cache from the allocation path, if we ended
	 * up using the specific cached tag.
	 */
done:
	if (tag == org_last_tag) {
		last_tag = tag + 1;
		if (last_tag >= bt->depth - 1)
			last_tag = 0;

		*tag_cache = last_tag;
	}

	return tag;
}

static struct bt_wait_state *bt_wait_ptr(struct blk_mq_bitmap_tags *bt,
					 struct blk_mq_hw_ctx *hctx)
{
	struct bt_wait_state *bs;
	int wait_index;

	if (!hctx)
		return &bt->bs[0];

	wait_index = atomic_read(&hctx->wait_index);
	bs = &bt->bs[wait_index];
	bt_index_atomic_inc(&hctx->wait_index);
	return bs;
}

static int bt_get(struct blk_mq_alloc_data *data,
		struct blk_mq_bitmap_tags *bt,
		struct blk_mq_hw_ctx *hctx,
		unsigned int *last_tag)
{
	struct bt_wait_state *bs;
	DEFINE_WAIT(wait);
	int tag;

	tag = __bt_get(hctx, bt, last_tag);
	if (tag != -1)
		return tag;

	if (!(data->gfp & __GFP_WAIT))
		return -1;

	bs = bt_wait_ptr(bt, hctx);
	do {
		prepare_to_wait(&bs->wait, &wait, TASK_UNINTERRUPTIBLE);

		tag = __bt_get(hctx, bt, last_tag);
		if (tag != -1)
			break;

		blk_mq_put_ctx(data->ctx);

		io_schedule();

		data->ctx = blk_mq_get_ctx(data->q);
		data->hctx = data->q->mq_ops->map_queue(data->q,
				data->ctx->cpu);
		if (data->reserved) {
			bt = &data->hctx->tags->breserved_tags;
		} else {
			last_tag = &data->ctx->last_tag;
			hctx = data->hctx;
			bt = &hctx->tags->bitmap_tags;
		}
		finish_wait(&bs->wait, &wait);
		bs = bt_wait_ptr(bt, hctx);
	} while (1);

	finish_wait(&bs->wait, &wait);
	return tag;
}

static unsigned int __blk_mq_get_tag(struct blk_mq_alloc_data *data)
{
	int tag;

	tag = bt_get(data, &data->hctx->tags->bitmap_tags, data->hctx,
			&data->ctx->last_tag);
	if (tag >= 0)
		return tag + data->hctx->tags->nr_reserved_tags;

	return BLK_MQ_TAG_FAIL;
}

static unsigned int __blk_mq_get_reserved_tag(struct blk_mq_alloc_data *data)
{
	int tag, zero = 0;

	if (unlikely(!data->hctx->tags->nr_reserved_tags)) {
		WARN_ON_ONCE(1);
		return BLK_MQ_TAG_FAIL;
	}

	tag = bt_get(data, &data->hctx->tags->breserved_tags, NULL, &zero);
	if (tag < 0)
		return BLK_MQ_TAG_FAIL;

	return tag;
}

unsigned int blk_mq_get_tag(struct blk_mq_alloc_data *data)
{
	if (!data->reserved)
		return __blk_mq_get_tag(data);

	return __blk_mq_get_reserved_tag(data);
}

static struct bt_wait_state *bt_wake_ptr(struct blk_mq_bitmap_tags *bt)
{
	int i, wake_index;

	wake_index = atomic_read(&bt->wake_index);
	for (i = 0; i < BT_WAIT_QUEUES; i++) {
		struct bt_wait_state *bs = &bt->bs[wake_index];

		if (waitqueue_active(&bs->wait)) {
			int o = atomic_read(&bt->wake_index);
			if (wake_index != o)
				atomic_cmpxchg(&bt->wake_index, o, wake_index);

			return bs;
		}

		wake_index = bt_index_inc(wake_index);
	}

	return NULL;
}

static void bt_clear_tag(struct blk_mq_bitmap_tags *bt, unsigned int tag)
{
	const int index = TAG_TO_INDEX(bt, tag);
	struct bt_wait_state *bs;
	int wait_cnt;

	/*
	 * The unlock memory barrier need to order access to req in free
	 * path and clearing tag bit
	 */
	clear_bit_unlock(TAG_TO_BIT(bt, tag), &bt->map[index].word);

	bs = bt_wake_ptr(bt);
	if (!bs)
		return;

	wait_cnt = atomic_dec_return(&bs->wait_cnt);
	if (unlikely(wait_cnt < 0))
		wait_cnt = atomic_inc_return(&bs->wait_cnt);
	if (wait_cnt == 0) {
		atomic_add(bt->wake_cnt, &bs->wait_cnt);
		bt_index_atomic_inc(&bt->wake_index);
		wake_up(&bs->wait);
	}
}

static void __blk_mq_put_tag(struct blk_mq_tags *tags, unsigned int tag)
{
	BUG_ON(tag >= tags->nr_tags);

	bt_clear_tag(&tags->bitmap_tags, tag);
}

static void __blk_mq_put_reserved_tag(struct blk_mq_tags *tags,
				      unsigned int tag)
{
	BUG_ON(tag >= tags->nr_reserved_tags);

	bt_clear_tag(&tags->breserved_tags, tag);
}

void blk_mq_put_tag(struct blk_mq_hw_ctx *hctx, unsigned int tag,
		    unsigned int *last_tag)
{
	struct blk_mq_tags *tags = hctx->tags;

	if (tag >= tags->nr_reserved_tags) {
		const int real_tag = tag - tags->nr_reserved_tags;

		__blk_mq_put_tag(tags, real_tag);
		*last_tag = real_tag;
	} else
		__blk_mq_put_reserved_tag(tags, tag);
}

static void bt_for_each(struct blk_mq_hw_ctx *hctx,
		struct blk_mq_bitmap_tags *bt, unsigned int off,
		busy_iter_fn *fn, void *data, bool reserved)
{
	struct request *rq;
	int bit, i;

	for (i = 0; i < bt->map_nr; i++) {
		struct blk_align_bitmap *bm = &bt->map[i];

		for (bit = find_first_bit(&bm->word, bm->depth);
		     bit < bm->depth;
		     bit = find_next_bit(&bm->word, bm->depth, bit + 1)) {
		     	rq = blk_mq_tag_to_rq(hctx->tags, off + bit);
			if (rq->q == hctx->queue)
				fn(hctx, rq, data, reserved);
		}

		off += (1 << bt->bits_per_word);
	}
}

void blk_mq_tag_busy_iter(struct blk_mq_hw_ctx *hctx, busy_iter_fn *fn,
		void *priv)
{
	struct blk_mq_tags *tags = hctx->tags;

	if (tags->nr_reserved_tags)
		bt_for_each(hctx, &tags->breserved_tags, 0, fn, priv, true);
	bt_for_each(hctx, &tags->bitmap_tags, tags->nr_reserved_tags, fn, priv,
			false);
}
EXPORT_SYMBOL(blk_mq_tag_busy_iter);

static unsigned int bt_unused_tags(struct blk_mq_bitmap_tags *bt)
{
	unsigned int i, used;

	for (i = 0, used = 0; i < bt->map_nr; i++) {
		struct blk_align_bitmap *bm = &bt->map[i];

		used += bitmap_weight(&bm->word, bm->depth);
	}

	return bt->depth - used;
}

static void bt_update_count(struct blk_mq_bitmap_tags *bt,
			    unsigned int depth)
{
	unsigned int tags_per_word = 1U << bt->bits_per_word;
	unsigned int map_depth = depth;

	if (depth) {
		int i;

		for (i = 0; i < bt->map_nr; i++) {
			bt->map[i].depth = min(map_depth, tags_per_word);
			map_depth -= bt->map[i].depth;
		}
	}

	bt->wake_cnt = BT_WAIT_BATCH;
	if (bt->wake_cnt > depth / BT_WAIT_QUEUES)
		bt->wake_cnt = max(1U, depth / BT_WAIT_QUEUES);

	bt->depth = depth;
}

static int bt_alloc(struct blk_mq_bitmap_tags *bt, unsigned int depth,
			int node, bool reserved)
{
	int i;

	bt->bits_per_word = ilog2(BITS_PER_LONG);

	/*
	 * Depth can be zero for reserved tags, that's not a failure
	 * condition.
	 */
	if (depth) {
		unsigned int nr, tags_per_word;

		tags_per_word = (1 << bt->bits_per_word);

		/*
		 * If the tag space is small, shrink the number of tags
		 * per word so we spread over a few cachelines, at least.
		 * If less than 4 tags, just forget about it, it's not
		 * going to work optimally anyway.
		 */
		if (depth >= 4) {
			while (tags_per_word * 4 > depth) {
				bt->bits_per_word--;
				tags_per_word = (1 << bt->bits_per_word);
			}
		}

		nr = ALIGN(depth, tags_per_word) / tags_per_word;
		bt->map = kzalloc_node(nr * sizeof(struct blk_align_bitmap),
						GFP_KERNEL, node);
		if (!bt->map)
			return -ENOMEM;

		bt->map_nr = nr;
	}

	bt->bs = kzalloc(BT_WAIT_QUEUES * sizeof(*bt->bs), GFP_KERNEL);
	if (!bt->bs) {
		kfree(bt->map);
		return -ENOMEM;
	}

	bt_update_count(bt, depth);

	for (i = 0; i < BT_WAIT_QUEUES; i++) {
		init_waitqueue_head(&bt->bs[i].wait);
		atomic_set(&bt->bs[i].wait_cnt, bt->wake_cnt);
	}

	return 0;
}

static void bt_free(struct blk_mq_bitmap_tags *bt)
{
	kfree(bt->map);
	kfree(bt->bs);
}

static struct blk_mq_tags *blk_mq_init_bitmap_tags(struct blk_mq_tags *tags,
						   int node)
{
	unsigned int depth = tags->nr_tags - tags->nr_reserved_tags;

	if (bt_alloc(&tags->bitmap_tags, depth, node, false))
		goto enomem;
	if (bt_alloc(&tags->breserved_tags, tags->nr_reserved_tags, node, true))
		goto enomem;

	return tags;
enomem:
	bt_free(&tags->bitmap_tags);
	kfree(tags);
	return NULL;
}

struct blk_mq_tags *blk_mq_init_tags(unsigned int total_tags,
				     unsigned int reserved_tags, int node)
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

	return blk_mq_init_bitmap_tags(tags, node);
}

void blk_mq_free_tags(struct blk_mq_tags *tags)
{
	bt_free(&tags->bitmap_tags);
	bt_free(&tags->breserved_tags);
	kfree(tags);
}

void blk_mq_tag_init_last_tag(struct blk_mq_tags *tags, unsigned int *tag)
{
	unsigned int depth = tags->nr_tags - tags->nr_reserved_tags;

	*tag = prandom_u32() % depth;
}

int blk_mq_tag_update_depth(struct blk_mq_tags *tags, unsigned int tdepth)
{
	tdepth -= tags->nr_reserved_tags;
	if (tdepth > tags->nr_tags)
		return -EINVAL;

	/*
	 * Don't need (or can't) update reserved tags here, they remain
	 * static and should never need resizing.
	 */
	bt_update_count(&tags->bitmap_tags, tdepth);
	blk_mq_tag_wakeup_all(tags);
	return 0;
}

ssize_t blk_mq_tag_sysfs_show(struct blk_mq_tags *tags, char *page)
{
	char *orig_page = page;
	unsigned int free, res;

	if (!tags)
		return 0;

	page += sprintf(page, "nr_tags=%u, reserved_tags=%u, "
			"bits_per_word=%u\n",
			tags->nr_tags, tags->nr_reserved_tags,
			tags->bitmap_tags.bits_per_word);

	free = bt_unused_tags(&tags->bitmap_tags);
	res = bt_unused_tags(&tags->breserved_tags);

	page += sprintf(page, "nr_free=%u, nr_reserved=%u\n", free, res);
	page += sprintf(page, "active_queues=%u\n", atomic_read(&tags->active_queues));

	return page - orig_page;
}
