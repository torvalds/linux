// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/u64_stats_sync.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/smp.h>
#include <asm/byteorder.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sctp.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>

#include "hinic_common.h"
#include "hinic_hw_if.h"
#include "hinic_hw_wqe.h"
#include "hinic_hw_wq.h"
#include "hinic_hw_qp.h"
#include "hinic_hw_dev.h"
#include "hinic_dev.h"
#include "hinic_tx.h"

#define TX_IRQ_NO_PENDING               0
#define TX_IRQ_NO_COALESC               0
#define TX_IRQ_NO_LLI_TIMER             0
#define TX_IRQ_NO_CREDIT                0
#define TX_IRQ_NO_RESEND_TIMER          0

#define CI_UPDATE_NO_PENDING            0
#define CI_UPDATE_NO_COALESC            0

#define HW_CONS_IDX(sq)                 be16_to_cpu(*(u16 *)((sq)->hw_ci_addr))

#define MIN_SKB_LEN                     17

#define	MAX_PAYLOAD_OFFSET	        221
#define TRANSPORT_OFFSET(l4_hdr, skb)	((u32)((l4_hdr) - (skb)->data))

union hinic_l3 {
	struct iphdr *v4;
	struct ipv6hdr *v6;
	unsigned char *hdr;
};

union hinic_l4 {
	struct tcphdr *tcp;
	struct udphdr *udp;
	unsigned char *hdr;
};

enum hinic_offload_type {
	TX_OFFLOAD_TSO     = BIT(0),
	TX_OFFLOAD_CSUM    = BIT(1),
	TX_OFFLOAD_VLAN    = BIT(2),
	TX_OFFLOAD_INVALID = BIT(3),
};

/**
 * hinic_txq_clean_stats - Clean the statistics of specific queue
 * @txq: Logical Tx Queue
 **/
void hinic_txq_clean_stats(struct hinic_txq *txq)
{
	struct hinic_txq_stats *txq_stats = &txq->txq_stats;

	u64_stats_update_begin(&txq_stats->syncp);
	txq_stats->pkts    = 0;
	txq_stats->bytes   = 0;
	txq_stats->tx_busy = 0;
	txq_stats->tx_wake = 0;
	txq_stats->tx_dropped = 0;
	txq_stats->big_frags_pkts = 0;
	u64_stats_update_end(&txq_stats->syncp);
}

/**
 * hinic_txq_get_stats - get statistics of Tx Queue
 * @txq: Logical Tx Queue
 * @stats: return updated stats here
 **/
void hinic_txq_get_stats(struct hinic_txq *txq, struct hinic_txq_stats *stats)
{
	struct hinic_txq_stats *txq_stats = &txq->txq_stats;
	unsigned int start;

	u64_stats_update_begin(&stats->syncp);
	do {
		start = u64_stats_fetch_begin(&txq_stats->syncp);
		stats->pkts    = txq_stats->pkts;
		stats->bytes   = txq_stats->bytes;
		stats->tx_busy = txq_stats->tx_busy;
		stats->tx_wake = txq_stats->tx_wake;
		stats->tx_dropped = txq_stats->tx_dropped;
		stats->big_frags_pkts = txq_stats->big_frags_pkts;
	} while (u64_stats_fetch_retry(&txq_stats->syncp, start));
	u64_stats_update_end(&stats->syncp);
}

/**
 * txq_stats_init - Initialize the statistics of specific queue
 * @txq: Logical Tx Queue
 **/
static void txq_stats_init(struct hinic_txq *txq)
{
	struct hinic_txq_stats *txq_stats = &txq->txq_stats;

	u64_stats_init(&txq_stats->syncp);
	hinic_txq_clean_stats(txq);
}

/**
 * tx_map_skb - dma mapping for skb and return sges
 * @nic_dev: nic device
 * @skb: the skb
 * @sges: returned sges
 *
 * Return 0 - Success, negative - Failure
 **/
