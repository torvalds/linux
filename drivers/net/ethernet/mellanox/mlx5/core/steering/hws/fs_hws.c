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

static const struct mlx5_flow_cmds mlx5_flow_cmds_hws = {
	.create_ns = mlx5_cmd_hws_create_ns,
	.destroy_ns = mlx5_cmd_hws_destroy_ns,
	.set_peer = mlx5_cmd_hws_set_peer,
};

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_hws_cmds(void)
{
	return &mlx5_flow_cmds_hws;
}
