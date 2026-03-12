/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_SH_DEVLINK_H__
#define __MLX5_SH_DEVLINK_H__

#include <linux/mlx5/driver.h>

int mlx5_shd_init(struct mlx5_core_dev *dev);
void mlx5_shd_uninit(struct mlx5_core_dev *dev);

#endif /* __MLX5_SH_DEVLINK_H__ */
