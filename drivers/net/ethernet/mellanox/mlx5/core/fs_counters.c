/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/mlx5/driver.h>
#include <linux/mlx5/fs.h>
#include "mlx5_core.h"
#include "fs_core.h"
#include "fs_pool.h"
#include "fs_cmd.h"

#define MLX5_FC_STATS_PERIOD msecs_to_jiffies(1000)
/* Max number of counters to query in bulk read is 32K */
#define MLX5_SW_MAX_COUNTERS_BULK BIT(15)
#define MLX5_INIT_COUNTERS_BULK 8
#define MLX5_FC_POOL_MAX_THRESHOLD BIT(18)
#define MLX5_FC_POOL_USED_BUFF_RATIO 10

struct mlx5_fc_stats {
	struct xarray counters;

	struct workqueue_struct *wq;
	struct delayed_work work;
	unsigned long sampling_interval; /* jiffies */
	u32 *bulk_query_out;
	int bulk_query_len;
	bool bulk_query_alloc_failed;
	unsigned long next_bulk_query_alloc;
	struct mlx5_fs_pool fc_pool;
};

static void mlx5_fc_pool_init(struct mlx5_fs_pool *fc_pool, struct mlx5_core_dev *dev);
static void mlx5_fc_pool_cleanup(struct mlx5_fs_pool *fc_pool);
static struct mlx5_fc *mlx5_fc_pool_acquire_counter(struct mlx5_fs_pool *fc_pool);
static void mlx5_fc_pool_release_counter(struct mlx5_fs_pool *fc_pool, struct mlx5_fc *fc);

static int get_init_bulk_query_len(struct mlx5_core_dev *dev)
{
	return min_t(int, MLX5_INIT_COUNTERS_BULK,
		     (1 << MLX5_CAP_GEN(dev, log_max_flow_counter_bulk)));
}

static int get_max_bulk_query_len(struct mlx5_core_dev *dev)
{
	return min_t(int, MLX5_SW_MAX_COUNTERS_BULK,
		     (1 << MLX5_CAP_GEN(dev, log_max_flow_counter_bulk)));
}

static void update_counter_cache(int index, u32 *bulk_raw_data,
				 struct mlx5_fc_cache *cache)
{
	void *stats = MLX5_ADDR_OF(query_flow_counter_out, bulk_raw_data,
			     flow_statistics[index]);
	u64 packets = MLX5_GET64(traffic_counter, stats, packets);
	u64 bytes = MLX5_GET64(traffic_counter, stats, octets);

	if (cache->packets == packets)
		return;

	cache->packets = packets;
	cache->bytes = bytes;
	cache->lastuse = jiffies;
}

/* Synchronization notes
 *
 * Access to counter array:
 * - create - mlx5_fc_create() (user context)
 *   - inserts the counter into the xarray.
 *
 * - destroy - mlx5_fc_destroy() (user context)
 *   - erases the counter from the xarray and releases it.
 *
 * - query mlx5_fc_query(), mlx5_fc_query_cached{,_raw}() (user context)
 *   - user should not access a counter after destroy.
 *
 * - bulk query (single thread workqueue context)
 *   - create: query relies on 'lastuse' to avoid updating counters added
 *             around the same time as the current bulk cmd.
 *   - destroy: destroyed counters will not be accessed, even if they are
 *              destroyed during a bulk query command.
 */
static void mlx5_fc_stats_query_all_counters(struct mlx5_core_dev *dev)
{
	struct mlx5_fc_stats *fc_stats = dev->priv.fc_stats;
	u32 bulk_len = fc_stats->bulk_query_len;
	XA_STATE(xas, &fc_stats->counters, 0);
	u32 *data = fc_stats->bulk_query_out;
	struct mlx5_fc *counter;
	u32 last_bulk_id = 0;
	u64 bulk_query_time;
	u32 bulk_base_id;
	int err;

	xas_lock(&xas);
	xas_for_each(&xas, counter, U32_MAX) {
		if (xas_retry(&xas, counter))
			continue;
		if (unlikely(counter->id >= last_bulk_id)) {
			/* Start new bulk query. */
			/* First id must be aligned to 4 when using bulk query. */
			bulk_base_id = counter->id & ~0x3;
			last_bulk_id = bulk_base_id + bulk_len;
			/* The lock is released while querying the hw and reacquired after. */
			xas_unlock(&xas);
			/* The same id needs to be processed again in the next loop iteration. */
			xas_reset(&xas);
			bulk_query_time = jiffies;
			err = mlx5_cmd_fc_bulk_query(dev, bulk_base_id, bulk_len, data);
			if (err) {
				mlx5_core_err(dev, "Error doing bulk query: %d\n", err);
				return;
			}
			xas_lock(&xas);
			continue;
		}
		/* Do not update counters added after bulk query was started. */
		if (time_after64(bulk_query_time, counter->cache.lastuse))
			update_counter_cache(counter->id - bulk_base_id, data,
					     &counter->cache);
	}
	xas_unlock(&xas);
}

