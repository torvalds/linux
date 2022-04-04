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
	atomic64_t tx_tls_del;
	atomic64_t rx_tls_ctx;
	atomic64_t rx_tls_del;
};

struct mlx5e_tls {
	struct mlx5e_tls_sw_stats sw_stats;
	struct workqueue_struct *rx_wq;
};

void mlx5e_tls_build_netdev(struct mlx5e_priv *priv);
int mlx5e_tls_init(struct mlx5e_priv *priv);
void mlx5e_tls_cleanup(struct mlx5e_priv *priv);

int mlx5e_tls_get_count(struct mlx5e_priv *priv);
int mlx5e_tls_get_strings(struct mlx5e_priv *priv, uint8_t *data);
int mlx5e_tls_get_stats(struct mlx5e_priv *priv, u64 *data);

static inline bool mlx5e_accel_is_tls_device(struct mlx5_core_dev *mdev)
{
	return !is_kdump_kernel() && mlx5_accel_is_ktls_device(mdev);
}

#else

static inline void mlx5e_tls_build_netdev(struct mlx5e_priv *priv)
{
	if (!is_kdump_kernel() &&
	    mlx5_accel_is_ktls_device(priv->mdev))
		mlx5e_ktls_build_netdev(priv);
}

static inline int mlx5e_tls_init(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_tls_cleanup(struct mlx5e_priv *priv) { }
static inline int mlx5e_tls_get_count(struct mlx5e_priv *priv) { return 0; }
static inline int mlx5e_tls_get_strings(struct mlx5e_priv *priv, uint8_t *data) { return 0; }
static inline int mlx5e_tls_get_stats(struct mlx5e_priv *priv, u64 *data) { return 0; }
static inline bool mlx5e_accel_is_tls_device(struct mlx5_core_dev *mdev) { return false; }

#endif

#endif /* __MLX5E_TLS_H__ */
