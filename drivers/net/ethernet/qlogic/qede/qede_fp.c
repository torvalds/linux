/* QLogic qede NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bpf_trace.h>
#include <net/udp_tunnel.h>
#include <linux/ip.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <net/ip6_checksum.h>
#include "qede_ptp.h"

#include <linux/qed/qed_if.h>
#include "qede.h"
/*********************************
 * Content also used by slowpath *
 *********************************/

int qede_alloc_rx_buffer(struct qede_rx_queue *rxq, bool allow_lazy)
{
	struct sw_rx_data *sw_rx_data;
	struct eth_rx_bd *rx_bd;
	dma_addr_t mapping;
	struct page *data;

	/* In case lazy-allocation is allowed, postpone allocation until the
	 * end of the NAPI run. We'd still need to make sure the Rx ring has
	 * sufficient buffers to guarantee an additional Rx interrupt.
	 */
	if (allow_lazy && likely(rxq->filled_buffers > 12)) {
		rxq->filled_buffers--;
		return 0;
	}

	data = alloc_pages(GFP_ATOMIC, 0);
	if (unlikely(!data))
		return -ENOMEM;

	/* Map the entire page as it would be used
	 * for multiple RX buffer segment size mapping.
	 */
	mapping = dma_map_page(rxq->dev, data, 0,
			       PAGE_SIZE, rxq->data_direction);
	if (unlikely(dma_mapping_error(rxq->dev, mapping))) {
		__free_page(data);
		return -ENOMEM;
	}

	sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_prod & NUM_RX_BDS_MAX];
	sw_rx_data->page_offset = 0;
	sw_rx_data->data = data;
	sw_rx_data->mapping = mapping;

	/* Advance PROD and get BD pointer */
	rx_bd = (struct eth_rx_bd *)qed_chain_produce(&rxq->rx_bd_ring);
	WARN_ON(!rx_bd);
	rx_bd->addr.hi = cpu_to_le32(upper_32_bits(mapping));
	rx_bd->addr.lo = cpu_to_le32(lower_32_bits(mapping) +
				     rxq->rx_headroom);

	rxq->sw_rx_prod++;
	rxq->filled_buffers++;

	return 0;
}

/* Unmap the data and free skb */
int qede_free_tx_pkt(struct qede_dev *edev, struct qede_tx_queue *txq, int *len)
{
	u16 idx = txq->sw_tx_cons;
	struct sk_buff *skb = txq->sw_tx_ring.skbs[idx].skb;
	struct eth_tx_1st_bd *first_bd;
	struct eth_tx_bd *tx_data_bd;
	int bds_consumed = 0;
	int nbds;
	bool data_split = txq->sw_tx_ring.skbs[idx].flags & QEDE_TSO_SPLIT_BD;
	int i, split_bd_len = 0;

	if (unlikely(!skb)) {
		DP_ERR(edev,
		       "skb is null for txq idx=%d txq->sw_tx_cons=%d txq->sw_tx_prod=%d\n",
		       idx, txq->sw_tx_cons, txq->sw_tx_prod);
		return -1;
	}

	*len = skb->len;

	first_bd = (struct eth_tx_1st_bd *)qed_chain_consume(&txq->tx_pbl);

	bds_consumed++;

	nbds = first_bd->data.nbds;

	if (data_split) {
		struct eth_tx_bd *split = (struct eth_tx_bd *)
			qed_chain_consume(&txq->tx_pbl);
		split_bd_len = BD_UNMAP_LEN(split);
		bds_consumed++;
	}
	dma_unmap_single(&edev->pdev->dev, BD_UNMAP_ADDR(first_bd),
			 BD_UNMAP_LEN(first_bd) + split_bd_len, DMA_TO_DEVICE);

	/* Unmap the data of the skb frags */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++, bds_consumed++) {
		tx_data_bd = (struct eth_tx_bd *)
			qed_chain_consume(&txq->tx_pbl);
		dma_unmap_page(&edev->pdev->dev, BD_UNMAP_ADDR(tx_data_bd),
			       BD_UNMAP_LEN(tx_data_bd), DMA_TO_DEVICE);
	}

	while (bds_consumed++ < nbds)
		qed_chain_consume(&txq->tx_pbl);

	/* Free skb */
	dev_kfree_skb_any(skb);
	txq->sw_tx_ring.skbs[idx].skb = NULL;
	txq->sw_tx_ring.skbs[idx].flags = 0;

	return 0;
}

/* Unmap the data and free skb when mapping failed during start_xmit */
static void qede_free_failed_tx_pkt(struct qede_tx_queue *txq,
				    struct eth_tx_1st_bd *first_bd,
				    int nbd, bool data_split)
{
	u16 idx = txq->sw_tx_prod;
	struct sk_buff *skb = txq->sw_tx_ring.skbs[idx].skb;
	struct eth_tx_bd *tx_data_bd;
	int i, split_bd_len = 0;

	/* Return prod to its position before this skb was handled */
	qed_chain_set_prod(&txq->tx_pbl,
			   le16_to_cpu(txq->tx_db.data.bd_prod), first_bd);

	first_bd = (struct eth_tx_1st_bd *)qed_chain_produce(&txq->tx_pbl);

	if (data_split) {
		struct eth_tx_bd *split = (struct eth_tx_bd *)
					  qed_chain_produce(&txq->tx_pbl);
		split_bd_len = BD_UNMAP_LEN(split);
		nbd--;
	}

	dma_unmap_single(txq->dev, BD_UNMAP_ADDR(first_bd),
			 BD_UNMAP_LEN(first_bd) + split_bd_len, DMA_TO_DEVICE);

	/* Unmap the data of the skb frags */
	for (i = 0; i < nbd; i++) {
		tx_data_bd = (struct eth_tx_bd *)
			qed_chain_produce(&txq->tx_pbl);
		if (tx_data_bd->nbytes)
			dma_unmap_page(txq->dev,
				       BD_UNMAP_ADDR(tx_data_bd),
				       BD_UNMAP_LEN(tx_data_bd), DMA_TO_DEVICE);
	}

	/* Return again prod to its position before this skb was handled */
	qed_chain_set_prod(&txq->tx_pbl,
			   le16_to_cpu(txq->tx_db.data.bd_prod), first_bd);

	/* Free skb */
	dev_kfree_skb_any(skb);
	txq->sw_tx_ring.skbs[idx].skb = NULL;
	txq->sw_tx_ring.skbs[idx].flags = 0;
}

static u32 qede_xmit_type(struct sk_buff *skb, int *ipv6_ext)
{
	u32 rc = XMIT_L4_CSUM;
	__be16 l3_proto;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return XMIT_PLAIN;

	l3_proto = vlan_get_protocol(skb);
	if (l3_proto == htons(ETH_P_IPV6) &&
	    (ipv6_hdr(skb)->nexthdr == NEXTHDR_IPV6))
		*ipv6_ext = 1;

	if (skb->encapsulation) {
		rc |= XMIT_ENC;
		if (skb_is_gso(skb)) {
			unsigned short gso_type = skb_shinfo(skb)->gso_type;

			if ((gso_type & SKB_GSO_UDP_TUNNEL_CSUM) ||
			    (gso_type & SKB_GSO_GRE_CSUM))
				rc |= XMIT_ENC_GSO_L4_CSUM;

			rc |= XMIT_LSO;
			return rc;
		}
	}

	if (skb_is_gso(skb))
		rc |= XMIT_LSO;

	return rc;
}

static void qede_set_params_for_ipv6_ext(struct sk_buff *skb,
					 struct eth_tx_2nd_bd *second_bd,
					 struct eth_tx_3rd_bd *third_bd)
{
	u8 l4_proto;
	u16 bd2_bits1 = 0, bd2_bits2 = 0;

	bd2_bits1 |= (1 << ETH_TX_DATA_2ND_BD_IPV6_EXT_SHIFT);

	bd2_bits2 |= ((((u8 *)skb_transport_header(skb) - skb->data) >> 1) &
		     ETH_TX_DATA_2ND_BD_L4_HDR_START_OFFSET_W_MASK)
		    << ETH_TX_DATA_2ND_BD_L4_HDR_START_OFFSET_W_SHIFT;

