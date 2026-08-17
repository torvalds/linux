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

	mlx5_lag_for_each(i, 0, ldev, MLX5_LAG_FILTER_ALL) {
		dev = mlx5_lag_pf(ldev, i)->dev;
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

	mlx5_lag_for_each(i, 0, ldev, MLX5_LAG_FILTER_ALL) {
		dev = mlx5_lag_pf(ldev, i)->dev;
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

	mlx5_lag_for_each(i, 0, ldev, MLX5_LAG_FILTER_ALL) {
		dev = mlx5_lag_pf(ldev, i)->dev;
		mlx5_notifier_call_chain(dev->priv.events, MLX5_DEV_EVENT_MULTIPORT_ESW,
					 (void *)0);
	}

	return 0;

err_metadata:
	mlx5_mpesw_metadata_cleanup(ldev);
	return err;
}

static void mlx5_mpesw_restore_sd_fdb(struct mlx5_lag *ldev)
{
	struct lag_func *pf;
	int err, i;

	mlx5_ldev_for_each(i, 0, ldev) {
		pf = mlx5_lag_pf(ldev, i);
		err = mlx5_lag_shared_fdb_create(ldev, NULL, 0, pf->group_id);
		if (err)
			mlx5_core_warn(pf->dev,
				       "Failed to restore SD shared FDB (%d)\n",
				       err);
	}
}

static int mlx5_mpesw_teardown_sd_fdb(struct mlx5_lag *ldev)
{
	struct lag_func *pf;
	int i;

	mlx5_ldev_for_each(i, 0, ldev) {
		pf = mlx5_lag_pf(ldev, i);
		if (!pf->sd_fdb_active)
			continue;
		mlx5_lag_shared_fdb_destroy(ldev, pf->group_id);
	}
	return 0;
}

static bool mlx5_lag_has_sd_group(struct mlx5_lag *ldev)
{
	struct lag_func *pf;
	int i;

	mlx5_ldev_for_each(i, 0, ldev) {
		pf = mlx5_lag_pf(ldev, i);
		if (pf->group_id)
			return true;
	}
	return false;
}

static int mlx5_lag_enable_mpesw(struct mlx5_lag *ldev)
{
	int idx = mlx5_lag_get_dev_index_by_seq(ldev, MLX5_LAG_P1);
	struct mlx5_core_dev *dev0;
	int err;

	if (ldev->mode == MLX5_LAG_MODE_MPESW)
		return 0;

	if (ldev->mode != MLX5_LAG_MODE_NONE)
		return -EINVAL;

	if (idx < 0)
		return -EINVAL;

	dev0 = mlx5_lag_pf(ldev, idx)->dev;
	if (mlx5_eswitch_mode(dev0) != MLX5_ESWITCH_OFFLOADS ||
	    !MLX5_CAP_PORT_SELECTION(dev0, port_select_flow_table) ||
	    !MLX5_CAP_GEN(dev0, create_lag_when_not_master_up) ||
	    !mlx5_lag_check_prereq(ldev) ||
	    !mlx5_lag_shared_fdb_supported_filter(ldev, MLX5_LAG_FILTER_ALL))
		return -EOPNOTSUPP;

	err = mlx5_mpesw_metadata_set(ldev);
	if (err)
		return err;

	if (mlx5_lag_has_sd_group(ldev))
		mlx5_mpesw_teardown_sd_fdb(ldev);

	err = mlx5_lag_shared_fdb_create(ldev, NULL, MLX5_LAG_MODE_MPESW,
					 MLX5_LAG_FILTER_ALL);
	if (err) {
		mlx5_core_warn(dev0,
			       "Failed to create LAG in MPESW mode (%d)\n",
			       err);
		if (mlx5_lag_has_sd_group(ldev))
			mlx5_mpesw_restore_sd_fdb(ldev);
		mlx5_mpesw_metadata_cleanup(ldev);
		return err;
	}

	return 0;
}

void mlx5_lag_disable_mpesw(struct mlx5_lag *ldev)
{
	if (ldev->mode != MLX5_LAG_MODE_MPESW)
		return;

	mlx5_mpesw_metadata_cleanup(ldev);
	mlx5_lag_shared_fdb_destroy(ldev, MLX5_LAG_FILTER_ALL);
	if (mlx5_lag_has_sd_group(ldev))
		mlx5_mpesw_restore_sd_fdb(ldev);
}

void mlx5_mpesw_sd_devcoms_lock(struct mlx5_lag *ldev)
{
	struct mlx5_devcom_comp_dev *sd_devcom;
	int i;

	mlx5_ldev_for_each(i, 0, ldev) {
		sd_devcom = mlx5_sd_get_devcom(mlx5_lag_pf(ldev, i)->dev);
		if (sd_devcom)
			mlx5_devcom_comp_lock(sd_devcom);
	}
}

void mlx5_mpesw_sd_devcoms_unlock(struct mlx5_lag *ldev)
{
	struct mlx5_devcom_comp_dev *sd_devcom;
	int i;

	mlx5_ldev_for_each_reverse(i, MLX5_MAX_PORTS, 0, ldev) {
		sd_devcom = mlx5_sd_get_devcom(mlx5_lag_pf(ldev, i)->dev);
		if (sd_devcom)
			mlx5_devcom_comp_unlock(sd_devcom);
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
	mlx5_mpesw_sd_devcoms_lock(ldev);
	mutex_lock(&ldev->lock);
	if (ldev->mode_changes_in_progress) {
		mpesww->result = -EAGAIN;
		goto unlock;
	}

	if (mpesww->op == MLX5_MPESW_OP_ENABLE)
		mpesww->result = mlx5_lag_enable_mpesw(ldev);
	else if (mpesww->op == MLX5_MPESW_OP_DISABLE)
		mlx5_lag_disable_mpesw(ldev);
unlock:
	mutex_unlock(&ldev->lock);
	mlx5_mpesw_sd_devcoms_unlock(ldev);
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

	work = kzalloc_obj(*work);
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

	return ldev && ldev->mode == MLX5_LAG_MODE_MPESW &&
	       __mlx5_lag_dev_is_port(ldev, dev);
}
EXPORT_SYMBOL(mlx5_lag_is_mpesw);

void mlx5_mpesw_speed_update_work(struct work_struct *work)
{
	struct mlx5_lag *ldev = container_of(work, struct mlx5_lag,
					     speed_update_work);

	mutex_lock(&ldev->lock);
	if (ldev->mode == MLX5_LAG_MODE_MPESW) {
		if (ldev->mode_changes_in_progress)
			queue_work(ldev->wq, &ldev->speed_update_work);
		else
			mlx5_lag_set_vports_agg_speed(ldev);
	}

	mutex_unlock(&ldev->lock);
}

int mlx5_lag_mpesw_port_change_event(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct mlx5_nb *mlx5_nb = container_of(nb, struct mlx5_nb, nb);
	struct lag_func *lag_func = container_of(mlx5_nb,
						 struct lag_func,
						 port_change_nb);
	struct mlx5_core_dev *dev = lag_func->dev;
	struct mlx5_lag *ldev = dev->priv.lag;
	struct mlx5_eqe *eqe = data;

	if (!ldev)
		return NOTIFY_DONE;

	if (eqe->sub_type == MLX5_PORT_CHANGE_SUBTYPE_DOWN ||
	    eqe->sub_type == MLX5_PORT_CHANGE_SUBTYPE_ACTIVE)
		queue_work(ldev->wq, &ldev->speed_update_work);

	return NOTIFY_OK;
}
