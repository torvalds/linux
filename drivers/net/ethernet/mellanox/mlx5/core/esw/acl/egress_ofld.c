// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Inc. All rights reserved. */

#include "mlx5_core.h"
#include "eswitch.h"
#include "helper.h"
#include "ofld.h"

static void esw_acl_egress_ofld_fwd2vport_destroy(struct mlx5_vport *vport)
{
	if (!vport->egress.offloads.fwd_rule)
		return;

	mlx5_del_flow_rules(vport->egress.offloads.fwd_rule);
	vport->egress.offloads.fwd_rule = NULL;
}

static int esw_acl_egress_ofld_fwd2vport_create(struct mlx5_eswitch *esw,
						struct mlx5_vport *vport,
						struct mlx5_flow_destination *fwd_dest)
{
	struct mlx5_flow_act flow_act = {};
	int err = 0;

	esw_debug(esw->dev, "vport(%d) configure egress acl rule fwd2vport(%d)\n",
		  vport->vport, fwd_dest->vport.num);

	/* Delete the old egress forward-to-vport rule if any */
	esw_acl_egress_ofld_fwd2vport_destroy(vport);

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;

	vport->egress.offloads.fwd_rule =
		mlx5_add_flow_rules(vport->egress.acl, NULL,
				    &flow_act, fwd_dest, 1);
	if (IS_ERR(vport->egress.offloads.fwd_rule)) {
		err = PTR_ERR(vport->egress.offloads.fwd_rule);
		esw_warn(esw->dev,
			 "vport(%d) failed to add fwd2vport acl rule err(%d)\n",
			 vport->vport, err);
		vport->egress.offloads.fwd_rule = NULL;
	}

	return err;
}

static int esw_acl_egress_ofld_rules_create(struct mlx5_eswitch *esw,
					    struct mlx5_vport *vport,
					    struct mlx5_flow_destination *fwd_dest)
{
	int err = 0;
	int action;

	if (MLX5_CAP_GEN(esw->dev, prio_tag_required)) {
		/* For prio tag mode, there is only 1 FTEs:
		 * 1) prio tag packets - pop the prio tag VLAN, allow
		 * Unmatched traffic is allowed by default
		 */
		esw_debug(esw->dev,
			  "vport[%d] configure prio tag egress rules\n", vport->vport);

		action = MLX5_FLOW_CONTEXT_ACTION_VLAN_POP;
		action |= fwd_dest ? MLX5_FLOW_CONTEXT_ACTION_FWD_DEST :
			  MLX5_FLOW_CONTEXT_ACTION_ALLOW;

		/* prio tag vlan rule - pop it so vport receives untagged packets */
		err = esw_egress_acl_vlan_create(esw, vport, fwd_dest, 0, action);
		if (err)
			goto prio_err;
	}

	if (fwd_dest) {
		err = esw_acl_egress_ofld_fwd2vport_create(esw, vport, fwd_dest);
		if (err)
			goto fwd_err;
	}

	return 0;

fwd_err:
	esw_acl_egress_vlan_destroy(vport);
prio_err:
	return err;
}

static void esw_acl_egress_ofld_rules_destroy(struct mlx5_vport *vport)
{
	esw_acl_egress_vlan_destroy(vport);
	esw_acl_egress_ofld_fwd2vport_destroy(vport);
}

static int esw_acl_egress_ofld_groups_create(struct mlx5_eswitch *esw,
					     struct mlx5_vport *vport)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fwd_grp;
	u32 *flow_group_in;
	u32 flow_index = 0;
	int ret = 0;

	if (MLX5_CAP_GEN(esw->dev, prio_tag_required)) {
		ret = esw_acl_egress_vlan_grp_create(esw, vport);
		if (ret)
			return ret;

		flow_index++;
	}

	if (!mlx5_esw_acl_egress_fwd2vport_supported(esw))
		goto out;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in) {
		ret = -ENOMEM;
		goto fwd_grp_err;
	}

	/* This group holds 1 FTE to forward all packets to other vport
	 * when bond vports is supported.
	 */
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, flow_index);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, flow_index);
	fwd_grp = mlx5_create_flow_group(vport->egress.acl, flow_group_in);
	if (IS_ERR(fwd_grp)) {
		ret = PTR_ERR(fwd_grp);
		esw_warn(esw->dev,
			 "Failed to create vport[%d] egress fwd2vport flow group, err(%d)\n",
			 vport->vport, ret);
		kvfree(flow_group_in);
		goto fwd_grp_err;
	}
	vport->egress.offloads.fwd_grp = fwd_grp;
	kvfree(flow_group_in);
	return 0;