static int tx_map_skb(struct hinic_dev *nic_dev, struct sk_buff *skb,
		      struct hinic_sge *sges)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct skb_frag_struct *frag;
	dma_addr_t dma_addr;
	int i, j;

	dma_addr = dma_map_single(&pdev->dev, skb->data, skb_headlen(skb),
				  DMA_TO_DEVICE);
	if (dma_mapping_error(&pdev->dev, dma_addr)) {
		dev_err(&pdev->dev, "Failed to map Tx skb data\n");
		return -EFAULT;
	}

	hinic_set_sge(&sges[0], dma_addr, skb_headlen(skb));

	for (i = 0 ; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];

		dma_addr = skb_frag_dma_map(&pdev->dev, frag, 0,
					    skb_frag_size(frag),
					    DMA_TO_DEVICE);
		if (dma_mapping_error(&pdev->dev, dma_addr)) {
			dev_err(&pdev->dev, "Failed to map Tx skb frag\n");
			goto err_tx_map;
		}

		hinic_set_sge(&sges[i + 1], dma_addr, skb_frag_size(frag));
	}

	return 0;

err_tx_map:
	for (j = 0; j < i; j++)
		dma_unmap_page(&pdev->dev, hinic_sge_to_dma(&sges[j + 1]),
			       sges[j + 1].len, DMA_TO_DEVICE);

	dma_unmap_single(&pdev->dev, hinic_sge_to_dma(&sges[0]), sges[0].len,
			 DMA_TO_DEVICE);
	return -EFAULT;
}

/**
 * tx_unmap_skb - unmap the dma address of the skb
 * @nic_dev: nic device
 * @skb: the skb
 * @sges: the sges that are connected to the skb
 **/
static void tx_unmap_skb(struct hinic_dev *nic_dev, struct sk_buff *skb,
			 struct hinic_sge *sges)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int i;

	for (i = 0; i < skb_shinfo(skb)->nr_frags ; i++)
		dma_unmap_page(&pdev->dev, hinic_sge_to_dma(&sges[i + 1]),
			       sges[i + 1].len, DMA_TO_DEVICE);

	dma_unmap_single(&pdev->dev, hinic_sge_to_dma(&sges[0]), sges[0].len,
			 DMA_TO_DEVICE);
}

static void get_inner_l3_l4_type(struct sk_buff *skb, union hinic_l3 *ip,
				 union hinic_l4 *l4,
				 enum hinic_offload_type offload_type,
				 enum hinic_l3_offload_type *l3_type,
				 u8 *l4_proto)
{
	u8 *exthdr;

	if (ip->v4->version == 4) {
		*l3_type = (offload_type == TX_OFFLOAD_CSUM) ?
			   IPV4_PKT_NO_CHKSUM_OFFLOAD :
			   IPV4_PKT_WITH_CHKSUM_OFFLOAD;
		*l4_proto = ip->v4->protocol;
	} else if (ip->v4->version == 6) {
		*l3_type = IPV6_PKT;
		exthdr = ip->hdr + sizeof(*ip->v6);
		*l4_proto = ip->v6->nexthdr;
		if (exthdr != l4->hdr) {
			int start = exthdr - skb->data;
			__be16 frag_off;

			ipv6_skip_exthdr(skb, start, l4_proto, &frag_off);
		}
	} else {
		*l3_type = L3TYPE_UNKNOWN;
		*l4_proto = 0;
	}
}

static void get_inner_l4_info(struct sk_buff *skb, union hinic_l4 *l4,
			      enum hinic_offload_type offload_type, u8 l4_proto,
			      enum hinic_l4_offload_type *l4_offload,
			      u32 *l4_len, u32 *offset)
{
	*l4_offload = OFFLOAD_DISABLE;
	*offset = 0;
	*l4_len = 0;

	switch (l4_proto) {
	case IPPROTO_TCP:
		*l4_offload = TCP_OFFLOAD_ENABLE;
		/* doff in unit of 4B */
		*l4_len = l4->tcp->doff * 4;
		*offset = *l4_len + TRANSPORT_OFFSET(l4->hdr, skb);
		break;

	case IPPROTO_UDP:
		*l4_offload = UDP_OFFLOAD_ENABLE;
		*l4_len = sizeof(struct udphdr);
		*offset = TRANSPORT_OFFSET(l4->hdr, skb);
		break;

	case IPPROTO_SCTP:
		/* only csum offload support sctp */
		if (offload_type != TX_OFFLOAD_CSUM)
			break;

		*l4_offload = SCTP_OFFLOAD_ENABLE;
		*l4_len = sizeof(struct sctphdr);
		*offset = TRANSPORT_OFFSET(l4->hdr, skb);
		break;

	default:
		break;
	}
}

