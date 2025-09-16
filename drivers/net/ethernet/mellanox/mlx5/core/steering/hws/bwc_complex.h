/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef HWS_BWC_COMPLEX_H_
#define HWS_BWC_COMPLEX_H_

struct mlx5hws_bwc_complex_rule_hash_node {
	u32 match_buf[MLX5_ST_SZ_DW_MATCH_PARAM];
	u32 tag;
	refcount_t refcount;
	bool rtc_valid;
	u32 rtc_0;
	u32 rtc_1;
	struct rhash_head hash_node;
};

struct mlx5hws_bwc_matcher_complex_data {
	struct mlx5hws_table *isolated_tbl;
	struct mlx5hws_bwc_matcher *isolated_bwc_matcher;
	struct mlx5hws_action *action_metadata;
	struct mlx5hws_action *action_go_to_tbl;
	struct mlx5hws_action *action_last;
	struct rhashtable refcount_hash;
	struct mutex hash_lock; /* Protect the refcount rhashtable */
	struct ida metadata_ida;
};

bool mlx5hws_bwc_match_params_is_complex(struct mlx5hws_context *ctx,
					 u8 match_criteria_enable,
					 struct mlx5hws_match_parameters *mask);

int mlx5hws_bwc_matcher_create_complex(struct mlx5hws_bwc_matcher *bwc_matcher,
				       struct mlx5hws_table *table,
				       u32 priority,
				       u8 match_criteria_enable,
				       struct mlx5hws_match_parameters *mask);

void mlx5hws_bwc_matcher_destroy_complex(struct mlx5hws_bwc_matcher *bwc_matcher);

int mlx5hws_bwc_matcher_move_all_complex(struct mlx5hws_bwc_matcher *bwc_matcher);

int mlx5hws_bwc_rule_create_complex(struct mlx5hws_bwc_rule *bwc_rule,
				    struct mlx5hws_match_parameters *params,
				    u32 flow_source,
				    struct mlx5hws_rule_action rule_actions[],
				    u16 bwc_queue_idx);

int mlx5hws_bwc_rule_destroy_complex(struct mlx5hws_bwc_rule *bwc_rule);

#endif /* HWS_BWC_COMPLEX_H_ */
