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
#include <linux/rbtree.h>
#include "mlx5_core.h"
#include "fs_core.h"
#include "fs_cmd.h"

#define MLX5_FC_STATS_PERIOD msecs_to_jiffies(1000)
/* Max number of counters to query in bulk read is 32K */
#define MLX5_SW_MAX_COUNTERS_BULK BIT(15)

struct mlx5_fc_cache {
	u64 packets;
	u64 bytes;
	u64 lastuse;
};

struct mlx5_fc {
	struct list_head list;
	struct llist_node addlist;
	struct llist_node dellist;

	/* last{packets,bytes} members are used when calculating the delta since
	 * last reading
	 */
	u64 lastpackets;
	u64 lastbytes;

	u32 id;
	bool aging;

	struct mlx5_fc_cache cache ____cacheline_aligned_in_smp;
};

/* locking scheme:
 *
 * It is the responsibility of the user to prevent concurrent calls or bad
 * ordering to mlx5_fc_create(), mlx5_fc_destroy() and accessing a reference
 * to struct mlx5_fc.
 * e.g en_tc.c is protected by RTNL lock of its caller, and will never call a
 * dump (access to struct mlx5_fc) after a counter is destroyed.
 *
 * access to counter list:
 * - create (user context)
 *   - mlx5_fc_create() only adds to an addlist to be used by
 *     mlx5_fc_stats_query_work(). addlist is a lockless single linked list
 *     that doesn't require any additional synchronization when adding single
 *     node.
 *   - spawn thread to do the actual destroy
 *
 * - destroy (user context)
 *   - add a counter to lockless dellist
 *   - spawn thread to do the actual del
 *
 * - dump (user context)
 *   user should not call dump after destroy
 *
 * - query (single thread workqueue context)
 *   destroy/dump - no conflict (see destroy)
 *   query/dump - packets and bytes might be inconsistent (since update is not
 *                atomic)
 *   query/create - no conflict (see create)
 *   since every create/destroy spawn the work, only after necessary time has
 *   elapsed, the thread will actually query the hardware.
 */

static struct list_head *mlx5_fc_counters_lookup_next(struct mlx5_core_dev *dev,
						      u32 id)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;
	unsigned long next_id = (unsigned long)id + 1;
	struct mlx5_fc *counter;
	unsigned long tmp;

	rcu_read_lock();
	/* skip counters that are in idr, but not yet in counters list */
	idr_for_each_entry_continue_ul(&fc_stats->counters_idr,
				       counter, tmp, next_id) {
		if (!list_empty(&counter->list))
			break;
	}
	rcu_read_unlock();

	return counter ? &counter->list : &fc_stats->counters;
}

static void mlx5_fc_stats_insert(struct mlx5_core_dev *dev,
				 struct mlx5_fc *counter)
{
	struct list_head *next = mlx5_fc_counters_lookup_next(dev, counter->id);

	list_add_tail(&counter->list, next);
}

static void mlx5_fc_stats_remove(struct mlx5_core_dev *dev,
				 struct mlx5_fc *counter)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;

	list_del(&counter->list);

	spin_lock(&fc_stats->counters_idr_lock);
	WARN_ON(!idr_remove(&fc_stats->counters_idr, counter->id));
	spin_unlock(&fc_stats->counters_idr_lock);
}

/* The function returns the last counter that was queried so the caller
 * function can continue calling it till all counters are queried.
 */
static struct mlx5_fc *mlx5_fc_stats_query(struct mlx5_core_dev *dev,
					   struct mlx5_fc *first,
					   u32 last_id)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;
	struct mlx5_fc *counter = NULL;
	struct mlx5_cmd_fc_bulk *b;
	bool more = false;
	u32 afirst_id;
	int num;
	int err;

	int max_bulk = min_t(int, MLX5_SW_MAX_COUNTERS_BULK,
			     (1 << MLX5_CAP_GEN(dev, log_max_flow_counter_bulk)));

	/* first id must be aligned to 4 when using bulk query */
	afirst_id = first->id & ~0x3;

	/* number of counters to query inc. the last counter */
	num = ALIGN(last_id - afirst_id + 1, 4);
	if (num > max_bulk) {
		num = max_bulk;
		last_id = afirst_id + num - 1;
	}

	b = mlx5_cmd_fc_bulk_alloc(dev, afirst_id, num);
	if (!b) {
		mlx5_core_err(dev, "Error allocating resources for bulk query\n");
		return NULL;
	}

	err = mlx5_cmd_fc_bulk_query(dev, b);
	if (err) {
		mlx5_core_err(dev, "Error doing bulk query: %d\n", err);
		goto out;
	}

	counter = first;
	list_for_each_entry_from(counter, &fc_stats->counters, list) {
		struct mlx5_fc_cache *c = &counter->cache;
		u64 packets;
		u64 bytes;

		if (counter->id > last_id) {
			more = true;
			break;
		}

		mlx5_cmd_fc_bulk_get(dev, b,
				     counter->id, &packets, &bytes);

		if (c->packets == packets)
			continue;

		c->packets = packets;
		c->bytes = bytes;
		c->lastuse = jiffies;
	}

out:
	mlx5_cmd_fc_bulk_free(b);

	return more ? counter : NULL;
}