static __sum16 csum_magic(union hinic_l3 *ip, unsigned short proto)
{
	return (ip->v4->version == 4) ?
		csum_tcpudp_magic(ip->v4->saddr, ip->v4->daddr, 0, proto, 0) :
		csum_ipv6_magic(&ip->v6->saddr, &ip->v6->daddr, 0, proto, 0);
}

static int offload_tso(struct hinic_sq_task *task, u32 *queue_info,
		       struct sk_buff *skb)
{
	u32 offset, l4_len, ip_identify, network_hdr_len;
	enum hinic_l3_offload_type l3_offload;
	enum hinic_l4_offload_type l4_offload;
	union hinic_l3 ip;
	union hinic_l4 l4;
	u8 l4_proto;

	if (!skb_is_gso(skb))
		return 0;

	if (skb_cow_head(skb, 0) < 0)
		return -EPROTONOSUPPORT;

	if (skb->encapsulation) {
		u32 gso_type = skb_shinfo(skb)->gso_type;
		u32 tunnel_type = 0;
		u32 l4_tunnel_len;

		ip.hdr = skb_network_header(skb);
		l4.hdr = skb_transport_header(skb);
		network_hdr_len = skb_inner_network_header_len(skb);

		if (ip.v4->version == 4) {
			ip.v4->tot_len = 0;
			l3_offload = IPV4_PKT_WITH_CHKSUM_OFFLOAD;
		} else if (ip.v4->version == 6) {
			l3_offload = IPV6_PKT;
		} else {
			l3_offload = 0;
		}

		hinic_task_set_outter_l3(task, l3_offload,
					 skb_network_header_len(skb));

		if (gso_type & SKB_GSO_UDP_TUNNEL_CSUM) {
			l4.udp->check = ~csum_magic(&ip, IPPROTO_UDP);
			tunnel_type = TUNNEL_UDP_CSUM;
		} else if (gso_type & SKB_GSO_UDP_TUNNEL) {
			tunnel_type = TUNNEL_UDP_NO_CSUM;
		}

		l4_tunnel_len = skb_inner_network_offset(skb) -
				skb_transport_offset(skb);
		hinic_task_set_tunnel_l4(task, tunnel_type, l4_tunnel_len);

		ip.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);
	} else {
		ip.hdr = skb_network_header(skb);
		l4.hdr = skb_transport_header(skb);
		network_hdr_len = skb_network_header_len(skb);
	}

	/* initialize inner IP header fields */
	if (ip.v4->version == 4)
		ip.v4->tot_len = 0;
	else
		ip.v6->payload_len = 0;

	get_inner_l3_l4_type(skb, &ip, &l4, TX_OFFLOAD_TSO, &l3_offload,
			     &l4_proto);

	hinic_task_set_inner_l3(task, l3_offload, network_hdr_len);

	ip_identify = 0;
	if (l4_proto == IPPROTO_TCP)
		l4.tcp->check = ~csum_magic(&ip, IPPROTO_TCP);

	get_inner_l4_info(skb, &l4, TX_OFFLOAD_TSO, l4_proto, &l4_offload,
			  &l4_len, &offset);

	hinic_set_tso_inner_l4(task, queue_info, l4_offload, l4_len, offset,
			       ip_identify, skb_shinfo(skb)->gso_size);

	return 1;
}

