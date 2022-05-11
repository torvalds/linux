// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "lag.h"

static char *get_str_mode_type(struct mlx5_lag *ldev)
{
	if (ldev->flags & MLX5_LAG_FLAG_ROCE)
		return "roce";
	if (ldev->flags & MLX5_LAG_FLAG_SRIOV)
		return "switchdev";
	if (ldev->flags & MLX5_LAG_FLAG_MULTIPATH)
		return "multipath";

	return NULL;
}

static int type_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	struct mlx5_lag *ldev;
	char *mode = NULL;

	ldev = dev->priv.lag;
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

	ldev = dev->priv.lag;
	mutex_lock(&ldev->lock);
	if (__mlx5_lag_is_active(ldev))
		mode = get_str_port_sel_mode(ldev->flags);
	else
		ret = -EINVAL;
	mutex_unlock(&ldev->lock);
	if (ret || !mode)
		return ret;

	seq_printf(file, "%s\n", mode);
	return 0;
}

static int state_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	struct mlx5_lag *ldev;
	bool active;

	ldev = dev->priv.lag;
	mutex_lock(&ldev->lock);
	active = __mlx5_lag_is_active(ldev);
	mutex_unlock(&ldev->lock);
	seq_printf(file, "%s\n", active ? "active" : "disabled");
	return 0;
}

static int flags_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	struct mlx5_lag *ldev;
	bool shared_fdb;
	bool lag_active;

	ldev = dev->priv.lag;
	mutex_lock(&ldev->lock);
	lag_active = __mlx5_lag_is_active(ldev);
	if (lag_active)
		shared_fdb = ldev->shared_fdb;

	mutex_unlock(&ldev->lock);
	if (!lag_active)
		return -EINVAL;

	seq_printf(file, "%s:%s\n", "shared_fdb", shared_fdb ? "on" : "off");
	return 0;
}

static int mapping_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	u8 ports[MLX5_MAX_PORTS] = {};
	struct mlx5_lag *ldev;
	bool hash = false;
	bool lag_active;
	int num_ports;
	int i;

	ldev = dev->priv.lag;
	mutex_lock(&ldev->lock);
	lag_active = __mlx5_lag_is_active(ldev);
	if (lag_active) {
		if (ldev->flags & MLX5_LAG_FLAG_HASH_BASED) {
			mlx5_infer_tx_enabled(&ldev->tracker, ldev->ports, ports,
					      &num_ports);
			hash = true;
		} else {
			for (i = 0; i < ldev->ports; i++)
				ports[i] = ldev->v2p_map[i];
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

	ldev = dev->priv.lag;
	mutex_lock(&ldev->lock);
	for (i = 0; i < ldev->ports; i++) {
		if (!ldev->pf[i].dev)
			continue;
		seq_printf(file, "%s\n", dev_name(ldev->pf[i].dev->device));
	}
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
