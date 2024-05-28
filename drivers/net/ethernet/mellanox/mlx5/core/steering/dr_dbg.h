/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#define MLX5DR_DEBUG_DUMP_BUFF_SIZE (64 * 1024 * 1024)
#define MLX5DR_DEBUG_DUMP_BUFF_LENGTH 512

enum {
	MLX5DR_DEBUG_DUMP_STATE_FREE,
	MLX5DR_DEBUG_DUMP_STATE_IN_PROGRESS,
};

struct mlx5dr_dbg_dump_buff {
	char *buff;
	u32 index;
	struct list_head node;
};

struct mlx5dr_dbg_dump_data {
	struct list_head buff_list;
};

struct mlx5dr_dbg_dump_info {
	struct mutex dbg_mutex; /* protect dbg lists */
	struct dentry *steering_debugfs;
	struct dentry *fdb_debugfs;
	struct mlx5dr_dbg_dump_data *dump_data;
	atomic_t state;
};

void mlx5dr_dbg_init_dump(struct mlx5dr_domain *dmn);
void mlx5dr_dbg_uninit_dump(struct mlx5dr_domain *dmn);
void mlx5dr_dbg_tbl_add(struct mlx5dr_table *tbl);
void mlx5dr_dbg_tbl_del(struct mlx5dr_table *tbl);
void mlx5dr_dbg_rule_add(struct mlx5dr_rule *rule);
void mlx5dr_dbg_rule_del(struct mlx5dr_rule *rule);
