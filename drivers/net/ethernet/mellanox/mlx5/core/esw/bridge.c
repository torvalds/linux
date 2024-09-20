// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include <linux/build_bug.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <net/netevent.h>
#include <net/switchdev.h>
#include "lib/devcom.h"
#include "bridge.h"
#include "eswitch.h"
#include "bridge_priv.h"
#define CREATE_TRACE_POINTS
#include "diag/bridge_tracepoint.h"

static const struct rhashtable_params fdb_ht_params = {
	.key_offset = offsetof(struct mlx5_esw_bridge_fdb_entry, key),
	.key_len = sizeof(struct mlx5_esw_bridge_fdb_key),
	.head_offset = offsetof(struct mlx5_esw_bridge_fdb_entry, ht_node),
	.automatic_shrinking = true,
};

static void
mlx5_esw_bridge_fdb_offload_notify(struct net_device *dev, const unsigned char *addr, u16 vid,
				   unsigned long val)
{
	struct switchdev_notifier_fdb_info send_info = {};

	send_info.addr = addr;
	send_info.vid = vid;
	send_info.offloaded = true;
	call_switchdev_notifiers(val, dev, &send_info.info, NULL);
}

static void
mlx5_esw_bridge_fdb_del_notify(struct mlx5_esw_bridge_fdb_entry *entry)
{
	if (!(entry->flags & (MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER | MLX5_ESW_BRIDGE_FLAG_PEER)))
		mlx5_esw_bridge_fdb_offload_notify(entry->dev, entry->key.addr,
						   entry->key.vid,
						   SWITCHDEV_FDB_DEL_TO_BRIDGE);
}

static bool mlx5_esw_bridge_pkt_reformat_vlan_pop_supported(struct mlx5_eswitch *esw)
{
	return BIT(MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, reformat_remove)) &&
		MLX5_CAP_GEN_2(esw->dev, max_reformat_remove_size) >= sizeof(struct vlan_hdr) &&
		MLX5_CAP_GEN_2(esw->dev, max_reformat_remove_offset) >=
		offsetof(struct vlan_ethhdr, h_vlan_proto);
}

static struct mlx5_pkt_reformat *
mlx5_esw_bridge_pkt_reformat_vlan_pop_create(struct mlx5_eswitch *esw)
{
	struct mlx5_pkt_reformat_params reformat_params = {};

	reformat_params.type = MLX5_REFORMAT_TYPE_REMOVE_HDR;
	reformat_params.param_0 = MLX5_REFORMAT_CONTEXT_ANCHOR_MAC_START;
	reformat_params.param_1 = offsetof(struct vlan_ethhdr, h_vlan_proto);
	reformat_params.size = sizeof(struct vlan_hdr);
	return mlx5_packet_reformat_alloc(esw->dev, &reformat_params, MLX5_FLOW_NAMESPACE_FDB);
}

struct mlx5_flow_table *
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

	ft_attr.flags = MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;
	ft_attr.max_fte = max_fte;
	ft_attr.level = level;
	ft_attr.prio = FDB_BR_OFFLOAD;
	fdb = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(fdb))
		esw_warn(dev, "Failed to create bridge FDB Table (err=%ld)\n", PTR_ERR(fdb));

	return fdb;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_vlan_proto_fg_create(unsigned int from, unsigned int to, u16 vlan_proto,
					     struct mlx5_eswitch *esw,
					     struct mlx5_flow_table *ingress_ft)
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
	if (vlan_proto == ETH_P_8021Q)
		MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.cvlan_tag);
	else if (vlan_proto == ETH_P_8021AD)
		MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.svlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.first_vid);

	MLX5_SET(fte_match_param, match, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_mask());

	MLX5_SET(create_flow_group_in, in, start_flow_index, from);
	MLX5_SET(create_flow_group_in, in, end_flow_index, to);

	fg = mlx5_create_flow_group(ingress_ft, in);
	kvfree(in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create VLAN(proto=%x) flow group for bridge ingress table (err=%ld)\n",
			 vlan_proto, PTR_ERR(fg));

	return fg;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_vlan_fg_create(struct mlx5_eswitch *esw,
				       struct mlx5_flow_table *ingress_ft)
{
	unsigned int from = MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_FROM;
	unsigned int to = MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_TO;

	return mlx5_esw_bridge_ingress_vlan_proto_fg_create(from, to, ETH_P_8021Q, esw, ingress_ft);
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_qinq_fg_create(struct mlx5_eswitch *esw,
				       struct mlx5_flow_table *ingress_ft)
{
	unsigned int from = MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_GRP_IDX_FROM;
	unsigned int to = MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_GRP_IDX_TO;

	return mlx5_esw_bridge_ingress_vlan_proto_fg_create(from, to, ETH_P_8021AD, esw,
							    ingress_ft);
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_vlan_proto_filter_fg_create(unsigned int from, unsigned int to,
						    u16 vlan_proto, struct mlx5_eswitch *esw,
						    struct mlx5_flow_table *ingress_ft)
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
	if (vlan_proto == ETH_P_8021Q)
		MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.cvlan_tag);
	else if (vlan_proto == ETH_P_8021AD)
		MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.svlan_tag);
	MLX5_SET(fte_match_param, match, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_mask());

	MLX5_SET(create_flow_group_in, in, start_flow_index, from);
	MLX5_SET(create_flow_group_in, in, end_flow_index, to);

	fg = mlx5_create_flow_group(ingress_ft, in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create bridge ingress table VLAN filter flow group (err=%ld)\n",
			 PTR_ERR(fg));
	kvfree(in);
	return fg;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_vlan_filter_fg_create(struct mlx5_eswitch *esw,
					      struct mlx5_flow_table *ingress_ft)
{
	unsigned int from = MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_FILTER_GRP_IDX_FROM;
	unsigned int to = MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_FILTER_GRP_IDX_TO;

	return mlx5_esw_bridge_ingress_vlan_proto_filter_fg_create(from, to, ETH_P_8021Q, esw,
								   ingress_ft);
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_qinq_filter_fg_create(struct mlx5_eswitch *esw,
					      struct mlx5_flow_table *ingress_ft)
{
	unsigned int from = MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_FILTER_GRP_IDX_FROM;
	unsigned int to = MLX5_ESW_BRIDGE_INGRESS_TABLE_QINQ_FILTER_GRP_IDX_TO;

	return mlx5_esw_bridge_ingress_vlan_proto_filter_fg_create(from, to, ETH_P_8021AD, esw,
								   ingress_ft);
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
			 "Failed to create MAC flow group for bridge ingress table (err=%ld)\n",
			 PTR_ERR(fg));

	kvfree(in);
	return fg;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_egress_vlan_proto_fg_create(unsigned int from, unsigned int to, u16 vlan_proto,
					    struct mlx5_eswitch *esw,
					    struct mlx5_flow_table *egress_ft)
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
	if (vlan_proto == ETH_P_8021Q)
		MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.cvlan_tag);
	else if (vlan_proto == ETH_P_8021AD)
		MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.svlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.first_vid);

	MLX5_SET(create_flow_group_in, in, start_flow_index, from);
	MLX5_SET(create_flow_group_in, in, end_flow_index, to);

	fg = mlx5_create_flow_group(egress_ft, in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create VLAN flow group for bridge egress table (err=%ld)\n",
			 PTR_ERR(fg));
	kvfree(in);
	return fg;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_egress_vlan_fg_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *egress_ft)
{
	unsigned int from = MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_IDX_FROM;
	unsigned int to = MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_IDX_TO;

	return mlx5_esw_bridge_egress_vlan_proto_fg_create(from, to, ETH_P_8021Q, esw, egress_ft);
}

static struct mlx5_flow_group *
mlx5_esw_bridge_egress_qinq_fg_create(struct mlx5_eswitch *esw,
				      struct mlx5_flow_table *egress_ft)
{
	unsigned int from = MLX5_ESW_BRIDGE_EGRESS_TABLE_QINQ_GRP_IDX_FROM;
	unsigned int to = MLX5_ESW_BRIDGE_EGRESS_TABLE_QINQ_GRP_IDX_TO;

	return mlx5_esw_bridge_egress_vlan_proto_fg_create(from, to, ETH_P_8021AD, esw, egress_ft);
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

static struct mlx5_flow_group *
mlx5_esw_bridge_egress_miss_fg_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *egress_ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *in, *match;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(create_flow_group_in, in, match_criteria_enable, MLX5_MATCH_MISC_PARAMETERS_2);
	match = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	MLX5_SET(fte_match_param, match, misc_parameters_2.metadata_reg_c_1, ESW_TUN_MASK);

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_EGRESS_TABLE_MISS_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_EGRESS_TABLE_MISS_GRP_IDX_TO);

	fg = mlx5_create_flow_group(egress_ft, in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create bridge egress table miss flow group (err=%ld)\n",
			 PTR_ERR(fg));
	kvfree(in);
	return fg;
}

