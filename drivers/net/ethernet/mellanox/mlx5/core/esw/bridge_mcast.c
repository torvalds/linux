// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "bridge.h"
#include "eswitch.h"
#include "bridge_priv.h"

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
	return 0;
}

void mlx5_esw_bridge_mcast_disable(struct mlx5_esw_bridge *bridge)
{
	bridge->flags &= ~MLX5_ESW_BRIDGE_MCAST_FLAG;
	mlx5_esw_brige_mcast_global_disable(bridge->br_offloads);
}
