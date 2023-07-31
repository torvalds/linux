/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_ESW_IPSEC_FS_H__
#define __MLX5_ESW_IPSEC_FS_H__

struct mlx5e_ipsec;

#ifdef CONFIG_MLX5_ESWITCH
void mlx5_esw_ipsec_rx_status_destroy(struct mlx5e_ipsec *ipsec,
				      struct mlx5e_ipsec_rx *rx);
int mlx5_esw_ipsec_rx_status_create(struct mlx5e_ipsec *ipsec,
				    struct mlx5e_ipsec_rx *rx,
				    struct mlx5_flow_destination *dest);
void mlx5_esw_ipsec_rx_create_attr_set(struct mlx5e_ipsec *ipsec,
				       struct mlx5e_ipsec_rx_create_attr *attr);
int mlx5_esw_ipsec_rx_status_pass_dest_get(struct mlx5e_ipsec *ipsec,
					   struct mlx5_flow_destination *dest);
#else
static inline void mlx5_esw_ipsec_rx_status_destroy(struct mlx5e_ipsec *ipsec,
						    struct mlx5e_ipsec_rx *rx) {}

static inline int mlx5_esw_ipsec_rx_status_create(struct mlx5e_ipsec *ipsec,
						  struct mlx5e_ipsec_rx *rx,
						  struct mlx5_flow_destination *dest)
{
	return  -EINVAL;
}

static inline void mlx5_esw_ipsec_rx_create_attr_set(struct mlx5e_ipsec *ipsec,
						     struct mlx5e_ipsec_rx_create_attr *attr) {}

static inline int mlx5_esw_ipsec_rx_status_pass_dest_get(struct mlx5e_ipsec *ipsec,
							 struct mlx5_flow_destination *dest)
{
	return -EINVAL;
}
#endif /* CONFIG_MLX5_ESWITCH */
#endif /* __MLX5_ESW_IPSEC_FS_H__ */
