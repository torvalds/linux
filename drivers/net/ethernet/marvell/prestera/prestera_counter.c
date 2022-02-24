// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2021 Marvell International Ltd. All rights reserved */

#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_acl.h"
#include "prestera_counter.h"

#define COUNTER_POLL_TIME	(msecs_to_jiffies(1000))
#define COUNTER_RESCHED_TIME	(msecs_to_jiffies(50))
#define COUNTER_BULK_SIZE	(256)

struct prestera_counter {
	struct prestera_switch *sw;
	struct delayed_work stats_dw;
	struct mutex mtx;  /* protect block_list */
	struct prestera_counter_block **block_list;
	u32 total_read;
	u32 block_list_len;
	u32 curr_idx;
	bool is_fetching;
};

struct prestera_counter_block {
	struct list_head list;
	u32 id;
	u32 offset;
	u32 num_counters;
	u32 client;
	struct idr counter_idr;
	refcount_t refcnt;
	struct mutex mtx;  /* protect stats and counter_idr */
	struct prestera_counter_stats *stats;
	u8 *counter_flag;
	bool is_updating;
	bool full;
};

enum {
	COUNTER_FLAG_READY = 0,
	COUNTER_FLAG_INVALID = 1
};

static bool
prestera_counter_is_ready(struct prestera_counter_block *block, u32 id)
{
	return block->counter_flag[id - block->offset] == COUNTER_FLAG_READY;
}

static void prestera_counter_lock(struct prestera_counter *counter)
{
	mutex_lock(&counter->mtx);
}

static void prestera_counter_unlock(struct prestera_counter *counter)
{
	mutex_unlock(&counter->mtx);
}

static void prestera_counter_block_lock(struct prestera_counter_block *block)
{
	mutex_lock(&block->mtx);
}

static void prestera_counter_block_unlock(struct prestera_counter_block *block)
{
	mutex_unlock(&block->mtx);
}

static bool prestera_counter_block_incref(struct prestera_counter_block *block)
{
	return refcount_inc_not_zero(&block->refcnt);
}

static bool prestera_counter_block_decref(struct prestera_counter_block *block)
{
	return refcount_dec_and_test(&block->refcnt);
}

/* must be called with prestera_counter_block_lock() */
static void prestera_counter_stats_clear(struct prestera_counter_block *block,
					 u32 counter_id)
{
	memset(&block->stats[counter_id - block->offset], 0,
	       sizeof(*block->stats));
}

static struct prestera_counter_block *
prestera_counter_block_lookup_not_full(struct prestera_counter *counter,
				       u32 client)
{
	u32 i;

	prestera_counter_lock(counter);
	for (i = 0; i < counter->block_list_len; i++) {
		if (counter->block_list[i] &&
		    counter->block_list[i]->client == client &&
		    !counter->block_list[i]->full &&
		    prestera_counter_block_incref(counter->block_list[i])) {
			prestera_counter_unlock(counter);
			return counter->block_list[i];
		}
	}
	prestera_counter_unlock(counter);

	return NULL;
}

static int prestera_counter_block_list_add(struct prestera_counter *counter,
					   struct prestera_counter_block *block)
{
	struct prestera_counter_block **arr;
	u32 i;

	prestera_counter_lock(counter);

	for (i = 0; i < counter->block_list_len; i++) {
		if (counter->block_list[i])
			continue;

		counter->block_list[i] = block;
		prestera_counter_unlock(counter);
		return 0;
	}

	arr = krealloc(counter->block_list, (counter->block_list_len + 1) *
		       sizeof(*counter->block_list), GFP_KERNEL);
	if (!arr) {
		prestera_counter_unlock(counter);
		return -ENOMEM;
	}

	counter->block_list = arr;
	counter->block_list[counter->block_list_len] = block;
	counter->block_list_len++;
	prestera_counter_unlock(counter);
	return 0;
}

