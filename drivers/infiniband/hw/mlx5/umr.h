/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. */

#ifndef _MLX5_IB_UMR_H
#define _MLX5_IB_UMR_H

#include "mlx5_ib.h"

int mlx5r_umr_resource_init(struct mlx5_ib_dev *dev);
void mlx5r_umr_resource_cleanup(struct mlx5_ib_dev *dev);

#endif /* _MLX5_IB_UMR_H */
