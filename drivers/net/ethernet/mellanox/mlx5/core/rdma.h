/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_RDMA_H__
#define __MLX5_RDMA_H__

#include "mlx5_core.h"

#ifdef CONFIG_MLX5_ESWITCH

void mlx5_rdma_enable_roce(struct mlx5_core_dev *dev);
void mlx5_rdma_disable_roce(struct mlx5_core_dev *dev);

#else /* CONFIG_MLX5_ESWITCH */

static inline void mlx5_rdma_enable_roce(struct mlx5_core_dev *dev) {}
static inline void mlx5_rdma_disable_roce(struct mlx5_core_dev *dev) {}

#endif /* CONFIG_MLX5_ESWITCH */
#endif /* __MLX5_RDMA_H__ */
