/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef __MLX5_FS_POOL_H__
#define __MLX5_FS_POOL_H__

#include <linux/mlx5/driver.h>

struct mlx5_fs_bulk {
	struct list_head pool_list;
	int bulk_len;
	unsigned long *bitmask;
};

struct mlx5_fs_pool_index {
	struct mlx5_fs_bulk *fs_bulk;
	int index;
};

struct mlx5_fs_pool;

struct mlx5_fs_pool_ops {
	int (*bulk_destroy)(struct mlx5_core_dev *dev, struct mlx5_fs_bulk *bulk);
	struct mlx5_fs_bulk * (*bulk_create)(struct mlx5_core_dev *dev,
					     void *pool_ctx);
	void (*update_threshold)(struct mlx5_fs_pool *pool);
};

struct mlx5_fs_pool {
	struct mlx5_core_dev *dev;
	void *pool_ctx;
	const struct mlx5_fs_pool_ops *ops;
	struct mutex pool_lock; /* protects pool lists */
	struct list_head fully_used;
	struct list_head partially_used;
	struct list_head unused;
	int available_units;
	int used_units;
	int threshold;
};

int mlx5_fs_bulk_init(struct mlx5_core_dev *dev, struct mlx5_fs_bulk *fs_bulk,
		      int bulk_len);
void mlx5_fs_bulk_cleanup(struct mlx5_fs_bulk *fs_bulk);
int mlx5_fs_bulk_get_free_amount(struct mlx5_fs_bulk *bulk);

void mlx5_fs_pool_init(struct mlx5_fs_pool *pool, struct mlx5_core_dev *dev,
		       const struct mlx5_fs_pool_ops *ops, void *pool_ctx);
void mlx5_fs_pool_cleanup(struct mlx5_fs_pool *pool);
int mlx5_fs_pool_acquire_index(struct mlx5_fs_pool *fs_pool,
			       struct mlx5_fs_pool_index *pool_index);
int mlx5_fs_pool_release_index(struct mlx5_fs_pool *fs_pool,
			       struct mlx5_fs_pool_index *pool_index);

#endif /* __MLX5_FS_POOL_H__ */
