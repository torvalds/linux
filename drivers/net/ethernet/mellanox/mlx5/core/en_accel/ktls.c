// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include "en.h"
#include "en_accel/ktls.h"
#include "en_accel/ktls_utils.h"

static int mlx5e_ktls_add(struct net_device *netdev, struct sock *sk,
			  enum tls_offload_ctx_dir direction,
			  struct tls_crypto_info *crypto_info,
			  u32 start_offload_tcp_sn)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (WARN_ON(direction != TLS_OFFLOAD_CTX_DIR_TX))
		return -EINVAL;

	if (WARN_ON(!mlx5e_ktls_type_check(mdev, crypto_info)))
		return -EOPNOTSUPP;

	err = mlx5e_ktls_add_tx(netdev, sk, crypto_info, start_offload_tcp_sn);

	return err;
}

static void mlx5e_ktls_del(struct net_device *netdev,
			   struct tls_context *tls_ctx,
			   enum tls_offload_ctx_dir direction)
{
	if (direction != TLS_OFFLOAD_CTX_DIR_TX)
		return;

	mlx5e_ktls_del_tx(netdev, tls_ctx);
}

static const struct tlsdev_ops mlx5e_ktls_ops = {
	.tls_dev_add = mlx5e_ktls_add,
	.tls_dev_del = mlx5e_ktls_del,
};

void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;

	if (!mlx5_accel_is_ktls_device(priv->mdev))
		return;

	netdev->hw_features |= NETIF_F_HW_TLS_TX;
	netdev->features    |= NETIF_F_HW_TLS_TX;

	netdev->tlsdev_ops = &mlx5e_ktls_ops;
}
