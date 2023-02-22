// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "lib/devcom.h"
#include "bridge.h"
#include "eswitch.h"
#include "bridge_priv.h"

static int mlx5_esw_bridge_port_mcast_fts_init(struct mlx5_esw_bridge_port *port,
					       struct mlx5_esw_bridge *bridge)
{
	struct mlx5_eswitch *esw = bridge->br_offloads->esw;
	struct mlx5_flow_table *mcast_ft;

	mcast_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_MCAST_TABLE_SIZE,
						MLX5_ESW_BRIDGE_LEVEL_MCAST_TABLE,
						esw);
	if (IS_ERR(mcast_ft))
		return PTR_ERR(mcast_ft);

	port->mcast.ft = mcast_ft;
	return 0;
}

static void mlx5_esw_bridge_port_mcast_fts_cleanup(struct mlx5_esw_bridge_port *port)
{
	if (port->mcast.ft)
		mlx5_destroy_flow_table(port->mcast.ft);
	port->mcast.ft = NULL;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_mcast_filter_fg_create(struct mlx5_eswitch *esw,
				       struct mlx5_flow_table *mcast_ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *in, *match;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(create_flow_group_in, in, match_criteria_enable, MLX5_MATCH_MISC_PARAMETERS_2);
	match = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	MLX5_SET(fte_match_param, match, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_mask());

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_MCAST_TABLE_FILTER_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_MCAST_TABLE_FILTER_GRP_IDX_TO);

	fg = mlx5_create_flow_group(mcast_ft, in);
	kvfree(in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create filter flow group for bridge mcast table (err=%pe)\n",
			 fg);

	return fg;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_mcast_vlan_proto_fg_create(unsigned int from, unsigned int to, u16 vlan_proto,
					   struct mlx5_eswitch *esw,
					   struct mlx5_flow_table *mcast_ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *in, *match;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(create_flow_group_in, in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	match = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	if (vlan_proto == ETH_P_8021Q)
		MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.cvlan_tag);
	else if (vlan_proto == ETH_P_8021AD)
		MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.svlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.first_vid);

	MLX5_SET(create_flow_group_in, in, start_flow_index, from);
	MLX5_SET(create_flow_group_in, in, end_flow_index, to);

	fg = mlx5_create_flow_group(mcast_ft, in);
	kvfree(in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create VLAN(proto=%x) flow group for bridge mcast table (err=%pe)\n",
			 vlan_proto, fg);

	return fg;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_mcast_vlan_fg_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *mcast_ft)
{
	unsigned int from = MLX5_ESW_BRIDGE_MCAST_TABLE_VLAN_GRP_IDX_FROM;
	unsigned int to = MLX5_ESW_BRIDGE_MCAST_TABLE_VLAN_GRP_IDX_TO;

	return mlx5_esw_bridge_mcast_vlan_proto_fg_create(from, to, ETH_P_8021Q, esw, mcast_ft);
}

static struct mlx5_flow_group *
mlx5_esw_bridge_mcast_qinq_fg_create(struct mlx5_eswitch *esw,
				     struct mlx5_flow_table *mcast_ft)
{
	unsigned int from = MLX5_ESW_BRIDGE_MCAST_TABLE_QINQ_GRP_IDX_FROM;
	unsigned int to = MLX5_ESW_BRIDGE_MCAST_TABLE_QINQ_GRP_IDX_TO;

	return mlx5_esw_bridge_mcast_vlan_proto_fg_create(from, to, ETH_P_8021AD, esw, mcast_ft);
}

static struct mlx5_flow_group *
mlx5_esw_bridge_mcast_fwd_fg_create(struct mlx5_eswitch *esw,
				    struct mlx5_flow_table *mcast_ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *in;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_MCAST_TABLE_FWD_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_MCAST_TABLE_FWD_GRP_IDX_TO);

	fg = mlx5_create_flow_group(mcast_ft, in);
	kvfree(in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create forward flow group for bridge mcast table (err=%pe)\n",
			 fg);

	return fg;
}