static int
mlx5_esw_bridge_ingress_table_init(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_flow_group *mac_fg, *qinq_filter_fg, *qinq_fg, *vlan_filter_fg, *vlan_fg;
	struct mlx5_flow_table *ingress_ft, *skip_ft;
	struct mlx5_eswitch *esw = br_offloads->esw;
	int err;

	if (!mlx5_eswitch_vport_match_metadata_enabled(esw))
		return -EOPNOTSUPP;

	ingress_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE,
						  MLX5_ESW_BRIDGE_LEVEL_INGRESS_TABLE,
						  esw);
	if (IS_ERR(ingress_ft))
		return PTR_ERR(ingress_ft);

	skip_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_SKIP_TABLE_SIZE,
					       MLX5_ESW_BRIDGE_LEVEL_SKIP_TABLE,
					       esw);
	if (IS_ERR(skip_ft)) {
		err = PTR_ERR(skip_ft);
		goto err_skip_tbl;
	}

	vlan_fg = mlx5_esw_bridge_ingress_vlan_fg_create(esw, ingress_ft);
	if (IS_ERR(vlan_fg)) {
		err = PTR_ERR(vlan_fg);
		goto err_vlan_fg;
	}

	vlan_filter_fg = mlx5_esw_bridge_ingress_vlan_filter_fg_create(esw, ingress_ft);
	if (IS_ERR(vlan_filter_fg)) {
		err = PTR_ERR(vlan_filter_fg);
		goto err_vlan_filter_fg;
	}

	qinq_fg = mlx5_esw_bridge_ingress_qinq_fg_create(esw, ingress_ft);
	if (IS_ERR(qinq_fg)) {
		err = PTR_ERR(qinq_fg);
		goto err_qinq_fg;
	}

	qinq_filter_fg = mlx5_esw_bridge_ingress_qinq_filter_fg_create(esw, ingress_ft);
	if (IS_ERR(qinq_filter_fg)) {
		err = PTR_ERR(qinq_filter_fg);
		goto err_qinq_filter_fg;
	}

	mac_fg = mlx5_esw_bridge_ingress_mac_fg_create(esw, ingress_ft);
	if (IS_ERR(mac_fg)) {
		err = PTR_ERR(mac_fg);
		goto err_mac_fg;
	}

	br_offloads->ingress_ft = ingress_ft;
	br_offloads->skip_ft = skip_ft;
	br_offloads->ingress_vlan_fg = vlan_fg;
	br_offloads->ingress_vlan_filter_fg = vlan_filter_fg;
	br_offloads->ingress_qinq_fg = qinq_fg;
	br_offloads->ingress_qinq_filter_fg = qinq_filter_fg;
	br_offloads->ingress_mac_fg = mac_fg;
	return 0;

err_mac_fg:
	mlx5_destroy_flow_group(qinq_filter_fg);
err_qinq_filter_fg:
	mlx5_destroy_flow_group(qinq_fg);
err_qinq_fg:
	mlx5_destroy_flow_group(vlan_filter_fg);
err_vlan_filter_fg:
	mlx5_destroy_flow_group(vlan_fg);
err_vlan_fg:
	mlx5_destroy_flow_table(skip_ft);
err_skip_tbl:
	mlx5_destroy_flow_table(ingress_ft);
	return err;
}

static void
mlx5_esw_bridge_ingress_table_cleanup(struct mlx5_esw_bridge_offloads *br_offloads)
{
	mlx5_destroy_flow_group(br_offloads->ingress_mac_fg);
	br_offloads->ingress_mac_fg = NULL;
	mlx5_destroy_flow_group(br_offloads->ingress_qinq_filter_fg);
	br_offloads->ingress_qinq_filter_fg = NULL;
	mlx5_destroy_flow_group(br_offloads->ingress_qinq_fg);
	br_offloads->ingress_qinq_fg = NULL;
	mlx5_destroy_flow_group(br_offloads->ingress_vlan_filter_fg);
	br_offloads->ingress_vlan_filter_fg = NULL;
	mlx5_destroy_flow_group(br_offloads->ingress_vlan_fg);
	br_offloads->ingress_vlan_fg = NULL;
	mlx5_destroy_flow_table(br_offloads->skip_ft);
	br_offloads->skip_ft = NULL;
	mlx5_destroy_flow_table(br_offloads->ingress_ft);
	br_offloads->ingress_ft = NULL;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_egress_miss_flow_create(struct mlx5_flow_table *egress_ft,
					struct mlx5_flow_table *skip_ft,
					struct mlx5_pkt_reformat *pkt_reformat);

static int
mlx5_esw_bridge_egress_table_init(struct mlx5_esw_bridge_offloads *br_offloads,
				  struct mlx5_esw_bridge *bridge)
{
	struct mlx5_flow_group *miss_fg = NULL, *mac_fg, *vlan_fg, *qinq_fg;
	struct mlx5_pkt_reformat *miss_pkt_reformat = NULL;
	struct mlx5_flow_handle *miss_handle = NULL;
	struct mlx5_eswitch *esw = br_offloads->esw;
	struct mlx5_flow_table *egress_ft;
	int err;

	egress_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE,
						 MLX5_ESW_BRIDGE_LEVEL_EGRESS_TABLE,
						 esw);
	if (IS_ERR(egress_ft))
		return PTR_ERR(egress_ft);

	vlan_fg = mlx5_esw_bridge_egress_vlan_fg_create(esw, egress_ft);
	if (IS_ERR(vlan_fg)) {
		err = PTR_ERR(vlan_fg);
		goto err_vlan_fg;
	}

	qinq_fg = mlx5_esw_bridge_egress_qinq_fg_create(esw, egress_ft);
	if (IS_ERR(qinq_fg)) {
		err = PTR_ERR(qinq_fg);
		goto err_qinq_fg;
	}

	mac_fg = mlx5_esw_bridge_egress_mac_fg_create(esw, egress_ft);
	if (IS_ERR(mac_fg)) {
		err = PTR_ERR(mac_fg);
		goto err_mac_fg;
	}

	if (mlx5_esw_bridge_pkt_reformat_vlan_pop_supported(esw)) {
		miss_fg = mlx5_esw_bridge_egress_miss_fg_create(esw, egress_ft);
		if (IS_ERR(miss_fg)) {
			esw_warn(esw->dev, "Failed to create miss flow group (err=%ld)\n",
				 PTR_ERR(miss_fg));
			miss_fg = NULL;
			goto skip_miss_flow;
		}

		miss_pkt_reformat = mlx5_esw_bridge_pkt_reformat_vlan_pop_create(esw);
		if (IS_ERR(miss_pkt_reformat)) {
			esw_warn(esw->dev,
				 "Failed to alloc packet reformat REMOVE_HEADER (err=%ld)\n",
				 PTR_ERR(miss_pkt_reformat));
			miss_pkt_reformat = NULL;
			mlx5_destroy_flow_group(miss_fg);
			miss_fg = NULL;
			goto skip_miss_flow;
		}

		miss_handle = mlx5_esw_bridge_egress_miss_flow_create(egress_ft,
								      br_offloads->skip_ft,
								      miss_pkt_reformat);
		if (IS_ERR(miss_handle)) {
			esw_warn(esw->dev, "Failed to create miss flow (err=%ld)\n",
				 PTR_ERR(miss_handle));
			miss_handle = NULL;
			mlx5_packet_reformat_dealloc(esw->dev, miss_pkt_reformat);
			miss_pkt_reformat = NULL;
			mlx5_destroy_flow_group(miss_fg);
			miss_fg = NULL;
			goto skip_miss_flow;
		}
	}
skip_miss_flow:

	bridge->egress_ft = egress_ft;
	bridge->egress_vlan_fg = vlan_fg;
	bridge->egress_qinq_fg = qinq_fg;
	bridge->egress_mac_fg = mac_fg;
	bridge->egress_miss_fg = miss_fg;
	bridge->egress_miss_pkt_reformat = miss_pkt_reformat;
	bridge->egress_miss_handle = miss_handle;
	return 0;

err_mac_fg:
	mlx5_destroy_flow_group(qinq_fg);
err_qinq_fg:
	mlx5_destroy_flow_group(vlan_fg);
err_vlan_fg:
	mlx5_destroy_flow_table(egress_ft);
	return err;
}

