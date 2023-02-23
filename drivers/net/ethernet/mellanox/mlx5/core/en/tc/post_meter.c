// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "en/tc_priv.h"
#include "post_meter.h"
#include "en/tc/post_act.h"

#define MLX5_PACKET_COLOR_BITS MLX5_REG_MAPPING_MBITS(PACKET_COLOR_TO_REG)
#define MLX5_PACKET_COLOR_MASK MLX5_REG_MAPPING_MASK(PACKET_COLOR_TO_REG)

struct mlx5e_post_meter_rate_table {
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct mlx5_flow_handle *green_rule;
	struct mlx5_flow_attr *green_attr;
	struct mlx5_flow_handle *red_rule;
	struct mlx5_flow_attr *red_attr;
};

struct mlx5e_post_meter_mtu_table {
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_attr *attr;
};

struct mlx5e_post_meter_mtu_tables {
	struct mlx5e_post_meter_mtu_table green_table;
	struct mlx5e_post_meter_mtu_table red_table;
};

struct mlx5e_post_meter_priv {
	enum mlx5e_post_meter_type type;
	union {
		struct mlx5e_post_meter_rate_table rate_steering_table;
		struct mlx5e_post_meter_mtu_tables  mtu_tables;
	};
};

struct mlx5_flow_table *
mlx5e_post_meter_get_ft(struct mlx5e_post_meter_priv *post_meter)
{
	return post_meter->rate_steering_table.ft;
}

struct mlx5_flow_table *
mlx5e_post_meter_get_mtu_true_ft(struct mlx5e_post_meter_priv *post_meter)
{
	return post_meter->mtu_tables.green_table.ft;
}

struct mlx5_flow_table *
mlx5e_post_meter_get_mtu_false_ft(struct mlx5e_post_meter_priv *post_meter)
{
	return post_meter->mtu_tables.red_table.ft;
}

static struct mlx5_flow_table *
mlx5e_post_meter_table_create(struct mlx5e_priv *priv,
			      enum mlx5_flow_namespace_type ns_type)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *root_ns;

	root_ns = mlx5_get_flow_namespace(priv->mdev, ns_type);
	if (!root_ns) {
		mlx5_core_warn(priv->mdev, "Failed to get namespace for flow meter\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	ft_attr.flags = MLX5_FLOW_TABLE_UNMANAGED;
	ft_attr.prio = FDB_SLOW_PATH;
	ft_attr.max_fte = 2;
	ft_attr.level = 1;

	return mlx5_create_flow_table(root_ns, &ft_attr);
}

static int
mlx5e_post_meter_rate_fg_create(struct mlx5e_priv *priv,
				struct mlx5e_post_meter_priv *post_meter)
{
	struct mlx5e_post_meter_rate_table *table = &post_meter->rate_steering_table;
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

	table->fg = mlx5_create_flow_group(table->ft, flow_group_in);
	if (IS_ERR(table->fg)) {
		mlx5_core_warn(priv->mdev, "Failed to create post_meter flow group\n");
		err = PTR_ERR(table->fg);
	}

	kvfree(flow_group_in);
	return err;
}

static struct mlx5_flow_handle *
mlx5e_post_meter_add_rule(struct mlx5e_priv *priv,
			  struct mlx5e_post_meter_priv *post_meter,
			  struct mlx5_flow_spec *spec,
			  struct mlx5_flow_attr *attr,
			  struct mlx5_fc *act_counter,
			  struct mlx5_fc *drop_counter)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_flow_handle *ret;

	attr->action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_DROP)
		attr->counter = drop_counter;
	else
		attr->counter = act_counter;

	attr->flags |= MLX5_ATTR_FLAG_NO_IN_PORT;
	attr->inner_match_level = MLX5_MATCH_NONE;
	attr->outer_match_level = MLX5_MATCH_NONE;
	attr->chain = 0;
	attr->prio = 0;

	ret = mlx5_eswitch_add_offloaded_rule(esw, spec, attr);

	/* We did not create the counter, so we can't delete it.
	 * Avoid freeing the counter when the attr is deleted in free_branching_attr
	 */
	attr->action &= ~MLX5_FLOW_CONTEXT_ACTION_COUNT;

	return ret;
}

static int
mlx5e_post_meter_rate_rules_create(struct mlx5e_priv *priv,
				   struct mlx5e_post_meter_priv *post_meter,
				   struct mlx5e_post_act *post_act,
				   struct mlx5_fc *act_counter,
				   struct mlx5_fc *drop_counter,
				   struct mlx5_flow_attr *green_attr,
				   struct mlx5_flow_attr *red_attr)
{
	struct mlx5e_post_meter_rate_table *table = &post_meter->rate_steering_table;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	mlx5e_tc_match_to_reg_match(spec, PACKET_COLOR_TO_REG,
				    MLX5_FLOW_METER_COLOR_RED, MLX5_PACKET_COLOR_MASK);
	red_attr->ft = post_meter->rate_steering_table.ft;
	rule = mlx5e_post_meter_add_rule(priv, post_meter, spec, red_attr,
					 act_counter, drop_counter);
	if (IS_ERR(rule)) {
		mlx5_core_warn(priv->mdev, "Failed to create post_meter exceed rule\n");
		err = PTR_ERR(rule);
		goto err_red;
	}
	table->red_rule = rule;
	table->red_attr = red_attr;

