// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>

#include "core_acl_flex_actions.h"
#include "spectrum.h"
#include "spectrum_mr.h"

static int
mlxsw_sp2_mr_tcam_route_create(struct mlxsw_sp *mlxsw_sp, void *priv,
			       void *route_priv,
			       struct mlxsw_sp_mr_route_key *key,
			       struct mlxsw_afa_block *afa_block,
			       enum mlxsw_sp_mr_route_prio prio)
{
	return 0;
}

static void
mlxsw_sp2_mr_tcam_route_destroy(struct mlxsw_sp *mlxsw_sp, void *priv,
				void *route_priv,
				struct mlxsw_sp_mr_route_key *key)
{
}

static int
mlxsw_sp2_mr_tcam_route_update(struct mlxsw_sp *mlxsw_sp,
			       void *route_priv,
			       struct mlxsw_sp_mr_route_key *key,
			       struct mlxsw_afa_block *afa_block)
{
	return 0;
}

static int mlxsw_sp2_mr_tcam_init(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	return 0;
}

static void mlxsw_sp2_mr_tcam_fini(void *priv)
{
}

const struct mlxsw_sp_mr_tcam_ops mlxsw_sp2_mr_tcam_ops = {
	.init = mlxsw_sp2_mr_tcam_init,
	.fini = mlxsw_sp2_mr_tcam_fini,
	.route_create = mlxsw_sp2_mr_tcam_route_create,
	.route_destroy = mlxsw_sp2_mr_tcam_route_destroy,
	.route_update = mlxsw_sp2_mr_tcam_route_update,
};
