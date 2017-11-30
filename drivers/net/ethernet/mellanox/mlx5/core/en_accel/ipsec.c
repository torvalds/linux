/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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

#include <crypto/internal/geniv.h>
#include <crypto/aead.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/module.h>

#include "en.h"
#include "accel/ipsec.h"
#include "en_accel/ipsec.h"
#include "en_accel/ipsec_rxtx.h"

struct mlx5e_ipsec_sa_entry {
	struct hlist_node hlist; /* Item in SADB_RX hashtable */
	unsigned int handle; /* Handle in SADB_RX */
	struct xfrm_state *x;
	struct mlx5e_ipsec *ipsec;
	void *context;
};

struct xfrm_state *mlx5e_ipsec_sadb_rx_lookup(struct mlx5e_ipsec *ipsec,
					      unsigned int handle)
{
	struct mlx5e_ipsec_sa_entry *sa_entry;
	struct xfrm_state *ret = NULL;

	rcu_read_lock();
	hash_for_each_possible_rcu(ipsec->sadb_rx, sa_entry, hlist, handle)
		if (sa_entry->handle == handle) {
			ret = sa_entry->x;
			xfrm_state_hold(ret);
			break;
		}
	rcu_read_unlock();

	return ret;
}

static int mlx5e_ipsec_sadb_rx_add(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ipsec->sadb_rx_lock, flags);
	ret = ida_simple_get(&ipsec->halloc, 1, 0, GFP_KERNEL);
	if (ret < 0)
		goto out;

	sa_entry->handle = ret;
	hash_add_rcu(ipsec->sadb_rx, &sa_entry->hlist, sa_entry->handle);
	ret = 0;

out:
	spin_unlock_irqrestore(&ipsec->sadb_rx_lock, flags);
	return ret;
}

static void mlx5e_ipsec_sadb_rx_del(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	unsigned long flags;

	spin_lock_irqsave(&ipsec->sadb_rx_lock, flags);
	hash_del_rcu(&sa_entry->hlist);
	spin_unlock_irqrestore(&ipsec->sadb_rx_lock, flags);
}

static void mlx5e_ipsec_sadb_rx_free(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	unsigned long flags;

	/* Wait for the hash_del_rcu call in sadb_rx_del to affect data path */
	synchronize_rcu();
	spin_lock_irqsave(&ipsec->sadb_rx_lock, flags);
	ida_simple_remove(&ipsec->halloc, sa_entry->handle);
	spin_unlock_irqrestore(&ipsec->sadb_rx_lock, flags);
}

static enum mlx5_accel_ipsec_enc_mode mlx5e_ipsec_enc_mode(struct xfrm_state *x)
{
	unsigned int key_len = (x->aead->alg_key_len + 7) / 8 - 4;

	switch (key_len) {
	case 16:
		return MLX5_IPSEC_SADB_MODE_AES_GCM_128_AUTH_128;
	case 32:
		return MLX5_IPSEC_SADB_MODE_AES_GCM_256_AUTH_128;
	default:
		netdev_warn(x->xso.dev, "Bad key len: %d for alg %s\n",
			    key_len, x->aead->alg_name);
		return -1;
	}
}

static void mlx5e_ipsec_build_hw_sa(u32 op, struct mlx5e_ipsec_sa_entry *sa_entry,
				    struct mlx5_accel_ipsec_sa *hw_sa)
{
	struct xfrm_state *x = sa_entry->x;
	struct aead_geniv_ctx *geniv_ctx;
	unsigned int crypto_data_len;
	struct crypto_aead *aead;
	unsigned int key_len;
	int ivsize;

	memset(hw_sa, 0, sizeof(*hw_sa));

