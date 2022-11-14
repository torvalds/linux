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

#if defined(CONFIG_MLX5_VDPA_STEERING_DEBUG)
static int packets_show(struct seq_file *file, void *priv)
{
	struct mlx5_vdpa_counter *counter = file->private;
	u64 packets;
	u64 bytes;
	int err;

	err = mlx5_fc_query(counter->mdev, counter->counter, &packets, &bytes);
	if (err)
		return err;

	seq_printf(file, "0x%llx\n", packets);
	return 0;
}

static int bytes_show(struct seq_file *file, void *priv)
{
	struct mlx5_vdpa_counter *counter = file->private;
	u64 packets;
	u64 bytes;
	int err;

	err = mlx5_fc_query(counter->mdev, counter->counter, &packets, &bytes);
	if (err)
		return err;

	seq_printf(file, "0x%llx\n", bytes);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(packets);
DEFINE_SHOW_ATTRIBUTE(bytes);

static void add_counter_node(struct mlx5_vdpa_counter *counter,
			     struct dentry *parent)
{
	debugfs_create_file("packets", 0444, parent, counter,
			    &packets_fops);
	debugfs_create_file("bytes", 0444, parent, counter,
			    &bytes_fops);
}

void mlx5_vdpa_add_rx_counters(struct mlx5_vdpa_net *ndev,
			       struct macvlan_node *node)
{
	static const char *ut = "untagged";
	char vidstr[9];
	u16 vid;

	node->ucast_counter.mdev = ndev->mvdev.mdev;
	node->mcast_counter.mdev = ndev->mvdev.mdev;
	if (node->tagged) {
		vid = key2vid(node->macvlan);
		snprintf(vidstr, sizeof(vidstr), "0x%x", vid);
	} else {
		strcpy(vidstr, ut);
	}

	node->dent = debugfs_create_dir(vidstr, ndev->rx_dent);
	if (IS_ERR(node->dent)) {
		node->dent = NULL;
		return;
	}

	node->ucast_counter.dent = debugfs_create_dir("ucast", node->dent);
	if (IS_ERR(node->ucast_counter.dent))
		return;

	add_counter_node(&node->ucast_counter, node->ucast_counter.dent);

	node->mcast_counter.dent = debugfs_create_dir("mcast", node->dent);
	if (IS_ERR(node->mcast_counter.dent))
		return;

	add_counter_node(&node->mcast_counter, node->mcast_counter.dent);
}

void mlx5_vdpa_remove_rx_counters(struct mlx5_vdpa_net *ndev,
				  struct macvlan_node *node)
{
	if (node->dent && ndev->debugfs)
		debugfs_remove_recursive(node->dent);
}
#endif

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
