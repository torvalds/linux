// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include <linux/netdevice.h>
#include <linux/list.h>
#include <net/switchdev.h>
#include "bridge.h"
#include "eswitch.h"
#include "fs_core.h"

#define MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE 64000
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_FROM 0
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_TO (MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE - 1)

#define MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE 64000
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_FROM 0
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_TO (MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE - 1)

enum {
	MLX5_ESW_BRIDGE_LEVEL_INGRESS_TABLE,
	MLX5_ESW_BRIDGE_LEVEL_EGRESS_TABLE,
};

struct mlx5_esw_bridge {
	int ifindex;
	int refcnt;
	struct list_head list;

	struct mlx5_flow_table *egress_ft;
	struct mlx5_flow_group *egress_mac_fg;
};

static struct mlx5_flow_table *
mlx5_esw_bridge_table_create(int max_fte, u32 level, struct mlx5_eswitch *esw)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *fdb;

	ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!ns) {
		esw_warn(dev, "Failed to get FDB namespace\n");
		return ERR_PTR(-ENOENT);
	}

	ft_attr.max_fte = max_fte;
	ft_attr.level = level;
	ft_attr.prio = FDB_BR_OFFLOAD;
	fdb = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(fdb))
		esw_warn(dev, "Failed to create bridge FDB Table (err=%ld)\n", PTR_ERR(fdb));

	return fdb;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_mac_fg_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *ingress_ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *in, *match;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(create_flow_group_in, in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS_2);
	match = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.smac_47_16);
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.smac_15_0);

	MLX5_SET(fte_match_param, match, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_mask());

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_TO);

	fg = mlx5_create_flow_group(ingress_ft, in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create bridge ingress table MAC flow group (err=%ld)\n",
			 PTR_ERR(fg));

	kvfree(in);
	return fg;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_egress_mac_fg_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *egress_ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *in, *match;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(create_flow_group_in, in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	match = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.dmac_47_16);
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.dmac_15_0);

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_TO);

	fg = mlx5_create_flow_group(egress_ft, in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create bridge egress table MAC flow group (err=%ld)\n",
			 PTR_ERR(fg));
	kvfree(in);
	return fg;
}

static int
mlx5_esw_bridge_ingress_table_init(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_flow_table *ingress_ft;
	struct mlx5_flow_group *mac_fg;
	int err;

	ingress_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE,
						  MLX5_ESW_BRIDGE_LEVEL_INGRESS_TABLE,
						  br_offloads->esw);
	if (IS_ERR(ingress_ft))
		return PTR_ERR(ingress_ft);

	mac_fg = mlx5_esw_bridge_ingress_mac_fg_create(br_offloads->esw, ingress_ft);
	if (IS_ERR(mac_fg)) {
		err = PTR_ERR(mac_fg);
		goto err_mac_fg;
	}

	br_offloads->ingress_ft = ingress_ft;
	br_offloads->ingress_mac_fg = mac_fg;
	return 0;

err_mac_fg:
	mlx5_destroy_flow_table(ingress_ft);
	return err;
}

static void
mlx5_esw_bridge_ingress_table_cleanup(struct mlx5_esw_bridge_offloads *br_offloads)
{
	mlx5_destroy_flow_group(br_offloads->ingress_mac_fg);
	br_offloads->ingress_mac_fg = NULL;
	mlx5_destroy_flow_table(br_offloads->ingress_ft);
	br_offloads->ingress_ft = NULL;
}

static int
mlx5_esw_bridge_egress_table_init(struct mlx5_esw_bridge_offloads *br_offloads,
				  struct mlx5_esw_bridge *bridge)
{
	struct mlx5_flow_table *egress_ft;
	struct mlx5_flow_group *mac_fg;
	int err;

	egress_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE,
						 MLX5_ESW_BRIDGE_LEVEL_EGRESS_TABLE,
						 br_offloads->esw);
	if (IS_ERR(egress_ft))
		return PTR_ERR(egress_ft);

	mac_fg = mlx5_esw_bridge_egress_mac_fg_create(br_offloads->esw, egress_ft);
	if (IS_ERR(mac_fg)) {
		err = PTR_ERR(mac_fg);
		goto err_mac_fg;
	}

	bridge->egress_ft = egress_ft;
	bridge->egress_mac_fg = mac_fg;
	return 0;

err_mac_fg:
	mlx5_destroy_flow_table(egress_ft);
	return err;
}

static void
mlx5_esw_bridge_egress_table_cleanup(struct mlx5_esw_bridge *bridge)
{
	mlx5_destroy_flow_group(bridge->egress_mac_fg);
	mlx5_destroy_flow_table(bridge->egress_ft);
}

