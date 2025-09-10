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

/* Action RTC size multiplier that is required in order
 * to support rule update for rules with action STEs.
 */
#define MLX5HWS_MATCHER_ACTION_RTC_UPDATE_MULT 1

/* Maximum number of action templates that can be attached to a matcher. */
#define MLX5HWS_MATCHER_MAX_AT 128

enum mlx5hws_matcher_offset {
	MLX5HWS_MATCHER_OFFSET_TAG_DW1 = 12,
	MLX5HWS_MATCHER_OFFSET_TAG_DW0 = 13,
};

enum mlx5hws_matcher_flags {
	MLX5HWS_MATCHER_FLAGS_COLLISION = 1 << 2,
	MLX5HWS_MATCHER_FLAGS_RESIZABLE	= 1 << 3,
	MLX5HWS_MATCHER_FLAGS_ISOLATED	= 1 << 4,
};

struct mlx5hws_match_template {
	struct mlx5hws_definer *definer;
	struct mlx5hws_definer_fc *fc;
	u32 *match_param;
	u8 match_criteria_enable;
	u16 fc_sz;
};

struct mlx5hws_matcher_match_ste {
	u32 rtc_0_id;
	u32 rtc_1_id;
	u32 ste_0_base;
	u32 ste_1_base;
};

enum {
	MLX5HWS_MATCHER_IPV_UNSET = 0,
	MLX5HWS_MATCHER_IPV_4 = 1,
	MLX5HWS_MATCHER_IPV_6 = 2,
};

struct mlx5hws_matcher {
	struct mlx5hws_table *tbl;
	struct mlx5hws_matcher_attr attr;
	struct mlx5hws_match_template *mt;
	struct mlx5hws_action_template *at;
	u8 num_of_at;
	u8 size_of_at_array;
	u8 num_of_mt;
	u8 num_of_action_stes;
	/* enum mlx5hws_matcher_flags */
	u8 flags;
	u8 matches_outer_ethertype:1;
	u8 matches_outer_ip_version:1;
	u8 matches_inner_ethertype:1;
	u8 matches_inner_ip_version:1;
	u8 outer_ip_version:2;
	u8 inner_ip_version:2;
	u32 end_ft_id;
	struct mlx5hws_matcher *col_matcher;
	struct mlx5hws_matcher *resize_dst;
	struct mlx5hws_matcher_match_ste match_ste;
	struct list_head list_node;
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

static inline bool mlx5hws_matcher_is_isolated(struct mlx5hws_matcher *matcher)
{
	return !!(matcher->flags & MLX5HWS_MATCHER_FLAGS_ISOLATED);
}

static inline bool mlx5hws_matcher_is_insert_by_idx(struct mlx5hws_matcher *matcher)
{
	return matcher->attr.insert_mode == MLX5HWS_MATCHER_INSERT_BY_INDEX;
}

int mlx5hws_matcher_update_end_ft_isolated(struct mlx5hws_table *tbl,
					   u32 miss_ft_id);

#endif /* HWS_MATCHER_H_ */
