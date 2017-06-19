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

#include <crypto/aead.h>
#include <net/xfrm.h>

#include "en_accel/ipsec_rxtx.h"
#include "en_accel/ipsec.h"
#include "en.h"

enum {
	MLX5E_IPSEC_RX_SYNDROME_DECRYPTED = 0x11,
	MLX5E_IPSEC_RX_SYNDROME_AUTH_FAILED = 0x12,
};

struct mlx5e_ipsec_rx_metadata {
	unsigned char   reserved;
	__be32		sa_handle;
} __packed;

struct mlx5e_ipsec_metadata {
	unsigned char syndrome;
	union {
		unsigned char raw[5];
		/* from FPGA to host, on successful decrypt */
		struct mlx5e_ipsec_rx_metadata rx;
	} __packed content;
	/* packet type ID field	*/
	__be16 ethertype;
} __packed;

static inline struct xfrm_state *
mlx5e_ipsec_build_sp(struct net_device *netdev, struct sk_buff *skb,
		     struct mlx5e_ipsec_metadata *mdata)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct xfrm_offload *xo;
	struct xfrm_state *xs;
	u32 sa_handle;

	skb->sp = secpath_dup(skb->sp);
	if (unlikely(!skb->sp)) {
		atomic64_inc(&priv->ipsec->sw_stats.ipsec_rx_drop_sp_alloc);
		return NULL;
	}

	sa_handle = be32_to_cpu(mdata->content.rx.sa_handle);
	xs = mlx5e_ipsec_sadb_rx_lookup(priv->ipsec, sa_handle);
	if (unlikely(!xs)) {
		atomic64_inc(&priv->ipsec->sw_stats.ipsec_rx_drop_sadb_miss);
		return NULL;
	}

	skb->sp->xvec[skb->sp->len++] = xs;
	skb->sp->olen++;

	xo = xfrm_offload(skb);
	xo->flags = CRYPTO_DONE;
	switch (mdata->syndrome) {
	case MLX5E_IPSEC_RX_SYNDROME_DECRYPTED:
		xo->status = CRYPTO_SUCCESS;
		break;
	case MLX5E_IPSEC_RX_SYNDROME_AUTH_FAILED:
		xo->status = CRYPTO_TUNNEL_ESP_AUTH_FAILED;
		break;
	default:
		atomic64_inc(&priv->ipsec->sw_stats.ipsec_rx_drop_syndrome);
		return NULL;
	}
	return xs;
}

struct sk_buff *mlx5e_ipsec_handle_rx_skb(struct net_device *netdev,
					  struct sk_buff *skb)
{
	struct mlx5e_ipsec_metadata *mdata;
	struct ethhdr *old_eth;
	struct ethhdr *new_eth;
	struct xfrm_state *xs;
	__be16 *ethtype;

	/* Detect inline metadata */
	if (skb->len < ETH_HLEN + MLX5E_METADATA_ETHER_LEN)
		return skb;
	ethtype = (__be16 *)(skb->data + ETH_ALEN * 2);
	if (*ethtype != cpu_to_be16(MLX5E_METADATA_ETHER_TYPE))
		return skb;

	/* Use the metadata */
	mdata = (struct mlx5e_ipsec_metadata *)(skb->data + ETH_HLEN);
	xs = mlx5e_ipsec_build_sp(netdev, skb, mdata);
	if (unlikely(!xs)) {
		kfree_skb(skb);
		return NULL;
	}

	/* Remove the metadata from the buffer */
	old_eth = (struct ethhdr *)skb->data;
	new_eth = (struct ethhdr *)(skb->data + MLX5E_METADATA_ETHER_LEN);
	memmove(new_eth, old_eth, 2 * ETH_ALEN);
	/* Ethertype is already in its new place */
	skb_pull_inline(skb, MLX5E_METADATA_ETHER_LEN);

	return skb;
}
