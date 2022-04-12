/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_IPSEC_OFFLOAD_H__
#define __MLX5_IPSEC_OFFLOAD_H__

#include <linux/mlx5/driver.h>
#include <linux/mlx5/accel.h>

void *mlx5_accel_esp_create_hw_context(struct mlx5_core_dev *mdev,
				       struct mlx5_accel_esp_xfrm *xfrm,
				       u32 *sa_handle);
void mlx5_accel_esp_free_hw_context(struct mlx5_core_dev *mdev, void *context);
#endif /* __MLX5_IPSEC_OFFLOAD_H__ */
