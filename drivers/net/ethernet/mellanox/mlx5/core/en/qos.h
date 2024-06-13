/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5E_EN_QOS_H
#define __MLX5E_EN_QOS_H

#include <linux/mlx5/driver.h>

#define BYTES_IN_MBIT 125000

struct mlx5e_priv;
struct mlx5e_htb;
struct mlx5e_channels;
struct mlx5e_channel;
struct tc_htb_qopt_offload;

int mlx5e_qos_bytes_rate_check(struct mlx5_core_dev *mdev, u64 nbytes);
int mlx5e_qos_max_leaf_nodes(struct mlx5_core_dev *mdev);

/* SQ lifecycle */
int mlx5e_open_qos_sq(struct mlx5e_priv *priv, struct mlx5e_channels *chs,
		      u16 node_qid, u32 hw_id);
int mlx5e_activate_qos_sq(void *data, u16 node_qid, u32 hw_id);
void mlx5e_deactivate_qos_sq(struct mlx5e_priv *priv, u16 qid);
void mlx5e_close_qos_sq(struct mlx5e_priv *priv, u16 qid);
void mlx5e_reactivate_qos_sq(struct mlx5e_priv *priv, u16 qid, struct netdev_queue *txq);
void mlx5e_reset_qdisc(struct net_device *dev, u16 qid);

int mlx5e_qos_open_queues(struct mlx5e_priv *priv, struct mlx5e_channels *chs);
void mlx5e_qos_activate_queues(struct mlx5e_priv *priv);
void mlx5e_qos_deactivate_queues(struct mlx5e_channel *c);
void mlx5e_qos_deactivate_all_queues(struct mlx5e_channels *chs);
void mlx5e_qos_close_queues(struct mlx5e_channel *c);
void mlx5e_qos_close_all_queues(struct mlx5e_channels *chs);
int mlx5e_qos_alloc_queues(struct mlx5e_priv *priv, struct mlx5e_channels *chs);

/* TX datapath API */
u16 mlx5e_qid_from_qos(struct mlx5e_channels *chs, u16 qid);

/* HTB API */
int mlx5e_htb_setup_tc(struct mlx5e_priv *priv, struct tc_htb_qopt_offload *htb);

/* MQPRIO TX rate limit */
struct mlx5e_mqprio_rl;
struct mlx5e_mqprio_rl *mlx5e_mqprio_rl_alloc(void);
void mlx5e_mqprio_rl_free(struct mlx5e_mqprio_rl *rl);
int mlx5e_mqprio_rl_init(struct mlx5e_mqprio_rl *rl, struct mlx5_core_dev *mdev, u8 num_tc,
			 u64 max_rate[]);
void mlx5e_mqprio_rl_cleanup(struct mlx5e_mqprio_rl *rl);
int mlx5e_mqprio_rl_get_node_hw_id(struct mlx5e_mqprio_rl *rl, int tc, u32 *hw_id);
#endif