static int mlx5_esw_bridge_port_mcast_fgs_init(struct mlx5_esw_bridge_port *port)
{
	struct mlx5_flow_group *fwd_fg, *qinq_fg, *vlan_fg, *filter_fg;
	struct mlx5_eswitch *esw = port->bridge->br_offloads->esw;
	struct mlx5_flow_table *mcast_ft = port->mcast.ft;
	int err;

	filter_fg = mlx5_esw_bridge_mcast_filter_fg_create(esw, mcast_ft);
	if (IS_ERR(filter_fg))
		return PTR_ERR(filter_fg);

	vlan_fg = mlx5_esw_bridge_mcast_vlan_fg_create(esw, mcast_ft);
	if (IS_ERR(vlan_fg)) {
		err = PTR_ERR(vlan_fg);
		goto err_vlan_fg;
	}

	qinq_fg = mlx5_esw_bridge_mcast_qinq_fg_create(esw, mcast_ft);
	if (IS_ERR(qinq_fg)) {
		err = PTR_ERR(qinq_fg);
		goto err_qinq_fg;
	}

	fwd_fg = mlx5_esw_bridge_mcast_fwd_fg_create(esw, mcast_ft);
	if (IS_ERR(fwd_fg)) {
		err = PTR_ERR(fwd_fg);
		goto err_fwd_fg;
	}

	port->mcast.filter_fg = filter_fg;
	port->mcast.vlan_fg = vlan_fg;
	port->mcast.qinq_fg = qinq_fg;
	port->mcast.fwd_fg = fwd_fg;

	return 0;

err_fwd_fg:
	mlx5_destroy_flow_group(qinq_fg);
err_qinq_fg:
	mlx5_destroy_flow_group(vlan_fg);
err_vlan_fg:
	mlx5_destroy_flow_group(filter_fg);
	return err;
}