	bd2_bits1 |= (ETH_L4_PSEUDO_CSUM_CORRECT_LENGTH <<
		      ETH_TX_DATA_2ND_BD_L4_PSEUDO_CSUM_MODE_SHIFT);

	if (vlan_get_protocol(skb) == htons(ETH_P_IPV6))
		l4_proto = ipv6_hdr(skb)->nexthdr;
	else
		l4_proto = ip_hdr(skb)->protocol;

	if (l4_proto == IPPROTO_UDP)
		bd2_bits1 |= 1 << ETH_TX_DATA_2ND_BD_L4_UDP_SHIFT;

	if (third_bd)
		third_bd->data.bitfields |=
			cpu_to_le16(((tcp_hdrlen(skb) / 4) &
				ETH_TX_DATA_3RD_BD_TCP_HDR_LEN_DW_MASK) <<
				ETH_TX_DATA_3RD_BD_TCP_HDR_LEN_DW_SHIFT);

	second_bd->data.bitfields1 = cpu_to_le16(bd2_bits1);
	second_bd->data.bitfields2 = cpu_to_le16(bd2_bits2);
}

static int map_frag_to_bd(struct qede_tx_queue *txq,
			  skb_frag_t *frag, struct eth_tx_bd *bd)
{
	dma_addr_t mapping;

	/* Map skb non-linear frag data for DMA */
	mapping = skb_frag_dma_map(txq->dev, frag, 0,
				   skb_frag_size(frag), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(txq->dev, mapping)))
		return -ENOMEM;

	/* Setup the data pointer of the frag data */
	BD_SET_UNMAP_ADDR_LEN(bd, mapping, skb_frag_size(frag));

	return 0;
}

static u16 qede_get_skb_hlen(struct sk_buff *skb, bool is_encap_pkt)
{
	if (is_encap_pkt)
		return (skb_inner_transport_header(skb) +
			inner_tcp_hdrlen(skb) - skb->data);
	else
		return (skb_transport_header(skb) +
			tcp_hdrlen(skb) - skb->data);
}

/* +2 for 1st BD for headers and 2nd BD for headlen (if required) */
#if ((MAX_SKB_FRAGS + 2) > ETH_TX_MAX_BDS_PER_NON_LSO_PACKET)
static bool qede_pkt_req_lin(struct sk_buff *skb, u8 xmit_type)
{
	int allowed_frags = ETH_TX_MAX_BDS_PER_NON_LSO_PACKET - 1;

	if (xmit_type & XMIT_LSO) {
		int hlen;

		hlen = qede_get_skb_hlen(skb, xmit_type & XMIT_ENC);

		/* linear payload would require its own BD */
		if (skb_headlen(skb) > hlen)
			allowed_frags--;
	}

	return (skb_shinfo(skb)->nr_frags > allowed_frags);
}
#endif

static inline void qede_update_tx_producer(struct qede_tx_queue *txq)
{
	/* wmb makes sure that the BDs data is updated before updating the
	 * producer, otherwise FW may read old data from the BDs.
	 */
	wmb();
	barrier();
	writel(txq->tx_db.raw, txq->doorbell_addr);

	/* Fence required to flush the write combined buffer, since another
	 * CPU may write to the same doorbell address and data may be lost
	 * due to relaxed order nature of write combined bar.
	 */
	wmb();
}

static int qede_xdp_xmit(struct qede_dev *edev, struct qede_fastpath *fp,
			 struct sw_rx_data *metadata, u16 padding, u16 length)
{
	struct qede_tx_queue *txq = fp->xdp_tx;
	struct eth_tx_1st_bd *first_bd;
	u16 idx = txq->sw_tx_prod;
	u16 val;

	if (!qed_chain_get_elem_left(&txq->tx_pbl)) {
		txq->stopped_cnt++;
		return -ENOMEM;
	}

	first_bd = (struct eth_tx_1st_bd *)qed_chain_produce(&txq->tx_pbl);

	memset(first_bd, 0, sizeof(*first_bd));
	first_bd->data.bd_flags.bitfields =
	    BIT(ETH_TX_1ST_BD_FLAGS_START_BD_SHIFT);

	val = (length & ETH_TX_DATA_1ST_BD_PKT_LEN_MASK) <<
	       ETH_TX_DATA_1ST_BD_PKT_LEN_SHIFT;

	first_bd->data.bitfields |= cpu_to_le16(val);
	first_bd->data.nbds = 1;

	/* We can safely ignore the offset, as it's 0 for XDP */
	BD_SET_UNMAP_ADDR_LEN(first_bd, metadata->mapping + padding, length);

	/* Synchronize the buffer back to device, as program [probably]
	 * has changed it.
	 */
	dma_sync_single_for_device(&edev->pdev->dev,
				   metadata->mapping + padding,
				   length, PCI_DMA_TODEVICE);

	txq->sw_tx_ring.xdp[idx].page = metadata->data;
	txq->sw_tx_ring.xdp[idx].mapping = metadata->mapping;
	txq->sw_tx_prod = (txq->sw_tx_prod + 1) % txq->num_tx_buffers;

	/* Mark the fastpath for future XDP doorbell */
	fp->xdp_xmit = 1;

	return 0;
}

int qede_txq_has_work(struct qede_tx_queue *txq)
{
	u16 hw_bd_cons;

	/* Tell compiler that consumer and producer can change */
	barrier();
	hw_bd_cons = le16_to_cpu(*txq->hw_cons_ptr);
	if (qed_chain_get_cons_idx(&txq->tx_pbl) == hw_bd_cons + 1)
		return 0;

	return hw_bd_cons != qed_chain_get_cons_idx(&txq->tx_pbl);
}

static void qede_xdp_tx_int(struct qede_dev *edev, struct qede_tx_queue *txq)
{
	u16 hw_bd_cons, idx;

	hw_bd_cons = le16_to_cpu(*txq->hw_cons_ptr);
	barrier();

	while (hw_bd_cons != qed_chain_get_cons_idx(&txq->tx_pbl)) {
		qed_chain_consume(&txq->tx_pbl);
		idx = txq->sw_tx_cons;

		dma_unmap_page(&edev->pdev->dev,
			       txq->sw_tx_ring.xdp[idx].mapping,
			       PAGE_SIZE, DMA_BIDIRECTIONAL);
		__free_page(txq->sw_tx_ring.xdp[idx].page);

		txq->sw_tx_cons = (txq->sw_tx_cons + 1) % txq->num_tx_buffers;
		txq->xmit_pkts++;
	}
}

static int qede_tx_int(struct qede_dev *edev, struct qede_tx_queue *txq)
{
	unsigned int pkts_compl = 0, bytes_compl = 0;
	struct netdev_queue *netdev_txq;
	u16 hw_bd_cons;
	int rc;

	netdev_txq = netdev_get_tx_queue(edev->ndev, txq->ndev_txq_id);

	hw_bd_cons = le16_to_cpu(*txq->hw_cons_ptr);
	barrier();

	while (hw_bd_cons != qed_chain_get_cons_idx(&txq->tx_pbl)) {
		int len = 0;

		rc = qede_free_tx_pkt(edev, txq, &len);
		if (rc) {
			DP_NOTICE(edev, "hw_bd_cons = %d, chain_cons=%d\n",
				  hw_bd_cons,
				  qed_chain_get_cons_idx(&txq->tx_pbl));
			break;
		}

		bytes_compl += len;
		pkts_compl++;
		txq->sw_tx_cons = (txq->sw_tx_cons + 1) % txq->num_tx_buffers;
		txq->xmit_pkts++;
	}

	netdev_tx_completed_queue(netdev_txq, pkts_compl, bytes_compl);

	/* Need to make the tx_bd_cons update visible to start_xmit()
	 * before checking for netif_tx_queue_stopped().  Without the
	 * memory barrier, there is a small possibility that
	 * start_xmit() will miss it and cause the queue to be stopped
	 * forever.
	 * On the other hand we need an rmb() here to ensure the proper
	 * ordering of bit testing in the following
	 * netif_tx_queue_stopped(txq) call.
	 */
	smp_mb();

	if (unlikely(netif_tx_queue_stopped(netdev_txq))) {
		/* Taking tx_lock is needed to prevent reenabling the queue
		 * while it's empty. This could have happen if rx_action() gets
		 * suspended in qede_tx_int() after the condition before
		 * netif_tx_wake_queue(), while tx_action (qede_start_xmit()):
		 *
		 * stops the queue->sees fresh tx_bd_cons->releases the queue->
		 * sends some packets consuming the whole queue again->
		 * stops the queue
		 */

		__netif_tx_lock(netdev_txq, smp_processor_id());

		if ((netif_tx_queue_stopped(netdev_txq)) &&
		    (edev->state == QEDE_STATE_OPEN) &&
		    (qed_chain_get_elem_left(&txq->tx_pbl)
		      >= (MAX_SKB_FRAGS + 1))) {
			netif_tx_wake_queue(netdev_txq);
			DP_VERBOSE(edev, NETIF_MSG_TX_DONE,
				   "Wake queue was called\n");
		}

		__netif_tx_unlock(netdev_txq);
	}

	return 0;
}

