/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#ifndef __MLX5E_TLS_H__
#define __MLX5E_TLS_H__

#include "accel/tls.h"
#include "en_accel/ktls.h"

#ifdef CONFIG_MLX5_EN_TLS
#include <net/tls.h>
#include "en.h"

struct mlx5e_tls_sw_stats {
	atomic64_t tx_tls_ctx;
	atomic64_t tx_tls_drop_metadata;
	atomic64_t tx_tls_drop_resync_alloc;
	atomic64_t tx_tls_drop_no_sync_data;
	atomic64_t tx_tls_drop_bypass_required;
	atomic64_t rx_tls_ctx;
	atomic64_t rx_tls_del;
	atomic64_t rx_tls_drop_resync_request;
	atomic64_t rx_tls_resync_request;
	atomic64_t rx_tls_resync_reply;
	atomic64_t rx_tls_auth_fail;
};

struct mlx5e_tls {
	struct mlx5e_tls_sw_stats sw_stats;
	struct workqueue_struct *rx_wq;
};

struct mlx5e_tls_offload_context_tx {
	struct tls_offload_context_tx base;
	u32 expected_seq;
	__be32 swid;
};

static inline struct mlx5e_tls_offload_context_tx *
mlx5e_get_tls_tx_context(struct tls_context *tls_ctx)
{
	BUILD_BUG_ON(sizeof(struct mlx5e_tls_offload_context_tx) >
		     TLS_OFFLOAD_CONTEXT_SIZE_TX);
	return container_of(tls_offload_ctx_tx(tls_ctx),
			    struct mlx5e_tls_offload_context_tx,
			    base);
}

struct mlx5e_tls_offload_context_rx {
	struct tls_offload_context_rx base;
	__be32 handle;
};

static inline struct mlx5e_tls_offload_context_rx *
mlx5e_get_tls_rx_context(struct tls_context *tls_ctx)
{
	BUILD_BUG_ON(sizeof(struct mlx5e_tls_offload_context_rx) >
		     TLS_OFFLOAD_CONTEXT_SIZE_RX);
	return container_of(tls_offload_ctx_rx(tls_ctx),
			    struct mlx5e_tls_offload_context_rx,
			    base);
}

static inline bool mlx5e_is_tls_on(struct mlx5e_priv *priv)
{
	return priv->tls;
}

void mlx5e_tls_build_netdev(struct mlx5e_priv *priv);
int mlx5e_tls_init(struct mlx5e_priv *priv);
void mlx5e_tls_cleanup(struct mlx5e_priv *priv);

int mlx5e_tls_get_count(struct mlx5e_priv *priv);
int mlx5e_tls_get_strings(struct mlx5e_priv *priv, uint8_t *data);
int mlx5e_tls_get_stats(struct mlx5e_priv *priv, u64 *data);

static inline bool mlx5e_accel_is_tls_device(struct mlx5_core_dev *mdev)
{
	return !is_kdump_kernel() &&
		mlx5_accel_is_tls_device(mdev);
}

#else

static inline void mlx5e_tls_build_netdev(struct mlx5e_priv *priv)
{
	if (!is_kdump_kernel() &&
	    mlx5_accel_is_ktls_device(priv->mdev))
		mlx5e_ktls_build_netdev(priv);
}

static inline bool mlx5e_is_tls_on(struct mlx5e_priv *priv) { return false; }
static inline int mlx5e_tls_init(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_tls_cleanup(struct mlx5e_priv *priv) { }
static inline int mlx5e_tls_get_count(struct mlx5e_priv *priv) { return 0; }
static inline int mlx5e_tls_get_strings(struct mlx5e_priv *priv, uint8_t *data) { return 0; }
static inline int mlx5e_tls_get_stats(struct mlx5e_priv *priv, u64 *data) { return 0; }
static inline bool mlx5e_accel_is_tls_device(struct mlx5_core_dev *mdev) { return false; }

#endif

#endif /* __MLX5E_TLS_H__ */
