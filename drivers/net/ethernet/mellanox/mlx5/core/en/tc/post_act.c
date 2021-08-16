// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "post_act.h"
#include "mlx5_core.h"

struct mlx5e_post_act {
	enum mlx5_flow_namespace_type ns_type;
	struct mlx5_fs_chains *chains;
	struct mlx5_flow_table *ft;
	struct mlx5e_priv *priv;
};

struct mlx5e_post_act *
mlx5e_tc_post_act_init(struct mlx5e_priv *priv, struct mlx5_fs_chains *chains,
		       enum mlx5_flow_namespace_type ns_type)
{
	struct mlx5e_post_act *post_act;
	int err;

	if (ns_type == MLX5_FLOW_NAMESPACE_FDB &&
	    !MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev, ignore_flow_level)) {
		mlx5_core_warn(priv->mdev, "firmware level support is missing\n");
		err = -EOPNOTSUPP;
		goto err_check;
	} else if (!MLX5_CAP_FLOWTABLE_NIC_RX(priv->mdev, ignore_flow_level)) {
		mlx5_core_warn(priv->mdev, "firmware level support is missing\n");
		err = -EOPNOTSUPP;
		goto err_check;
	}

	post_act = kzalloc(sizeof(*post_act), GFP_KERNEL);
	if (!post_act) {
		err = -ENOMEM;
		goto err_check;
	}
	post_act->ft = mlx5_chains_create_global_table(chains);
	if (IS_ERR(post_act->ft)) {
		err = PTR_ERR(post_act->ft);
		mlx5_core_warn(priv->mdev, "failed to create post action table, err: %d\n", err);
		goto err_ft;
	}
	post_act->chains = chains;
	post_act->ns_type = ns_type;
	post_act->priv = priv;
	return post_act;

err_ft:
	kfree(post_act);
err_check:
	return ERR_PTR(err);
}

void
mlx5e_tc_post_act_destroy(struct mlx5e_post_act *post_act)
{
	if (IS_ERR_OR_NULL(post_act))
		return;

	mlx5_chains_destroy_global_table(post_act->chains, post_act->ft);
	kfree(post_act);
}
