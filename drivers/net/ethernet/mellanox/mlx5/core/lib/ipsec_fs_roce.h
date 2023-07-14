/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_LIB_IPSEC_H__
#define __MLX5_LIB_IPSEC_H__

struct mlx5_ipsec_fs;

struct mlx5_flow_table *
mlx5_ipsec_fs_roce_ft_get(struct mlx5_ipsec_fs *ipsec_roce, u32 family);
void mlx5_ipsec_fs_roce_rx_destroy(struct mlx5_ipsec_fs *ipsec_roce,
				   u32 family);
int mlx5_ipsec_fs_roce_rx_create(struct mlx5_core_dev *mdev,
				 struct mlx5_ipsec_fs *ipsec_roce,
				 struct mlx5_flow_namespace *ns,
				 struct mlx5_flow_destination *default_dst,
				 u32 family, u32 level, u32 prio);
void mlx5_ipsec_fs_roce_tx_destroy(struct mlx5_ipsec_fs *ipsec_roce);
int mlx5_ipsec_fs_roce_tx_create(struct mlx5_core_dev *mdev,
				 struct mlx5_ipsec_fs *ipsec_roce,
				 struct mlx5_flow_table *pol_ft);
void mlx5_ipsec_fs_roce_cleanup(struct mlx5_ipsec_fs *ipsec_roce);
struct mlx5_ipsec_fs *mlx5_ipsec_fs_roce_init(struct mlx5_core_dev *mdev);

#endif /* __MLX5_LIB_IPSEC_H__ */
