// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_vlan.h>
#include <net/ip6_checksum.h>

#include "ionic.h"
#include "ionic_lif.h"
#include "ionic_txrx.h"

static void ionic_rx_clean(struct ionic_queue *q,
			   struct ionic_desc_info *desc_info,
			   struct ionic_cq_info *cq_info,
			   void *cb_arg);

static bool ionic_rx_service(struct ionic_cq *cq, struct ionic_cq_info *cq_info);

static bool ionic_tx_service(struct ionic_cq *cq, struct ionic_cq_info *cq_info);

static inline void ionic_txq_post(struct ionic_queue *q, bool ring_dbell,
				  ionic_desc_cb cb_func, void *cb_arg)
{
	DEBUG_STATS_TXQ_POST(q, ring_dbell);

	ionic_q_post(q, ring_dbell, cb_func, cb_arg);
}

static inline void ionic_rxq_post(struct ionic_queue *q, bool ring_dbell,
				  ionic_desc_cb cb_func, void *cb_arg)
{
	ionic_q_post(q, ring_dbell, cb_func, cb_arg);

	DEBUG_STATS_RX_BUFF_CNT(q);
}

static inline struct netdev_queue *q_to_ndq(struct ionic_queue *q)
{
	return netdev_get_tx_queue(q->lif->netdev, q->index);
}

static struct sk_buff *ionic_rx_skb_alloc(struct ionic_queue *q,
					  unsigned int len, bool frags)
{
	struct ionic_lif *lif = q->lif;
	struct ionic_rx_stats *stats;
	struct net_device *netdev;
	struct sk_buff *skb;

	netdev = lif->netdev;
	stats = &q->lif->rxqstats[q->index];

	if (frags)
		skb = napi_get_frags(&q_to_qcq(q)->napi);
	else
		skb = netdev_alloc_skb_ip_align(netdev, len);

	if (unlikely(!skb)) {
		net_warn_ratelimited("%s: SKB alloc failed on %s!\n",
				     netdev->name, q->name);
		stats->alloc_err++;
		return NULL;
	}

	return skb;
}

static struct sk_buff *ionic_rx_frags(struct ionic_queue *q,
				      struct ionic_desc_info *desc_info,
				      struct ionic_cq_info *cq_info)
{
	struct ionic_rxq_comp *comp = cq_info->cq_desc;
	struct device *dev = q->lif->ionic->dev;
	struct ionic_page_info *page_info;
	struct sk_buff *skb;
	unsigned int i;
	u16 frag_len;
	u16 len;

	page_info = &desc_info->pages[0];
	len = le16_to_cpu(comp->len);

	prefetch(page_address(page_info->page) + NET_IP_ALIGN);

	skb = ionic_rx_skb_alloc(q, len, true);
	if (unlikely(!skb))
		return NULL;

	i = comp->num_sg_elems + 1;
	do {
		if (unlikely(!page_info->page)) {
			struct napi_struct *napi = &q_to_qcq(q)->napi;

			napi->skb = NULL;
			dev_kfree_skb(skb);
			return NULL;
		}

		frag_len = min(len, (u16)PAGE_SIZE);
		len -= frag_len;

		dma_unmap_page(dev, dma_unmap_addr(page_info, dma_addr),
			       PAGE_SIZE, DMA_FROM_DEVICE);
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
				page_info->page, 0, frag_len, PAGE_SIZE);
		page_info->page = NULL;
		page_info++;
		i--;
	} while (i > 0);

	return skb;
}

static struct sk_buff *ionic_rx_copybreak(struct ionic_queue *q,
					  struct ionic_desc_info *desc_info,
					  struct ionic_cq_info *cq_info)
{
	struct ionic_rxq_comp *comp = cq_info->cq_desc;
	struct device *dev = q->lif->ionic->dev;
	struct ionic_page_info *page_info;
	struct sk_buff *skb;
	u16 len;

	page_info = &desc_info->pages[0];
	len = le16_to_cpu(comp->len);

	skb = ionic_rx_skb_alloc(q, len, false);
	if (unlikely(!skb))
		return NULL;

	if (unlikely(!page_info->page)) {
		dev_kfree_skb(skb);
		return NULL;
	}

	dma_sync_single_for_cpu(dev, dma_unmap_addr(page_info, dma_addr),
				len, DMA_FROM_DEVICE);
	skb_copy_to_linear_data(skb, page_address(page_info->page), len);
	dma_sync_single_for_device(dev, dma_unmap_addr(page_info, dma_addr),
				   len, DMA_FROM_DEVICE);

	skb_put(skb, len);
	skb->protocol = eth_type_trans(skb, q->lif->netdev);

	return skb;
}

