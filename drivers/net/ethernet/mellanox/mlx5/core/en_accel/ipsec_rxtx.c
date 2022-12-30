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
#include <net/esp.h>
#include "ipsec.h"
#include "ipsec_rxtx.h"
#include "en.h"

enum {
	MLX5E_IPSEC_TX_SYNDROME_OFFLOAD = 0x8,
	MLX5E_IPSEC_TX_SYNDROME_OFFLOAD_WITH_LSO_TCP = 0x9,
};

static int mlx5e_ipsec_remove_trailer(struct sk_buff *skb, struct xfrm_state *x)
{
	unsigned int alen = crypto_aead_authsize(x->data);
	struct ipv6hdr *ipv6hdr = ipv6_hdr(skb);
	struct iphdr *ipv4hdr = ip_hdr(skb);
	unsigned int trailer_len;
	u8 plen;
	int ret;

	ret = skb_copy_bits(skb, skb->len - alen - 2, &plen, 1);
	if (unlikely(ret))
		return ret;

	trailer_len = alen + plen + 2;

	pskb_trim(skb, skb->len - trailer_len);
	if (skb->protocol == htons(ETH_P_IP)) {
		ipv4hdr->tot_len = htons(ntohs(ipv4hdr->tot_len) - trailer_len);
		ip_send_check(ipv4hdr);
	} else {
		ipv6hdr->payload_len = htons(ntohs(ipv6hdr->payload_len) -
					     trailer_len);
	}
	return 0;
}

static void mlx5e_ipsec_set_swp(struct sk_buff *skb,
				struct mlx5_wqe_eth_seg *eseg, u8 mode,
				struct xfrm_offload *xo)
{
	/* Tunnel Mode:
	 * SWP:      OutL3       InL3  InL4
	 * Pkt: MAC  IP     ESP  IP    L4
	 *
	 * Transport Mode:
	 * SWP:      OutL3       OutL4
	 * Pkt: MAC  IP     ESP  L4
	 *
	 * Tunnel(VXLAN TCP/UDP) over Transport Mode
	 * SWP:      OutL3                   InL3  InL4
	 * Pkt: MAC  IP     ESP  UDP  VXLAN  IP    L4
	 */

	/* Shared settings */
	eseg->swp_outer_l3_offset = skb_network_offset(skb) / 2;
	if (skb->protocol == htons(ETH_P_IPV6))
		eseg->swp_flags |= MLX5_ETH_WQE_SWP_OUTER_L3_IPV6;

	/* Tunnel mode */
	if (mode == XFRM_MODE_TUNNEL) {
		eseg->swp_inner_l3_offset = skb_inner_network_offset(skb) / 2;
		if (xo->proto == IPPROTO_IPV6)
			eseg->swp_flags |= MLX5_ETH_WQE_SWP_INNER_L3_IPV6;

		switch (xo->inner_ipproto) {
		case IPPROTO_UDP:
			eseg->swp_flags |= MLX5_ETH_WQE_SWP_INNER_L4_UDP;
			fallthrough;
		case IPPROTO_TCP:
			/* IP | ESP | IP | [TCP | UDP] */
			eseg->swp_inner_l4_offset = skb_inner_transport_offset(skb) / 2;
			break;
		default:
			break;
		}
		return;
	}

	/* Transport mode */
	if (mode != XFRM_MODE_TRANSPORT)
		return;

	if (!xo->inner_ipproto) {
		switch (xo->proto) {
		case IPPROTO_UDP:
			eseg->swp_flags |= MLX5_ETH_WQE_SWP_OUTER_L4_UDP;
			fallthrough;
		case IPPROTO_TCP:
			/* IP | ESP | TCP */
			eseg->swp_outer_l4_offset = skb_inner_transport_offset(skb) / 2;
			break;
		default:
			break;
		}
	} else {
		/* Tunnel(VXLAN TCP/UDP) over Transport Mode */
		switch (xo->inner_ipproto) {
		case IPPROTO_UDP:
			eseg->swp_flags |= MLX5_ETH_WQE_SWP_INNER_L4_UDP;
			fallthrough;
		case IPPROTO_TCP:
			eseg->swp_inner_l3_offset = skb_inner_network_offset(skb) / 2;
			eseg->swp_inner_l4_offset =
				(skb->csum_start + skb->head - skb->data) / 2;
			if (inner_ip_hdr(skb)->version == 6)
				eseg->swp_flags |= MLX5_ETH_WQE_SWP_INNER_L3_IPV6;
			break;
		default:
			break;
		}
	}

}

