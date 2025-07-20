// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "lag.h"

static char *get_str_mode_type(struct mlx5_lag *ldev)
{
	switch (ldev->mode) {
	case MLX5_LAG_MODE_ROCE: return "roce";
	case MLX5_LAG_MODE_SRIOV: return "switchdev";
	case MLX5_LAG_MODE_MULTIPATH: return "multipath";
	case MLX5_LAG_MODE_MPESW: return "multiport_eswitch";
	default: return "invalid";
	}

	return NULL;
}

static int type_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	struct mlx5_lag *ldev;
	char *mode = NULL;

	ldev = mlx5_lag_dev(dev);
	mutex_lock(&ldev->lock);
	if (__mlx5_lag_is_active(ldev))
		mode = get_str_mode_type(ldev);
	mutex_unlock(&ldev->lock);
	if (!mode)
		return -EINVAL;
	seq_printf(file, "%s\n", mode);

	return 0;
}

static int port_sel_mode_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	struct mlx5_lag *ldev;
	int ret = 0;
	char *mode;

	ldev = mlx5_lag_dev(dev);
	mutex_lock(&ldev->lock);
	if (__mlx5_lag_is_active(ldev))
		mode = mlx5_get_str_port_sel_mode(ldev->mode, ldev->mode_flags);
	else
		ret = -EINVAL;
	mutex_unlock(&ldev->lock);
	if (ret)
		return ret;

	seq_printf(file, "%s\n", mode);
	return 0;
}

static int state_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	struct mlx5_lag *ldev;
	bool active;

	ldev = mlx5_lag_dev(dev);
	mutex_lock(&ldev->lock);
	active = __mlx5_lag_is_active(ldev);
	mutex_unlock(&ldev->lock);
	seq_printf(file, "%s\n", active ? "active" : "disabled");
	return 0;
}

static int flags_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	bool fdb_sel_mode_native;
	struct mlx5_lag *ldev;
	bool shared_fdb;
	bool lag_active;

	ldev = mlx5_lag_dev(dev);
	mutex_lock(&ldev->lock);
	lag_active = __mlx5_lag_is_active(ldev);
	if (!lag_active)
		goto unlock;

	shared_fdb = test_bit(MLX5_LAG_MODE_FLAG_SHARED_FDB, &ldev->mode_flags);
	fdb_sel_mode_native = test_bit(MLX5_LAG_MODE_FLAG_FDB_SEL_MODE_NATIVE,
				       &ldev->mode_flags);

unlock:
	mutex_unlock(&ldev->lock);
	if (!lag_active)
		return -EINVAL;

	seq_printf(file, "%s:%s\n", "shared_fdb", shared_fdb ? "on" : "off");
	seq_printf(file, "%s:%s\n", "fdb_selection_mode",
		   fdb_sel_mode_native ? "native" : "affinity");
	return 0;
}

static int mapping_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	u8 ports[MLX5_MAX_PORTS] = {};
	struct mlx5_lag *ldev;
	bool hash = false;
	bool lag_active;
	int i, idx = 0;
	int num_ports;

	ldev = mlx5_lag_dev(dev);
	mutex_lock(&ldev->lock);
	lag_active = __mlx5_lag_is_active(ldev);
	if (lag_active) {
		if (test_bit(MLX5_LAG_MODE_FLAG_HASH_BASED, &ldev->mode_flags)) {
			mlx5_infer_tx_enabled(&ldev->tracker, ldev, ports,
					      &num_ports);
			hash = true;
		} else {
			mlx5_ldev_for_each(i, 0, ldev)
				ports[idx++] = ldev->v2p_map[i];
			num_ports = ldev->ports;
		}
	}
	mutex_unlock(&ldev->lock);
	if (!lag_active)
		return -EINVAL;

	for (i = 0; i < num_ports; i++) {
		if (hash)
			seq_printf(file, "%d\n", ports[i] + 1);
		else
			seq_printf(file, "%d:%d\n", i + 1, ports[i]);
	}

	return 0;
}

static int members_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	struct mlx5_lag *ldev;
	int i;

	ldev = mlx5_lag_dev(dev);
	mutex_lock(&ldev->lock);
	mlx5_ldev_for_each(i, 0, ldev)
		seq_printf(file, "%s\n", dev_name(ldev->pf[i].dev->device));
	mutex_unlock(&ldev->lock);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(type);
DEFINE_SHOW_ATTRIBUTE(port_sel_mode);
DEFINE_SHOW_ATTRIBUTE(state);
DEFINE_SHOW_ATTRIBUTE(flags);
DEFINE_SHOW_ATTRIBUTE(mapping);
DEFINE_SHOW_ATTRIBUTE(members);

void mlx5_ldev_add_debugfs(struct mlx5_core_dev *dev)
{
	struct dentry *dbg;

	dbg = debugfs_create_dir("lag", mlx5_debugfs_get_dev_root(dev));
	dev->priv.dbg.lag_debugfs = dbg;

	debugfs_create_file("type", 0444, dbg, dev, &type_fops);
	debugfs_create_file("port_sel_mode", 0444, dbg, dev, &port_sel_mode_fops);
	debugfs_create_file("state", 0444, dbg, dev, &state_fops);
	debugfs_create_file("flags", 0444, dbg, dev, &flags_fops);
	debugfs_create_file("mapping", 0444, dbg, dev, &mapping_fops);
	debugfs_create_file("members", 0444, dbg, dev, &members_fops);
}

void mlx5_ldev_remove_debugfs(struct dentry *dbg)
{
	debugfs_remove_recursive(dbg);
}
