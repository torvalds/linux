/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_EN_RQT_H__
#define __MLX5_EN_RQT_H__

#include <linux/kernel.h>

#define MLX5E_INDIR_MIN_RQT_SIZE (BIT(8))

struct mlx5_core_dev;

struct mlx5e_rss_params_indir {
	u32 *table;
	u32 actual_table_size;
	u32 max_table_size;
};

void mlx5e_rss_params_indir_init_uniform(struct mlx5e_rss_params_indir *indir,
					 unsigned int num_channels);

struct mlx5e_rqt {
	struct mlx5_core_dev *mdev; /* primary */
	u32 rqtn;
	u16 size;
};

int mlx5e_rqt_init_direct(struct mlx5e_rqt *rqt, struct mlx5_core_dev *mdev,
			  bool indir_enabled, u32 init_rqn, u32 indir_table_size);
int mlx5e_rqt_init_indir(struct mlx5e_rqt *rqt, struct mlx5_core_dev *mdev,
			 u32 *rqns, u32 *vhca_ids, unsigned int num_rqns,
			 u8 hfunc, struct mlx5e_rss_params_indir *indir);
void mlx5e_rqt_destroy(struct mlx5e_rqt *rqt);

static inline u32 mlx5e_rqt_get_rqtn(struct mlx5e_rqt *rqt)
{
	return rqt->rqtn;
}

u32 mlx5e_rqt_size(struct mlx5_core_dev *mdev, unsigned int num_channels);
int mlx5e_rqt_redirect_direct(struct mlx5e_rqt *rqt, u32 rqn, u32 *vhca_id);
int mlx5e_rqt_redirect_indir(struct mlx5e_rqt *rqt, u32 *rqns, u32 *vhca_ids,
			     unsigned int num_rqns,
			     u8 hfunc, struct mlx5e_rss_params_indir *indir);

#endif /* __MLX5_EN_RQT_H__ */
