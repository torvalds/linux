// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/netdevice.h>
#include <net/gro.h>
#include <net/page_pool/helpers.h>

#include "hinic3_hwdev.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_io.h"
#include "hinic3_rx.h"

#define HINIC3_RX_HDR_SIZE              256
#define HINIC3_RX_BUFFER_WRITE          16

#define HINIC3_RX_TCP_PKT               0x3
#define HINIC3_RX_UDP_PKT               0x4
#define HINIC3_RX_SCTP_PKT              0x7

#define HINIC3_RX_IPV4_PKT              0
#define HINIC3_RX_IPV6_PKT              1
#define HINIC3_RX_INVALID_IP_TYPE       2

#define HINIC3_RX_PKT_FORMAT_NON_TUNNEL 0
#define HINIC3_RX_PKT_FORMAT_VXLAN      1

#define HINIC3_LRO_PKT_HDR_LEN_IPV4     66
#define HINIC3_LRO_PKT_HDR_LEN_IPV6     86
#define HINIC3_LRO_PKT_HDR_LEN(cqe) \
	(RQ_CQE_OFFOLAD_TYPE_GET((cqe)->offload_type, IP_TYPE) == \
	 HINIC3_RX_IPV6_PKT ? HINIC3_LRO_PKT_HDR_LEN_IPV6 : \
	 HINIC3_LRO_PKT_HDR_LEN_IPV4)

int hinic3_alloc_rxqs(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct pci_dev *pdev = nic_dev->pdev;
	u16 num_rxqs = nic_dev->max_qps;
	struct hinic3_rxq *rxq;
	u16 q_id;

	nic_dev->rxqs = kcalloc(num_rxqs, sizeof(*nic_dev->rxqs), GFP_KERNEL);
	if (!nic_dev->rxqs)
		return -ENOMEM;

	for (q_id = 0; q_id < num_rxqs; q_id++) {
		rxq = &nic_dev->rxqs[q_id];
		rxq->netdev = netdev;
		rxq->dev = &pdev->dev;
		rxq->q_id = q_id;
		rxq->buf_len = nic_dev->rx_buf_len;
		rxq->buf_len_shift = ilog2(nic_dev->rx_buf_len);
		rxq->q_depth = nic_dev->q_params.rq_depth;
		rxq->q_mask = nic_dev->q_params.rq_depth - 1;
	}

	return 0;
}

void hinic3_free_rxqs(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	kfree(nic_dev->rxqs);
}

static int rx_alloc_mapped_page(struct page_pool *page_pool,
				struct hinic3_rx_info *rx_info, u16 buf_len)
{
	struct page *page;
	u32 page_offset;

	if (likely(rx_info->page))
		return 0;

	page = page_pool_dev_alloc_frag(page_pool, &page_offset, buf_len);
	if (unlikely(!page))
		return -ENOMEM;

	rx_info->page = page;
	rx_info->page_offset = page_offset;

	return 0;
}

/* Associate fixed completion element to every wqe in the rq. Every rq wqe will
 * always post completion to the same place.
 */
static void rq_associate_cqes(struct hinic3_rxq *rxq)
{
	struct hinic3_queue_pages *qpages;
	struct hinic3_rq_wqe *rq_wqe;
	dma_addr_t cqe_dma;
	u32 i;

	qpages = &rxq->rq->wq.qpages;

	for (i = 0; i < rxq->q_depth; i++) {
		rq_wqe = get_q_element(qpages, i, NULL);
		cqe_dma = rxq->cqe_start_paddr +
			  i * sizeof(struct hinic3_rq_cqe);
		rq_wqe->cqe_hi_addr = cpu_to_le32(upper_32_bits(cqe_dma));
		rq_wqe->cqe_lo_addr = cpu_to_le32(lower_32_bits(cqe_dma));
	}
}