static void mlx5_fc_free(struct mlx5_core_dev *dev, struct mlx5_fc *counter)
{
	mlx5_cmd_fc_free(dev, counter->id);
	kfree(counter);
}

static void mlx5_fc_release(struct mlx5_core_dev *dev, struct mlx5_fc *counter)
{
	struct mlx5_fc_stats *fc_stats = dev->priv.fc_stats;

	if (WARN_ON(counter->type == MLX5_FC_TYPE_LOCAL))
		return;

	if (counter->bulk)
		mlx5_fc_pool_release_counter(&fc_stats->fc_pool, counter);
	else
		mlx5_fc_free(dev, counter);
}

static void mlx5_fc_stats_bulk_query_buf_realloc(struct mlx5_core_dev *dev,
						 int bulk_query_len)
{
	struct mlx5_fc_stats *fc_stats = dev->priv.fc_stats;
	u32 *bulk_query_out_tmp;
	int out_len;

	out_len = mlx5_cmd_fc_get_bulk_query_out_len(bulk_query_len);
	bulk_query_out_tmp = kvzalloc(out_len, GFP_KERNEL);
	if (!bulk_query_out_tmp) {
		mlx5_core_warn_once(dev,
				    "Can't increase flow counters bulk query buffer size, alloc failed, bulk_query_len(%d)\n",
				    bulk_query_len);
		return;
	}

	kvfree(fc_stats->bulk_query_out);
	fc_stats->bulk_query_out = bulk_query_out_tmp;
	fc_stats->bulk_query_len = bulk_query_len;
	mlx5_core_info(dev,
		       "Flow counters bulk query buffer size increased, bulk_query_len(%d)\n",
		       bulk_query_len);
}

static int mlx5_fc_num_counters(struct mlx5_fc_stats *fc_stats)
{
	struct mlx5_fc *counter;
	int num_counters = 0;
	unsigned long id;

	xa_for_each(&fc_stats->counters, id, counter)
		num_counters++;
	return num_counters;
}

static void mlx5_fc_stats_work(struct work_struct *work)
{
	struct mlx5_fc_stats *fc_stats = container_of(work, struct mlx5_fc_stats,
						      work.work);
	struct mlx5_core_dev *dev = fc_stats->fc_pool.dev;

	queue_delayed_work(fc_stats->wq, &fc_stats->work, fc_stats->sampling_interval);

	/* Grow the bulk query buffer to max if not maxed and enough counters are present. */
	if (unlikely(fc_stats->bulk_query_len < get_max_bulk_query_len(dev) &&
		     mlx5_fc_num_counters(fc_stats) > get_init_bulk_query_len(dev)))
		mlx5_fc_stats_bulk_query_buf_realloc(dev, get_max_bulk_query_len(dev));

	mlx5_fc_stats_query_all_counters(dev);
}

static struct mlx5_fc *mlx5_fc_single_alloc(struct mlx5_core_dev *dev)
{
	struct mlx5_fc *counter;
	int err;

	counter = kzalloc(sizeof(*counter), GFP_KERNEL);
	if (!counter)
		return ERR_PTR(-ENOMEM);

	err = mlx5_cmd_fc_alloc(dev, &counter->id);
	if (err) {
		kfree(counter);
		return ERR_PTR(err);
	}

	return counter;
}

static struct mlx5_fc *mlx5_fc_acquire(struct mlx5_core_dev *dev, bool aging)
{
	struct mlx5_fc_stats *fc_stats = dev->priv.fc_stats;
	struct mlx5_fc *counter;

