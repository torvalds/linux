/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved. */

#ifndef __MLX5E_EN_DEVLINK_H
#define __MLX5E_EN_DEVLINK_H

#include <net/devlink.h>
#include "en.h"

struct mlx5e_dev *mlx5e_create_devlink(struct device *dev);
void mlx5e_destroy_devlink(struct mlx5e_dev *mlx5e_dev);
int mlx5e_devlink_port_register(struct mlx5e_dev *mlx5e_dev,
				struct mlx5e_priv *priv,
				struct mlx5_core_dev *mdev);
void mlx5e_devlink_port_unregister(struct mlx5e_priv *priv);

static inline struct devlink_port *
mlx5e_devlink_get_dl_port(struct mlx5e_priv *priv)
{
	return &priv->mdev->mlx5e_res.dl_port;
}

#endif
