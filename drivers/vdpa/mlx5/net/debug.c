// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/debugfs.h>
#include <linux/mlx5/fs.h>
#include "mlx5_vnet.h"

static int tirn_show(struct seq_file *file, void *priv)
{
	struct mlx5_vdpa_net *ndev = file->private;

	seq_printf(file, "0x%x\n", ndev->res.tirn);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(tirn);

void mlx5_vdpa_remove_tirn(struct mlx5_vdpa_net *ndev)
{
	if (ndev->debugfs)
		debugfs_remove(ndev->res.tirn_dent);
}

void mlx5_vdpa_add_tirn(struct mlx5_vdpa_net *ndev)
{
	ndev->res.tirn_dent = debugfs_create_file("tirn", 0444, ndev->rx_dent,
						  ndev, &tirn_fops);
}

static int rx_flow_table_show(struct seq_file *file, void *priv)
{
	struct mlx5_vdpa_net *ndev = file->private;

	seq_printf(file, "0x%x\n", mlx5_flow_table_id(ndev->rxft));
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(rx_flow_table);

void mlx5_vdpa_remove_rx_flow_table(struct mlx5_vdpa_net *ndev)
{
	if (ndev->debugfs)
		debugfs_remove(ndev->rx_table_dent);
}

void mlx5_vdpa_add_rx_flow_table(struct mlx5_vdpa_net *ndev)
{
	ndev->rx_table_dent = debugfs_create_file("table_id", 0444, ndev->rx_dent,
						  ndev, &rx_flow_table_fops);
}

void mlx5_vdpa_add_debugfs(struct mlx5_vdpa_net *ndev)
{
	struct mlx5_core_dev *mdev;

	mdev = ndev->mvdev.mdev;
	ndev->debugfs = debugfs_create_dir(dev_name(&ndev->mvdev.vdev.dev),
					   mlx5_debugfs_get_dev_root(mdev));
	if (!IS_ERR(ndev->debugfs))
		ndev->rx_dent = debugfs_create_dir("rx", ndev->debugfs);
}

void mlx5_vdpa_remove_debugfs(struct dentry *dbg)
{
	debugfs_remove_recursive(dbg);
}
