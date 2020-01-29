/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_IPSEC_OFFLOAD_H__
#define __MLX5_IPSEC_OFFLOAD_H__

#include <linux/mlx5/driver.h>
#include "accel/ipsec.h"

#ifdef CONFIG_MLX5_IPSEC

const struct mlx5_accel_ipsec_ops *mlx5_ipsec_offload_ops(struct mlx5_core_dev *mdev);
static inline bool mlx5_is_ipsec_device(struct mlx5_core_dev *mdev)
{
	if (!MLX5_CAP_GEN(mdev, ipsec_offload))
		return false;

	if (!MLX5_CAP_GEN(mdev, log_max_dek))
		return false;

	if (!(MLX5_CAP_GEN_64(mdev, general_obj_types) &
	    MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC))
		return false;

	return MLX5_CAP_IPSEC(mdev, ipsec_crypto_offload) &&
		MLX5_CAP_ETH(mdev, insert_trailer);
}

#else
static inline const struct mlx5_accel_ipsec_ops *
mlx5_ipsec_offload_ops(struct mlx5_core_dev *mdev) { return NULL; }
static inline bool mlx5_is_ipsec_device(struct mlx5_core_dev *mdev)
{
	return false;
}

#endif /* CONFIG_MLX5_IPSEC */
#endif /* __MLX5_IPSEC_OFFLOAD_H__ */