static void mlx5_esw_bridge_port_mcast_fgs_cleanup(struct mlx5_esw_bridge_port *port)
{
	if (port->mcast.fwd_fg)
		mlx5_destroy_flow_group(port->mcast.fwd_fg);
	port->mcast.fwd_fg = NULL;
	if (port->mcast.qinq_fg)
		mlx5_destroy_flow_group(port->mcast.qinq_fg);
	port->mcast.qinq_fg = NULL;
	if (port->mcast.vlan_fg)
		mlx5_destroy_flow_group(port->mcast.vlan_fg);
	port->mcast.vlan_fg = NULL;
	if (port->mcast.filter_fg)
		mlx5_destroy_flow_group(port->mcast.filter_fg);
	port->mcast.filter_fg = NULL;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_mcast_flow_with_esw_create(struct mlx5_esw_bridge_port *port,
					   struct mlx5_eswitch *esw)
{
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_DROP,
		.flags = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	rule_spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;

	MLX5_SET(fte_match_param, rule_spec->match_criteria,
		 misc_parameters_2.metadata_reg_c_0, mlx5_eswitch_get_vport_metadata_mask());
	MLX5_SET(fte_match_param, rule_spec->match_value, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_for_match(esw, port->vport_num));

	handle = mlx5_add_flow_rules(port->mcast.ft, rule_spec, &flow_act, NULL, 0);

	kvfree(rule_spec);
	return handle;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_mcast_filter_flow_create(struct mlx5_esw_bridge_port *port)
{
	return mlx5_esw_bridge_mcast_flow_with_esw_create(port, port->bridge->br_offloads->esw);
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_mcast_filter_flow_peer_create(struct mlx5_esw_bridge_port *port)
{
	struct mlx5_devcom *devcom = port->bridge->br_offloads->esw->dev->priv.devcom;
	static struct mlx5_flow_handle *handle;
	struct mlx5_eswitch *peer_esw;

	peer_esw = mlx5_devcom_get_peer_data(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
	if (!peer_esw)
		return ERR_PTR(-ENODEV);

	handle = mlx5_esw_bridge_mcast_flow_with_esw_create(port, peer_esw);

	mlx5_devcom_release_peer_data(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
	return handle;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_mcast_vlan_flow_create(u16 vlan_proto, struct mlx5_esw_bridge_port *port,
				       struct mlx5_esw_bridge_vlan *vlan)
{
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
		.flags = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_flow_destination dest = {
		.type = MLX5_FLOW_DESTINATION_TYPE_VPORT,
		.vport.num = port->vport_num,
	};
	struct mlx5_esw_bridge *bridge = port->bridge;
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	if (MLX5_CAP_ESW_FLOWTABLE(bridge->br_offloads->esw->dev, flow_source) &&
	    port->vport_num == MLX5_VPORT_UPLINK)
		rule_spec->flow_context.flow_source =
			MLX5_FLOW_CONTEXT_FLOW_SOURCE_LOCAL_VPORT;
	rule_spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
	flow_act.pkt_reformat = vlan->pkt_reformat_pop;

	if (vlan_proto == ETH_P_8021Q) {
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.cvlan_tag);
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
				 outer_headers.cvlan_tag);
	} else if (vlan_proto == ETH_P_8021AD) {
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria,
				 outer_headers.svlan_tag);
		MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_value,
				 outer_headers.svlan_tag);
	}
	MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria, outer_headers.first_vid);
	MLX5_SET(fte_match_param, rule_spec->match_value, outer_headers.first_vid, vlan->vid);

	if (MLX5_CAP_ESW(bridge->br_offloads->esw->dev, merged_eswitch)) {
		dest.vport.flags = MLX5_FLOW_DEST_VPORT_VHCA_ID;
		dest.vport.vhca_id = port->esw_owner_vhca_id;
	}
	handle = mlx5_add_flow_rules(port->mcast.ft, rule_spec, &flow_act, &dest, 1);

	kvfree(rule_spec);
	return handle;
}

int mlx5_esw_bridge_vlan_mcast_init(u16 vlan_proto, struct mlx5_esw_bridge_port *port,
				    struct mlx5_esw_bridge_vlan *vlan)
{
	struct mlx5_flow_handle *handle;

	if (!(port->bridge->flags & MLX5_ESW_BRIDGE_MCAST_FLAG))
		return 0;

	handle = mlx5_esw_bridge_mcast_vlan_flow_create(vlan_proto, port, vlan);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	vlan->mcast_handle = handle;
	return 0;
}

void mlx5_esw_bridge_vlan_mcast_cleanup(struct mlx5_esw_bridge_vlan *vlan)
{
	if (vlan->mcast_handle)
		mlx5_del_flow_rules(vlan->mcast_handle);
	vlan->mcast_handle = NULL;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_mcast_fwd_flow_create(struct mlx5_esw_bridge_port *port)
{
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
		.flags = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_flow_destination dest = {
		.type = MLX5_FLOW_DESTINATION_TYPE_VPORT,
		.vport.num = port->vport_num,
	};
	struct mlx5_esw_bridge *bridge = port->bridge;
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	if (MLX5_CAP_ESW_FLOWTABLE(bridge->br_offloads->esw->dev, flow_source) &&
	    port->vport_num == MLX5_VPORT_UPLINK)
		rule_spec->flow_context.flow_source =
			MLX5_FLOW_CONTEXT_FLOW_SOURCE_LOCAL_VPORT;

	if (MLX5_CAP_ESW(bridge->br_offloads->esw->dev, merged_eswitch)) {
		dest.vport.flags = MLX5_FLOW_DEST_VPORT_VHCA_ID;
		dest.vport.vhca_id = port->esw_owner_vhca_id;
	}
	handle = mlx5_add_flow_rules(port->mcast.ft, rule_spec, &flow_act, &dest, 1);

	kvfree(rule_spec);
	return handle;
}

static int mlx5_esw_bridge_port_mcast_fhs_init(struct mlx5_esw_bridge_port *port)
{
	struct mlx5_flow_handle *filter_handle, *fwd_handle;
	struct mlx5_esw_bridge_vlan *vlan, *failed;
	unsigned long index;
	int err;


	filter_handle = (port->flags & MLX5_ESW_BRIDGE_PORT_FLAG_PEER) ?
		mlx5_esw_bridge_mcast_filter_flow_peer_create(port) :
		mlx5_esw_bridge_mcast_filter_flow_create(port);
	if (IS_ERR(filter_handle))
		return PTR_ERR(filter_handle);

	fwd_handle = mlx5_esw_bridge_mcast_fwd_flow_create(port);
	if (IS_ERR(fwd_handle)) {
		err = PTR_ERR(fwd_handle);
		goto err_fwd;
	}

	xa_for_each(&port->vlans, index, vlan) {
		err = mlx5_esw_bridge_vlan_mcast_init(port->bridge->vlan_proto, port, vlan);
		if (err) {
			failed = vlan;
			goto err_vlan;
		}
	}

	port->mcast.filter_handle = filter_handle;
	port->mcast.fwd_handle = fwd_handle;

	return 0;

err_vlan:
	xa_for_each(&port->vlans, index, vlan) {
		if (vlan == failed)
			break;

		mlx5_esw_bridge_vlan_mcast_cleanup(vlan);
	}
	mlx5_del_flow_rules(fwd_handle);
err_fwd:
	mlx5_del_flow_rules(filter_handle);
	return err;
}

static void mlx5_esw_bridge_port_mcast_fhs_cleanup(struct mlx5_esw_bridge_port *port)
{
	struct mlx5_esw_bridge_vlan *vlan;
	unsigned long index;

	xa_for_each(&port->vlans, index, vlan)
		mlx5_esw_bridge_vlan_mcast_cleanup(vlan);

	if (port->mcast.fwd_handle)
		mlx5_del_flow_rules(port->mcast.fwd_handle);
	port->mcast.fwd_handle = NULL;
	if (port->mcast.filter_handle)
		mlx5_del_flow_rules(port->mcast.filter_handle);
	port->mcast.filter_handle = NULL;
}

int mlx5_esw_bridge_port_mcast_init(struct mlx5_esw_bridge_port *port)
{
	struct mlx5_esw_bridge *bridge = port->bridge;
	int err;

	if (!(bridge->flags & MLX5_ESW_BRIDGE_MCAST_FLAG))
		return 0;

	err = mlx5_esw_bridge_port_mcast_fts_init(port, bridge);
	if (err)
		return err;

	err = mlx5_esw_bridge_port_mcast_fgs_init(port);
	if (err)
		goto err_fgs;

	err = mlx5_esw_bridge_port_mcast_fhs_init(port);
	if (err)
		goto err_fhs;
	return err;

err_fhs:
	mlx5_esw_bridge_port_mcast_fgs_cleanup(port);
err_fgs:
	mlx5_esw_bridge_port_mcast_fts_cleanup(port);
	return err;
}

void mlx5_esw_bridge_port_mcast_cleanup(struct mlx5_esw_bridge_port *port)
{
	mlx5_esw_bridge_port_mcast_fhs_cleanup(port);
	mlx5_esw_bridge_port_mcast_fgs_cleanup(port);
	mlx5_esw_bridge_port_mcast_fts_cleanup(port);
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_igmp_fg_create(struct mlx5_eswitch *esw,
				       struct mlx5_flow_table *ingress_ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *in, *match;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(create_flow_group_in, in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	match = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.ip_version);
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.ip_protocol);

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_IGMP_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_IGMP_GRP_IDX_TO);

	fg = mlx5_create_flow_group(ingress_ft, in);
	kvfree(in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create IGMP flow group for bridge ingress table (err=%pe)\n",
			 fg);

	return fg;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_mld_fg_create(struct mlx5_eswitch *esw,
				      struct mlx5_flow_table *ingress_ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *in, *match;

	if (!(MLX5_CAP_GEN(esw->dev, flex_parser_protocols) & MLX5_FLEX_PROTO_ICMPV6)) {
		esw_warn(esw->dev,
			 "Can't create MLD flow group due to missing hardware ICMPv6 parsing support\n");
		return NULL;
	}

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(create_flow_group_in, in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS_3);
	match = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.ip_version);
	MLX5_SET_TO_ONES(fte_match_param, match, misc_parameters_3.icmpv6_type);

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_MLD_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_MLD_GRP_IDX_TO);

	fg = mlx5_create_flow_group(ingress_ft, in);
	kvfree(in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create MLD flow group for bridge ingress table (err=%pe)\n",
			 fg);

	return fg;
}

