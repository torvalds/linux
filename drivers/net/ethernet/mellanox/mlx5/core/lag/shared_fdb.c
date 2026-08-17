// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/netdevice.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/eswitch.h>
#include "mlx5_core.h"
#include "lag.h"
#include "eswitch.h"

bool mlx5_lag_shared_fdb_supported_filter(struct mlx5_lag *ldev, u32 filter)
{
	int idx = mlx5_lag_get_dev_index_by_seq_filter(ldev, MLX5_LAG_P1,
						       filter);
	struct mlx5_core_dev *dev0, *dev;
	bool ret = false;
	int i;

	if (idx < 0)
		return false;

	dev0 = mlx5_lag_pf(ldev, idx)->dev;
	mlx5_lag_for_each(i, 0, ldev, filter) {
		if (i == idx)
			continue;
		dev = mlx5_lag_pf(ldev, i)->dev;
		if (is_mdev_switchdev_mode(dev) &&
		    mlx5_eswitch_vport_match_metadata_enabled(dev->priv.eswitch) &&
		    MLX5_CAP_GEN(dev, lag_native_fdb_selection) &&
		    MLX5_CAP_ESW(dev, root_ft_on_other_esw) &&
		    mlx5_eswitch_is_peer(dev0->priv.eswitch, dev->priv.eswitch))
			continue;
		return false;
	}

	if (is_mdev_switchdev_mode(dev0) &&
	    mlx5_eswitch_vport_match_metadata_enabled(dev0->priv.eswitch) &&
	    mlx5_esw_offloads_devcom_is_ready(dev0->priv.eswitch) &&
	    MLX5_CAP_ESW(dev0, esw_shared_ingress_acl))
		ret = true;

	return ret;
}

bool mlx5_lag_shared_fdb_supported(struct mlx5_lag *ldev)
{
	return mlx5_lag_shared_fdb_supported_filter(ldev,
						    MLX5_LAG_FILTER_PORTS);
}

static int mlx5_lag_create_single_fdb_filter(struct mlx5_lag *ldev, u32 filter)
{
	int master_idx = mlx5_lag_get_dev_index_by_seq_filter(ldev, MLX5_LAG_P1,
							     filter);
	struct mlx5_eswitch *master_esw;
	struct mlx5_core_dev *dev0;
	int i, j;
	int err;

	if (master_idx < 0)
		return -EINVAL;

	dev0 = mlx5_lag_pf(ldev, master_idx)->dev;
	master_esw = dev0->priv.eswitch;
	mlx5_lag_for_each(i, 0, ldev, filter) {
		struct mlx5_eswitch *slave_esw;

		if (i == master_idx)
			continue;

		slave_esw = mlx5_lag_pf(ldev, i)->dev->priv.eswitch;

		err = mlx5_eswitch_offloads_single_fdb_add_one(master_esw,
							       slave_esw,
							       ldev->ports);
		if (err)
			goto err;
	}
	return 0;
err:
	mlx5_lag_for_each_reverse(j, i, 0, ldev, filter) {
		struct mlx5_eswitch *slave_esw;

		if (j == master_idx)
			continue;
		slave_esw = mlx5_lag_pf(ldev, j)->dev->priv.eswitch;
		mlx5_eswitch_offloads_single_fdb_del_one(master_esw, slave_esw);
	}
	return err;
}

int mlx5_lag_create_vport_lag(struct mlx5_lag *ldev, u32 group_id)
{
	u32 filter = group_id ? group_id : MLX5_LAG_FILTER_ALL;
	int master_idx = mlx5_lag_get_dev_index_by_seq_filter(ldev, MLX5_LAG_P1,
							     filter);
	struct mlx5_eswitch *master_esw;
	struct mlx5_core_dev *dev0;
	int i, j;
	int err;

	if (master_idx < 0)
		return -EINVAL;

	dev0 = mlx5_lag_pf(ldev, master_idx)->dev;
	master_esw = dev0->priv.eswitch;

	mlx5_lag_for_each(i, 0, ldev, filter) {
		struct mlx5_eswitch *slave_esw;

		if (i == master_idx)
			continue;

		slave_esw = mlx5_lag_pf(ldev, i)->dev->priv.eswitch;
		err = mlx5_eswitch_offloads_vport_lag_add_one(master_esw,
							      slave_esw);
		if (err)
			goto err;
	}

	return 0;

err:
	mlx5_lag_for_each_reverse(j, i - 1, 0, ldev, filter) {
		struct mlx5_eswitch *slave_esw;

		if (j == master_idx)
			continue;
		slave_esw = mlx5_lag_pf(ldev, j)->dev->priv.eswitch;
		mlx5_eswitch_offloads_vport_lag_del_one(master_esw, slave_esw);
	}
	return err;
}

int mlx5_lag_destroy_vport_lag(struct mlx5_lag *ldev, u32 group_id)
{
	u32 filter = group_id ? group_id : MLX5_LAG_FILTER_ALL;
	int master_idx = mlx5_lag_get_dev_index_by_seq_filter(ldev, MLX5_LAG_P1,
							     filter);
	struct mlx5_eswitch *master_esw;
	struct mlx5_core_dev *dev0;
	int i;

	if (master_idx < 0)
		return 0;

	dev0 = mlx5_lag_pf(ldev, master_idx)->dev;
	master_esw = dev0->priv.eswitch;

	mlx5_lag_for_each(i, 0, ldev, filter) {
		struct mlx5_core_dev *dev;

		if (i == master_idx)
			continue;
		dev = mlx5_lag_pf(ldev, i)->dev;
		mlx5_eswitch_offloads_vport_lag_del_one(master_esw,
							dev->priv.eswitch);
	}
	return 0;
}

