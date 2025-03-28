// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include <mlx5_core.h>
#include "fs_pool.h"

int mlx5_fs_bulk_init(struct mlx5_core_dev *dev, struct mlx5_fs_bulk *fs_bulk,
		      int bulk_len)
{
	int i;

	fs_bulk->bitmask = kvcalloc(BITS_TO_LONGS(bulk_len), sizeof(unsigned long),
				    GFP_KERNEL);
	if (!fs_bulk->bitmask)
		return -ENOMEM;

	fs_bulk->bulk_len = bulk_len;
	for (i = 0; i < bulk_len; i++)
		set_bit(i, fs_bulk->bitmask);

	return 0;
}

void mlx5_fs_bulk_cleanup(struct mlx5_fs_bulk *fs_bulk)
{
	kvfree(fs_bulk->bitmask);
}

int mlx5_fs_bulk_get_free_amount(struct mlx5_fs_bulk *bulk)
{
	return bitmap_weight(bulk->bitmask, bulk->bulk_len);
}

static int mlx5_fs_bulk_acquire_index(struct mlx5_fs_bulk *fs_bulk,
				      struct mlx5_fs_pool_index *pool_index)
{
	int free_index = find_first_bit(fs_bulk->bitmask, fs_bulk->bulk_len);

	WARN_ON_ONCE(!pool_index || !fs_bulk);
	if (free_index >= fs_bulk->bulk_len)
		return -ENOSPC;

	clear_bit(free_index, fs_bulk->bitmask);
	pool_index->fs_bulk = fs_bulk;
	pool_index->index = free_index;
	return 0;
}

static int mlx5_fs_bulk_release_index(struct mlx5_fs_bulk *fs_bulk, int index)
{
	if (test_bit(index, fs_bulk->bitmask))
		return -EINVAL;

	set_bit(index, fs_bulk->bitmask);
	return 0;
}

void mlx5_fs_pool_init(struct mlx5_fs_pool *pool, struct mlx5_core_dev *dev,
		       const struct mlx5_fs_pool_ops *ops, void *pool_ctx)
{
	WARN_ON_ONCE(!ops || !ops->bulk_destroy || !ops->bulk_create ||
		     !ops->update_threshold);
	pool->dev = dev;
	pool->pool_ctx = pool_ctx;
	mutex_init(&pool->pool_lock);
	INIT_LIST_HEAD(&pool->fully_used);
	INIT_LIST_HEAD(&pool->partially_used);
	INIT_LIST_HEAD(&pool->unused);
	pool->available_units = 0;
	pool->used_units = 0;
	pool->threshold = 0;
	pool->ops = ops;
}

void mlx5_fs_pool_cleanup(struct mlx5_fs_pool *pool)
{
	struct mlx5_core_dev *dev = pool->dev;
	struct mlx5_fs_bulk *bulk;
	struct mlx5_fs_bulk *tmp;

	list_for_each_entry_safe(bulk, tmp, &pool->fully_used, pool_list)
		pool->ops->bulk_destroy(dev, bulk);
	list_for_each_entry_safe(bulk, tmp, &pool->partially_used, pool_list)
		pool->ops->bulk_destroy(dev, bulk);
	list_for_each_entry_safe(bulk, tmp, &pool->unused, pool_list)
		pool->ops->bulk_destroy(dev, bulk);
}

static struct mlx5_fs_bulk *
mlx5_fs_pool_alloc_new_bulk(struct mlx5_fs_pool *fs_pool)
{
	struct mlx5_core_dev *dev = fs_pool->dev;
	struct mlx5_fs_bulk *new_bulk;

	new_bulk = fs_pool->ops->bulk_create(dev, fs_pool->pool_ctx);
	if (new_bulk)
		fs_pool->available_units += new_bulk->bulk_len;
	fs_pool->ops->update_threshold(fs_pool);
	return new_bulk;
}

static void
mlx5_fs_pool_free_bulk(struct mlx5_fs_pool *fs_pool, struct mlx5_fs_bulk *bulk)
{
	struct mlx5_core_dev *dev = fs_pool->dev;

	fs_pool->available_units -= bulk->bulk_len;
	fs_pool->ops->bulk_destroy(dev, bulk);
	fs_pool->ops->update_threshold(fs_pool);
}

static int
mlx5_fs_pool_acquire_from_list(struct list_head *src_list,
			       struct list_head *next_list,
			       bool move_non_full_bulk,
			       struct mlx5_fs_pool_index *pool_index)
{
	struct mlx5_fs_bulk *fs_bulk;
	int err;

	if (list_empty(src_list))
		return -ENODATA;

	fs_bulk = list_first_entry(src_list, struct mlx5_fs_bulk, pool_list);
	err = mlx5_fs_bulk_acquire_index(fs_bulk, pool_index);
	if (move_non_full_bulk || mlx5_fs_bulk_get_free_amount(fs_bulk) == 0)
		list_move(&fs_bulk->pool_list, next_list);
	return err;
}

int mlx5_fs_pool_acquire_index(struct mlx5_fs_pool *fs_pool,
			       struct mlx5_fs_pool_index *pool_index)
{
	struct mlx5_fs_bulk *new_bulk;
	int err;

	mutex_lock(&fs_pool->pool_lock);

	err = mlx5_fs_pool_acquire_from_list(&fs_pool->partially_used,
					     &fs_pool->fully_used, false,
					     pool_index);
	if (err)
		err = mlx5_fs_pool_acquire_from_list(&fs_pool->unused,
						     &fs_pool->partially_used,
						     true, pool_index);
	if (err) {
		new_bulk = mlx5_fs_pool_alloc_new_bulk(fs_pool);
		if (!new_bulk) {
			err = -ENOENT;
			goto out;
		}
		err = mlx5_fs_bulk_acquire_index(new_bulk, pool_index);
		WARN_ON_ONCE(err);
		list_add(&new_bulk->pool_list, &fs_pool->partially_used);
	}
	fs_pool->available_units--;
	fs_pool->used_units++;

out:
	mutex_unlock(&fs_pool->pool_lock);
	return err;
}

int mlx5_fs_pool_release_index(struct mlx5_fs_pool *fs_pool,
			       struct mlx5_fs_pool_index *pool_index)
{
	struct mlx5_fs_bulk *bulk = pool_index->fs_bulk;
	int bulk_free_amount;
	int err;

	mutex_lock(&fs_pool->pool_lock);

	/* TBD would rather return void if there was no warn here in original code */
	err = mlx5_fs_bulk_release_index(bulk, pool_index->index);
	if (err)
		goto unlock;

	fs_pool->available_units++;
	fs_pool->used_units--;

	bulk_free_amount = mlx5_fs_bulk_get_free_amount(bulk);
	if (bulk_free_amount == 1)
		list_move_tail(&bulk->pool_list, &fs_pool->partially_used);
	if (bulk_free_amount == bulk->bulk_len) {
		list_del(&bulk->pool_list);
		if (fs_pool->available_units > fs_pool->threshold)
			mlx5_fs_pool_free_bulk(fs_pool, bulk);
		else
			list_add(&bulk->pool_list, &fs_pool->unused);
	}

unlock:
	mutex_unlock(&fs_pool->pool_lock);
	return err;
}