static void
mlx5_esw_bridge_egress_table_cleanup(struct mlx5_esw_bridge *bridge)
{
	if (bridge->egress_miss_handle)
		mlx5_del_flow_rules(bridge->egress_miss_handle);
	if (bridge->egress_miss_pkt_reformat)
		mlx5_packet_reformat_dealloc(bridge->br_offloads->esw->dev,
					     bridge->egress_miss_pkt_reformat);
	if (bridge->egress_miss_fg)
		mlx5_destroy_flow_group(bridge->egress_miss_fg);
	mlx5_destroy_flow_group(bridge->egress_mac_fg);
	mlx5_destroy_flow_group(bridge->egress_qinq_fg);
	mlx5_destroy_flow_group(bridge->egress_vlan_fg);
	mlx5_destroy_flow_table(bridge->egress_ft);
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_ingress_flow_with_esw_create(u16 vport_num, const unsigned char *addr,
					     struct mlx5_esw_bridge_vlan *vlan, u32 counter_id,
					     struct mlx5_esw_bridge *bridge,
					     struct mlx5_eswitch *esw)
{
	struct mlx5_esw_bridge_offloads *br_offloads = bridge->br_offloads;
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST | MLX5_FLOW_CONTEXT_ACTION_COUNT,
		.flags = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_flow_destination dests[2] = {};
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;
	u8 *smac_v, *smac_c;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	rule_spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS_2;

	smac_v = MLX5_ADDR_OF(fte_match_param, rule_spec->match_value,
			      outer_headers.smac_47_16);
	ether_addr_copy(smac_v, addr);
	smac_c = MLX5_ADDR_OF(fte_match_param, rule_spec->match_criteria,
			      outer_headers.smac_47_16);
	eth_broadcast_addr(smac_c);

	MLX5_SET(fte_match_param, rule_spec->match_criteria,
		 misc_parameters_2.metadata_reg_c_0, mlx5_eswitch_get_vport_metadata_mask());
	MLX5_SET(fte_match_param, rule_spec->match_value, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_for_match(esw, vport_num));

	if (vlan && vlan->pkt_reformat_push) {
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT |
			MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
		flow_act.pkt_reformat = vlan->pkt_reformat_push;
		flow_act.modify_hdr = vlan->pkt_mod_hdr_push_mark;
	} else if (vlan) {
		if (bridge->vlan_proto == ETH_P_8021Q) {
			MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
					 outer_headers.cvlan_tag);
			MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
					 outer_headers.cvlan_tag);
		} else if (bridge->vlan_proto == ETH_P_8021AD) {
			MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
					 outer_headers.svlan_tag);
			MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
					 outer_headers.svlan_tag);
		}
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.first_vid);
		MLX5_SET(fte_match_param, rule_spec->match_value, outer_headers.first_vid,
			 vlan->vid);
	}

	dests[0].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dests[0].ft = bridge->egress_ft;
	dests[1].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dests[1].counter_id = counter_id;

	handle = mlx5_add_flow_rules(br_offloads->ingress_ft, rule_spec, &flow_act, dests,
				     ARRAY_SIZE(dests));

	kvfree(rule_spec);
	return handle;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_ingress_flow_create(u16 vport_num, const unsigned char *addr,
				    struct mlx5_esw_bridge_vlan *vlan, u32 counter_id,
				    struct mlx5_esw_bridge *bridge)
{
	return mlx5_esw_bridge_ingress_flow_with_esw_create(vport_num, addr, vlan, counter_id,
							    bridge, bridge->br_offloads->esw);
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_ingress_flow_peer_create(u16 vport_num, u16 esw_owner_vhca_id,
					 const unsigned char *addr,
					 struct mlx5_esw_bridge_vlan *vlan, u32 counter_id,
					 struct mlx5_esw_bridge *bridge)
{
	struct mlx5_devcom_comp_dev *devcom = bridge->br_offloads->esw->devcom, *pos;
	struct mlx5_eswitch *tmp, *peer_esw = NULL;
	static struct mlx5_flow_handle *handle;

	if (!mlx5_devcom_for_each_peer_begin(devcom))
		return ERR_PTR(-ENODEV);

	mlx5_devcom_for_each_peer_entry(devcom, tmp, pos) {
		if (mlx5_esw_is_owner(tmp, vport_num, esw_owner_vhca_id)) {
			peer_esw = tmp;
			break;
		}
	}

	if (!peer_esw) {
		handle = ERR_PTR(-ENODEV);
		goto out;
	}

	handle = mlx5_esw_bridge_ingress_flow_with_esw_create(vport_num, addr, vlan, counter_id,
							      bridge, peer_esw);

out:
	mlx5_devcom_for_each_peer_end(devcom);
	return handle;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_ingress_filter_flow_create(u16 vport_num, const unsigned char *addr,
					   struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_offloads *br_offloads = bridge->br_offloads;
	struct mlx5_flow_destination dest = {
		.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE,
		.ft = br_offloads->skip_ft,
	};
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
		.flags = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;
	u8 *smac_v, *smac_c;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	rule_spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS_2;

	smac_v = MLX5_ADDR_OF(fte_match_param, rule_spec->match_value,
			      outer_headers.smac_47_16);
	ether_addr_copy(smac_v, addr);
	smac_c = MLX5_ADDR_OF(fte_match_param, rule_spec->match_criteria,
			      outer_headers.smac_47_16);
	eth_broadcast_addr(smac_c);

	MLX5_SET(fte_match_param, rule_spec->match_criteria,
		 misc_parameters_2.metadata_reg_c_0, mlx5_eswitch_get_vport_metadata_mask());
	MLX5_SET(fte_match_param, rule_spec->match_value, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_for_match(br_offloads->esw, vport_num));

	if (bridge->vlan_proto == ETH_P_8021Q) {
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.cvlan_tag);
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
				 outer_headers.cvlan_tag);
	} else if (bridge->vlan_proto == ETH_P_8021AD) {
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.svlan_tag);
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
				 outer_headers.svlan_tag);
	}

	handle = mlx5_add_flow_rules(br_offloads->ingress_ft, rule_spec, &flow_act, &dest, 1);

	kvfree(rule_spec);
	return handle;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_egress_flow_create(u16 vport_num, u16 esw_owner_vhca_id, const unsigned char *addr,
				   struct mlx5_esw_bridge_vlan *vlan,
				   struct mlx5_esw_bridge *bridge)
{
	struct mlx5_flow_destination dest = {
		.type = MLX5_FLOW_DESTINATION_TYPE_VPORT,
		.vport.num = vport_num,
	};
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
		.flags = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;
	u8 *dmac_v, *dmac_c;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	if (MLX5_CAP_ESW_FLOWTABLE(bridge->br_offloads->esw->dev, flow_source) &&
	    vport_num == MLX5_VPORT_UPLINK)
		rule_spec->flow_context.flow_source =
			MLX5_FLOW_CONTEXT_FLOW_SOURCE_LOCAL_VPORT;
	rule_spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	dmac_v = MLX5_ADDR_OF(fte_match_param, rule_spec->match_value,
			      outer_headers.dmac_47_16);
	ether_addr_copy(dmac_v, addr);
	dmac_c = MLX5_ADDR_OF(fte_match_param, rule_spec->match_criteria,
			      outer_headers.dmac_47_16);
	eth_broadcast_addr(dmac_c);

	if (vlan) {
		if (vlan->pkt_reformat_pop) {
			flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
			flow_act.pkt_reformat = vlan->pkt_reformat_pop;
		}

		if (bridge->vlan_proto == ETH_P_8021Q) {
			MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
					 outer_headers.cvlan_tag);
			MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
					 outer_headers.cvlan_tag);
		} else if (bridge->vlan_proto == ETH_P_8021AD) {
			MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
					 outer_headers.svlan_tag);
			MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
					 outer_headers.svlan_tag);
		}
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.first_vid);
		MLX5_SET(fte_match_param, rule_spec->match_value, outer_headers.first_vid,
			 vlan->vid);
	}

	if (MLX5_CAP_ESW(bridge->br_offloads->esw->dev, merged_eswitch)) {
		dest.vport.flags = MLX5_FLOW_DEST_VPORT_VHCA_ID;
		dest.vport.vhca_id = esw_owner_vhca_id;
	}
	handle = mlx5_add_flow_rules(bridge->egress_ft, rule_spec, &flow_act, &dest, 1);

	kvfree(rule_spec);
	return handle;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_egress_miss_flow_create(struct mlx5_flow_table *egress_ft,
					struct mlx5_flow_table *skip_ft,
					struct mlx5_pkt_reformat *pkt_reformat)
{
	struct mlx5_flow_destination dest = {
		.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE,
		.ft = skip_ft,
	};
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
		MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT,
		.flags = FLOW_ACT_NO_APPEND,
		.pkt_reformat = pkt_reformat,
	};
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	rule_spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;

	MLX5_SET(fte_match_param, rule_spec->match_criteria,
		 misc_parameters_2.metadata_reg_c_1, ESW_TUN_MASK);
	MLX5_SET(fte_match_param, rule_spec->match_value, misc_parameters_2.metadata_reg_c_1,
		 ESW_TUN_BRIDGE_INGRESS_PUSH_VLAN_MARK);

	handle = mlx5_add_flow_rules(egress_ft, rule_spec, &flow_act, &dest, 1);

	kvfree(rule_spec);
	return handle;
}

