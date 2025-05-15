// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include "fs_ft_pool.h"

/* Firmware currently has 4 pool of 4 sizes that it supports (FT_POOLS),
 * and a virtual memory region of 16M (MLX5_FT_SIZE), this region is duplicated
 * for each flow table pool. We can allocate up to 16M of each pool,
 * and we keep track of how much we used via mlx5_ft_pool_get_avail_sz.
 * Firmware doesn't report any of this for now.
 * ESW_POOL is expected to be sorted from large to small and match firmware
 * pools.
 */
#define FT_SIZE (16 * 1024 * 1024)
static const unsigned int FT_POOLS[] = { 4 * 1024 * 1024,
					 1 * 1024 * 1024,
					 64 * 1024,
					 128,
					 1 /* size for termination tables */ };
struct mlx5_ft_pool {
	int ft_left[ARRAY_SIZE(FT_POOLS)];
};

int mlx5_ft_pool_init(struct mlx5_core_dev *dev)
{
	struct mlx5_ft_pool *ft_pool;
	int i;

	ft_pool = kzalloc(sizeof(*ft_pool), GFP_KERNEL);
	if (!ft_pool)
		return -ENOMEM;

	for (i = ARRAY_SIZE(FT_POOLS) - 1; i >= 0; i--)
		ft_pool->ft_left[i] = FT_SIZE / FT_POOLS[i];

	dev->priv.ft_pool = ft_pool;
	return 0;
}

void mlx5_ft_pool_destroy(struct mlx5_core_dev *dev)
{
	kfree(dev->priv.ft_pool);
}

int
mlx5_ft_pool_get_avail_sz(struct mlx5_core_dev *dev, enum fs_flow_table_type table_type,
			  int desired_size)
{
	u32 max_ft_size = 1 << MLX5_CAP_FLOWTABLE_TYPE(dev, log_max_ft_size, table_type);
	int i, found_i = -1;

	for (i = ARRAY_SIZE(FT_POOLS) - 1; i >= 0; i--) {
		if (dev->priv.ft_pool->ft_left[i] &&
		    (FT_POOLS[i] >= desired_size ||
		     desired_size == MLX5_FS_MAX_POOL_SIZE) &&
		    FT_POOLS[i] <= max_ft_size) {
			found_i = i;
			if (desired_size != MLX5_FS_MAX_POOL_SIZE)
				break;
		}
	}

	if (found_i != -1) {
		--dev->priv.ft_pool->ft_left[found_i];
		return FT_POOLS[found_i];
	}

	return 0;
}

void
mlx5_ft_pool_put_sz(struct mlx5_core_dev *dev, int sz)
{
	int i;

	if (!sz)
		return;

	for (i = ARRAY_SIZE(FT_POOLS) - 1; i >= 0; i--) {
		if (sz == FT_POOLS[i]) {
			++dev->priv.ft_pool->ft_left[i];
			return;
		}
	}

	WARN_ONCE(1, "Couldn't find size %d in flow table size pool", sz);
}
