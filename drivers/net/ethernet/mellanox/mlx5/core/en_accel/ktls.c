// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include "en.h"
#include "en_accel/ktls.h"

u16 mlx5e_ktls_get_stop_room(struct mlx5e_txqsq *sq)
{
	u16 num_dumps, stop_room = 0;

	num_dumps = mlx5e_ktls_dumps_num_wqes(sq, MAX_SKB_FRAGS, TLS_MAX_PAYLOAD_SIZE);

	stop_room += mlx5e_stop_room_for_wqe(MLX5E_KTLS_STATIC_WQEBBS);
	stop_room += mlx5e_stop_room_for_wqe(MLX5E_KTLS_PROGRESS_WQEBBS);
	stop_room += num_dumps * mlx5e_stop_room_for_wqe(MLX5E_KTLS_DUMP_WQEBBS);

	return stop_room;
}

static int mlx5e_ktls_create_tis(struct mlx5_core_dev *mdev, u32 *tisn)
{
	u32 in[MLX5_ST_SZ_DW(create_tis_in)] = {};
	void *tisc;

	tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

	MLX5_SET(tisc, tisc, tls_en, 1);

	return mlx5e_create_tis(mdev, in, tisn);
}

static int mlx5e_ktls_add(struct net_device *netdev, struct sock *sk,
			  enum tls_offload_ctx_dir direction,
			  struct tls_crypto_info *crypto_info,
			  u32 start_offload_tcp_sn)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_ktls_offload_context_tx *tx_priv;
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (WARN_ON(direction != TLS_OFFLOAD_CTX_DIR_TX))
		return -EINVAL;

	if (WARN_ON(!mlx5e_ktls_type_check(mdev, crypto_info)))
		return -EOPNOTSUPP;

	tx_priv = kvzalloc(sizeof(*tx_priv), GFP_KERNEL);
	if (!tx_priv)
		return -ENOMEM;

	tx_priv->expected_seq = start_offload_tcp_sn;
	tx_priv->crypto_info  = *(struct tls12_crypto_info_aes_gcm_128 *)crypto_info;
	mlx5e_set_ktls_tx_priv_ctx(tls_ctx, tx_priv);

	/* tc and underlay_qpn values are not in use for tls tis */
	err = mlx5e_ktls_create_tis(mdev, &tx_priv->tisn);
	if (err)
		goto create_tis_fail;

	err = mlx5_ktls_create_key(mdev, crypto_info, &tx_priv->key_id);
	if (err)
		goto encryption_key_create_fail;

	mlx5e_ktls_tx_offload_set_pending(tx_priv);

	return 0;

encryption_key_create_fail:
	mlx5e_destroy_tis(priv->mdev, tx_priv->tisn);
create_tis_fail:
	kvfree(tx_priv);
	return err;
}

static void mlx5e_ktls_del(struct net_device *netdev,
			   struct tls_context *tls_ctx,
			   enum tls_offload_ctx_dir direction)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_ktls_offload_context_tx *tx_priv =
		mlx5e_get_ktls_tx_priv_ctx(tls_ctx);

	mlx5e_destroy_tis(priv->mdev, tx_priv->tisn);
	mlx5_ktls_destroy_key(priv->mdev, tx_priv->key_id);
	kvfree(tx_priv);
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
