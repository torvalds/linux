/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5E_KTLS_H__
#define __MLX5E_KTLS_H__

#include <linux/debugfs.h>
#include <linux/tls.h>
#include <net/tls.h>
#include "en.h"

#ifdef CONFIG_MLX5_EN_TLS
#include "lib/crypto.h"
#include "lib/mlx5.h"

struct mlx5_crypto_dek *mlx5_ktls_create_key(struct mlx5_crypto_dek_pool *dek_pool,
					     struct tls_crypto_info *crypto_info);
void mlx5_ktls_destroy_key(struct mlx5_crypto_dek_pool *dek_pool,
			   struct mlx5_crypto_dek *dek);

static inline bool mlx5e_is_ktls_device(struct mlx5_core_dev *mdev)
{
	if (is_kdump_kernel())
		return false;

	if (!MLX5_CAP_GEN(mdev, tls_tx) && !MLX5_CAP_GEN(mdev, tls_rx))
		return false;

	if (!MLX5_CAP_GEN(mdev, log_max_dek))
		return false;

	return (MLX5_CAP_TLS(mdev, tls_1_2_aes_gcm_128) ||
		MLX5_CAP_TLS(mdev, tls_1_2_aes_gcm_256));
}

static inline bool mlx5e_ktls_type_check(struct mlx5_core_dev *mdev,
					 struct tls_crypto_info *crypto_info)
{
	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128:
		if (crypto_info->version == TLS_1_2_VERSION)
			return MLX5_CAP_TLS(mdev,  tls_1_2_aes_gcm_128);
		break;
	case TLS_CIPHER_AES_GCM_256:
		if (crypto_info->version == TLS_1_2_VERSION)
			return MLX5_CAP_TLS(mdev,  tls_1_2_aes_gcm_256);
		break;
	}

	return false;
}

void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv);
int mlx5e_ktls_init_tx(struct mlx5e_priv *priv);
void mlx5e_ktls_cleanup_tx(struct mlx5e_priv *priv);
int mlx5e_ktls_init_rx(struct mlx5e_priv *priv);
void mlx5e_ktls_cleanup_rx(struct mlx5e_priv *priv);
int mlx5e_ktls_set_feature_rx(struct net_device *netdev, bool enable);
struct mlx5e_ktls_resync_resp *
mlx5e_ktls_rx_resync_create_resp_list(void);
void mlx5e_ktls_rx_resync_destroy_resp_list(struct mlx5e_ktls_resync_resp *resp_list);

static inline bool mlx5e_is_ktls_tx(struct mlx5_core_dev *mdev)
{
	return !is_kdump_kernel() && MLX5_CAP_GEN(mdev, tls_tx) &&
		!mlx5_get_sd(mdev);
}

bool mlx5e_is_ktls_rx(struct mlx5_core_dev *mdev);

struct mlx5e_tls_sw_stats {
	atomic64_t tx_tls_ctx;
	atomic64_t tx_tls_del;
	atomic64_t tx_tls_pool_alloc;
	atomic64_t tx_tls_pool_free;
	atomic64_t rx_tls_ctx;
	atomic64_t rx_tls_del;
};

struct mlx5e_tls_debugfs {
	struct dentry *dfs;
	struct dentry *dfs_tx;
};

struct mlx5e_tls {
	struct mlx5_core_dev *mdev;
	struct mlx5e_tls_sw_stats sw_stats;
	struct workqueue_struct *rx_wq;
	struct mlx5e_tls_tx_pool *tx_pool;
	struct mlx5_crypto_dek_pool *dek_pool;
	struct mlx5e_tls_debugfs debugfs;
};

int mlx5e_ktls_init(struct mlx5e_priv *priv);
void mlx5e_ktls_cleanup(struct mlx5e_priv *priv);

int mlx5e_ktls_get_count(struct mlx5e_priv *priv);
int mlx5e_ktls_get_strings(struct mlx5e_priv *priv, uint8_t *data);
int mlx5e_ktls_get_stats(struct mlx5e_priv *priv, u64 *data);

#else
static inline void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv)
{
}

static inline int mlx5e_ktls_init_tx(struct mlx5e_priv *priv)
{
	return 0;
}

static inline void mlx5e_ktls_cleanup_tx(struct mlx5e_priv *priv)
{
}

static inline int mlx5e_ktls_init_rx(struct mlx5e_priv *priv)
{
	return 0;
}

static inline void mlx5e_ktls_cleanup_rx(struct mlx5e_priv *priv)
{
}

static inline int mlx5e_ktls_set_feature_rx(struct net_device *netdev, bool enable)
{
	netdev_warn(netdev, "kTLS is not supported\n");
	return -EOPNOTSUPP;
}

static inline struct mlx5e_ktls_resync_resp *
mlx5e_ktls_rx_resync_create_resp_list(void)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void
mlx5e_ktls_rx_resync_destroy_resp_list(struct mlx5e_ktls_resync_resp *resp_list) {}

static inline bool mlx5e_is_ktls_rx(struct mlx5_core_dev *mdev)
{
	return false;
}

static inline int mlx5e_ktls_init(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_ktls_cleanup(struct mlx5e_priv *priv) { }
static inline int mlx5e_ktls_get_count(struct mlx5e_priv *priv) { return 0; }
static inline int mlx5e_ktls_get_strings(struct mlx5e_priv *priv, uint8_t *data)
{
	return 0;
}

static inline int mlx5e_ktls_get_stats(struct mlx5e_priv *priv, u64 *data)
{
	return 0;
}
#endif

#endif /* __MLX5E_TLS_H__ */