static int
mlx5_esw_bridge_ingress_mcast_fgs_init(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_flow_table *ingress_ft = br_offloads->ingress_ft;
	struct mlx5_eswitch *esw = br_offloads->esw;
	struct mlx5_flow_group *igmp_fg, *mld_fg;

	igmp_fg = mlx5_esw_bridge_ingress_igmp_fg_create(esw, ingress_ft);
	if (IS_ERR(igmp_fg))
		return PTR_ERR(igmp_fg);

	mld_fg = mlx5_esw_bridge_ingress_mld_fg_create(esw, ingress_ft);
	if (IS_ERR(mld_fg)) {
		mlx5_destroy_flow_group(igmp_fg);
		return PTR_ERR(mld_fg);
	}

	br_offloads->ingress_igmp_fg = igmp_fg;
	br_offloads->ingress_mld_fg = mld_fg;
	return 0;
}

static void
mlx5_esw_bridge_ingress_mcast_fgs_cleanup(struct mlx5_esw_bridge_offloads *br_offloads)
{
	if (br_offloads->ingress_mld_fg)
		mlx5_destroy_flow_group(br_offloads->ingress_mld_fg);
	br_offloads->ingress_mld_fg = NULL;
	if (br_offloads->ingress_igmp_fg)
		mlx5_destroy_flow_group(br_offloads->ingress_igmp_fg);
	br_offloads->ingress_igmp_fg = NULL;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_ingress_igmp_fh_create(struct mlx5_flow_table *ingress_ft,
				       struct mlx5_flow_table *skip_ft)
{
	struct mlx5_flow_destination dest = {
		.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE,
		.ft = skip_ft,
	};
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
		.flags = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	rule_spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria, outer_headers.ip_version);
	MLX5_SET(fte_match_param, rule_spec->match_value, outer_headers.ip_version, 4);
	MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, rule_spec->match_value, outer_headers.ip_protocol, IPPROTO_IGMP);

	handle = mlx5_add_flow_rules(ingress_ft, rule_spec, &flow_act, &dest, 1);

	kvfree(rule_spec);
	return handle;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_ingress_mld_fh_create(u8 type, struct mlx5_flow_table *ingress_ft,
				      struct mlx5_flow_table *skip_ft)
{
	struct mlx5_flow_destination dest = {
		.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE,
		.ft = skip_ft,
	};
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
		.flags = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	rule_spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS_3;

	MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria, outer_headers.ip_version);
	MLX5_SET(fte_match_param, rule_spec->match_value, outer_headers.ip_version, 6);
	MLX5_SET_TO_ONES(fte_match_param, rule_spec->match_criteria, misc_parameters_3.icmpv6_type);
	MLX5_SET(fte_match_param, rule_spec->match_value, misc_parameters_3.icmpv6_type, type);

	handle = mlx5_add_flow_rules(ingress_ft, rule_spec, &flow_act, &dest, 1);

	kvfree(rule_spec);
	return handle;
}

static int
mlx5_esw_bridge_ingress_mcast_fhs_create(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_flow_handle *igmp_handle, *mld_query_handle, *mld_report_handle,
		*mld_done_handle;
	struct mlx5_flow_table *ingress_ft = br_offloads->ingress_ft,
		*skip_ft = br_offloads->skip_ft;
	int err;

	igmp_handle = mlx5_esw_bridge_ingress_igmp_fh_create(ingress_ft, skip_ft);
	if (IS_ERR(igmp_handle))
		return PTR_ERR(igmp_handle);

	if (br_offloads->ingress_mld_fg) {
		mld_query_handle = mlx5_esw_bridge_ingress_mld_fh_create(ICMPV6_MGM_QUERY,
									 ingress_ft,
									 skip_ft);
		if (IS_ERR(mld_query_handle)) {
			err = PTR_ERR(mld_query_handle);
			goto err_mld_query;
		}

		mld_report_handle = mlx5_esw_bridge_ingress_mld_fh_create(ICMPV6_MGM_REPORT,
									  ingress_ft,
									  skip_ft);
		if (IS_ERR(mld_report_handle)) {
			err = PTR_ERR(mld_report_handle);
			goto err_mld_report;
		}

		mld_done_handle = mlx5_esw_bridge_ingress_mld_fh_create(ICMPV6_MGM_REDUCTION,
									ingress_ft,
									skip_ft);
		if (IS_ERR(mld_done_handle)) {
			err = PTR_ERR(mld_done_handle);
			goto err_mld_done;
		}
	} else {
		mld_query_handle = NULL;
		mld_report_handle = NULL;
		mld_done_handle = NULL;
	}

	br_offloads->igmp_handle = igmp_handle;
	br_offloads->mld_query_handle = mld_query_handle;
	br_offloads->mld_report_handle = mld_report_handle;
	br_offloads->mld_done_handle = mld_done_handle;

	return 0;

err_mld_done:
	mlx5_del_flow_rules(mld_report_handle);
err_mld_report:
	mlx5_del_flow_rules(mld_query_handle);
err_mld_query:
	mlx5_del_flow_rules(igmp_handle);
	return err;
}