	if (aging && MLX5_CAP_GEN(dev, flow_counter_bulk_alloc) != 0) {
		counter = mlx5_fc_pool_acquire_counter(&fc_stats->fc_pool);
		if (!IS_ERR(counter))
			return counter;
	}

	return mlx5_fc_single_alloc(dev);
}

struct mlx5_fc *mlx5_fc_create(struct mlx5_core_dev *dev, bool aging)
{
	struct mlx5_fc *counter = mlx5_fc_acquire(dev, aging);
	struct mlx5_fc_stats *fc_stats = dev->priv.fc_stats;
	int err;

	if (IS_ERR(counter))
		return counter;

	counter->aging = aging;

	if (aging) {
		u32 id = counter->id;

		counter->cache.lastuse = jiffies;
		counter->lastbytes = counter->cache.bytes;
		counter->lastpackets = counter->cache.packets;

		err = xa_err(xa_store(&fc_stats->counters, id, counter, GFP_KERNEL));
		if (err != 0)
			goto err_out_alloc;
	}

	return counter;

err_out_alloc:
	mlx5_fc_release(dev, counter);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(mlx5_fc_create);

u32 mlx5_fc_id(struct mlx5_fc *counter)
{
	return counter->id;
}
EXPORT_SYMBOL(mlx5_fc_id);

void mlx5_fc_destroy(struct mlx5_core_dev *dev, struct mlx5_fc *counter)
{
	struct mlx5_fc_stats *fc_stats = dev->priv.fc_stats;

	if (!counter)
		return;

	if (counter->aging)
		xa_erase(&fc_stats->counters, counter->id);
	mlx5_fc_release(dev, counter);
}
EXPORT_SYMBOL(mlx5_fc_destroy);

int mlx5_init_fc_stats(struct mlx5_core_dev *dev)
{
	struct mlx5_fc_stats *fc_stats;

	fc_stats = kzalloc(sizeof(*fc_stats), GFP_KERNEL);
	if (!fc_stats)
		return -ENOMEM;
	dev->priv.fc_stats = fc_stats;

	xa_init(&fc_stats->counters);

	/* Allocate initial (small) bulk query buffer. */
	mlx5_fc_stats_bulk_query_buf_realloc(dev, get_init_bulk_query_len(dev));
	if (!fc_stats->bulk_query_out)
		goto err_bulk;

	fc_stats->wq = create_singlethread_workqueue("mlx5_fc");
	if (!fc_stats->wq)
		goto err_wq_create;

	fc_stats->sampling_interval = MLX5_FC_STATS_PERIOD;
	INIT_DELAYED_WORK(&fc_stats->work, mlx5_fc_stats_work);

	mlx5_fc_pool_init(&fc_stats->fc_pool, dev);
	queue_delayed_work(fc_stats->wq, &fc_stats->work, MLX5_FC_STATS_PERIOD);
	return 0;

err_wq_create:
	kvfree(fc_stats->bulk_query_out);
err_bulk:
	kfree(fc_stats);
	return -ENOMEM;
}

void mlx5_cleanup_fc_stats(struct mlx5_core_dev *dev)
{
	struct mlx5_fc_stats *fc_stats = dev->priv.fc_stats;
	struct mlx5_fc *counter;
	unsigned long id;

	cancel_delayed_work_sync(&fc_stats->work);
	destroy_workqueue(fc_stats->wq);
	fc_stats->wq = NULL;

	xa_for_each(&fc_stats->counters, id, counter) {
		xa_erase(&fc_stats->counters, id);
		mlx5_fc_release(dev, counter);
	}
	xa_destroy(&fc_stats->counters);

	mlx5_fc_pool_cleanup(&fc_stats->fc_pool);
	kvfree(fc_stats->bulk_query_out);
	kfree(fc_stats);
}

int mlx5_fc_query(struct mlx5_core_dev *dev, struct mlx5_fc *counter,
		  u64 *packets, u64 *bytes)
{
	return mlx5_cmd_fc_query(dev, counter->id, packets, bytes);
}
EXPORT_SYMBOL(mlx5_fc_query);

u64 mlx5_fc_query_lastuse(struct mlx5_fc *counter)
{
	return counter->cache.lastuse;
}

void mlx5_fc_query_cached(struct mlx5_fc *counter,
			  u64 *bytes, u64 *packets, u64 *lastuse)
{
	struct mlx5_fc_cache c;

	c = counter->cache;

	*bytes = c.bytes - counter->lastbytes;
	*packets = c.packets - counter->lastpackets;
	*lastuse = c.lastuse;

	counter->lastbytes = c.bytes;
	counter->lastpackets = c.packets;
}

void mlx5_fc_query_cached_raw(struct mlx5_fc *counter,
			      u64 *bytes, u64 *packets, u64 *lastuse)
{
	struct mlx5_fc_cache c = counter->cache;

	*bytes = c.bytes;
	*packets = c.packets;
	*lastuse = c.lastuse;
}

void mlx5_fc_queue_stats_work(struct mlx5_core_dev *dev,
			      struct delayed_work *dwork,
			      unsigned long delay)
{
	struct mlx5_fc_stats *fc_stats = dev->priv.fc_stats;

	queue_delayed_work(fc_stats->wq, dwork, delay);
}

void mlx5_fc_update_sampling_interval(struct mlx5_core_dev *dev,
				      unsigned long interval)
{
	struct mlx5_fc_stats *fc_stats = dev->priv.fc_stats;

	fc_stats->sampling_interval = min_t(unsigned long, interval,
					    fc_stats->sampling_interval);
}

/* Flow counter bulks */

static void mlx5_fc_init(struct mlx5_fc *counter, struct mlx5_fc_bulk *bulk,
			 u32 id)
{
	counter->bulk = bulk;
	counter->id = id;
}

u32 mlx5_fc_get_base_id(struct mlx5_fc *counter)
{
	return counter->bulk->base_id;
}

static struct mlx5_fs_bulk *mlx5_fc_bulk_create(struct mlx5_core_dev *dev,
						void *pool_ctx)
{
	enum mlx5_fc_bulk_alloc_bitmask alloc_bitmask;
	struct mlx5_fc_bulk *fc_bulk;
	int bulk_len;
	u32 base_id;
	int i;

	alloc_bitmask = MLX5_CAP_GEN(dev, flow_counter_bulk_alloc);
	bulk_len = alloc_bitmask > 0 ? MLX5_FC_BULK_NUM_FCS(alloc_bitmask) : 1;

	fc_bulk = kvzalloc(struct_size(fc_bulk, fcs, bulk_len), GFP_KERNEL);
	if (!fc_bulk)
		return NULL;

	if (mlx5_fs_bulk_init(dev, &fc_bulk->fs_bulk, bulk_len))
		goto fc_bulk_free;

	if (mlx5_cmd_fc_bulk_alloc(dev, alloc_bitmask, &base_id))
		goto fs_bulk_cleanup;
	fc_bulk->base_id = base_id;
	for (i = 0; i < bulk_len; i++)
		mlx5_fc_init(&fc_bulk->fcs[i], fc_bulk, base_id + i);

	refcount_set(&fc_bulk->hws_data.hws_action_refcount, 0);
	mutex_init(&fc_bulk->hws_data.lock);
	return &fc_bulk->fs_bulk;

fs_bulk_cleanup:
	mlx5_fs_bulk_cleanup(&fc_bulk->fs_bulk);
fc_bulk_free:
	kvfree(fc_bulk);
	return NULL;
}

static int
mlx5_fc_bulk_destroy(struct mlx5_core_dev *dev, struct mlx5_fs_bulk *fs_bulk)
{
	struct mlx5_fc_bulk *fc_bulk = container_of(fs_bulk,
						    struct mlx5_fc_bulk,
						    fs_bulk);

	if (mlx5_fs_bulk_get_free_amount(fs_bulk) < fs_bulk->bulk_len) {
		mlx5_core_err(dev, "Freeing bulk before all counters were released\n");
		return -EBUSY;
	}

	mlx5_cmd_fc_free(dev, fc_bulk->base_id);
	mlx5_fs_bulk_cleanup(fs_bulk);
	kvfree(fc_bulk);

	return 0;
}

static void mlx5_fc_pool_update_threshold(struct mlx5_fs_pool *fc_pool)
{
	fc_pool->threshold = min_t(int, MLX5_FC_POOL_MAX_THRESHOLD,
				   fc_pool->used_units / MLX5_FC_POOL_USED_BUFF_RATIO);
}

/* Flow counters pool API */

static const struct mlx5_fs_pool_ops mlx5_fc_pool_ops = {
	.bulk_destroy = mlx5_fc_bulk_destroy,
	.bulk_create = mlx5_fc_bulk_create,
	.update_threshold = mlx5_fc_pool_update_threshold,
};

static void
mlx5_fc_pool_init(struct mlx5_fs_pool *fc_pool, struct mlx5_core_dev *dev)
{
	mlx5_fs_pool_init(fc_pool, dev, &mlx5_fc_pool_ops, NULL);
}

static void mlx5_fc_pool_cleanup(struct mlx5_fs_pool *fc_pool)
{
	mlx5_fs_pool_cleanup(fc_pool);
}

static struct mlx5_fc *
mlx5_fc_pool_acquire_counter(struct mlx5_fs_pool *fc_pool)
{
	struct mlx5_fs_pool_index pool_index = {};
	struct mlx5_fc_bulk *fc_bulk;
	int err;

	err = mlx5_fs_pool_acquire_index(fc_pool, &pool_index);
	if (err)
		return ERR_PTR(err);
	fc_bulk = container_of(pool_index.fs_bulk, struct mlx5_fc_bulk, fs_bulk);
	return &fc_bulk->fcs[pool_index.index];
}

static void
mlx5_fc_pool_release_counter(struct mlx5_fs_pool *fc_pool, struct mlx5_fc *fc)
{
	struct mlx5_fs_bulk *fs_bulk = &fc->bulk->fs_bulk;
	struct mlx5_fs_pool_index pool_index = {};
	struct mlx5_core_dev *dev = fc_pool->dev;

	pool_index.fs_bulk = fs_bulk;
	pool_index.index = fc->id - fc->bulk->base_id;
	if (mlx5_fs_pool_release_index(fc_pool, &pool_index))
		mlx5_core_warn(dev, "Attempted to release a counter which is not acquired\n");
}

/**
 * mlx5_fc_local_create - Allocate mlx5_fc struct for a counter which
 * was already acquired using its counter id and bulk data.
 *
 * @counter_id: counter acquired counter id
 * @offset: counter offset from bulk base
 * @bulk_size: counter's bulk size as was allocated
 *
 * Return: Pointer to mlx5_fc on success, ERR_PTR otherwise.
 */
struct mlx5_fc *
mlx5_fc_local_create(u32 counter_id, u32 offset, u32 bulk_size)
{
	struct mlx5_fc_bulk *fc_bulk;
	struct mlx5_fc *counter;

	counter = kzalloc(sizeof(*counter), GFP_KERNEL);
	if (!counter)
		return ERR_PTR(-ENOMEM);
	fc_bulk = kzalloc(sizeof(*fc_bulk), GFP_KERNEL);
	if (!fc_bulk) {
		kfree(counter);
		return ERR_PTR(-ENOMEM);
	}

	counter->type = MLX5_FC_TYPE_LOCAL;
	counter->id = counter_id;
	fc_bulk->base_id = counter_id - offset;
	fc_bulk->fs_bulk.bulk_len = bulk_size;
	refcount_set(&fc_bulk->hws_data.hws_action_refcount, 0);
	mutex_init(&fc_bulk->hws_data.lock);
	counter->bulk = fc_bulk;
	refcount_set(&counter->fc_local_refcount, 1);
	return counter;
}
EXPORT_SYMBOL(mlx5_fc_local_create);

void mlx5_fc_local_destroy(struct mlx5_fc *counter)
{
	kfree(counter->bulk);
	kfree(counter);
}
EXPORT_SYMBOL(mlx5_fc_local_destroy);

void mlx5_fc_local_get(struct mlx5_fc *counter)
{
	if (!counter || counter->type != MLX5_FC_TYPE_LOCAL)
		return;

	refcount_inc(&counter->fc_local_refcount);
}

void mlx5_fc_local_put(struct mlx5_fc *counter)
{
	if (!counter || counter->type != MLX5_FC_TYPE_LOCAL)
		return;

	if (!refcount_dec_and_test(&counter->fc_local_refcount))
		return;

	mlx5_fc_local_destroy(counter);
}