static void ionic_rx_clean(struct ionic_queue *q,
			   struct ionic_desc_info *desc_info,
			   struct ionic_cq_info *cq_info,
			   void *cb_arg)
{
	struct ionic_rxq_comp *comp = cq_info->cq_desc;
	struct ionic_qcq *qcq = q_to_qcq(q);
	struct ionic_rx_stats *stats;
	struct net_device *netdev;
	struct sk_buff *skb;

	stats = q_to_rx_stats(q);
	netdev = q->lif->netdev;

	if (comp->status) {
		stats->dropped++;
		return;
	}

	stats->pkts++;
	stats->bytes += le16_to_cpu(comp->len);

	if (le16_to_cpu(comp->len) <= q->lif->rx_copybreak)
		skb = ionic_rx_copybreak(q, desc_info, cq_info);
	else
		skb = ionic_rx_frags(q, desc_info, cq_info);

	if (unlikely(!skb)) {
		stats->dropped++;
		return;
	}

	skb_record_rx_queue(skb, q->index);

	if (likely(netdev->features & NETIF_F_RXHASH)) {
		switch (comp->pkt_type_color & IONIC_RXQ_COMP_PKT_TYPE_MASK) {
		case IONIC_PKT_TYPE_IPV4:
		case IONIC_PKT_TYPE_IPV6:
			skb_set_hash(skb, le32_to_cpu(comp->rss_hash),
				     PKT_HASH_TYPE_L3);
			break;
		case IONIC_PKT_TYPE_IPV4_TCP:
		case IONIC_PKT_TYPE_IPV6_TCP:
		case IONIC_PKT_TYPE_IPV4_UDP:
		case IONIC_PKT_TYPE_IPV6_UDP:
			skb_set_hash(skb, le32_to_cpu(comp->rss_hash),
				     PKT_HASH_TYPE_L4);
			break;
		}
	}

	if (likely(netdev->features & NETIF_F_RXCSUM)) {
		if (comp->csum_flags & IONIC_RXQ_COMP_CSUM_F_CALC) {
			skb->ip_summed = CHECKSUM_COMPLETE;
			skb->csum = (__force __wsum)le16_to_cpu(comp->csum);
			stats->csum_complete++;
		}
	} else {
		stats->csum_none++;
	}

	if (unlikely((comp->csum_flags & IONIC_RXQ_COMP_CSUM_F_TCP_BAD) ||
		     (comp->csum_flags & IONIC_RXQ_COMP_CSUM_F_UDP_BAD) ||
		     (comp->csum_flags & IONIC_RXQ_COMP_CSUM_F_IP_BAD)))
		stats->csum_error++;

	if (likely(netdev->features & NETIF_F_HW_VLAN_CTAG_RX) &&
	    (comp->csum_flags & IONIC_RXQ_COMP_CSUM_F_VLAN)) {
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       le16_to_cpu(comp->vlan_tci));
		stats->vlan_stripped++;
	}

	if (le16_to_cpu(comp->len) <= q->lif->rx_copybreak)
		napi_gro_receive(&qcq->napi, skb);
	else
		napi_gro_frags(&qcq->napi);
}

static bool ionic_rx_service(struct ionic_cq *cq, struct ionic_cq_info *cq_info)
{
	struct ionic_rxq_comp *comp = cq_info->cq_desc;
	struct ionic_queue *q = cq->bound_q;
	struct ionic_desc_info *desc_info;

	if (!color_match(comp->pkt_type_color, cq->done_color))
		return false;

	/* check for empty queue */
	if (q->tail_idx == q->head_idx)
		return false;

	if (q->tail_idx != le16_to_cpu(comp->comp_index))
		return false;

	desc_info = &q->info[q->tail_idx];
	q->tail_idx = (q->tail_idx + 1) & (q->num_descs - 1);

	/* clean the related q entry, only one per qc completion */
	ionic_rx_clean(q, desc_info, cq_info, desc_info->cb_arg);

	desc_info->cb = NULL;
	desc_info->cb_arg = NULL;

	return true;
}

static int ionic_rx_page_alloc(struct ionic_queue *q,
			       struct ionic_page_info *page_info)
{
	struct ionic_lif *lif = q->lif;
	struct ionic_rx_stats *stats;
	struct net_device *netdev;
	struct device *dev;

	netdev = lif->netdev;
	dev = lif->ionic->dev;
	stats = q_to_rx_stats(q);

	if (unlikely(!page_info)) {
		net_err_ratelimited("%s: %s invalid page_info in alloc\n",
				    netdev->name, q->name);
		return -EINVAL;
	}

	page_info->page = dev_alloc_page();
	if (unlikely(!page_info->page)) {
		net_err_ratelimited("%s: %s page alloc failed\n",
				    netdev->name, q->name);
		stats->alloc_err++;
		return -ENOMEM;
	}

	page_info->dma_addr = dma_map_page(dev, page_info->page, 0, PAGE_SIZE,
					   DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(dev, page_info->dma_addr))) {
		put_page(page_info->page);
		page_info->dma_addr = 0;
		page_info->page = NULL;
		net_err_ratelimited("%s: %s dma map failed\n",
				    netdev->name, q->name);
		stats->dma_map_err++;
		return -EIO;
	}

	return 0;
}

static void ionic_rx_page_free(struct ionic_queue *q,
			       struct ionic_page_info *page_info)
{
	struct ionic_lif *lif = q->lif;
	struct net_device *netdev;
	struct device *dev;

	netdev = lif->netdev;
	dev = lif->ionic->dev;

