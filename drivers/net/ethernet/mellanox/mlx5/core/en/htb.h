/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5E_EN_HTB_H_
#define __MLX5E_EN_HTB_H_

#include "qos.h"

#define MLX5E_QOS_MAX_LEAF_NODES 256

struct mlx5e_selq;
struct mlx5e_htb;

typedef int (*mlx5e_fp_htb_enumerate)(void *data, u16 qid, u32 hw_id);
int mlx5e_htb_enumerate_leaves(struct mlx5e_htb *htb, mlx5e_fp_htb_enumerate callback, void *data);

int mlx5e_htb_cur_leaf_nodes(struct mlx5e_htb *htb);

/* TX datapath API */
int mlx5e_htb_get_txq_by_classid(struct mlx5e_htb *htb, u16 classid);

/* HTB TC handlers */

int
mlx5e_htb_leaf_alloc_queue(struct mlx5e_htb *htb, u16 classid,
			   u32 parent_classid, u64 rate, u64 ceil,
			   struct netlink_ext_ack *extack);
int
mlx5e_htb_leaf_to_inner(struct mlx5e_htb *htb, u16 classid, u16 child_classid,
			u64 rate, u64 ceil, struct netlink_ext_ack *extack);
int mlx5e_htb_leaf_del(struct mlx5e_htb *htb, u16 *classid,
		       struct netlink_ext_ack *extack);
int
mlx5e_htb_leaf_del_last(struct mlx5e_htb *htb, u16 classid, bool force,
			struct netlink_ext_ack *extack);
int
mlx5e_htb_node_modify(struct mlx5e_htb *htb, u16 classid, u64 rate, u64 ceil,
		      struct netlink_ext_ack *extack);
struct mlx5e_htb *mlx5e_htb_alloc(void);
void mlx5e_htb_free(struct mlx5e_htb *htb);
int mlx5e_htb_init(struct mlx5e_htb *htb, struct tc_htb_qopt_offload *htb_qopt,
		   struct net_device *netdev, struct mlx5_core_dev *mdev,
		   struct mlx5e_selq *selq, struct mlx5e_priv *priv);
void mlx5e_htb_cleanup(struct mlx5e_htb *htb);
#endif

