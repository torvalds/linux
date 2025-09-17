// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <net/protocol.h>
#include <net/udp.h>
#include <net/ip6_checksum.h>
#include <net/psp/types.h>

#include "en.h"
#include "psp.h"
#include "en_accel/psp_rxtx.h"
#include "en_accel/psp.h"

enum {
	MLX5E_PSP_OFFLOAD_RX_SYNDROME_DECRYPTED,
	MLX5E_PSP_OFFLOAD_RX_SYNDROME_AUTH_FAILED,
	MLX5E_PSP_OFFLOAD_RX_SYNDROME_BAD_TRAILER,
};

static void mlx5e_psp_set_swp(struct sk_buff *skb,
			      struct mlx5e_accel_tx_psp_state *psp_st,
			      struct mlx5_wqe_eth_seg *eseg)
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
	u8 inner_ipproto = 0;
	struct ethhdr *eth;

	/* Shared settings */
	eseg->swp_outer_l3_offset = skb_network_offset(skb) / 2;
	if (skb->protocol == htons(ETH_P_IPV6))
		eseg->swp_flags |= MLX5_ETH_WQE_SWP_OUTER_L3_IPV6;

	if (skb->inner_protocol_type == ENCAP_TYPE_IPPROTO) {
		inner_ipproto = skb->inner_ipproto;
		/* Set SWP additional flags for packet of type IP|UDP|PSP|[ TCP | UDP ] */
		switch (inner_ipproto) {
		case IPPROTO_UDP:
			eseg->swp_flags |= MLX5_ETH_WQE_SWP_INNER_L4_UDP;
			fallthrough;
		case IPPROTO_TCP:
			eseg->swp_inner_l4_offset = skb_inner_transport_offset(skb) / 2;
			break;
		default:
			break;
		}
	} else {
		/* IP in IP tunneling like vxlan*/
		if (skb->inner_protocol_type != ENCAP_TYPE_ETHER)
			return;

		eth = (struct ethhdr *)skb_inner_mac_header(skb);
		switch (ntohs(eth->h_proto)) {
		case ETH_P_IP:
			inner_ipproto = ((struct iphdr *)((char *)skb->data +
					 skb_inner_network_offset(skb)))->protocol;
			break;
		case ETH_P_IPV6:
			inner_ipproto = ((struct ipv6hdr *)((char *)skb->data +
					 skb_inner_network_offset(skb)))->nexthdr;
			break;
		default:
			break;
		}

		/* Tunnel(VXLAN TCP/UDP) over Transport Mode PSP i.e. PSP payload is vxlan tunnel */
		switch (inner_ipproto) {
		case IPPROTO_UDP:
			eseg->swp_flags |= MLX5_ETH_WQE_SWP_INNER_L4_UDP;
			fallthrough;
		case IPPROTO_TCP:
			eseg->swp_inner_l3_offset = skb_inner_network_offset(skb) / 2;
			eseg->swp_inner_l4_offset =
				(skb->csum_start + skb->head - skb->data) / 2;
			if (skb->protocol == htons(ETH_P_IPV6))
				eseg->swp_flags |= MLX5_ETH_WQE_SWP_INNER_L3_IPV6;
			break;
		default:
			break;
		}

		psp_st->inner_ipproto = inner_ipproto;
	}
}

static bool mlx5e_psp_set_state(struct mlx5e_priv *priv,
				struct sk_buff *skb,
				struct mlx5e_accel_tx_psp_state *psp_st)
{
	struct psp_assoc *pas;
	bool ret = false;

	rcu_read_lock();
	pas = psp_skb_get_assoc_rcu(skb);
	if (!pas)
		goto out;

	ret = true;
	psp_st->tailen = PSP_TRL_SIZE;
	psp_st->spi = pas->tx.spi;
	psp_st->ver = pas->version;
	psp_st->keyid = *(u32 *)pas->drv_data;

out:
	rcu_read_unlock();
	return ret;
}

bool mlx5e_psp_offload_handle_rx_skb(struct net_device *netdev, struct sk_buff *skb,
				     struct mlx5_cqe64 *cqe)
{
	u32 psp_meta_data = be32_to_cpu(cqe->ft_metadata);
	struct mlx5e_priv *priv = netdev_priv(netdev);
	u16 dev_id = priv->psp->psp->id;
	bool strip_icv = true;
	u8 generation = 0;

	/* TBD: report errors as SW counters to ethtool, any further handling ? */
	if (MLX5_PSP_METADATA_SYNDROME(psp_meta_data) != MLX5E_PSP_OFFLOAD_RX_SYNDROME_DECRYPTED)
		goto drop;

	if (psp_dev_rcv(skb, dev_id, generation, strip_icv))
		goto drop;

	skb->decrypted = 1;
	return false;

drop:
	kfree_skb(skb);
	return true;
}

void mlx5e_psp_tx_build_eseg(struct mlx5e_priv *priv, struct sk_buff *skb,
			     struct mlx5e_accel_tx_psp_state *psp_st,
			     struct mlx5_wqe_eth_seg *eseg)
{
	if (!mlx5_is_psp_device(priv->mdev))
		return;

	if (unlikely(skb->protocol != htons(ETH_P_IP) &&
		     skb->protocol != htons(ETH_P_IPV6)))
		return;

	mlx5e_psp_set_swp(skb, psp_st, eseg);
	/* Special WA for PSP LSO in ConnectX7 */
	eseg->swp_outer_l3_offset = 0;
	eseg->swp_inner_l3_offset = 0;

	eseg->flow_table_metadata |= cpu_to_be32(psp_st->keyid);
	eseg->trailer |= cpu_to_be32(MLX5_ETH_WQE_INSERT_TRAILER) |
			 cpu_to_be32(MLX5_ETH_WQE_TRAILER_HDR_OUTER_L4_ASSOC);
}

void mlx5e_psp_handle_tx_wqe(struct mlx5e_tx_wqe *wqe,
			     struct mlx5e_accel_tx_psp_state *psp_st,
			     struct mlx5_wqe_inline_seg *inlseg)
{
	inlseg->byte_count = cpu_to_be32(psp_st->tailen | MLX5_INLINE_SEG);
}

bool mlx5e_psp_handle_tx_skb(struct net_device *netdev,
			     struct sk_buff *skb,
			     struct mlx5e_accel_tx_psp_state *psp_st)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct net *net = sock_net(skb->sk);
	const struct ipv6hdr *ip6;
	struct tcphdr *th;

	if (!mlx5e_psp_set_state(priv, skb, psp_st))
		return true;

	/* psp_encap of the packet */
	if (!psp_dev_encapsulate(net, skb, psp_st->spi, psp_st->ver, 0)) {
		kfree_skb_reason(skb, SKB_DROP_REASON_PSP_OUTPUT);
		return false;
	}
	if (skb_is_gso(skb)) {
		ip6 = ipv6_hdr(skb);
		th = inner_tcp_hdr(skb);

		th->check = ~tcp_v6_check(skb_shinfo(skb)->gso_size + inner_tcp_hdrlen(skb), &ip6->saddr,
					  &ip6->daddr, 0);
	}

	return true;
}