bool qede_has_rx_work(struct qede_rx_queue *rxq)
{
	u16 hw_comp_cons, sw_comp_cons;

	/* Tell compiler that status block fields can change */
	barrier();

	hw_comp_cons = le16_to_cpu(*rxq->hw_cons_ptr);
	sw_comp_cons = qed_chain_get_cons_idx(&rxq->rx_comp_ring);

	return hw_comp_cons != sw_comp_cons;
}

static inline void qede_rx_bd_ring_consume(struct qede_rx_queue *rxq)
{
	qed_chain_consume(&rxq->rx_bd_ring);
	rxq->sw_rx_cons++;
}

/* This function reuses the buffer(from an offset) from
 * consumer index to producer index in the bd ring
 */
static inline void qede_reuse_page(struct qede_rx_queue *rxq,
				   struct sw_rx_data *curr_cons)
{
	struct eth_rx_bd *rx_bd_prod = qed_chain_produce(&rxq->rx_bd_ring);
	struct sw_rx_data *curr_prod;
	dma_addr_t new_mapping;

	curr_prod = &rxq->sw_rx_ring[rxq->sw_rx_prod & NUM_RX_BDS_MAX];
	*curr_prod = *curr_cons;

	new_mapping = curr_prod->mapping + curr_prod->page_offset;

	rx_bd_prod->addr.hi = cpu_to_le32(upper_32_bits(new_mapping));
	rx_bd_prod->addr.lo = cpu_to_le32(lower_32_bits(new_mapping) +
					  rxq->rx_headroom);

	rxq->sw_rx_prod++;
	curr_cons->data = NULL;
}

/* In case of allocation failures reuse buffers
 * from consumer index to produce buffers for firmware
 */
void qede_recycle_rx_bd_ring(struct qede_rx_queue *rxq, u8 count)
{
	struct sw_rx_data *curr_cons;

	for (; count > 0; count--) {
		curr_cons = &rxq->sw_rx_ring[rxq->sw_rx_cons & NUM_RX_BDS_MAX];
		qede_reuse_page(rxq, curr_cons);
		qede_rx_bd_ring_consume(rxq);
	}
}

static inline int qede_realloc_rx_buffer(struct qede_rx_queue *rxq,
					 struct sw_rx_data *curr_cons)
{
	/* Move to the next segment in the page */
	curr_cons->page_offset += rxq->rx_buf_seg_size;

	if (curr_cons->page_offset == PAGE_SIZE) {
		if (unlikely(qede_alloc_rx_buffer(rxq, true))) {
			/* Since we failed to allocate new buffer
			 * current buffer can be used again.
			 */
			curr_cons->page_offset -= rxq->rx_buf_seg_size;

			return -ENOMEM;
		}

		dma_unmap_page(rxq->dev, curr_cons->mapping,
			       PAGE_SIZE, rxq->data_direction);
	} else {
		/* Increment refcount of the page as we don't want
		 * network stack to take the ownership of the page
		 * which can be recycled multiple times by the driver.
		 */
		page_ref_inc(curr_cons->data);
		qede_reuse_page(rxq, curr_cons);
	}

	return 0;
}

void qede_update_rx_prod(struct qede_dev *edev, struct qede_rx_queue *rxq)
{
	u16 bd_prod = qed_chain_get_prod_idx(&rxq->rx_bd_ring);
	u16 cqe_prod = qed_chain_get_prod_idx(&rxq->rx_comp_ring);
	struct eth_rx_prod_data rx_prods = {0};

	/* Update producers */
	rx_prods.bd_prod = cpu_to_le16(bd_prod);
	rx_prods.cqe_prod = cpu_to_le16(cqe_prod);

	/* Make sure that the BD and SGE data is updated before updating the
	 * producers since FW might read the BD/SGE right after the producer
	 * is updated.
	 */
	wmb();

	internal_ram_wr(rxq->hw_rxq_prod_addr, sizeof(rx_prods),
			(u32 *)&rx_prods);
}

static void qede_get_rxhash(struct sk_buff *skb, u8 bitfields, __le32 rss_hash)
{
	enum pkt_hash_types hash_type = PKT_HASH_TYPE_NONE;
	enum rss_hash_type htype;
	u32 hash = 0;

	htype = GET_FIELD(bitfields, ETH_FAST_PATH_RX_REG_CQE_RSS_HASH_TYPE);
	if (htype) {
		hash_type = ((htype == RSS_HASH_TYPE_IPV4) ||
			     (htype == RSS_HASH_TYPE_IPV6)) ?
			    PKT_HASH_TYPE_L3 : PKT_HASH_TYPE_L4;
		hash = le32_to_cpu(rss_hash);
	}
	skb_set_hash(skb, hash, hash_type);
}

static void qede_set_skb_csum(struct sk_buff *skb, u8 csum_flag)
{
	skb_checksum_none_assert(skb);

	if (csum_flag & QEDE_CSUM_UNNECESSARY)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (csum_flag & QEDE_TUNN_CSUM_UNNECESSARY) {
		skb->csum_level = 1;
		skb->encapsulation = 1;
	}
}

static inline void qede_skb_receive(struct qede_dev *edev,
				    struct qede_fastpath *fp,
				    struct qede_rx_queue *rxq,
				    struct sk_buff *skb, u16 vlan_tag)
{
	if (vlan_tag)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan_tag);

	napi_gro_receive(&fp->napi, skb);
}

static void qede_set_gro_params(struct qede_dev *edev,
				struct sk_buff *skb,
				struct eth_fast_path_rx_tpa_start_cqe *cqe)
{
	u16 parsing_flags = le16_to_cpu(cqe->pars_flags.flags);

	if (((parsing_flags >> PARSING_AND_ERR_FLAGS_L3TYPE_SHIFT) &
	    PARSING_AND_ERR_FLAGS_L3TYPE_MASK) == 2)
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
	else
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;

	skb_shinfo(skb)->gso_size = __le16_to_cpu(cqe->len_on_first_bd) -
				    cqe->header_len;
}

static int qede_fill_frag_skb(struct qede_dev *edev,
			      struct qede_rx_queue *rxq,
			      u8 tpa_agg_index, u16 len_on_bd)
{
	struct sw_rx_data *current_bd = &rxq->sw_rx_ring[rxq->sw_rx_cons &
							 NUM_RX_BDS_MAX];
	struct qede_agg_info *tpa_info = &rxq->tpa_info[tpa_agg_index];
	struct sk_buff *skb = tpa_info->skb;

	if (unlikely(tpa_info->state != QEDE_AGG_STATE_START))
		goto out;

	/* Add one frag and update the appropriate fields in the skb */
	skb_fill_page_desc(skb, tpa_info->frag_id++,
			   current_bd->data,
			   current_bd->page_offset + rxq->rx_headroom,
			   len_on_bd);

	if (unlikely(qede_realloc_rx_buffer(rxq, current_bd))) {
		/* Incr page ref count to reuse on allocation failure
		 * so that it doesn't get freed while freeing SKB.
		 */
		page_ref_inc(current_bd->data);
		goto out;
	}

	qede_rx_bd_ring_consume(rxq);

	skb->data_len += len_on_bd;
	skb->truesize += rxq->rx_buf_seg_size;
	skb->len += len_on_bd;

	return 0;

out:
	tpa_info->state = QEDE_AGG_STATE_ERROR;
	qede_recycle_rx_bd_ring(rxq, 1);

	return -ENOMEM;
}

