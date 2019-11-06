// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Inc. All rights reserved. */

#include "mlx5_core.h"
#include "eswitch.h"
#include "helper.h"
#include "ofld.h"

static int esw_acl_egress_ofld_rules_create(struct mlx5_eswitch *esw,
					    struct mlx5_vport *vport)
{
	if (!MLX5_CAP_GEN(esw->dev, prio_tag_required))
		return 0;

	/* For prio tag mode, there is only 1 FTEs:
	 * 1) prio tag packets - pop the prio tag VLAN, allow
	 * Unmatched traffic is allowed by default
	 */
	esw_debug(esw->dev,
		  "vport[%d] configure prio tag egress rules\n", vport->vport);

	/* prio tag vlan rule - pop it so vport receives untagged packets */
	return esw_egress_acl_vlan_create(esw, vport, NULL, 0,
					  MLX5_FLOW_CONTEXT_ACTION_VLAN_POP |
					  MLX5_FLOW_CONTEXT_ACTION_ALLOW);
}

static void esw_acl_egress_ofld_rules_destroy(struct mlx5_vport *vport)
{
	esw_acl_egress_vlan_destroy(vport);
}

static int esw_acl_egress_ofld_groups_create(struct mlx5_eswitch *esw,
					     struct mlx5_vport *vport)
{
	if (!MLX5_CAP_GEN(esw->dev, prio_tag_required))
		return 0;

	return esw_acl_egress_vlan_grp_create(esw, vport);
}

static void esw_acl_egress_ofld_groups_destroy(struct mlx5_vport *vport)
{
	esw_acl_egress_vlan_grp_destroy(vport);
}

int esw_acl_egress_ofld_setup(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	int err;

	if (!MLX5_CAP_GEN(esw->dev, prio_tag_required))
		return 0;

	esw_acl_egress_ofld_rules_destroy(vport);

	vport->egress.acl = esw_acl_table_create(esw, vport->vport,
						 MLX5_FLOW_NAMESPACE_ESW_EGRESS, 0);
	if (IS_ERR_OR_NULL(vport->egress.acl)) {
		err = PTR_ERR(vport->egress.acl);
		vport->egress.acl = NULL;
		return err;
	}

	err = esw_acl_egress_ofld_groups_create(esw, vport);
	if (err)
		goto group_err;

	esw_debug(esw->dev, "vport[%d] configure egress rules\n", vport->vport);

	err = esw_acl_egress_ofld_rules_create(esw, vport);
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