static struct mlx5_esw_bridge *mlx5_esw_bridge_create(struct net_device *br_netdev,
						      struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge *bridge;
	int err;

	bridge = kvzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return ERR_PTR(-ENOMEM);

	bridge->br_offloads = br_offloads;
	err = mlx5_esw_bridge_egress_table_init(br_offloads, bridge);
	if (err)
		goto err_egress_tbl;

	err = rhashtable_init(&bridge->fdb_ht, &fdb_ht_params);
	if (err)
		goto err_fdb_ht;

	err = mlx5_esw_bridge_mdb_init(bridge);
	if (err)
		goto err_mdb_ht;

	INIT_LIST_HEAD(&bridge->fdb_list);
	bridge->ifindex = br_netdev->ifindex;
	bridge->refcnt = 1;
	bridge->ageing_time = clock_t_to_jiffies(BR_DEFAULT_AGEING_TIME);
	bridge->vlan_proto = ETH_P_8021Q;
	list_add(&bridge->list, &br_offloads->bridges);
	mlx5_esw_bridge_debugfs_init(br_netdev, bridge);

	return bridge;

err_mdb_ht:
	rhashtable_destroy(&bridge->fdb_ht);
err_fdb_ht:
	mlx5_esw_bridge_egress_table_cleanup(bridge);
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

	mlx5_esw_bridge_debugfs_cleanup(bridge);
	mlx5_esw_bridge_egress_table_cleanup(bridge);
	mlx5_esw_bridge_mcast_disable(bridge);
	list_del(&bridge->list);
	mlx5_esw_bridge_mdb_cleanup(bridge);
	rhashtable_destroy(&bridge->fdb_ht);
	kvfree(bridge);

	if (list_empty(&br_offloads->bridges))
		mlx5_esw_bridge_ingress_table_cleanup(br_offloads);
}

static struct mlx5_esw_bridge *
mlx5_esw_bridge_lookup(struct net_device *br_netdev, struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge *bridge;

	ASSERT_RTNL();

	list_for_each_entry(bridge, &br_offloads->bridges, list) {
		if (bridge->ifindex == br_netdev->ifindex) {
			mlx5_esw_bridge_get(bridge);
			return bridge;
		}
	}

	if (!br_offloads->ingress_ft) {
		int err = mlx5_esw_bridge_ingress_table_init(br_offloads);

		if (err)
			return ERR_PTR(err);
	}

	bridge = mlx5_esw_bridge_create(br_netdev, br_offloads);
	if (IS_ERR(bridge) && list_empty(&br_offloads->bridges))
		mlx5_esw_bridge_ingress_table_cleanup(br_offloads);
	return bridge;
}

static unsigned long mlx5_esw_bridge_port_key_from_data(u16 vport_num, u16 esw_owner_vhca_id)
{
	return vport_num | (unsigned long)esw_owner_vhca_id << sizeof(vport_num) * BITS_PER_BYTE;
}

unsigned long mlx5_esw_bridge_port_key(struct mlx5_esw_bridge_port *port)
{
	return mlx5_esw_bridge_port_key_from_data(port->vport_num, port->esw_owner_vhca_id);
}

static int mlx5_esw_bridge_port_insert(struct mlx5_esw_bridge_port *port,
				       struct mlx5_esw_bridge_offloads *br_offloads)
{
	return xa_insert(&br_offloads->ports, mlx5_esw_bridge_port_key(port), port, GFP_KERNEL);
}

static struct mlx5_esw_bridge_port *
mlx5_esw_bridge_port_lookup(u16 vport_num, u16 esw_owner_vhca_id,
			    struct mlx5_esw_bridge_offloads *br_offloads)
{
	return xa_load(&br_offloads->ports, mlx5_esw_bridge_port_key_from_data(vport_num,
									       esw_owner_vhca_id));
}

static void mlx5_esw_bridge_port_erase(struct mlx5_esw_bridge_port *port,
				       struct mlx5_esw_bridge_offloads *br_offloads)
{
	xa_erase(&br_offloads->ports, mlx5_esw_bridge_port_key(port));
}

static struct mlx5_esw_bridge *
mlx5_esw_bridge_from_port_lookup(u16 vport_num, u16 esw_owner_vhca_id,
				 struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge_port *port;

	port = mlx5_esw_bridge_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!port)
		return NULL;

	return port->bridge;
}

static void mlx5_esw_bridge_fdb_entry_refresh(struct mlx5_esw_bridge_fdb_entry *entry)
{
	trace_mlx5_esw_bridge_fdb_entry_refresh(entry);

	mlx5_esw_bridge_fdb_offload_notify(entry->dev, entry->key.addr,
					   entry->key.vid,
					   SWITCHDEV_FDB_ADD_TO_BRIDGE);
}

static void
mlx5_esw_bridge_fdb_entry_cleanup(struct mlx5_esw_bridge_fdb_entry *entry,
				  struct mlx5_esw_bridge *bridge)
{
	trace_mlx5_esw_bridge_fdb_entry_cleanup(entry);

	rhashtable_remove_fast(&bridge->fdb_ht, &entry->ht_node, fdb_ht_params);
	mlx5_del_flow_rules(entry->egress_handle);
	if (entry->filter_handle)
		mlx5_del_flow_rules(entry->filter_handle);
	mlx5_del_flow_rules(entry->ingress_handle);
	mlx5_fc_destroy(bridge->br_offloads->esw->dev, entry->ingress_counter);
	list_del(&entry->vlan_list);
	list_del(&entry->list);
	kvfree(entry);
}

static void
mlx5_esw_bridge_fdb_entry_notify_and_cleanup(struct mlx5_esw_bridge_fdb_entry *entry,
					     struct mlx5_esw_bridge *bridge)
{
	mlx5_esw_bridge_fdb_del_notify(entry);
	mlx5_esw_bridge_fdb_entry_cleanup(entry, bridge);
}

static void mlx5_esw_bridge_fdb_flush(struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_fdb_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &bridge->fdb_list, list)
		mlx5_esw_bridge_fdb_entry_notify_and_cleanup(entry, bridge);
}

static struct mlx5_esw_bridge_vlan *
mlx5_esw_bridge_vlan_lookup(u16 vid, struct mlx5_esw_bridge_port *port)
{
	return xa_load(&port->vlans, vid);
}

static int
mlx5_esw_bridge_vlan_push_create(u16 vlan_proto, struct mlx5_esw_bridge_vlan *vlan,
				 struct mlx5_eswitch *esw)
{
	struct {
		__be16	h_vlan_proto;
		__be16	h_vlan_TCI;
	} vlan_hdr = { htons(vlan_proto), htons(vlan->vid) };
	struct mlx5_pkt_reformat_params reformat_params = {};
	struct mlx5_pkt_reformat *pkt_reformat;

	if (!BIT(MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, reformat_insert)) ||
	    MLX5_CAP_GEN_2(esw->dev, max_reformat_insert_size) < sizeof(vlan_hdr) ||
	    MLX5_CAP_GEN_2(esw->dev, max_reformat_insert_offset) <
	    offsetof(struct vlan_ethhdr, h_vlan_proto)) {
		esw_warn(esw->dev, "Packet reformat INSERT_HEADER is not supported\n");
		return -EOPNOTSUPP;
	}

	reformat_params.type = MLX5_REFORMAT_TYPE_INSERT_HDR;
	reformat_params.param_0 = MLX5_REFORMAT_CONTEXT_ANCHOR_MAC_START;
	reformat_params.param_1 = offsetof(struct vlan_ethhdr, h_vlan_proto);
	reformat_params.size = sizeof(vlan_hdr);
	reformat_params.data = &vlan_hdr;
	pkt_reformat = mlx5_packet_reformat_alloc(esw->dev,
						  &reformat_params,
						  MLX5_FLOW_NAMESPACE_FDB);
	if (IS_ERR(pkt_reformat)) {
		esw_warn(esw->dev, "Failed to alloc packet reformat INSERT_HEADER (err=%ld)\n",
			 PTR_ERR(pkt_reformat));
		return PTR_ERR(pkt_reformat);
	}

	vlan->pkt_reformat_push = pkt_reformat;
	return 0;
}

