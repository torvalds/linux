/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies Ltd */

#ifndef __MLX5_SF_PRIV_H__
#define __MLX5_SF_PRIV_H__

#include <linux/mlx5/driver.h>

int mlx5_cmd_alloc_sf(struct mlx5_core_dev *dev, u16 function_id);
int mlx5_cmd_dealloc_sf(struct mlx5_core_dev *dev, u16 function_id);

int mlx5_cmd_sf_enable_hca(struct mlx5_core_dev *dev, u16 func_id);
int mlx5_cmd_sf_disable_hca(struct mlx5_core_dev *dev, u16 func_id);

u16 mlx5_sf_sw_to_hw_id(const struct mlx5_core_dev *dev, u16 sw_id);

int mlx5_sf_hw_table_sf_alloc(struct mlx5_core_dev *dev, u32 usr_sfnum);
void mlx5_sf_hw_table_sf_free(struct mlx5_core_dev *dev, u16 id);
void mlx5_sf_hw_table_sf_deferred_free(struct mlx5_core_dev *dev, u16 id);

#endif
