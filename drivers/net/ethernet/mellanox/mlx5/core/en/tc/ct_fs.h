/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#ifndef __MLX5_EN_TC_CT_FS_H__
#define __MLX5_EN_TC_CT_FS_H__

struct mlx5_ct_fs {
	const struct net_device *netdev;
	struct mlx5_core_dev *dev;
};

struct mlx5_ct_fs_rule {
};

struct mlx5_ct_fs_ops {
	struct mlx5_ct_fs_rule * (*ct_rule_add)(struct mlx5_ct_fs *fs,
						struct mlx5_flow_spec *spec,
						struct mlx5_flow_attr *attr);
	void (*ct_rule_del)(struct mlx5_ct_fs *fs, struct mlx5_ct_fs_rule *fs_rule);
};

struct mlx5_ct_fs_ops *mlx5_ct_fs_dmfs_ops_get(void);

#endif /* __MLX5_EN_TC_CT_FS_H__ */
