/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_XSK_SETUP_H__
#define __MLX5_EN_XSK_SETUP_H__

#include "en.h"

struct mlx5e_xsk_param;

bool mlx5e_validate_xsk_param(struct mlx5e_params *params,
			      struct mlx5e_xsk_param *xsk,
			      struct mlx5_core_dev *mdev);
int mlx5e_open_xsk(struct mlx5e_priv *priv, struct mlx5e_params *params,
		   struct mlx5e_xsk_param *xsk, struct xdp_umem *umem,
		   struct mlx5e_channel *c);
void mlx5e_close_xsk(struct mlx5e_channel *c);
void mlx5e_activate_xsk(struct mlx5e_channel *c);
void mlx5e_deactivate_xsk(struct mlx5e_channel *c);
int mlx5e_xsk_redirect_rqt_to_channel(struct mlx5e_priv *priv, struct mlx5e_channel *c);
int mlx5e_xsk_redirect_rqt_to_drop(struct mlx5e_priv *priv, u16 ix);
int mlx5e_xsk_redirect_rqts_to_channels(struct mlx5e_priv *priv, struct mlx5e_channels *chs);
void mlx5e_xsk_redirect_rqts_to_drop(struct mlx5e_priv *priv, struct mlx5e_channels *chs);

#endif /* __MLX5_EN_XSK_SETUP_H__ */