static void mlx5_lag_destroy_single_fdb_filter(struct mlx5_lag *ldev,
					       u32 filter)
{
	int master_idx = mlx5_lag_get_dev_index_by_seq_filter(ldev, MLX5_LAG_P1,
							     filter);
	struct mlx5_eswitch *master_esw;
	struct mlx5_eswitch *peer_esw;
	int i;

	if (master_idx < 0)
		return;

	master_esw = mlx5_lag_pf(ldev, master_idx)->dev->priv.eswitch;
	mlx5_lag_for_each(i, 0, ldev, filter) {
		if (i == master_idx)
			continue;

		peer_esw = mlx5_lag_pf(ldev, i)->dev->priv.eswitch;
		mlx5_eswitch_offloads_single_fdb_del_one(master_esw, peer_esw);
	}
}

int mlx5_lag_create_single_fdb(struct mlx5_lag *ldev)
{
	return mlx5_lag_create_single_fdb_filter(ldev, MLX5_LAG_FILTER_ALL);
}

void mlx5_lag_destroy_single_fdb(struct mlx5_lag *ldev)
{
	mlx5_lag_destroy_single_fdb_filter(ldev, MLX5_LAG_FILTER_ALL);
}

/**
 * mlx5_lag_shared_fdb_create - Create shared FDB LAG
 * @ldev: LAG device
 * @tracker: LAG tracker (NULL for SD)
 * @mode: LAG mode (unused for SD)
 * @group_id: SD group ID; 0 (MLX5_LAG_FILTER_PORTS) for ports LAG;
 *            MLX5_LAG_FILTER_ALL for all-device (mpesw) LAG
 *
 * When group_id is 0 (MLX5_LAG_FILTER_PORTS) or MLX5_LAG_FILTER_ALL,
 * activates a FW LAG with shared FDB.
 * When group_id is a specific SD group ID, creates a software-only shared
 * FDB scoped to that group (no FW LAG commands).
 *
 * Return: 0 on success, negative error code on failure.
 */
int mlx5_lag_shared_fdb_create(struct mlx5_lag *ldev,
			       struct lag_tracker *tracker,
			       enum mlx5_lag_mode mode,
			       u32 group_id)
{
	u32 filter = group_id ? group_id : MLX5_LAG_FILTER_ALL;
	int idx = mlx5_lag_get_dev_index_by_seq_filter(ldev, MLX5_LAG_P1,
						       filter);
	struct mlx5_core_dev *dev0;
	struct lag_func *pf;
	int err;
	int i;

	if (idx < 0)
		return -EINVAL;

	dev0 = mlx5_lag_pf(ldev, idx)->dev;

	mlx5_lag_remove_devices_filter(ldev, filter);

	if (filter == MLX5_LAG_FILTER_PORTS || filter == MLX5_LAG_FILTER_ALL) {
		err = mlx5_activate_lag(ldev, tracker, mode, true);
		if (err) {
			mlx5_core_warn(dev0,
				       "Failed to create LAG in shared FDB mode (%d)\n",
				       err);
			goto err_add_devices;
		}
	} else {
		err = mlx5_lag_create_single_fdb_filter(ldev, group_id);
		if (err) {
			mlx5_core_warn(dev0,
				       "Failed to create SD shared FDB (%d)\n",
				       err);
			goto err_add_devices;
		}
		mlx5_lag_for_each(i, 0, ldev, filter) {
			pf = mlx5_lag_pf(ldev, i);
			pf->sd_fdb_active = true;
		}
		BLOCKING_INIT_NOTIFIER_HEAD(&dev0->priv.lag_nh);
	}

	mlx5_lag_rescan_dev_locked(ldev, dev0, true);
	err = mlx5_lag_reload_ib_reps_from_locked(ldev, 0, filter, false);
	if (err) {
		mlx5_core_err(dev0, "Failed to enable lag\n");
		goto err_rescan_drivers;
	}

	if (filter == MLX5_LAG_FILTER_PORTS || filter == MLX5_LAG_FILTER_ALL)
		mlx5_lag_set_vports_agg_speed(ldev);
	return 0;

err_rescan_drivers:
	mlx5_lag_rescan_dev_locked(ldev, dev0, false);
	if (filter == MLX5_LAG_FILTER_PORTS || filter == MLX5_LAG_FILTER_ALL) {
		mlx5_deactivate_lag(ldev);
	} else {
		mlx5_lag_for_each(i, 0, ldev, filter) {
			pf = mlx5_lag_pf(ldev, i);
			pf->sd_fdb_active = false;
		}
		mlx5_lag_destroy_single_fdb_filter(ldev, group_id);
	}
err_add_devices:
	mlx5_lag_add_devices_filter(ldev, filter);
	mlx5_lag_reload_ib_reps_from_locked(ldev, 0, filter, true);
	return err;
}

void mlx5_lag_shared_fdb_destroy(struct mlx5_lag *ldev, u32 group_id)
{
	u32 filter = group_id ? group_id : MLX5_LAG_FILTER_ALL;
	struct lag_func *pf;
	int err;
	int i;

	mlx5_lag_remove_devices_filter(ldev, filter);

	if (filter == MLX5_LAG_FILTER_PORTS || filter == MLX5_LAG_FILTER_ALL) {
		err = mlx5_deactivate_lag(ldev);
		if (err)
			return;
	} else {
		mlx5_lag_for_each(i, 0, ldev, filter) {
			pf = mlx5_lag_pf(ldev, i);
			pf->sd_fdb_active = false;
		}
		mlx5_lag_destroy_single_fdb_filter(ldev, group_id);
		mlx5_lag_unload_reps_from_locked(ldev, filter);
	}

	mlx5_lag_add_devices_filter(ldev, filter);
	mlx5_lag_reload_ib_reps_from_locked(ldev,
					    MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV,
					    filter, true);
}
