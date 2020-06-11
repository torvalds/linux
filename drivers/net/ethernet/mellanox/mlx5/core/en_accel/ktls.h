/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5E_KTLS_H__
#define __MLX5E_KTLS_H__

#include "en.h"

#ifdef CONFIG_MLX5_EN_TLS
#include <net/tls.h>
#include "accel/tls.h"
#include "en_accel/tls_rxtx.h"

#define MLX5E_KTLS_STATIC_UMR_WQE_SZ \
	(offsetof(struct mlx5e_umr_wqe, tls_static_params_ctx) + \
	 MLX5_ST_SZ_BYTES(tls_static_params))
#define MLX5E_KTLS_STATIC_WQEBBS \
	(DIV_ROUND_UP(MLX5E_KTLS_STATIC_UMR_WQE_SZ, MLX5_SEND_WQE_BB))

#define MLX5E_KTLS_PROGRESS_WQE_SZ \
	(offsetof(struct mlx5e_tx_wqe, tls_progress_params_ctx) + \
	 MLX5_ST_SZ_BYTES(tls_progress_params))
#define MLX5E_KTLS_PROGRESS_WQEBBS \
	(DIV_ROUND_UP(MLX5E_KTLS_PROGRESS_WQE_SZ, MLX5_SEND_WQE_BB))

struct mlx5e_dump_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_wqe_data_seg data;
};

#define MLX5E_TLS_FETCH_UMR_WQE(sq, pi) \
	((struct mlx5e_umr_wqe *)mlx5e_fetch_wqe(&(sq)->wq, pi, MLX5E_KTLS_STATIC_UMR_WQE_SZ))
#define MLX5E_TLS_FETCH_PROGRESS_WQE(sq, pi) \
	((struct mlx5e_tx_wqe *)mlx5e_fetch_wqe(&(sq)->wq, pi, MLX5E_KTLS_PROGRESS_WQE_SZ))
#define MLX5E_TLS_FETCH_DUMP_WQE(sq, pi) \
	((struct mlx5e_dump_wqe *)mlx5e_fetch_wqe(&(sq)->wq, pi, \
						  sizeof(struct mlx5e_dump_wqe)))

#define MLX5E_KTLS_DUMP_WQEBBS \
	(DIV_ROUND_UP(sizeof(struct mlx5e_dump_wqe), MLX5_SEND_WQE_BB))

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

struct mlx5e_ktls_offload_context_tx {
	struct tls_offload_context_tx *tx_ctx;
	struct tls12_crypto_info_aes_gcm_128 crypto_info;
	u32 expected_seq;
	u32 tisn;
	u32 key_id;
	bool ctx_post_pending;
};

struct mlx5e_ktls_offload_context_tx_shadow {
	struct tls_offload_context_tx         tx_ctx;
	struct mlx5e_ktls_offload_context_tx *priv_tx;
};

static inline void
mlx5e_set_ktls_tx_priv_ctx(struct tls_context *tls_ctx,
			   struct mlx5e_ktls_offload_context_tx *priv_tx)
{
	struct tls_offload_context_tx *tx_ctx = tls_offload_ctx_tx(tls_ctx);
	struct mlx5e_ktls_offload_context_tx_shadow *shadow;

	BUILD_BUG_ON(sizeof(*shadow) > TLS_OFFLOAD_CONTEXT_SIZE_TX);

	shadow = (struct mlx5e_ktls_offload_context_tx_shadow *)tx_ctx;

	shadow->priv_tx = priv_tx;
	priv_tx->tx_ctx = tx_ctx;
}

static inline struct mlx5e_ktls_offload_context_tx *
mlx5e_get_ktls_tx_priv_ctx(struct tls_context *tls_ctx)
{
	struct tls_offload_context_tx *tx_ctx = tls_offload_ctx_tx(tls_ctx);
	struct mlx5e_ktls_offload_context_tx_shadow *shadow;

	BUILD_BUG_ON(sizeof(*shadow) > TLS_OFFLOAD_CONTEXT_SIZE_TX);

	shadow = (struct mlx5e_ktls_offload_context_tx_shadow *)tx_ctx;

	return shadow->priv_tx;
}

void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv);
void mlx5e_ktls_tx_offload_set_pending(struct mlx5e_ktls_offload_context_tx *priv_tx);

bool mlx5e_ktls_handle_tx_skb(struct tls_context *tls_ctx, struct mlx5e_txqsq *sq,
			      struct sk_buff *skb, int datalen,
			      struct mlx5e_accel_tx_tls_state *state);
void mlx5e_ktls_tx_handle_resync_dump_comp(struct mlx5e_txqsq *sq,
					   struct mlx5e_tx_wqe_info *wi,
					   u32 *dma_fifo_cc);
u16 mlx5e_ktls_get_stop_room(struct mlx5e_txqsq *sq);

static inline u8
mlx5e_ktls_dumps_num_wqes(struct mlx5e_txqsq *sq, unsigned int nfrags,
			  unsigned int sync_len)
{
	/* Given the MTU and sync_len, calculates an upper bound for the
	 * number of DUMP WQEs needed for the TX resync of a record.
	 */
	return nfrags + DIV_ROUND_UP(sync_len, sq->hw_mtu);
}
#else

static inline void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv)
{
}

static inline void
mlx5e_ktls_tx_handle_resync_dump_comp(struct mlx5e_txqsq *sq,
				      struct mlx5e_tx_wqe_info *wi,
				      u32 *dma_fifo_cc) {}
#endif

#endif /* __MLX5E_TLS_H__ */
