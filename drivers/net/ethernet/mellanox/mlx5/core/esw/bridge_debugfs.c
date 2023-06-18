// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/debugfs.h>
#include "bridge.h"
#include "bridge_priv.h"

static void *mlx5_esw_bridge_debugfs_start(struct seq_file *seq, loff_t *pos);
static void *mlx5_esw_bridge_debugfs_next(struct seq_file *seq, void *v, loff_t *pos);
static void mlx5_esw_bridge_debugfs_stop(struct seq_file *seq, void *v);
static int mlx5_esw_bridge_debugfs_show(struct seq_file *seq, void *v);

static const struct seq_operations mlx5_esw_bridge_debugfs_sops = {
	.start	= mlx5_esw_bridge_debugfs_start,
	.next	= mlx5_esw_bridge_debugfs_next,
	.stop	= mlx5_esw_bridge_debugfs_stop,
	.show	= mlx5_esw_bridge_debugfs_show,
};
DEFINE_SEQ_ATTRIBUTE(mlx5_esw_bridge_debugfs);

static void *mlx5_esw_bridge_debugfs_start(struct seq_file *seq, loff_t *pos)
{
	struct mlx5_esw_bridge *bridge = seq->private;

	rtnl_lock();
	return *pos ? seq_list_start(&bridge->fdb_list, *pos - 1) : SEQ_START_TOKEN;
}

static void *mlx5_esw_bridge_debugfs_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct mlx5_esw_bridge *bridge = seq->private;

	return seq_list_next(v == SEQ_START_TOKEN ? &bridge->fdb_list : v, &bridge->fdb_list, pos);
}

static void mlx5_esw_bridge_debugfs_stop(struct seq_file *seq, void *v)
{
	rtnl_unlock();
}

static int mlx5_esw_bridge_debugfs_show(struct seq_file *seq, void *v)
{
	struct mlx5_esw_bridge_fdb_entry *entry;
	u64 packets, bytes, lastuse;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "%-16s %-17s %4s %20s %20s %20s %5s\n",
			   "DEV", "MAC", "VLAN", "PACKETS", "BYTES", "LASTUSE", "FLAGS");
		return 0;
	}

	entry = list_entry(v, struct mlx5_esw_bridge_fdb_entry, list);
	mlx5_fc_query_cached_raw(entry->ingress_counter, &bytes, &packets, &lastuse);
	seq_printf(seq, "%-16s %-17pM %4d %20llu %20llu %20llu %#5x\n",
		   entry->dev->name, entry->key.addr, entry->key.vid, packets, bytes, lastuse,
		   entry->flags);
	return 0;
}

void mlx5_esw_bridge_debugfs_init(struct net_device *br_netdev, struct mlx5_esw_bridge *bridge)
{
	if (!bridge->br_offloads->debugfs_root)
		return;

	bridge->debugfs_dir = debugfs_create_dir(br_netdev->name,
						 bridge->br_offloads->debugfs_root);
	debugfs_create_file("fdb", 0444, bridge->debugfs_dir, bridge,
			    &mlx5_esw_bridge_debugfs_fops);
}

void mlx5_esw_bridge_debugfs_cleanup(struct mlx5_esw_bridge *bridge)
{
	debugfs_remove_recursive(bridge->debugfs_dir);
	bridge->debugfs_dir = NULL;
}

void mlx5_esw_bridge_debugfs_offloads_init(struct mlx5_esw_bridge_offloads *br_offloads)
{
	if (!br_offloads->esw->debugfs_root)
		return;

	br_offloads->debugfs_root = debugfs_create_dir("bridge", br_offloads->esw->debugfs_root);
}

void mlx5_esw_bridge_debugfs_offloads_cleanup(struct mlx5_esw_bridge_offloads *br_offloads)
{
	debugfs_remove_recursive(br_offloads->debugfs_root);
	br_offloads->debugfs_root = NULL;
}
