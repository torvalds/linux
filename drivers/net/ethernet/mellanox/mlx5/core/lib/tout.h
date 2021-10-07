/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef MLX5_TIMEOUTS_H
#define MLX5_TIMEOUTS_H

enum mlx5_timeouts_types {
	/* pre init timeouts (not read from FW) */
	MLX5_TO_FW_PRE_INIT_TIMEOUT_MS,
	MLX5_TO_FW_PRE_INIT_WARN_MESSAGE_INTERVAL_MS,
	MLX5_TO_FW_PRE_INIT_WAIT_MS,

	/* init segment timeouts */
	MLX5_TO_FW_INIT_MS,
	MLX5_TO_CMD_MS,

	MAX_TIMEOUT_TYPES
};

struct mlx5_core_dev;
int mlx5_tout_init(struct mlx5_core_dev *dev);
void mlx5_tout_cleanup(struct mlx5_core_dev *dev);
void mlx5_tout_query_iseg(struct mlx5_core_dev *dev);
u64 _mlx5_tout_ms(struct mlx5_core_dev *dev, enum mlx5_timeouts_types type);

#define mlx5_tout_ms(dev, type) _mlx5_tout_ms(dev, MLX5_TO_##type##_MS)

# endif /* MLX5_TIMEOUTS_H */