	mlx5e_tc_match_to_reg_match(spec, PACKET_COLOR_TO_REG,
				    MLX5_FLOW_METER_COLOR_GREEN, MLX5_PACKET_COLOR_MASK);
	green_attr->ft = post_meter->rate_steering_table.ft;
	rule = mlx5e_post_meter_add_rule(priv, post_meter, spec, green_attr,
					 act_counter, drop_counter);
	if (IS_ERR(rule)) {
		mlx5_core_warn(priv->mdev, "Failed to create post_meter notexceed rule\n");
		err = PTR_ERR(rule);
		goto err_green;
	}
	table->green_rule = rule;
	table->green_attr = green_attr;

	kvfree(spec);
	return 0;

err_green:
	mlx5_del_flow_rules(table->red_rule);
err_red:
	kvfree(spec);
	return err;
}

static void
mlx5e_post_meter_rate_rules_destroy(struct mlx5_eswitch *esw,
				    struct mlx5e_post_meter_priv *post_meter)
{
	struct mlx5e_post_meter_rate_table *rate_table = &post_meter->rate_steering_table;

	mlx5_eswitch_del_offloaded_rule(esw, rate_table->red_rule, rate_table->red_attr);
	mlx5_eswitch_del_offloaded_rule(esw, rate_table->green_rule, rate_table->green_attr);
}

static void
mlx5e_post_meter_rate_fg_destroy(struct mlx5e_post_meter_priv *post_meter)
{
	mlx5_destroy_flow_group(post_meter->rate_steering_table.fg);
}

static void
mlx5e_post_meter_rate_table_destroy(struct mlx5e_post_meter_priv *post_meter)
{
	mlx5_destroy_flow_table(post_meter->rate_steering_table.ft);
}

static void
mlx5e_post_meter_mtu_rules_destroy(struct mlx5e_post_meter_priv *post_meter)
{
	struct mlx5e_post_meter_mtu_tables *mtu_tables = &post_meter->mtu_tables;

	mlx5_del_flow_rules(mtu_tables->green_table.rule);
	mlx5_del_flow_rules(mtu_tables->red_table.rule);
}

static void
mlx5e_post_meter_mtu_fg_destroy(struct mlx5e_post_meter_priv *post_meter)
{
	struct mlx5e_post_meter_mtu_tables *mtu_tables = &post_meter->mtu_tables;

	mlx5_destroy_flow_group(mtu_tables->green_table.fg);
	mlx5_destroy_flow_group(mtu_tables->red_table.fg);
}

static void
mlx5e_post_meter_mtu_table_destroy(struct mlx5e_post_meter_priv *post_meter)
{
	struct mlx5e_post_meter_mtu_tables *mtu_tables = &post_meter->mtu_tables;

	mlx5_destroy_flow_table(mtu_tables->green_table.ft);
	mlx5_destroy_flow_table(mtu_tables->red_table.ft);
}

static int
mlx5e_post_meter_rate_create(struct mlx5e_priv *priv,
			     enum mlx5_flow_namespace_type ns_type,
			     struct mlx5e_post_act *post_act,
			     struct mlx5_fc *act_counter,
			     struct mlx5_fc *drop_counter,
			     struct mlx5e_post_meter_priv *post_meter,
			     struct mlx5_flow_attr *green_attr,
			     struct mlx5_flow_attr *red_attr)
{
	struct mlx5_flow_table *ft;
	int err;

	post_meter->type = MLX5E_POST_METER_RATE;

	ft = mlx5e_post_meter_table_create(priv, ns_type);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_warn(priv->mdev, "Failed to create post_meter table\n");
		goto err_ft;
	}

	post_meter->rate_steering_table.ft = ft;

	err = mlx5e_post_meter_rate_fg_create(priv, post_meter);
	if (err)
		goto err_fg;

	err = mlx5e_post_meter_rate_rules_create(priv, post_meter, post_act,
						 act_counter, drop_counter,
						 green_attr, red_attr);
	if (err)
		goto err_rules;

	return 0;

err_rules:
	mlx5e_post_meter_rate_fg_destroy(post_meter);
err_fg:
	mlx5e_post_meter_rate_table_destroy(post_meter);
err_ft:
	return err;
}

static int
mlx5e_post_meter_create_mtu_table(struct mlx5e_priv *priv,
				  enum mlx5_flow_namespace_type ns_type,
				  struct mlx5e_post_meter_mtu_table *table)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *flow_group_in;
	int err;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	table->ft = mlx5e_post_meter_table_create(priv, ns_type);
	if (IS_ERR(table->ft)) {
		err = PTR_ERR(table->ft);
		goto err_ft;
	}

	/* create miss group */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 1);
	fg = mlx5_create_flow_group(table->ft, flow_group_in);
	if (IS_ERR(fg)) {
		err = PTR_ERR(fg);
		goto err_miss_grp;
	}
	table->fg = fg;

	kvfree(flow_group_in);
	return 0;

