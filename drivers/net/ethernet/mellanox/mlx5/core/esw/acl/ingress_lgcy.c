// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Inc. All rights reserved. */

#include "mlx5_core.h"
#include "eswitch.h"
#include "helper.h"
#include "lgcy.h"

static void esw_acl_ingress_lgcy_rules_destroy(struct mlx5_vport *vport)
{
	if (vport->ingress.legacy.drop_rule) {
		mlx5_del_flow_rules(vport->ingress.legacy.drop_rule);
		vport->ingress.legacy.drop_rule = NULL;
	}
	esw_acl_ingress_allow_rule_destroy(vport);
}

static int esw_acl_ingress_lgcy_groups_create(struct mlx5_eswitch *esw,
					      struct mlx5_vport *vport)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_group *g;
	void *match_criteria;
	u32 *flow_group_in;
	int err;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in, match_criteria);

	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.cvlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.smac_47_16);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.smac_15_0);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 0);

	g = mlx5_create_flow_group(vport->ingress.acl, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "vport[%d] ingress create untagged spoofchk flow group, err(%d)\n",
			 vport->vport, err);
		goto spoof_err;
	}
	vport->ingress.legacy.allow_untagged_spoofchk_grp = g;

	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.cvlan_tag);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 1);

	g = mlx5_create_flow_group(vport->ingress.acl, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "vport[%d] ingress create untagged flow group, err(%d)\n",
			 vport->vport, err);
		goto untagged_err;
	}
	vport->ingress.legacy.allow_untagged_only_grp = g;

	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.smac_47_16);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.smac_15_0);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 2);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 2);

	g = mlx5_create_flow_group(vport->ingress.acl, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "vport[%d] ingress create spoofchk flow group, err(%d)\n",
			 vport->vport, err);
		goto allow_spoof_err;
	}
	vport->ingress.legacy.allow_spoofchk_only_grp = g;

	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 3);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 3);

	g = mlx5_create_flow_group(vport->ingress.acl, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "vport[%d] ingress create drop flow group, err(%d)\n",
			 vport->vport, err);
		goto drop_err;
	}
	vport->ingress.legacy.drop_grp = g;
	kvfree(flow_group_in);
	return 0;

drop_err:
	if (!IS_ERR_OR_NULL(vport->ingress.legacy.allow_spoofchk_only_grp)) {
		mlx5_destroy_flow_group(vport->ingress.legacy.allow_spoofchk_only_grp);
		vport->ingress.legacy.allow_spoofchk_only_grp = NULL;
	}
allow_spoof_err:
	if (!IS_ERR_OR_NULL(vport->ingress.legacy.allow_untagged_only_grp)) {
		mlx5_destroy_flow_group(vport->ingress.legacy.allow_untagged_only_grp);
		vport->ingress.legacy.allow_untagged_only_grp = NULL;
	}
untagged_err:
	if (!IS_ERR_OR_NULL(vport->ingress.legacy.allow_untagged_spoofchk_grp)) {
		mlx5_destroy_flow_group(vport->ingress.legacy.allow_untagged_spoofchk_grp);
		vport->ingress.legacy.allow_untagged_spoofchk_grp = NULL;
	}
spoof_err:
	kvfree(flow_group_in);
	return err;
}

static void esw_acl_ingress_lgcy_groups_destroy(struct mlx5_vport *vport)
{
	if (vport->ingress.legacy.allow_spoofchk_only_grp) {
		mlx5_destroy_flow_group(vport->ingress.legacy.allow_spoofchk_only_grp);
		vport->ingress.legacy.allow_spoofchk_only_grp = NULL;
	}
	if (vport->ingress.legacy.allow_untagged_only_grp) {
		mlx5_destroy_flow_group(vport->ingress.legacy.allow_untagged_only_grp);
		vport->ingress.legacy.allow_untagged_only_grp = NULL;
	}
	if (vport->ingress.legacy.allow_untagged_spoofchk_grp) {
		mlx5_destroy_flow_group(vport->ingress.legacy.allow_untagged_spoofchk_grp);
		vport->ingress.legacy.allow_untagged_spoofchk_grp = NULL;
	}
	if (vport->ingress.legacy.drop_grp) {
		mlx5_destroy_flow_group(vport->ingress.legacy.drop_grp);
		vport->ingress.legacy.drop_grp = NULL;
	}
}

int esw_acl_ingress_lgcy_setup(struct mlx5_eswitch *esw,
			       struct mlx5_vport *vport)
{
	bool vst_mode_steering = esw_vst_mode_is_steering(esw);
	struct mlx5_flow_destination drop_ctr_dst = {};
	struct mlx5_flow_destination *dst = NULL;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_spec *spec = NULL;
	struct mlx5_fc *counter = NULL;
	bool vst_check_cvlan = false;
	bool vst_push_cvlan = false;
	/* The ingress acl table contains 4 groups
	 * (2 active rules at the same time -
	 *      1 allow rule from one of the first 3 groups.
	 *      1 drop rule from the last group):
	 * 1)Allow untagged traffic with smac=original mac.
	 * 2)Allow untagged traffic.
	 * 3)Allow traffic with smac=original mac.
	 * 4)Drop all other traffic.
	 */
	int table_size = 4;
	int dest_num = 0;
	int err = 0;
	u8 *smac_v;