static int offload_csum(struct hinic_sq_task *task, u32 *queue_info,
			struct sk_buff *skb)
{
	enum hinic_l4_offload_type l4_offload;
	u32 offset, l4_len, network_hdr_len;
	enum hinic_l3_offload_type l3_type;
	union hinic_l3 ip;
	union hinic_l4 l4;
	u8 l4_proto;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (skb->encapsulation) {
		u32 l4_tunnel_len;

		ip.hdr = skb_network_header(skb);

		if (ip.v4->version == 4)
			l3_type = IPV4_PKT_NO_CHKSUM_OFFLOAD;
		else if (ip.v4->version == 6)
			l3_type = IPV6_PKT;
		else
			l3_type = L3TYPE_UNKNOWN;

		hinic_task_set_outter_l3(task, l3_type,
					 skb_network_header_len(skb));

		l4_tunnel_len = skb_inner_network_offset(skb) -
				skb_transport_offset(skb);

		hinic_task_set_tunnel_l4(task, TUNNEL_UDP_NO_CSUM,
					 l4_tunnel_len);

		ip.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);
		network_hdr_len = skb_inner_network_header_len(skb);
	} else {
		ip.hdr = skb_network_header(skb);
		l4.hdr = skb_transport_header(skb);
		network_hdr_len = skb_network_header_len(skb);
	}

	get_inner_l3_l4_type(skb, &ip, &l4, TX_OFFLOAD_CSUM, &l3_type,
			     &l4_proto);

	hinic_task_set_inner_l3(task, l3_type, network_hdr_len);

	get_inner_l4_info(skb, &l4, TX_OFFLOAD_CSUM, l4_proto, &l4_offload,
			  &l4_len, &offset);

	hinic_set_cs_inner_l4(task, queue_info, l4_offload, l4_len, offset);

	return 1;
}

static void offload_vlan(struct hinic_sq_task *task, u32 *queue_info,
			 u16 vlan_tag, u16 vlan_pri)
{
	task->pkt_info0 |= HINIC_SQ_TASK_INFO0_SET(vlan_tag, VLAN_TAG) |
				HINIC_SQ_TASK_INFO0_SET(1U, VLAN_OFFLOAD);

	*queue_info |= HINIC_SQ_CTRL_SET(vlan_pri, QUEUE_INFO_PRI);
}

static int hinic_tx_offload(struct sk_buff *skb, struct hinic_sq_task *task,
			    u32 *queue_info)
{
	enum hinic_offload_type offload = 0;
	u16 vlan_tag;
	int enabled;

	enabled = offload_tso(task, queue_info, skb);
	if (enabled > 0) {
		offload |= TX_OFFLOAD_TSO;
	} else if (enabled == 0) {
		enabled = offload_csum(task, queue_info, skb);
		if (enabled)
			offload |= TX_OFFLOAD_CSUM;
	} else {
		return -EPROTONOSUPPORT;
	}

	if (unlikely(skb_vlan_tag_present(skb))) {
		vlan_tag = skb_vlan_tag_get(skb);
		offload_vlan(task, queue_info, vlan_tag,
			     vlan_tag >> VLAN_PRIO_SHIFT);
		offload |= TX_OFFLOAD_VLAN;
	}

	if (offload)
		hinic_task_set_l2hdr(task, skb_network_offset(skb));

	/* payload offset should not more than 221 */
	if (HINIC_SQ_CTRL_GET(*queue_info, QUEUE_INFO_PLDOFF) >
	    MAX_PAYLOAD_OFFSET) {
		return -EPROTONOSUPPORT;
	}

	/* mss should not less than 80 */
	if (HINIC_SQ_CTRL_GET(*queue_info, QUEUE_INFO_MSS) < HINIC_MSS_MIN) {
		*queue_info = HINIC_SQ_CTRL_CLEAR(*queue_info, QUEUE_INFO_MSS);
		*queue_info |= HINIC_SQ_CTRL_SET(HINIC_MSS_MIN, QUEUE_INFO_MSS);
	}

	return 0;
}

