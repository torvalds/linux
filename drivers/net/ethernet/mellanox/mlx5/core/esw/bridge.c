// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include <linux/list.h>
#include <linux/notifier.h>
#include <net/netevent.h>
#include <net/switchdev.h>
#include "bridge.h"
#include "eswitch.h"
#include "bridge_priv.h"
#define CREATE_TRACE_POINTS
#include "diag/bridge_tracepoint.h"

#define MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE 64000
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_FROM 0
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_TO (MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE / 4 - 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_FILTER_GRP_IDX_FROM \
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_FILTER_GRP_IDX_TO \
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE / 2 - 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_FROM \
	(MLX5_ESW_BRIDGE_INGRESS_TABLE_FILTER_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_TO (MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE - 1)

#define MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE 64000
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_IDX_FROM 0
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_IDX_TO (MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE / 2 - 1)
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_FROM \
	(MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_IDX_TO + 1)
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_TO (MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE - 1)

#define MLX5_ESW_BRIDGE_SKIP_TABLE_SIZE 0

enum {
	MLX5_ESW_BRIDGE_LEVEL_INGRESS_TABLE,
	MLX5_ESW_BRIDGE_LEVEL_EGRESS_TABLE,
	MLX5_ESW_BRIDGE_LEVEL_SKIP_TABLE,
};

static const struct rhashtable_params fdb_ht_params = {
	.key_offset = offsetof(struct mlx5_esw_bridge_fdb_entry, key),
	.key_len = sizeof(struct mlx5_esw_bridge_fdb_key),
	.head_offset = offsetof(struct mlx5_esw_bridge_fdb_entry, ht_node),
	.automatic_shrinking = true,
};

enum {
	MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG = BIT(0),
};

struct mlx5_esw_bridge {
	int ifindex;
	int refcnt;
	struct list_head list;
	struct mlx5_esw_bridge_offloads *br_offloads;

	struct list_head fdb_list;
	struct rhashtable fdb_ht;
	struct xarray vports;

	struct mlx5_flow_table *egress_ft;
	struct mlx5_flow_group *egress_vlan_fg;
	struct mlx5_flow_group *egress_mac_fg;
	unsigned long ageing_time;
	u32 flags;
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
mlx5_esw_bridge_ingress_vlan_fg_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *ingress_ft)
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
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.cvlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.first_vid);

	MLX5_SET(fte_match_param, match, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_mask());

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_VLAN_GRP_IDX_TO);

	fg = mlx5_create_flow_group(ingress_ft, in);
	kvfree(in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create VLAN flow group for bridge ingress table (err=%ld)\n",
			 PTR_ERR(fg));

	return fg;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_filter_fg_create(struct mlx5_eswitch *esw,
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
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.cvlan_tag);

	MLX5_SET(fte_match_param, match, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_mask());

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_FILTER_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_FILTER_GRP_IDX_TO);

	fg = mlx5_create_flow_group(ingress_ft, in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create bridge ingress table VLAN filter flow group (err=%ld)\n",
			 PTR_ERR(fg));

	kvfree(in);
	return fg;
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
mlx5_esw_bridge_egress_vlan_fg_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *egress_ft)
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
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.cvlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.first_vid);

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_EGRESS_TABLE_VLAN_GRP_IDX_TO);

	fg = mlx5_create_flow_group(egress_ft, in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create VLAN flow group for bridge egress table (err=%ld)\n",
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
	struct mlx5_flow_group *mac_fg, *filter_fg, *vlan_fg;
	struct mlx5_flow_table *ingress_ft, *skip_ft;
	int err;

	if (!mlx5_eswitch_vport_match_metadata_enabled(br_offloads->esw))
		return -EOPNOTSUPP;

	ingress_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE,
						  MLX5_ESW_BRIDGE_LEVEL_INGRESS_TABLE,
						  br_offloads->esw);
	if (IS_ERR(ingress_ft))
		return PTR_ERR(ingress_ft);

	skip_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_SKIP_TABLE_SIZE,
					       MLX5_ESW_BRIDGE_LEVEL_SKIP_TABLE,
					       br_offloads->esw);
	if (IS_ERR(skip_ft)) {
		err = PTR_ERR(skip_ft);
		goto err_skip_tbl;
	}

	vlan_fg = mlx5_esw_bridge_ingress_vlan_fg_create(br_offloads->esw, ingress_ft);
	if (IS_ERR(vlan_fg)) {
		err = PTR_ERR(vlan_fg);
		goto err_vlan_fg;
	}

	filter_fg = mlx5_esw_bridge_ingress_filter_fg_create(br_offloads->esw, ingress_ft);
	if (IS_ERR(filter_fg)) {
		err = PTR_ERR(filter_fg);
		goto err_filter_fg;
	}

	mac_fg = mlx5_esw_bridge_ingress_mac_fg_create(br_offloads->esw, ingress_ft);
	if (IS_ERR(mac_fg)) {
		err = PTR_ERR(mac_fg);
		goto err_mac_fg;
	}

	br_offloads->ingress_ft = ingress_ft;
	br_offloads->skip_ft = skip_ft;
	br_offloads->ingress_vlan_fg = vlan_fg;
	br_offloads->ingress_filter_fg = filter_fg;
	br_offloads->ingress_mac_fg = mac_fg;
	return 0;