static void
mlx5_esw_bridge_vlan_push_cleanup(struct mlx5_esw_bridge_vlan *vlan, struct mlx5_eswitch *esw)
{
	mlx5_packet_reformat_dealloc(esw->dev, vlan->pkt_reformat_push);
	vlan->pkt_reformat_push = NULL;
}

static int
mlx5_esw_bridge_vlan_pop_create(struct mlx5_esw_bridge_vlan *vlan, struct mlx5_eswitch *esw)
{
	struct mlx5_pkt_reformat *pkt_reformat;

	if (!mlx5_esw_bridge_pkt_reformat_vlan_pop_supported(esw)) {
		esw_warn(esw->dev, "Packet reformat REMOVE_HEADER is not supported\n");
		return -EOPNOTSUPP;
	}

	pkt_reformat = mlx5_esw_bridge_pkt_reformat_vlan_pop_create(esw);
	if (IS_ERR(pkt_reformat)) {
		esw_warn(esw->dev, "Failed to alloc packet reformat REMOVE_HEADER (err=%ld)\n",
			 PTR_ERR(pkt_reformat));
		return PTR_ERR(pkt_reformat);
	}

	vlan->pkt_reformat_pop = pkt_reformat;
	return 0;
}

static void
mlx5_esw_bridge_vlan_pop_cleanup(struct mlx5_esw_bridge_vlan *vlan, struct mlx5_eswitch *esw)
{
	mlx5_packet_reformat_dealloc(esw->dev, vlan->pkt_reformat_pop);
	vlan->pkt_reformat_pop = NULL;
}

static int
mlx5_esw_bridge_vlan_push_mark_create(struct mlx5_esw_bridge_vlan *vlan, struct mlx5_eswitch *esw)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_modify_hdr *pkt_mod_hdr;

	MLX5_SET(set_action_in, action, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, action, field, MLX5_ACTION_IN_FIELD_METADATA_REG_C_1);
	MLX5_SET(set_action_in, action, offset, 8);
	MLX5_SET(set_action_in, action, length, ESW_TUN_OPTS_BITS + ESW_TUN_ID_BITS);
	MLX5_SET(set_action_in, action, data, ESW_TUN_BRIDGE_INGRESS_PUSH_VLAN);

	pkt_mod_hdr = mlx5_modify_header_alloc(esw->dev, MLX5_FLOW_NAMESPACE_FDB, 1, action);
	if (IS_ERR(pkt_mod_hdr))
		return PTR_ERR(pkt_mod_hdr);

	vlan->pkt_mod_hdr_push_mark = pkt_mod_hdr;
	return 0;
}

static void
mlx5_esw_bridge_vlan_push_mark_cleanup(struct mlx5_esw_bridge_vlan *vlan, struct mlx5_eswitch *esw)
{
	mlx5_modify_header_dealloc(esw->dev, vlan->pkt_mod_hdr_push_mark);
	vlan->pkt_mod_hdr_push_mark = NULL;
}

static int
mlx5_esw_bridge_vlan_push_pop_fhs_create(u16 vlan_proto, struct mlx5_esw_bridge_port *port,
					 struct mlx5_esw_bridge_vlan *vlan)
{
	return mlx5_esw_bridge_vlan_mcast_init(vlan_proto, port, vlan);
}

static void
mlx5_esw_bridge_vlan_push_pop_fhs_cleanup(struct mlx5_esw_bridge_vlan *vlan)
{
	mlx5_esw_bridge_vlan_mcast_cleanup(vlan);
}

static int
mlx5_esw_bridge_vlan_push_pop_create(u16 vlan_proto, u16 flags, struct mlx5_esw_bridge_port *port,
				     struct mlx5_esw_bridge_vlan *vlan, struct mlx5_eswitch *esw)
{
	int err;

	if (flags & BRIDGE_VLAN_INFO_PVID) {
		err = mlx5_esw_bridge_vlan_push_create(vlan_proto, vlan, esw);
		if (err)
			return err;

		err = mlx5_esw_bridge_vlan_push_mark_create(vlan, esw);
		if (err)
			goto err_vlan_push_mark;
	}

	if (flags & BRIDGE_VLAN_INFO_UNTAGGED) {
		err = mlx5_esw_bridge_vlan_pop_create(vlan, esw);
		if (err)
			goto err_vlan_pop;

		err = mlx5_esw_bridge_vlan_push_pop_fhs_create(vlan_proto, port, vlan);
		if (err)
			goto err_vlan_pop_fhs;
	}

	return 0;

err_vlan_pop_fhs:
	mlx5_esw_bridge_vlan_pop_cleanup(vlan, esw);
err_vlan_pop:
	if (vlan->pkt_mod_hdr_push_mark)
		mlx5_esw_bridge_vlan_push_mark_cleanup(vlan, esw);
err_vlan_push_mark:
	if (vlan->pkt_reformat_push)
		mlx5_esw_bridge_vlan_push_cleanup(vlan, esw);
	return err;
}

static struct mlx5_esw_bridge_vlan *
mlx5_esw_bridge_vlan_create(u16 vlan_proto, u16 vid, u16 flags, struct mlx5_esw_bridge_port *port,
			    struct mlx5_eswitch *esw)
{
	struct mlx5_esw_bridge_vlan *vlan;
	int err;

	vlan = kvzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan)
		return ERR_PTR(-ENOMEM);

	vlan->vid = vid;
	vlan->flags = flags;
	INIT_LIST_HEAD(&vlan->fdb_list);

	err = mlx5_esw_bridge_vlan_push_pop_create(vlan_proto, flags, port, vlan, esw);
	if (err)
		goto err_vlan_push_pop;

	err = xa_insert(&port->vlans, vid, vlan, GFP_KERNEL);
	if (err)
		goto err_xa_insert;

	trace_mlx5_esw_bridge_vlan_create(vlan);
	return vlan;

err_xa_insert:
	if (vlan->mcast_handle)
		mlx5_esw_bridge_vlan_push_pop_fhs_cleanup(vlan);
	if (vlan->pkt_reformat_pop)
		mlx5_esw_bridge_vlan_pop_cleanup(vlan, esw);
	if (vlan->pkt_mod_hdr_push_mark)
		mlx5_esw_bridge_vlan_push_mark_cleanup(vlan, esw);
	if (vlan->pkt_reformat_push)
		mlx5_esw_bridge_vlan_push_cleanup(vlan, esw);
err_vlan_push_pop:
	kvfree(vlan);
	return ERR_PTR(err);
}

static void mlx5_esw_bridge_vlan_erase(struct mlx5_esw_bridge_port *port,
				       struct mlx5_esw_bridge_vlan *vlan)
{
	xa_erase(&port->vlans, vlan->vid);
}

static void mlx5_esw_bridge_vlan_flush(struct mlx5_esw_bridge_port *port,
				       struct mlx5_esw_bridge_vlan *vlan,
				       struct mlx5_esw_bridge *bridge)
{
	struct mlx5_eswitch *esw = bridge->br_offloads->esw;
	struct mlx5_esw_bridge_fdb_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &vlan->fdb_list, vlan_list)
		mlx5_esw_bridge_fdb_entry_notify_and_cleanup(entry, bridge);
	mlx5_esw_bridge_port_mdb_vlan_flush(port, vlan);

	if (vlan->mcast_handle)
		mlx5_esw_bridge_vlan_push_pop_fhs_cleanup(vlan);
	if (vlan->pkt_reformat_pop)
		mlx5_esw_bridge_vlan_pop_cleanup(vlan, esw);
	if (vlan->pkt_mod_hdr_push_mark)
		mlx5_esw_bridge_vlan_push_mark_cleanup(vlan, esw);
	if (vlan->pkt_reformat_push)
		mlx5_esw_bridge_vlan_push_cleanup(vlan, esw);
}

static void mlx5_esw_bridge_vlan_cleanup(struct mlx5_esw_bridge_port *port,
					 struct mlx5_esw_bridge_vlan *vlan,
					 struct mlx5_esw_bridge *bridge)
{
	trace_mlx5_esw_bridge_vlan_cleanup(vlan);
	mlx5_esw_bridge_vlan_flush(port, vlan, bridge);
	mlx5_esw_bridge_vlan_erase(port, vlan);
	kvfree(vlan);
}

static void mlx5_esw_bridge_port_vlans_flush(struct mlx5_esw_bridge_port *port,
					     struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_vlan *vlan;
	unsigned long index;

	xa_for_each(&port->vlans, index, vlan)
		mlx5_esw_bridge_vlan_cleanup(port, vlan, bridge);
}

