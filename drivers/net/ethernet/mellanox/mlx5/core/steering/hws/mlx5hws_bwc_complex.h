/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef MLX5HWS_BWC_COMPLEX_H_
#define MLX5HWS_BWC_COMPLEX_H_

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

#endif /* MLX5HWS_BWC_COMPLEX_H_ */
