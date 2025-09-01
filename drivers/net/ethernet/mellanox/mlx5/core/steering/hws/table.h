/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef MLX5HWS_TABLE_H_
#define MLX5HWS_TABLE_H_

struct mlx5hws_default_miss {
	/* My miss table */
	struct mlx5hws_table *miss_tbl;
	struct list_head next;
	/* Tables missing to my table */
	struct list_head head;
};

struct mlx5hws_table {
	struct mlx5hws_context *ctx;
	u32 ft_id;
	enum mlx5hws_table_type type;
	u32 fw_ft_type;
	u32 level;
	u16 uid;
	struct list_head matchers_list;
	struct list_head tbl_list_node;
	struct mlx5hws_default_miss default_miss;
};

static inline
u32 mlx5hws_table_get_fw_ft_type(enum mlx5hws_table_type type,
				 u8 *ret_type)
{
	if (type != MLX5HWS_TABLE_TYPE_FDB)
		return -EOPNOTSUPP;

	*ret_type = FS_FT_FDB;

	return 0;
}

static inline
u32 mlx5hws_table_get_res_fw_ft_type(enum mlx5hws_table_type tbl_type,
				     bool is_mirror)
{
	if (tbl_type == MLX5HWS_TABLE_TYPE_FDB)
		return is_mirror ? FS_FT_FDB_TX : FS_FT_FDB_RX;

	return 0;
}

int mlx5hws_table_create_default_ft(struct mlx5_core_dev *mdev,
				    struct mlx5hws_table *tbl,
				    u16 uid, u32 *ft_id);

void mlx5hws_table_destroy_default_ft(struct mlx5hws_table *tbl,
				      u32 ft_id);

int mlx5hws_table_connect_to_miss_table(struct mlx5hws_table *src_tbl,
					struct mlx5hws_table *dst_tbl);

int mlx5hws_table_update_connected_miss_tables(struct mlx5hws_table *dst_tbl);

int mlx5hws_table_ft_set_default_next_ft(struct mlx5hws_table *tbl, u32 ft_id);

int mlx5hws_table_ft_set_next_rtc(struct mlx5hws_context *ctx,
				  u32 ft_id,
				  u32 fw_ft_type,
				  u32 rtc_0_id,
				  u32 rtc_1_id);

int mlx5hws_table_ft_set_next_ft(struct mlx5hws_context *ctx,
				 u32 ft_id,
				 u32 fw_ft_type,
				 u32 next_ft_id);

#endif /* MLX5HWS_TABLE_H_ */
