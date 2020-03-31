/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5E_EN_HEALTH_H
#define __MLX5E_EN_HEALTH_H

#include "en.h"

#define MLX5E_RX_ERR_CQE(cqe) (get_cqe_opcode(cqe) != MLX5_CQE_RESP_SEND)

static inline bool cqe_syndrome_needs_recover(u8 syndrome)
{
	return syndrome == MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR ||
	       syndrome == MLX5_CQE_SYNDROME_LOCAL_PROT_ERR ||
	       syndrome == MLX5_CQE_SYNDROME_WR_FLUSH_ERR;
}

int mlx5e_reporter_tx_create(struct mlx5e_priv *priv);
void mlx5e_reporter_tx_destroy(struct mlx5e_priv *priv);
void mlx5e_reporter_tx_err_cqe(struct mlx5e_txqsq *sq);
int mlx5e_reporter_tx_timeout(struct mlx5e_txqsq *sq);

int mlx5e_reporter_cq_diagnose(struct mlx5e_cq *cq, struct devlink_fmsg *fmsg);
int mlx5e_reporter_cq_common_diagnose(struct mlx5e_cq *cq, struct devlink_fmsg *fmsg);
int mlx5e_reporter_named_obj_nest_start(struct devlink_fmsg *fmsg, char *name);
int mlx5e_reporter_named_obj_nest_end(struct devlink_fmsg *fmsg);

int mlx5e_reporter_rx_create(struct mlx5e_priv *priv);
void mlx5e_reporter_rx_destroy(struct mlx5e_priv *priv);
void mlx5e_reporter_icosq_cqe_err(struct mlx5e_icosq *icosq);
void mlx5e_reporter_rq_cqe_err(struct mlx5e_rq *rq);
void mlx5e_reporter_rx_timeout(struct mlx5e_rq *rq);

#define MLX5E_REPORTER_PER_Q_MAX_LEN 256

struct mlx5e_err_ctx {
	int (*recover)(void *ctx);
	void *ctx;
};

int mlx5e_health_sq_to_ready(struct mlx5e_channel *channel, u32 sqn);
int mlx5e_health_channel_eq_recover(struct mlx5_eq_comp *eq, struct mlx5e_channel *channel);
int mlx5e_health_recover_channels(struct mlx5e_priv *priv);
int mlx5e_health_report(struct mlx5e_priv *priv,
			struct devlink_health_reporter *reporter, char *err_str,
			struct mlx5e_err_ctx *err_ctx);
int mlx5e_health_create_reporters(struct mlx5e_priv *priv);
void mlx5e_health_destroy_reporters(struct mlx5e_priv *priv);
void mlx5e_health_channels_update(struct mlx5e_priv *priv);


#endif
