/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies Inc. All rights reserved. */

#ifndef __MLX5_ESWITCH_ACL_HELPER_H__
#define __MLX5_ESWITCH_ACL_HELPER_H__

#include "eswitch.h"

/* General acl helper functions */
struct mlx5_flow_table *
esw_acl_table_create(struct mlx5_eswitch *esw, struct mlx5_vport *vport, int ns, int size);

/* Egress acl helper functions */
void esw_acl_egress_table_destroy(struct mlx5_vport *vport);
int esw_egress_acl_vlan_create(struct mlx5_eswitch *esw, struct mlx5_vport *vport,
			       struct mlx5_flow_destination *fwd_dest,
			       u16 vlan_id, u32 flow_action);
void esw_acl_egress_vlan_destroy(struct mlx5_vport *vport);
int esw_acl_egress_vlan_grp_create(struct mlx5_eswitch *esw, struct mlx5_vport *vport);
void esw_acl_egress_vlan_grp_destroy(struct mlx5_vport *vport);

/* Ingress acl helper functions */
void esw_acl_ingress_table_destroy(struct mlx5_vport *vport);
void esw_acl_ingress_allow_rule_destroy(struct mlx5_vport *vport);

#endif /* __MLX5_ESWITCH_ACL_HELPER_H__ */
