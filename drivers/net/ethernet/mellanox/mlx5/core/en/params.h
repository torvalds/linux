/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_PARAMS_H__
#define __MLX5_EN_PARAMS_H__

#include "en.h"

struct mlx5e_xsk_param {
	u16 headroom;
	u16 chunk_size;
};

struct mlx5e_rq_param {
	u32                        rqc[MLX5_ST_SZ_DW(rqc)];
	struct mlx5_wq_param       wq;
	struct mlx5e_rq_frags_info frags_info;
};

struct mlx5e_sq_param {
	u32                        sqc[MLX5_ST_SZ_DW(sqc)];
	struct mlx5_wq_param       wq;
	bool                       is_mpw;
};

struct mlx5e_cq_param {
	u32                        cqc[MLX5_ST_SZ_DW(cqc)];
	struct mlx5_wq_param       wq;
	u16                        eq_ix;
	u8                         cq_period_mode;
};

struct mlx5e_channel_param {
	struct mlx5e_rq_param      rq;
	struct mlx5e_sq_param      sq;
	struct mlx5e_sq_param      xdp_sq;
	struct mlx5e_sq_param      icosq;
	struct mlx5e_cq_param      rx_cq;
	struct mlx5e_cq_param      tx_cq;
	struct mlx5e_cq_param      icosq_cq;
};

/* Parameter calculations */

u16 mlx5e_get_linear_rq_headroom(struct mlx5e_params *params,
				 struct mlx5e_xsk_param *xsk);
u32 mlx5e_rx_get_linear_frag_sz(struct mlx5e_params *params,
				struct mlx5e_xsk_param *xsk);
u8 mlx5e_mpwqe_log_pkts_per_wqe(struct mlx5e_params *params);
bool mlx5e_rx_is_linear_skb(struct mlx5e_params *params);
bool mlx5e_rx_mpwqe_is_linear_skb(struct mlx5_core_dev *mdev,
				  struct mlx5e_params *params);
u8 mlx5e_mpwqe_get_log_rq_size(struct mlx5e_params *params);
u8 mlx5e_mpwqe_get_log_stride_size(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params);
u8 mlx5e_mpwqe_get_log_num_strides(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params);
u16 mlx5e_get_rq_headroom(struct mlx5_core_dev *mdev,
			  struct mlx5e_params *params);

#endif /* __MLX5_EN_PARAMS_H__ */