static int mlx5_esw_bridge_port_vlans_recreate(struct mlx5_esw_bridge_port *port,
					       struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_offloads *br_offloads = bridge->br_offloads;
	struct mlx5_esw_bridge_vlan *vlan;
	unsigned long i;
	int err;

	xa_for_each(&port->vlans, i, vlan) {
		mlx5_esw_bridge_vlan_flush(port, vlan, bridge);
		err = mlx5_esw_bridge_vlan_push_pop_create(bridge->vlan_proto, vlan->flags, port,
							   vlan, br_offloads->esw);
		if (err) {
			esw_warn(br_offloads->esw->dev,
				 "Failed to create VLAN=%u(proto=%x) push/pop actions (vport=%u,err=%d)\n",
				 vlan->vid, bridge->vlan_proto, port->vport_num,
				 err);
			return err;
		}
	}

	return 0;
}

static int
mlx5_esw_bridge_vlans_recreate(struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_offloads *br_offloads = bridge->br_offloads;
	struct mlx5_esw_bridge_port *port;
	unsigned long i;
	int err;

	xa_for_each(&br_offloads->ports, i, port) {
		if (port->bridge != bridge)
			continue;

		err = mlx5_esw_bridge_port_vlans_recreate(port, bridge);
		if (err)
			return err;
	}

	return 0;
}

static struct mlx5_esw_bridge_vlan *
mlx5_esw_bridge_port_vlan_lookup(u16 vid, u16 vport_num, u16 esw_owner_vhca_id,
				 struct mlx5_esw_bridge *bridge, struct mlx5_eswitch *esw)
{
	struct mlx5_esw_bridge_port *port;
	struct mlx5_esw_bridge_vlan *vlan;

	port = mlx5_esw_bridge_port_lookup(vport_num, esw_owner_vhca_id, bridge->br_offloads);
	if (!port) {
		/* FDB is added asynchronously on wq while port might have been deleted
		 * concurrently. Report on 'info' logging level and skip the FDB offload.
		 */
		esw_info(esw->dev, "Failed to lookup bridge port (vport=%u)\n", vport_num);
		return ERR_PTR(-EINVAL);
	}

	vlan = mlx5_esw_bridge_vlan_lookup(vid, port);
	if (!vlan) {
		/* FDB is added asynchronously on wq while vlan might have been deleted
		 * concurrently. Report on 'info' logging level and skip the FDB offload.
		 */
		esw_info(esw->dev, "Failed to lookup bridge port vlan metadata (vport=%u)\n",
			 vport_num);
		return ERR_PTR(-EINVAL);
	}

	return vlan;
}

static struct mlx5_esw_bridge_fdb_entry *
mlx5_esw_bridge_fdb_lookup(struct mlx5_esw_bridge *bridge,
			   const unsigned char *addr, u16 vid)
{
	struct mlx5_esw_bridge_fdb_key key = {};

	ether_addr_copy(key.addr, addr);
	key.vid = vid;
	return rhashtable_lookup_fast(&bridge->fdb_ht, &key, fdb_ht_params);
}

static struct mlx5_esw_bridge_fdb_entry *
mlx5_esw_bridge_fdb_entry_init(struct net_device *dev, u16 vport_num, u16 esw_owner_vhca_id,
			       const unsigned char *addr, u16 vid, bool added_by_user, bool peer,
			       struct mlx5_eswitch *esw, struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_vlan *vlan = NULL;
	struct mlx5_esw_bridge_fdb_entry *entry;
	struct mlx5_flow_handle *handle;
	struct mlx5_fc *counter;
	int err;

	if (bridge->flags & MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG && vid) {
		vlan = mlx5_esw_bridge_port_vlan_lookup(vid, vport_num, esw_owner_vhca_id, bridge,
							esw);
		if (IS_ERR(vlan))
			return ERR_CAST(vlan);
	}

	entry = mlx5_esw_bridge_fdb_lookup(bridge, addr, vid);
	if (entry)
		mlx5_esw_bridge_fdb_entry_notify_and_cleanup(entry, bridge);

	entry = kvzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	ether_addr_copy(entry->key.addr, addr);
	entry->key.vid = vid;
	entry->dev = dev;
	entry->vport_num = vport_num;
	entry->esw_owner_vhca_id = esw_owner_vhca_id;
	entry->lastuse = jiffies;
	if (added_by_user)
		entry->flags |= MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER;
	if (peer)
		entry->flags |= MLX5_ESW_BRIDGE_FLAG_PEER;

	counter = mlx5_fc_create(esw->dev, true);
	if (IS_ERR(counter)) {
		err = PTR_ERR(counter);
		goto err_ingress_fc_create;
	}
	entry->ingress_counter = counter;

	handle = peer ?
		mlx5_esw_bridge_ingress_flow_peer_create(vport_num, esw_owner_vhca_id,
							 addr, vlan, mlx5_fc_id(counter),
							 bridge) :
		mlx5_esw_bridge_ingress_flow_create(vport_num, addr, vlan,
						    mlx5_fc_id(counter), bridge);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		esw_warn(esw->dev, "Failed to create ingress flow(vport=%u,err=%d,peer=%d)\n",
			 vport_num, err, peer);
		goto err_ingress_flow_create;
	}
	entry->ingress_handle = handle;

	if (bridge->flags & MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG) {
		handle = mlx5_esw_bridge_ingress_filter_flow_create(vport_num, addr, bridge);
		if (IS_ERR(handle)) {
			err = PTR_ERR(handle);
			esw_warn(esw->dev, "Failed to create ingress filter(vport=%u,err=%d)\n",
				 vport_num, err);
			goto err_ingress_filter_flow_create;
		}
		entry->filter_handle = handle;
	}

	handle = mlx5_esw_bridge_egress_flow_create(vport_num, esw_owner_vhca_id, addr, vlan,
						    bridge);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		esw_warn(esw->dev, "Failed to create egress flow(vport=%u,err=%d)\n",
			 vport_num, err);
		goto err_egress_flow_create;
	}
	entry->egress_handle = handle;

	err = rhashtable_insert_fast(&bridge->fdb_ht, &entry->ht_node, fdb_ht_params);
	if (err) {
		esw_warn(esw->dev, "Failed to insert FDB flow(vport=%u,err=%d)\n", vport_num, err);
		goto err_ht_init;
	}

	if (vlan)
		list_add(&entry->vlan_list, &vlan->fdb_list);
	else
		INIT_LIST_HEAD(&entry->vlan_list);
	list_add(&entry->list, &bridge->fdb_list);

	trace_mlx5_esw_bridge_fdb_entry_init(entry);
	return entry;

err_ht_init:
	mlx5_del_flow_rules(entry->egress_handle);
err_egress_flow_create:
	if (entry->filter_handle)
		mlx5_del_flow_rules(entry->filter_handle);
err_ingress_filter_flow_create:
	mlx5_del_flow_rules(entry->ingress_handle);
err_ingress_flow_create:
	mlx5_fc_destroy(esw->dev, entry->ingress_counter);
err_ingress_fc_create:
	kvfree(entry);
	return ERR_PTR(err);
}

int mlx5_esw_bridge_ageing_time_set(u16 vport_num, u16 esw_owner_vhca_id, unsigned long ageing_time,
				    struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge *bridge;

	bridge = mlx5_esw_bridge_from_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!bridge)
		return -EINVAL;

	bridge->ageing_time = clock_t_to_jiffies(ageing_time);
	return 0;
}

int mlx5_esw_bridge_vlan_filtering_set(u16 vport_num, u16 esw_owner_vhca_id, bool enable,
				       struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge *bridge;
	bool filtering;

	bridge = mlx5_esw_bridge_from_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!bridge)
		return -EINVAL;

	filtering = bridge->flags & MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG;
	if (filtering == enable)
		return 0;

	mlx5_esw_bridge_fdb_flush(bridge);
	mlx5_esw_bridge_mdb_flush(bridge);
	if (enable)
		bridge->flags |= MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG;
	else
		bridge->flags &= ~MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG;

	return 0;
}

int mlx5_esw_bridge_vlan_proto_set(u16 vport_num, u16 esw_owner_vhca_id, u16 proto,
				   struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge *bridge;

	bridge = mlx5_esw_bridge_from_port_lookup(vport_num, esw_owner_vhca_id,
						  br_offloads);
	if (!bridge)
		return -EINVAL;

	if (bridge->vlan_proto == proto)
		return 0;
	if (proto != ETH_P_8021Q && proto != ETH_P_8021AD) {
		esw_warn(br_offloads->esw->dev, "Can't set unsupported VLAN protocol %x", proto);
		return -EOPNOTSUPP;
	}

	mlx5_esw_bridge_fdb_flush(bridge);
	mlx5_esw_bridge_mdb_flush(bridge);
	bridge->vlan_proto = proto;
	mlx5_esw_bridge_vlans_recreate(bridge);

	return 0;
}