static void rq_wqe_buf_set(struct hinic3_io_queue *rq, uint32_t wqe_idx,
			   dma_addr_t dma_addr, u16 len)
{
	struct hinic3_rq_wqe *rq_wqe;

	rq_wqe = get_q_element(&rq->wq.qpages, wqe_idx, NULL);
	rq_wqe->buf_hi_addr = cpu_to_le32(upper_32_bits(dma_addr));
	rq_wqe->buf_lo_addr = cpu_to_le32(lower_32_bits(dma_addr));
}

static u32 hinic3_rx_fill_buffers(struct hinic3_rxq *rxq)
{
	u32 i, free_wqebbs = rxq->delta - 1;
	struct hinic3_rx_info *rx_info;
	dma_addr_t dma_addr;
	int err;

	for (i = 0; i < free_wqebbs; i++) {
		rx_info = &rxq->rx_info[rxq->next_to_update];

		err = rx_alloc_mapped_page(rxq->page_pool, rx_info,
					   rxq->buf_len);
		if (unlikely(err))
			break;

		dma_addr = page_pool_get_dma_addr(rx_info->page) +
			rx_info->page_offset;
		rq_wqe_buf_set(rxq->rq, rxq->next_to_update, dma_addr,
			       rxq->buf_len);
		rxq->next_to_update = (rxq->next_to_update + 1) & rxq->q_mask;
	}

	if (likely(i)) {
		hinic3_write_db(rxq->rq, rxq->q_id & 3, DB_CFLAG_DP_RQ,
				rxq->next_to_update << HINIC3_NORMAL_RQ_WQE);
		rxq->delta -= i;
		rxq->next_to_alloc = rxq->next_to_update;
	}

	return i;
}

static u32 hinic3_alloc_rx_buffers(struct hinic3_dyna_rxq_res *rqres,
				   u32 rq_depth, u16 buf_len)
{
	u32 free_wqebbs = rq_depth - 1;
	u32 idx;
	int err;

	for (idx = 0; idx < free_wqebbs; idx++) {
		err = rx_alloc_mapped_page(rqres->page_pool,
					   &rqres->rx_info[idx], buf_len);
		if (err)
			break;
	}

	return idx;
}

static void hinic3_free_rx_buffers(struct hinic3_dyna_rxq_res *rqres,
				   u32 q_depth)
{
	struct hinic3_rx_info *rx_info;
	u32 i;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < q_depth; i++) {
		rx_info = &rqres->rx_info[i];

		if (rx_info->page) {
			page_pool_put_full_page(rqres->page_pool,
						rx_info->page, false);
			rx_info->page = NULL;
		}
	}
}

static void hinic3_add_rx_frag(struct hinic3_rxq *rxq,
			       struct hinic3_rx_info *rx_info,
			       struct sk_buff *skb, u32 size)
{
	struct page *page;
	u8 *va;

	page = rx_info->page;
	va = (u8 *)page_address(page) + rx_info->page_offset;
	net_prefetch(va);

	page_pool_dma_sync_for_cpu(rxq->page_pool, page, rx_info->page_offset,
				   rxq->buf_len);

	if (size <= HINIC3_RX_HDR_SIZE && !skb_is_nonlinear(skb)) {
		memcpy(__skb_put(skb, size), va,
		       ALIGN(size, sizeof(long)));
		page_pool_put_full_page(rxq->page_pool, page, false);

		return;
	}

	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page,
			rx_info->page_offset, size, rxq->buf_len);
	skb_mark_for_recycle(skb);
}

static void packaging_skb(struct hinic3_rxq *rxq, struct sk_buff *skb,
			  u32 sge_num, u32 pkt_len)
{
	struct hinic3_rx_info *rx_info;
	u32 temp_pkt_len = pkt_len;
	u32 temp_sge_num = sge_num;
	u32 sw_ci;
	u32 size;

	sw_ci = rxq->cons_idx & rxq->q_mask;
	while (temp_sge_num) {
		rx_info = &rxq->rx_info[sw_ci];
		sw_ci = (sw_ci + 1) & rxq->q_mask;
		if (unlikely(temp_pkt_len > rxq->buf_len)) {
			size = rxq->buf_len;
			temp_pkt_len -= rxq->buf_len;
		} else {
			size = temp_pkt_len;
		}

		hinic3_add_rx_frag(rxq, rx_info, skb, size);

		/* clear contents of buffer_info */
		rx_info->page = NULL;
		temp_sge_num--;
	}
}

