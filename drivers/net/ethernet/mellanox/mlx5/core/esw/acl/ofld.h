/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies Inc. All rights reserved. */

#ifndef __MLX5_ESWITCH_ACL_OFLD_H__
#define __MLX5_ESWITCH_ACL_OFLD_H__

#include "eswitch.h"

/* Eswitch acl egress external APIs */
int esw_acl_egress_ofld_setup(struct mlx5_eswitch *esw, struct mlx5_vport *vport);
void esw_acl_egress_ofld_cleanup(struct mlx5_vport *vport);

#endif /* __MLX5_ESWITCH_ACL_OFLD_H__ */