int mlx5_esw_bridge_mcast_set(u16 vport_num, u16 esw_owner_vhca_id, bool enable,
			      struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_eswitch *esw = br_offloads->esw;
	struct mlx5_esw_bridge *bridge;
	int err = 0;
	bool mcast;

	if (!(MLX5_CAP_ESW_FLOWTABLE((esw)->dev, fdb_multi_path_any_table) ||
	      MLX5_CAP_ESW_FLOWTABLE((esw)->dev, fdb_multi_path_any_table_limit_regc)) ||
	    !MLX5_CAP_ESW_FLOWTABLE((esw)->dev, fdb_uplink_hairpin) ||
	    !MLX5_CAP_ESW_FLOWTABLE_FDB((esw)->dev, ignore_flow_level))
		return -EOPNOTSUPP;

	bridge = mlx5_esw_bridge_from_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!bridge)
		return -EINVAL;

	mcast = bridge->flags & MLX5_ESW_BRIDGE_MCAST_FLAG;
	if (mcast == enable)
		return 0;

	if (enable)
		err = mlx5_esw_bridge_mcast_enable(bridge);
	else
		mlx5_esw_bridge_mcast_disable(bridge);

	return err;
}

static int mlx5_esw_bridge_vport_init(u16 vport_num, u16 esw_owner_vhca_id, u16 flags,
				      struct mlx5_esw_bridge_offloads *br_offloads,
				      struct mlx5_esw_bridge *bridge)
{
	struct mlx5_eswitch *esw = br_offloads->esw;
	struct mlx5_esw_bridge_port *port;
	int err;

	port = kvzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->vport_num = vport_num;
	port->esw_owner_vhca_id = esw_owner_vhca_id;
	port->bridge = bridge;
	port->flags |= flags;
	xa_init(&port->vlans);

	err = mlx5_esw_bridge_port_mcast_init(port);
	if (err) {
		esw_warn(esw->dev,
			 "Failed to initialize port multicast (vport=%u,esw_owner_vhca_id=%u,err=%d)\n",
			 port->vport_num, port->esw_owner_vhca_id, err);
		goto err_port_mcast;
	}

	err = mlx5_esw_bridge_port_insert(port, br_offloads);
	if (err) {
		esw_warn(esw->dev,
			 "Failed to insert port metadata (vport=%u,esw_owner_vhca_id=%u,err=%d)\n",
			 port->vport_num, port->esw_owner_vhca_id, err);
		goto err_port_insert;
	}
	trace_mlx5_esw_bridge_vport_init(port);

	return 0;

err_port_insert:
	mlx5_esw_bridge_port_mcast_cleanup(port);
err_port_mcast:
	kvfree(port);
	return err;
}

static int mlx5_esw_bridge_vport_cleanup(struct mlx5_esw_bridge_offloads *br_offloads,
					 struct mlx5_esw_bridge_port *port)
{
	u16 vport_num = port->vport_num, esw_owner_vhca_id = port->esw_owner_vhca_id;
	struct mlx5_esw_bridge *bridge = port->bridge;
	struct mlx5_esw_bridge_fdb_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &bridge->fdb_list, list)
		if (entry->vport_num == vport_num && entry->esw_owner_vhca_id == esw_owner_vhca_id)
			mlx5_esw_bridge_fdb_entry_cleanup(entry, bridge);

	trace_mlx5_esw_bridge_vport_cleanup(port);
	mlx5_esw_bridge_port_vlans_flush(port, bridge);
	mlx5_esw_bridge_port_mcast_cleanup(port);
	mlx5_esw_bridge_port_erase(port, br_offloads);
	kvfree(port);
	mlx5_esw_bridge_put(br_offloads, bridge);
	return 0;
}

static int mlx5_esw_bridge_vport_link_with_flags(struct net_device *br_netdev, u16 vport_num,
						 u16 esw_owner_vhca_id, u16 flags,
						 struct mlx5_esw_bridge_offloads *br_offloads,
						 struct netlink_ext_ack *extack)
{
	struct mlx5_esw_bridge *bridge;
	int err;

	bridge = mlx5_esw_bridge_lookup(br_netdev, br_offloads);
	if (IS_ERR(bridge)) {
		NL_SET_ERR_MSG_MOD(extack, "Error checking for existing bridge with same ifindex");
		return PTR_ERR(bridge);
	}

	err = mlx5_esw_bridge_vport_init(vport_num, esw_owner_vhca_id, flags, br_offloads, bridge);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Error initializing port");
		goto err_vport;
	}
	return 0;

err_vport:
	mlx5_esw_bridge_put(br_offloads, bridge);
	return err;
}

int mlx5_esw_bridge_vport_link(struct net_device *br_netdev, u16 vport_num, u16 esw_owner_vhca_id,
			       struct mlx5_esw_bridge_offloads *br_offloads,
			       struct netlink_ext_ack *extack)
{
	return mlx5_esw_bridge_vport_link_with_flags(br_netdev, vport_num, esw_owner_vhca_id, 0,
						     br_offloads, extack);
}

int mlx5_esw_bridge_vport_unlink(struct net_device *br_netdev, u16 vport_num,
				 u16 esw_owner_vhca_id,
				 struct mlx5_esw_bridge_offloads *br_offloads,
				 struct netlink_ext_ack *extack)
{
	struct mlx5_esw_bridge_port *port;
	int err;

	port = mlx5_esw_bridge_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!port) {
		NL_SET_ERR_MSG_MOD(extack, "Port is not attached to any bridge");
		return -EINVAL;
	}
	if (port->bridge->ifindex != br_netdev->ifindex) {
		NL_SET_ERR_MSG_MOD(extack, "Port is attached to another bridge");
		return -EINVAL;
	}

	err = mlx5_esw_bridge_vport_cleanup(br_offloads, port);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "Port cleanup failed");
	return err;
}

int mlx5_esw_bridge_vport_peer_link(struct net_device *br_netdev, u16 vport_num,
				    u16 esw_owner_vhca_id,
				    struct mlx5_esw_bridge_offloads *br_offloads,
				    struct netlink_ext_ack *extack)
{
	if (!MLX5_CAP_ESW(br_offloads->esw->dev, merged_eswitch))
		return 0;

	return mlx5_esw_bridge_vport_link_with_flags(br_netdev, vport_num, esw_owner_vhca_id,
						     MLX5_ESW_BRIDGE_PORT_FLAG_PEER,
						     br_offloads, extack);
}

int mlx5_esw_bridge_vport_peer_unlink(struct net_device *br_netdev, u16 vport_num,
				      u16 esw_owner_vhca_id,
				      struct mlx5_esw_bridge_offloads *br_offloads,
				      struct netlink_ext_ack *extack)
{
	return mlx5_esw_bridge_vport_unlink(br_netdev, vport_num, esw_owner_vhca_id, br_offloads,
					    extack);
}

int mlx5_esw_bridge_port_vlan_add(u16 vport_num, u16 esw_owner_vhca_id, u16 vid, u16 flags,
				  struct mlx5_esw_bridge_offloads *br_offloads,
				  struct netlink_ext_ack *extack)
{
	struct mlx5_esw_bridge_port *port;
	struct mlx5_esw_bridge_vlan *vlan;

	port = mlx5_esw_bridge_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!port)
		return -EINVAL;

	vlan = mlx5_esw_bridge_vlan_lookup(vid, port);
	if (vlan) {
		if (vlan->flags == flags)
			return 0;
		mlx5_esw_bridge_vlan_cleanup(port, vlan, port->bridge);
	}

	vlan = mlx5_esw_bridge_vlan_create(port->bridge->vlan_proto, vid, flags, port,
					   br_offloads->esw);
	if (IS_ERR(vlan)) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to create VLAN entry");
		return PTR_ERR(vlan);
	}
	return 0;
}

void mlx5_esw_bridge_port_vlan_del(u16 vport_num, u16 esw_owner_vhca_id, u16 vid,
				   struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge_port *port;
	struct mlx5_esw_bridge_vlan *vlan;

	port = mlx5_esw_bridge_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!port)
		return;

	vlan = mlx5_esw_bridge_vlan_lookup(vid, port);
	if (!vlan)
		return;
	mlx5_esw_bridge_vlan_cleanup(port, vlan, port->bridge);
}

