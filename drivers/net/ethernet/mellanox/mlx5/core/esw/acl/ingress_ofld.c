// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Inc. All rights reserved. */

#include "mlx5_core.h"
#include "eswitch.h"
#include "helper.h"
#include "ofld.h"

static bool
esw_acl_ingress_prio_tag_enabled(struct mlx5_eswitch *esw,
				 const struct mlx5_vport *vport)
{
	return (MLX5_CAP_GEN(esw->dev, prio_tag_required) &&
		mlx5_eswitch_is_vf_vport(esw, vport->vport));
}

static int esw_acl_ingress_prio_tag_create(struct mlx5_eswitch *esw,
					   struct mlx5_vport *vport)
{
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_spec *spec;
	int err = 0;

	/* For prio tag mode, there is only 1 FTEs:
	 * 1) Untagged packets - push prio tag VLAN and modify metadata if
	 * required, allow
	 * Unmatched traffic is allowed by default
	 */
	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	/* Untagged packets - push prio tag VLAN, allow */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.cvlan_tag);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.cvlan_tag, 0);
	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH |
			  MLX5_FLOW_CONTEXT_ACTION_ALLOW;
	flow_act.vlan[0].ethtype = ETH_P_8021Q;
	flow_act.vlan[0].vid = 0;
	flow_act.vlan[0].prio = 0;

	if (vport->ingress.offloads.modify_metadata_rule) {
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
		flow_act.modify_hdr = vport->ingress.offloads.modify_metadata;
	}

	vport->ingress.allow_rule = mlx5_add_flow_rules(vport->ingress.acl, spec,
							&flow_act, NULL, 0);
	if (IS_ERR(vport->ingress.allow_rule)) {
		err = PTR_ERR(vport->ingress.allow_rule);
		esw_warn(esw->dev,
			 "vport[%d] configure ingress untagged allow rule, err(%d)\n",
			 vport->vport, err);
		vport->ingress.allow_rule = NULL;
	}

	kvfree(spec);
	return err;
}

static int esw_acl_ingress_mod_metadata_create(struct mlx5_eswitch *esw,
					       struct mlx5_vport *vport)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_flow_act flow_act = {};
	int err = 0;
	u32 key;

	key = mlx5_eswitch_get_vport_metadata_for_match(esw, vport->vport);
	key >>= ESW_SOURCE_PORT_METADATA_OFFSET;

	MLX5_SET(set_action_in, action, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, action, field,
		 MLX5_ACTION_IN_FIELD_METADATA_REG_C_0);
	MLX5_SET(set_action_in, action, data, key);
	MLX5_SET(set_action_in, action, offset,
		 ESW_SOURCE_PORT_METADATA_OFFSET);
	MLX5_SET(set_action_in, action, length,
		 ESW_SOURCE_PORT_METADATA_BITS);

	vport->ingress.offloads.modify_metadata =
		mlx5_modify_header_alloc(esw->dev, MLX5_FLOW_NAMESPACE_ESW_INGRESS,
					 1, action);
	if (IS_ERR(vport->ingress.offloads.modify_metadata)) {
		err = PTR_ERR(vport->ingress.offloads.modify_metadata);
		esw_warn(esw->dev,
			 "failed to alloc modify header for vport %d ingress acl (%d)\n",
			 vport->vport, err);
		return err;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_MOD_HDR | MLX5_FLOW_CONTEXT_ACTION_ALLOW;
	flow_act.modify_hdr = vport->ingress.offloads.modify_metadata;
	flow_act.fg = vport->ingress.offloads.metadata_allmatch_grp;
	vport->ingress.offloads.modify_metadata_rule =
				mlx5_add_flow_rules(vport->ingress.acl,
						    NULL, &flow_act, NULL, 0);
	if (IS_ERR(vport->ingress.offloads.modify_metadata_rule)) {
		err = PTR_ERR(vport->ingress.offloads.modify_metadata_rule);
		esw_warn(esw->dev,
			 "failed to add setting metadata rule for vport %d ingress acl, err(%d)\n",
			 vport->vport, err);
		mlx5_modify_header_dealloc(esw->dev, vport->ingress.offloads.modify_metadata);
		vport->ingress.offloads.modify_metadata_rule = NULL;
	}
	return err;
}

static void esw_acl_ingress_mod_metadata_destroy(struct mlx5_eswitch *esw,
						 struct mlx5_vport *vport)
{
	if (!vport->ingress.offloads.modify_metadata_rule)
		return;

	mlx5_del_flow_rules(vport->ingress.offloads.modify_metadata_rule);
	mlx5_modify_header_dealloc(esw->dev, vport->ingress.offloads.modify_metadata);
	vport->ingress.offloads.modify_metadata_rule = NULL;
}

