/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021 Mellanox Technologies. */

#ifndef _MLX5_ESW_BRIDGE_PRIVATE_
#define _MLX5_ESW_BRIDGE_PRIVATE_

#include <linux/netdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/if_ether.h>
#include <linux/rhashtable.h>
#include <linux/xarray.h>
#include "fs_core.h"

struct mlx5_esw_bridge_fdb_key {
	unsigned char addr[ETH_ALEN];
	u16 vid;
};

enum {
	MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER = BIT(0),
};

struct mlx5_esw_bridge_fdb_entry {
	struct mlx5_esw_bridge_fdb_key key;
	struct rhash_head ht_node;
	struct net_device *dev;
	struct list_head list;
	struct list_head vlan_list;
	u16 vport_num;
	u16 flags;

	struct mlx5_flow_handle *ingress_handle;
	struct mlx5_fc *ingress_counter;
	unsigned long lastuse;
	struct mlx5_flow_handle *egress_handle;
	struct mlx5_flow_handle *filter_handle;
};

struct mlx5_esw_bridge_vlan {
	u16 vid;
	u16 flags;
	struct list_head fdb_list;
	struct mlx5_pkt_reformat *pkt_reformat_push;
	struct mlx5_pkt_reformat *pkt_reformat_pop;
};

struct mlx5_esw_bridge_port {
	u16 vport_num;
	struct xarray vlans;
};

#endif /* _MLX5_ESW_BRIDGE_PRIVATE_ */
