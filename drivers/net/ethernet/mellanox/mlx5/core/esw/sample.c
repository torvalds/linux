// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include "esw/sample.h"
#include "eswitch.h"

struct mlx5_esw_psample {
	struct mlx5e_priv *priv;
	struct mlx5_flow_table *termtbl;
	struct mlx5_flow_handle *termtbl_rule;
};

static int
sampler_termtbl_create(struct mlx5_esw_psample *esw_psample)
{
	struct mlx5_core_dev *dev = esw_psample->priv->mdev;
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_act act = {};
	int err;

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(dev, termination_table))  {
		mlx5_core_warn(dev, "termination table is not supported\n");
		return -EOPNOTSUPP;
	}

	root_ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!root_ns) {
		mlx5_core_warn(dev, "failed to get FDB flow namespace\n");
		return -EOPNOTSUPP;
	}

	ft_attr.flags = MLX5_FLOW_TABLE_TERMINATION | MLX5_FLOW_TABLE_UNMANAGED;
	ft_attr.autogroup.max_num_groups = 1;
	ft_attr.prio = FDB_SLOW_PATH;
	ft_attr.max_fte = 1;
	ft_attr.level = 1;
	esw_psample->termtbl = mlx5_create_auto_grouped_flow_table(root_ns, &ft_attr);
	if (IS_ERR(esw_psample->termtbl)) {
		err = PTR_ERR(esw_psample->termtbl);
		mlx5_core_warn(dev, "failed to create termtbl, err: %d\n", err);
		return err;
	}

	act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dest.vport.num = esw->manager_vport;
	esw_psample->termtbl_rule = mlx5_add_flow_rules(esw_psample->termtbl, NULL, &act, &dest, 1);
	if (IS_ERR(esw_psample->termtbl_rule)) {
		err = PTR_ERR(esw_psample->termtbl_rule);
		mlx5_core_warn(dev, "failed to create termtbl rule, err: %d\n", err);
		mlx5_destroy_flow_table(esw_psample->termtbl);
		return err;
	}

	return 0;
}

static void
sampler_termtbl_destroy(struct mlx5_esw_psample *esw_psample)
{
	mlx5_del_flow_rules(esw_psample->termtbl_rule);
	mlx5_destroy_flow_table(esw_psample->termtbl);
}

struct mlx5_esw_psample *
mlx5_esw_sample_init(struct mlx5e_priv *priv)
{
	struct mlx5_esw_psample *esw_psample;
	int err;

	esw_psample = kzalloc(sizeof(*esw_psample), GFP_KERNEL);
	if (!esw_psample)
		return ERR_PTR(-ENOMEM);
	esw_psample->priv = priv;
	err = sampler_termtbl_create(esw_psample);
	if (err)
		goto err_termtbl;

	return esw_psample;

err_termtbl:
	kfree(esw_psample);
	return ERR_PTR(err);
}

void
mlx5_esw_sample_cleanup(struct mlx5_esw_psample *esw_psample)
{
	if (IS_ERR_OR_NULL(esw_psample))
		return;

	sampler_termtbl_destroy(esw_psample);
	kfree(esw_psample);
}
