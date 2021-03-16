/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_QOS_H
#define __MLX5_QOS_H

#include "mlx5_core.h"

#define MLX5_DEBUG_QOS_MASK BIT(4)

#define qos_err(mdev, fmt, ...) \
	mlx5_core_err(mdev, "QoS: " fmt, ##__VA_ARGS__)
#define qos_warn(mdev, fmt, ...) \
	mlx5_core_warn(mdev, "QoS: " fmt, ##__VA_ARGS__)
#define qos_dbg(mdev, fmt, ...) \
	mlx5_core_dbg_mask(mdev, MLX5_DEBUG_QOS_MASK, "QoS: " fmt, ##__VA_ARGS__)

bool mlx5_qos_is_supported(struct mlx5_core_dev *mdev);
int mlx5_qos_max_leaf_nodes(struct mlx5_core_dev *mdev);

int mlx5_qos_create_leaf_node(struct mlx5_core_dev *mdev, u32 parent_id,
			      u32 bw_share, u32 max_avg_bw, u32 *id);
int mlx5_qos_create_inner_node(struct mlx5_core_dev *mdev, u32 parent_id,
			       u32 bw_share, u32 max_avg_bw, u32 *id);
int mlx5_qos_create_root_node(struct mlx5_core_dev *mdev, u32 *id);
int mlx5_qos_update_node(struct mlx5_core_dev *mdev, u32 parent_id, u32 bw_share,
			 u32 max_avg_bw, u32 id);
int mlx5_qos_destroy_node(struct mlx5_core_dev *mdev, u32 id);

#endif
