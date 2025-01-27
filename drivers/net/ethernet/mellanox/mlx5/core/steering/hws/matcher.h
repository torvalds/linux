/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef HWS_MATCHER_H_
#define HWS_MATCHER_H_

/* We calculated that concatenating a collision table to the main table with
 * 3% of the main table rows will be enough resources for high insertion
 * success probability.
 *
 * The calculation: log2(2^x * 3 / 100) = log2(2^x) + log2(3/100) = x - 5.05 ~ 5
 */
#define MLX5HWS_MATCHER_ASSURED_ROW_RATIO 5
/* Threshold to determine if amount of rules require a collision table */
#define MLX5HWS_MATCHER_ASSURED_RULES_TH 10
/* Required depth of an assured collision table */
#define MLX5HWS_MATCHER_ASSURED_COL_TBL_DEPTH 4
/* Required depth of the main large table */
#define MLX5HWS_MATCHER_ASSURED_MAIN_TBL_DEPTH 2

enum mlx5hws_matcher_offset {
	MLX5HWS_MATCHER_OFFSET_TAG_DW1 = 12,
	MLX5HWS_MATCHER_OFFSET_TAG_DW0 = 13,
};

enum mlx5hws_matcher_flags {
	MLX5HWS_MATCHER_FLAGS_COLLISION = 1 << 2,
	MLX5HWS_MATCHER_FLAGS_RESIZABLE	= 1 << 3,
};

struct mlx5hws_match_template {
	struct mlx5hws_definer *definer;
	struct mlx5hws_definer_fc *fc;
	u32 *match_param;
	u8 match_criteria_enable;
	u16 fc_sz;
};

struct mlx5hws_matcher_match_ste {
	struct mlx5hws_pool_chunk ste;
	u32 rtc_0_id;
	u32 rtc_1_id;
	struct mlx5hws_pool *pool;
};

struct mlx5hws_matcher_action_ste {
	struct mlx5hws_pool_chunk ste;
	struct mlx5hws_pool_chunk stc;
	u32 rtc_0_id;
	u32 rtc_1_id;
	struct mlx5hws_pool *pool;
	u8 max_stes;
};

struct mlx5hws_matcher_resize_data_node {
	struct mlx5hws_pool_chunk stc;
	u32 rtc_0_id;
	u32 rtc_1_id;
	struct mlx5hws_pool *pool;
};

struct mlx5hws_matcher_resize_data {
	struct mlx5hws_matcher_resize_data_node action_ste[2];
	u8 max_stes;
	struct list_head list_node;
};

struct mlx5hws_matcher {
	struct mlx5hws_table *tbl;
	struct mlx5hws_matcher_attr attr;
	struct mlx5hws_match_template *mt;
	struct mlx5hws_action_template *at;
	u8 num_of_at;
	u8 num_of_mt;
	/* enum mlx5hws_matcher_flags */
	u8 flags;
	u32 end_ft_id;
	struct mlx5hws_matcher *col_matcher;
	struct mlx5hws_matcher *resize_dst;
	struct mlx5hws_matcher_match_ste match_ste;
	struct mlx5hws_matcher_action_ste action_ste[2];
	struct list_head list_node;
	struct list_head resize_data;
};

static inline bool
mlx5hws_matcher_mt_is_jumbo(struct mlx5hws_match_template *mt)
{
	return mlx5hws_definer_is_jumbo(mt->definer);
}

static inline bool mlx5hws_matcher_is_resizable(struct mlx5hws_matcher *matcher)
{
	return !!(matcher->flags & MLX5HWS_MATCHER_FLAGS_RESIZABLE);
}

static inline bool mlx5hws_matcher_is_in_resize(struct mlx5hws_matcher *matcher)
{
	return !!matcher->resize_dst;
}

static inline bool mlx5hws_matcher_is_insert_by_idx(struct mlx5hws_matcher *matcher)
{
	return matcher->attr.insert_mode == MLX5HWS_MATCHER_INSERT_BY_INDEX;
}

#endif /* HWS_MATCHER_H_ */
