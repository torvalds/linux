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

#define MLX5_ESW_BRIDGE_INGRESS_TABLE_IGMP_GRP_SIZE 1
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MLD_GRP_SIZE 3
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_SIZE 131072
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_UNTAGGED_GRP_SIZE			\
	(524288 - MLX5_ESW_BRIDGE_INGRESS_TABLE_IGMP_GRP_SIZE -		\
	 MLX5_ESW_BRIDGE_INGRESS_TABLE_MLD_GRP_SIZE)

#define MLX5_ESW_BRIDGE_INGRESS_TABLE_IGMP_GRP_IDX_FROM 0
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_IGMP_GRP_IDX_TO		\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_IGMP_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MLD_GRP_IDX_FROM	\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_IGMP_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MLD_GRP_IDX_TO		\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_MLD_GRP_IDX_FROM +	\
	 MLX5_ESW_BRIDGE_INGRESS_TABLE_MLD_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_FROM			\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_MLD_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_TO			\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_FROM +		\
	 MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_FILTER_GRP_IDX_FROM	\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_FILTER_GRP_IDX_TO		\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_FILTER_GRP_IDX_FROM +	\
	 MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_GRP_IDX_FROM			\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_FILTER_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_GRP_IDX_TO			\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_GRP_IDX_FROM +		\
	 MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_FILTER_GRP_IDX_FROM	\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_FILTER_GRP_IDX_TO		\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_FILTER_GRP_IDX_FROM +	\
	 MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_FROM			\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_FILTER_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_TO			\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_FROM +		\
	 MLX5_ESW_BRIDGE_INGRESS_TABLE_UNTAGGED_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE			\
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_TO + 1)
static_assert(MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE == 1048576);

