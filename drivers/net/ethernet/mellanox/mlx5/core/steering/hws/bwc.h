/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef HWS_BWC_H_
#define HWS_BWC_H_

#define MLX5HWS_BWC_MATCHER_INIT_SIZE_LOG 1
#define MLX5HWS_BWC_MATCHER_SIZE_LOG_STEP 1
#define MLX5HWS_BWC_MATCHER_REHASH_PERCENT_TH 70
#define MLX5HWS_BWC_MATCHER_REHASH_BURST_TH 32
#define MLX5HWS_BWC_MATCHER_ATTACH_AT_NUM 255

#define MLX5HWS_BWC_MAX_ACTS 16

struct mlx5hws_bwc_matcher {
	struct mlx5hws_matcher *matcher;
	struct mlx5hws_match_template *mt;
	struct mlx5hws_action_template *at[MLX5HWS_BWC_MATCHER_ATTACH_AT_NUM];
	u8 num_of_at;
	u16 priority;
	u8 size_log;
	u32 num_of_rules; /* atomically accessed */
	struct list_head *rules;
};

struct mlx5hws_bwc_rule {
	struct mlx5hws_bwc_matcher *bwc_matcher;
	struct mlx5hws_rule *rule;
	u16 bwc_queue_idx;
	struct list_head list_node;
};

int
mlx5hws_bwc_matcher_create_simple(struct mlx5hws_bwc_matcher *bwc_matcher,
				  struct mlx5hws_table *table,
				  u32 priority,
				  u8 match_criteria_enable,
				  struct mlx5hws_match_parameters *mask,
				  enum mlx5hws_action_type action_types[]);

int mlx5hws_bwc_matcher_destroy_simple(struct mlx5hws_bwc_matcher *bwc_matcher);

struct mlx5hws_bwc_rule *mlx5hws_bwc_rule_alloc(struct mlx5hws_bwc_matcher *bwc_matcher);

void mlx5hws_bwc_rule_free(struct mlx5hws_bwc_rule *bwc_rule);

int mlx5hws_bwc_rule_create_simple(struct mlx5hws_bwc_rule *bwc_rule,
				   u32 *match_param,
				   struct mlx5hws_rule_action rule_actions[],
				   u32 flow_source,
				   u16 bwc_queue_idx);

int mlx5hws_bwc_rule_destroy_simple(struct mlx5hws_bwc_rule *bwc_rule);

void mlx5hws_bwc_rule_fill_attr(struct mlx5hws_bwc_matcher *bwc_matcher,
				u16 bwc_queue_idx,
				u32 flow_source,
				struct mlx5hws_rule_attr *rule_attr);

static inline u16 mlx5hws_bwc_queues(struct mlx5hws_context *ctx)
{
	/* Besides the control queue, half of the queues are
	 * reguler HWS queues, and the other half are BWC queues.
	 */
	return (ctx->queues - 1) / 2;
}

static inline u16 mlx5hws_bwc_get_queue_id(struct mlx5hws_context *ctx, u16 idx)
{
	return idx + mlx5hws_bwc_queues(ctx);
}

#endif /* HWS_BWC_H_ */
