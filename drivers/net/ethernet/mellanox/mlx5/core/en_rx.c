/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
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
 */

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <net/busy_poll.h>
#include "en.h"

static inline bool mlx5e_rx_hw_stamp(struct mlx5e_tstamp *tstamp)
{
	return tstamp->hwtstamp_config.rx_filter == HWTSTAMP_FILTER_ALL;
}

static inline int mlx5e_alloc_rx_wqe(struct mlx5e_rq *rq,
				     struct mlx5e_rx_wqe *wqe, u16 ix)
{
	struct sk_buff *skb;
	dma_addr_t dma_addr;

	skb = netdev_alloc_skb(rq->netdev, rq->wqe_sz);
	if (unlikely(!skb))
		return -ENOMEM;

	dma_addr = dma_map_single(rq->pdev,
				  /* hw start padding */
				  skb->data,
				  /* hw end padding */
				  rq->wqe_sz,
				  DMA_FROM_DEVICE);

	if (unlikely(dma_mapping_error(rq->pdev, dma_addr)))
		goto err_free_skb;

	skb_reserve(skb, MLX5E_NET_IP_ALIGN);

	*((dma_addr_t *)skb->cb) = dma_addr;
	wqe->data.addr = cpu_to_be64(dma_addr + MLX5E_NET_IP_ALIGN);

	rq->skb[ix] = skb;

	return 0;

err_free_skb:
	dev_kfree_skb(skb);

	return -ENOMEM;
}

bool mlx5e_post_rx_wqes(struct mlx5e_rq *rq)
{
	struct mlx5_wq_ll *wq = &rq->wq;

	if (unlikely(!test_bit(MLX5E_RQ_STATE_POST_WQES_ENABLE, &rq->state)))
		return false;

	while (!mlx5_wq_ll_is_full(wq)) {
		struct mlx5e_rx_wqe *wqe = mlx5_wq_ll_get_wqe(wq, wq->head);

		if (unlikely(mlx5e_alloc_rx_wqe(rq, wqe, wq->head)))
			break;

		mlx5_wq_ll_push(wq, be16_to_cpu(wqe->next.next_wqe_index));
	}

	/* ensure wqes are visible to device before updating doorbell record */
	dma_wmb();

	mlx5_wq_ll_update_db_record(wq);

	return !mlx5_wq_ll_is_full(wq);
}

static void mlx5e_lro_update_hdr(struct sk_buff *skb, struct mlx5_cqe64 *cqe)
{
	struct ethhdr	*eth	= (struct ethhdr *)(skb->data);
	struct iphdr	*ipv4	= (struct iphdr *)(skb->data + ETH_HLEN);
	struct ipv6hdr	*ipv6	= (struct ipv6hdr *)(skb->data + ETH_HLEN);
	struct tcphdr	*tcp;

	u8 l4_hdr_type = get_cqe_l4_hdr_type(cqe);
	int tcp_ack = ((CQE_L4_HDR_TYPE_TCP_ACK_NO_DATA  == l4_hdr_type) ||
		       (CQE_L4_HDR_TYPE_TCP_ACK_AND_DATA == l4_hdr_type));

	u16 tot_len = be32_to_cpu(cqe->byte_cnt) - ETH_HLEN;

	if (eth->h_proto == htons(ETH_P_IP)) {
		tcp = (struct tcphdr *)(skb->data + ETH_HLEN +
					sizeof(struct iphdr));
		ipv6 = NULL;
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
	} else {
		tcp = (struct tcphdr *)(skb->data + ETH_HLEN +
					sizeof(struct ipv6hdr));
		ipv4 = NULL;
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
	}

	if (get_cqe_lro_tcppsh(cqe))
		tcp->psh                = 1;

	if (tcp_ack) {
		tcp->ack                = 1;
		tcp->ack_seq            = cqe->lro_ack_seq_num;
		tcp->window             = cqe->lro_tcp_win;
	}

	if (ipv4) {
		ipv4->ttl               = cqe->lro_min_ttl;
		ipv4->tot_len           = cpu_to_be16(tot_len);
		ipv4->check             = 0;
		ipv4->check             = ip_fast_csum((unsigned char *)ipv4,
						       ipv4->ihl);
	} else {
		ipv6->hop_limit         = cqe->lro_min_ttl;
		ipv6->payload_len       = cpu_to_be16(tot_len -
						      sizeof(struct ipv6hdr));
	}
}