#define MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_SIZE 131072
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_SIZE (262144 - 1)
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_IDX_FROM 0
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_IDX_TO		\
	(MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_QINQ_GRP_IDX_FROM		\
	(MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_QINQ_GRP_IDX_TO			\
	(MLX5_ESW_BRIDGE_EGRESS_TABLE_QINQ_GRP_IDX_FROM +		\
	 MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_FROM \
	(MLX5_ESW_BRIDGE_EGRESS_TABLE_QINQ_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_TO			\
	(MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_FROM +		\
	 MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MISS_GRP_IDX_FROM \
	(MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MISS_GRP_IDX_TO	\
	MLX5_ESW_BRIDGE_EGRESS_TABLE_MISS_GRP_IDX_FROM
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE			\
	(MLX5_ESW_BRIDGE_EGRESS_TABLE_MISS_GRP_IDX_TO + 1)
static_assert(MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE == 524288);

#define MLX5_ESW_BRIDGE_SKIP_TABLE_SIZE 0

#define MLX5_ESW_BRIDGE_MCAST_TABLE_FILTER_GRP_SIZE 1
#define MLX5_ESW_BRIDGE_MCAST_TABLE_FWD_GRP_SIZE 1
#define MLX5_ESW_BRIDGE_MCAST_TABLE_VLAN_GRP_SIZE 4095
#define MLX5_ESW_BRIDGE_MCAST_TABLE_QINQ_GRP_SIZE MLX5_ESW_BRIDGE_MCAST_TABLE_VLAN_GRP_SIZE
#define MLX5_ESW_BRIDGE_MCAST_TABLE_FILTER_GRP_IDX_FROM 0
#define MLX5_ESW_BRIDGE_MCAST_TABLE_FILTER_GRP_IDX_TO		\
	(MLX5_ESW_BRIDGE_MCAST_TABLE_FILTER_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_MCAST_TABLE_VLAN_GRP_IDX_FROM		\
	(MLX5_ESW_BRIDGE_MCAST_TABLE_FILTER_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_MCAST_TABLE_VLAN_GRP_IDX_TO			\
	(MLX5_ESW_BRIDGE_MCAST_TABLE_VLAN_GRP_IDX_FROM +		\
	 MLX5_ESW_BRIDGE_MCAST_TABLE_VLAN_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_MCAST_TABLE_QINQ_GRP_IDX_FROM		\
	(MLX5_ESW_BRIDGE_MCAST_TABLE_VLAN_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_MCAST_TABLE_QINQ_GRP_IDX_TO			\
	(MLX5_ESW_BRIDGE_MCAST_TABLE_QINQ_GRP_IDX_FROM +		\
	 MLX5_ESW_BRIDGE_MCAST_TABLE_QINQ_GRP_SIZE - 1)
#define MLX5_ESW_BRIDGE_MCAST_TABLE_FWD_GRP_IDX_FROM		\
	(MLX5_ESW_BRIDGE_MCAST_TABLE_QINQ_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_MCAST_TABLE_FWD_GRP_IDX_TO			\
	(MLX5_ESW_BRIDGE_MCAST_TABLE_FWD_GRP_IDX_FROM +			\
	 MLX5_ESW_BRIDGE_MCAST_TABLE_FWD_GRP_SIZE - 1)

#define MLX5_ESW_BRIDGE_MCAST_TABLE_SIZE			\
	(MLX5_ESW_BRIDGE_MCAST_TABLE_FWD_GRP_IDX_TO + 1)
static_assert(MLX5_ESW_BRIDGE_MCAST_TABLE_SIZE == 8192);

enum {
	MLX5_ESW_BRIDGE_LEVEL_INGRESS_TABLE,
	MLX5_ESW_BRIDGE_LEVEL_EGRESS_TABLE,
	MLX5_ESW_BRIDGE_LEVEL_MCAST_TABLE,
	MLX5_ESW_BRIDGE_LEVEL_SKIP_TABLE,
};

enum {
	MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG = BIT(0),
	MLX5_ESW_BRIDGE_MCAST_FLAG = BIT(1),
};

struct mlx5_esw_bridge_fdb_key {
	unsigned char addr[ETH_ALEN];
	u16 vid;
};

struct mlx5_esw_bridge_mdb_key {
	unsigned char addr[ETH_ALEN];
	u16 vid;
};

enum {
	MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER = BIT(0),
	MLX5_ESW_BRIDGE_FLAG_PEER = BIT(1),
	MLX5_ESW_BRIDGE_FLAG_DELETED = BIT(2),
};

enum {
	MLX5_ESW_BRIDGE_PORT_FLAG_PEER = BIT(0),
};

struct mlx5_esw_bridge_fdb_entry {
	struct mlx5_esw_bridge_fdb_key key;
	struct rhash_head ht_node;
	struct net_device *dev;
	struct list_head list;
	struct list_head vlan_list;
	u16 vport_num;
	u16 esw_owner_vhca_id;
	u16 flags;

	struct mlx5_flow_handle *ingress_handle;
	struct mlx5_fc *ingress_counter;
	unsigned long lastuse;
	struct mlx5_flow_handle *egress_handle;
	struct mlx5_flow_handle *filter_handle;
};

struct mlx5_esw_bridge_mdb_entry {
	struct mlx5_esw_bridge_mdb_key key;
	struct rhash_head ht_node;
	struct list_head list;
	struct xarray ports;
	int num_ports;

	struct mlx5_flow_handle *egress_handle;
};

struct mlx5_esw_bridge_vlan {
	u16 vid;
	u16 flags;
	struct list_head fdb_list;
	struct mlx5_pkt_reformat *pkt_reformat_push;
	struct mlx5_pkt_reformat *pkt_reformat_pop;
	struct mlx5_modify_hdr *pkt_mod_hdr_push_mark;
	struct mlx5_flow_handle *mcast_handle;
};

struct mlx5_esw_bridge_port {
	u16 vport_num;
	u16 esw_owner_vhca_id;
	u16 flags;
	struct mlx5_esw_bridge *bridge;
	struct xarray vlans;
	struct {
		struct mlx5_flow_table *ft;
		struct mlx5_flow_group *filter_fg;
		struct mlx5_flow_group *vlan_fg;
		struct mlx5_flow_group *qinq_fg;
		struct mlx5_flow_group *fwd_fg;

		struct mlx5_flow_handle *filter_handle;
		struct mlx5_flow_handle *fwd_handle;
	} mcast;
};

struct mlx5_esw_bridge {
	int ifindex;
	int refcnt;
	struct list_head list;
	struct mlx5_esw_bridge_offloads *br_offloads;
	struct dentry *debugfs_dir;

	struct list_head fdb_list;
	struct rhashtable fdb_ht;

	struct list_head mdb_list;
	struct rhashtable mdb_ht;

	struct mlx5_flow_table *egress_ft;
	struct mlx5_flow_group *egress_vlan_fg;
	struct mlx5_flow_group *egress_qinq_fg;
	struct mlx5_flow_group *egress_mac_fg;
	struct mlx5_flow_group *egress_miss_fg;
	struct mlx5_pkt_reformat *egress_miss_pkt_reformat;
	struct mlx5_flow_handle *egress_miss_handle;
	unsigned long ageing_time;
	u32 flags;
	u16 vlan_proto;
};

struct mlx5_flow_table *mlx5_esw_bridge_table_create(int max_fte, u32 level,
						     struct mlx5_eswitch *esw);
unsigned long mlx5_esw_bridge_port_key(struct mlx5_esw_bridge_port *port);

int mlx5_esw_bridge_port_mcast_init(struct mlx5_esw_bridge_port *port);
void mlx5_esw_bridge_port_mcast_cleanup(struct mlx5_esw_bridge_port *port);
int mlx5_esw_bridge_vlan_mcast_init(u16 vlan_proto, struct mlx5_esw_bridge_port *port,
				    struct mlx5_esw_bridge_vlan *vlan);
void mlx5_esw_bridge_vlan_mcast_cleanup(struct mlx5_esw_bridge_vlan *vlan);

int mlx5_esw_bridge_mcast_enable(struct mlx5_esw_bridge *bridge);
void mlx5_esw_bridge_mcast_disable(struct mlx5_esw_bridge *bridge);

int mlx5_esw_bridge_mdb_init(struct mlx5_esw_bridge *bridge);
void mlx5_esw_bridge_mdb_cleanup(struct mlx5_esw_bridge *bridge);
int mlx5_esw_bridge_port_mdb_attach(struct net_device *dev, struct mlx5_esw_bridge_port *port,
				    const unsigned char *addr, u16 vid);
void mlx5_esw_bridge_port_mdb_detach(struct net_device *dev, struct mlx5_esw_bridge_port *port,
				     const unsigned char *addr, u16 vid);
void mlx5_esw_bridge_port_mdb_vlan_flush(struct mlx5_esw_bridge_port *port,
					 struct mlx5_esw_bridge_vlan *vlan);
void mlx5_esw_bridge_mdb_flush(struct mlx5_esw_bridge *bridge);

void mlx5_esw_bridge_debugfs_offloads_init(struct mlx5_esw_bridge_offloads *br_offloads);
void mlx5_esw_bridge_debugfs_offloads_cleanup(struct mlx5_esw_bridge_offloads *br_offloads);
void mlx5_esw_bridge_debugfs_init(struct net_device *br_netdev, struct mlx5_esw_bridge *bridge);
void mlx5_esw_bridge_debugfs_cleanup(struct mlx5_esw_bridge *bridge);

#endif /* _MLX5_ESW_BRIDGE_PRIVATE_ */