static bool qede_tunn_exist(u16 flag)
{
	return !!(flag & (PARSING_AND_ERR_FLAGS_TUNNELEXIST_MASK <<
			  PARSING_AND_ERR_FLAGS_TUNNELEXIST_SHIFT));
}

static u8 qede_check_tunn_csum(u16 flag)
{
	u16 csum_flag = 0;
	u8 tcsum = 0;

	if (flag & (PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMWASCALCULATED_MASK <<
		    PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMWASCALCULATED_SHIFT))
		csum_flag |= PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMERROR_MASK <<
			     PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMERROR_SHIFT;

	if (flag & (PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_MASK <<
		    PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_SHIFT)) {
		csum_flag |= PARSING_AND_ERR_FLAGS_L4CHKSMERROR_MASK <<
			     PARSING_AND_ERR_FLAGS_L4CHKSMERROR_SHIFT;
		tcsum = QEDE_TUNN_CSUM_UNNECESSARY;
	}

	csum_flag |= PARSING_AND_ERR_FLAGS_TUNNELIPHDRERROR_MASK <<
		     PARSING_AND_ERR_FLAGS_TUNNELIPHDRERROR_SHIFT |
		     PARSING_AND_ERR_FLAGS_IPHDRERROR_MASK <<
		     PARSING_AND_ERR_FLAGS_IPHDRERROR_SHIFT;

	if (csum_flag & flag)
		return QEDE_CSUM_ERROR;

	return QEDE_CSUM_UNNECESSARY | tcsum;
}

static inline struct sk_buff *
qede_build_skb(struct qede_rx_queue *rxq,
	       struct sw_rx_data *bd, u16 len, u16 pad)
{
	struct sk_buff *skb;
	void *buf;

	buf = page_address(bd->data) + bd->page_offset;
	skb = build_skb(buf, rxq->rx_buf_seg_size);

	skb_reserve(skb, pad);
	skb_put(skb, len);

	return skb;
}

static struct sk_buff *
qede_tpa_rx_build_skb(struct qede_dev *edev,
		      struct qede_rx_queue *rxq,
		      struct sw_rx_data *bd, u16 len, u16 pad,
		      bool alloc_skb)
{
	struct sk_buff *skb;

	skb = qede_build_skb(rxq, bd, len, pad);
	bd->page_offset += rxq->rx_buf_seg_size;

	if (bd->page_offset == PAGE_SIZE) {
		if (unlikely(qede_alloc_rx_buffer(rxq, true))) {
			DP_NOTICE(edev,
				  "Failed to allocate RX buffer for tpa start\n");
			bd->page_offset -= rxq->rx_buf_seg_size;
			page_ref_inc(bd->data);
			dev_kfree_skb_any(skb);
			return NULL;
		}
	} else {
		page_ref_inc(bd->data);
		qede_reuse_page(rxq, bd);
	}

	/* We've consumed the first BD and prepared an SKB */
	qede_rx_bd_ring_consume(rxq);

	return skb;
}

static struct sk_buff *
qede_rx_build_skb(struct qede_dev *edev,
		  struct qede_rx_queue *rxq,
		  struct sw_rx_data *bd, u16 len, u16 pad)
{
	struct sk_buff *skb = NULL;

	/* For smaller frames still need to allocate skb, memcpy
	 * data and benefit in reusing the page segment instead of
	 * un-mapping it.
	 */
	if ((len + pad <= edev->rx_copybreak)) {
		unsigned int offset = bd->page_offset + pad;

		skb = netdev_alloc_skb(edev->ndev, QEDE_RX_HDR_SIZE);
		if (unlikely(!skb))
			return NULL;

		skb_reserve(skb, pad);
		skb_put_data(skb, page_address(bd->data) + offset, len);
		qede_reuse_page(rxq, bd);
		goto out;
	}

	skb = qede_build_skb(rxq, bd, len, pad);

	if (unlikely(qede_realloc_rx_buffer(rxq, bd))) {
		/* Incr page ref count to reuse on allocation failure so
		 * that it doesn't get freed while freeing SKB [as its
		 * already mapped there].
		 */
		page_ref_inc(bd->data);
		dev_kfree_skb_any(skb);
		return NULL;
	}
out:
	/* We've consumed the first BD and prepared an SKB */
	qede_rx_bd_ring_consume(rxq);

	return skb;
}

static void qede_tpa_start(struct qede_dev *edev,
			   struct qede_rx_queue *rxq,
			   struct eth_fast_path_rx_tpa_start_cqe *cqe)
{
	struct qede_agg_info *tpa_info = &rxq->tpa_info[cqe->tpa_agg_index];
	struct sw_rx_data *sw_rx_data_cons;
	u16 pad;

	sw_rx_data_cons = &rxq->sw_rx_ring[rxq->sw_rx_cons & NUM_RX_BDS_MAX];
	pad = cqe->placement_offset + rxq->rx_headroom;

	tpa_info->skb = qede_tpa_rx_build_skb(edev, rxq, sw_rx_data_cons,
					      le16_to_cpu(cqe->len_on_first_bd),
					      pad, false);
	tpa_info->buffer.page_offset = sw_rx_data_cons->page_offset;
	tpa_info->buffer.mapping = sw_rx_data_cons->mapping;

	if (unlikely(!tpa_info->skb)) {
		DP_NOTICE(edev, "Failed to allocate SKB for gro\n");

		/* Consume from ring but do not produce since
		 * this might be used by FW still, it will be re-used
		 * at TPA end.
		 */
		tpa_info->tpa_start_fail = true;
		qede_rx_bd_ring_consume(rxq);
		tpa_info->state = QEDE_AGG_STATE_ERROR;
		goto cons_buf;
	}

	tpa_info->frag_id = 0;
	tpa_info->state = QEDE_AGG_STATE_START;

	if ((le16_to_cpu(cqe->pars_flags.flags) >>
	     PARSING_AND_ERR_FLAGS_TAG8021QEXIST_SHIFT) &
	    PARSING_AND_ERR_FLAGS_TAG8021QEXIST_MASK)
		tpa_info->vlan_tag = le16_to_cpu(cqe->vlan_tag);
	else
		tpa_info->vlan_tag = 0;

	qede_get_rxhash(tpa_info->skb, cqe->bitfields, cqe->rss_hash);

	/* This is needed in order to enable forwarding support */
	qede_set_gro_params(edev, tpa_info->skb, cqe);

cons_buf: /* We still need to handle bd_len_list to consume buffers */
	if (likely(cqe->bw_ext_bd_len_list[0]))
		qede_fill_frag_skb(edev, rxq, cqe->tpa_agg_index,
				   le16_to_cpu(cqe->bw_ext_bd_len_list[0]));

	if (unlikely(cqe->bw_ext_bd_len_list[1])) {
		DP_ERR(edev,
		       "Unlikely - got a TPA aggregation with more than one bw_ext_bd_len_list entry in the TPA start\n");
		tpa_info->state = QEDE_AGG_STATE_ERROR;
	}
}

#ifdef CONFIG_INET
static void qede_gro_ip_csum(struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct tcphdr *th;

	skb_set_transport_header(skb, sizeof(struct iphdr));
	th = tcp_hdr(skb);

	th->check = ~tcp_v4_check(skb->len - skb_transport_offset(skb),
				  iph->saddr, iph->daddr, 0);

	tcp_gro_complete(skb);
}

static void qede_gro_ipv6_csum(struct sk_buff *skb)
{
	struct ipv6hdr *iph = ipv6_hdr(skb);
	struct tcphdr *th;

	skb_set_transport_header(skb, sizeof(struct ipv6hdr));
	th = tcp_hdr(skb);

	th->check = ~tcp_v6_check(skb->len - skb_transport_offset(skb),
				  &iph->saddr, &iph->daddr, 0);
	tcp_gro_complete(skb);
}
#endif

