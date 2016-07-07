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

static void mlx5_fc_stats_work(struct work_struct *work)
{
	struct mlx5_core_dev *dev = container_of(work, struct mlx5_core_dev,
						 priv.fc_stats.work.work);
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;
	unsigned long now = jiffies;
	struct mlx5_fc *counter;
	struct mlx5_fc *tmp;
	int err = 0;

	spin_lock(&fc_stats->addlist_lock);

	list_splice_tail_init(&fc_stats->addlist, &fc_stats->list);

	if (!list_empty(&fc_stats->list))
		queue_delayed_work(fc_stats->wq, &fc_stats->work, MLX5_FC_STATS_PERIOD);

	spin_unlock(&fc_stats->addlist_lock);

	list_for_each_entry_safe(counter, tmp, &fc_stats->list, list) {
		struct mlx5_fc_cache *c = &counter->cache;
		u64 packets;
		u64 bytes;

		if (counter->deleted) {
			list_del(&counter->list);

			mlx5_cmd_fc_free(dev, counter->id);

			kfree(counter);
			continue;
		}

		if (time_before(now, fc_stats->next_query))
			continue;

		err = mlx5_cmd_fc_query(dev, counter->id, &packets, &bytes);
		if (err) {
			pr_err("Error querying stats for counter id %d\n",
			       counter->id);
			continue;
		}

		if (packets == c->packets)
			continue;

		c->lastuse = jiffies;
		c->packets = packets;
		c->bytes   = bytes;
	}

	if (time_after_eq(now, fc_stats->next_query))
		fc_stats->next_query = now + MLX5_FC_STATS_PERIOD;
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

	INIT_LIST_HEAD(&fc_stats->list);
	INIT_LIST_HEAD(&fc_stats->addlist);
	spin_lock_init(&fc_stats->addlist_lock);

	fc_stats->wq = create_singlethread_workqueue("mlx5_fc");
	if (!fc_stats->wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&fc_stats->work, mlx5_fc_stats_work);

	return 0;
}

void mlx5_cleanup_fc_stats(struct mlx5_core_dev *dev)
{
	struct mlx5_fc_stats *fc_stats = &dev->priv.fc_stats;
	struct mlx5_fc *counter;
	struct mlx5_fc *tmp;

	cancel_delayed_work_sync(&dev->priv.fc_stats.work);
	destroy_workqueue(dev->priv.fc_stats.wq);
	dev->priv.fc_stats.wq = NULL;

	list_splice_tail_init(&fc_stats->addlist, &fc_stats->list);

	list_for_each_entry_safe(counter, tmp, &fc_stats->list, list) {
		list_del(&counter->list);

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