void mlx5_esw_bridge_fdb_update_used(struct net_device *dev, u16 vport_num, u16 esw_owner_vhca_id,
				     struct mlx5_esw_bridge_offloads *br_offloads,
				     struct switchdev_notifier_fdb_info *fdb_info)
{
	struct mlx5_esw_bridge_fdb_entry *entry;
	struct mlx5_esw_bridge *bridge;

	bridge = mlx5_esw_bridge_from_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!bridge)
		return;

	entry = mlx5_esw_bridge_fdb_lookup(bridge, fdb_info->addr, fdb_info->vid);
	if (!entry) {
		esw_debug(br_offloads->esw->dev,
			  "FDB update entry with specified key not found (MAC=%pM,vid=%u,vport=%u)\n",
			  fdb_info->addr, fdb_info->vid, vport_num);
		return;
	}

	entry->lastuse = jiffies;
}

void mlx5_esw_bridge_fdb_mark_deleted(struct net_device *dev, u16 vport_num, u16 esw_owner_vhca_id,
				      struct mlx5_esw_bridge_offloads *br_offloads,
				      struct switchdev_notifier_fdb_info *fdb_info)
{
	struct mlx5_esw_bridge_fdb_entry *entry;
	struct mlx5_esw_bridge *bridge;

	bridge = mlx5_esw_bridge_from_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!bridge)
		return;

	entry = mlx5_esw_bridge_fdb_lookup(bridge, fdb_info->addr, fdb_info->vid);
	if (!entry) {
		esw_debug(br_offloads->esw->dev,
			  "FDB mark deleted entry with specified key not found (MAC=%pM,vid=%u,vport=%u)\n",
			  fdb_info->addr, fdb_info->vid, vport_num);
		return;
	}

	entry->flags |= MLX5_ESW_BRIDGE_FLAG_DELETED;
}

void mlx5_esw_bridge_fdb_create(struct net_device *dev, u16 vport_num, u16 esw_owner_vhca_id,
				struct mlx5_esw_bridge_offloads *br_offloads,
				struct switchdev_notifier_fdb_info *fdb_info)
{
	struct mlx5_esw_bridge_fdb_entry *entry;
	struct mlx5_esw_bridge_port *port;
	struct mlx5_esw_bridge *bridge;

	port = mlx5_esw_bridge_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!port)
		return;

	bridge = port->bridge;
	entry = mlx5_esw_bridge_fdb_entry_init(dev, vport_num, esw_owner_vhca_id, fdb_info->addr,
					       fdb_info->vid, fdb_info->added_by_user,
					       port->flags & MLX5_ESW_BRIDGE_PORT_FLAG_PEER,
					       br_offloads->esw, bridge);
	if (IS_ERR(entry))
		return;

	if (entry->flags & MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER)
		mlx5_esw_bridge_fdb_offload_notify(dev, entry->key.addr, entry->key.vid,
						   SWITCHDEV_FDB_OFFLOADED);
	else if (!(entry->flags & MLX5_ESW_BRIDGE_FLAG_PEER))
		/* Take over dynamic entries to prevent kernel bridge from aging them out. */
		mlx5_esw_bridge_fdb_offload_notify(dev, entry->key.addr, entry->key.vid,
						   SWITCHDEV_FDB_ADD_TO_BRIDGE);
}

void mlx5_esw_bridge_fdb_remove(struct net_device *dev, u16 vport_num, u16 esw_owner_vhca_id,
				struct mlx5_esw_bridge_offloads *br_offloads,
				struct switchdev_notifier_fdb_info *fdb_info)
{
	struct mlx5_eswitch *esw = br_offloads->esw;
	struct mlx5_esw_bridge_fdb_entry *entry;
	struct mlx5_esw_bridge *bridge;

	bridge = mlx5_esw_bridge_from_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!bridge)
		return;

	entry = mlx5_esw_bridge_fdb_lookup(bridge, fdb_info->addr, fdb_info->vid);
	if (!entry) {
		esw_debug(esw->dev,
			  "FDB remove entry with specified key not found (MAC=%pM,vid=%u,vport=%u)\n",
			  fdb_info->addr, fdb_info->vid, vport_num);
		return;
	}

	mlx5_esw_bridge_fdb_entry_notify_and_cleanup(entry, bridge);
}

void mlx5_esw_bridge_update(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge_fdb_entry *entry, *tmp;
	struct mlx5_esw_bridge *bridge;

	list_for_each_entry(bridge, &br_offloads->bridges, list) {
		list_for_each_entry_safe(entry, tmp, &bridge->fdb_list, list) {
			unsigned long lastuse =
				(unsigned long)mlx5_fc_query_lastuse(entry->ingress_counter);

			if (entry->flags & (MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER |
					    MLX5_ESW_BRIDGE_FLAG_DELETED))
				continue;

			if (time_after(lastuse, entry->lastuse))
				mlx5_esw_bridge_fdb_entry_refresh(entry);
			else if (!(entry->flags & MLX5_ESW_BRIDGE_FLAG_PEER) &&
				 time_is_before_jiffies(entry->lastuse + bridge->ageing_time))
				mlx5_esw_bridge_fdb_entry_notify_and_cleanup(entry, bridge);
		}
	}
}

int mlx5_esw_bridge_port_mdb_add(struct net_device *dev, u16 vport_num, u16 esw_owner_vhca_id,
				 const unsigned char *addr, u16 vid,
				 struct mlx5_esw_bridge_offloads *br_offloads,
				 struct netlink_ext_ack *extack)
{
	struct mlx5_esw_bridge_vlan *vlan;
	struct mlx5_esw_bridge_port *port;
	struct mlx5_esw_bridge *bridge;
	int err;

	port = mlx5_esw_bridge_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!port) {
		esw_warn(br_offloads->esw->dev,
			 "Failed to lookup bridge port to add MDB (MAC=%pM,vport=%u)\n",
			 addr, vport_num);
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Failed to lookup bridge port to add MDB (MAC=%pM,vport=%u)\n",
				       addr, vport_num);
		return -EINVAL;
	}

	bridge = port->bridge;
	if (bridge->flags & MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG && vid) {
		vlan = mlx5_esw_bridge_vlan_lookup(vid, port);
		if (!vlan) {
			esw_warn(br_offloads->esw->dev,
				 "Failed to lookup bridge port vlan metadata to create MDB (MAC=%pM,vid=%u,vport=%u)\n",
				 addr, vid, vport_num);
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Failed to lookup vlan metadata for MDB (MAC=%pM,vid=%u,vport=%u)\n",
					       addr, vid, vport_num);
			return -EINVAL;
		}
	}

	err = mlx5_esw_bridge_port_mdb_attach(dev, port, addr, vid);
	if (err) {
		NL_SET_ERR_MSG_FMT_MOD(extack, "Failed to add MDB (MAC=%pM,vid=%u,vport=%u)\n",
				       addr, vid, vport_num);
		return err;
	}

	return 0;
}

void mlx5_esw_bridge_port_mdb_del(struct net_device *dev, u16 vport_num, u16 esw_owner_vhca_id,
				  const unsigned char *addr, u16 vid,
				  struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge_port *port;

	port = mlx5_esw_bridge_port_lookup(vport_num, esw_owner_vhca_id, br_offloads);
	if (!port)
		return;

	mlx5_esw_bridge_port_mdb_detach(dev, port, addr, vid);
}

static void mlx5_esw_bridge_flush(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge_port *port;
	unsigned long i;

	xa_for_each(&br_offloads->ports, i, port)
		mlx5_esw_bridge_vport_cleanup(br_offloads, port);

	WARN_ONCE(!list_empty(&br_offloads->bridges),
		  "Cleaning up bridge offloads while still having bridges attached\n");
}

struct mlx5_esw_bridge_offloads *mlx5_esw_bridge_init(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_bridge_offloads *br_offloads;

	ASSERT_RTNL();

	br_offloads = kvzalloc(sizeof(*br_offloads), GFP_KERNEL);
	if (!br_offloads)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&br_offloads->bridges);
	xa_init(&br_offloads->ports);
	br_offloads->esw = esw;
	esw->br_offloads = br_offloads;
	mlx5_esw_bridge_debugfs_offloads_init(br_offloads);

	return br_offloads;
}

void mlx5_esw_bridge_cleanup(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_bridge_offloads *br_offloads = esw->br_offloads;

	ASSERT_RTNL();

	if (!br_offloads)
		return;

	mlx5_esw_bridge_flush(br_offloads);
	WARN_ON(!xa_empty(&br_offloads->ports));
	mlx5_esw_bridge_debugfs_offloads_cleanup(br_offloads);

	esw->br_offloads = NULL;
	kvfree(br_offloads);
}