static struct prestera_counter_block *
prestera_counter_block_get(struct prestera_counter *counter, u32 client)
{
	struct prestera_counter_block *block;
	int err;

	block = prestera_counter_block_lookup_not_full(counter, client);
	if (block)
		return block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return ERR_PTR(-ENOMEM);

	err = prestera_hw_counter_block_get(counter->sw, client,
					    &block->id, &block->offset,
					    &block->num_counters);
	if (err)
		goto err_block;

	block->stats = kcalloc(block->num_counters,
			       sizeof(*block->stats), GFP_KERNEL);
	if (!block->stats) {
		err = -ENOMEM;
		goto err_stats;
	}

	block->counter_flag = kcalloc(block->num_counters,
				      sizeof(*block->counter_flag),
				      GFP_KERNEL);
	if (!block->counter_flag) {
		err = -ENOMEM;
		goto err_flag;
	}

	block->client = client;
	mutex_init(&block->mtx);
	refcount_set(&block->refcnt, 1);
	idr_init_base(&block->counter_idr, block->offset);

	err = prestera_counter_block_list_add(counter, block);
	if (err)
		goto err_list_add;

	return block;

err_list_add:
	idr_destroy(&block->counter_idr);
	mutex_destroy(&block->mtx);
	kfree(block->counter_flag);
err_flag:
	kfree(block->stats);
err_stats:
	prestera_hw_counter_block_release(counter->sw, block->id);
err_block:
	kfree(block);
	return ERR_PTR(err);
}

static void prestera_counter_block_put(struct prestera_counter *counter,
				       struct prestera_counter_block *block)
{
	u32 i;

	if (!prestera_counter_block_decref(block))
		return;

	prestera_counter_lock(counter);
	for (i = 0; i < counter->block_list_len; i++) {
		if (counter->block_list[i] &&
		    counter->block_list[i]->id == block->id) {
			counter->block_list[i] = NULL;
			break;
		}
	}
	prestera_counter_unlock(counter);

	WARN_ON(!idr_is_empty(&block->counter_idr));

	prestera_hw_counter_block_release(counter->sw, block->id);
	idr_destroy(&block->counter_idr);
	mutex_destroy(&block->mtx);
	kfree(block->stats);
	kfree(block);
}

static int prestera_counter_get_vacant(struct prestera_counter_block *block,
				       u32 *id)
{
	int free_id;

	if (block->full)
		return -ENOSPC;

	prestera_counter_block_lock(block);
	free_id = idr_alloc_cyclic(&block->counter_idr, NULL, block->offset,
				   block->offset + block->num_counters,
				   GFP_KERNEL);
	if (free_id < 0) {
		if (free_id == -ENOSPC)
			block->full = true;

		prestera_counter_block_unlock(block);
		return free_id;
	}
	*id = free_id;
	prestera_counter_block_unlock(block);

	return 0;
}

int prestera_counter_get(struct prestera_counter *counter, u32 client,
			 struct prestera_counter_block **bl, u32 *counter_id)
{
	struct prestera_counter_block *block;
	int err;
	u32 id;

get_next_block:
	block = prestera_counter_block_get(counter, client);
	if (IS_ERR(block))
		return PTR_ERR(block);

	err = prestera_counter_get_vacant(block, &id);
	if (err) {
		prestera_counter_block_put(counter, block);

		if (err == -ENOSPC)
			goto get_next_block;

		return err;
	}

	prestera_counter_block_lock(block);
	if (block->is_updating)
		block->counter_flag[id - block->offset] = COUNTER_FLAG_INVALID;
	prestera_counter_block_unlock(block);

	*counter_id = id;
	*bl = block;

	return 0;
}

void prestera_counter_put(struct prestera_counter *counter,
			  struct prestera_counter_block *block, u32 counter_id)
{
	if (!block)
		return;

	prestera_counter_block_lock(block);
	idr_remove(&block->counter_idr, counter_id);
	block->full = false;
	prestera_counter_stats_clear(block, counter_id);
	prestera_counter_block_unlock(block);

	prestera_hw_counter_clear(counter->sw, block->id, counter_id);
	prestera_counter_block_put(counter, block);
}

static u32 prestera_counter_block_idx_next(struct prestera_counter *counter,
					   u32 curr_idx)
{
	u32 idx, i, start = curr_idx + 1;

	prestera_counter_lock(counter);
	for (i = 0; i < counter->block_list_len; i++) {
		idx = (start + i) % counter->block_list_len;
		if (!counter->block_list[idx])
			continue;

		prestera_counter_unlock(counter);
		return idx;
	}
	prestera_counter_unlock(counter);

	return 0;
}

