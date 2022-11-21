/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies */

#ifndef __MLX5E_TRAP_H__
#define __MLX5E_TRAP_H__

#include "../en.h"
#include "../devlink.h"

struct mlx5e_trap {
	/* data path */
	struct mlx5e_rq            rq;
	struct mlx5e_tir           tir;
	struct napi_struct         napi;
	struct device             *pdev;
	struct net_device         *netdev;
	__be32                     mkey_be;

	/* data path - accessed per napi poll */
	struct mlx5e_ch_stats     *stats;

	/* control */
	struct mlx5e_priv         *priv;
	struct mlx5_core_dev      *mdev;
	struct hwtstamp_config    *tstamp;
	DECLARE_BITMAP(state, MLX5E_CHANNEL_NUM_STATES);

	struct mlx5e_params        params;
	struct mlx5e_rq_param      rq_param;
};

void mlx5e_close_trap(struct mlx5e_trap *trap);
void mlx5e_deactivate_trap(struct mlx5e_priv *priv);
int mlx5e_handle_trap_event(struct mlx5e_priv *priv, struct mlx5_trap_ctx *trap_ctx);
int mlx5e_apply_traps(struct mlx5e_priv *priv, bool enable);

#endif
