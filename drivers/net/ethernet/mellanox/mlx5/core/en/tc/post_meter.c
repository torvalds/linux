// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "en/tc_priv.h"
#include "post_meter.h"
#include "en/tc/post_act.h"

#define MLX5_PACKET_COLOR_BITS MLX5_REG_MAPPING_MBITS(PACKET_COLOR_TO_REG)
#define MLX5_PACKET_COLOR_MASK MLX5_REG_MAPPING_MASK(PACKET_COLOR_TO_REG)

struct mlx5e_post_meter_priv {
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct mlx5_flow_handle *fwd_green_rule;
	struct mlx5_flow_handle *drop_red_rule;
};

struct mlx5_flow_table *
mlx5e_post_meter_get_ft(struct mlx5e_post_meter_priv *post_meter)
{
	return post_meter->ft;
}

static int
mlx5e_post_meter_table_create(struct mlx5e_priv *priv,
			      enum mlx5_flow_namespace_type ns_type,
			      struct mlx5e_post_meter_priv *post_meter)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *root_ns;

	root_ns = mlx5_get_flow_namespace(priv->mdev, ns_type);
	if (!root_ns) {
		mlx5_core_warn(priv->mdev, "Failed to get namespace for flow meter\n");
		return -EOPNOTSUPP;
	}

	ft_attr.flags = MLX5_FLOW_TABLE_UNMANAGED;
	ft_attr.prio = FDB_SLOW_PATH;
	ft_attr.max_fte = 2;
	ft_attr.level = 1;

	post_meter->ft = mlx5_create_flow_table(root_ns, &ft_attr);
	if (IS_ERR(post_meter->ft)) {
		mlx5_core_warn(priv->mdev, "Failed to create post_meter table\n");
		return PTR_ERR(post_meter->ft);
	}

	return 0;
}

static int
mlx5e_post_meter_fg_create(struct mlx5e_priv *priv,
			   struct mlx5e_post_meter_priv *post_meter)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	void *misc2, *match_criteria;
	u32 *flow_group_in;
	int err = 0;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS_2);
	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in,
				      match_criteria);
	misc2 = MLX5_ADDR_OF(fte_match_param, match_criteria, misc_parameters_2);
	MLX5_SET(fte_match_set_misc2, misc2, metadata_reg_c_5, MLX5_PACKET_COLOR_MASK);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 1);

	post_meter->fg = mlx5_create_flow_group(post_meter->ft, flow_group_in);
	if (IS_ERR(post_meter->fg)) {
		mlx5_core_warn(priv->mdev, "Failed to create post_meter flow group\n");
		err = PTR_ERR(post_meter->fg);
	}

	kvfree(flow_group_in);
	return err;
}

static int
mlx5e_post_meter_rules_create(struct mlx5e_priv *priv,
			      struct mlx5e_post_meter_priv *post_meter,
			      struct mlx5e_post_act *post_act)
{
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	mlx5e_tc_match_to_reg_match(spec, PACKET_COLOR_TO_REG,
				    MLX5_FLOW_METER_COLOR_RED, MLX5_PACKET_COLOR_MASK);
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP;
	flow_act.flags |= FLOW_ACT_IGNORE_FLOW_LEVEL;

	rule = mlx5_add_flow_rules(post_meter->ft, spec, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		mlx5_core_warn(priv->mdev, "Failed to create post_meter flow drop rule\n");
		err = PTR_ERR(rule);
		goto err_red;
	}
	post_meter->drop_red_rule = rule;

	mlx5e_tc_match_to_reg_match(spec, PACKET_COLOR_TO_REG,
				    MLX5_FLOW_METER_COLOR_GREEN, MLX5_PACKET_COLOR_MASK);
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = mlx5e_tc_post_act_get_ft(post_act);

	rule = mlx5_add_flow_rules(post_meter->ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		mlx5_core_warn(priv->mdev, "Failed to create post_meter flow fwd rule\n");
		err = PTR_ERR(rule);
		goto err_green;
	}
	post_meter->fwd_green_rule = rule;

	kvfree(spec);
	return 0;

err_green:
	mlx5_del_flow_rules(post_meter->drop_red_rule);
err_red:
	kvfree(spec);
	return err;
}

static void
mlx5e_post_meter_rules_destroy(struct mlx5e_post_meter_priv *post_meter)
{
	mlx5_del_flow_rules(post_meter->drop_red_rule);
	mlx5_del_flow_rules(post_meter->fwd_green_rule);
}

static void
mlx5e_post_meter_fg_destroy(struct mlx5e_post_meter_priv *post_meter)
{
	mlx5_destroy_flow_group(post_meter->fg);
}

static void
mlx5e_post_meter_table_destroy(struct mlx5e_post_meter_priv *post_meter)
{
	mlx5_destroy_flow_table(post_meter->ft);
}

struct mlx5e_post_meter_priv *
mlx5e_post_meter_init(struct mlx5e_priv *priv,
		      enum mlx5_flow_namespace_type ns_type,
		      struct mlx5e_post_act *post_act)
{
	struct mlx5e_post_meter_priv *post_meter;
	int err;

	post_meter = kzalloc(sizeof(*post_meter), GFP_KERNEL);
	if (!post_meter)
		return ERR_PTR(-ENOMEM);

	err = mlx5e_post_meter_table_create(priv, ns_type, post_meter);
	if (err)
		goto err_ft;

	err = mlx5e_post_meter_fg_create(priv, post_meter);
	if (err)
		goto err_fg;

	err = mlx5e_post_meter_rules_create(priv, post_meter, post_act);
	if (err)
		goto err_rules;

	return post_meter;

err_rules:
	mlx5e_post_meter_fg_destroy(post_meter);
err_fg:
	mlx5e_post_meter_table_destroy(post_meter);
err_ft:
	kfree(post_meter);
	return ERR_PTR(err);
}

void
mlx5e_post_meter_cleanup(struct mlx5e_post_meter_priv *post_meter)
{
	mlx5e_post_meter_rules_destroy(post_meter);
	mlx5e_post_meter_fg_destroy(post_meter);
	mlx5e_post_meter_table_destroy(post_meter);
	kfree(post_meter);
}