	esw_acl_ingress_lgcy_rules_destroy(vport);

	if (vport->ingress.legacy.drop_counter) {
		counter = vport->ingress.legacy.drop_counter;
	} else if (MLX5_CAP_ESW_INGRESS_ACL(esw->dev, flow_counter)) {
		counter = mlx5_fc_create(esw->dev, false);
		if (IS_ERR(counter)) {
			esw_warn(esw->dev,
				 "vport[%d] configure ingress drop rule counter failed\n",
				 vport->vport);
			counter = NULL;
		}
		vport->ingress.legacy.drop_counter = counter;
	}

	if (!vport->info.vlan && !vport->info.qos && !vport->info.spoofchk) {
		esw_acl_ingress_lgcy_cleanup(esw, vport);
		return 0;
	}

	if (!vport->ingress.acl) {
		vport->ingress.acl = esw_acl_table_create(esw, vport,
							  MLX5_FLOW_NAMESPACE_ESW_INGRESS,
							  table_size);
		if (IS_ERR(vport->ingress.acl)) {
			err = PTR_ERR(vport->ingress.acl);
			vport->ingress.acl = NULL;
			return err;
		}

		err = esw_acl_ingress_lgcy_groups_create(esw, vport);
		if (err)
			goto out;
	}

	esw_debug(esw->dev,
		  "vport[%d] configure ingress rules, vlan(%d) qos(%d)\n",
		  vport->vport, vport->info.vlan, vport->info.qos);

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto out;
	}

	if ((vport->info.vlan || vport->info.qos)) {
		if (vst_mode_steering)
			vst_push_cvlan = true;
		else if (!MLX5_CAP_ESW(esw->dev, vport_cvlan_insert_always))
			vst_check_cvlan = true;
	}

	if (vst_check_cvlan || vport->info.spoofchk)
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	/* Create ingress allow rule */
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW;
	if (vst_push_cvlan) {
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH;
		flow_act.vlan[0].prio = vport->info.qos;
		flow_act.vlan[0].vid = vport->info.vlan;
		flow_act.vlan[0].ethtype = ETH_P_8021Q;
	}

	if (vst_check_cvlan)
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.cvlan_tag);

	if (vport->info.spoofchk) {
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.smac_47_16);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.smac_15_0);
		smac_v = MLX5_ADDR_OF(fte_match_param,
				      spec->match_value,
				      outer_headers.smac_47_16);
		ether_addr_copy(smac_v, vport->info.mac);
	}

	vport->ingress.allow_rule = mlx5_add_flow_rules(vport->ingress.acl, spec,
							&flow_act, NULL, 0);
	if (IS_ERR(vport->ingress.allow_rule)) {
		err = PTR_ERR(vport->ingress.allow_rule);
		esw_warn(esw->dev,
			 "vport[%d] configure ingress allow rule, err(%d)\n",
			 vport->vport, err);
		vport->ingress.allow_rule = NULL;
		goto out;
	}

	if (!vst_check_cvlan && !vport->info.spoofchk)
		goto out;

	memset(&flow_act, 0, sizeof(flow_act));
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP;
	/* Attach drop flow counter */
	if (counter) {
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
		drop_ctr_dst.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		drop_ctr_dst.counter = counter;
		dst = &drop_ctr_dst;
		dest_num++;
	}
	vport->ingress.legacy.drop_rule =
		mlx5_add_flow_rules(vport->ingress.acl, NULL,
				    &flow_act, dst, dest_num);
	if (IS_ERR(vport->ingress.legacy.drop_rule)) {
		err = PTR_ERR(vport->ingress.legacy.drop_rule);
		esw_warn(esw->dev,
			 "vport[%d] configure ingress drop rule, err(%d)\n",
			 vport->vport, err);
		vport->ingress.legacy.drop_rule = NULL;
		goto out;
	}
	kvfree(spec);
	return 0;

out:
	if (err)
		esw_acl_ingress_lgcy_cleanup(esw, vport);
	kvfree(spec);
	return err;
}

void esw_acl_ingress_lgcy_cleanup(struct mlx5_eswitch *esw,
				  struct mlx5_vport *vport)
{
	if (IS_ERR_OR_NULL(vport->ingress.acl))
		goto clean_drop_counter;

	esw_debug(esw->dev, "Destroy vport[%d] E-Switch ingress ACL\n", vport->vport);

	esw_acl_ingress_lgcy_rules_destroy(vport);
	esw_acl_ingress_lgcy_groups_destroy(vport);
	esw_acl_ingress_table_destroy(vport);

clean_drop_counter:
	if (vport->ingress.legacy.drop_counter) {
		mlx5_fc_destroy(esw->dev, vport->ingress.legacy.drop_counter);
		vport->ingress.legacy.drop_counter = NULL;
	}
}
