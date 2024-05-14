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

struct mlx5_flow_table *
mlx5e_post_meter_get_ft(struct mlx5e_post_meter_priv *post_meter);

struct mlx5e_post_meter_priv *
mlx5e_post_meter_init(struct mlx5e_priv *priv,
		      enum mlx5_flow_namespace_type ns_type,
		      struct mlx5e_post_act *post_act,
		      struct mlx5_fc *green_counter,
		      struct mlx5_fc *red_counter);
void
mlx5e_post_meter_cleanup(struct mlx5e_post_meter_priv *post_meter);

#endif /* __MLX5_EN_POST_METER_H__ */