static u32 hinic3_get_sge_num(struct hinic3_rxq *rxq, u32 pkt_len)
{
	u32 sge_num;

	sge_num = pkt_len >> rxq->buf_len_shift;
	sge_num += (pkt_len & (rxq->buf_len - 1)) ? 1 : 0;

	return sge_num;
}

static struct sk_buff *hinic3_fetch_rx_buffer(struct hinic3_rxq *rxq,
					      u32 pkt_len)
{
	struct sk_buff *skb;
	u32 sge_num;

	skb = napi_alloc_skb(&rxq->irq_cfg->napi, HINIC3_RX_HDR_SIZE);
	if (unlikely(!skb))
		return NULL;

	sge_num = hinic3_get_sge_num(rxq, pkt_len);

	net_prefetchw(skb->data);
	packaging_skb(rxq, skb, sge_num, pkt_len);

	rxq->cons_idx += sge_num;
	rxq->delta += sge_num;

	return skb;
}

static void hinic3_pull_tail(struct sk_buff *skb)
{
	skb_frag_t *frag = &skb_shinfo(skb)->frags[0];
	unsigned int pull_len;
	unsigned char *va;

	va = skb_frag_address(frag);

	/* we need the header to contain the greater of either ETH_HLEN or
	 * 60 bytes if the skb->len is less than 60 for skb_pad.
	 */
	pull_len = eth_get_headlen(skb->dev, va, HINIC3_RX_HDR_SIZE);

	/* align pull length to size of long to optimize memcpy performance */
	skb_copy_to_linear_data(skb, va, ALIGN(pull_len, sizeof(long)));

	/* update all of the pointers */
	skb_frag_size_sub(frag, pull_len);
	skb_frag_off_add(frag, pull_len);

	skb->data_len -= pull_len;
	skb->tail += pull_len;
}

static void hinic3_rx_csum(struct hinic3_rxq *rxq, u32 offload_type,
			   u32 status, struct sk_buff *skb)
{
	u32 pkt_fmt = RQ_CQE_OFFOLAD_TYPE_GET(offload_type, TUNNEL_PKT_FORMAT);
	u32 pkt_type = RQ_CQE_OFFOLAD_TYPE_GET(offload_type, PKT_TYPE);
	u32 ip_type = RQ_CQE_OFFOLAD_TYPE_GET(offload_type, IP_TYPE);
	u32 csum_err = RQ_CQE_STATUS_GET(status, CSUM_ERR);
	struct net_device *netdev = rxq->netdev;

	if (!(netdev->features & NETIF_F_RXCSUM))
		return;

	if (unlikely(csum_err)) {
		/* pkt type is recognized by HW, and csum is wrong */
		skb->ip_summed = CHECKSUM_NONE;
		return;
	}

	if (ip_type == HINIC3_RX_INVALID_IP_TYPE ||
	    !(pkt_fmt == HINIC3_RX_PKT_FORMAT_NON_TUNNEL ||
	      pkt_fmt == HINIC3_RX_PKT_FORMAT_VXLAN)) {
		skb->ip_summed = CHECKSUM_NONE;
		return;
	}

	switch (pkt_type) {
	case HINIC3_RX_TCP_PKT:
	case HINIC3_RX_UDP_PKT:
	case HINIC3_RX_SCTP_PKT:
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		break;
	default:
		skb->ip_summed = CHECKSUM_NONE;
		break;
	}
}

static void hinic3_lro_set_gso_params(struct sk_buff *skb, u16 num_lro)
{
	struct ethhdr *eth = (struct ethhdr *)(skb->data);
	__be16 proto;

	proto = __vlan_get_protocol(skb, eth->h_proto, NULL);

	skb_shinfo(skb)->gso_size = DIV_ROUND_UP(skb->len - skb_headlen(skb),
						 num_lro);
	skb_shinfo(skb)->gso_type = proto == htons(ETH_P_IP) ?
				    SKB_GSO_TCPV4 : SKB_GSO_TCPV6;
	skb_shinfo(skb)->gso_segs = num_lro;
}

