/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies Ltd */

#ifndef __MLX5_SF_DEV_H__
#define __MLX5_SF_DEV_H__

#ifdef CONFIG_MLX5_SF

#include <linux/auxiliary_bus.h>

#define MLX5_SF_DEV_ID_NAME "sf"

struct mlx5_sf_dev {
	struct auxiliary_device adev;
	struct mlx5_core_dev *parent_mdev;
	struct mlx5_core_dev *mdev;
	phys_addr_t bar_base_addr;
	u32 sfnum;
};

void mlx5_sf_dev_table_create(struct mlx5_core_dev *dev);
void mlx5_sf_dev_table_destroy(struct mlx5_core_dev *dev);

int mlx5_sf_driver_register(void);
void mlx5_sf_driver_unregister(void);

bool mlx5_sf_dev_allocated(const struct mlx5_core_dev *dev);

#else

static inline void mlx5_sf_dev_table_create(struct mlx5_core_dev *dev)
{
}

static inline void mlx5_sf_dev_table_destroy(struct mlx5_core_dev *dev)
{
}

static inline int mlx5_sf_driver_register(void)
{
	return 0;
}

static inline void mlx5_sf_driver_unregister(void)
{
}

static inline bool mlx5_sf_dev_allocated(const struct mlx5_core_dev *dev)
{
	return false;
}

#endif

#endif
