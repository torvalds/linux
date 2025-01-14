// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. */

#include "en_tc.h"
#include "en/tc_ct.h"
#include "en/tc/ct_fs.h"

static int mlx5_ct_fs_hmfs_init(struct mlx5_ct_fs *fs, struct mlx5_flow_table *ct,
				struct mlx5_flow_table *ct_nat, struct mlx5_flow_table *post_ct)
{
	return 0;
}

static void mlx5_ct_fs_hmfs_destroy(struct mlx5_ct_fs *fs)
{
}

static struct mlx5_ct_fs_rule *
mlx5_ct_fs_hmfs_ct_rule_add(struct mlx5_ct_fs *fs, struct mlx5_flow_spec *spec,
			    struct mlx5_flow_attr *attr, struct flow_rule *flow_rule)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static void mlx5_ct_fs_hmfs_ct_rule_del(struct mlx5_ct_fs *fs, struct mlx5_ct_fs_rule *fs_rule)
{
}

static int mlx5_ct_fs_hmfs_ct_rule_update(struct mlx5_ct_fs *fs, struct mlx5_ct_fs_rule *fs_rule,
					  struct mlx5_flow_spec *spec, struct mlx5_flow_attr *attr)
{
	return -EOPNOTSUPP;
}

static struct mlx5_ct_fs_ops hmfs_ops = {
	.ct_rule_add = mlx5_ct_fs_hmfs_ct_rule_add,
	.ct_rule_del = mlx5_ct_fs_hmfs_ct_rule_del,
	.ct_rule_update = mlx5_ct_fs_hmfs_ct_rule_update,

	.init = mlx5_ct_fs_hmfs_init,
	.destroy = mlx5_ct_fs_hmfs_destroy,
};

struct mlx5_ct_fs_ops *mlx5_ct_fs_hmfs_ops_get(void)
{
	return &hmfs_ops;
}