	if (unlikely(!page_info)) {
		net_err_ratelimited("%s: %s invalid page_info in free\n",
				    netdev->name, q->name);
		return;
	}

	if (unlikely(!page_info->page)) {
		net_err_ratelimited("%s: %s invalid page in free\n",
				    netdev->name, q->name);
		return;
	}

	dma_unmap_page(dev, page_info->dma_addr, PAGE_SIZE, DMA_FROM_DEVICE);

	put_page(page_info->page);
	page_info->dma_addr = 0;
	page_info->page = NULL;
}

void ionic_rx_fill(struct ionic_queue *q)
{
	struct net_device *netdev = q->lif->netdev;
	struct ionic_desc_info *desc_info;
	struct ionic_page_info *page_info;
	struct ionic_rxq_sg_desc *sg_desc;
	struct ionic_rxq_sg_elem *sg_elem;
	struct ionic_rxq_desc *desc;
	unsigned int remain_len;
	unsigned int seg_len;
	unsigned int nfrags;
	unsigned int i, j;
	unsigned int len;

	len = netdev->mtu + ETH_HLEN + VLAN_HLEN;
	nfrags = round_up(len, PAGE_SIZE) / PAGE_SIZE;

	for (i = ionic_q_space_avail(q); i; i--) {
		remain_len = len;
		desc_info = &q->info[q->head_idx];
		desc = desc_info->desc;
		sg_desc = desc_info->sg_desc;
		page_info = &desc_info->pages[0];

		if (page_info->page) { /* recycle the buffer */
			ionic_rxq_post(q, false, ionic_rx_clean, NULL);
			continue;
		}

		/* fill main descriptor - pages[0] */
		desc->opcode = (nfrags > 1) ? IONIC_RXQ_DESC_OPCODE_SG :
					      IONIC_RXQ_DESC_OPCODE_SIMPLE;
		desc_info->npages = nfrags;
		if (unlikely(ionic_rx_page_alloc(q, page_info))) {
			desc->addr = 0;
			desc->len = 0;
			return;
		}
		desc->addr = cpu_to_le64(page_info->dma_addr);
		seg_len = min_t(unsigned int, PAGE_SIZE, len);
		desc->len = cpu_to_le16(seg_len);
		remain_len -= seg_len;
		page_info++;

		/* fill sg descriptors - pages[1..n] */
		for (j = 0; j < nfrags - 1; j++) {
			if (page_info->page) /* recycle the sg buffer */
				continue;

			sg_elem = &sg_desc->elems[j];
			if (unlikely(ionic_rx_page_alloc(q, page_info))) {
				sg_elem->addr = 0;
				sg_elem->len = 0;
				return;
			}
			sg_elem->addr = cpu_to_le64(page_info->dma_addr);
			seg_len = min_t(unsigned int, PAGE_SIZE, remain_len);
			sg_elem->len = cpu_to_le16(seg_len);
			remain_len -= seg_len;
			page_info++;
		}

		ionic_rxq_post(q, false, ionic_rx_clean, NULL);
	}

	ionic_dbell_ring(q->lif->kern_dbpage, q->hw_type,
			 q->dbval | q->head_idx);
}

static void ionic_rx_fill_cb(void *arg)
{
	ionic_rx_fill(arg);
}

void ionic_rx_empty(struct ionic_queue *q)
{
	struct ionic_desc_info *desc_info;
	struct ionic_page_info *page_info;
	unsigned int i, j;

	for (i = 0; i < q->num_descs; i++) {
		desc_info = &q->info[i];
		for (j = 0; j < IONIC_RX_MAX_SG_ELEMS + 1; j++) {
			page_info = &desc_info->pages[j];
			if (page_info->page)
				ionic_rx_page_free(q, page_info);
		}

		desc_info->npages = 0;
		desc_info->cb = NULL;
		desc_info->cb_arg = NULL;
	}
}

static void ionic_dim_update(struct ionic_qcq *qcq, int napi_mode)
{
	struct dim_sample dim_sample;
	struct ionic_lif *lif;
	unsigned int qi;
	u64 pkts, bytes;

	if (!qcq->intr.dim_coal_hw)
		return;

	lif = qcq->q.lif;
	qi = qcq->cq.bound_q->index;

	switch (napi_mode) {
	case IONIC_LIF_F_TX_DIM_INTR:
		pkts = lif->txqstats[qi].pkts;
		bytes = lif->txqstats[qi].bytes;
		break;
	case IONIC_LIF_F_RX_DIM_INTR:
		pkts = lif->rxqstats[qi].pkts;
		bytes = lif->rxqstats[qi].bytes;
		break;
	default:
		pkts = lif->txqstats[qi].pkts + lif->rxqstats[qi].pkts;
		bytes = lif->txqstats[qi].bytes + lif->rxqstats[qi].bytes;
		break;
	}

	dim_update_sample(qcq->cq.bound_intr->rearm_count,
			  pkts, bytes, &dim_sample);

	net_dim(&qcq->dim, dim_sample);
}

