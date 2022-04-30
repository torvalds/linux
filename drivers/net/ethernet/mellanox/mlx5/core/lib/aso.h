/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_LIB_ASO_H__
#define __MLX5_LIB_ASO_H__

#include "mlx5_core.h"

struct mlx5_aso;

struct mlx5_aso *mlx5_aso_create(struct mlx5_core_dev *mdev, u32 pdn);
void mlx5_aso_destroy(struct mlx5_aso *aso);
#endif /* __MLX5_LIB_ASO_H__ */
