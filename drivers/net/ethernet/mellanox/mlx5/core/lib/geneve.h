/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_GENEVE_H__
#define __MLX5_GENEVE_H__

#include <net/geneve.h>
#include <linux/mlx5/driver.h>

struct mlx5_geneve;

#ifdef CONFIG_MLX5_ESWITCH

struct mlx5_geneve *mlx5_geneve_create(struct mlx5_core_dev *mdev);
void mlx5_geneve_destroy(struct mlx5_geneve *geneve);

int mlx5_geneve_tlv_option_add(struct mlx5_geneve *geneve, struct geneve_opt *opt);
void mlx5_geneve_tlv_option_del(struct mlx5_geneve *geneve);

#else /* CONFIG_MLX5_ESWITCH */

static inline struct mlx5_geneve
*mlx5_geneve_create(struct mlx5_core_dev *mdev) { return NULL; }
static inline void
mlx5_geneve_destroy(struct mlx5_geneve *geneve) {}
static inline int
mlx5_geneve_tlv_option_add(struct mlx5_geneve *geneve, struct geneve_opt *opt) { return 0; }
static inline void
mlx5_geneve_tlv_option_del(struct mlx5_geneve *geneve) {}

#endif /* CONFIG_MLX5_ESWITCH */

#endif /* __MLX5_GENEVE_H__ */
