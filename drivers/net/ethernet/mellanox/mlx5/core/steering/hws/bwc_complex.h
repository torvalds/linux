/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef HWS_BWC_COMPLEX_H_
#define HWS_BWC_COMPLEX_H_

#define MLX5HWS_BWC_COMPLEX_MAX_SUBMATCHERS 4

/* A matcher can't contain two rules with the same match tag, but it is possible
 * that two different complex rules' subrules have the same match tag. In that
 * case, those subrules correspond to a single rule, and we need to refcount.
 */
struct mlx5hws_bwc_complex_subrule_data {
	struct mlx5hws_rule_match_tag match_tag;
	refcount_t refcount;
	/* The chain_id is what glues individual subrules into larger complex
	 * rules. It is the value that this subrule writes to register C6, and
	 * that the next subrule matches against.
	 */
	u32 chain_id;
	u32 rtc_0;
	u32 rtc_1;
	/* During rehash we iterate through all the subrules to move them. But
	 * two or more subrules can share the same physical rule in the
	 * submatcher, so we use `was_moved` to keep track if a given rule was
	 * already moved.
	 */
	bool was_moved;
	struct rhash_head hash_node;
};

struct mlx5hws_bwc_complex_submatcher {
	/* Isolated table that the matcher lives in. Not set for the first
	 * matcher, which lives in the original table.
	 */
	struct mlx5hws_table *tbl;
	/* Match a rule with this action to go to `tbl`. This is set in all
	 * submatchers but the first.
	 */
	struct mlx5hws_action *action_tbl;
	/* This submatcher's simple matcher. The first submatcher points to the
	 * outer (complex) matcher.
	 */
	struct mlx5hws_bwc_matcher *bwc_matcher;
	struct rhashtable rules_hash;
	struct ida chain_ida;
	struct mutex hash_lock; /* Protect the hash and ida. */
};

struct mlx5hws_bwc_matcher_complex_data {
	struct mlx5hws_bwc_complex_submatcher
		submatchers[MLX5HWS_BWC_COMPLEX_MAX_SUBMATCHERS];
	int num_submatchers;
	/* Actions used by all but the last submatcher to point to the next
	 * submatcher in the chain. The last submatcher uses the action template
	 * from the complex matcher, to perform the actions that the user
	 * originally requested.
	 */
	struct mlx5hws_action *action_metadata;
	struct mlx5hws_action *action_last;
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

int mlx5hws_bwc_matcher_complex_move(struct mlx5hws_bwc_matcher *bwc_matcher);

int
mlx5hws_bwc_matcher_complex_move_first(struct mlx5hws_bwc_matcher *bwc_matcher);

int mlx5hws_bwc_rule_create_complex(struct mlx5hws_bwc_rule *bwc_rule,
				    struct mlx5hws_match_parameters *params,
				    u32 flow_source,
				    struct mlx5hws_rule_action rule_actions[],
				    u16 bwc_queue_idx);

int mlx5hws_bwc_rule_destroy_complex(struct mlx5hws_bwc_rule *bwc_rule);

#endif /* HWS_BWC_COMPLEX_H_ */
