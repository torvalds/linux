/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies. */

#ifndef __MLX5_EN_PTP_H__
#define __MLX5_EN_PTP_H__

#include "en.h"
#include "en/params.h"
#include "en_stats.h"

struct mlx5e_ptpsq {
	struct mlx5e_txqsq       txqsq;
};

struct mlx5e_port_ptp {
	/* data path */
	struct mlx5e_ptpsq         ptpsq[MLX5E_MAX_NUM_TC];
	struct napi_struct         napi;
	struct device             *pdev;
	struct net_device         *netdev;
	__be32                     mkey_be;
	u8                         num_tc;
	u8                         lag_port;

	/* data path - accessed per napi poll */
	struct irq_desc *irq_desc;
	struct mlx5e_ch_stats     *stats;

	/* control */
	struct mlx5e_priv         *priv;
	struct mlx5_core_dev      *mdev;
	struct hwtstamp_config    *tstamp;
	DECLARE_BITMAP(state, MLX5E_CHANNEL_NUM_STATES);
	int                        ix;
};

struct mlx5e_ptp_params {
	struct mlx5e_params        params;
	struct mlx5e_sq_param      txq_sq_param;
};

int mlx5e_port_ptp_open(struct mlx5e_priv *priv, struct mlx5e_params *params,
			u8 lag_port, struct mlx5e_port_ptp **cp);
void mlx5e_port_ptp_close(struct mlx5e_port_ptp *c);
void mlx5e_ptp_activate_channel(struct mlx5e_port_ptp *c);
void mlx5e_ptp_deactivate_channel(struct mlx5e_port_ptp *c);

#endif /* __MLX5_EN_PTP_H__ */