static struct prestera_counter_block *
prestera_counter_block_get_by_idx(struct prestera_counter *counter, u32 idx)
{
	if (idx >= counter->block_list_len)
		return NULL;

	prestera_counter_lock(counter);

	if (!counter->block_list[idx] ||
	    !prestera_counter_block_incref(counter->block_list[idx])) {
		prestera_counter_unlock(counter);
		return NULL;
	}

	prestera_counter_unlock(counter);
	return counter->block_list[idx];
}

static void prestera_counter_stats_work(struct work_struct *work)
{
	struct delayed_work *dl_work =
		container_of(work, struct delayed_work, work);
	struct prestera_counter *counter =
		container_of(dl_work, struct prestera_counter, stats_dw);
	struct prestera_counter_block *block;
	u32 resched_time = COUNTER_POLL_TIME;
	u32 count = COUNTER_BULK_SIZE;
	bool done = false;
	int err;
	u32 i;

	block = prestera_counter_block_get_by_idx(counter, counter->curr_idx);
	if (!block) {
		if (counter->is_fetching)
			goto abort;

		goto next;
	}

	if (!counter->is_fetching) {
		err = prestera_hw_counter_trigger(counter->sw, block->id);
		if (err)
			goto abort;

		prestera_counter_block_lock(block);
		block->is_updating = true;
		prestera_counter_block_unlock(block);

		counter->is_fetching = true;
		counter->total_read = 0;
		resched_time = COUNTER_RESCHED_TIME;
		goto resched;
	}

	prestera_counter_block_lock(block);
	err = prestera_hw_counters_get(counter->sw, counter->total_read,
				       &count, &done,
				       &block->stats[counter->total_read]);
	prestera_counter_block_unlock(block);
	if (err)
		goto abort;

	counter->total_read += count;
	if (!done || counter->total_read < block->num_counters) {
		resched_time = COUNTER_RESCHED_TIME;
		goto resched;
	}

	for (i = 0; i < block->num_counters; i++) {
		if (block->counter_flag[i] == COUNTER_FLAG_INVALID) {
			prestera_counter_block_lock(block);
			block->counter_flag[i] = COUNTER_FLAG_READY;
			memset(&block->stats[i], 0, sizeof(*block->stats));
			prestera_counter_block_unlock(block);
		}
	}

	prestera_counter_block_lock(block);
	block->is_updating = false;
	prestera_counter_block_unlock(block);

	goto next;
abort:
	prestera_hw_counter_abort(counter->sw);
next:
	counter->is_fetching = false;
	counter->curr_idx =
		prestera_counter_block_idx_next(counter, counter->curr_idx);
resched:
	if (block)
		prestera_counter_block_put(counter, block);

	schedule_delayed_work(&counter->stats_dw, resched_time);
}

/* Can be executed without rtnl_lock().
 * So pay attention when something changing.
 */
int prestera_counter_stats_get(struct prestera_counter *counter,
			       struct prestera_counter_block *block,
			       u32 counter_id, u64 *packets, u64 *bytes)
{
	if (!block || !prestera_counter_is_ready(block, counter_id)) {
		*packets = 0;
		*bytes = 0;
		return 0;
	}

	prestera_counter_block_lock(block);
	*packets = block->stats[counter_id - block->offset].packets;
	*bytes = block->stats[counter_id - block->offset].bytes;

	prestera_counter_stats_clear(block, counter_id);
	prestera_counter_block_unlock(block);

	return 0;
}

int prestera_counter_init(struct prestera_switch *sw)
{
	struct prestera_counter *counter;

	counter = kzalloc(sizeof(*counter), GFP_KERNEL);
	if (!counter)
		return -ENOMEM;

	counter->block_list = kzalloc(sizeof(*counter->block_list), GFP_KERNEL);
	if (!counter->block_list) {
		kfree(counter);
		return -ENOMEM;
	}

	mutex_init(&counter->mtx);
	counter->block_list_len = 1;
	counter->sw = sw;
	sw->counter = counter;

	INIT_DELAYED_WORK(&counter->stats_dw, prestera_counter_stats_work);
	schedule_delayed_work(&counter->stats_dw, COUNTER_POLL_TIME);

	return 0;
}

void prestera_counter_fini(struct prestera_switch *sw)
{
	struct prestera_counter *counter = sw->counter;
	u32 i;

	cancel_delayed_work_sync(&counter->stats_dw);

	for (i = 0; i < counter->block_list_len; i++)
		WARN_ON(counter->block_list[i]);

	mutex_destroy(&counter->mtx);
	kfree(counter->block_list);
	kfree(counter);
}