netdev_tx_t hinic_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u16 prod_idx, q_id = skb->queue_mapping;
	struct netdev_queue *netdev_txq;
	int nr_sges, err = NETDEV_TX_OK;
	struct hinic_sq_wqe *sq_wqe;
	unsigned int wqe_size;
	struct hinic_txq *txq;
	struct hinic_qp *qp;

	txq = &nic_dev->txqs[q_id];
	qp = container_of(txq->sq, struct hinic_qp, sq);

	if (skb->len < MIN_SKB_LEN) {
		if (skb_pad(skb, MIN_SKB_LEN - skb->len)) {
			netdev_err(netdev, "Failed to pad skb\n");
			goto update_error_stats;
		}

		skb->len = MIN_SKB_LEN;
	}

	nr_sges = skb_shinfo(skb)->nr_frags + 1;
	if (nr_sges > 17) {
		u64_stats_update_begin(&txq->txq_stats.syncp);
		txq->txq_stats.big_frags_pkts++;
		u64_stats_update_end(&txq->txq_stats.syncp);
	}

	if (nr_sges > txq->max_sges) {
		netdev_err(netdev, "Too many Tx sges\n");
		goto skb_error;
	}

	err = tx_map_skb(nic_dev, skb, txq->sges);
	if (err)
		goto skb_error;

	wqe_size = HINIC_SQ_WQE_SIZE(nr_sges);

	sq_wqe = hinic_sq_get_wqe(txq->sq, wqe_size, &prod_idx);
	if (!sq_wqe) {
		netif_stop_subqueue(netdev, qp->q_id);

		/* Check for the case free_tx_poll is called in another cpu
		 * and we stopped the subqueue after free_tx_poll check.
		 */
		sq_wqe = hinic_sq_get_wqe(txq->sq, wqe_size, &prod_idx);
		if (sq_wqe) {
			netif_wake_subqueue(nic_dev->netdev, qp->q_id);
			goto process_sq_wqe;
		}

		tx_unmap_skb(nic_dev, skb, txq->sges);

		u64_stats_update_begin(&txq->txq_stats.syncp);
		txq->txq_stats.tx_busy++;
		u64_stats_update_end(&txq->txq_stats.syncp);
		err = NETDEV_TX_BUSY;
		wqe_size = 0;
		goto flush_skbs;
	}

process_sq_wqe:
	hinic_sq_prepare_wqe(txq->sq, prod_idx, sq_wqe, txq->sges, nr_sges);

	err = hinic_tx_offload(skb, &sq_wqe->task, &sq_wqe->ctrl.queue_info);
	if (err)
		goto offload_error;

	hinic_sq_write_wqe(txq->sq, prod_idx, sq_wqe, skb, wqe_size);

flush_skbs:
	netdev_txq = netdev_get_tx_queue(netdev, q_id);
	if ((!netdev_xmit_more()) || (netif_xmit_stopped(netdev_txq)))
		hinic_sq_write_db(txq->sq, prod_idx, wqe_size, 0);

	return err;

offload_error:
	hinic_sq_return_wqe(txq->sq, wqe_size);
	tx_unmap_skb(nic_dev, skb, txq->sges);

skb_error:
	dev_kfree_skb_any(skb);

update_error_stats:
	u64_stats_update_begin(&txq->txq_stats.syncp);
	txq->txq_stats.tx_dropped++;
	u64_stats_update_end(&txq->txq_stats.syncp);

	return NETDEV_TX_OK;
}

/**
 * tx_free_skb - unmap and free skb
 * @nic_dev: nic device
 * @skb: the skb
 * @sges: the sges that are connected to the skb
 **/
static void tx_free_skb(struct hinic_dev *nic_dev, struct sk_buff *skb,
			struct hinic_sge *sges)
{
	tx_unmap_skb(nic_dev, skb, sges);

	dev_kfree_skb_any(skb);
}

/**
 * free_all_rx_skbs - free all skbs in tx queue
 * @txq: tx queue
 **/
static void free_all_tx_skbs(struct hinic_txq *txq)
{
	struct hinic_dev *nic_dev = netdev_priv(txq->netdev);
	struct hinic_sq *sq = txq->sq;
	struct hinic_sq_wqe *sq_wqe;
	unsigned int wqe_size;
	struct sk_buff *skb;
	int nr_sges;
	u16 ci;

	while ((sq_wqe = hinic_sq_read_wqebb(sq, &skb, &wqe_size, &ci))) {
		sq_wqe = hinic_sq_read_wqe(sq, &skb, wqe_size, &ci);
		if (!sq_wqe)
			break;

		nr_sges = skb_shinfo(skb)->nr_frags + 1;

		hinic_sq_get_sges(sq_wqe, txq->free_sges, nr_sges);

		hinic_sq_put_wqe(sq, wqe_size);

		tx_free_skb(nic_dev, skb, txq->free_sges);
	}
}