void mlx5e_ipsec_set_iv_esn(struct sk_buff *skb, struct xfrm_state *x,
			    struct xfrm_offload *xo)
{
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;
	__u32 oseq = replay_esn->oseq;
	int iv_offset;
	__be64 seqno;
	u32 seq_hi;

	if (unlikely(skb_is_gso(skb) && oseq < MLX5E_IPSEC_ESN_SCOPE_MID &&
		     MLX5E_IPSEC_ESN_SCOPE_MID < (oseq - skb_shinfo(skb)->gso_segs))) {
		seq_hi = xo->seq.hi - 1;
	} else {
		seq_hi = xo->seq.hi;
	}

	/* Place the SN in the IV field */
	seqno = cpu_to_be64(xo->seq.low + ((u64)seq_hi << 32));
	iv_offset = skb_transport_offset(skb) + sizeof(struct ip_esp_hdr);
	skb_store_bits(skb, iv_offset, &seqno, 8);
}

void mlx5e_ipsec_set_iv(struct sk_buff *skb, struct xfrm_state *x,
			struct xfrm_offload *xo)
{
	int iv_offset;
	__be64 seqno;

	/* Place the SN in the IV field */
	seqno = cpu_to_be64(xo->seq.low + ((u64)xo->seq.hi << 32));
	iv_offset = skb_transport_offset(skb) + sizeof(struct ip_esp_hdr);
	skb_store_bits(skb, iv_offset, &seqno, 8);
}

void mlx5e_ipsec_handle_tx_wqe(struct mlx5e_tx_wqe *wqe,
			       struct mlx5e_accel_tx_ipsec_state *ipsec_st,
			       struct mlx5_wqe_inline_seg *inlseg)
{
	inlseg->byte_count = cpu_to_be32(ipsec_st->tailen | MLX5_INLINE_SEG);
	esp_output_fill_trailer((u8 *)inlseg->data, 0, ipsec_st->plen, ipsec_st->xo->proto);
}

static int mlx5e_ipsec_set_state(struct mlx5e_priv *priv,
				 struct sk_buff *skb,
				 struct xfrm_state *x,
				 struct xfrm_offload *xo,
				 struct mlx5e_accel_tx_ipsec_state *ipsec_st)
{
	unsigned int blksize, clen, alen, plen;
	struct crypto_aead *aead;
	unsigned int tailen;

	ipsec_st->x = x;
	ipsec_st->xo = xo;
	aead = x->data;
	alen = crypto_aead_authsize(aead);
	blksize = ALIGN(crypto_aead_blocksize(aead), 4);
	clen = ALIGN(skb->len + 2, blksize);
	plen = max_t(u32, clen - skb->len, 4);
	tailen = plen + alen;
	ipsec_st->plen = plen;
	ipsec_st->tailen = tailen;

	return 0;
}

void mlx5e_ipsec_tx_build_eseg(struct mlx5e_priv *priv, struct sk_buff *skb,
			       struct mlx5_wqe_eth_seg *eseg)
{
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct xfrm_encap_tmpl  *encap;
	struct xfrm_state *x;
	struct sec_path *sp;
	u8 l3_proto;

	sp = skb_sec_path(skb);
	if (unlikely(sp->len != 1))
		return;

	x = xfrm_input_state(skb);
	if (unlikely(!x))
		return;

	if (unlikely(!x->xso.offload_handle ||
		     (skb->protocol != htons(ETH_P_IP) &&
		      skb->protocol != htons(ETH_P_IPV6))))
		return;

	mlx5e_ipsec_set_swp(skb, eseg, x->props.mode, xo);

	l3_proto = (x->props.family == AF_INET) ?
		   ((struct iphdr *)skb_network_header(skb))->protocol :
		   ((struct ipv6hdr *)skb_network_header(skb))->nexthdr;

	eseg->flow_table_metadata |= cpu_to_be32(MLX5_ETH_WQE_FT_META_IPSEC);
	eseg->trailer |= cpu_to_be32(MLX5_ETH_WQE_INSERT_TRAILER);
	encap = x->encap;
	if (!encap) {
		eseg->trailer |= (l3_proto == IPPROTO_ESP) ?
			cpu_to_be32(MLX5_ETH_WQE_TRAILER_HDR_OUTER_IP_ASSOC) :
			cpu_to_be32(MLX5_ETH_WQE_TRAILER_HDR_OUTER_L4_ASSOC);
	} else if (encap->encap_type == UDP_ENCAP_ESPINUDP) {
		eseg->trailer |= (l3_proto == IPPROTO_ESP) ?
			cpu_to_be32(MLX5_ETH_WQE_TRAILER_HDR_INNER_IP_ASSOC) :
			cpu_to_be32(MLX5_ETH_WQE_TRAILER_HDR_INNER_L4_ASSOC);
	}
}