int ionic_tx_napi(struct napi_struct *napi, int budget)
{
	struct ionic_qcq *qcq = napi_to_qcq(napi);
	struct ionic_cq *cq = napi_to_cq(napi);
	struct ionic_dev *idev;
	struct ionic_lif *lif;
	u32 work_done = 0;
	u32 flags = 0;

	lif = cq->bound_q->lif;
	idev = &lif->ionic->idev;

	work_done = ionic_cq_service(cq, budget,
				     ionic_tx_service, NULL, NULL);

	if (work_done < budget && napi_complete_done(napi, work_done)) {
		ionic_dim_update(qcq, IONIC_LIF_F_TX_DIM_INTR);
		flags |= IONIC_INTR_CRED_UNMASK;
		cq->bound_intr->rearm_count++;
	}

	if (work_done || flags) {
		flags |= IONIC_INTR_CRED_RESET_COALESCE;
		ionic_intr_credits(idev->intr_ctrl,
				   cq->bound_intr->index,
				   work_done, flags);
	}

	DEBUG_STATS_NAPI_POLL(qcq, work_done);

	return work_done;
}

int ionic_rx_napi(struct napi_struct *napi, int budget)
{
	struct ionic_qcq *qcq = napi_to_qcq(napi);
	struct ionic_cq *cq = napi_to_cq(napi);
	struct ionic_dev *idev;
	struct ionic_lif *lif;
	u32 work_done = 0;
	u32 flags = 0;

	lif = cq->bound_q->lif;
	idev = &lif->ionic->idev;

	work_done = ionic_cq_service(cq, budget,
				     ionic_rx_service, NULL, NULL);

	if (work_done)
		ionic_rx_fill(cq->bound_q);

	if (work_done < budget && napi_complete_done(napi, work_done)) {
		ionic_dim_update(qcq, IONIC_LIF_F_RX_DIM_INTR);
		flags |= IONIC_INTR_CRED_UNMASK;
		cq->bound_intr->rearm_count++;
	}

	if (work_done || flags) {
		flags |= IONIC_INTR_CRED_RESET_COALESCE;
		ionic_intr_credits(idev->intr_ctrl,
				   cq->bound_intr->index,
				   work_done, flags);
	}

	DEBUG_STATS_NAPI_POLL(qcq, work_done);

	return work_done;
}

int ionic_txrx_napi(struct napi_struct *napi, int budget)
{
	struct ionic_qcq *qcq = napi_to_qcq(napi);
	struct ionic_cq *rxcq = napi_to_cq(napi);
	unsigned int qi = rxcq->bound_q->index;
	struct ionic_dev *idev;
	struct ionic_lif *lif;
	struct ionic_cq *txcq;
	u32 rx_work_done = 0;
	u32 tx_work_done = 0;
	u32 flags = 0;

	lif = rxcq->bound_q->lif;
	idev = &lif->ionic->idev;
	txcq = &lif->txqcqs[qi]->cq;

	tx_work_done = ionic_cq_service(txcq, lif->tx_budget,
					ionic_tx_service, NULL, NULL);

	rx_work_done = ionic_cq_service(rxcq, budget,
					ionic_rx_service, NULL, NULL);
	if (rx_work_done)
		ionic_rx_fill_cb(rxcq->bound_q);

	if (rx_work_done < budget && napi_complete_done(napi, rx_work_done)) {
		ionic_dim_update(qcq, 0);
		flags |= IONIC_INTR_CRED_UNMASK;
		rxcq->bound_intr->rearm_count++;
	}

	if (rx_work_done || flags) {
		flags |= IONIC_INTR_CRED_RESET_COALESCE;
		ionic_intr_credits(idev->intr_ctrl, rxcq->bound_intr->index,
				   tx_work_done + rx_work_done, flags);
	}

	DEBUG_STATS_NAPI_POLL(qcq, rx_work_done);
	DEBUG_STATS_NAPI_POLL(qcq, tx_work_done);

	return rx_work_done;
}

static dma_addr_t ionic_tx_map_single(struct ionic_queue *q,
				      void *data, size_t len)
{
	struct ionic_tx_stats *stats = q_to_tx_stats(q);
	struct device *dev = q->lif->ionic->dev;
	dma_addr_t dma_addr;

	dma_addr = dma_map_single(dev, data, len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_addr)) {
		net_warn_ratelimited("%s: DMA single map failed on %s!\n",
				     q->lif->netdev->name, q->name);
		stats->dma_map_err++;
		return 0;
	}
	return dma_addr;
}

static dma_addr_t ionic_tx_map_frag(struct ionic_queue *q,
				    const skb_frag_t *frag,
				    size_t offset, size_t len)
{
	struct ionic_tx_stats *stats = q_to_tx_stats(q);
	struct device *dev = q->lif->ionic->dev;
	dma_addr_t dma_addr;

	dma_addr = skb_frag_dma_map(dev, frag, offset, len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_addr)) {
		net_warn_ratelimited("%s: DMA frag map failed on %s!\n",
				     q->lif->netdev->name, q->name);
		stats->dma_map_err++;
	}
	return dma_addr;
}