static void qede_gro_receive(struct qede_dev *edev,
			     struct qede_fastpath *fp,
			     struct sk_buff *skb,
			     u16 vlan_tag)
{
	/* FW can send a single MTU sized packet from gro flow
	 * due to aggregation timeout/last segment etc. which
	 * is not expected to be a gro packet. If a skb has zero
	 * frags then simply push it in the stack as non gso skb.
	 */
	if (unlikely(!skb->data_len)) {
		skb_shinfo(skb)->gso_type = 0;
		skb_shinfo(skb)->gso_size = 0;
		goto send_skb;
	}

#ifdef CONFIG_INET
	if (skb_shinfo(skb)->gso_size) {
		skb_reset_network_header(skb);

		switch (skb->protocol) {
		case htons(ETH_P_IP):
			qede_gro_ip_csum(skb);
			break;
		case htons(ETH_P_IPV6):
			qede_gro_ipv6_csum(skb);
			break;
		default:
			DP_ERR(edev,
			       "Error: FW GRO supports only IPv4/IPv6, not 0x%04x\n",
			       ntohs(skb->protocol));
		}
	}
#endif

send_skb:
	skb_record_rx_queue(skb, fp->rxq->rxq_id);
	qede_skb_receive(edev, fp, fp->rxq, skb, vlan_tag);
}

static inline void qede_tpa_cont(struct qede_dev *edev,
				 struct qede_rx_queue *rxq,
				 struct eth_fast_path_rx_tpa_cont_cqe *cqe)
{
	int i;

	for (i = 0; cqe->len_list[i]; i++)
		qede_fill_frag_skb(edev, rxq, cqe->tpa_agg_index,
				   le16_to_cpu(cqe->len_list[i]));

	if (unlikely(i > 1))
		DP_ERR(edev,
		       "Strange - TPA cont with more than a single len_list entry\n");
}

static int qede_tpa_end(struct qede_dev *edev,
			struct qede_fastpath *fp,
			struct eth_fast_path_rx_tpa_end_cqe *cqe)
{
	struct qede_rx_queue *rxq = fp->rxq;
	struct qede_agg_info *tpa_info;
	struct sk_buff *skb;
	int i;

	tpa_info = &rxq->tpa_info[cqe->tpa_agg_index];
	skb = tpa_info->skb;

	if (tpa_info->buffer.page_offset == PAGE_SIZE)
		dma_unmap_page(rxq->dev, tpa_info->buffer.mapping,
			       PAGE_SIZE, rxq->data_direction);

	for (i = 0; cqe->len_list[i]; i++)
		qede_fill_frag_skb(edev, rxq, cqe->tpa_agg_index,
				   le16_to_cpu(cqe->len_list[i]));
	if (unlikely(i > 1))
		DP_ERR(edev,
		       "Strange - TPA emd with more than a single len_list entry\n");

	if (unlikely(tpa_info->state != QEDE_AGG_STATE_START))
		goto err;

	/* Sanity */
	if (unlikely(cqe->num_of_bds != tpa_info->frag_id + 1))
		DP_ERR(edev,
		       "Strange - TPA had %02x BDs, but SKB has only %d frags\n",
		       cqe->num_of_bds, tpa_info->frag_id);
	if (unlikely(skb->len != le16_to_cpu(cqe->total_packet_len)))
		DP_ERR(edev,
		       "Strange - total packet len [cqe] is %4x but SKB has len %04x\n",
		       le16_to_cpu(cqe->total_packet_len), skb->len);

	/* Finalize the SKB */
	skb->protocol = eth_type_trans(skb, edev->ndev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	/* tcp_gro_complete() will copy NAPI_GRO_CB(skb)->count
	 * to skb_shinfo(skb)->gso_segs
	 */
	NAPI_GRO_CB(skb)->count = le16_to_cpu(cqe->num_of_coalesced_segs);

	qede_gro_receive(edev, fp, skb, tpa_info->vlan_tag);

	tpa_info->state = QEDE_AGG_STATE_NONE;

	return 1;
err:
	tpa_info->state = QEDE_AGG_STATE_NONE;

	if (tpa_info->tpa_start_fail) {
		qede_reuse_page(rxq, &tpa_info->buffer);
		tpa_info->tpa_start_fail = false;
	}

	dev_kfree_skb_any(tpa_info->skb);
	tpa_info->skb = NULL;
	return 0;
}

static u8 qede_check_notunn_csum(u16 flag)
{
	u16 csum_flag = 0;
	u8 csum = 0;

	if (flag & (PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_MASK <<
		    PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_SHIFT)) {
		csum_flag |= PARSING_AND_ERR_FLAGS_L4CHKSMERROR_MASK <<
			     PARSING_AND_ERR_FLAGS_L4CHKSMERROR_SHIFT;
		csum = QEDE_CSUM_UNNECESSARY;
	}

	csum_flag |= PARSING_AND_ERR_FLAGS_IPHDRERROR_MASK <<
		     PARSING_AND_ERR_FLAGS_IPHDRERROR_SHIFT;

	if (csum_flag & flag)
		return QEDE_CSUM_ERROR;

	return csum;
}

static u8 qede_check_csum(u16 flag)
{
	if (!qede_tunn_exist(flag))
		return qede_check_notunn_csum(flag);
	else
		return qede_check_tunn_csum(flag);
}

static bool qede_pkt_is_ip_fragmented(struct eth_fast_path_rx_reg_cqe *cqe,
				      u16 flag)
{
	u8 tun_pars_flg = cqe->tunnel_pars_flags.flags;

	if ((tun_pars_flg & (ETH_TUNNEL_PARSING_FLAGS_IPV4_FRAGMENT_MASK <<
			     ETH_TUNNEL_PARSING_FLAGS_IPV4_FRAGMENT_SHIFT)) ||
	    (flag & (PARSING_AND_ERR_FLAGS_IPV4FRAG_MASK <<
		     PARSING_AND_ERR_FLAGS_IPV4FRAG_SHIFT)))
		return true;

	return false;
}

/* Return true iff packet is to be passed to stack */
static bool qede_rx_xdp(struct qede_dev *edev,
			struct qede_fastpath *fp,
			struct qede_rx_queue *rxq,
			struct bpf_prog *prog,
			struct sw_rx_data *bd,
			struct eth_fast_path_rx_reg_cqe *cqe,
			u16 *data_offset, u16 *len)
{
	struct xdp_buff xdp;
	enum xdp_action act;

	xdp.data_hard_start = page_address(bd->data);
	xdp.data = xdp.data_hard_start + *data_offset;
	xdp_set_data_meta_invalid(&xdp);
	xdp.data_end = xdp.data + *len;
	xdp.rxq = &rxq->xdp_rxq;

	/* Queues always have a full reset currently, so for the time
	 * being until there's atomic program replace just mark read
	 * side for map helpers.
	 */
	rcu_read_lock();
	act = bpf_prog_run_xdp(prog, &xdp);
	rcu_read_unlock();

	/* Recalculate, as XDP might have changed the headers */
	*data_offset = xdp.data - xdp.data_hard_start;
	*len = xdp.data_end - xdp.data;

	if (act == XDP_PASS)
		return true;

	/* Count number of packets not to be passed to stack */
	rxq->xdp_no_pass++;

	switch (act) {
	case XDP_TX:
		/* We need the replacement buffer before transmit. */
		if (qede_alloc_rx_buffer(rxq, true)) {
			qede_recycle_rx_bd_ring(rxq, 1);
			trace_xdp_exception(edev->ndev, prog, act);
			return false;
		}

		/* Now if there's a transmission problem, we'd still have to
		 * throw current buffer, as replacement was already allocated.
		 */
		if (qede_xdp_xmit(edev, fp, bd, *data_offset, *len)) {
			dma_unmap_page(rxq->dev, bd->mapping,
				       PAGE_SIZE, DMA_BIDIRECTIONAL);
			__free_page(bd->data);
			trace_xdp_exception(edev->ndev, prog, act);
		}

		/* Regardless, we've consumed an Rx BD */
		qede_rx_bd_ring_consume(rxq);
		return false;

	default:
		bpf_warn_invalid_xdp_action(act);
		/* Fall through */
	case XDP_ABORTED:
		trace_xdp_exception(edev->ndev, prog, act);
		/* Fall through */
	case XDP_DROP:
		qede_recycle_rx_bd_ring(rxq, cqe->bd_num);
	}

	return false;
}

static int qede_rx_build_jumbo(struct qede_dev *edev,
			       struct qede_rx_queue *rxq,
			       struct sk_buff *skb,
			       struct eth_fast_path_rx_reg_cqe *cqe,
			       u16 first_bd_len)
{
	u16 pkt_len = le16_to_cpu(cqe->pkt_len);
	struct sw_rx_data *bd;
	u16 bd_cons_idx;
	u8 num_frags;

	pkt_len -= first_bd_len;

	/* We've already used one BD for the SKB. Now take care of the rest */
	for (num_frags = cqe->bd_num - 1; num_frags > 0; num_frags--) {
		u16 cur_size = pkt_len > rxq->rx_buf_size ? rxq->rx_buf_size :
		    pkt_len;

		if (unlikely(!cur_size)) {
			DP_ERR(edev,
			       "Still got %d BDs for mapping jumbo, but length became 0\n",
			       num_frags);
			goto out;
		}

		/* We need a replacement buffer for each BD */
		if (unlikely(qede_alloc_rx_buffer(rxq, true)))
			goto out;

		/* Now that we've allocated the replacement buffer,
		 * we can safely consume the next BD and map it to the SKB.
		 */
		bd_cons_idx = rxq->sw_rx_cons & NUM_RX_BDS_MAX;
		bd = &rxq->sw_rx_ring[bd_cons_idx];
		qede_rx_bd_ring_consume(rxq);

		dma_unmap_page(rxq->dev, bd->mapping,
			       PAGE_SIZE, DMA_FROM_DEVICE);

		skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags++,
				   bd->data, rxq->rx_headroom, cur_size);

		skb->truesize += PAGE_SIZE;
		skb->data_len += cur_size;
		skb->len += cur_size;
		pkt_len -= cur_size;
	}

