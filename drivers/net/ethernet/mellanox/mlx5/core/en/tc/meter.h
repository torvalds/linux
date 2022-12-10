/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_FLOW_METER_H__
#define __MLX5_EN_FLOW_METER_H__

struct mlx5e_post_meter_priv;
struct mlx5e_flow_meter_aso_obj;
struct mlx5e_flow_meters;
struct mlx5_flow_attr;

enum mlx5e_flow_meter_mode {
	MLX5_RATE_LIMIT_BPS,
	MLX5_RATE_LIMIT_PPS,
};

struct mlx5e_flow_meter_params {
	enum mlx5e_flow_meter_mode mode;
	 /* police action index */
	u32 index;
	u64 rate;
	u64 burst;
	u32 mtu;
};

struct mlx5e_flow_meter_handle {
	struct mlx5e_flow_meters *flow_meters;
	struct mlx5e_flow_meter_aso_obj *meters_obj;
	u32 obj_id;
	u8 idx;

	int refcnt;
	struct hlist_node hlist;
	struct mlx5e_flow_meter_params params;

	struct mlx5_fc *act_counter;
	struct mlx5_fc *drop_counter;
};

struct mlx5e_meter_attr {
	struct mlx5e_flow_meter_params params;
	struct mlx5e_flow_meter_handle *meter;
	struct mlx5e_post_meter_priv *post_meter;
};

int
mlx5e_tc_meter_modify(struct mlx5_core_dev *mdev,
		      struct mlx5e_flow_meter_handle *meter,
		      struct mlx5e_flow_meter_params *meter_params);

struct mlx5e_flow_meter_handle *
mlx5e_tc_meter_get(struct mlx5_core_dev *mdev, struct mlx5e_flow_meter_params *params);
void
mlx5e_tc_meter_put(struct mlx5e_flow_meter_handle *meter);
int
mlx5e_tc_meter_update(struct mlx5e_flow_meter_handle *meter,
		      struct mlx5e_flow_meter_params *params);
struct mlx5e_flow_meter_handle *
mlx5e_tc_meter_replace(struct mlx5_core_dev *mdev, struct mlx5e_flow_meter_params *params);

enum mlx5_flow_namespace_type
mlx5e_tc_meter_get_namespace(struct mlx5e_flow_meters *flow_meters);

struct mlx5e_flow_meters *
mlx5e_flow_meters_init(struct mlx5e_priv *priv,
		       enum mlx5_flow_namespace_type ns_type,
		       struct mlx5e_post_act *post_action);
void
mlx5e_flow_meters_cleanup(struct mlx5e_flow_meters *flow_meters);

void
mlx5e_tc_meter_get_stats(struct mlx5e_flow_meter_handle *meter,
			 u64 *bytes, u64 *packets, u64 *drops, u64 *lastuse);

#endif /* __MLX5_EN_FLOW_METER_H__ */
