/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved */

#ifndef __MLX5_EN_DIM_H__
#define __MLX5_EN_DIM_H__

#include <linux/types.h>

/* Forward declarations */
struct work_struct;

void mlx5e_rx_dim_work(struct work_struct *work);
void mlx5e_tx_dim_work(struct work_struct *work);

#endif /* __MLX5_EN_DIM_H__ */