static void ionic_tx_clean(struct ionic_queue *q,
			   struct ionic_desc_info *desc_info,
			   struct ionic_cq_info *cq_info,
			   void *cb_arg)
{
	struct ionic_txq_sg_desc *sg_desc = desc_info->sg_desc;
	struct ionic_txq_sg_elem *elem = sg_desc->elems;
	struct ionic_tx_stats *stats = q_to_tx_stats(q);
	struct ionic_txq_desc *desc = desc_info->desc;
	struct device *dev = q->lif->ionic->dev;
	u8 opcode, flags, nsge;
	u16 queue_index;
	unsigned int i;
	u64 addr;

	decode_txq_desc_cmd(le64_to_cpu(desc->cmd),
			    &opcode, &flags, &nsge, &addr);

	/* use unmap_single only if either this is not TSO,
	 * or this is first descriptor of a TSO
	 */
	if (opcode != IONIC_TXQ_DESC_OPCODE_TSO ||
	    flags & IONIC_TXQ_DESC_FLAG_TSO_SOT)
		dma_unmap_single(dev, (dma_addr_t)addr,
				 le16_to_cpu(desc->len), DMA_TO_DEVICE);
	else
		dma_unmap_page(dev, (dma_addr_t)addr,
			       le16_to_cpu(desc->len), DMA_TO_DEVICE);

	for (i = 0; i < nsge; i++, elem++)
		dma_unmap_page(dev, (dma_addr_t)le64_to_cpu(elem->addr),
			       le16_to_cpu(elem->len), DMA_TO_DEVICE);

	if (cb_arg) {
		struct sk_buff *skb = cb_arg;
		u32 len = skb->len;

		queue_index = skb_get_queue_mapping(skb);
		if (unlikely(__netif_subqueue_stopped(q->lif->netdev,
						      queue_index))) {
			netif_wake_subqueue(q->lif->netdev, queue_index);
			q->wake++;
		}
		dev_kfree_skb_any(skb);
		stats->clean++;
		netdev_tx_completed_queue(q_to_ndq(q), 1, len);
	}
}

static bool ionic_tx_service(struct ionic_cq *cq, struct ionic_cq_info *cq_info)
{
	struct ionic_txq_comp *comp = cq_info->cq_desc;
	struct ionic_queue *q = cq->bound_q;
	struct ionic_desc_info *desc_info;
	u16 index;

	if (!color_match(comp->color, cq->done_color))
		return false;

	/* clean the related q entries, there could be
	 * several q entries completed for each cq completion
	 */
	do {
		desc_info = &q->info[q->tail_idx];
		index = q->tail_idx;
		q->tail_idx = (q->tail_idx + 1) & (q->num_descs - 1);
		ionic_tx_clean(q, desc_info, cq_info, desc_info->cb_arg);
		desc_info->cb = NULL;
		desc_info->cb_arg = NULL;
	} while (index != le16_to_cpu(comp->comp_index));

	return true;
}

void ionic_tx_flush(struct ionic_cq *cq)
{
	struct ionic_dev *idev = &cq->lif->ionic->idev;
	u32 work_done;

	work_done = ionic_cq_service(cq, cq->num_descs,
				     ionic_tx_service, NULL, NULL);
	if (work_done)
		ionic_intr_credits(idev->intr_ctrl, cq->bound_intr->index,
				   work_done, IONIC_INTR_CRED_RESET_COALESCE);
}

void ionic_tx_empty(struct ionic_queue *q)
{
	struct ionic_desc_info *desc_info;

	/* walk the not completed tx entries, if any */
	while (q->head_idx != q->tail_idx) {
		desc_info = &q->info[q->tail_idx];
		q->tail_idx = (q->tail_idx + 1) & (q->num_descs - 1);
		ionic_tx_clean(q, desc_info, NULL, desc_info->cb_arg);
		desc_info->cb = NULL;
		desc_info->cb_arg = NULL;
	}
}

static int ionic_tx_tcp_inner_pseudo_csum(struct sk_buff *skb)
{
	int err;

	err = skb_cow_head(skb, 0);
	if (err)
		return err;

	if (skb->protocol == cpu_to_be16(ETH_P_IP)) {
		inner_ip_hdr(skb)->check = 0;
		inner_tcp_hdr(skb)->check =
			~csum_tcpudp_magic(inner_ip_hdr(skb)->saddr,
					   inner_ip_hdr(skb)->daddr,
					   0, IPPROTO_TCP, 0);
	} else if (skb->protocol == cpu_to_be16(ETH_P_IPV6)) {
		inner_tcp_hdr(skb)->check =
			~csum_ipv6_magic(&inner_ipv6_hdr(skb)->saddr,
					 &inner_ipv6_hdr(skb)->daddr,
					 0, IPPROTO_TCP, 0);
	}

	return 0;
}

