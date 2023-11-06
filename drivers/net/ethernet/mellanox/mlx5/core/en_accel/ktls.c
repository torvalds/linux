// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include <linux/debugfs.h>
#include "en.h"
#include "lib/mlx5.h"
#include "lib/crypto.h"
#include "en_accel/ktls.h"
#include "en_accel/ktls_utils.h"
#include "en_accel/fs_tcp.h"

struct mlx5_crypto_dek *mlx5_ktls_create_key(struct mlx5_crypto_dek_pool *dek_pool,
					     struct tls_crypto_info *crypto_info)
{
	const void *key;
	u32 sz_bytes;

	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128: {
		struct tls12_crypto_info_aes_gcm_128 *info =
			(struct tls12_crypto_info_aes_gcm_128 *)crypto_info;

		key      = info->key;
		sz_bytes = sizeof(info->key);
		break;
	}
	case TLS_CIPHER_AES_GCM_256: {
		struct tls12_crypto_info_aes_gcm_256 *info =
			(struct tls12_crypto_info_aes_gcm_256 *)crypto_info;

		key      = info->key;
		sz_bytes = sizeof(info->key);
		break;
	}
	default:
		return ERR_PTR(-EINVAL);
	}

	return mlx5_crypto_dek_create(dek_pool, key, sz_bytes);
}

void mlx5_ktls_destroy_key(struct mlx5_crypto_dek_pool *dek_pool,
			   struct mlx5_crypto_dek *dek)
{
	mlx5_crypto_dek_destroy(dek_pool, dek);
}

static int mlx5e_ktls_add(struct net_device *netdev, struct sock *sk,
			  enum tls_offload_ctx_dir direction,
			  struct tls_crypto_info *crypto_info,
			  u32 start_offload_tcp_sn)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (!mlx5e_ktls_type_check(mdev, crypto_info))
		return -EOPNOTSUPP;

	if (direction == TLS_OFFLOAD_CTX_DIR_TX)
		err = mlx5e_ktls_add_tx(netdev, sk, crypto_info, start_offload_tcp_sn);
	else
		err = mlx5e_ktls_add_rx(netdev, sk, crypto_info, start_offload_tcp_sn);

	return err;
}

static void mlx5e_ktls_del(struct net_device *netdev,
			   struct tls_context *tls_ctx,
			   enum tls_offload_ctx_dir direction)
{
	if (direction == TLS_OFFLOAD_CTX_DIR_TX)
		mlx5e_ktls_del_tx(netdev, tls_ctx);
	else
		mlx5e_ktls_del_rx(netdev, tls_ctx);
}

static int mlx5e_ktls_resync(struct net_device *netdev,
			     struct sock *sk, u32 seq, u8 *rcd_sn,
			     enum tls_offload_ctx_dir direction)
{
	if (unlikely(direction != TLS_OFFLOAD_CTX_DIR_RX))
		return -EOPNOTSUPP;

	mlx5e_ktls_rx_resync(netdev, sk, seq, rcd_sn);
	return 0;
}

static const struct tlsdev_ops mlx5e_ktls_ops = {
	.tls_dev_add = mlx5e_ktls_add,
	.tls_dev_del = mlx5e_ktls_del,
	.tls_dev_resync = mlx5e_ktls_resync,
};

bool mlx5e_is_ktls_rx(struct mlx5_core_dev *mdev)
{
	u8 max_sq_wqebbs = mlx5e_get_max_sq_wqebbs(mdev);

	if (is_kdump_kernel() || !MLX5_CAP_GEN(mdev, tls_rx))
		return false;

	/* Check the possibility to post the required ICOSQ WQEs. */
	if (WARN_ON_ONCE(max_sq_wqebbs < MLX5E_TLS_SET_STATIC_PARAMS_WQEBBS))
		return false;
	if (WARN_ON_ONCE(max_sq_wqebbs < MLX5E_TLS_SET_PROGRESS_PARAMS_WQEBBS))
		return false;
	if (WARN_ON_ONCE(max_sq_wqebbs < MLX5E_KTLS_GET_PROGRESS_WQEBBS))
		return false;

	return true;
}

void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!mlx5e_is_ktls_tx(mdev) && !mlx5e_is_ktls_rx(mdev))
		return;

	if (mlx5e_is_ktls_tx(mdev)) {
		netdev->hw_features |= NETIF_F_HW_TLS_TX;
		netdev->features    |= NETIF_F_HW_TLS_TX;
	}

	if (mlx5e_is_ktls_rx(mdev))
		netdev->hw_features |= NETIF_F_HW_TLS_RX;

	netdev->tlsdev_ops = &mlx5e_ktls_ops;
}

int mlx5e_ktls_set_feature_rx(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err = 0;

	mutex_lock(&priv->state_lock);
	if (enable)
		err = mlx5e_accel_fs_tcp_create(priv->fs);
	else
		mlx5e_accel_fs_tcp_destroy(priv->fs);
	mutex_unlock(&priv->state_lock);

	return err;
}

int mlx5e_ktls_init_rx(struct mlx5e_priv *priv)
{
	int err;

	if (!mlx5e_is_ktls_rx(priv->mdev))
		return 0;

	priv->tls->rx_wq = create_singlethread_workqueue("mlx5e_tls_rx");
	if (!priv->tls->rx_wq)
		return -ENOMEM;

	if (priv->netdev->features & NETIF_F_HW_TLS_RX) {
		err = mlx5e_accel_fs_tcp_create(priv->fs);
		if (err) {
			destroy_workqueue(priv->tls->rx_wq);
			return err;
		}
	}

	return 0;
}

void mlx5e_ktls_cleanup_rx(struct mlx5e_priv *priv)
{
	if (!mlx5e_is_ktls_rx(priv->mdev))
		return;

	if (priv->netdev->features & NETIF_F_HW_TLS_RX)
		mlx5e_accel_fs_tcp_destroy(priv->fs);

	destroy_workqueue(priv->tls->rx_wq);
}

static void mlx5e_tls_debugfs_init(struct mlx5e_tls *tls,
				   struct dentry *dfs_root)
{
	if (IS_ERR_OR_NULL(dfs_root))
		return;

	tls->debugfs.dfs = debugfs_create_dir("tls", dfs_root);
}

int mlx5e_ktls_init(struct mlx5e_priv *priv)
{
	struct mlx5e_tls *tls;

	if (!mlx5e_is_ktls_device(priv->mdev))
		return 0;

	tls = kzalloc(sizeof(*tls), GFP_KERNEL);
	if (!tls)
		return -ENOMEM;
	tls->mdev = priv->mdev;

	priv->tls = tls;

	mlx5e_tls_debugfs_init(tls, priv->dfs_root);

	return 0;
}

void mlx5e_ktls_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_tls *tls = priv->tls;

	if (!mlx5e_is_ktls_device(priv->mdev))
		return;

	debugfs_remove_recursive(tls->debugfs.dfs);
	tls->debugfs.dfs = NULL;

	kfree(priv->tls);
	priv->tls = NULL;
}