	if (op == MLX5_IPSEC_CMD_ADD_SA) {
		crypto_data_len = (x->aead->alg_key_len + 7) / 8;
		key_len = crypto_data_len - 4; /* 4 bytes salt at end */
		aead = x->data;
		geniv_ctx = crypto_aead_ctx(aead);
		ivsize = crypto_aead_ivsize(aead);

		memcpy(&hw_sa->key_enc, x->aead->alg_key, key_len);
		/* Duplicate 128 bit key twice according to HW layout */
		if (key_len == 16)
			memcpy(&hw_sa->key_enc[16], x->aead->alg_key, key_len);
		memcpy(&hw_sa->gcm.salt_iv, geniv_ctx->salt, ivsize);
		hw_sa->gcm.salt = *((__be32 *)(x->aead->alg_key + key_len));
	}

	hw_sa->cmd = htonl(op);
	hw_sa->flags |= MLX5_IPSEC_SADB_SA_VALID | MLX5_IPSEC_SADB_SPI_EN;
	if (x->props.family == AF_INET) {
		hw_sa->sip[3] = x->props.saddr.a4;
		hw_sa->dip[3] = x->id.daddr.a4;
		hw_sa->sip_masklen = 32;
		hw_sa->dip_masklen = 32;
	} else {
		memcpy(hw_sa->sip, x->props.saddr.a6, sizeof(hw_sa->sip));
		memcpy(hw_sa->dip, x->id.daddr.a6, sizeof(hw_sa->dip));
		hw_sa->sip_masklen = 128;
		hw_sa->dip_masklen = 128;
		hw_sa->flags |= MLX5_IPSEC_SADB_IPV6;
	}
	hw_sa->spi = x->id.spi;
	hw_sa->sw_sa_handle = htonl(sa_entry->handle);
	switch (x->id.proto) {
	case IPPROTO_ESP:
		hw_sa->flags |= MLX5_IPSEC_SADB_IP_ESP;
		break;
	case IPPROTO_AH:
		hw_sa->flags |= MLX5_IPSEC_SADB_IP_AH;
		break;
	default:
		break;
	}
	hw_sa->enc_mode = mlx5e_ipsec_enc_mode(x);
	if (!(x->xso.flags & XFRM_OFFLOAD_INBOUND))
		hw_sa->flags |= MLX5_IPSEC_SADB_DIR_SX;
}

static inline int mlx5e_xfrm_validate_state(struct xfrm_state *x)
{
	struct net_device *netdev = x->xso.dev;
	struct mlx5e_priv *priv;

	priv = netdev_priv(netdev);

	if (x->props.aalgo != SADB_AALG_NONE) {
		netdev_info(netdev, "Cannot offload authenticated xfrm states\n");
		return -EINVAL;
	}
	if (x->props.ealgo != SADB_X_EALG_AES_GCM_ICV16) {
		netdev_info(netdev, "Only AES-GCM-ICV16 xfrm state may be offloaded\n");
		return -EINVAL;
	}
	if (x->props.calgo != SADB_X_CALG_NONE) {
		netdev_info(netdev, "Cannot offload compressed xfrm states\n");
		return -EINVAL;
	}
	if (x->props.flags & XFRM_STATE_ESN) {
		netdev_info(netdev, "Cannot offload ESN xfrm states\n");
		return -EINVAL;
	}
	if (x->props.family != AF_INET &&
	    x->props.family != AF_INET6) {
		netdev_info(netdev, "Only IPv4/6 xfrm states may be offloaded\n");
		return -EINVAL;
	}
	if (x->props.mode != XFRM_MODE_TRANSPORT &&
	    x->props.mode != XFRM_MODE_TUNNEL) {
		dev_info(&netdev->dev, "Only transport and tunnel xfrm states may be offloaded\n");
		return -EINVAL;
	}
	if (x->id.proto != IPPROTO_ESP) {
		netdev_info(netdev, "Only ESP xfrm state may be offloaded\n");
		return -EINVAL;
	}
	if (x->encap) {
		netdev_info(netdev, "Encapsulated xfrm state may not be offloaded\n");
		return -EINVAL;
	}
	if (!x->aead) {
		netdev_info(netdev, "Cannot offload xfrm states without aead\n");
		return -EINVAL;
	}
	if (x->aead->alg_icv_len != 128) {
		netdev_info(netdev, "Cannot offload xfrm states with AEAD ICV length other than 128bit\n");
		return -EINVAL;
	}
	if ((x->aead->alg_key_len != 128 + 32) &&
	    (x->aead->alg_key_len != 256 + 32)) {
		netdev_info(netdev, "Cannot offload xfrm states with AEAD key length other than 128/256 bit\n");
		return -EINVAL;
	}
	if (x->tfcpad) {
		netdev_info(netdev, "Cannot offload xfrm states with tfc padding\n");
		return -EINVAL;
	}
	if (!x->geniv) {
		netdev_info(netdev, "Cannot offload xfrm states without geniv\n");
		return -EINVAL;
	}
	if (strcmp(x->geniv, "seqiv")) {
		netdev_info(netdev, "Cannot offload xfrm states with geniv other than seqiv\n");
		return -EINVAL;
	}
	if (x->props.family == AF_INET6 &&
	    !(mlx5_accel_ipsec_device_caps(priv->mdev) & MLX5_ACCEL_IPSEC_IPV6)) {
		netdev_info(netdev, "IPv6 xfrm state offload is not supported by this device\n");
		return -EINVAL;
	}
	return 0;
}

