/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_SELQ_H__
#define __MLX5_EN_SELQ_H__

#include <linux/kernel.h>

struct mlx5e_selq_params;

struct mlx5e_selq {
	struct mlx5e_selq_params __rcu *active;
	struct mlx5e_selq_params *standby;
	struct mutex *state_lock; /* points to priv->state_lock */
	bool is_prepared;
};

struct mlx5e_params;
struct net_device;
struct sk_buff;

int mlx5e_selq_init(struct mlx5e_selq *selq, struct mutex *state_lock);
void mlx5e_selq_cleanup(struct mlx5e_selq *selq);
void mlx5e_selq_prepare(struct mlx5e_selq *selq, struct mlx5e_params *params, bool htb);
void mlx5e_selq_apply(struct mlx5e_selq *selq);
void mlx5e_selq_cancel(struct mlx5e_selq *selq);

u16 mlx5e_select_queue(struct net_device *dev, struct sk_buff *skb,
		       struct net_device *sb_dev);

#endif /* __MLX5_EN_SELQ_H__ */
