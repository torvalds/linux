/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_TC_ACT_VLAN_H__
#define __MLX5_EN_TC_ACT_VLAN_H__

#include <net/flow_offload.h>
#include "en/tc_priv.h"

struct pedit_headers_action;

int
mlx5e_tc_act_vlan_add_push_action(struct mlx5e_priv *priv,
				  struct mlx5_flow_attr *attr,
				  struct net_device **out_dev,
				  struct netlink_ext_ack *extack);

int
mlx5e_tc_act_vlan_add_pop_action(struct mlx5e_priv *priv,
				 struct mlx5_flow_attr *attr,
				 struct netlink_ext_ack *extack);

int
mlx5e_tc_act_vlan_add_rewrite_action(struct mlx5e_priv *priv, int namespace,
				     const struct flow_action_entry *act,
				     struct mlx5e_tc_flow_parse_attr *parse_attr,
				     struct pedit_headers_action *hdrs,
				     u32 *action, struct netlink_ext_ack *extack);

#endif /* __MLX5_EN_TC_ACT_VLAN_H__ */
