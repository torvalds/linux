// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Inc. All rights reserved. */

#include "mlx5_core.h"
#include "eswitch.h"
#include "helper.h"
#include "lgcy.h"

static void esw_acl_egress_lgcy_rules_destroy(struct mlx5_vport *vport)
{
	esw_acl_egress_vlan_destroy(vport);
	if (!IS_ERR_OR_NULL(vport->egress.legacy.drop_rule)) {
		mlx5_del_flow_rules(vport->egress.legacy.drop_rule);
		vport->egress.legacy.drop_rule = NULL;
	}
}

static int esw_acl_egress_lgcy_groups_create(struct mlx5_eswitch *esw,
					     struct mlx5_vport *vport)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_group *drop_grp;
	u32 *flow_group_in;
	int err = 0;

	err = esw_acl_egress_vlan_grp_create(esw, vport);
	if (err)
		return err;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in) {
		err = -ENOMEM;
		goto alloc_err;
	}

	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 1);
	drop_grp = mlx5_create_flow_group(vport->egress.acl, flow_group_in);
	if (IS_ERR(drop_grp)) {
		err = PTR_ERR(drop_grp);
		esw_warn(dev, "Failed to create E-Switch vport[%d] egress drop flow group, err(%d)\n",
			 vport->vport, err);
		goto drop_grp_err;
	}

	vport->egress.legacy.drop_grp = drop_grp;
	kvfree(flow_group_in);
	return 0;

drop_grp_err:
	kvfree(flow_group_in);
alloc_err:
	esw_acl_egress_vlan_grp_destroy(vport);
	return err;
}

static void esw_acl_egress_lgcy_groups_destroy(struct mlx5_vport *vport)
{
	if (!IS_ERR_OR_NULL(vport->egress.legacy.drop_grp)) {
		mlx5_destroy_flow_group(vport->egress.legacy.drop_grp);
		vport->egress.legacy.drop_grp = NULL;
	}
	esw_acl_egress_vlan_grp_destroy(vport);
}

int esw_acl_egress_lgcy_setup(struct mlx5_eswitch *esw,
			      struct mlx5_vport *vport)
{
	struct mlx5_flow_destination drop_ctr_dst = {};
	struct mlx5_flow_destination *dst = NULL;
	struct mlx5_fc *drop_counter = NULL;
	struct mlx5_flow_act flow_act = {};
	/* The egress acl table contains 2 rules:
	 * 1)Allow traffic with vlan_tag=vst_vlan_id
	 * 2)Drop all other traffic.
	 */
	int table_size = 2;
	int dest_num = 0;
	int err = 0;

	if (MLX5_CAP_ESW_EGRESS_ACL(esw->dev, flow_counter)) {
		drop_counter = mlx5_fc_create(esw->dev, false);
		if (IS_ERR(drop_counter))
			esw_warn(esw->dev,
				 "vport[%d] configure egress drop rule counter err(%ld)\n",
				 vport->vport, PTR_ERR(drop_counter));
		vport->egress.legacy.drop_counter = drop_counter;
	}

	esw_acl_egress_lgcy_rules_destroy(vport);

	if (!vport->info.vlan && !vport->info.qos) {
		esw_acl_egress_lgcy_cleanup(esw, vport);
		return 0;
	}

	if (!vport->egress.acl) {
		vport->egress.acl = esw_acl_table_create(esw, vport->vport,
							 MLX5_FLOW_NAMESPACE_ESW_EGRESS,
							 table_size);
		if (IS_ERR(vport->egress.acl)) {
			err = PTR_ERR(vport->egress.acl);
			vport->egress.acl = NULL;
			goto out;
		}

		err = esw_acl_egress_lgcy_groups_create(esw, vport);
		if (err)
			goto out;
	}

	esw_debug(esw->dev,
		  "vport[%d] configure egress rules, vlan(%d) qos(%d)\n",
		  vport->vport, vport->info.vlan, vport->info.qos);

	/* Allowed vlan rule */
	err = esw_egress_acl_vlan_create(esw, vport, NULL, vport->info.vlan,
					 MLX5_FLOW_CONTEXT_ACTION_ALLOW);
	if (err)
		goto out;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP;

	/* Attach egress drop flow counter */
	if (!IS_ERR_OR_NULL(drop_counter)) {
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
		drop_ctr_dst.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		drop_ctr_dst.counter_id = mlx5_fc_id(drop_counter);
		dst = &drop_ctr_dst;
		dest_num++;
	}
	vport->egress.legacy.drop_rule =
		mlx5_add_flow_rules(vport->egress.acl, NULL,
				    &flow_act, dst, dest_num);
	if (IS_ERR(vport->egress.legacy.drop_rule)) {
		err = PTR_ERR(vport->egress.legacy.drop_rule);
		esw_warn(esw->dev,
			 "vport[%d] configure egress drop rule failed, err(%d)\n",
			 vport->vport, err);
		vport->egress.legacy.drop_rule = NULL;
		goto out;
	}

	return err;

out:
	esw_acl_egress_lgcy_cleanup(esw, vport);
	return err;
}

void esw_acl_egress_lgcy_cleanup(struct mlx5_eswitch *esw,
				 struct mlx5_vport *vport)
{
	if (IS_ERR_OR_NULL(vport->egress.acl))
		goto clean_drop_counter;

	esw_debug(esw->dev, "Destroy vport[%d] E-Switch egress ACL\n", vport->vport);

	esw_acl_egress_lgcy_rules_destroy(vport);
	esw_acl_egress_lgcy_groups_destroy(vport);
	esw_acl_egress_table_destroy(vport);

clean_drop_counter:
	if (!IS_ERR_OR_NULL(vport->egress.legacy.drop_counter)) {
		mlx5_fc_destroy(esw->dev, vport->egress.legacy.drop_counter);
		vport->egress.legacy.drop_counter = NULL;
	}
}