	if (unlikely(pkt_len))
		DP_ERR(edev,
		       "Mapped all BDs of jumbo, but still have %d bytes\n",
		       pkt_len);

out:
	return num_frags;
}

static int qede_rx_process_tpa_cqe(struct qede_dev *edev,
				   struct qede_fastpath *fp,
				   struct qede_rx_queue *rxq,
				   union eth_rx_cqe *cqe,
				   enum eth_rx_cqe_type type)
{
	switch (type) {
	case ETH_RX_CQE_TYPE_TPA_START:
		qede_tpa_start(edev, rxq, &cqe->fast_path_tpa_start);
		return 0;
	case ETH_RX_CQE_TYPE_TPA_CONT:
		qede_tpa_cont(edev, rxq, &cqe->fast_path_tpa_cont);
		return 0;
	case ETH_RX_CQE_TYPE_TPA_END:
		return qede_tpa_end(edev, fp, &cqe->fast_path_tpa_end);
	default:
		return 0;
	}
}

static int qede_rx_process_cqe(struct qede_dev *edev,
			       struct qede_fastpath *fp,
			       struct qede_rx_queue *rxq)
{
	struct bpf_prog *xdp_prog = READ_ONCE(rxq->xdp_prog);
	struct eth_fast_path_rx_reg_cqe *fp_cqe;
	u16 len, pad, bd_cons_idx, parse_flag;
	enum eth_rx_cqe_type cqe_type;
	union eth_rx_cqe *cqe;
	struct sw_rx_data *bd;
	struct sk_buff *skb;
	__le16 flags;
	u8 csum_flag;

	/* Get the CQE from the completion ring */
	cqe = (union eth_rx_cqe *)qed_chain_consume(&rxq->rx_comp_ring);
	cqe_type = cqe->fast_path_regular.type;

	/* Process an unlikely slowpath event */
	if (unlikely(cqe_type == ETH_RX_CQE_TYPE_SLOW_PATH)) {
		struct eth_slow_path_rx_cqe *sp_cqe;

		sp_cqe = (struct eth_slow_path_rx_cqe *)cqe;
		edev->ops->eth_cqe_completion(edev->cdev, fp->id, sp_cqe);
		return 0;
	}

	/* Handle TPA cqes */
	if (cqe_type != ETH_RX_CQE_TYPE_REGULAR)
		return qede_rx_process_tpa_cqe(edev, fp, rxq, cqe, cqe_type);

	/* Get the data from the SW ring; Consume it only after it's evident
	 * we wouldn't recycle it.
	 */
	bd_cons_idx = rxq->sw_rx_cons & NUM_RX_BDS_MAX;
	bd = &rxq->sw_rx_ring[bd_cons_idx];

	fp_cqe = &cqe->fast_path_regular;
	len = le16_to_cpu(fp_cqe->len_on_first_bd);
	pad = fp_cqe->placement_offset + rxq->rx_headroom;

	/* Run eBPF program if one is attached */
	if (xdp_prog)
		if (!qede_rx_xdp(edev, fp, rxq, xdp_prog, bd, fp_cqe,
				 &pad, &len))
			return 0;

	/* If this is an error packet then drop it */
	flags = cqe->fast_path_regular.pars_flags.flags;
	parse_flag = le16_to_cpu(flags);

	csum_flag = qede_check_csum(parse_flag);
	if (unlikely(csum_flag == QEDE_CSUM_ERROR)) {
		if (qede_pkt_is_ip_fragmented(fp_cqe, parse_flag))
			rxq->rx_ip_frags++;
		else
			rxq->rx_hw_errors++;
	}

	/* Basic validation passed; Need to prepare an SKB. This would also
	 * guarantee to finally consume the first BD upon success.
	 */
	skb = qede_rx_build_skb(edev, rxq, bd, len, pad);
	if (!skb) {
		rxq->rx_alloc_errors++;
		qede_recycle_rx_bd_ring(rxq, fp_cqe->bd_num);
		return 0;
	}

	/* In case of Jumbo packet, several PAGE_SIZEd buffers will be pointed
	 * by a single cqe.
	 */
	if (fp_cqe->bd_num > 1) {
		u16 unmapped_frags = qede_rx_build_jumbo(edev, rxq, skb,
							 fp_cqe, len);

		if (unlikely(unmapped_frags > 0)) {
			qede_recycle_rx_bd_ring(rxq, unmapped_frags);
			dev_kfree_skb_any(skb);
			return 0;
		}
	}

	/* The SKB contains all the data. Now prepare meta-magic */
	skb->protocol = eth_type_trans(skb, edev->ndev);
	qede_get_rxhash(skb, fp_cqe->bitfields, fp_cqe->rss_hash);
	qede_set_skb_csum(skb, csum_flag);
	skb_record_rx_queue(skb, rxq->rxq_id);
	qede_ptp_record_rx_ts(edev, cqe, skb);

	/* SKB is prepared - pass it to stack */
	qede_skb_receive(edev, fp, rxq, skb, le16_to_cpu(fp_cqe->vlan_tag));

	return 1;
}

static int qede_rx_int(struct qede_fastpath *fp, int budget)
{
	struct qede_rx_queue *rxq = fp->rxq;
	struct qede_dev *edev = fp->edev;
	int work_done = 0, rcv_pkts = 0;
	u16 hw_comp_cons, sw_comp_cons;

	hw_comp_cons = le16_to_cpu(*rxq->hw_cons_ptr);
	sw_comp_cons = qed_chain_get_cons_idx(&rxq->rx_comp_ring);

	/* Memory barrier to prevent the CPU from doing speculative reads of CQE
	 * / BD in the while-loop before reading hw_comp_cons. If the CQE is
	 * read before it is written by FW, then FW writes CQE and SB, and then
	 * the CPU reads the hw_comp_cons, it will use an old CQE.
	 */
	rmb();

	/* Loop to complete all indicated BDs */
	while ((sw_comp_cons != hw_comp_cons) && (work_done < budget)) {
		rcv_pkts += qede_rx_process_cqe(edev, fp, rxq);
		qed_chain_recycle_consumed(&rxq->rx_comp_ring);
		sw_comp_cons = qed_chain_get_cons_idx(&rxq->rx_comp_ring);
		work_done++;
	}

	rxq->rcv_pkts += rcv_pkts;

	/* Allocate replacement buffers */
	while (rxq->num_rx_buffers - rxq->filled_buffers)
		if (qede_alloc_rx_buffer(rxq, false))
			break;

	/* Update producers */
	qede_update_rx_prod(edev, rxq);

	return work_done;
}

