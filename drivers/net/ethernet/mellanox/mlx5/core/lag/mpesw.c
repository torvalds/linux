// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/netdevice.h>
#include <net/nexthop.h>
#include "lag/lag.h"
#include "eswitch.h"
#include "lib/mlx5.h"

static int enable_mpesw(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev = ldev->pf[MLX5_LAG_P1].dev;
	int err;

	if (ldev->mode != MLX5_LAG_MODE_NONE)
		return -EINVAL;

	if (mlx5_eswitch_mode(dev) != MLX5_ESWITCH_OFFLOADS ||
	    !MLX5_CAP_PORT_SELECTION(dev, port_select_flow_table) ||
	    !MLX5_CAP_GEN(dev, create_lag_when_not_master_up) ||
	    !mlx5_lag_check_prereq(ldev))
		return -EOPNOTSUPP;

	err = mlx5_activate_lag(ldev, NULL, MLX5_LAG_MODE_MPESW, false);
	if (err) {
		mlx5_core_warn(dev, "Failed to create LAG in MPESW mode (%d)\n", err);
		goto out_err;
	}

	return 0;

out_err:
	return err;
}

static void disable_mpesw(struct mlx5_lag *ldev)
{
	if (ldev->mode == MLX5_LAG_MODE_MPESW)
		mlx5_disable_lag(ldev);
}

static void mlx5_mpesw_work(struct work_struct *work)
{
	struct mlx5_mpesw_work_st *mpesww = container_of(work, struct mlx5_mpesw_work_st, work);
	struct mlx5_lag *ldev = mpesww->lag;

	mutex_lock(&ldev->lock);
	if (ldev->mode_changes_in_progress) {
		mpesww->result = -EAGAIN;
		goto unlock;
	}

	if (mpesww->op == MLX5_MPESW_OP_ENABLE)
		mpesww->result = enable_mpesw(ldev);
	else if (mpesww->op == MLX5_MPESW_OP_DISABLE)
		disable_mpesw(ldev);
unlock:
	mutex_unlock(&ldev->lock);
	complete(&mpesww->comp);
}

static int mlx5_lag_mpesw_queue_work(struct mlx5_core_dev *dev,
				     enum mpesw_op op)
{
	struct mlx5_lag *ldev = mlx5_lag_dev(dev);
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

void mlx5_lag_mpesw_disable(struct mlx5_core_dev *dev)
{
	mlx5_lag_mpesw_queue_work(dev, MLX5_MPESW_OP_DISABLE);
}

int mlx5_lag_mpesw_enable(struct mlx5_core_dev *dev)
{
	return mlx5_lag_mpesw_queue_work(dev, MLX5_MPESW_OP_ENABLE);
}

int mlx5_lag_mpesw_do_mirred(struct mlx5_core_dev *mdev,
			     struct net_device *out_dev,
			     struct netlink_ext_ack *extack)
{
	struct mlx5_lag *ldev = mlx5_lag_dev(mdev);

	if (!netif_is_bond_master(out_dev) || !ldev)
		return 0;

	if (ldev->mode != MLX5_LAG_MODE_MPESW)
		return 0;

	NL_SET_ERR_MSG_MOD(extack, "can't forward to bond in mpesw mode");
	return -EOPNOTSUPP;
}

bool mlx5_lag_is_mpesw(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev = mlx5_lag_dev(dev);

	return ldev && ldev->mode == MLX5_LAG_MODE_MPESW;
}