static struct mlx5_esw_bridge *mlx5_esw_bridge_create(int ifindex,
						      struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge *bridge;
	int err;

	bridge = kvzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return ERR_PTR(-ENOMEM);

	err = mlx5_esw_bridge_egress_table_init(br_offloads, bridge);
	if (err)
		goto err_egress_tbl;

	bridge->ifindex = ifindex;
	bridge->refcnt = 1;
	list_add(&bridge->list, &br_offloads->bridges);

	return bridge;

err_egress_tbl:
	kvfree(bridge);
	return ERR_PTR(err);
}

static void mlx5_esw_bridge_get(struct mlx5_esw_bridge *bridge)
{
	bridge->refcnt++;
}

static void mlx5_esw_bridge_put(struct mlx5_esw_bridge_offloads *br_offloads,
				struct mlx5_esw_bridge *bridge)
{
	if (--bridge->refcnt)
		return;

	mlx5_esw_bridge_egress_table_cleanup(bridge);
	list_del(&bridge->list);
	kvfree(bridge);

	if (list_empty(&br_offloads->bridges))
		mlx5_esw_bridge_ingress_table_cleanup(br_offloads);
}

static struct mlx5_esw_bridge *
mlx5_esw_bridge_lookup(int ifindex, struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge *bridge;

	ASSERT_RTNL();

	list_for_each_entry(bridge, &br_offloads->bridges, list) {
		if (bridge->ifindex == ifindex) {
			mlx5_esw_bridge_get(bridge);
			return bridge;
		}
	}

	if (!br_offloads->ingress_ft) {
		int err = mlx5_esw_bridge_ingress_table_init(br_offloads);

		if (err)
			return ERR_PTR(err);
	}

	bridge = mlx5_esw_bridge_create(ifindex, br_offloads);
	if (IS_ERR(bridge) && list_empty(&br_offloads->bridges))
		mlx5_esw_bridge_ingress_table_cleanup(br_offloads);
	return bridge;
}

static int mlx5_esw_bridge_vport_init(struct mlx5_esw_bridge *bridge,
				      struct mlx5_vport *vport)
{
	vport->bridge = bridge;
	return 0;
}

static int mlx5_esw_bridge_vport_cleanup(struct mlx5_esw_bridge_offloads *br_offloads,
					 struct mlx5_vport *vport)
{
	mlx5_esw_bridge_put(br_offloads, vport->bridge);
	vport->bridge = NULL;
	return 0;
}

int mlx5_esw_bridge_vport_link(int ifindex, struct mlx5_esw_bridge_offloads *br_offloads,
			       struct mlx5_vport *vport, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_bridge *bridge;

	WARN_ON(vport->bridge);

	bridge = mlx5_esw_bridge_lookup(ifindex, br_offloads);
	if (IS_ERR(bridge)) {
		NL_SET_ERR_MSG_MOD(extack, "Error checking for existing bridge with same ifindex");
		return PTR_ERR(bridge);
	}

	return mlx5_esw_bridge_vport_init(bridge, vport);
}

int mlx5_esw_bridge_vport_unlink(int ifindex, struct mlx5_esw_bridge_offloads *br_offloads,
				 struct mlx5_vport *vport, struct netlink_ext_ack *extack)
{
	if (!vport->bridge) {
		NL_SET_ERR_MSG_MOD(extack, "Port is not attached to any bridge");
		return -EINVAL;
	}
	if (vport->bridge->ifindex != ifindex) {
		NL_SET_ERR_MSG_MOD(extack, "Port is attached to another bridge");
		return -EINVAL;
	}

	return mlx5_esw_bridge_vport_cleanup(br_offloads, vport);
}

static void mlx5_esw_bridge_flush(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_eswitch *esw = br_offloads->esw;
	struct mlx5_vport *vport;
	unsigned long i;

	mlx5_esw_for_each_vport(esw, i, vport)
		if (vport->bridge)
			mlx5_esw_bridge_vport_cleanup(br_offloads, vport);

	WARN_ONCE(!list_empty(&br_offloads->bridges),
		  "Cleaning up bridge offloads while still having bridges attached\n");
}

struct mlx5_esw_bridge_offloads *mlx5_esw_bridge_init(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_bridge_offloads *br_offloads;

	br_offloads = kvzalloc(sizeof(*br_offloads), GFP_KERNEL);
	if (!br_offloads)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&br_offloads->bridges);
	br_offloads->esw = esw;
	esw->br_offloads = br_offloads;

	return br_offloads;
}

void mlx5_esw_bridge_cleanup(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_bridge_offloads *br_offloads = esw->br_offloads;

	if (!br_offloads)
		return;

	mlx5_esw_bridge_flush(br_offloads);

	esw->br_offloads = NULL;
	kvfree(br_offloads);
}
