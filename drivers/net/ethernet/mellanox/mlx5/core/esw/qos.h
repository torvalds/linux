/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_ESW_QOS_H__
#define __MLX5_ESW_QOS_H__

#ifdef CONFIG_MLX5_ESWITCH

int mlx5_esw_qos_init(struct mlx5_eswitch *esw);
void mlx5_esw_qos_cleanup(struct mlx5_eswitch *esw);

int mlx5_esw_qos_set_vport_rate(struct mlx5_vport *evport, u32 max_rate, u32 min_rate);
bool mlx5_esw_qos_get_vport_rate(struct mlx5_vport *vport, u32 *max_rate, u32 *min_rate);
void mlx5_esw_qos_vport_disable(struct mlx5_vport *vport);

void mlx5_esw_qos_vport_qos_free(struct mlx5_vport *vport);
u32 mlx5_esw_qos_vport_get_sched_elem_ix(const struct mlx5_vport *vport);
struct mlx5_esw_sched_node *mlx5_esw_qos_vport_get_parent(const struct mlx5_vport *vport);

int mlx5_esw_devlink_rate_leaf_tx_share_set(struct devlink_rate *rate_leaf, void *priv,
					    u64 tx_share, struct netlink_ext_ack *extack);
int mlx5_esw_devlink_rate_leaf_tx_max_set(struct devlink_rate *rate_leaf, void *priv,
					  u64 tx_max, struct netlink_ext_ack *extack);
int mlx5_esw_devlink_rate_leaf_tc_bw_set(struct devlink_rate *rate_node,
					 void *priv,
					 u32 *tc_bw,
					 struct netlink_ext_ack *extack);
int mlx5_esw_devlink_rate_node_tc_bw_set(struct devlink_rate *rate_node,
					 void *priv,
					 u32 *tc_bw,
					 struct netlink_ext_ack *extack);
int mlx5_esw_devlink_rate_node_tx_share_set(struct devlink_rate *rate_node, void *priv,
					    u64 tx_share, struct netlink_ext_ack *extack);
int mlx5_esw_devlink_rate_node_tx_max_set(struct devlink_rate *rate_node, void *priv,
					  u64 tx_max, struct netlink_ext_ack *extack);
int mlx5_esw_devlink_rate_node_new(struct devlink_rate *rate_node, void **priv,
				   struct netlink_ext_ack *extack);
int mlx5_esw_devlink_rate_node_del(struct devlink_rate *rate_node, void *priv,
				   struct netlink_ext_ack *extack);
int mlx5_esw_devlink_rate_leaf_parent_set(struct devlink_rate *devlink_rate,
					  struct devlink_rate *parent,
					  void *priv, void *parent_priv,
					  struct netlink_ext_ack *extack);
int mlx5_esw_devlink_rate_node_parent_set(struct devlink_rate *devlink_rate,
					  struct devlink_rate *parent,
					  void *priv, void *parent_priv,
					  struct netlink_ext_ack *extack);
#endif

#endif
