/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_POST_METER_H__
#define __MLX5_EN_POST_METER_H__

#define packet_color_to_reg { \
	.mfield = MLX5_ACTION_IN_FIELD_METADATA_REG_C_5, \
	.moffset = 0, \
	.mlen = 8, \
	.soffset = MLX5_BYTE_OFF(fte_match_param, \
				 misc_parameters_2.metadata_reg_c_5), \
}

struct mlx5e_post_meter_priv;

enum mlx5e_post_meter_type {
	MLX5E_POST_METER_RATE = 0,
	MLX5E_POST_METER_MTU
};

#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)

struct mlx5_flow_table *
mlx5e_post_meter_get_ft(struct mlx5e_post_meter_priv *post_meter);

struct mlx5_flow_table *
mlx5e_post_meter_get_mtu_true_ft(struct mlx5e_post_meter_priv *post_meter);

struct mlx5_flow_table *
mlx5e_post_meter_get_mtu_false_ft(struct mlx5e_post_meter_priv *post_meter);

struct mlx5e_post_meter_priv *
mlx5e_post_meter_init(struct mlx5e_priv *priv,
		      enum mlx5_flow_namespace_type ns_type,
		      struct mlx5e_post_act *post_act,
		      enum mlx5e_post_meter_type type,
		      struct mlx5_fc *act_counter,
		      struct mlx5_fc *drop_counter,
		      struct mlx5_flow_attr *branch_true,
		      struct mlx5_flow_attr *branch_false);

void
mlx5e_post_meter_cleanup(struct mlx5_eswitch *esw, struct mlx5e_post_meter_priv *post_meter);

#else /* CONFIG_MLX5_CLS_ACT */

static inline struct mlx5_flow_table *
mlx5e_post_meter_get_mtu_true_ft(struct mlx5e_post_meter_priv *post_meter)
{
	return NULL;
}

static inline struct mlx5_flow_table *
mlx5e_post_meter_get_mtu_false_ft(struct mlx5e_post_meter_priv *post_meter)
{
	return NULL;
}

#endif

#endif /* __MLX5_EN_POST_METER_H__ */