err_mac_fg:
	mlx5_destroy_flow_group(filter_fg);
err_filter_fg:
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
	mlx5_destroy_flow_group(br_offloads->ingress_filter_fg);
	br_offloads->ingress_filter_fg = NULL;
	mlx5_destroy_flow_group(br_offloads->ingress_vlan_fg);
	br_offloads->ingress_vlan_fg = NULL;
	mlx5_destroy_flow_table(br_offloads->skip_ft);
	br_offloads->skip_ft = NULL;
	mlx5_destroy_flow_table(br_offloads->ingress_ft);
	br_offloads->ingress_ft = NULL;
}

static int
mlx5_esw_bridge_egress_table_init(struct mlx5_esw_bridge_offloads *br_offloads,
				  struct mlx5_esw_bridge *bridge)
{
	struct mlx5_flow_group *mac_fg, *vlan_fg;
	struct mlx5_flow_table *egress_ft;
	int err;

	egress_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE,
						 MLX5_ESW_BRIDGE_LEVEL_EGRESS_TABLE,
						 br_offloads->esw);
	if (IS_ERR(egress_ft))
		return PTR_ERR(egress_ft);

	vlan_fg = mlx5_esw_bridge_egress_vlan_fg_create(br_offloads->esw, egress_ft);
	if (IS_ERR(vlan_fg)) {
		err = PTR_ERR(vlan_fg);
		goto err_vlan_fg;
	}

	mac_fg = mlx5_esw_bridge_egress_mac_fg_create(br_offloads->esw, egress_ft);
	if (IS_ERR(mac_fg)) {
		err = PTR_ERR(mac_fg);
		goto err_mac_fg;
	}

	bridge->egress_ft = egress_ft;
	bridge->egress_vlan_fg = vlan_fg;
	bridge->egress_mac_fg = mac_fg;
	return 0;

err_mac_fg:
	mlx5_destroy_flow_group(vlan_fg);
err_vlan_fg:
	mlx5_destroy_flow_table(egress_ft);
	return err;
}

static void
mlx5_esw_bridge_egress_table_cleanup(struct mlx5_esw_bridge *bridge)
{
	mlx5_destroy_flow_group(bridge->egress_mac_fg);
	mlx5_destroy_flow_group(bridge->egress_vlan_fg);
	mlx5_destroy_flow_table(bridge->egress_ft);
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_ingress_flow_create(u16 vport_num, const unsigned char *addr,
				    struct mlx5_esw_bridge_vlan *vlan, u32 counter_id,
				    struct mlx5_esw_bridge *bridge)
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
		 mlx5_eswitch_get_vport_metadata_for_match(br_offloads->esw, vport_num));

	if (vlan && vlan->pkt_reformat_push) {
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
		flow_act.pkt_reformat = vlan->pkt_reformat_push;
	} else if (vlan) {
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.cvlan_tag);
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
				 outer_headers.cvlan_tag);
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

	MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
			 outer_headers.cvlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
			 outer_headers.cvlan_tag);

	handle = mlx5_add_flow_rules(br_offloads->ingress_ft, rule_spec, &flow_act, &dest, 1);

	kvfree(rule_spec);
	return handle;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_egress_flow_create(u16 vport_num, const unsigned char *addr,
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

		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.cvlan_tag);
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
				 outer_headers.cvlan_tag);
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.first_vid);
		MLX5_SET(fte_match_param, rule_spec->match_value, outer_headers.first_vid,
			 vlan->vid);
	}

	handle = mlx5_add_flow_rules(bridge->egress_ft, rule_spec, &flow_act, &dest, 1);

	kvfree(rule_spec);
	return handle;
}