static bool qede_poll_is_more_work(struct qede_fastpath *fp)
{
	qed_sb_update_sb_idx(fp->sb_info);

	/* *_has_*_work() reads the status block, thus we need to ensure that
	 * status block indices have been actually read (qed_sb_update_sb_idx)
	 * prior to this check (*_has_*_work) so that we won't write the
	 * "newer" value of the status block to HW (if there was a DMA right
	 * after qede_has_rx_work and if there is no rmb, the memory reading
	 * (qed_sb_update_sb_idx) may be postponed to right before *_ack_sb).
	 * In this case there will never be another interrupt until there is
	 * another update of the status block, while there is still unhandled
	 * work.
	 */
	rmb();

	if (likely(fp->type & QEDE_FASTPATH_RX))
		if (qede_has_rx_work(fp->rxq))
			return true;

	if (fp->type & QEDE_FASTPATH_XDP)
		if (qede_txq_has_work(fp->xdp_tx))
			return true;

	if (likely(fp->type & QEDE_FASTPATH_TX)) {
		int cos;

		for_each_cos_in_txq(fp->edev, cos) {
			if (qede_txq_has_work(&fp->txq[cos]))
				return true;
		}
	}

	return false;
}

/*********************
 * NDO & API related *
 *********************/
int qede_poll(struct napi_struct *napi, int budget)
{
	struct qede_fastpath *fp = container_of(napi, struct qede_fastpath,
						napi);
	struct qede_dev *edev = fp->edev;
	int rx_work_done = 0;

	if (likely(fp->type & QEDE_FASTPATH_TX)) {
		int cos;

		for_each_cos_in_txq(fp->edev, cos) {
			if (qede_txq_has_work(&fp->txq[cos]))
				qede_tx_int(edev, &fp->txq[cos]);
		}
	}

	if ((fp->type & QEDE_FASTPATH_XDP) && qede_txq_has_work(fp->xdp_tx))
		qede_xdp_tx_int(edev, fp->xdp_tx);

	rx_work_done = (likely(fp->type & QEDE_FASTPATH_RX) &&
			qede_has_rx_work(fp->rxq)) ?
			qede_rx_int(fp, budget) : 0;
	if (rx_work_done < budget) {
		if (!qede_poll_is_more_work(fp)) {
			napi_complete_done(napi, rx_work_done);

			/* Update and reenable interrupts */
			qed_sb_ack(fp->sb_info, IGU_INT_ENABLE, 1);
		} else {
			rx_work_done = budget;
		}
	}

	if (fp->xdp_xmit) {
		u16 xdp_prod = qed_chain_get_prod_idx(&fp->xdp_tx->tx_pbl);

		fp->xdp_xmit = 0;
		fp->xdp_tx->tx_db.data.bd_prod = cpu_to_le16(xdp_prod);
		qede_update_tx_producer(fp->xdp_tx);
	}

	return rx_work_done;
}

irqreturn_t qede_msix_fp_int(int irq, void *fp_cookie)
{
	struct qede_fastpath *fp = fp_cookie;

	qed_sb_ack(fp->sb_info, IGU_INT_DISABLE, 0 /*do not update*/);

	napi_schedule_irqoff(&fp->napi);
	return IRQ_HANDLED;
}

/* Main transmit function */
netdev_tx_t qede_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);
	struct netdev_queue *netdev_txq;
	struct qede_tx_queue *txq;
	struct eth_tx_1st_bd *first_bd;
	struct eth_tx_2nd_bd *second_bd = NULL;
	struct eth_tx_3rd_bd *third_bd = NULL;
	struct eth_tx_bd *tx_data_bd = NULL;
	u16 txq_index, val = 0;
	u8 nbd = 0;
	dma_addr_t mapping;
	int rc, frag_idx = 0, ipv6_ext = 0;
	u8 xmit_type;
	u16 idx;
	u16 hlen;
	bool data_split = false;

	/* Get tx-queue context and netdev index */
	txq_index = skb_get_queue_mapping(skb);
	WARN_ON(txq_index >= QEDE_TSS_COUNT(edev) * edev->dev_info.num_tc);
	txq = QEDE_NDEV_TXQ_ID_TO_TXQ(edev, txq_index);
	netdev_txq = netdev_get_tx_queue(ndev, txq_index);

	WARN_ON(qed_chain_get_elem_left(&txq->tx_pbl) < (MAX_SKB_FRAGS + 1));

	xmit_type = qede_xmit_type(skb, &ipv6_ext);