static int recv_one_pkt(struct hinic3_rxq *rxq, struct hinic3_rq_cqe *rx_cqe,
			u32 pkt_len, u32 vlan_len, u32 status)
{
	struct net_device *netdev = rxq->netdev;
	struct sk_buff *skb;
	u32 offload_type;
	u16 num_lro;

	skb = hinic3_fetch_rx_buffer(rxq, pkt_len);
	if (unlikely(!skb))
		return -ENOMEM;

	/* place header in linear portion of buffer */
	if (skb_is_nonlinear(skb))
		hinic3_pull_tail(skb);

	offload_type = le32_to_cpu(rx_cqe->offload_type);
	hinic3_rx_csum(rxq, offload_type, status, skb);

	num_lro = RQ_CQE_STATUS_GET(status, NUM_LRO);
	if (num_lro)
		hinic3_lro_set_gso_params(skb, num_lro);

	skb_record_rx_queue(skb, rxq->q_id);
	skb->protocol = eth_type_trans(skb, netdev);

	if (skb_has_frag_list(skb)) {
		napi_gro_flush(&rxq->irq_cfg->napi, false);
		netif_receive_skb(skb);
	} else {
		napi_gro_receive(&rxq->irq_cfg->napi, skb);
	}

	return 0;
}

int hinic3_alloc_rxqs_res(struct net_device *netdev, u16 num_rq,
			  u32 rq_depth, struct hinic3_dyna_rxq_res *rxqs_res)
{
	u64 cqe_mem_size = sizeof(struct hinic3_rq_cqe) * rq_depth;
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct page_pool_params pp_params = {};
	struct hinic3_dyna_rxq_res *rqres;
	u32 pkt_idx;
	int idx;

	for (idx = 0; idx < num_rq; idx++) {
		rqres = &rxqs_res[idx];
		rqres->rx_info = kcalloc(rq_depth, sizeof(*rqres->rx_info),
					 GFP_KERNEL);
		if (!rqres->rx_info)
			goto err_free_rqres;

		rqres->cqe_start_vaddr =
			dma_alloc_coherent(&nic_dev->pdev->dev, cqe_mem_size,
					   &rqres->cqe_start_paddr, GFP_KERNEL);
		if (!rqres->cqe_start_vaddr) {
			netdev_err(netdev, "Failed to alloc rxq%d rx cqe\n",
				   idx);
			goto err_free_rx_info;
		}

		pp_params.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV;
		pp_params.pool_size = rq_depth * nic_dev->rx_buf_len /
				      PAGE_SIZE;
		pp_params.nid = dev_to_node(&nic_dev->pdev->dev);
		pp_params.dev = &nic_dev->pdev->dev;
		pp_params.dma_dir = DMA_FROM_DEVICE;
		pp_params.max_len = PAGE_SIZE;
		rqres->page_pool = page_pool_create(&pp_params);
		if (IS_ERR(rqres->page_pool)) {
			netdev_err(netdev, "Failed to create rxq%d page pool\n",
				   idx);
			goto err_free_cqe;
		}

		pkt_idx = hinic3_alloc_rx_buffers(rqres, rq_depth,
						  nic_dev->rx_buf_len);
		if (!pkt_idx) {
			netdev_err(netdev, "Failed to alloc rxq%d rx buffers\n",
				   idx);
			goto err_destroy_page_pool;
		}
		rqres->next_to_alloc = pkt_idx;
	}

	return 0;

err_destroy_page_pool:
	page_pool_destroy(rqres->page_pool);
err_free_cqe:
	dma_free_coherent(&nic_dev->pdev->dev, cqe_mem_size,
			  rqres->cqe_start_vaddr,
			  rqres->cqe_start_paddr);
err_free_rx_info:
	kfree(rqres->rx_info);
err_free_rqres:
	hinic3_free_rxqs_res(netdev, idx, rq_depth, rxqs_res);

	return -ENOMEM;
}