static struct mlx5_esw_bridge *mlx5_esw_bridge_create(int ifindex,
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

	INIT_LIST_HEAD(&bridge->fdb_list);
	xa_init(&bridge->vports);
	bridge->ifindex = ifindex;
	bridge->refcnt = 1;
	bridge->ageing_time = clock_t_to_jiffies(BR_DEFAULT_AGEING_TIME);
	list_add(&bridge->list, &br_offloads->bridges);

	return bridge;

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

	mlx5_esw_bridge_egress_table_cleanup(bridge);
	WARN_ON(!xa_empty(&bridge->vports));
	list_del(&bridge->list);
	rhashtable_destroy(&bridge->fdb_ht);
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

static int mlx5_esw_bridge_port_insert(struct mlx5_esw_bridge_port *port,
				       struct mlx5_esw_bridge *bridge)
{
	return xa_insert(&bridge->vports, port->vport_num, port, GFP_KERNEL);
}

static struct mlx5_esw_bridge_port *
mlx5_esw_bridge_port_lookup(u16 vport_num, struct mlx5_esw_bridge *bridge)
{
	return xa_load(&bridge->vports, vport_num);
}

static void mlx5_esw_bridge_port_erase(struct mlx5_esw_bridge_port *port,
				       struct mlx5_esw_bridge *bridge)
{
	xa_erase(&bridge->vports, port->vport_num);
}

static void mlx5_esw_bridge_fdb_entry_refresh(unsigned long lastuse,
					      struct mlx5_esw_bridge_fdb_entry *entry)
{
	trace_mlx5_esw_bridge_fdb_entry_refresh(entry);

	entry->lastuse = lastuse;
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

static void mlx5_esw_bridge_fdb_flush(struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_fdb_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &bridge->fdb_list, list) {
		if (!(entry->flags & MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER))
			mlx5_esw_bridge_fdb_offload_notify(entry->dev, entry->key.addr,
							   entry->key.vid,
							   SWITCHDEV_FDB_DEL_TO_BRIDGE);
		mlx5_esw_bridge_fdb_entry_cleanup(entry, bridge);
	}
}

static struct mlx5_esw_bridge_vlan *
mlx5_esw_bridge_vlan_lookup(u16 vid, struct mlx5_esw_bridge_port *port)
{
	return xa_load(&port->vlans, vid);
}

static int
mlx5_esw_bridge_vlan_push_create(struct mlx5_esw_bridge_vlan *vlan, struct mlx5_eswitch *esw)
{
	struct {
		__be16	h_vlan_proto;
		__be16	h_vlan_TCI;
	} vlan_hdr = { htons(ETH_P_8021Q), htons(vlan->vid) };
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
	struct mlx5_pkt_reformat_params reformat_params = {};
	struct mlx5_pkt_reformat *pkt_reformat;

	if (!BIT(MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, reformat_remove)) ||
	    MLX5_CAP_GEN_2(esw->dev, max_reformat_remove_size) < sizeof(struct vlan_hdr) ||
	    MLX5_CAP_GEN_2(esw->dev, max_reformat_remove_offset) <
	    offsetof(struct vlan_ethhdr, h_vlan_proto)) {
		esw_warn(esw->dev, "Packet reformat REMOVE_HEADER is not supported\n");
		return -EOPNOTSUPP;
	}

	reformat_params.type = MLX5_REFORMAT_TYPE_REMOVE_HDR;
	reformat_params.param_0 = MLX5_REFORMAT_CONTEXT_ANCHOR_MAC_START;
	reformat_params.param_1 = offsetof(struct vlan_ethhdr, h_vlan_proto);
	reformat_params.size = sizeof(struct vlan_hdr);
	pkt_reformat = mlx5_packet_reformat_alloc(esw->dev,
						  &reformat_params,
						  MLX5_FLOW_NAMESPACE_FDB);
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

static struct mlx5_esw_bridge_vlan *
mlx5_esw_bridge_vlan_create(u16 vid, u16 flags, struct mlx5_esw_bridge_port *port,
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

	if (flags & BRIDGE_VLAN_INFO_PVID) {
		err = mlx5_esw_bridge_vlan_push_create(vlan, esw);
		if (err)
			goto err_vlan_push;
	}
	if (flags & BRIDGE_VLAN_INFO_UNTAGGED) {
		err = mlx5_esw_bridge_vlan_pop_create(vlan, esw);
		if (err)
			goto err_vlan_pop;
	}

	err = xa_insert(&port->vlans, vid, vlan, GFP_KERNEL);
	if (err)
		goto err_xa_insert;

	trace_mlx5_esw_bridge_vlan_create(vlan);
	return vlan;

err_xa_insert:
	if (vlan->pkt_reformat_pop)
		mlx5_esw_bridge_vlan_pop_cleanup(vlan, esw);
err_vlan_pop:
	if (vlan->pkt_reformat_push)
		mlx5_esw_bridge_vlan_push_cleanup(vlan, esw);
err_vlan_push:
	kvfree(vlan);
	return ERR_PTR(err);
}

static void mlx5_esw_bridge_vlan_erase(struct mlx5_esw_bridge_port *port,
				       struct mlx5_esw_bridge_vlan *vlan)
{
	xa_erase(&port->vlans, vlan->vid);
}

static void mlx5_esw_bridge_vlan_flush(struct mlx5_esw_bridge_vlan *vlan,
				       struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_fdb_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &vlan->fdb_list, vlan_list) {
		if (!(entry->flags & MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER))
			mlx5_esw_bridge_fdb_offload_notify(entry->dev, entry->key.addr,
							   entry->key.vid,
							   SWITCHDEV_FDB_DEL_TO_BRIDGE);
		mlx5_esw_bridge_fdb_entry_cleanup(entry, bridge);
	}

	if (vlan->pkt_reformat_pop)
		mlx5_esw_bridge_vlan_pop_cleanup(vlan, bridge->br_offloads->esw);
	if (vlan->pkt_reformat_push)
		mlx5_esw_bridge_vlan_push_cleanup(vlan, bridge->br_offloads->esw);
}

