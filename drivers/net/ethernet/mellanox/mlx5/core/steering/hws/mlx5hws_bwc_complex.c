// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "mlx5hws_internal.h"

bool mlx5hws_bwc_match_params_is_complex(struct mlx5hws_context *ctx,
					 u8 match_criteria_enable,
					 struct mlx5hws_match_parameters *mask)
{
	struct mlx5hws_definer match_layout = {0};
	struct mlx5hws_match_template *mt;
	bool is_complex = false;
	int ret;

	if (!match_criteria_enable)
		return false; /* empty matcher */

	mt = mlx5hws_match_template_create(ctx,
					   mask->match_buf,
					   mask->match_sz,
					   match_criteria_enable);
	if (!mt) {
		mlx5hws_err(ctx, "BWC: failed creating match template\n");
		return false;
	}

	ret = mlx5hws_definer_calc_layout(ctx, mt, &match_layout);
	if (ret) {
		/* The only case that we're interested in is E2BIG,
		 * which means that the match parameters need to be
		 * split into complex martcher.
		 * For all other cases (good or bad) - just return true
		 * and let the usual match creation path handle it,
		 * both for good and bad flows.
		 */
		if (ret == -E2BIG) {
			is_complex = true;
			mlx5hws_dbg(ctx, "Matcher definer layout: need complex matcher\n");
		} else {
			mlx5hws_err(ctx, "Failed to calculate matcher definer layout\n");
		}
	}

	mlx5hws_match_template_destroy(mt);

	return is_complex;
}

int mlx5hws_bwc_matcher_create_complex(struct mlx5hws_bwc_matcher *bwc_matcher,
				       struct mlx5hws_table *table,
				       u32 priority,
				       u8 match_criteria_enable,
				       struct mlx5hws_match_parameters *mask)
{
	mlx5hws_err(table->ctx, "Complex matcher is not supported yet\n");
	return -EOPNOTSUPP;
}

void
mlx5hws_bwc_matcher_destroy_complex(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	/* nothing to do here */
}

int mlx5hws_bwc_rule_create_complex(struct mlx5hws_bwc_rule *bwc_rule,
				    struct mlx5hws_match_parameters *params,
				    u32 flow_source,
				    struct mlx5hws_rule_action rule_actions[],
				    u16 bwc_queue_idx)
{
	mlx5hws_err(bwc_rule->bwc_matcher->matcher->tbl->ctx,
		    "Complex rule is not supported yet\n");
	return -EOPNOTSUPP;
}

int mlx5hws_bwc_rule_destroy_complex(struct mlx5hws_bwc_rule *bwc_rule)
{
	return 0;
}

int mlx5hws_bwc_matcher_move_all_complex(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	mlx5hws_err(bwc_matcher->matcher->tbl->ctx,
		    "Moving complex rule is not supported yet\n");
	return -EOPNOTSUPP;
}
