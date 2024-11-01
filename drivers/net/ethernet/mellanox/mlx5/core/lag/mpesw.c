// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/netdevice.h>
#include <net/nexthop.h>
#include "lag/lag.h"
#include "eswitch.h"
#include "lib/mlx5.h"

static int add_mpesw_rule(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev = ldev->pf[MLX5_LAG_P1].dev;
	int err;

	if (atomic_add_return(1, &ldev->lag_mpesw.mpesw_rule_count) != 1)
		return 0;

	if (ldev->mode != MLX5_LAG_MODE_NONE) {
		err = -EINVAL;
		goto out_err;
	}

	err = mlx5_activate_lag(ldev, NULL, MLX5_LAG_MODE_MPESW, false);
	if (err) {
		mlx5_core_warn(dev, "Failed to create LAG in MPESW mode (%d)\n", err);
		goto out_err;
	}

	return 0;

out_err:
	atomic_dec(&ldev->lag_mpesw.mpesw_rule_count);
	return err;
}

static void del_mpesw_rule(struct mlx5_lag *ldev)
{
	if (!atomic_dec_return(&ldev->lag_mpesw.mpesw_rule_count) &&
	    ldev->mode == MLX5_LAG_MODE_MPESW)
		mlx5_disable_lag(ldev);
}

static void mlx5_mpesw_work(struct work_struct *work)
{
	struct mlx5_mpesw_work_st *mpesww = container_of(work, struct mlx5_mpesw_work_st, work);
	struct mlx5_lag *ldev = mpesww->lag;

	mutex_lock(&ldev->lock);
	if (mpesww->op == MLX5_MPESW_OP_ENABLE)
		mpesww->result = add_mpesw_rule(ldev);
	else if (mpesww->op == MLX5_MPESW_OP_DISABLE)
		del_mpesw_rule(ldev);
	mutex_unlock(&ldev->lock);

	complete(&mpesww->comp);
}

static int mlx5_lag_mpesw_queue_work(struct mlx5_core_dev *dev,
				     enum mpesw_op op)
{
	struct mlx5_lag *ldev = dev->priv.lag;
	struct mlx5_mpesw_work_st *work;
	int err = 0;

	if (!ldev)
		return 0;

	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	INIT_WORK(&work->work, mlx5_mpesw_work);
	init_completion(&work->comp);
	work->op = op;
	work->lag = ldev;

	if (!queue_work(ldev->wq, &work->work)) {
		mlx5_core_warn(dev, "failed to queue mpesw work\n");
		err = -EINVAL;
		goto out;
	}
	wait_for_completion(&work->comp);
	err = work->result;
out:
	kfree(work);
	return err;
}

void mlx5_lag_del_mpesw_rule(struct mlx5_core_dev *dev)
{
	mlx5_lag_mpesw_queue_work(dev, MLX5_MPESW_OP_DISABLE);
}

int mlx5_lag_add_mpesw_rule(struct mlx5_core_dev *dev)
{
	return mlx5_lag_mpesw_queue_work(dev, MLX5_MPESW_OP_ENABLE);
}

int mlx5_lag_do_mirred(struct mlx5_core_dev *mdev, struct net_device *out_dev)
{
	struct mlx5_lag *ldev = mdev->priv.lag;

	if (!netif_is_bond_master(out_dev) || !ldev)
		return 0;

	if (ldev->mode == MLX5_LAG_MODE_MPESW)
		return -EOPNOTSUPP;

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
	atomic_set(&ldev->lag_mpesw.mpesw_rule_count, 0);
}

void mlx5_lag_mpesw_cleanup(struct mlx5_lag *ldev)
{
	WARN_ON(atomic_read(&ldev->lag_mpesw.mpesw_rule_count));
}
