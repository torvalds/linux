/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_EN_RX_RES_H__
#define __MLX5_EN_RX_RES_H__

#include <linux/kernel.h>
#include "rqt.h"
#include "fs.h"

#define MLX5E_MAX_NUM_CHANNELS (MLX5E_INDIR_RQT_SIZE / 2)

struct mlx5e_rss_params {
	struct mlx5e_rss_params_indir indir;
	u32 rx_hash_fields[MLX5E_NUM_INDIR_TIRS];
	u8 toeplitz_hash_key[40];
	u8 hfunc;
};

struct mlx5e_tir {
	u32 tirn;
	struct mlx5e_rqt rqt;
	struct list_head list;
};

struct mlx5e_rx_res {
	struct mlx5e_rqt indir_rqt;
	struct mlx5e_tir indir_tirs[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_tir inner_indir_tirs[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_tir direct_tirs[MLX5E_MAX_NUM_CHANNELS];
	struct mlx5e_tir xsk_tirs[MLX5E_MAX_NUM_CHANNELS];
	struct mlx5e_tir ptp_tir;
	struct mlx5e_rss_params rss_params;
};

#endif /* __MLX5_EN_RX_RES_H__ */