static void mlx5_esw_bridge_vlan_cleanup(struct mlx5_esw_bridge_port *port,
					 struct mlx5_esw_bridge_vlan *vlan,
					 struct mlx5_esw_bridge *bridge)
{
	trace_mlx5_esw_bridge_vlan_cleanup(vlan);
	mlx5_esw_bridge_vlan_flush(vlan, bridge);
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

static struct mlx5_esw_bridge_vlan *
mlx5_esw_bridge_port_vlan_lookup(u16 vid, u16 vport_num, struct mlx5_esw_bridge *bridge,
				 struct mlx5_eswitch *esw)
{
	struct mlx5_esw_bridge_port *port;
	struct mlx5_esw_bridge_vlan *vlan;

	port = mlx5_esw_bridge_port_lookup(vport_num, bridge);
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
mlx5_esw_bridge_fdb_entry_init(struct net_device *dev, u16 vport_num, const unsigned char *addr,
			       u16 vid, bool added_by_user, struct mlx5_eswitch *esw,
			       struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_vlan *vlan = NULL;
	struct mlx5_esw_bridge_fdb_entry *entry;
	struct mlx5_flow_handle *handle;
	struct mlx5_fc *counter;
	int err;

	if (bridge->flags & MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG && vid) {
		vlan = mlx5_esw_bridge_port_vlan_lookup(vid, vport_num, bridge, esw);
		if (IS_ERR(vlan))
			return ERR_CAST(vlan);
	}

	entry = kvzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	ether_addr_copy(entry->key.addr, addr);
	entry->key.vid = vid;
	entry->dev = dev;
	entry->vport_num = vport_num;
	entry->lastuse = jiffies;
	if (added_by_user)
		entry->flags |= MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER;

	counter = mlx5_fc_create(esw->dev, true);
	if (IS_ERR(counter)) {
		err = PTR_ERR(counter);
		goto err_ingress_fc_create;
	}
	entry->ingress_counter = counter;

	handle = mlx5_esw_bridge_ingress_flow_create(vport_num, addr, vlan, mlx5_fc_id(counter),
						     bridge);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		esw_warn(esw->dev, "Failed to create ingress flow(vport=%u,err=%d)\n",
			 vport_num, err);
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

	handle = mlx5_esw_bridge_egress_flow_create(vport_num, addr, vlan, bridge);
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

int mlx5_esw_bridge_ageing_time_set(unsigned long ageing_time, struct mlx5_eswitch *esw,
				    struct mlx5_vport *vport)
{
	if (!vport->bridge)
		return -EINVAL;

	vport->bridge->ageing_time = clock_t_to_jiffies(ageing_time);
	return 0;
}

int mlx5_esw_bridge_vlan_filtering_set(bool enable, struct mlx5_eswitch *esw,
				       struct mlx5_vport *vport)
{
	struct mlx5_esw_bridge *bridge;
	bool filtering;

	if (!vport->bridge)
		return -EINVAL;

	bridge = vport->bridge;
	filtering = bridge->flags & MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG;
	if (filtering == enable)
		return 0;

	mlx5_esw_bridge_fdb_flush(bridge);
	if (enable)
		bridge->flags |= MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG;
	else
		bridge->flags &= ~MLX5_ESW_BRIDGE_VLAN_FILTERING_FLAG;

	return 0;
}

static int mlx5_esw_bridge_vport_init(struct mlx5_esw_bridge_offloads *br_offloads,
				      struct mlx5_esw_bridge *bridge,
				      struct mlx5_vport *vport)
{
	struct mlx5_eswitch *esw = br_offloads->esw;
	struct mlx5_esw_bridge_port *port;
	int err;

	port = kvzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->vport_num = vport->vport;
	xa_init(&port->vlans);
	err = mlx5_esw_bridge_port_insert(port, bridge);
	if (err) {
		esw_warn(esw->dev, "Failed to insert port metadata (vport=%u,err=%d)\n",
			 vport->vport, err);
		goto err_port_insert;
	}
	trace_mlx5_esw_bridge_vport_init(port);

	vport->bridge = bridge;
	return 0;

err_port_insert:
	kvfree(port);
	return err;
}

static int mlx5_esw_bridge_vport_cleanup(struct mlx5_esw_bridge_offloads *br_offloads,
					 struct mlx5_vport *vport)
{
	struct mlx5_esw_bridge *bridge = vport->bridge;
	struct mlx5_esw_bridge_fdb_entry *entry, *tmp;
	struct mlx5_esw_bridge_port *port;

	list_for_each_entry_safe(entry, tmp, &bridge->fdb_list, list)
		if (entry->vport_num == vport->vport)
			mlx5_esw_bridge_fdb_entry_cleanup(entry, bridge);

	port = mlx5_esw_bridge_port_lookup(vport->vport, bridge);
	if (!port) {
		WARN(1, "Vport %u metadata not found on bridge", vport->vport);
		return -EINVAL;
	}

	trace_mlx5_esw_bridge_vport_cleanup(port);
	mlx5_esw_bridge_port_vlans_flush(port, bridge);
	mlx5_esw_bridge_port_erase(port, bridge);
	kvfree(port);
	mlx5_esw_bridge_put(br_offloads, bridge);
	vport->bridge = NULL;
	return 0;
}

int mlx5_esw_bridge_vport_link(int ifindex, struct mlx5_esw_bridge_offloads *br_offloads,
			       struct mlx5_vport *vport, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_bridge *bridge;
	int err;

	WARN_ON(vport->bridge);

	bridge = mlx5_esw_bridge_lookup(ifindex, br_offloads);
	if (IS_ERR(bridge)) {
		NL_SET_ERR_MSG_MOD(extack, "Error checking for existing bridge with same ifindex");
		return PTR_ERR(bridge);
	}

	err = mlx5_esw_bridge_vport_init(br_offloads, bridge, vport);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Error initializing port");
		goto err_vport;
	}
	return 0;

err_vport:
	mlx5_esw_bridge_put(br_offloads, bridge);
	return err;
}

int mlx5_esw_bridge_vport_unlink(int ifindex, struct mlx5_esw_bridge_offloads *br_offloads,
				 struct mlx5_vport *vport, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_bridge *bridge = vport->bridge;
	int err;

	if (!bridge) {
		NL_SET_ERR_MSG_MOD(extack, "Port is not attached to any bridge");
		return -EINVAL;
	}
	if (bridge->ifindex != ifindex) {
		NL_SET_ERR_MSG_MOD(extack, "Port is attached to another bridge");
		return -EINVAL;
	}

	err = mlx5_esw_bridge_vport_cleanup(br_offloads, vport);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "Port cleanup failed");
	return err;
}

int mlx5_esw_bridge_port_vlan_add(u16 vid, u16 flags, struct mlx5_eswitch *esw,
				  struct mlx5_vport *vport, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_bridge_port *port;
	struct mlx5_esw_bridge_vlan *vlan;

	port = mlx5_esw_bridge_port_lookup(vport->vport, vport->bridge);
	if (!port)
		return -EINVAL;

	vlan = mlx5_esw_bridge_vlan_lookup(vid, port);
	if (vlan) {
		if (vlan->flags == flags)
			return 0;
		mlx5_esw_bridge_vlan_cleanup(port, vlan, vport->bridge);
	}

	vlan = mlx5_esw_bridge_vlan_create(vid, flags, port, esw);
	if (IS_ERR(vlan)) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to create VLAN entry");
		return PTR_ERR(vlan);
	}
	return 0;
}

