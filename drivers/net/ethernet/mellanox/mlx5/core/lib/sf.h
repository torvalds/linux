/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021 Mellanox Technologies Ltd */

#ifndef __LIB_MLX5_SF_H__
#define __LIB_MLX5_SF_H__

#include <linux/mlx5/driver.h>

static inline u16 mlx5_sf_start_function_id(const struct mlx5_core_dev *dev)
{
	return MLX5_CAP_GEN(dev, sf_base_id);
}

#ifdef CONFIG_MLX5_SF

static inline bool mlx5_sf_supported(const struct mlx5_core_dev *dev)
{
	return MLX5_CAP_GEN(dev, sf);
}

static inline u16 mlx5_sf_max_functions(const struct mlx5_core_dev *dev)
{
	if (!mlx5_sf_supported(dev))
		return 0;
	if (MLX5_CAP_GEN(dev, max_num_sf))
		return MLX5_CAP_GEN(dev, max_num_sf);
	else
		return 1 << MLX5_CAP_GEN(dev, log_max_sf);
}

#else

static inline bool mlx5_sf_supported(const struct mlx5_core_dev *dev)
{
	return false;
}

static inline u16 mlx5_sf_max_functions(const struct mlx5_core_dev *dev)
{
	return 0;
}

#endif

#endif