static void
mlx5_esw_bridge_ingress_mcast_fhs_cleanup(struct mlx5_esw_bridge_offloads *br_offloads)
{
	if (br_offloads->mld_done_handle)
		mlx5_del_flow_rules(br_offloads->mld_done_handle);
	br_offloads->mld_done_handle = NULL;
	if (br_offloads->mld_report_handle)
		mlx5_del_flow_rules(br_offloads->mld_report_handle);
	br_offloads->mld_report_handle = NULL;
	if (br_offloads->mld_query_handle)
		mlx5_del_flow_rules(br_offloads->mld_query_handle);
	br_offloads->mld_query_handle = NULL;
	if (br_offloads->igmp_handle)
		mlx5_del_flow_rules(br_offloads->igmp_handle);
	br_offloads->igmp_handle = NULL;
}

static int mlx5_esw_brige_mcast_init(struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_offloads *br_offloads = bridge->br_offloads;
	struct mlx5_esw_bridge_port *port, *failed;
	unsigned long i;
	int err;

	xa_for_each(&br_offloads->ports, i, port) {
		if (port->bridge != bridge)
			continue;

		err = mlx5_esw_bridge_port_mcast_init(port);
		if (err) {
			failed = port;
			goto err_port;
		}
	}
	return 0;

err_port:
	xa_for_each(&br_offloads->ports, i, port) {
		if (port == failed)
			break;
		if (port->bridge != bridge)
			continue;

		mlx5_esw_bridge_port_mcast_cleanup(port);
	}
	return err;
}

static void mlx5_esw_brige_mcast_cleanup(struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_offloads *br_offloads = bridge->br_offloads;
	struct mlx5_esw_bridge_port *port;
	unsigned long i;

	xa_for_each(&br_offloads->ports, i, port) {
		if (port->bridge != bridge)
			continue;

		mlx5_esw_bridge_port_mcast_cleanup(port);
	}
}

static int mlx5_esw_brige_mcast_global_enable(struct mlx5_esw_bridge_offloads *br_offloads)
{
	int err;

	if (br_offloads->ingress_igmp_fg)
		return 0; /* already enabled by another bridge */

	err = mlx5_esw_bridge_ingress_mcast_fgs_init(br_offloads);
	if (err) {
		esw_warn(br_offloads->esw->dev,
			 "Failed to create global multicast flow groups (err=%d)\n",
			 err);
		return err;
	}

	err = mlx5_esw_bridge_ingress_mcast_fhs_create(br_offloads);
	if (err) {
		esw_warn(br_offloads->esw->dev,
			 "Failed to create global multicast flows (err=%d)\n",
			 err);
		goto err_fhs;
	}

	return 0;

err_fhs:
	mlx5_esw_bridge_ingress_mcast_fgs_cleanup(br_offloads);
	return err;
}

static void mlx5_esw_brige_mcast_global_disable(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge *br;

	list_for_each_entry(br, &br_offloads->bridges, list) {
		/* Ingress table is global, so only disable snooping when all
		 * bridges on esw have multicast disabled.
		 */
		if (br->flags & MLX5_ESW_BRIDGE_MCAST_FLAG)
			return;
	}

	mlx5_esw_bridge_ingress_mcast_fhs_cleanup(br_offloads);
	mlx5_esw_bridge_ingress_mcast_fgs_cleanup(br_offloads);
}

int mlx5_esw_bridge_mcast_enable(struct mlx5_esw_bridge *bridge)
{
	int err;

	err = mlx5_esw_brige_mcast_global_enable(bridge->br_offloads);
	if (err)
		return err;

	bridge->flags |= MLX5_ESW_BRIDGE_MCAST_FLAG;

	err = mlx5_esw_brige_mcast_init(bridge);
	if (err) {
		esw_warn(bridge->br_offloads->esw->dev, "Failed to enable multicast (err=%d)\n",
			 err);
		bridge->flags &= ~MLX5_ESW_BRIDGE_MCAST_FLAG;
		mlx5_esw_brige_mcast_global_disable(bridge->br_offloads);
	}
	return err;
}

void mlx5_esw_bridge_mcast_disable(struct mlx5_esw_bridge *bridge)
{
	mlx5_esw_brige_mcast_cleanup(bridge);
	bridge->flags &= ~MLX5_ESW_BRIDGE_MCAST_FLAG;
	mlx5_esw_brige_mcast_global_disable(bridge->br_offloads);
}
