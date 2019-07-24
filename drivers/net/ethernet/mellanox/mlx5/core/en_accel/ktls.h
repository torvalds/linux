/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5E_KTLS_H__
#define __MLX5E_KTLS_H__

#include "en.h"

#ifdef CONFIG_MLX5_EN_TLS
#include <net/tls.h>
#include "accel/tls.h"

#define MLX5E_KTLS_STATIC_UMR_WQE_SZ \
	(sizeof(struct mlx5e_umr_wqe) + MLX5_ST_SZ_BYTES(tls_static_params))
#define MLX5E_KTLS_STATIC_WQEBBS \
	(DIV_ROUND_UP(MLX5E_KTLS_STATIC_UMR_WQE_SZ, MLX5_SEND_WQE_BB))

#define MLX5E_KTLS_PROGRESS_WQE_SZ \
	(sizeof(struct mlx5e_tx_wqe) + MLX5_ST_SZ_BYTES(tls_progress_params))
#define MLX5E_KTLS_PROGRESS_WQEBBS \
	(DIV_ROUND_UP(MLX5E_KTLS_PROGRESS_WQE_SZ, MLX5_SEND_WQE_BB))
#define MLX5E_KTLS_MAX_DUMP_WQEBBS 2

enum {
	MLX5E_TLS_PROGRESS_PARAMS_AUTH_STATE_NO_OFFLOAD     = 0,
	MLX5E_TLS_PROGRESS_PARAMS_AUTH_STATE_OFFLOAD        = 1,
	MLX5E_TLS_PROGRESS_PARAMS_AUTH_STATE_AUTHENTICATION = 2,
};

enum {
	MLX5E_TLS_PROGRESS_PARAMS_RECORD_TRACKER_STATE_START     = 0,
	MLX5E_TLS_PROGRESS_PARAMS_RECORD_TRACKER_STATE_SEARCHING = 1,
	MLX5E_TLS_PROGRESS_PARAMS_RECORD_TRACKER_STATE_TRACKING  = 2,
};

struct mlx5e_ktls_offload_context_tx {
	struct tls_offload_context_tx *tx_ctx;
	struct tls_crypto_info *crypto_info;
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

struct sk_buff *mlx5e_ktls_handle_tx_skb(struct net_device *netdev,
					 struct mlx5e_txqsq *sq,
					 struct sk_buff *skb,
					 struct mlx5e_tx_wqe **wqe, u16 *pi);
void mlx5e_ktls_tx_handle_resync_dump_comp(struct mlx5e_txqsq *sq,
					   struct mlx5e_tx_wqe_info *wi,
					   struct mlx5e_sq_dma *dma);

#else

static inline void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv)
{
}

#endif

#endif /* __MLX5E_TLS_H__ */