static inline void mlx5e_skb_set_hash(struct mlx5_cqe64 *cqe,
				      struct sk_buff *skb)
{
	u8 cht = cqe->rss_hash_type;
	int ht = (cht & CQE_RSS_HTYPE_L4) ? PKT_HASH_TYPE_L4 :
		 (cht & CQE_RSS_HTYPE_IP) ? PKT_HASH_TYPE_L3 :
					    PKT_HASH_TYPE_NONE;
	skb_set_hash(skb, be32_to_cpu(cqe->rss_hash_result), ht);
}

static inline bool is_first_ethertype_ip(struct sk_buff *skb)
{
	__be16 ethertype = ((struct ethhdr *)skb->data)->h_proto;

	return (ethertype == htons(ETH_P_IP) || ethertype == htons(ETH_P_IPV6));
}

static inline void mlx5e_handle_csum(struct net_device *netdev,
				     struct mlx5_cqe64 *cqe,
				     struct mlx5e_rq *rq,
				     struct sk_buff *skb)
{
	if (unlikely(!(netdev->features & NETIF_F_RXCSUM)))
		goto csum_none;

	if (likely(cqe->hds_ip_ext & CQE_L4_OK)) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else if (is_first_ethertype_ip(skb)) {
		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->csum = csum_unfold((__force __sum16)cqe->check_sum);
		rq->stats.csum_sw++;
	} else {
		goto csum_none;
	}

	return;

csum_none:
	skb->ip_summed = CHECKSUM_NONE;
	rq->stats.csum_none++;
}

static inline void mlx5e_build_rx_skb(struct mlx5_cqe64 *cqe,
				      struct mlx5e_rq *rq,
				      struct sk_buff *skb)
{
	struct net_device *netdev = rq->netdev;
	u32 cqe_bcnt = be32_to_cpu(cqe->byte_cnt);
	struct mlx5e_tstamp *tstamp = rq->tstamp;
	int lro_num_seg;

	skb_put(skb, cqe_bcnt);

	lro_num_seg = be32_to_cpu(cqe->srqn) >> 24;
	if (lro_num_seg > 1) {
		mlx5e_lro_update_hdr(skb, cqe);
		skb_shinfo(skb)->gso_size = DIV_ROUND_UP(cqe_bcnt, lro_num_seg);
		rq->stats.lro_packets++;
		rq->stats.lro_bytes += cqe_bcnt;
	}

	if (unlikely(mlx5e_rx_hw_stamp(tstamp)))
		mlx5e_fill_hwstamp(tstamp, get_cqe_ts(cqe), skb_hwtstamps(skb));

	mlx5e_handle_csum(netdev, cqe, rq, skb);

	skb->protocol = eth_type_trans(skb, netdev);

	skb_record_rx_queue(skb, rq->ix);

	if (likely(netdev->features & NETIF_F_RXHASH))
		mlx5e_skb_set_hash(cqe, skb);

	if (cqe_has_vlan(cqe))
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       be16_to_cpu(cqe->vlan_info));
}

int mlx5e_poll_rx_cq(struct mlx5e_cq *cq, int budget)
{
	struct mlx5e_rq *rq = container_of(cq, struct mlx5e_rq, cq);
	int work_done;

	for (work_done = 0; work_done < budget; work_done++) {
		struct mlx5e_rx_wqe *wqe;
		struct mlx5_cqe64 *cqe;
		struct sk_buff *skb;
		__be16 wqe_counter_be;
		u16 wqe_counter;

		cqe = mlx5e_get_cqe(cq);
		if (!cqe)
			break;

		mlx5_cqwq_pop(&cq->wq);

		wqe_counter_be = cqe->wqe_counter;
		wqe_counter    = be16_to_cpu(wqe_counter_be);
		wqe            = mlx5_wq_ll_get_wqe(&rq->wq, wqe_counter);
		skb            = rq->skb[wqe_counter];
		prefetch(skb->data);
		rq->skb[wqe_counter] = NULL;

		dma_unmap_single(rq->pdev,
				 *((dma_addr_t *)skb->cb),
				 rq->wqe_sz,
				 DMA_FROM_DEVICE);

		if (unlikely((cqe->op_own >> 4) != MLX5_CQE_RESP_SEND)) {
			rq->stats.wqe_err++;
			dev_kfree_skb(skb);
			goto wq_ll_pop;
		}

		mlx5e_build_rx_skb(cqe, rq, skb);
		rq->stats.packets++;
		rq->stats.bytes += be32_to_cpu(cqe->byte_cnt);
		napi_gro_receive(cq->napi, skb);

wq_ll_pop:
		mlx5_wq_ll_pop(&rq->wq, wqe_counter_be,
			       &wqe->next.next_wqe_index);
	}

	mlx5_cqwq_update_db_record(&cq->wq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	return work_done;
}