static int esw_acl_ingress_src_port_drop_create(struct mlx5_eswitch *esw,
						struct mlx5_vport *vport)
{
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *flow_rule;
	int err = 0;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP;
	flow_act.fg = vport->ingress.offloads.drop_grp;
	flow_rule = mlx5_add_flow_rules(vport->ingress.acl, NULL, &flow_act, NULL, 0);
	if (IS_ERR(flow_rule)) {
		err = PTR_ERR(flow_rule);
		goto out;
	}

	vport->ingress.offloads.drop_rule = flow_rule;
out:
	return err;
}

static void esw_acl_ingress_src_port_drop_destroy(struct mlx5_eswitch *esw,
						  struct mlx5_vport *vport)
{
	if (!vport->ingress.offloads.drop_rule)
		return;

	mlx5_del_flow_rules(vport->ingress.offloads.drop_rule);
	vport->ingress.offloads.drop_rule = NULL;
}

static int esw_acl_ingress_ofld_rules_create(struct mlx5_eswitch *esw,
					     struct mlx5_vport *vport)
{
	int err;

	if (mlx5_eswitch_vport_match_metadata_enabled(esw)) {
		err = esw_acl_ingress_mod_metadata_create(esw, vport);
		if (err) {
			esw_warn(esw->dev,
				 "vport(%d) create ingress modify metadata, err(%d)\n",
				 vport->vport, err);
			return err;
		}
	}

	if (esw_acl_ingress_prio_tag_enabled(esw, vport)) {
		err = esw_acl_ingress_prio_tag_create(esw, vport);
		if (err) {
			esw_warn(esw->dev,
				 "vport(%d) create ingress prio tag rule, err(%d)\n",
				 vport->vport, err);
			goto prio_tag_err;
		}
	}

	return 0;

prio_tag_err:
	esw_acl_ingress_mod_metadata_destroy(esw, vport);
	return err;
}

static void esw_acl_ingress_ofld_rules_destroy(struct mlx5_eswitch *esw,
					       struct mlx5_vport *vport)
{
	esw_acl_ingress_allow_rule_destroy(vport);
	esw_acl_ingress_mod_metadata_destroy(esw, vport);
	esw_acl_ingress_src_port_drop_destroy(esw, vport);
}

static int esw_acl_ingress_ofld_groups_create(struct mlx5_eswitch *esw,
					      struct mlx5_vport *vport)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *g;
	void *match_criteria;
	u32 *flow_group_in;
	u32 flow_index = 0;
	int ret = 0;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	if (vport->vport == MLX5_VPORT_UPLINK) {
		/* This group can hold an FTE to drop all traffic.
		 * Need in case LAG is enabled.
		 */
		MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, flow_index);
		MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, flow_index);

		g = mlx5_create_flow_group(vport->ingress.acl, flow_group_in);
		if (IS_ERR(g)) {
			ret = PTR_ERR(g);
			esw_warn(esw->dev, "vport[%d] ingress create drop flow group, err(%d)\n",
				 vport->vport, ret);
			goto drop_err;
		}
		vport->ingress.offloads.drop_grp = g;
		flow_index++;
	}

	if (esw_acl_ingress_prio_tag_enabled(esw, vport)) {
		/* This group is to hold FTE to match untagged packets when prio_tag
		 * is enabled.
		 */
		memset(flow_group_in, 0, inlen);
		match_criteria = MLX5_ADDR_OF(create_flow_group_in,
					      flow_group_in, match_criteria);
		MLX5_SET(create_flow_group_in, flow_group_in,
			 match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
		MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.cvlan_tag);
		MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, flow_index);
		MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, flow_index);

		g = mlx5_create_flow_group(vport->ingress.acl, flow_group_in);
		if (IS_ERR(g)) {
			ret = PTR_ERR(g);
			esw_warn(esw->dev, "vport[%d] ingress create untagged flow group, err(%d)\n",
				 vport->vport, ret);
			goto prio_tag_err;
		}
		vport->ingress.offloads.metadata_prio_tag_grp = g;
		flow_index++;
	}

	if (mlx5_eswitch_vport_match_metadata_enabled(esw)) {
		/* This group holds an FTE with no match to add metadata for
		 * tagged packets if prio-tag is enabled, or for all untagged
		 * traffic in case prio-tag is disabled.
		 */
		memset(flow_group_in, 0, inlen);
		MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, flow_index);
		MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, flow_index);

		g = mlx5_create_flow_group(vport->ingress.acl, flow_group_in);
		if (IS_ERR(g)) {
			ret = PTR_ERR(g);
			esw_warn(esw->dev, "vport[%d] ingress create drop flow group, err(%d)\n",
				 vport->vport, ret);
			goto metadata_err;
		}
		vport->ingress.offloads.metadata_allmatch_grp = g;
	}

	kvfree(flow_group_in);
	return 0;

metadata_err:
	if (!IS_ERR_OR_NULL(vport->ingress.offloads.metadata_prio_tag_grp)) {
		mlx5_destroy_flow_group(vport->ingress.offloads.metadata_prio_tag_grp);
		vport->ingress.offloads.metadata_prio_tag_grp = NULL;
	}