static int ionic_tx_tcp_pseudo_csum(struct sk_buff *skb)
{
	int err;

	err = skb_cow_head(skb, 0);
	if (err)
		return err;

	if (skb->protocol == cpu_to_be16(ETH_P_IP)) {
		ip_hdr(skb)->check = 0;
		tcp_hdr(skb)->check =
			~csum_tcpudp_magic(ip_hdr(skb)->saddr,
					   ip_hdr(skb)->daddr,
					   0, IPPROTO_TCP, 0);
	} else if (skb->protocol == cpu_to_be16(ETH_P_IPV6)) {
		tcp_v6_gso_csum_prep(skb);
	}

	return 0;
}

static void ionic_tx_tso_post(struct ionic_queue *q, struct ionic_txq_desc *desc,
			      struct sk_buff *skb,
			      dma_addr_t addr, u8 nsge, u16 len,
			      unsigned int hdrlen, unsigned int mss,
			      bool outer_csum,
			      u16 vlan_tci, bool has_vlan,
			      bool start, bool done)
{
	u8 flags = 0;
	u64 cmd;

	flags |= has_vlan ? IONIC_TXQ_DESC_FLAG_VLAN : 0;
	flags |= outer_csum ? IONIC_TXQ_DESC_FLAG_ENCAP : 0;
	flags |= start ? IONIC_TXQ_DESC_FLAG_TSO_SOT : 0;
	flags |= done ? IONIC_TXQ_DESC_FLAG_TSO_EOT : 0;

	cmd = encode_txq_desc_cmd(IONIC_TXQ_DESC_OPCODE_TSO, flags, nsge, addr);
	desc->cmd = cpu_to_le64(cmd);
	desc->len = cpu_to_le16(len);
	desc->vlan_tci = cpu_to_le16(vlan_tci);
	desc->hdr_len = cpu_to_le16(hdrlen);
	desc->mss = cpu_to_le16(mss);

	if (done) {
		skb_tx_timestamp(skb);
		netdev_tx_sent_queue(q_to_ndq(q), skb->len);
		ionic_txq_post(q, !netdev_xmit_more(), ionic_tx_clean, skb);
	} else {
		ionic_txq_post(q, false, ionic_tx_clean, NULL);
	}
}

static struct ionic_txq_desc *ionic_tx_tso_next(struct ionic_queue *q,
						struct ionic_txq_sg_elem **elem)
{
	struct ionic_txq_sg_desc *sg_desc = q->info[q->head_idx].txq_sg_desc;
	struct ionic_txq_desc *desc = q->info[q->head_idx].txq_desc;

	*elem = sg_desc->elems;
	return desc;
}