/**
 * free_tx_poll - free finished tx skbs in tx queue that connected to napi
 * @napi: napi
 * @budget: number of tx
 *
 * Return 0 - Success, negative - Failure
 **/
static int free_tx_poll(struct napi_struct *napi, int budget)
{
	struct hinic_txq *txq = container_of(napi, struct hinic_txq, napi);
	struct hinic_qp *qp = container_of(txq->sq, struct hinic_qp, sq);
	struct hinic_dev *nic_dev = netdev_priv(txq->netdev);
	struct netdev_queue *netdev_txq;
	struct hinic_sq *sq = txq->sq;
	struct hinic_wq *wq = sq->wq;
	struct hinic_sq_wqe *sq_wqe;
	unsigned int wqe_size;
	int nr_sges, pkts = 0;
	struct sk_buff *skb;
	u64 tx_bytes = 0;
	u16 hw_ci, sw_ci;

	do {
		hw_ci = HW_CONS_IDX(sq) & wq->mask;

		/* Reading a WQEBB to get real WQE size and consumer index. */
		sq_wqe = hinic_sq_read_wqebb(sq, &skb, &wqe_size, &sw_ci);
		if ((!sq_wqe) ||
		    (((hw_ci - sw_ci) & wq->mask) * wq->wqebb_size < wqe_size))
			break;

		/* If this WQE have multiple WQEBBs, we will read again to get
		 * full size WQE.
		 */
		if (wqe_size > wq->wqebb_size) {
			sq_wqe = hinic_sq_read_wqe(sq, &skb, wqe_size, &sw_ci);
			if (unlikely(!sq_wqe))
				break;
		}

		tx_bytes += skb->len;
		pkts++;

		nr_sges = skb_shinfo(skb)->nr_frags + 1;

		hinic_sq_get_sges(sq_wqe, txq->free_sges, nr_sges);

		hinic_sq_put_wqe(sq, wqe_size);

		tx_free_skb(nic_dev, skb, txq->free_sges);
	} while (pkts < budget);

	if (__netif_subqueue_stopped(nic_dev->netdev, qp->q_id) &&
	    hinic_get_sq_free_wqebbs(sq) >= HINIC_MIN_TX_NUM_WQEBBS(sq)) {
		netdev_txq = netdev_get_tx_queue(txq->netdev, qp->q_id);

		__netif_tx_lock(netdev_txq, smp_processor_id());

		netif_wake_subqueue(nic_dev->netdev, qp->q_id);

		__netif_tx_unlock(netdev_txq);

		u64_stats_update_begin(&txq->txq_stats.syncp);
		txq->txq_stats.tx_wake++;
		u64_stats_update_end(&txq->txq_stats.syncp);
	}

	u64_stats_update_begin(&txq->txq_stats.syncp);
	txq->txq_stats.bytes += tx_bytes;
	txq->txq_stats.pkts += pkts;
	u64_stats_update_end(&txq->txq_stats.syncp);

	if (pkts < budget) {
		napi_complete(napi);
		hinic_hwdev_set_msix_state(nic_dev->hwdev,
					   sq->msix_entry,
					   HINIC_MSIX_ENABLE);
		return pkts;
	}

	return budget;
}

static void tx_napi_add(struct hinic_txq *txq, int weight)
{
	netif_napi_add(txq->netdev, &txq->napi, free_tx_poll, weight);
	napi_enable(&txq->napi);
}

static void tx_napi_del(struct hinic_txq *txq)
{
	napi_disable(&txq->napi);
	netif_napi_del(&txq->napi);
}