void mlx5_esw_bridge_port_vlan_del(u16 vid, struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	struct mlx5_esw_bridge_port *port;
	struct mlx5_esw_bridge_vlan *vlan;

	port = mlx5_esw_bridge_port_lookup(vport->vport, vport->bridge);
	if (!port)
		return;

	vlan = mlx5_esw_bridge_vlan_lookup(vid, port);
	if (!vlan)
		return;
	mlx5_esw_bridge_vlan_cleanup(port, vlan, vport->bridge);
}

void mlx5_esw_bridge_fdb_create(struct net_device *dev, struct mlx5_eswitch *esw,
				struct mlx5_vport *vport,
				struct switchdev_notifier_fdb_info *fdb_info)
{
	struct mlx5_esw_bridge *bridge = vport->bridge;
	struct mlx5_esw_bridge_fdb_entry *entry;
	u16 vport_num = vport->vport;

	if (!bridge) {
		esw_info(esw->dev, "Vport is not assigned to bridge (vport=%u)\n", vport_num);
		return;
	}

	entry = mlx5_esw_bridge_fdb_entry_init(dev, vport_num, fdb_info->addr, fdb_info->vid,
					       fdb_info->added_by_user, esw, bridge);
	if (IS_ERR(entry))
		return;

	if (entry->flags & MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER)
		mlx5_esw_bridge_fdb_offload_notify(dev, entry->key.addr, entry->key.vid,
						   SWITCHDEV_FDB_OFFLOADED);
	else
		/* Take over dynamic entries to prevent kernel bridge from aging them out. */
		mlx5_esw_bridge_fdb_offload_notify(dev, entry->key.addr, entry->key.vid,
						   SWITCHDEV_FDB_ADD_TO_BRIDGE);
}