static int mlx5e_xfrm_add_state(struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = NULL;
	struct net_device *netdev = x->xso.dev;
	struct mlx5_accel_ipsec_sa hw_sa;
	struct mlx5e_priv *priv;
	void *context;
	int err;

	priv = netdev_priv(netdev);

	err = mlx5e_xfrm_validate_state(x);
	if (err)
		return err;

	sa_entry = kzalloc(sizeof(*sa_entry), GFP_KERNEL);
	if (!sa_entry) {
		err = -ENOMEM;
		goto out;
	}

	sa_entry->x = x;
	sa_entry->ipsec = priv->ipsec;

	/* Add the SA to handle processed incoming packets before the add SA
	 * completion was received
	 */
	if (x->xso.flags & XFRM_OFFLOAD_INBOUND) {
		err = mlx5e_ipsec_sadb_rx_add(sa_entry);
		if (err) {
			netdev_info(netdev, "Failed adding to SADB_RX: %d\n", err);
			goto err_entry;
		}
	}

	mlx5e_ipsec_build_hw_sa(MLX5_IPSEC_CMD_ADD_SA, sa_entry, &hw_sa);
	context = mlx5_accel_ipsec_sa_cmd_exec(sa_entry->ipsec->en_priv->mdev, &hw_sa);
	if (IS_ERR(context)) {
		err = PTR_ERR(context);
		goto err_sadb_rx;
	}

	err = mlx5_accel_ipsec_sa_cmd_wait(context);
	if (err)
		goto err_sadb_rx;

	x->xso.offload_handle = (unsigned long)sa_entry;
	goto out;

err_sadb_rx:
	if (x->xso.flags & XFRM_OFFLOAD_INBOUND) {
		mlx5e_ipsec_sadb_rx_del(sa_entry);
		mlx5e_ipsec_sadb_rx_free(sa_entry);
	}
err_entry:
	kfree(sa_entry);
out:
	return err;
}

static void mlx5e_xfrm_del_state(struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry;
	struct mlx5_accel_ipsec_sa hw_sa;
	void *context;

	if (!x->xso.offload_handle)
		return;

	sa_entry = (struct mlx5e_ipsec_sa_entry *)x->xso.offload_handle;
	WARN_ON(sa_entry->x != x);

	if (x->xso.flags & XFRM_OFFLOAD_INBOUND)
		mlx5e_ipsec_sadb_rx_del(sa_entry);

	mlx5e_ipsec_build_hw_sa(MLX5_IPSEC_CMD_DEL_SA, sa_entry, &hw_sa);
	context = mlx5_accel_ipsec_sa_cmd_exec(sa_entry->ipsec->en_priv->mdev, &hw_sa);
	if (IS_ERR(context))
		return;

	sa_entry->context = context;
}

