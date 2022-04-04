// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include "en.h"
#include "lib/mlx5.h"
#include "en_accel/ktls.h"
#include "en_accel/ktls_utils.h"
#include "en_accel/fs_tcp.h"

int mlx5_ktls_create_key(struct mlx5_core_dev *mdev,
			 struct tls_crypto_info *crypto_info,
			 u32 *p_key_id)
{
	u32 sz_bytes;
	void *key;

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
		return -EINVAL;
	}

	return mlx5_create_encryption_key(mdev, key, sz_bytes,
					  MLX5_ACCEL_OBJ_TLS_KEY,
					  p_key_id);
}

void mlx5_ktls_destroy_key(struct mlx5_core_dev *mdev, u32 key_id)
{
	mlx5_destroy_encryption_key(mdev, key_id);
}

static int mlx5e_ktls_add(struct net_device *netdev, struct sock *sk,
			  enum tls_offload_ctx_dir direction,
			  struct tls_crypto_info *crypto_info,
			  u32 start_offload_tcp_sn)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (WARN_ON(!mlx5e_ktls_type_check(mdev, crypto_info)))
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
		err = mlx5e_accel_fs_tcp_create(priv);
	else
		mlx5e_accel_fs_tcp_destroy(priv);
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
		err = mlx5e_accel_fs_tcp_create(priv);
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
		mlx5e_accel_fs_tcp_destroy(priv);

	destroy_workqueue(priv->tls->rx_wq);
}

int mlx5e_ktls_init(struct mlx5e_priv *priv)
{
	struct mlx5e_tls *tls;

	if (!mlx5e_is_ktls_device(priv->mdev))
		return 0;

	tls = kzalloc(sizeof(*tls), GFP_KERNEL);
	if (!tls)
		return -ENOMEM;

	priv->tls = tls;
	return 0;
}

void mlx5e_ktls_cleanup(struct mlx5e_priv *priv)
{
	kfree(priv->tls);
	priv->tls = NULL;
}
