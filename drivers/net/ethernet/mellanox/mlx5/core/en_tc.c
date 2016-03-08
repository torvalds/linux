/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/mlx5/fs.h>
#include <linux/mlx5/device.h>
#include <linux/rhashtable.h>
#include "en.h"
#include "en_tc.h"

struct mlx5e_tc_flow {
	struct rhash_head	node;
	u64			cookie;
	struct mlx5_flow_rule	*rule;
};

#define MLX5E_TC_FLOW_TABLE_NUM_ENTRIES 1024
#define MLX5E_TC_FLOW_TABLE_NUM_GROUPS 4

static struct mlx5_flow_rule *mlx5e_tc_add_flow(struct mlx5e_priv *priv,
						u32 *match_c, u32 *match_v,
						u32 action, u32 flow_tag)
{
	struct mlx5_flow_destination dest = {
		.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE,
		{.ft = priv->fts.vlan.t},
	};
	struct mlx5_flow_rule *rule;
	bool table_created = false;

	if (IS_ERR_OR_NULL(priv->fts.tc.t)) {
		priv->fts.tc.t =
			mlx5_create_auto_grouped_flow_table(priv->fts.ns, 0,
							    MLX5E_TC_FLOW_TABLE_NUM_ENTRIES,
							    MLX5E_TC_FLOW_TABLE_NUM_GROUPS);
		if (IS_ERR(priv->fts.tc.t)) {
			netdev_err(priv->netdev,
				   "Failed to create tc offload table\n");
			return ERR_CAST(priv->fts.tc.t);
		}

		table_created = true;
	}

	rule = mlx5_add_flow_rule(priv->fts.tc.t, MLX5_MATCH_OUTER_HEADERS,
				  match_c, match_v,
				  action, flow_tag,
				  action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST ? &dest : NULL);

	if (IS_ERR(rule) && table_created) {
		mlx5_destroy_flow_table(priv->fts.tc.t);
		priv->fts.tc.t = NULL;
	}

	return rule;
}

static void mlx5e_tc_del_flow(struct mlx5e_priv *priv,
			      struct mlx5_flow_rule *rule)
{
	mlx5_del_flow_rule(rule);

	if (!mlx5e_tc_num_filters(priv)) {
		mlx5_destroy_flow_table(priv->fts.tc.t);
		priv->fts.tc.t = NULL;
	}
}

static const struct rhashtable_params mlx5e_tc_flow_ht_params = {
	.head_offset = offsetof(struct mlx5e_tc_flow, node),
	.key_offset = offsetof(struct mlx5e_tc_flow, cookie),
	.key_len = sizeof(((struct mlx5e_tc_flow *)0)->cookie),
	.automatic_shrinking = true,
};

int mlx5e_tc_init(struct mlx5e_priv *priv)
{
	struct mlx5e_tc_flow_table *tc = &priv->fts.tc;

	tc->ht_params = mlx5e_tc_flow_ht_params;
	return rhashtable_init(&tc->ht, &tc->ht_params);
}

static void _mlx5e_tc_del_flow(void *ptr, void *arg)
{
	struct mlx5e_tc_flow *flow = ptr;
	struct mlx5e_priv *priv = arg;

	mlx5e_tc_del_flow(priv, flow->rule);
	kfree(flow);
}

void mlx5e_tc_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_tc_flow_table *tc = &priv->fts.tc;

	rhashtable_free_and_destroy(&tc->ht, _mlx5e_tc_del_flow, priv);

	if (!IS_ERR_OR_NULL(priv->fts.tc.t)) {
		mlx5_destroy_flow_table(priv->fts.tc.t);
		priv->fts.tc.t = NULL;
	}
}
