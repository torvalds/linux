/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies Ltd. */

#ifndef __MLX5_VNET_H_
#define __MLX5_VNET_H_

#include <linux/vdpa.h>
#include <linux/virtio_net.h>
#include <linux/vringh.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/qp.h>
#include "mlx5_vdpa.h"

static inline u32 mlx5_vdpa_max_qps(int max_vqs)
{
	return max_vqs / 2;
}

#define to_mlx5_vdpa_ndev(__mvdev) container_of(__mvdev, struct mlx5_vdpa_net, mvdev)
void *mlx5_vdpa_add_dev(struct mlx5_core_dev *mdev);
void mlx5_vdpa_remove_dev(struct mlx5_vdpa_dev *mvdev);

#endif /* __MLX5_VNET_H_ */
