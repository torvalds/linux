/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5E_EN_QOS_H
#define __MLX5E_EN_QOS_H

#include <linux/mlx5/driver.h>

#define MLX5E_QOS_MAX_LEAF_NODES 256

struct mlx5e_priv;
struct mlx5e_channels;
struct mlx5e_channel;

int mlx5e_qos_bytes_rate_check(struct mlx5_core_dev *mdev, u64 nbytes);
int mlx5e_qos_max_leaf_nodes(struct mlx5_core_dev *mdev);
int mlx5e_qos_cur_leaf_nodes(struct mlx5e_priv *priv);

/* TX datapath API */
int mlx5e_get_txq_by_classid(struct mlx5e_priv *priv, u16 classid);
struct mlx5e_txqsq *mlx5e_get_sq(struct mlx5e_priv *priv, int qid);

/* SQ lifecycle */
int mlx5e_qos_open_queues(struct mlx5e_priv *priv, struct mlx5e_channels *chs);
void mlx5e_qos_activate_queues(struct mlx5e_priv *priv);
void mlx5e_qos_deactivate_queues(struct mlx5e_channel *c);
void mlx5e_qos_close_queues(struct mlx5e_channel *c);

/* HTB API */
int mlx5e_htb_root_add(struct mlx5e_priv *priv, u16 htb_maj_id, u16 htb_defcls,
		       struct netlink_ext_ack *extack);
int mlx5e_htb_root_del(struct mlx5e_priv *priv);
int mlx5e_htb_leaf_alloc_queue(struct mlx5e_priv *priv, u16 classid,
			       u32 parent_classid, u64 rate, u64 ceil,
			       struct netlink_ext_ack *extack);
int mlx5e_htb_leaf_to_inner(struct mlx5e_priv *priv, u16 classid, u16 child_classid,
			    u64 rate, u64 ceil, struct netlink_ext_ack *extack);
int mlx5e_htb_leaf_del(struct mlx5e_priv *priv, u16 *classid,
		       struct netlink_ext_ack *extack);
int mlx5e_htb_leaf_del_last(struct mlx5e_priv *priv, u16 classid, bool force,
			    struct netlink_ext_ack *extack);
int mlx5e_htb_node_modify(struct mlx5e_priv *priv, u16 classid, u64 rate, u64 ceil,
			  struct netlink_ext_ack *extack);

/* MQPRIO TX rate limit */
struct mlx5e_mqprio_rl;
struct mlx5e_mqprio_rl *mlx5e_mqprio_rl_alloc(void);
void mlx5e_mqprio_rl_free(struct mlx5e_mqprio_rl *rl);
int mlx5e_mqprio_rl_init(struct mlx5e_mqprio_rl *rl, struct mlx5_core_dev *mdev, u8 num_tc,
			 u64 max_rate[]);
void mlx5e_mqprio_rl_cleanup(struct mlx5e_mqprio_rl *rl);
int mlx5e_mqprio_rl_get_node_hw_id(struct mlx5e_mqprio_rl *rl, int tc, u32 *hw_id);
#endif
