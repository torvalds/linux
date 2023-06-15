/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */
#ifndef __MLX5_THERMAL_DRIVER_H
#define __MLX5_THERMAL_DRIVER_H

#if IS_ENABLED(CONFIG_THERMAL)
int mlx5_thermal_init(struct mlx5_core_dev *mdev);
void mlx5_thermal_uninit(struct mlx5_core_dev *mdev);
#else
static inline int mlx5_thermal_init(struct mlx5_core_dev *mdev)
{
	mdev->thermal = NULL;
	return 0;
}

static inline void mlx5_thermal_uninit(struct mlx5_core_dev *mdev) { }
#endif

#endif /* __MLX5_THERMAL_DRIVER_H */
