/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_LAG_MPESW_H__
#define __MLX5_LAG_MPESW_H__

#include "lag.h"
#include "mlx5_core.h"

struct lag_mpesw {
	struct work_struct mpesw_work;
	atomic_t mpesw_rule_count;
};

void mlx5_mpesw_work(struct work_struct *work);
int mlx5_lag_do_mirred(struct mlx5_core_dev *mdev, struct net_device *out_dev);
bool mlx5_lag_mpesw_is_activated(struct mlx5_core_dev *dev);
#if IS_ENABLED(CONFIG_MLX5_ESWITCH)
void mlx5_lag_mpesw_init(struct mlx5_lag *ldev);
void mlx5_lag_mpesw_cleanup(struct mlx5_lag *ldev);
#else
static inline void mlx5_lag_mpesw_init(struct mlx5_lag *ldev) {}
static inline void mlx5_lag_mpesw_cleanup(struct mlx5_lag *ldev) {}
#endif

#endif /* __MLX5_LAG_MPESW_H__ */
