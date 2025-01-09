// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2025 NVIDIA Corporation & Affiliates */

#include <mlx5_core.h>
#include <fs_core.h>
#include <fs_cmd.h>
#include "mlx5hws.h"

#define MLX5HWS_CTX_MAX_NUM_OF_QUEUES 16
#define MLX5HWS_CTX_QUEUE_SIZE 256

static int mlx5_cmd_hws_create_ns(struct mlx5_flow_root_namespace *ns)
{
	struct mlx5hws_context_attr hws_ctx_attr = {};

	hws_ctx_attr.queues = min_t(int, num_online_cpus(),
				    MLX5HWS_CTX_MAX_NUM_OF_QUEUES);
	hws_ctx_attr.queue_size = MLX5HWS_CTX_QUEUE_SIZE;

	ns->fs_hws_context.hws_ctx =
		mlx5hws_context_open(ns->dev, &hws_ctx_attr);
	if (!ns->fs_hws_context.hws_ctx) {
		mlx5_core_err(ns->dev, "Failed to create hws flow namespace\n");
		return -EINVAL;
	}
	return 0;
}

static int mlx5_cmd_hws_destroy_ns(struct mlx5_flow_root_namespace *ns)
{
	return mlx5hws_context_close(ns->fs_hws_context.hws_ctx);
}

static int mlx5_cmd_hws_set_peer(struct mlx5_flow_root_namespace *ns,
				 struct mlx5_flow_root_namespace *peer_ns,
				 u16 peer_vhca_id)
{
	struct mlx5hws_context *peer_ctx = NULL;

	if (peer_ns)
		peer_ctx = peer_ns->fs_hws_context.hws_ctx;
	mlx5hws_context_set_peer(ns->fs_hws_context.hws_ctx, peer_ctx,
				 peer_vhca_id);
	return 0;
}

static int mlx5_fs_set_ft_default_miss(struct mlx5_flow_root_namespace *ns,
				       struct mlx5_flow_table *ft,
				       struct mlx5_flow_table *next_ft)
{
	struct mlx5hws_table *next_tbl;
	int err;

	if (!ns->fs_hws_context.hws_ctx)
		return -EINVAL;

	/* if no change required, return */
	if (!next_ft && !ft->fs_hws_table.miss_ft_set)
		return 0;

	next_tbl = next_ft ? next_ft->fs_hws_table.hws_table : NULL;
	err = mlx5hws_table_set_default_miss(ft->fs_hws_table.hws_table, next_tbl);
	if (err) {
		mlx5_core_err(ns->dev, "Failed setting FT default miss (%d)\n", err);
		return err;
	}
	ft->fs_hws_table.miss_ft_set = !!next_tbl;
	return 0;
}

static int mlx5_cmd_hws_create_flow_table(struct mlx5_flow_root_namespace *ns,
					  struct mlx5_flow_table *ft,
					  struct mlx5_flow_table_attr *ft_attr,
					  struct mlx5_flow_table *next_ft)
{
	struct mlx5hws_context *ctx = ns->fs_hws_context.hws_ctx;
	struct mlx5hws_table_attr tbl_attr = {};
	struct mlx5hws_table *tbl;
	int err;

	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->create_flow_table(ns, ft, ft_attr,
								    next_ft);

	if (ns->table_type != FS_FT_FDB) {
		mlx5_core_err(ns->dev, "Table type %d not supported for HWS\n",
			      ns->table_type);
		return -EOPNOTSUPP;
	}

	tbl_attr.type = MLX5HWS_TABLE_TYPE_FDB;
	tbl_attr.level = ft_attr->level;
	tbl = mlx5hws_table_create(ctx, &tbl_attr);
	if (!tbl) {
		mlx5_core_err(ns->dev, "Failed creating hws flow_table\n");
		return -EINVAL;
	}

	ft->fs_hws_table.hws_table = tbl;
	ft->id = mlx5hws_table_get_id(tbl);