bool mlx5e_ipsec_handle_tx_skb(struct net_device *netdev,
			       struct sk_buff *skb,
			       struct mlx5e_accel_tx_ipsec_state *ipsec_st)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct mlx5e_ipsec_sa_entry *sa_entry;
	struct xfrm_state *x;
	struct sec_path *sp;

	sp = skb_sec_path(skb);
	if (unlikely(sp->len != 1)) {
		atomic64_inc(&priv->ipsec->sw_stats.ipsec_tx_drop_bundle);
		goto drop;
	}

	x = xfrm_input_state(skb);
	if (unlikely(!x)) {
		atomic64_inc(&priv->ipsec->sw_stats.ipsec_tx_drop_no_state);
		goto drop;
	}

	if (unlikely(!x->xso.offload_handle ||
		     (skb->protocol != htons(ETH_P_IP) &&
		      skb->protocol != htons(ETH_P_IPV6)))) {
		atomic64_inc(&priv->ipsec->sw_stats.ipsec_tx_drop_not_ip);
		goto drop;
	}

	if (!skb_is_gso(skb))
		if (unlikely(mlx5e_ipsec_remove_trailer(skb, x))) {
			atomic64_inc(&priv->ipsec->sw_stats.ipsec_tx_drop_trailer);
			goto drop;
		}

	sa_entry = (struct mlx5e_ipsec_sa_entry *)x->xso.offload_handle;
	sa_entry->set_iv_op(skb, x, xo);
	mlx5e_ipsec_set_state(priv, skb, x, xo, ipsec_st);

	return true;

drop:
	kfree_skb(skb);
	return false;
}

enum {
	MLX5E_IPSEC_OFFLOAD_RX_SYNDROME_DECRYPTED,
	MLX5E_IPSEC_OFFLOAD_RX_SYNDROME_AUTH_FAILED,
	MLX5E_IPSEC_OFFLOAD_RX_SYNDROME_BAD_TRAILER,
};

void mlx5e_ipsec_offload_handle_rx_skb(struct net_device *netdev,
				       struct sk_buff *skb,
				       struct mlx5_cqe64 *cqe)
{
	u32 ipsec_meta_data = be32_to_cpu(cqe->ft_metadata);
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_ipsec *ipsec = priv->ipsec;
	struct mlx5e_ipsec_sa_entry *sa_entry;
	struct xfrm_offload *xo;
	struct sec_path *sp;
	u32  sa_handle;

	sa_handle = MLX5_IPSEC_METADATA_HANDLE(ipsec_meta_data);
	sp = secpath_set(skb);
	if (unlikely(!sp)) {
		atomic64_inc(&ipsec->sw_stats.ipsec_rx_drop_sp_alloc);
		return;
	}

	rcu_read_lock();
	sa_entry = xa_load(&ipsec->sadb, sa_handle);
	if (unlikely(!sa_entry)) {
		rcu_read_unlock();
		atomic64_inc(&ipsec->sw_stats.ipsec_rx_drop_sadb_miss);
		return;
	}
	xfrm_state_hold(sa_entry->x);
	rcu_read_unlock();

	sp->xvec[sp->len++] = sa_entry->x;
	sp->olen++;

	xo = xfrm_offload(skb);
	xo->flags = CRYPTO_DONE;

	switch (MLX5_IPSEC_METADATA_SYNDROM(ipsec_meta_data)) {
	case MLX5E_IPSEC_OFFLOAD_RX_SYNDROME_DECRYPTED:
		xo->status = CRYPTO_SUCCESS;
		break;
	case MLX5E_IPSEC_OFFLOAD_RX_SYNDROME_AUTH_FAILED:
		xo->status = CRYPTO_TUNNEL_ESP_AUTH_FAILED;
		break;
	case MLX5E_IPSEC_OFFLOAD_RX_SYNDROME_BAD_TRAILER:
		xo->status = CRYPTO_INVALID_PACKET_SYNTAX;
		break;
	default:
		atomic64_inc(&ipsec->sw_stats.ipsec_rx_drop_syndrome);
	}
}