static void mlx5_free_fc(struct mlx5_core_dev *dev,
			 struct mlx5_fc *counter)
{
	mlx5_cmd_fc_free(dev, counter->id);
	kfree(counter);
}

static void mlx5_fc_stats_work(struct work_struct *work)
{
	struct mlx5_core_dev *dev = container_of(work, struct mlx5_core_dev,
						 priv.fc_stats.work.work);
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;
	/* Take dellist first to ensure that counters cannot be deleted before
	 * they are inserted.
	 */
	struct llist_node *dellist = llist_del_all(&fc_stats->dellist);
	struct llist_node *addlist = llist_del_all(&fc_stats->addlist);
	struct mlx5_fc *counter = NULL, *last = NULL, *tmp;
	unsigned long now = jiffies;

	if (addlist || !list_empty(&fc_stats->counters))
		queue_delayed_work(fc_stats->wq, &fc_stats->work,
				   fc_stats->sampling_interval);

	llist_for_each_entry(counter, addlist, addlist)
		mlx5_fc_stats_insert(dev, counter);

	llist_for_each_entry_safe(counter, tmp, dellist, dellist) {
		mlx5_fc_stats_remove(dev, counter);

		mlx5_free_fc(dev, counter);
	}

	if (time_before(now, fc_stats->next_query) ||
	    list_empty(&fc_stats->counters))
		return;
	last = list_last_entry(&fc_stats->counters, struct mlx5_fc, list);

	counter = list_first_entry(&fc_stats->counters, struct mlx5_fc,
				   list);
	while (counter)
		counter = mlx5_fc_stats_query(dev, counter, last->id);

	fc_stats->next_query = now + fc_stats->sampling_interval;
}

struct mlx5_fc *mlx5_fc_create(struct mlx5_core_dev *dev, bool aging)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;
	struct mlx5_fc *counter;
	int err;

	counter = kzalloc(sizeof(*counter), GFP_KERNEL);
	if (!counter)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&counter->list);

	err = mlx5_cmd_fc_alloc(dev, &counter->id);
	if (err)
		goto err_out;

	if (aging) {
		u32 id = counter->id;

		counter->cache.lastuse = jiffies;
		counter->aging = true;

		idr_preload(GFP_KERNEL);
		spin_lock(&fc_stats->counters_idr_lock);

		err = idr_alloc_u32(&fc_stats->counters_idr, counter, &id, id,
				    GFP_NOWAIT);

		spin_unlock(&fc_stats->counters_idr_lock);
		idr_preload_end();
		if (err)
			goto err_out_alloc;

		llist_add(&counter->addlist, &fc_stats->addlist);

		mod_delayed_work(fc_stats->wq, &fc_stats->work, 0);
	}

	return counter;

err_out_alloc:
	mlx5_cmd_fc_free(dev, counter->id);
err_out:
	kfree(counter);

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
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;

	if (!counter)
		return;

	if (counter->aging) {
		llist_add(&counter->dellist, &fc_stats->dellist);
		mod_delayed_work(fc_stats->wq, &fc_stats->work, 0);
		return;
	}

	mlx5_free_fc(dev, counter);
}
EXPORT_SYMBOL(mlx5_fc_destroy);

int mlx5_init_fc_stats(struct mlx5_core_dev *dev)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;

	spin_lock_init(&fc_stats->counters_idr_lock);
	idr_init(&fc_stats->counters_idr);
	INIT_LIST_HEAD(&fc_stats->counters);
	init_llist_head(&fc_stats->addlist);
	init_llist_head(&fc_stats->dellist);

	fc_stats->wq = create_singlethread_workqueue("mlx5_fc");
	if (!fc_stats->wq)
		return -ENOMEM;

	fc_stats->sampling_interval = MLX5_FC_STATS_PERIOD;
	INIT_DELAYED_WORK(&fc_stats->work, mlx5_fc_stats_work);

	return 0;
}

void mlx5_cleanup_fc_stats(struct mlx5_core_dev *dev)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;
	struct llist_node *tmplist;
	struct mlx5_fc *counter;
	struct mlx5_fc *tmp;

	cancel_delayed_work_sync(&dev->priv.fc_stats.work);
	destroy_workqueue(dev->priv.fc_stats.wq);
	dev->priv.fc_stats.wq = NULL;

	idr_destroy(&fc_stats->counters_idr);

	tmplist = llist_del_all(&fc_stats->addlist);
	llist_for_each_entry_safe(counter, tmp, tmplist, addlist)
		mlx5_free_fc(dev, counter);

	list_for_each_entry_safe(counter, tmp, &fc_stats->counters, list)
		mlx5_free_fc(dev, counter);
}

int mlx5_fc_query(struct mlx5_core_dev *dev, struct mlx5_fc *counter,
		  u64 *packets, u64 *bytes)
{
	return mlx5_cmd_fc_query(dev, counter->id, packets, bytes);
}
EXPORT_SYMBOL(mlx5_fc_query);

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

void mlx5_fc_queue_stats_work(struct mlx5_core_dev *dev,
			      struct delayed_work *dwork,
			      unsigned long delay)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;

	queue_delayed_work(fc_stats->wq, dwork, delay);
}

void mlx5_fc_update_sampling_interval(struct mlx5_core_dev *dev,
				      unsigned long interval)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;

	fc_stats->sampling_interval = min_t(unsigned long, interval,
					    fc_stats->sampling_interval);
}
