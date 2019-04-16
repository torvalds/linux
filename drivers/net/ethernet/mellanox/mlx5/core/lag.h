/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_LAG_H__
#define __MLX5_LAG_H__

#include "mlx5_core.h"
#include "lag_mp.h"

enum {
	MLX5_LAG_FLAG_ROCE   = 1 << 0,
	MLX5_LAG_FLAG_SRIOV  = 1 << 1,
	MLX5_LAG_FLAG_MULTIPATH = 1 << 2,
};

#define MLX5_LAG_MODE_FLAGS (MLX5_LAG_FLAG_ROCE | MLX5_LAG_FLAG_SRIOV |\
			     MLX5_LAG_FLAG_MULTIPATH)

struct lag_func {
	struct mlx5_core_dev *dev;
	struct net_device    *netdev;
};

/* Used for collection of netdev event info. */
struct lag_tracker {
	enum   netdev_lag_tx_type           tx_type;
	struct netdev_lag_lower_state_info  netdev_state[MLX5_MAX_PORTS];
	unsigned int is_bonded:1;
};

/* LAG data of a ConnectX card.
 * It serves both its phys functions.
 */
struct mlx5_lag {
	u8                        flags;
	u8                        v2p_map[MLX5_MAX_PORTS];
	struct lag_func           pf[MLX5_MAX_PORTS];
	struct lag_tracker        tracker;
	struct workqueue_struct   *wq;
	struct delayed_work       bond_work;
	struct notifier_block     nb;
	struct lag_mp             lag_mp;
};

static inline struct mlx5_lag *
mlx5_lag_dev_get(struct mlx5_core_dev *dev)
{
	return dev->priv.lag;
}

static inline bool
__mlx5_lag_is_active(struct mlx5_lag *ldev)
{
	return !!(ldev->flags & MLX5_LAG_MODE_FLAGS);
}

void mlx5_modify_lag(struct mlx5_lag *ldev,
		     struct lag_tracker *tracker);
int mlx5_activate_lag(struct mlx5_lag *ldev,
		      struct lag_tracker *tracker,
		      u8 flags);
int mlx5_lag_dev_get_netdev_idx(struct mlx5_lag *ldev,
				struct net_device *ndev);

#endif /* __MLX5_LAG_H__ */
