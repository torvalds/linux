/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_EN_TIR_H__
#define __MLX5_EN_TIR_H__

#include <linux/kernel.h>

struct mlx5e_rss_params_hash {
	u8 hfunc;
	u8 toeplitz_hash_key[40];
};

struct mlx5e_rss_params_traffic_type {
	u8 l3_prot_type;
	u8 l4_prot_type;
	u32 rx_hash_fields;
};

struct mlx5e_tir_builder;
struct mlx5e_lro_param;

struct mlx5e_tir_builder *mlx5e_tir_builder_alloc(bool modify);
void mlx5e_tir_builder_free(struct mlx5e_tir_builder *builder);
void mlx5e_tir_builder_clear(struct mlx5e_tir_builder *builder);

void mlx5e_tir_builder_build_inline(struct mlx5e_tir_builder *builder, u32 tdn, u32 rqn);
void mlx5e_tir_builder_build_rqt(struct mlx5e_tir_builder *builder, u32 tdn,
				 u32 rqtn, bool inner_ft_support);
void mlx5e_tir_builder_build_lro(struct mlx5e_tir_builder *builder,
				 const struct mlx5e_lro_param *lro_param);
void mlx5e_tir_builder_build_rss(struct mlx5e_tir_builder *builder,
				 const struct mlx5e_rss_params_hash *rss_hash,
				 const struct mlx5e_rss_params_traffic_type *rss_tt,
				 bool inner);
void mlx5e_tir_builder_build_direct(struct mlx5e_tir_builder *builder);
void mlx5e_tir_builder_build_tls(struct mlx5e_tir_builder *builder);

struct mlx5_core_dev;

struct mlx5e_tir {
	struct mlx5_core_dev *mdev;
	u32 tirn;
	struct list_head list;
};

int mlx5e_tir_init(struct mlx5e_tir *tir, struct mlx5e_tir_builder *builder,
		   struct mlx5_core_dev *mdev, bool reg);
void mlx5e_tir_destroy(struct mlx5e_tir *tir);

static inline u32 mlx5e_tir_get_tirn(struct mlx5e_tir *tir)
{
	return tir->tirn;
}

int mlx5e_tir_modify(struct mlx5e_tir *tir, struct mlx5e_tir_builder *builder);

#endif /* __MLX5_EN_TIR_H__ */
