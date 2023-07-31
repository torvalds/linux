// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "fs_core.h"
#include "eswitch.h"
#include "en_accel/ipsec.h"
#include "esw/ipsec_fs.h"

enum {
	MLX5_ESW_IPSEC_RX_POL_FT_LEVEL,
	MLX5_ESW_IPSEC_RX_ESP_FT_LEVEL,
	MLX5_ESW_IPSEC_RX_ESP_FT_CHK_LEVEL,
};

static void esw_ipsec_rx_status_drop_destroy(struct mlx5e_ipsec *ipsec,
					     struct mlx5e_ipsec_rx *rx)
{
	mlx5_del_flow_rules(rx->status_drop.rule);
	mlx5_destroy_flow_group(rx->status_drop.group);
	mlx5_fc_destroy(ipsec->mdev, rx->status_drop_cnt);
}

static void esw_ipsec_rx_status_pass_destroy(struct mlx5e_ipsec *ipsec,
					     struct mlx5e_ipsec_rx *rx)
{
	mlx5_del_flow_rules(rx->status.rule);
	mlx5_chains_put_table(esw_chains(ipsec->mdev->priv.eswitch), 0, 1, 0);
}

static int esw_ipsec_rx_status_drop_create(struct mlx5e_ipsec *ipsec,
					   struct mlx5e_ipsec_rx *rx)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_table *ft = rx->ft.status;
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_fc *flow_counter;
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_group *g;
	u32 *flow_group_in;
	int err = 0;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!flow_group_in || !spec) {
		err = -ENOMEM;
		goto err_out;
	}

	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, ft->max_fte - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, ft->max_fte - 1);
	g = mlx5_create_flow_group(ft, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev,
			      "Failed to add ipsec rx status drop flow group, err=%d\n", err);
		goto err_out;
	}

	flow_counter = mlx5_fc_create(mdev, false);
	if (IS_ERR(flow_counter)) {
		err = PTR_ERR(flow_counter);
		mlx5_core_err(mdev,
			      "Failed to add ipsec rx status drop rule counter, err=%d\n", err);
		goto err_cnt;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP | MLX5_FLOW_CONTEXT_ACTION_COUNT;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest.counter_id = mlx5_fc_id(flow_counter);
	spec->flow_context.flow_source = MLX5_FLOW_CONTEXT_FLOW_SOURCE_UPLINK;
	rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev,
			      "Failed to add ipsec rx status drop rule, err=%d\n", err);
		goto err_rule;
	}

	rx->status_drop.group = g;
	rx->status_drop.rule = rule;
	rx->status_drop_cnt = flow_counter;

	kvfree(flow_group_in);
	kvfree(spec);
	return 0;

err_rule:
	mlx5_fc_destroy(mdev, flow_counter);
err_cnt:
	mlx5_destroy_flow_group(g);
err_out:
	kvfree(flow_group_in);
	kvfree(spec);
	return err;
}

static int esw_ipsec_rx_status_pass_create(struct mlx5e_ipsec *ipsec,
					   struct mlx5e_ipsec_rx *rx,
					   struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 misc_parameters_2.ipsec_syndrome);
	MLX5_SET(fte_match_param, spec->match_value,
		 misc_parameters_2.ipsec_syndrome, 0);
	spec->flow_context.flow_source = MLX5_FLOW_CONTEXT_FLOW_SOURCE_UPLINK;
	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;
	flow_act.flags = FLOW_ACT_NO_APPEND;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			  MLX5_FLOW_CONTEXT_ACTION_COUNT;
	rule = mlx5_add_flow_rules(rx->ft.status, spec, &flow_act, dest, 2);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_warn(ipsec->mdev,
			       "Failed to add ipsec rx status pass rule, err=%d\n", err);
		goto err_rule;
	}

	rx->status.rule = rule;
	kvfree(spec);
	return 0;

err_rule:
	kvfree(spec);
	return err;
}

void mlx5_esw_ipsec_rx_status_destroy(struct mlx5e_ipsec *ipsec,
				      struct mlx5e_ipsec_rx *rx)
{
	esw_ipsec_rx_status_pass_destroy(ipsec, rx);
	esw_ipsec_rx_status_drop_destroy(ipsec, rx);
}

int mlx5_esw_ipsec_rx_status_create(struct mlx5e_ipsec *ipsec,
				    struct mlx5e_ipsec_rx *rx,
				    struct mlx5_flow_destination *dest)
{
	int err;

	err = esw_ipsec_rx_status_drop_create(ipsec, rx);
	if (err)
		return err;

	err = esw_ipsec_rx_status_pass_create(ipsec, rx, dest);
	if (err)
		goto err_pass_create;

	return 0;

err_pass_create:
	esw_ipsec_rx_status_drop_destroy(ipsec, rx);
	return err;
}

void mlx5_esw_ipsec_rx_create_attr_set(struct mlx5e_ipsec *ipsec,
				       struct mlx5e_ipsec_rx_create_attr *attr)
{
	attr->prio = FDB_CRYPTO_INGRESS;
	attr->pol_level = MLX5_ESW_IPSEC_RX_POL_FT_LEVEL;
	attr->sa_level = MLX5_ESW_IPSEC_RX_ESP_FT_LEVEL;
	attr->status_level = MLX5_ESW_IPSEC_RX_ESP_FT_CHK_LEVEL;
	attr->chains_ns = MLX5_FLOW_NAMESPACE_FDB;
}

int mlx5_esw_ipsec_rx_status_pass_dest_get(struct mlx5e_ipsec *ipsec,
					   struct mlx5_flow_destination *dest)
{
	dest->type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest->ft = mlx5_chains_get_table(esw_chains(ipsec->mdev->priv.eswitch), 0, 1, 0);

	return 0;
}