static int ionic_tx_tso(struct ionic_queue *q, struct sk_buff *skb)
{
	struct ionic_tx_stats *stats = q_to_tx_stats(q);
	struct ionic_desc_info *rewind_desc_info;
	struct device *dev = q->lif->ionic->dev;
	struct ionic_txq_sg_elem *elem;
	struct ionic_txq_desc *desc;
	unsigned int frag_left = 0;
	unsigned int offset = 0;
	u16 abort = q->head_idx;
	unsigned int len_left;
	dma_addr_t desc_addr;
	unsigned int hdrlen;
	unsigned int nfrags;
	unsigned int seglen;
	u64 total_bytes = 0;
	u64 total_pkts = 0;
	u16 rewind = abort;
	unsigned int left;
	unsigned int len;
	unsigned int mss;
	skb_frag_t *frag;
	bool start, done;
	bool outer_csum;
	dma_addr_t addr;
	bool has_vlan;
	u16 desc_len;
	u8 desc_nsge;
	u16 vlan_tci;
	bool encap;
	int err;

	mss = skb_shinfo(skb)->gso_size;
	nfrags = skb_shinfo(skb)->nr_frags;
	len_left = skb->len - skb_headlen(skb);
	outer_csum = (skb_shinfo(skb)->gso_type & SKB_GSO_GRE_CSUM) ||
		     (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_TUNNEL_CSUM);
	has_vlan = !!skb_vlan_tag_present(skb);
	vlan_tci = skb_vlan_tag_get(skb);
	encap = skb->encapsulation;

	/* Preload inner-most TCP csum field with IP pseudo hdr
	 * calculated with IP length set to zero.  HW will later
	 * add in length to each TCP segment resulting from the TSO.
	 */

	if (encap)
		err = ionic_tx_tcp_inner_pseudo_csum(skb);
	else
		err = ionic_tx_tcp_pseudo_csum(skb);
	if (err)
		return err;

	if (encap)
		hdrlen = skb_inner_transport_header(skb) - skb->data +
			 inner_tcp_hdrlen(skb);
	else
		hdrlen = skb_transport_offset(skb) + tcp_hdrlen(skb);

	seglen = hdrlen + mss;
	left = skb_headlen(skb);

	desc = ionic_tx_tso_next(q, &elem);
	start = true;

	/* Chop skb->data up into desc segments */

	while (left > 0) {
		len = min(seglen, left);
		frag_left = seglen - len;
		desc_addr = ionic_tx_map_single(q, skb->data + offset, len);
		if (dma_mapping_error(dev, desc_addr))
			goto err_out_abort;
		desc_len = len;
		desc_nsge = 0;
		left -= len;
		offset += len;
		if (nfrags > 0 && frag_left > 0)
			continue;
		done = (nfrags == 0 && left == 0);
		ionic_tx_tso_post(q, desc, skb,
				  desc_addr, desc_nsge, desc_len,
				  hdrlen, mss,
				  outer_csum,
				  vlan_tci, has_vlan,
				  start, done);
		total_pkts++;
		total_bytes += start ? len : len + hdrlen;
		desc = ionic_tx_tso_next(q, &elem);
		start = false;
		seglen = mss;
	}

	/* Chop skb frags into desc segments */

	for (frag = skb_shinfo(skb)->frags; len_left; frag++) {
		offset = 0;
		left = skb_frag_size(frag);
		len_left -= left;
		nfrags--;
		stats->frags++;

		while (left > 0) {
			if (frag_left > 0) {
				len = min(frag_left, left);
				frag_left -= len;
				addr = ionic_tx_map_frag(q, frag, offset, len);
				if (dma_mapping_error(dev, addr))
					goto err_out_abort;
				elem->addr = cpu_to_le64(addr);
				elem->len = cpu_to_le16(len);
				elem++;
				desc_nsge++;
				left -= len;
				offset += len;
				if (nfrags > 0 && frag_left > 0)
					continue;
				done = (nfrags == 0 && left == 0);
				ionic_tx_tso_post(q, desc, skb, desc_addr,
						  desc_nsge, desc_len,
						  hdrlen, mss, outer_csum,
						  vlan_tci, has_vlan,
						  start, done);
				total_pkts++;
				total_bytes += start ? len : len + hdrlen;
				desc = ionic_tx_tso_next(q, &elem);
				start = false;
			} else {
				len = min(mss, left);
				frag_left = mss - len;
				desc_addr = ionic_tx_map_frag(q, frag,
							      offset, len);
				if (dma_mapping_error(dev, desc_addr))
					goto err_out_abort;
				desc_len = len;
				desc_nsge = 0;
				left -= len;
				offset += len;
				if (nfrags > 0 && frag_left > 0)
					continue;
				done = (nfrags == 0 && left == 0);
				ionic_tx_tso_post(q, desc, skb, desc_addr,
						  desc_nsge, desc_len,
						  hdrlen, mss, outer_csum,
						  vlan_tci, has_vlan,
						  start, done);
				total_pkts++;
				total_bytes += start ? len : len + hdrlen;
				desc = ionic_tx_tso_next(q, &elem);
				start = false;
			}
		}
	}

	stats->pkts += total_pkts;
	stats->bytes += total_bytes;
	stats->tso++;
	stats->tso_bytes += total_bytes;

	return 0;

err_out_abort:
	while (rewind != q->head_idx) {
		rewind_desc_info = &q->info[rewind];
		ionic_tx_clean(q, rewind_desc_info, NULL, NULL);
		rewind = (rewind + 1) & (q->num_descs - 1);
	}
	q->head_idx = abort;

	return -ENOMEM;
}

static int ionic_tx_calc_csum(struct ionic_queue *q, struct sk_buff *skb)
{
	struct ionic_txq_desc *desc = q->info[q->head_idx].txq_desc;
	struct ionic_tx_stats *stats = q_to_tx_stats(q);
	struct device *dev = q->lif->ionic->dev;
	dma_addr_t dma_addr;
	bool has_vlan;
	u8 flags = 0;
	bool encap;
	u64 cmd;

	has_vlan = !!skb_vlan_tag_present(skb);
	encap = skb->encapsulation;

	dma_addr = ionic_tx_map_single(q, skb->data, skb_headlen(skb));
	if (dma_mapping_error(dev, dma_addr))
		return -ENOMEM;

	flags |= has_vlan ? IONIC_TXQ_DESC_FLAG_VLAN : 0;
	flags |= encap ? IONIC_TXQ_DESC_FLAG_ENCAP : 0;

	cmd = encode_txq_desc_cmd(IONIC_TXQ_DESC_OPCODE_CSUM_PARTIAL,
				  flags, skb_shinfo(skb)->nr_frags, dma_addr);
	desc->cmd = cpu_to_le64(cmd);
	desc->len = cpu_to_le16(skb_headlen(skb));
	desc->csum_start = cpu_to_le16(skb_checksum_start_offset(skb));
	desc->csum_offset = cpu_to_le16(skb->csum_offset);
	if (has_vlan) {
		desc->vlan_tci = cpu_to_le16(skb_vlan_tag_get(skb));
		stats->vlan_inserted++;
	}

	if (skb->csum_not_inet)
		stats->crc32_csum++;
	else
		stats->csum++;

	return 0;
}