	if (next_ft) {
		err = mlx5_fs_set_ft_default_miss(ns, ft, next_ft);
		if (err)
			goto destroy_table;
	}

	ft->max_fte = INT_MAX;

	return 0;

destroy_table:
	mlx5hws_table_destroy(tbl);
	ft->fs_hws_table.hws_table = NULL;
	return err;
}

static int mlx5_cmd_hws_destroy_flow_table(struct mlx5_flow_root_namespace *ns,
					   struct mlx5_flow_table *ft)
{
	int err;

	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->destroy_flow_table(ns, ft);

	err = mlx5_fs_set_ft_default_miss(ns, ft, NULL);
	if (err)
		mlx5_core_err(ns->dev, "Failed to disconnect next table (%d)\n", err);

	err = mlx5hws_table_destroy(ft->fs_hws_table.hws_table);
	if (err)
		mlx5_core_err(ns->dev, "Failed to destroy flow_table (%d)\n", err);

	return err;
}

static int mlx5_cmd_hws_modify_flow_table(struct mlx5_flow_root_namespace *ns,
					  struct mlx5_flow_table *ft,
					  struct mlx5_flow_table *next_ft)
{
	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->modify_flow_table(ns, ft, next_ft);

	return mlx5_fs_set_ft_default_miss(ns, ft, next_ft);
}

static int mlx5_cmd_hws_update_root_ft(struct mlx5_flow_root_namespace *ns,
				       struct mlx5_flow_table *ft,
				       u32 underlay_qpn,
				       bool disconnect)
{
	return mlx5_fs_cmd_get_fw_cmds()->update_root_ft(ns, ft, underlay_qpn,
							 disconnect);
}

static int mlx5_cmd_hws_create_flow_group(struct mlx5_flow_root_namespace *ns,
					  struct mlx5_flow_table *ft, u32 *in,
					  struct mlx5_flow_group *fg)
{
	struct mlx5hws_match_parameters mask;
	struct mlx5hws_bwc_matcher *matcher;
	u8 match_criteria_enable;
	u32 priority;

	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->create_flow_group(ns, ft, in, fg);

	mask.match_buf = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	mask.match_sz = sizeof(fg->mask.match_criteria);

	match_criteria_enable = MLX5_GET(create_flow_group_in, in,
					 match_criteria_enable);
	priority = MLX5_GET(create_flow_group_in, in, start_flow_index);
	matcher = mlx5hws_bwc_matcher_create(ft->fs_hws_table.hws_table,
					     priority, match_criteria_enable,
					     &mask);
	if (!matcher) {
		mlx5_core_err(ns->dev, "Failed creating matcher\n");
		return -EINVAL;
	}

	fg->fs_hws_matcher.matcher = matcher;
	return 0;
}

static int mlx5_cmd_hws_destroy_flow_group(struct mlx5_flow_root_namespace *ns,
					   struct mlx5_flow_table *ft,
					   struct mlx5_flow_group *fg)
{
	if (mlx5_fs_cmd_is_fw_term_table(ft))
		return mlx5_fs_cmd_get_fw_cmds()->destroy_flow_group(ns, ft, fg);

	return mlx5hws_bwc_matcher_destroy(fg->fs_hws_matcher.matcher);
}

static const struct mlx5_flow_cmds mlx5_flow_cmds_hws = {
	.create_flow_table = mlx5_cmd_hws_create_flow_table,
	.destroy_flow_table = mlx5_cmd_hws_destroy_flow_table,
	.modify_flow_table = mlx5_cmd_hws_modify_flow_table,
	.update_root_ft = mlx5_cmd_hws_update_root_ft,
	.create_flow_group = mlx5_cmd_hws_create_flow_group,
	.destroy_flow_group = mlx5_cmd_hws_destroy_flow_group,
	.create_ns = mlx5_cmd_hws_create_ns,
	.destroy_ns = mlx5_cmd_hws_destroy_ns,
	.set_peer = mlx5_cmd_hws_set_peer,
};

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_hws_cmds(void)
{
	return &mlx5_flow_cmds_hws;
}