static irqreturn_t tx_irq(int irq, void *data)
{
	struct hinic_txq *txq = data;
	struct hinic_dev *nic_dev;

	nic_dev = netdev_priv(txq->netdev);

	/* Disable the interrupt until napi will be completed */
	hinic_hwdev_set_msix_state(nic_dev->hwdev,
				   txq->sq->msix_entry,
				   HINIC_MSIX_DISABLE);

	hinic_hwdev_msix_cnt_set(nic_dev->hwdev, txq->sq->msix_entry);

	napi_schedule(&txq->napi);
	return IRQ_HANDLED;
}

static int tx_request_irq(struct hinic_txq *txq)
{
	struct hinic_dev *nic_dev = netdev_priv(txq->netdev);
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_sq *sq = txq->sq;
	int err;

	tx_napi_add(txq, nic_dev->tx_weight);

	hinic_hwdev_msix_set(nic_dev->hwdev, sq->msix_entry,
			     TX_IRQ_NO_PENDING, TX_IRQ_NO_COALESC,
			     TX_IRQ_NO_LLI_TIMER, TX_IRQ_NO_CREDIT,
			     TX_IRQ_NO_RESEND_TIMER);

	err = request_irq(sq->irq, tx_irq, 0, txq->irq_name, txq);
	if (err) {
		dev_err(&pdev->dev, "Failed to request Tx irq\n");
		tx_napi_del(txq);
		return err;
	}

	return 0;
}

static void tx_free_irq(struct hinic_txq *txq)
{
	struct hinic_sq *sq = txq->sq;

	free_irq(sq->irq, txq);
	tx_napi_del(txq);
}

/**
 * hinic_init_txq - Initialize the Tx Queue
 * @txq: Logical Tx Queue
 * @sq: Hardware Tx Queue to connect the Logical queue with
 * @netdev: network device to connect the Logical queue with
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_init_txq(struct hinic_txq *txq, struct hinic_sq *sq,
		   struct net_device *netdev)
{
	struct hinic_qp *qp = container_of(sq, struct hinic_qp, sq);
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	int err, irqname_len;
	size_t sges_size;

	txq->netdev = netdev;
	txq->sq = sq;

	txq_stats_init(txq);

	txq->max_sges = HINIC_MAX_SQ_BUFDESCS;

	sges_size = txq->max_sges * sizeof(*txq->sges);
	txq->sges = devm_kzalloc(&netdev->dev, sges_size, GFP_KERNEL);
	if (!txq->sges)
		return -ENOMEM;

	sges_size = txq->max_sges * sizeof(*txq->free_sges);
	txq->free_sges = devm_kzalloc(&netdev->dev, sges_size, GFP_KERNEL);
	if (!txq->free_sges) {
		err = -ENOMEM;
		goto err_alloc_free_sges;
	}

	irqname_len = snprintf(NULL, 0, "hinic_txq%d", qp->q_id) + 1;
	txq->irq_name = devm_kzalloc(&netdev->dev, irqname_len, GFP_KERNEL);
	if (!txq->irq_name) {
		err = -ENOMEM;
		goto err_alloc_irqname;
	}

	sprintf(txq->irq_name, "hinic_txq%d", qp->q_id);

	err = hinic_hwdev_hw_ci_addr_set(hwdev, sq, CI_UPDATE_NO_PENDING,
					 CI_UPDATE_NO_COALESC);
	if (err)
		goto err_hw_ci;

	err = tx_request_irq(txq);
	if (err) {
		netdev_err(netdev, "Failed to request Tx irq\n");
		goto err_req_tx_irq;
	}

	return 0;

err_req_tx_irq:
err_hw_ci:
	devm_kfree(&netdev->dev, txq->irq_name);

err_alloc_irqname:
	devm_kfree(&netdev->dev, txq->free_sges);

err_alloc_free_sges:
	devm_kfree(&netdev->dev, txq->sges);
	return err;
}

/**
 * hinic_clean_txq - Clean the Tx Queue
 * @txq: Logical Tx Queue
 **/
void hinic_clean_txq(struct hinic_txq *txq)
{
	struct net_device *netdev = txq->netdev;

	tx_free_irq(txq);

	free_all_tx_skbs(txq);

	devm_kfree(&netdev->dev, txq->irq_name);
	devm_kfree(&netdev->dev, txq->free_sges);
	devm_kfree(&netdev->dev, txq->sges);
}