static void mlx5e_xfrm_free_state(struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry;
	int res;

	if (!x->xso.offload_handle)
		return;

	sa_entry = (struct mlx5e_ipsec_sa_entry *)x->xso.offload_handle;
	WARN_ON(sa_entry->x != x);

	res = mlx5_accel_ipsec_sa_cmd_wait(sa_entry->context);
	sa_entry->context = NULL;
	if (res) {
		/* Leftover object will leak */
		return;
	}

	if (x->xso.flags & XFRM_OFFLOAD_INBOUND)
		mlx5e_ipsec_sadb_rx_free(sa_entry);

	kfree(sa_entry);
}

int mlx5e_ipsec_init(struct mlx5e_priv *priv)
{
	struct mlx5e_ipsec *ipsec = NULL;

	if (!MLX5_IPSEC_DEV(priv->mdev)) {
		netdev_dbg(priv->netdev, "Not an IPSec offload device\n");
		return 0;
	}

	ipsec = kzalloc(sizeof(*ipsec), GFP_KERNEL);
	if (!ipsec)
		return -ENOMEM;

	hash_init(ipsec->sadb_rx);
	spin_lock_init(&ipsec->sadb_rx_lock);
	ida_init(&ipsec->halloc);
	ipsec->en_priv = priv;
	ipsec->en_priv->ipsec = ipsec;
	netdev_dbg(priv->netdev, "IPSec attached to netdevice\n");
	return 0;
}

void mlx5e_ipsec_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_ipsec *ipsec = priv->ipsec;

	if (!ipsec)
		return;

	ida_destroy(&ipsec->halloc);
	kfree(ipsec);
	priv->ipsec = NULL;
}

static bool mlx5e_ipsec_offload_ok(struct sk_buff *skb, struct xfrm_state *x)
{
	if (x->props.family == AF_INET) {
		/* Offload with IPv4 options is not supported yet */
		if (ip_hdr(skb)->ihl > 5)
			return false;
	} else {
		/* Offload with IPv6 extension headers is not support yet */
		if (ipv6_ext_hdr(ipv6_hdr(skb)->nexthdr))
			return false;
	}

	return true;
}

static const struct xfrmdev_ops mlx5e_ipsec_xfrmdev_ops = {
	.xdo_dev_state_add	= mlx5e_xfrm_add_state,
	.xdo_dev_state_delete	= mlx5e_xfrm_del_state,
	.xdo_dev_state_free	= mlx5e_xfrm_free_state,
	.xdo_dev_offload_ok	= mlx5e_ipsec_offload_ok,
};

void mlx5e_ipsec_build_netdev(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct net_device *netdev = priv->netdev;

	if (!priv->ipsec)
		return;

	if (!(mlx5_accel_ipsec_device_caps(mdev) & MLX5_ACCEL_IPSEC_ESP) ||
	    !MLX5_CAP_ETH(mdev, swp)) {
		mlx5_core_dbg(mdev, "mlx5e: ESP and SWP offload not supported\n");
		return;
	}

	mlx5_core_info(mdev, "mlx5e: IPSec ESP acceleration enabled\n");
	netdev->xfrmdev_ops = &mlx5e_ipsec_xfrmdev_ops;
	netdev->features |= NETIF_F_HW_ESP;
	netdev->hw_enc_features |= NETIF_F_HW_ESP;

	if (!MLX5_CAP_ETH(mdev, swp_csum)) {
		mlx5_core_dbg(mdev, "mlx5e: SWP checksum not supported\n");
		return;
	}

	netdev->features |= NETIF_F_HW_ESP_TX_CSUM;
	netdev->hw_enc_features |= NETIF_F_HW_ESP_TX_CSUM;

	if (!(mlx5_accel_ipsec_device_caps(mdev) & MLX5_ACCEL_IPSEC_LSO) ||
	    !MLX5_CAP_ETH(mdev, swp_lso)) {
		mlx5_core_dbg(mdev, "mlx5e: ESP LSO not supported\n");
		return;
	}

	mlx5_core_dbg(mdev, "mlx5e: ESP GSO capability turned on\n");
	netdev->features |= NETIF_F_GSO_ESP;
	netdev->hw_features |= NETIF_F_GSO_ESP;
	netdev->hw_enc_features |= NETIF_F_GSO_ESP;
}