prio_tag_err:
	if (!IS_ERR_OR_NULL(vport->ingress.offloads.drop_grp)) {
		mlx5_destroy_flow_group(vport->ingress.offloads.drop_grp);
		vport->ingress.offloads.drop_grp = NULL;
	}
drop_err:
	kvfree(flow_group_in);
	return ret;
}

static void esw_acl_ingress_ofld_groups_destroy(struct mlx5_vport *vport)
{
	if (vport->ingress.offloads.metadata_allmatch_grp) {
		mlx5_destroy_flow_group(vport->ingress.offloads.metadata_allmatch_grp);
		vport->ingress.offloads.metadata_allmatch_grp = NULL;
	}

	if (vport->ingress.offloads.metadata_prio_tag_grp) {
		mlx5_destroy_flow_group(vport->ingress.offloads.metadata_prio_tag_grp);
		vport->ingress.offloads.metadata_prio_tag_grp = NULL;
	}

	if (vport->ingress.offloads.drop_grp) {
		mlx5_destroy_flow_group(vport->ingress.offloads.drop_grp);
		vport->ingress.offloads.drop_grp = NULL;
	}
}

int esw_acl_ingress_ofld_setup(struct mlx5_eswitch *esw,
			       struct mlx5_vport *vport)
{
	int num_ftes = 0;
	int err;

	if (!mlx5_eswitch_vport_match_metadata_enabled(esw) &&
	    !esw_acl_ingress_prio_tag_enabled(esw, vport))
		return 0;

	esw_acl_ingress_allow_rule_destroy(vport);

	if (mlx5_eswitch_vport_match_metadata_enabled(esw))
		num_ftes++;
	if (vport->vport == MLX5_VPORT_UPLINK)
		num_ftes++;
	if (esw_acl_ingress_prio_tag_enabled(esw, vport))
		num_ftes++;

	vport->ingress.acl = esw_acl_table_create(esw, vport,
						  MLX5_FLOW_NAMESPACE_ESW_INGRESS,
						  num_ftes);
	if (IS_ERR(vport->ingress.acl)) {
		err = PTR_ERR(vport->ingress.acl);
		vport->ingress.acl = NULL;
		return err;
	}

	err = esw_acl_ingress_ofld_groups_create(esw, vport);
	if (err)
		goto group_err;

	esw_debug(esw->dev,
		  "vport[%d] configure ingress rules\n", vport->vport);

	err = esw_acl_ingress_ofld_rules_create(esw, vport);
	if (err)
		goto rules_err;

	return 0;

rules_err:
	esw_acl_ingress_ofld_groups_destroy(vport);
group_err:
	esw_acl_ingress_table_destroy(vport);
	return err;
}

void esw_acl_ingress_ofld_cleanup(struct mlx5_eswitch *esw,
				  struct mlx5_vport *vport)
{
	esw_acl_ingress_ofld_rules_destroy(esw, vport);
	esw_acl_ingress_ofld_groups_destroy(vport);
	esw_acl_ingress_table_destroy(vport);
}

/* Caller must hold rtnl_lock */
int mlx5_esw_acl_ingress_vport_metadata_update(struct mlx5_eswitch *esw, u16 vport_num,
					       u32 metadata)
{
	struct mlx5_vport *vport = mlx5_eswitch_get_vport(esw, vport_num);
	int err;

	if (WARN_ON_ONCE(IS_ERR(vport))) {
		esw_warn(esw->dev, "vport(%d) invalid!\n", vport_num);
		return PTR_ERR(vport);
	}

	esw_acl_ingress_ofld_rules_destroy(esw, vport);

	vport->metadata = metadata ? metadata : vport->default_metadata;

	/* Recreate ingress acl rules with vport->metadata */
	err = esw_acl_ingress_ofld_rules_create(esw, vport);
	if (err)
		goto out;

	return 0;

out:
	vport->metadata = vport->default_metadata;
	return err;
}

int mlx5_esw_acl_ingress_vport_drop_rule_create(struct mlx5_eswitch *esw, u16 vport_num)
{
	struct mlx5_vport *vport = mlx5_eswitch_get_vport(esw, vport_num);

	if (IS_ERR(vport)) {
		esw_warn(esw->dev, "vport(%d) invalid!\n", vport_num);
		return PTR_ERR(vport);
	}

	return esw_acl_ingress_src_port_drop_create(esw, vport);
}

void mlx5_esw_acl_ingress_vport_drop_rule_destroy(struct mlx5_eswitch *esw, u16 vport_num)
{
	struct mlx5_vport *vport = mlx5_eswitch_get_vport(esw, vport_num);

	if (WARN_ON_ONCE(IS_ERR(vport))) {
		esw_warn(esw->dev, "vport(%d) invalid!\n", vport_num);
		return;
	}

	esw_acl_ingress_src_port_drop_destroy(esw, vport);
}
