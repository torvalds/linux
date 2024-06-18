/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5E_EN_HEALTH_H
#define __MLX5E_EN_HEALTH_H

#include "en.h"
#include "diag/rsc_dump.h"

static inline bool cqe_syndrome_needs_recover(u8 syndrome)
{
	return syndrome == MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR ||
	       syndrome == MLX5_CQE_SYNDROME_LOCAL_PROT_ERR ||
	       syndrome == MLX5_CQE_SYNDROME_WR_FLUSH_ERR;
}

void mlx5e_reporter_tx_create(struct mlx5e_priv *priv);
void mlx5e_reporter_tx_destroy(struct mlx5e_priv *priv);
void mlx5e_reporter_tx_err_cqe(struct mlx5e_txqsq *sq);
int mlx5e_reporter_tx_timeout(struct mlx5e_txqsq *sq);
void mlx5e_reporter_tx_ptpsq_unhealthy(struct mlx5e_ptpsq *ptpsq);

void mlx5e_health_cq_diag_fmsg(struct mlx5e_cq *cq, struct devlink_fmsg *fmsg);
void mlx5e_health_cq_common_diag_fmsg(struct mlx5e_cq *cq, struct devlink_fmsg *fmsg);
void mlx5e_health_eq_diag_fmsg(struct mlx5_eq_comp *eq, struct devlink_fmsg *fmsg);
void mlx5e_health_fmsg_named_obj_nest_start(struct devlink_fmsg *fmsg, char *name);
void mlx5e_health_fmsg_named_obj_nest_end(struct devlink_fmsg *fmsg);

void mlx5e_reporter_rx_create(struct mlx5e_priv *priv);
void mlx5e_reporter_rx_destroy(struct mlx5e_priv *priv);
void mlx5e_reporter_icosq_cqe_err(struct mlx5e_icosq *icosq);
void mlx5e_reporter_rq_cqe_err(struct mlx5e_rq *rq);
void mlx5e_reporter_rx_timeout(struct mlx5e_rq *rq);
void mlx5e_reporter_icosq_suspend_recovery(struct mlx5e_channel *c);
void mlx5e_reporter_icosq_resume_recovery(struct mlx5e_channel *c);

#define MLX5E_REPORTER_PER_Q_MAX_LEN 256

struct mlx5e_err_ctx {
	int (*recover)(void *ctx);
	int (*dump)(struct mlx5e_priv *priv, struct devlink_fmsg *fmsg, void *ctx);
	void *ctx;
};

int mlx5e_health_sq_to_ready(struct mlx5_core_dev *mdev, struct net_device *dev, u32 sqn);
int mlx5e_health_channel_eq_recover(struct net_device *dev, struct mlx5_eq_comp *eq,
				    struct mlx5e_ch_stats *stats);
int mlx5e_health_recover_channels(struct mlx5e_priv *priv);
int mlx5e_health_report(struct mlx5e_priv *priv,
			struct devlink_health_reporter *reporter, char *err_str,
			struct mlx5e_err_ctx *err_ctx);
void mlx5e_health_create_reporters(struct mlx5e_priv *priv);
void mlx5e_health_destroy_reporters(struct mlx5e_priv *priv);
void mlx5e_health_channels_update(struct mlx5e_priv *priv);
int mlx5e_health_rsc_fmsg_dump(struct mlx5e_priv *priv, struct mlx5_rsc_key *key,
			       struct devlink_fmsg *fmsg);
void mlx5e_health_queue_dump(struct mlx5e_priv *priv, struct devlink_fmsg *fmsg,
			     int queue_idx, char *lbl);
#endif
