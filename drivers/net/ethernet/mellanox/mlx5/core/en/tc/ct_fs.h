/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#ifndef __MLX5_EN_TC_CT_FS_H__
#define __MLX5_EN_TC_CT_FS_H__

struct mlx5_ct_fs {
	const struct net_device *netdev;
	struct mlx5_core_dev *dev;

	/* private data */
	void *priv_data[];
};

struct mlx5_ct_fs_rule {
};

struct mlx5_ct_fs_ops {
	int (*init)(struct mlx5_ct_fs *fs, struct mlx5_flow_table *ct,
		    struct mlx5_flow_table *ct_nat, struct mlx5_flow_table *post_ct);
	void (*destroy)(struct mlx5_ct_fs *fs);

	struct mlx5_ct_fs_rule * (*ct_rule_add)(struct mlx5_ct_fs *fs,
						struct mlx5_flow_spec *spec,
						struct mlx5_flow_attr *attr,
						struct flow_rule *flow_rule);
	void (*ct_rule_del)(struct mlx5_ct_fs *fs, struct mlx5_ct_fs_rule *fs_rule);
	int (*ct_rule_update)(struct mlx5_ct_fs *fs, struct mlx5_ct_fs_rule *fs_rule,
			      struct mlx5_flow_spec *spec, struct mlx5_flow_attr *attr);

	size_t priv_size;
};

static inline void *mlx5_ct_fs_priv(struct mlx5_ct_fs *fs)
{
	return &fs->priv_data;
}

struct mlx5_ct_fs_ops *mlx5_ct_fs_dmfs_ops_get(void);

#if IS_ENABLED(CONFIG_MLX5_SW_STEERING)
struct mlx5_ct_fs_ops *mlx5_ct_fs_smfs_ops_get(void);
#else
static inline struct mlx5_ct_fs_ops *
mlx5_ct_fs_smfs_ops_get(void)
{
	return NULL;
}
#endif /* IS_ENABLED(CONFIG_MLX5_SW_STEERING) */

#endif /* __MLX5_EN_TC_CT_FS_H__ */
