/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. */

#ifndef __MLX5_MACSEC_H__
#define __MLX5_MACSEC_H__

#include <net/macsec.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_addr.h>
#include "mlx5_ib.h"

#ifdef CONFIG_MLX5_MACSEC
struct mlx5_reserved_gids;

int mlx5r_add_gid_macsec_operations(const struct ib_gid_attr *attr);
void mlx5r_del_gid_macsec_operations(const struct ib_gid_attr *attr);
int mlx5r_macsec_init_gids_and_devlist(struct mlx5_ib_dev *dev);
void mlx5r_macsec_dealloc_gids(struct mlx5_ib_dev *dev);
void mlx5r_macsec_event_register(struct mlx5_ib_dev *dev);
void mlx5r_macsec_event_unregister(struct mlx5_ib_dev *dev);
#else
static inline int mlx5r_add_gid_macsec_operations(const struct ib_gid_attr *attr) { return 0; }
static inline void mlx5r_del_gid_macsec_operations(const struct ib_gid_attr *attr) {}
static inline int mlx5r_macsec_init_gids_and_devlist(struct mlx5_ib_dev *dev) { return 0; }
static inline void mlx5r_macsec_dealloc_gids(struct mlx5_ib_dev *dev) {}
static inline void mlx5r_macsec_event_register(struct mlx5_ib_dev *dev) {}
static inline void mlx5r_macsec_event_unregister(struct mlx5_ib_dev *dev) {}
#endif
#endif /* __MLX5_MACSEC_H__ */
