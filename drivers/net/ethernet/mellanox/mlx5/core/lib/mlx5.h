/*
 * Copyright (c) 2017, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __LIB_MLX5_H__
#define __LIB_MLX5_H__

#include "mlx5_core.h"

void mlx5_init_reserved_gids(struct mlx5_core_dev *dev);
void mlx5_cleanup_reserved_gids(struct mlx5_core_dev *dev);
int  mlx5_core_reserve_gids(struct mlx5_core_dev *dev, unsigned int count);
void mlx5_core_unreserve_gids(struct mlx5_core_dev *dev, unsigned int count);
int  mlx5_core_reserved_gid_alloc(struct mlx5_core_dev *dev, int *gid_index);
void mlx5_core_reserved_gid_free(struct mlx5_core_dev *dev, int gid_index);
int mlx5_crdump_enable(struct mlx5_core_dev *dev);
void mlx5_crdump_disable(struct mlx5_core_dev *dev);
int mlx5_crdump_collect(struct mlx5_core_dev *dev, u32 *cr_data);

static inline struct net *mlx5_core_net(struct mlx5_core_dev *dev)
{
	return devlink_net(priv_to_devlink(dev));
}

static inline struct net_device *mlx5_uplink_netdev_get(struct mlx5_core_dev *mdev)
{
	return mdev->mlx5e_res.uplink_netdev;
}

struct mlx5_sd;

static inline struct mlx5_sd *mlx5_get_sd(struct mlx5_core_dev *dev)
{
	return dev->sd;
}

static inline void mlx5_set_sd(struct mlx5_core_dev *dev, struct mlx5_sd *sd)
{
	dev->sd = sd;
}
#endif
