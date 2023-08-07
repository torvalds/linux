// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/rhashtable.h>
#include <net/flow_offload.h>
#include "en/tc_priv.h"
#include "act_stats.h"
#include "en/fs.h"

struct mlx5e_tc_act_stats_handle {
	struct rhashtable ht;
	spinlock_t ht_lock; /* protects hashtable */
};

struct mlx5e_tc_act_stats {
	unsigned long		tc_act_cookie;

	struct mlx5_fc		*counter;
	u64			lastpackets;
	u64			lastbytes;

	struct rhash_head	hash;
	struct rcu_head		rcu_head;
};

static const struct rhashtable_params act_counters_ht_params = {
	.head_offset = offsetof(struct mlx5e_tc_act_stats, hash),
	.key_offset = offsetof(struct mlx5e_tc_act_stats, tc_act_cookie),
	.key_len = sizeof_field(struct mlx5e_tc_act_stats, tc_act_cookie),
	.automatic_shrinking = true,
};

struct mlx5e_tc_act_stats_handle *
mlx5e_tc_act_stats_create(void)
{
	struct mlx5e_tc_act_stats_handle *handle;
	int err;

	handle = kvzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return ERR_PTR(-ENOMEM);

	err = rhashtable_init(&handle->ht, &act_counters_ht_params);
	if (err)
		goto err;

	spin_lock_init(&handle->ht_lock);
	return handle;
err:
	kvfree(handle);
	return ERR_PTR(err);
}

void mlx5e_tc_act_stats_free(struct mlx5e_tc_act_stats_handle *handle)
{
	rhashtable_destroy(&handle->ht);
	kvfree(handle);
}

static int
mlx5e_tc_act_stats_add(struct mlx5e_tc_act_stats_handle *handle,
		       unsigned long act_cookie,
		       struct mlx5_fc *counter)
{
	struct mlx5e_tc_act_stats *act_stats, *old_act_stats;
	struct rhashtable *ht = &handle->ht;
	u64 lastused;
	int err = 0;

	act_stats = kvzalloc(sizeof(*act_stats), GFP_KERNEL);
	if (!act_stats)
		return -ENOMEM;

	act_stats->tc_act_cookie = act_cookie;
	act_stats->counter = counter;

	mlx5_fc_query_cached_raw(counter,
				 &act_stats->lastbytes,
				 &act_stats->lastpackets, &lastused);

	rcu_read_lock();
	old_act_stats = rhashtable_lookup_get_insert_fast(ht,
							  &act_stats->hash,
							  act_counters_ht_params);
	if (IS_ERR(old_act_stats)) {
		err = PTR_ERR(old_act_stats);
		goto err_hash_insert;
	} else if (old_act_stats) {
		err = -EEXIST;
		goto err_hash_insert;
	}
	rcu_read_unlock();

	return 0;

err_hash_insert:
	rcu_read_unlock();
	kvfree(act_stats);
	return err;
}

void
mlx5e_tc_act_stats_del_flow(struct mlx5e_tc_act_stats_handle *handle,
			    struct mlx5e_tc_flow *flow)
{
	struct mlx5_flow_attr *attr;
	struct mlx5e_tc_act_stats *act_stats;
	int i;

	if (!flow_flag_test(flow, USE_ACT_STATS))
		return;

	list_for_each_entry(attr, &flow->attrs, list) {
		for (i = 0; i < attr->tc_act_cookies_count; i++) {
			struct rhashtable *ht = &handle->ht;

			spin_lock(&handle->ht_lock);
			act_stats = rhashtable_lookup_fast(ht,
							   &attr->tc_act_cookies[i],
							   act_counters_ht_params);
			if (act_stats &&
			    rhashtable_remove_fast(ht, &act_stats->hash,
						   act_counters_ht_params) == 0)
				kvfree_rcu(act_stats, rcu_head);

			spin_unlock(&handle->ht_lock);
		}
	}
}

int
mlx5e_tc_act_stats_add_flow(struct mlx5e_tc_act_stats_handle *handle,
			    struct mlx5e_tc_flow *flow)
{
	struct mlx5_fc *curr_counter = NULL;
	unsigned long last_cookie = 0;
	struct mlx5_flow_attr *attr;
	int err;
	int i;

	if (!flow_flag_test(flow, USE_ACT_STATS))
		return 0;

	list_for_each_entry(attr, &flow->attrs, list) {
		if (attr->counter)
			curr_counter = attr->counter;

		for (i = 0; i < attr->tc_act_cookies_count; i++) {
			/* jump over identical ids (e.g. pedit)*/
			if (last_cookie == attr->tc_act_cookies[i])
				continue;

			err = mlx5e_tc_act_stats_add(handle, attr->tc_act_cookies[i], curr_counter);
			if (err)
				goto out_err;
			last_cookie = attr->tc_act_cookies[i];
		}
	}

	return 0;
out_err:
	mlx5e_tc_act_stats_del_flow(handle, flow);
	return err;
}

int
mlx5e_tc_act_stats_fill_stats(struct mlx5e_tc_act_stats_handle *handle,
			      struct flow_offload_action *fl_act)
{
	struct rhashtable *ht = &handle->ht;
	struct mlx5e_tc_act_stats *item;
	u64 pkts, bytes, lastused;
	int err = 0;

	rcu_read_lock();
	item = rhashtable_lookup(ht, &fl_act->cookie, act_counters_ht_params);
	if (!item) {
		rcu_read_unlock();
		err = -ENOENT;
		goto err_out;
	}

	mlx5_fc_query_cached_raw(item->counter,
				 &bytes, &pkts, &lastused);

	flow_stats_update(&fl_act->stats,
			  bytes - item->lastbytes,
			  pkts - item->lastpackets,
			  0, lastused, FLOW_ACTION_HW_STATS_DELAYED);

	item->lastpackets = pkts;
	item->lastbytes = bytes;
	rcu_read_unlock();

	return 0;

err_out:
	return err;
}