err_miss_grp:
	mlx5_destroy_flow_table(table->ft);
err_ft:
	kvfree(flow_group_in);
	return err;
}

static int
mlx5e_post_meter_mtu_create(struct mlx5e_priv *priv,
			    enum mlx5_flow_namespace_type ns_type,
			    struct mlx5e_post_act *post_act,
			    struct mlx5_fc *act_counter,
			    struct mlx5_fc *drop_counter,
			    struct mlx5e_post_meter_priv *post_meter,
			    struct mlx5_flow_attr *green_attr,
			    struct mlx5_flow_attr *red_attr)
{
	struct mlx5e_post_meter_mtu_tables *mtu_tables = &post_meter->mtu_tables;
	static struct mlx5_flow_spec zero_spec = {};
	struct mlx5_flow_handle *rule;
	int err;

	post_meter->type = MLX5E_POST_METER_MTU;

	err = mlx5e_post_meter_create_mtu_table(priv, ns_type, &mtu_tables->green_table);
	if (err)
		goto err_green_ft;

	green_attr->ft = mtu_tables->green_table.ft;
	rule = mlx5e_post_meter_add_rule(priv, post_meter, &zero_spec, green_attr,
					 act_counter, drop_counter);
	if (IS_ERR(rule)) {
		mlx5_core_warn(priv->mdev, "Failed to create post_meter conform rule\n");
		err = PTR_ERR(rule);
		goto err_green_rule;
	}
	mtu_tables->green_table.rule = rule;
	mtu_tables->green_table.attr = green_attr;

	err = mlx5e_post_meter_create_mtu_table(priv, ns_type, &mtu_tables->red_table);
	if (err)
		goto err_red_ft;

	red_attr->ft = mtu_tables->red_table.ft;
	rule = mlx5e_post_meter_add_rule(priv, post_meter, &zero_spec, red_attr,
					 act_counter, drop_counter);
	if (IS_ERR(rule)) {
		mlx5_core_warn(priv->mdev, "Failed to create post_meter exceed rule\n");
		err = PTR_ERR(rule);
		goto err_red_rule;
	}
	mtu_tables->red_table.rule = rule;
	mtu_tables->red_table.attr = red_attr;

	return 0;

err_red_rule:
	mlx5_destroy_flow_table(mtu_tables->red_table.ft);
err_red_ft:
	mlx5_del_flow_rules(mtu_tables->green_table.rule);
err_green_rule:
	mlx5_destroy_flow_table(mtu_tables->green_table.ft);
err_green_ft:
	return err;
}

struct mlx5e_post_meter_priv *
mlx5e_post_meter_init(struct mlx5e_priv *priv,
		      enum mlx5_flow_namespace_type ns_type,
		      struct mlx5e_post_act *post_act,
		      enum mlx5e_post_meter_type type,
		      struct mlx5_fc *act_counter,
		      struct mlx5_fc *drop_counter,
		      struct mlx5_flow_attr *branch_true,
		      struct mlx5_flow_attr *branch_false)
{
	struct mlx5e_post_meter_priv *post_meter;
	int err;

	post_meter = kzalloc(sizeof(*post_meter), GFP_KERNEL);
	if (!post_meter)
		return ERR_PTR(-ENOMEM);

	switch (type) {
	case MLX5E_POST_METER_MTU:
		err = mlx5e_post_meter_mtu_create(priv, ns_type, post_act,
						  act_counter, drop_counter, post_meter,
						  branch_true, branch_false);
		break;
	case MLX5E_POST_METER_RATE:
		err = mlx5e_post_meter_rate_create(priv, ns_type, post_act,
						   act_counter, drop_counter, post_meter,
						   branch_true, branch_false);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	if (err)
		goto err;

	return post_meter;

err:
	kfree(post_meter);
	return ERR_PTR(err);
}

static void
mlx5e_post_meter_rate_destroy(struct mlx5_eswitch *esw, struct mlx5e_post_meter_priv *post_meter)
{
	mlx5e_post_meter_rate_rules_destroy(esw, post_meter);
	mlx5e_post_meter_rate_fg_destroy(post_meter);
	mlx5e_post_meter_rate_table_destroy(post_meter);
}

static void
mlx5e_post_meter_mtu_destroy(struct mlx5e_post_meter_priv *post_meter)
{
	mlx5e_post_meter_mtu_rules_destroy(post_meter);
	mlx5e_post_meter_mtu_fg_destroy(post_meter);
	mlx5e_post_meter_mtu_table_destroy(post_meter);
}

void
mlx5e_post_meter_cleanup(struct mlx5_eswitch *esw, struct mlx5e_post_meter_priv *post_meter)
{
	if (post_meter->type == MLX5E_POST_METER_RATE)
		mlx5e_post_meter_rate_destroy(esw, post_meter);
	else
		mlx5e_post_meter_mtu_destroy(post_meter);

	kfree(post_meter);
}

