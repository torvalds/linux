/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. */

#ifndef __MLX5_PCIE_CONG_EVENT_H__
#define __MLX5_PCIE_CONG_EVENT_H__

int mlx5e_pcie_cong_event_init(struct mlx5e_priv *priv);
void mlx5e_pcie_cong_event_cleanup(struct mlx5e_priv *priv);

#endif /* __MLX5_PCIE_CONG_EVENT_H__ */