#if ((MAX_SKB_FRAGS + 2) > ETH_TX_MAX_BDS_PER_NON_LSO_PACKET)
	if (qede_pkt_req_lin(skb, xmit_type)) {
		if (skb_linearize(skb)) {
			txq->tx_mem_alloc_err++;

			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
	}
#endif

	/* Fill the entry in the SW ring and the BDs in the FW ring */
	idx = txq->sw_tx_prod;
	txq->sw_tx_ring.skbs[idx].skb = skb;
	first_bd = (struct eth_tx_1st_bd *)
		   qed_chain_produce(&txq->tx_pbl);
	memset(first_bd, 0, sizeof(*first_bd));
	first_bd->data.bd_flags.bitfields =
		1 << ETH_TX_1ST_BD_FLAGS_START_BD_SHIFT;

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		qede_ptp_tx_ts(edev, skb);

	/* Map skb linear data for DMA and set in the first BD */
	mapping = dma_map_single(txq->dev, skb->data,
				 skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(txq->dev, mapping))) {
		DP_NOTICE(edev, "SKB mapping failed\n");
		qede_free_failed_tx_pkt(txq, first_bd, 0, false);
		qede_update_tx_producer(txq);
		return NETDEV_TX_OK;
	}
	nbd++;
	BD_SET_UNMAP_ADDR_LEN(first_bd, mapping, skb_headlen(skb));

	/* In case there is IPv6 with extension headers or LSO we need 2nd and
	 * 3rd BDs.
	 */
	if (unlikely((xmit_type & XMIT_LSO) | ipv6_ext)) {
		second_bd = (struct eth_tx_2nd_bd *)
			qed_chain_produce(&txq->tx_pbl);
		memset(second_bd, 0, sizeof(*second_bd));

		nbd++;
		third_bd = (struct eth_tx_3rd_bd *)
			qed_chain_produce(&txq->tx_pbl);
		memset(third_bd, 0, sizeof(*third_bd));

		nbd++;
		/* We need to fill in additional data in second_bd... */
		tx_data_bd = (struct eth_tx_bd *)second_bd;
	}

	if (skb_vlan_tag_present(skb)) {
		first_bd->data.vlan = cpu_to_le16(skb_vlan_tag_get(skb));
		first_bd->data.bd_flags.bitfields |=
			1 << ETH_TX_1ST_BD_FLAGS_VLAN_INSERTION_SHIFT;
	}

	/* Fill the parsing flags & params according to the requested offload */
	if (xmit_type & XMIT_L4_CSUM) {
		/* We don't re-calculate IP checksum as it is already done by
		 * the upper stack
		 */
		first_bd->data.bd_flags.bitfields |=
			1 << ETH_TX_1ST_BD_FLAGS_L4_CSUM_SHIFT;

		if (xmit_type & XMIT_ENC) {
			first_bd->data.bd_flags.bitfields |=
				1 << ETH_TX_1ST_BD_FLAGS_IP_CSUM_SHIFT;

			val |= (1 << ETH_TX_DATA_1ST_BD_TUNN_FLAG_SHIFT);
		}

		/* Legacy FW had flipped behavior in regard to this bit -
		 * I.e., needed to set to prevent FW from touching encapsulated
		 * packets when it didn't need to.
		 */
		if (unlikely(txq->is_legacy))
			val ^= (1 << ETH_TX_DATA_1ST_BD_TUNN_FLAG_SHIFT);

		/* If the packet is IPv6 with extension header, indicate that
		 * to FW and pass few params, since the device cracker doesn't
		 * support parsing IPv6 with extension header/s.
		 */
		if (unlikely(ipv6_ext))
			qede_set_params_for_ipv6_ext(skb, second_bd, third_bd);
	}

	if (xmit_type & XMIT_LSO) {
		first_bd->data.bd_flags.bitfields |=
			(1 << ETH_TX_1ST_BD_FLAGS_LSO_SHIFT);
		third_bd->data.lso_mss =
			cpu_to_le16(skb_shinfo(skb)->gso_size);

		if (unlikely(xmit_type & XMIT_ENC)) {
			first_bd->data.bd_flags.bitfields |=
				1 << ETH_TX_1ST_BD_FLAGS_TUNN_IP_CSUM_SHIFT;

			if (xmit_type & XMIT_ENC_GSO_L4_CSUM) {
				u8 tmp = ETH_TX_1ST_BD_FLAGS_TUNN_L4_CSUM_SHIFT;

				first_bd->data.bd_flags.bitfields |= 1 << tmp;
			}
			hlen = qede_get_skb_hlen(skb, true);
		} else {
			first_bd->data.bd_flags.bitfields |=
				1 << ETH_TX_1ST_BD_FLAGS_IP_CSUM_SHIFT;
			hlen = qede_get_skb_hlen(skb, false);
		}

		/* @@@TBD - if will not be removed need to check */
		third_bd->data.bitfields |=
			cpu_to_le16(1 << ETH_TX_DATA_3RD_BD_HDR_NBD_SHIFT);

		/* Make life easier for FW guys who can't deal with header and
		 * data on same BD. If we need to split, use the second bd...
		 */
		if (unlikely(skb_headlen(skb) > hlen)) {
			DP_VERBOSE(edev, NETIF_MSG_TX_QUEUED,
				   "TSO split header size is %d (%x:%x)\n",
				   first_bd->nbytes, first_bd->addr.hi,
				   first_bd->addr.lo);

			mapping = HILO_U64(le32_to_cpu(first_bd->addr.hi),
					   le32_to_cpu(first_bd->addr.lo)) +
					   hlen;

			BD_SET_UNMAP_ADDR_LEN(tx_data_bd, mapping,
					      le16_to_cpu(first_bd->nbytes) -
					      hlen);

			/* this marks the BD as one that has no
			 * individual mapping
			 */
			txq->sw_tx_ring.skbs[idx].flags |= QEDE_TSO_SPLIT_BD;

			first_bd->nbytes = cpu_to_le16(hlen);

			tx_data_bd = (struct eth_tx_bd *)third_bd;
			data_split = true;
		}
	} else {
		val |= ((skb->len & ETH_TX_DATA_1ST_BD_PKT_LEN_MASK) <<
			 ETH_TX_DATA_1ST_BD_PKT_LEN_SHIFT);
	}

	first_bd->data.bitfields = cpu_to_le16(val);

	/* Handle fragmented skb */
	/* special handle for frags inside 2nd and 3rd bds.. */
	while (tx_data_bd && frag_idx < skb_shinfo(skb)->nr_frags) {
		rc = map_frag_to_bd(txq,
				    &skb_shinfo(skb)->frags[frag_idx],
				    tx_data_bd);
		if (rc) {
			qede_free_failed_tx_pkt(txq, first_bd, nbd, data_split);
			qede_update_tx_producer(txq);
			return NETDEV_TX_OK;
		}

		if (tx_data_bd == (struct eth_tx_bd *)second_bd)
			tx_data_bd = (struct eth_tx_bd *)third_bd;
		else
			tx_data_bd = NULL;

		frag_idx++;
	}

	/* map last frags into 4th, 5th .... */
	for (; frag_idx < skb_shinfo(skb)->nr_frags; frag_idx++, nbd++) {
		tx_data_bd = (struct eth_tx_bd *)
			     qed_chain_produce(&txq->tx_pbl);

		memset(tx_data_bd, 0, sizeof(*tx_data_bd));

		rc = map_frag_to_bd(txq,
				    &skb_shinfo(skb)->frags[frag_idx],
				    tx_data_bd);
		if (rc) {
			qede_free_failed_tx_pkt(txq, first_bd, nbd, data_split);
			qede_update_tx_producer(txq);
			return NETDEV_TX_OK;
		}
	}

	/* update the first BD with the actual num BDs */
	first_bd->data.nbds = nbd;

	netdev_tx_sent_queue(netdev_txq, skb->len);

	skb_tx_timestamp(skb);

	/* Advance packet producer only before sending the packet since mapping
	 * of pages may fail.
	 */
	txq->sw_tx_prod = (txq->sw_tx_prod + 1) % txq->num_tx_buffers;

	/* 'next page' entries are counted in the producer value */
	txq->tx_db.data.bd_prod =
		cpu_to_le16(qed_chain_get_prod_idx(&txq->tx_pbl));

	if (!netdev_xmit_more() || netif_xmit_stopped(netdev_txq))
		qede_update_tx_producer(txq);

	if (unlikely(qed_chain_get_elem_left(&txq->tx_pbl)
		      < (MAX_SKB_FRAGS + 1))) {
		if (netdev_xmit_more())
			qede_update_tx_producer(txq);

		netif_tx_stop_queue(netdev_txq);
		txq->stopped_cnt++;
		DP_VERBOSE(edev, NETIF_MSG_TX_QUEUED,
			   "Stop queue was called\n");
		/* paired memory barrier is in qede_tx_int(), we have to keep
		 * ordering of set_bit() in netif_tx_stop_queue() and read of
		 * fp->bd_tx_cons
		 */
		smp_mb();

		if ((qed_chain_get_elem_left(&txq->tx_pbl) >=
		     (MAX_SKB_FRAGS + 1)) &&
		    (edev->state == QEDE_STATE_OPEN)) {
			netif_tx_wake_queue(netdev_txq);
			DP_VERBOSE(edev, NETIF_MSG_TX_QUEUED,
				   "Wake queue was called\n");
		}
	}

	return NETDEV_TX_OK;
}

u16 qede_select_queue(struct net_device *dev, struct sk_buff *skb,
		      struct net_device *sb_dev)
{
	struct qede_dev *edev = netdev_priv(dev);
	int total_txq;

	total_txq = QEDE_TSS_COUNT(edev) * edev->dev_info.num_tc;

	return QEDE_TSS_COUNT(edev) ?
		netdev_pick_tx(dev, skb, NULL) % total_txq :  0;
}

/* 8B udp header + 8B base tunnel header + 32B option length */
#define QEDE_MAX_TUN_HDR_LEN 48

netdev_features_t qede_features_check(struct sk_buff *skb,
				      struct net_device *dev,
				      netdev_features_t features)
{
	if (skb->encapsulation) {
		u8 l4_proto = 0;

		switch (vlan_get_protocol(skb)) {
		case htons(ETH_P_IP):
			l4_proto = ip_hdr(skb)->protocol;
			break;
		case htons(ETH_P_IPV6):
			l4_proto = ipv6_hdr(skb)->nexthdr;
			break;
		default:
			return features;
		}

		/* Disable offloads for geneve tunnels, as HW can't parse
		 * the geneve header which has option length greater than 32b
		 * and disable offloads for the ports which are not offloaded.
		 */
		if (l4_proto == IPPROTO_UDP) {
			struct qede_dev *edev = netdev_priv(dev);
			u16 hdrlen, vxln_port, gnv_port;

			hdrlen = QEDE_MAX_TUN_HDR_LEN;
			vxln_port = edev->vxlan_dst_port;
			gnv_port = edev->geneve_dst_port;

			if ((skb_inner_mac_header(skb) -
			     skb_transport_header(skb)) > hdrlen ||
			     (ntohs(udp_hdr(skb)->dest) != vxln_port &&
			      ntohs(udp_hdr(skb)->dest) != gnv_port))
				return features & ~(NETIF_F_CSUM_MASK |
						    NETIF_F_GSO_MASK);
		}
	}

	return features;
}
