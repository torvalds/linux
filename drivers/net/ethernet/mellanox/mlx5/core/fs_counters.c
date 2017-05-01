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
 *     mlx5_fc_stats_query_work(). addlist is protected by a spinlock.
 *   - spawn thread to do the actual destroy
 *
 * - destroy (user context)
 *   - mark a counter as deleted
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

static void mlx5_fc_stats_insert(struct rb_root *root, struct mlx5_fc *counter)
{
	struct rb_node **new = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*new) {
		struct mlx5_fc *this = rb_entry(*new, struct mlx5_fc, node);
		int result = counter->id - this->id;

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&counter->node, parent, new);
	rb_insert_color(&counter->node, root);
}

static struct rb_node *mlx5_fc_stats_query(struct mlx5_core_dev *dev,
					   struct mlx5_fc *first,
					   u16 last_id)
{
	struct mlx5_cmd_fc_bulk *b;
	struct rb_node *node = NULL;
	u16 afirst_id;
	int num;
	int err;
	int max_bulk = 1 << MLX5_CAP_GEN(dev, log_max_flow_counter_bulk);

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

	for (node = &first->node; node; node = rb_next(node)) {
		struct mlx5_fc *counter = rb_entry(node, struct mlx5_fc, node);
		struct mlx5_fc_cache *c = &counter->cache;
		u64 packets;
		u64 bytes;

		if (counter->id > last_id)
			break;

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

	return node;
}

static void mlx5_fc_stats_work(struct work_struct *work)
{
	struct mlx5_core_dev *dev = container_of(work, struct mlx5_core_dev,
						 priv.fc_stats.work.work);
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;
	unsigned long now = jiffies;
	struct mlx5_fc *counter = NULL;
	struct mlx5_fc *last = NULL;
	struct rb_node *node;
	LIST_HEAD(tmplist);

	spin_lock(&fc_stats->addlist_lock);

	list_splice_tail_init(&fc_stats->addlist, &tmplist);

	if (!list_empty(&tmplist) || !RB_EMPTY_ROOT(&fc_stats->counters))
		queue_delayed_work(fc_stats->wq, &fc_stats->work,
				   fc_stats->sampling_interval);

	spin_unlock(&fc_stats->addlist_lock);

	list_for_each_entry(counter, &tmplist, list)
		mlx5_fc_stats_insert(&fc_stats->counters, counter);

	node = rb_first(&fc_stats->counters);
	while (node) {
		counter = rb_entry(node, struct mlx5_fc, node);

		node = rb_next(node);

		if (counter->deleted) {
			rb_erase(&counter->node, &fc_stats->counters);

			mlx5_cmd_fc_free(dev, counter->id);

			kfree(counter);
			continue;
		}

		last = counter;
	}

	if (time_before(now, fc_stats->next_query) || !last)
		return;

	node = rb_first(&fc_stats->counters);
	while (node) {
		counter = rb_entry(node, struct mlx5_fc, node);

		node = mlx5_fc_stats_query(dev, counter, last->id);
	}

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

	err = mlx5_cmd_fc_alloc(dev, &counter->id);
	if (err)
		goto err_out;

	if (aging) {
		counter->cache.lastuse = jiffies;
		counter->aging = true;

		spin_lock(&fc_stats->addlist_lock);
		list_add(&counter->list, &fc_stats->addlist);
		spin_unlock(&fc_stats->addlist_lock);

		mod_delayed_work(fc_stats->wq, &fc_stats->work, 0);
	}

	return counter;

err_out:
	kfree(counter);

	return ERR_PTR(err);
}

void mlx5_fc_destroy(struct mlx5_core_dev *dev, struct mlx5_fc *counter)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;

	if (!counter)
		return;

	if (counter->aging) {
		counter->deleted = true;
		mod_delayed_work(fc_stats->wq, &fc_stats->work, 0);
		return;
	}

	mlx5_cmd_fc_free(dev, counter->id);
	kfree(counter);
}

int mlx5_init_fc_stats(struct mlx5_core_dev *dev)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;

	fc_stats->counters = RB_ROOT;
	INIT_LIST_HEAD(&fc_stats->addlist);
	spin_lock_init(&fc_stats->addlist_lock);

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
	struct mlx5_fc *counter;
	struct mlx5_fc *tmp;
	struct rb_node *node;

	cancel_delayed_work_sync(&dev->priv.fc_stats.work);
	destroy_workqueue(dev->priv.fc_stats.wq);
	dev->priv.fc_stats.wq = NULL;

	list_for_each_entry_safe(counter, tmp, &fc_stats->addlist, list) {
		list_del(&counter->list);

		mlx5_cmd_fc_free(dev, counter->id);

		kfree(counter);
	}

	node = rb_first(&fc_stats->counters);
	while (node) {
		counter = rb_entry(node, struct mlx5_fc, node);

		node = rb_next(node);

		rb_erase(&counter->node, &fc_stats->counters);

		mlx5_cmd_fc_free(dev, counter->id);

		kfree(counter);
	}
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
