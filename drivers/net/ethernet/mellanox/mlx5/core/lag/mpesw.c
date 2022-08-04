// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/netdevice.h>
#include <net/nexthop.h>
#include "lag/lag.h"
#include "eswitch.h"
#include "lib/mlx5.h"

void mlx5_mpesw_work(struct work_struct *work)
{
	struct mlx5_lag *ldev = container_of(work, struct mlx5_lag, mpesw_work);

	mutex_lock(&ldev->lock);
	mlx5_disable_lag(ldev);
	mutex_unlock(&ldev->lock);
}

static void mlx5_lag_disable_mpesw(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev = dev->priv.lag;

	if (!queue_work(ldev->wq, &ldev->mpesw_work))
		mlx5_core_warn(dev, "failed to queue work\n");
}

void mlx5_lag_del_mpesw_rule(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev = dev->priv.lag;

	if (!ldev)
		return;

	mutex_lock(&ldev->lock);
	if (!atomic_dec_return(&ldev->lag_mpesw.mpesw_rule_count) &&
	    ldev->mode == MLX5_LAG_MODE_MPESW)
		mlx5_lag_disable_mpesw(dev);
	mutex_unlock(&ldev->lock);
}

int mlx5_lag_add_mpesw_rule(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev = dev->priv.lag;
	bool shared_fdb;
	int err = 0;

	if (!ldev)
		return 0;

	mutex_lock(&ldev->lock);
	if (atomic_add_return(1, &ldev->lag_mpesw.mpesw_rule_count) != 1)
		goto out;

	if (ldev->mode != MLX5_LAG_MODE_NONE) {
		err = -EINVAL;
		goto out;
	}
	shared_fdb = mlx5_shared_fdb_supported(ldev);
	err = mlx5_activate_lag(ldev, NULL, MLX5_LAG_MODE_MPESW, shared_fdb);
	if (err)
		mlx5_core_warn(dev, "Failed to create LAG in MPESW mode (%d)\n", err);

out:
	mutex_unlock(&ldev->lock);
	return err;
}

int mlx5_lag_do_mirred(struct mlx5_core_dev *mdev, struct net_device *out_dev)
{
	struct mlx5_lag *ldev = mdev->priv.lag;

	if (!netif_is_bond_master(out_dev) || !ldev)
		return 0;

	mutex_lock(&ldev->lock);
	if (ldev->mode == MLX5_LAG_MODE_MPESW) {
		mutex_unlock(&ldev->lock);
		return -EOPNOTSUPP;
	}
	mutex_unlock(&ldev->lock);
	return 0;
}

bool mlx5_lag_mpesw_is_activated(struct mlx5_core_dev *dev)
{
	bool ret;

	ret = dev->priv.lag && dev->priv.lag->mode == MLX5_LAG_MODE_MPESW;
	return ret;
}

void mlx5_lag_mpesw_init(struct mlx5_lag *ldev)
{
	INIT_WORK(&ldev->mpesw_work, mlx5_mpesw_work);
	atomic_set(&ldev->lag_mpesw.mpesw_rule_count, 0);
}

void mlx5_lag_mpesw_cleanup(struct mlx5_lag *ldev)
{
	cancel_delayed_work_sync(&ldev->bond_work);
}
