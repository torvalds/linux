/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5E_EN_REPORTER_H
#define __MLX5E_EN_REPORTER_H

#include <linux/mlx5/driver.h>
#include "en.h"

int mlx5e_tx_reporter_create(struct mlx5e_priv *priv);
void mlx5e_tx_reporter_destroy(struct mlx5e_priv *priv);
void mlx5e_tx_reporter_err_cqe(struct mlx5e_txqsq *sq);
int mlx5e_tx_reporter_timeout(struct mlx5e_txqsq *sq);

#endif