static int ionic_tx_calc_no_csum(struct ionic_queue *q, struct sk_buff *skb)
{
	struct ionic_txq_desc *desc = q->info[q->head_idx].txq_desc;
	struct ionic_tx_stats *stats = q_to_tx_stats(q);
	struct device *dev = q->lif->ionic->dev;
	dma_addr_t dma_addr;
	bool has_vlan;
	u8 flags = 0;
	bool encap;
	u64 cmd;

	has_vlan = !!skb_vlan_tag_present(skb);
	encap = skb->encapsulation;

	dma_addr = ionic_tx_map_single(q, skb->data, skb_headlen(skb));
	if (dma_mapping_error(dev, dma_addr))
		return -ENOMEM;

	flags |= has_vlan ? IONIC_TXQ_DESC_FLAG_VLAN : 0;
	flags |= encap ? IONIC_TXQ_DESC_FLAG_ENCAP : 0;

	cmd = encode_txq_desc_cmd(IONIC_TXQ_DESC_OPCODE_CSUM_NONE,
				  flags, skb_shinfo(skb)->nr_frags, dma_addr);
	desc->cmd = cpu_to_le64(cmd);
	desc->len = cpu_to_le16(skb_headlen(skb));
	if (has_vlan) {
		desc->vlan_tci = cpu_to_le16(skb_vlan_tag_get(skb));
		stats->vlan_inserted++;
	}

	stats->csum_none++;

	return 0;
}

static int ionic_tx_skb_frags(struct ionic_queue *q, struct sk_buff *skb)
{
	struct ionic_txq_sg_desc *sg_desc = q->info[q->head_idx].txq_sg_desc;
	unsigned int len_left = skb->len - skb_headlen(skb);
	struct ionic_txq_sg_elem *elem = sg_desc->elems;
	struct ionic_tx_stats *stats = q_to_tx_stats(q);
	struct device *dev = q->lif->ionic->dev;
	dma_addr_t dma_addr;
	skb_frag_t *frag;
	u16 len;

	for (frag = skb_shinfo(skb)->frags; len_left; frag++, elem++) {
		len = skb_frag_size(frag);
		elem->len = cpu_to_le16(len);
		dma_addr = ionic_tx_map_frag(q, frag, 0, len);
		if (dma_mapping_error(dev, dma_addr))
			return -ENOMEM;
		elem->addr = cpu_to_le64(dma_addr);
		len_left -= len;
		stats->frags++;
	}

	return 0;
}

static int ionic_tx(struct ionic_queue *q, struct sk_buff *skb)
{
	struct ionic_tx_stats *stats = q_to_tx_stats(q);
	int err;

	/* set up the initial descriptor */
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		err = ionic_tx_calc_csum(q, skb);
	else
		err = ionic_tx_calc_no_csum(q, skb);
	if (err)
		return err;

	/* add frags */
	err = ionic_tx_skb_frags(q, skb);
	if (err)
		return err;

	skb_tx_timestamp(skb);
	stats->pkts++;
	stats->bytes += skb->len;

	netdev_tx_sent_queue(q_to_ndq(q), skb->len);
	ionic_txq_post(q, !netdev_xmit_more(), ionic_tx_clean, skb);

	return 0;
}

static int ionic_tx_descs_needed(struct ionic_queue *q, struct sk_buff *skb)
{
	int sg_elems = q->lif->qtype_info[IONIC_QTYPE_TXQ].max_sg_elems;
	struct ionic_tx_stats *stats = q_to_tx_stats(q);
	int ndescs;
	int err;

	/* Each desc is mss long max, so a descriptor for each gso_seg */
	if (skb_is_gso(skb))
		ndescs = skb_shinfo(skb)->gso_segs;
	else
		ndescs = 1;

	if (skb_shinfo(skb)->nr_frags <= sg_elems)
		return ndescs;

	/* Too many frags, so linearize */
	err = skb_linearize(skb);
	if (err)
		return err;

	stats->linearize++;

	return ndescs;
}

static int ionic_maybe_stop_tx(struct ionic_queue *q, int ndescs)
{
	int stopped = 0;

	if (unlikely(!ionic_q_has_space(q, ndescs))) {
		netif_stop_subqueue(q->lif->netdev, q->index);
		q->stop++;
		stopped = 1;

		/* Might race with ionic_tx_clean, check again */
		smp_rmb();
		if (ionic_q_has_space(q, ndescs)) {
			netif_wake_subqueue(q->lif->netdev, q->index);
			stopped = 0;
		}
	}

	return stopped;
}

netdev_tx_t ionic_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	u16 queue_index = skb_get_queue_mapping(skb);
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_queue *q;
	int ndescs;
	int err;

	if (unlikely(!test_bit(IONIC_LIF_F_UP, lif->state))) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	if (unlikely(queue_index >= lif->nxqs))
		queue_index = 0;
	q = &lif->txqcqs[queue_index]->q;

	ndescs = ionic_tx_descs_needed(q, skb);
	if (ndescs < 0)
		goto err_out_drop;

	if (unlikely(ionic_maybe_stop_tx(q, ndescs)))
		return NETDEV_TX_BUSY;

	if (skb_is_gso(skb))
		err = ionic_tx_tso(q, skb);
	else
		err = ionic_tx(q, skb);

	if (err)
		goto err_out_drop;

	/* Stop the queue if there aren't descriptors for the next packet.
	 * Since our SG lists per descriptor take care of most of the possible
	 * fragmentation, we don't need to have many descriptors available.
	 */
	ionic_maybe_stop_tx(q, 4);

	return NETDEV_TX_OK;

err_out_drop:
	q->stop++;
	q->drop++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}
