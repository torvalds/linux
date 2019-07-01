/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5E_EN_HEALTH_H
#define __MLX5E_EN_HEALTH_H

#include "en.h"

int mlx5e_reporter_tx_create(struct mlx5e_priv *priv);
void mlx5e_reporter_tx_destroy(struct mlx5e_priv *priv);
void mlx5e_reporter_tx_err_cqe(struct mlx5e_txqsq *sq);
int mlx5e_reporter_tx_timeout(struct mlx5e_txqsq *sq);

#endif
