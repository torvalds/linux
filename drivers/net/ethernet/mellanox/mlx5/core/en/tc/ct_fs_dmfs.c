// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#include "en_tc.h"
#include "en/tc_ct.h"
#include "en/tc/ct_fs.h"

#define ct_dbg(fmt, args...)\
	netdev_dbg(fs->netdev, "ct_fs_dmfs debug: " fmt "\n", ##args)

struct mlx5_ct_fs_dmfs_rule {
	struct mlx5_ct_fs_rule fs_rule;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_attr *attr;
};

static int
mlx5_ct_fs_dmfs_init(struct mlx5_ct_fs *fs, struct mlx5_flow_table *ct,
		     struct mlx5_flow_table *ct_nat, struct mlx5_flow_table *post_ct)
{
	return 0;
}

static void
mlx5_ct_fs_dmfs_destroy(struct mlx5_ct_fs *fs)
{
}

static struct mlx5_ct_fs_rule *
mlx5_ct_fs_dmfs_ct_rule_add(struct mlx5_ct_fs *fs, struct mlx5_flow_spec *spec,
			    struct mlx5_flow_attr *attr, struct flow_rule *flow_rule)
{
	struct mlx5e_priv *priv = netdev_priv(fs->netdev);
	struct mlx5_ct_fs_dmfs_rule *dmfs_rule;
	int err;

	dmfs_rule = kzalloc(sizeof(*dmfs_rule), GFP_KERNEL);
	if (!dmfs_rule)
		return ERR_PTR(-ENOMEM);

	dmfs_rule->rule = mlx5_tc_rule_insert(priv, spec, attr);
	if (IS_ERR(dmfs_rule->rule)) {
		err = PTR_ERR(dmfs_rule->rule);
		ct_dbg("Failed to add ct entry fs rule");
		goto err_insert;
	}

	dmfs_rule->attr = attr;

	return &dmfs_rule->fs_rule;

err_insert:
	kfree(dmfs_rule);
	return ERR_PTR(err);
}

static void
mlx5_ct_fs_dmfs_ct_rule_del(struct mlx5_ct_fs *fs, struct mlx5_ct_fs_rule *fs_rule)
{
	struct mlx5_ct_fs_dmfs_rule *dmfs_rule = container_of(fs_rule,
							      struct mlx5_ct_fs_dmfs_rule,
							      fs_rule);

	mlx5_tc_rule_delete(netdev_priv(fs->netdev), dmfs_rule->rule, dmfs_rule->attr);
	kfree(dmfs_rule);
}

static struct mlx5_ct_fs_ops dmfs_ops = {
	.ct_rule_add = mlx5_ct_fs_dmfs_ct_rule_add,
	.ct_rule_del = mlx5_ct_fs_dmfs_ct_rule_del,

	.init = mlx5_ct_fs_dmfs_init,
	.destroy = mlx5_ct_fs_dmfs_destroy,
};

struct mlx5_ct_fs_ops *mlx5_ct_fs_dmfs_ops_get(void)
{
	return &dmfs_ops;
}
