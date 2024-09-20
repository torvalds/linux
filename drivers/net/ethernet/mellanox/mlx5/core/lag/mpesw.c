// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/netdevice.h>
#include <net/nexthop.h>
#include "lag/lag.h"
#include "eswitch.h"
#include "esw/acl/ofld.h"
#include "lib/events.h"

static void mlx5_mpesw_metadata_cleanup(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev;
	struct mlx5_eswitch *esw;
	u32 pf_metadata;
	int i;

	for (i = 0; i < ldev->ports; i++) {
		dev = ldev->pf[i].dev;
		esw = dev->priv.eswitch;
		pf_metadata = ldev->lag_mpesw.pf_metadata[i];
		if (!pf_metadata)
			continue;
		mlx5_esw_acl_ingress_vport_metadata_update(esw, MLX5_VPORT_UPLINK, 0);
		mlx5_notifier_call_chain(dev->priv.events, MLX5_DEV_EVENT_MULTIPORT_ESW,
					 (void *)0);
		mlx5_esw_match_metadata_free(esw, pf_metadata);
		ldev->lag_mpesw.pf_metadata[i] = 0;
	}
}

static int mlx5_mpesw_metadata_set(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev;
	struct mlx5_eswitch *esw;
	u32 pf_metadata;
	int i, err;

	for (i = 0; i < ldev->ports; i++) {
		dev = ldev->pf[i].dev;
		esw = dev->priv.eswitch;
		pf_metadata = mlx5_esw_match_metadata_alloc(esw);
		if (!pf_metadata) {
			err = -ENOSPC;
			goto err_metadata;
		}

		ldev->lag_mpesw.pf_metadata[i] = pf_metadata;
		err = mlx5_esw_acl_ingress_vport_metadata_update(esw, MLX5_VPORT_UPLINK,
								 pf_metadata);
		if (err)
			goto err_metadata;
	}

	for (i = 0; i < ldev->ports; i++) {
		dev = ldev->pf[i].dev;
		mlx5_notifier_call_chain(dev->priv.events, MLX5_DEV_EVENT_MULTIPORT_ESW,
					 (void *)0);
	}

	return 0;

err_metadata:
	mlx5_mpesw_metadata_cleanup(ldev);
	return err;
}

#define MLX5_LAG_MPESW_OFFLOADS_SUPPORTED_PORTS 4
static int enable_mpesw(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	int err;
	int i;

	if (ldev->mode != MLX5_LAG_MODE_NONE)
		return -EINVAL;

	if (ldev->ports > MLX5_LAG_MPESW_OFFLOADS_SUPPORTED_PORTS)
		return -EOPNOTSUPP;

	if (mlx5_eswitch_mode(dev0) != MLX5_ESWITCH_OFFLOADS ||
	    !MLX5_CAP_PORT_SELECTION(dev0, port_select_flow_table) ||
	    !MLX5_CAP_GEN(dev0, create_lag_when_not_master_up) ||
	    !mlx5_lag_check_prereq(ldev))
		return -EOPNOTSUPP;

	err = mlx5_mpesw_metadata_set(ldev);
	if (err)
		return err;

	mlx5_lag_remove_devices(ldev);

	err = mlx5_activate_lag(ldev, NULL, MLX5_LAG_MODE_MPESW, true);
	if (err) {
		mlx5_core_warn(dev0, "Failed to create LAG in MPESW mode (%d)\n", err);
		goto err_add_devices;
	}

	dev0->priv.flags &= ~MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
	mlx5_rescan_drivers_locked(dev0);
	for (i = 0; i < ldev->ports; i++) {
		err = mlx5_eswitch_reload_ib_reps(ldev->pf[i].dev->priv.eswitch);
		if (err)
			goto err_rescan_drivers;
	}

	return 0;

err_rescan_drivers:
	dev0->priv.flags |= MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
	mlx5_rescan_drivers_locked(dev0);
	mlx5_deactivate_lag(ldev);
err_add_devices:
	mlx5_lag_add_devices(ldev);
	for (i = 0; i < ldev->ports; i++)
		mlx5_eswitch_reload_ib_reps(ldev->pf[i].dev->priv.eswitch);
	mlx5_mpesw_metadata_cleanup(ldev);
	return err;
}

static void disable_mpesw(struct mlx5_lag *ldev)
{
	if (ldev->mode == MLX5_LAG_MODE_MPESW) {
		mlx5_mpesw_metadata_cleanup(ldev);
		mlx5_disable_lag(ldev);
	}
}

static void mlx5_mpesw_work(struct work_struct *work)
{
	struct mlx5_mpesw_work_st *mpesww = container_of(work, struct mlx5_mpesw_work_st, work);
	struct mlx5_devcom_comp_dev *devcom;
	struct mlx5_lag *ldev = mpesww->lag;

	devcom = mlx5_lag_get_devcom_comp(ldev);
	if (!devcom)
		return;

	mlx5_devcom_comp_lock(devcom);
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
	mlx5_devcom_comp_unlock(devcom);
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
EXPORT_SYMBOL(mlx5_lag_is_mpesw);