void mlx5_esw_bridge_fdb_remove(struct net_device *dev, struct mlx5_eswitch *esw,
				struct mlx5_vport *vport,
				struct switchdev_notifier_fdb_info *fdb_info)
{
	struct mlx5_esw_bridge *bridge = vport->bridge;
	struct mlx5_esw_bridge_fdb_entry *entry;
	struct mlx5_esw_bridge_fdb_key key;
	u16 vport_num = vport->vport;

	if (!bridge) {
		esw_warn(esw->dev, "Vport is not assigned to bridge (vport=%u)\n", vport_num);
		return;
	}

	ether_addr_copy(key.addr, fdb_info->addr);
	key.vid = fdb_info->vid;
	entry = rhashtable_lookup_fast(&bridge->fdb_ht, &key, fdb_ht_params);
	if (!entry) {
		esw_warn(esw->dev,
			 "FDB entry with specified key not found (MAC=%pM,vid=%u,vport=%u)\n",
			 key.addr, key.vid, vport_num);
		return;
	}

	if (!(entry->flags & MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER))
		mlx5_esw_bridge_fdb_offload_notify(dev, entry->key.addr, entry->key.vid,
						   SWITCHDEV_FDB_DEL_TO_BRIDGE);
	mlx5_esw_bridge_fdb_entry_cleanup(entry, bridge);
}

void mlx5_esw_bridge_update(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge_fdb_entry *entry, *tmp;
	struct mlx5_esw_bridge *bridge;

	list_for_each_entry(bridge, &br_offloads->bridges, list) {
		list_for_each_entry_safe(entry, tmp, &bridge->fdb_list, list) {
			unsigned long lastuse =
				(unsigned long)mlx5_fc_query_lastuse(entry->ingress_counter);

			if (entry->flags & MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER)
				continue;

			if (time_after(lastuse, entry->lastuse)) {
				mlx5_esw_bridge_fdb_entry_refresh(lastuse, entry);
			} else if (time_is_before_jiffies(entry->lastuse + bridge->ageing_time)) {
				mlx5_esw_bridge_fdb_offload_notify(entry->dev, entry->key.addr,
								   entry->key.vid,
								   SWITCHDEV_FDB_DEL_TO_BRIDGE);
				mlx5_esw_bridge_fdb_entry_cleanup(entry, bridge);
			}
		}
	}
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
