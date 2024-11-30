// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"

int mlx5hws_vport_init_vports(struct mlx5hws_context *ctx)
{
	int ret;

	if (!ctx->caps->eswitch_manager)
		return 0;

	xa_init(&ctx->vports.vport_gvmi_xa);

	/* Set gvmi for eswitch manager and uplink vports only. Rest of the vports
	 * (vport 0 of other function, VFs and SFs) will be queried dynamically.
	 */

	ret = mlx5hws_cmd_query_gvmi(ctx->mdev, false, 0, &ctx->vports.esw_manager_gvmi);
	if (ret)
		return ret;

	ctx->vports.uplink_gvmi = 0;
	return 0;
}

void mlx5hws_vport_uninit_vports(struct mlx5hws_context *ctx)
{
	if (ctx->caps->eswitch_manager)
		xa_destroy(&ctx->vports.vport_gvmi_xa);
}

static int hws_vport_add_gvmi(struct mlx5hws_context *ctx, u16 vport)
{
	u16 vport_gvmi;
	int ret;

	ret = mlx5hws_cmd_query_gvmi(ctx->mdev, true, vport, &vport_gvmi);
	if (ret)
		return -EINVAL;

	ret = xa_insert(&ctx->vports.vport_gvmi_xa, vport,
			xa_mk_value(vport_gvmi), GFP_KERNEL);
	if (ret)
		mlx5hws_dbg(ctx, "Couldn't insert new vport gvmi into xarray (%d)\n", ret);

	return ret;
}

static bool hws_vport_is_esw_mgr_vport(struct mlx5hws_context *ctx, u16 vport)
{
	return ctx->caps->is_ecpf ? vport == MLX5_VPORT_ECPF :
				    vport == MLX5_VPORT_PF;
}

int mlx5hws_vport_get_gvmi(struct mlx5hws_context *ctx, u16 vport, u16 *vport_gvmi)
{
	void *entry;
	int ret;

	if (!ctx->caps->eswitch_manager)
		return -EINVAL;

	if (hws_vport_is_esw_mgr_vport(ctx, vport)) {
		*vport_gvmi = ctx->vports.esw_manager_gvmi;
		return 0;
	}

	if (vport == MLX5_VPORT_UPLINK) {
		*vport_gvmi = ctx->vports.uplink_gvmi;
		return 0;
	}

load_entry:
	entry = xa_load(&ctx->vports.vport_gvmi_xa, vport);

	if (!xa_is_value(entry)) {
		ret = hws_vport_add_gvmi(ctx, vport);
		if (ret && ret != -EBUSY)
			return ret;
		goto load_entry;
	}

	*vport_gvmi = (u16)xa_to_value(entry);
	return 0;
}