fwd_grp_err:
	esw_acl_egress_vlan_grp_destroy(vport);
out:
	return ret;
}

static void esw_acl_egress_ofld_groups_destroy(struct mlx5_vport *vport)
{
	if (!IS_ERR_OR_NULL(vport->egress.offloads.fwd_grp)) {
		mlx5_destroy_flow_group(vport->egress.offloads.fwd_grp);
		vport->egress.offloads.fwd_grp = NULL;
	}
	esw_acl_egress_vlan_grp_destroy(vport);
}

static bool esw_acl_egress_needed(const struct mlx5_eswitch *esw, u16 vport_num)
{
	return mlx5_eswitch_is_vf_vport(esw, vport_num) || mlx5_esw_is_sf_vport(esw, vport_num);
}

int esw_acl_egress_ofld_setup(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	int table_size = 0;
	int err;

	if (!mlx5_esw_acl_egress_fwd2vport_supported(esw) &&
	    !MLX5_CAP_GEN(esw->dev, prio_tag_required))
		return 0;

	if (!esw_acl_egress_needed(esw, vport->vport))
		return 0;

	esw_acl_egress_ofld_rules_destroy(vport);

	if (mlx5_esw_acl_egress_fwd2vport_supported(esw))
		table_size++;
	if (MLX5_CAP_GEN(esw->dev, prio_tag_required))
		table_size++;
	vport->egress.acl = esw_acl_table_create(esw, vport->vport,
						 MLX5_FLOW_NAMESPACE_ESW_EGRESS, table_size);
	if (IS_ERR(vport->egress.acl)) {
		err = PTR_ERR(vport->egress.acl);
		vport->egress.acl = NULL;
		return err;
	}

	err = esw_acl_egress_ofld_groups_create(esw, vport);
	if (err)
		goto group_err;

	esw_debug(esw->dev, "vport[%d] configure egress rules\n", vport->vport);

	err = esw_acl_egress_ofld_rules_create(esw, vport, NULL);
	if (err)
		goto rules_err;

	return 0;

rules_err:
	esw_acl_egress_ofld_groups_destroy(vport);
group_err:
	esw_acl_egress_table_destroy(vport);
	return err;
}

void esw_acl_egress_ofld_cleanup(struct mlx5_vport *vport)
{
	esw_acl_egress_ofld_rules_destroy(vport);
	esw_acl_egress_ofld_groups_destroy(vport);
	esw_acl_egress_table_destroy(vport);
}

int mlx5_esw_acl_egress_vport_bond(struct mlx5_eswitch *esw, u16 active_vport_num,
				   u16 passive_vport_num)
{
	struct mlx5_vport *passive_vport = mlx5_eswitch_get_vport(esw, passive_vport_num);
	struct mlx5_vport *active_vport = mlx5_eswitch_get_vport(esw, active_vport_num);
	struct mlx5_flow_destination fwd_dest = {};

	if (IS_ERR(active_vport))
		return PTR_ERR(active_vport);
	if (IS_ERR(passive_vport))
		return PTR_ERR(passive_vport);

	/* Cleanup and recreate rules WITHOUT fwd2vport of active vport */
	esw_acl_egress_ofld_rules_destroy(active_vport);
	esw_acl_egress_ofld_rules_create(esw, active_vport, NULL);

	/* Cleanup and recreate all rules + fwd2vport rule of passive vport to forward */
	esw_acl_egress_ofld_rules_destroy(passive_vport);
	fwd_dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	fwd_dest.vport.num = active_vport_num;
	fwd_dest.vport.vhca_id = MLX5_CAP_GEN(esw->dev, vhca_id);
	fwd_dest.vport.flags = MLX5_FLOW_DEST_VPORT_VHCA_ID;

	return esw_acl_egress_ofld_rules_create(esw, passive_vport, &fwd_dest);
}

int mlx5_esw_acl_egress_vport_unbond(struct mlx5_eswitch *esw, u16 vport_num)
{
	struct mlx5_vport *vport = mlx5_eswitch_get_vport(esw, vport_num);

	if (IS_ERR(vport))
		return PTR_ERR(vport);

	esw_acl_egress_ofld_rules_destroy(vport);
	return esw_acl_egress_ofld_rules_create(esw, vport, NULL);
}
