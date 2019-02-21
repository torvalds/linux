// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/netdevice.h>
#include "lag.h"
#include "mlx5_core.h"
#include "eswitch.h"

static bool __mlx5_lag_is_multipath(struct mlx5_lag *ldev)
{
	return !!(ldev->flags & MLX5_LAG_FLAG_MULTIPATH);
}

bool mlx5_lag_is_multipath(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	ldev = mlx5_lag_dev_get(dev);
	res  = ldev && __mlx5_lag_is_multipath(ldev);

	return res;
}
