/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_LAG_MPESW_H__
#define __MLX5_LAG_MPESW_H__

#include "lag.h"
#include "mlx5_core.h"

struct lag_mpesw {
	struct work_struct mpesw_work;
	u32 pf_metadata[MLX5_MAX_PORTS];
};

enum mpesw_op {
	MLX5_MPESW_OP_ENABLE,
	MLX5_MPESW_OP_DISABLE,
};

struct mlx5_mpesw_work_st {
	struct work_struct work;
	struct mlx5_lag    *lag;
	enum mpesw_op      op;
	struct completion  comp;
	int result;
};

int mlx5_lag_mpesw_do_mirred(struct mlx5_core_dev *mdev,
			     struct net_device *out_dev,
			     struct netlink_ext_ack *extack);
bool mlx5_lag_is_mpesw(struct mlx5_core_dev *dev);
void mlx5_lag_mpesw_disable(struct mlx5_core_dev *dev);
int mlx5_lag_mpesw_enable(struct mlx5_core_dev *dev);

#endif /* __MLX5_LAG_MPESW_H__ */
