/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5E_KTLS_UTILS_H__
#define __MLX5E_KTLS_UTILS_H__

#include <net/tls.h>
#include "en.h"

enum {
	MLX5E_TLS_PROGRESS_PARAMS_AUTH_STATE_NO_OFFLOAD     = 0,
	MLX5E_TLS_PROGRESS_PARAMS_AUTH_STATE_OFFLOAD        = 1,
	MLX5E_TLS_PROGRESS_PARAMS_AUTH_STATE_AUTHENTICATION = 2,
};

enum {
	MLX5E_TLS_PROGRESS_PARAMS_RECORD_TRACKER_STATE_START     = 0,
	MLX5E_TLS_PROGRESS_PARAMS_RECORD_TRACKER_STATE_TRACKING  = 1,
	MLX5E_TLS_PROGRESS_PARAMS_RECORD_TRACKER_STATE_SEARCHING = 2,
};

int mlx5e_ktls_add_tx(struct net_device *netdev, struct sock *sk,
		      struct tls_crypto_info *crypto_info, u32 start_offload_tcp_sn);
void mlx5e_ktls_del_tx(struct net_device *netdev, struct tls_context *tls_ctx);
int mlx5e_ktls_add_rx(struct net_device *netdev, struct sock *sk,
		      struct tls_crypto_info *crypto_info, u32 start_offload_tcp_sn);
void mlx5e_ktls_del_rx(struct net_device *netdev, struct tls_context *tls_ctx);
void mlx5e_ktls_rx_resync(struct net_device *netdev, struct sock *sk, u32 seq, u8 *rcd_sn);

struct mlx5e_set_tls_static_params_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_wqe_umr_ctrl_seg uctrl;
	struct mlx5_mkey_seg mkc;
	struct mlx5_wqe_tls_static_params_seg params;
};

struct mlx5e_set_tls_progress_params_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_wqe_tls_progress_params_seg params;
};

struct mlx5e_get_tls_progress_params_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_seg_get_psv  psv;
};

#define MLX5E_TLS_SET_STATIC_PARAMS_WQEBBS \
	(DIV_ROUND_UP(sizeof(struct mlx5e_set_tls_static_params_wqe), MLX5_SEND_WQE_BB))

#define MLX5E_TLS_SET_PROGRESS_PARAMS_WQEBBS \
	(DIV_ROUND_UP(sizeof(struct mlx5e_set_tls_progress_params_wqe), MLX5_SEND_WQE_BB))

#define MLX5E_KTLS_GET_PROGRESS_WQEBBS \
	(DIV_ROUND_UP(sizeof(struct mlx5e_get_tls_progress_params_wqe), MLX5_SEND_WQE_BB))

#define MLX5E_TLS_FETCH_SET_STATIC_PARAMS_WQE(sq, pi) \
	((struct mlx5e_set_tls_static_params_wqe *)\
	 mlx5e_fetch_wqe(&(sq)->wq, pi, sizeof(struct mlx5e_set_tls_static_params_wqe)))

#define MLX5E_TLS_FETCH_SET_PROGRESS_PARAMS_WQE(sq, pi) \
	((struct mlx5e_set_tls_progress_params_wqe *)\
	 mlx5e_fetch_wqe(&(sq)->wq, pi, sizeof(struct mlx5e_set_tls_progress_params_wqe)))

#define MLX5E_TLS_FETCH_GET_PROGRESS_PARAMS_WQE(sq, pi) \
	((struct mlx5e_get_tls_progress_params_wqe *)\
	 mlx5e_fetch_wqe(&(sq)->wq, pi, sizeof(struct mlx5e_get_tls_progress_params_wqe)))

#define MLX5E_TLS_FETCH_DUMP_WQE(sq, pi) \
	((struct mlx5e_dump_wqe *)\
	 mlx5e_fetch_wqe(&(sq)->wq, pi, sizeof(struct mlx5e_dump_wqe)))

void
mlx5e_ktls_build_static_params(struct mlx5e_set_tls_static_params_wqe *wqe,
			       u16 pc, u32 sqn,
			       struct tls12_crypto_info_aes_gcm_128 *info,
			       u32 tis_tir_num, u32 key_id, u32 resync_tcp_sn,
			       bool fence, enum tls_offload_ctx_dir direction);
void
mlx5e_ktls_build_progress_params(struct mlx5e_set_tls_progress_params_wqe *wqe,
				 u16 pc, u32 sqn,
				 u32 tis_tir_num, bool fence,
				 u32 next_record_tcp_sn,
				 enum tls_offload_ctx_dir direction);

#endif /* __MLX5E_TLS_UTILS_H__ */