void hinic3_free_rxqs_res(struct net_device *netdev, u16 num_rq,
			  u32 rq_depth, struct hinic3_dyna_rxq_res *rxqs_res)
{
	u64 cqe_mem_size = sizeof(struct hinic3_rq_cqe) * rq_depth;
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_dyna_rxq_res *rqres;
	int idx;

	for (idx = 0; idx < num_rq; idx++) {
		rqres = &rxqs_res[idx];

		hinic3_free_rx_buffers(rqres, rq_depth);
		page_pool_destroy(rqres->page_pool);
		dma_free_coherent(&nic_dev->pdev->dev, cqe_mem_size,
				  rqres->cqe_start_vaddr,
				  rqres->cqe_start_paddr);
		kfree(rqres->rx_info);
	}
}

int hinic3_configure_rxqs(struct net_device *netdev, u16 num_rq,
			  u32 rq_depth, struct hinic3_dyna_rxq_res *rxqs_res)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_dyna_rxq_res *rqres;
	struct msix_entry *msix_entry;
	struct hinic3_rxq *rxq;
	u16 q_id;
	u32 pkts;

	for (q_id = 0; q_id < num_rq; q_id++) {
		rxq = &nic_dev->rxqs[q_id];
		rqres = &rxqs_res[q_id];
		msix_entry = &nic_dev->qps_msix_entries[q_id];

		rxq->irq_id = msix_entry->vector;
		rxq->msix_entry_idx = msix_entry->entry;
		rxq->next_to_update = 0;
		rxq->next_to_alloc = rqres->next_to_alloc;
		rxq->q_depth = rq_depth;
		rxq->delta = rxq->q_depth;
		rxq->q_mask = rxq->q_depth - 1;
		rxq->cons_idx = 0;

		rxq->cqe_arr = rqres->cqe_start_vaddr;
		rxq->cqe_start_paddr = rqres->cqe_start_paddr;
		rxq->rx_info = rqres->rx_info;
		rxq->page_pool = rqres->page_pool;

		rxq->rq = &nic_dev->nic_io->rq[rxq->q_id];

		rq_associate_cqes(rxq);

		pkts = hinic3_rx_fill_buffers(rxq);
		if (!pkts) {
			netdev_err(netdev, "Failed to fill Rx buffer\n");
			return -ENOMEM;
		}
	}

	return 0;
}

int hinic3_rx_poll(struct hinic3_rxq *rxq, int budget)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(rxq->netdev);
	u32 sw_ci, status, pkt_len, vlan_len;
	struct hinic3_rq_cqe *rx_cqe;
	u32 num_wqe = 0;
	int nr_pkts = 0;
	u16 num_lro;

	while (likely(nr_pkts < budget)) {
		sw_ci = rxq->cons_idx & rxq->q_mask;
		rx_cqe = rxq->cqe_arr + sw_ci;
		status = le32_to_cpu(rx_cqe->status);
		if (!RQ_CQE_STATUS_GET(status, RXDONE))
			break;

		/* make sure we read rx_done before packet length */
		rmb();

		vlan_len = le32_to_cpu(rx_cqe->vlan_len);
		pkt_len = RQ_CQE_SGE_GET(vlan_len, LEN);
		if (recv_one_pkt(rxq, rx_cqe, pkt_len, vlan_len, status))
			break;

		nr_pkts++;
		num_lro = RQ_CQE_STATUS_GET(status, NUM_LRO);
		if (num_lro)
			num_wqe += hinic3_get_sge_num(rxq, pkt_len);

		rx_cqe->status = 0;

		if (num_wqe >= nic_dev->lro_replenish_thld)
			break;
	}

	if (rxq->delta >= HINIC3_RX_BUFFER_WRITE)
		hinic3_rx_fill_buffers(rxq);

	return nr_pkts;
}
