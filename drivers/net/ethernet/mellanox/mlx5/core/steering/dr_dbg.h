/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

struct mlx5dr_dbg_dump_info {
	struct mutex dbg_mutex; /* protect dbg lists */
	struct dentry *steering_debugfs;
	struct dentry *fdb_debugfs;
};

void mlx5dr_dbg_init_dump(struct mlx5dr_domain *dmn);
void mlx5dr_dbg_uninit_dump(struct mlx5dr_domain *dmn);
void mlx5dr_dbg_tbl_add(struct mlx5dr_table *tbl);
void mlx5dr_dbg_tbl_del(struct mlx5dr_table *tbl);
void mlx5dr_dbg_rule_add(struct mlx5dr_rule *rule);
void mlx5dr_dbg_rule_del(struct mlx5dr_rule *rule);
