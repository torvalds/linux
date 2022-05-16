/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies Inc. All rights reserved. */

#ifndef __MLX5_ESWITCH_ACL_OFLD_H__
#define __MLX5_ESWITCH_ACL_OFLD_H__

#include "eswitch.h"

/* Eswitch acl egress external APIs */
int esw_acl_egress_ofld_setup(struct mlx5_eswitch *esw, struct mlx5_vport *vport);
void esw_acl_egress_ofld_cleanup(struct mlx5_vport *vport);
int mlx5_esw_acl_egress_vport_bond(struct mlx5_eswitch *esw, u16 active_vport_num,
				   u16 passive_vport_num);
int mlx5_esw_acl_egress_vport_unbond(struct mlx5_eswitch *esw, u16 vport_num);

static inline bool mlx5_esw_acl_egress_fwd2vport_supported(struct mlx5_eswitch *esw)
{
	return esw && esw->mode == MLX5_ESWITCH_OFFLOADS &&
		mlx5_eswitch_vport_match_metadata_enabled(esw) &&
		MLX5_CAP_ESW_FLOWTABLE(esw->dev, egress_acl_forward_to_vport);
}

/* Eswitch acl ingress external APIs */
int esw_acl_ingress_ofld_setup(struct mlx5_eswitch *esw, struct mlx5_vport *vport);
void esw_acl_ingress_ofld_cleanup(struct mlx5_eswitch *esw, struct mlx5_vport *vport);
int mlx5_esw_acl_ingress_vport_bond_update(struct mlx5_eswitch *esw, u16 vport_num,
					   u32 metadata);

#endif /* __MLX5_ESWITCH_ACL_OFLD_H__ */
