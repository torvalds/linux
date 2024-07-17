// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include <net/libeth/rx.h>

#include "idpf.h"

/**
 * idpf_tx_singleq_csum - Enable tx checksum offloads
 * @skb: pointer to skb
 * @off: pointer to struct that holds offload parameters
 *
 * Returns 0 or error (negative) if checksum offload cannot be executed, 1
 * otherwise.
 */
static int idpf_tx_singleq_csum(struct sk_buff *skb,
				struct idpf_tx_offload_params *off)
{
	u32 l4_len, l3_len, l2_len;
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} ip;
	union {
		struct tcphdr *tcp;
		unsigned char *hdr;
	} l4;
	u32 offset, cmd = 0;
	u8 l4_proto = 0;
	__be16 frag_off;
	bool is_tso;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	ip.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* compute outer L2 header size */
	l2_len = ip.hdr - skb->data;
	offset = FIELD_PREP(0x3F << IDPF_TX_DESC_LEN_MACLEN_S, l2_len / 2);
	is_tso = !!(off->tx_flags & IDPF_TX_FLAGS_TSO);
	if (skb->encapsulation) {
		u32 tunnel = 0;

		/* define outer network header type */
		if (off->tx_flags & IDPF_TX_FLAGS_IPV4) {
			/* The stack computes the IP header already, the only
			 * time we need the hardware to recompute it is in the
			 * case of TSO.
			 */
			tunnel |= is_tso ?
				  IDPF_TX_CTX_EXT_IP_IPV4 :
				  IDPF_TX_CTX_EXT_IP_IPV4_NO_CSUM;

			l4_proto = ip.v4->protocol;
		} else if (off->tx_flags & IDPF_TX_FLAGS_IPV6) {
			tunnel |= IDPF_TX_CTX_EXT_IP_IPV6;

			l4_proto = ip.v6->nexthdr;
			if (ipv6_ext_hdr(l4_proto))
				ipv6_skip_exthdr(skb, skb_network_offset(skb) +
						 sizeof(*ip.v6),
						 &l4_proto, &frag_off);
		}

		/* define outer transport */
		switch (l4_proto) {
		case IPPROTO_UDP:
			tunnel |= IDPF_TXD_CTX_UDP_TUNNELING;
			break;
		case IPPROTO_GRE:
			tunnel |= IDPF_TXD_CTX_GRE_TUNNELING;
			break;
		case IPPROTO_IPIP:
		case IPPROTO_IPV6:
			l4.hdr = skb_inner_network_header(skb);
			break;
		default:
			if (is_tso)
				return -1;

			skb_checksum_help(skb);

			return 0;
		}
		off->tx_flags |= IDPF_TX_FLAGS_TUNNEL;

		/* compute outer L3 header size */
		tunnel |= FIELD_PREP(IDPF_TXD_CTX_QW0_TUNN_EXT_IPLEN_M,
				     (l4.hdr - ip.hdr) / 4);

		/* switch IP header pointer from outer to inner header */
		ip.hdr = skb_inner_network_header(skb);

		/* compute tunnel header size */
		tunnel |= FIELD_PREP(IDPF_TXD_CTX_QW0_TUNN_NATLEN_M,
				     (ip.hdr - l4.hdr) / 2);

		/* indicate if we need to offload outer UDP header */
		if (is_tso &&
		    !(skb_shinfo(skb)->gso_type & SKB_GSO_PARTIAL) &&
		    (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_TUNNEL_CSUM))
			tunnel |= IDPF_TXD_CTX_QW0_TUNN_L4T_CS_M;

		/* record tunnel offload values */
		off->cd_tunneling |= tunnel;

		/* switch L4 header pointer from outer to inner */
		l4.hdr = skb_inner_transport_header(skb);
		l4_proto = 0;

		/* reset type as we transition from outer to inner headers */
		off->tx_flags &= ~(IDPF_TX_FLAGS_IPV4 | IDPF_TX_FLAGS_IPV6);
		if (ip.v4->version == 4)
			off->tx_flags |= IDPF_TX_FLAGS_IPV4;
		if (ip.v6->version == 6)
			off->tx_flags |= IDPF_TX_FLAGS_IPV6;
	}

	/* Enable IP checksum offloads */
	if (off->tx_flags & IDPF_TX_FLAGS_IPV4) {
		l4_proto = ip.v4->protocol;
		/* See comment above regarding need for HW to recompute IP
		 * header checksum in the case of TSO.
		 */
		if (is_tso)
			cmd |= IDPF_TX_DESC_CMD_IIPT_IPV4_CSUM;
		else
			cmd |= IDPF_TX_DESC_CMD_IIPT_IPV4;

	} else if (off->tx_flags & IDPF_TX_FLAGS_IPV6) {
		cmd |= IDPF_TX_DESC_CMD_IIPT_IPV6;
		l4_proto = ip.v6->nexthdr;
		if (ipv6_ext_hdr(l4_proto))
			ipv6_skip_exthdr(skb, skb_network_offset(skb) +
					 sizeof(*ip.v6), &l4_proto,
					 &frag_off);
	} else {
		return -1;
	}

	/* compute inner L3 header size */
	l3_len = l4.hdr - ip.hdr;
	offset |= (l3_len / 4) << IDPF_TX_DESC_LEN_IPLEN_S;

	/* Enable L4 checksum offloads */
	switch (l4_proto) {
	case IPPROTO_TCP:
		/* enable checksum offloads */
		cmd |= IDPF_TX_DESC_CMD_L4T_EOFT_TCP;
		l4_len = l4.tcp->doff;
		break;
	case IPPROTO_UDP:
		/* enable UDP checksum offload */
		cmd |= IDPF_TX_DESC_CMD_L4T_EOFT_UDP;
		l4_len = sizeof(struct udphdr) >> 2;
		break;
	case IPPROTO_SCTP:
		/* enable SCTP checksum offload */
		cmd |= IDPF_TX_DESC_CMD_L4T_EOFT_SCTP;
		l4_len = sizeof(struct sctphdr) >> 2;
		break;
	default:
		if (is_tso)
			return -1;

		skb_checksum_help(skb);

		return 0;
	}

	offset |= l4_len << IDPF_TX_DESC_LEN_L4_LEN_S;
	off->td_cmd |= cmd;
	off->hdr_offsets |= offset;

	return 1;
}

/**
 * idpf_tx_singleq_map - Build the Tx base descriptor
 * @tx_q: queue to send buffer on
 * @first: first buffer info buffer to use
 * @offloads: pointer to struct that holds offload parameters
 *
 * This function loops over the skb data pointed to by *first
 * and gets a physical address for each memory location and programs
 * it and the length into the transmit base mode descriptor.
 */
static void idpf_tx_singleq_map(struct idpf_tx_queue *tx_q,
				struct idpf_tx_buf *first,
				struct idpf_tx_offload_params *offloads)
{
	u32 offsets = offloads->hdr_offsets;
	struct idpf_tx_buf *tx_buf = first;
	struct idpf_base_tx_desc *tx_desc;
	struct sk_buff *skb = first->skb;
	u64 td_cmd = offloads->td_cmd;
	unsigned int data_len, size;
	u16 i = tx_q->next_to_use;
	struct netdev_queue *nq;
	skb_frag_t *frag;
	dma_addr_t dma;
	u64 td_tag = 0;

	data_len = skb->data_len;
	size = skb_headlen(skb);

	tx_desc = &tx_q->base_tx[i];

	dma = dma_map_single(tx_q->dev, skb->data, size, DMA_TO_DEVICE);

	/* write each descriptor with CRC bit */
	if (idpf_queue_has(CRC_EN, tx_q))
		td_cmd |= IDPF_TX_DESC_CMD_ICRC;

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		unsigned int max_data = IDPF_TX_MAX_DESC_DATA_ALIGNED;

		if (dma_mapping_error(tx_q->dev, dma))
			return idpf_tx_dma_map_error(tx_q, skb, first, i);

		/* record length, and DMA address */
		dma_unmap_len_set(tx_buf, len, size);
		dma_unmap_addr_set(tx_buf, dma, dma);

		/* align size to end of page */
		max_data += -dma & (IDPF_TX_MAX_READ_REQ_SIZE - 1);
		tx_desc->buf_addr = cpu_to_le64(dma);

		/* account for data chunks larger than the hardware
		 * can handle
		 */
		while (unlikely(size > IDPF_TX_MAX_DESC_DATA)) {
			tx_desc->qw1 = idpf_tx_singleq_build_ctob(td_cmd,
								  offsets,
								  max_data,
								  td_tag);
			tx_desc++;
			i++;

			if (i == tx_q->desc_count) {
				tx_desc = &tx_q->base_tx[0];
				i = 0;
			}

			dma += max_data;
			size -= max_data;

			max_data = IDPF_TX_MAX_DESC_DATA_ALIGNED;
			tx_desc->buf_addr = cpu_to_le64(dma);
		}

		if (!data_len)
			break;

		tx_desc->qw1 = idpf_tx_singleq_build_ctob(td_cmd, offsets,
							  size, td_tag);
		tx_desc++;
		i++;

		if (i == tx_q->desc_count) {
			tx_desc = &tx_q->base_tx[0];
			i = 0;
		}

		size = skb_frag_size(frag);
		data_len -= size;

		dma = skb_frag_dma_map(tx_q->dev, frag, 0, size,
				       DMA_TO_DEVICE);

		tx_buf = &tx_q->tx_buf[i];
	}

	skb_tx_timestamp(first->skb);

	/* write last descriptor with RS and EOP bits */
	td_cmd |= (u64)(IDPF_TX_DESC_CMD_EOP | IDPF_TX_DESC_CMD_RS);

	tx_desc->qw1 = idpf_tx_singleq_build_ctob(td_cmd, offsets,
						  size, td_tag);

	IDPF_SINGLEQ_BUMP_RING_IDX(tx_q, i);

	/* set next_to_watch value indicating a packet is present */
	first->next_to_watch = tx_desc;

	nq = netdev_get_tx_queue(tx_q->netdev, tx_q->idx);
	netdev_tx_sent_queue(nq, first->bytecount);

	idpf_tx_buf_hw_update(tx_q, i, netdev_xmit_more());
}

/**
 * idpf_tx_singleq_get_ctx_desc - grab next desc and update buffer ring
 * @txq: queue to put context descriptor on
 *
 * Since the TX buffer rings mimics the descriptor ring, update the tx buffer
 * ring entry to reflect that this index is a context descriptor
 */
static struct idpf_base_tx_ctx_desc *
idpf_tx_singleq_get_ctx_desc(struct idpf_tx_queue *txq)
{
	struct idpf_base_tx_ctx_desc *ctx_desc;
	int ntu = txq->next_to_use;

	memset(&txq->tx_buf[ntu], 0, sizeof(struct idpf_tx_buf));
	txq->tx_buf[ntu].ctx_entry = true;

	ctx_desc = &txq->base_ctx[ntu];

	IDPF_SINGLEQ_BUMP_RING_IDX(txq, ntu);
	txq->next_to_use = ntu;

	return ctx_desc;
}

/**
 * idpf_tx_singleq_build_ctx_desc - populate context descriptor
 * @txq: queue to send buffer on
 * @offload: offload parameter structure
 **/
static void idpf_tx_singleq_build_ctx_desc(struct idpf_tx_queue *txq,
					   struct idpf_tx_offload_params *offload)
{
	struct idpf_base_tx_ctx_desc *desc = idpf_tx_singleq_get_ctx_desc(txq);
	u64 qw1 = (u64)IDPF_TX_DESC_DTYPE_CTX;

	if (offload->tso_segs) {
		qw1 |= IDPF_TX_CTX_DESC_TSO << IDPF_TXD_CTX_QW1_CMD_S;
		qw1 |= FIELD_PREP(IDPF_TXD_CTX_QW1_TSO_LEN_M,
				  offload->tso_len);
		qw1 |= FIELD_PREP(IDPF_TXD_CTX_QW1_MSS_M, offload->mss);

		u64_stats_update_begin(&txq->stats_sync);
		u64_stats_inc(&txq->q_stats.lso_pkts);
		u64_stats_update_end(&txq->stats_sync);
	}

	desc->qw0.tunneling_params = cpu_to_le32(offload->cd_tunneling);

	desc->qw0.l2tag2 = 0;
	desc->qw0.rsvd1 = 0;
	desc->qw1 = cpu_to_le64(qw1);
}

/**
 * idpf_tx_singleq_frame - Sends buffer on Tx ring using base descriptors
 * @skb: send buffer
 * @tx_q: queue to send buffer on
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
netdev_tx_t idpf_tx_singleq_frame(struct sk_buff *skb,
				  struct idpf_tx_queue *tx_q)
{
	struct idpf_tx_offload_params offload = { };
	struct idpf_tx_buf *first;
	unsigned int count;
	__be16 protocol;
	int csum, tso;

	count = idpf_tx_desc_count_required(tx_q, skb);
	if (unlikely(!count))
		return idpf_tx_drop_skb(tx_q, skb);

	if (idpf_tx_maybe_stop_common(tx_q,
				      count + IDPF_TX_DESCS_PER_CACHE_LINE +
				      IDPF_TX_DESCS_FOR_CTX)) {
		idpf_tx_buf_hw_update(tx_q, tx_q->next_to_use, false);

		return NETDEV_TX_BUSY;
	}

	protocol = vlan_get_protocol(skb);
	if (protocol == htons(ETH_P_IP))
		offload.tx_flags |= IDPF_TX_FLAGS_IPV4;
	else if (protocol == htons(ETH_P_IPV6))
		offload.tx_flags |= IDPF_TX_FLAGS_IPV6;

	tso = idpf_tso(skb, &offload);
	if (tso < 0)
		goto out_drop;

	csum = idpf_tx_singleq_csum(skb, &offload);
	if (csum < 0)
		goto out_drop;

	if (tso || offload.cd_tunneling)
		idpf_tx_singleq_build_ctx_desc(tx_q, &offload);

	/* record the location of the first descriptor for this packet */
	first = &tx_q->tx_buf[tx_q->next_to_use];
	first->skb = skb;

	if (tso) {
		first->gso_segs = offload.tso_segs;
		first->bytecount = skb->len + ((first->gso_segs - 1) * offload.tso_hdr_len);
	} else {
		first->bytecount = max_t(unsigned int, skb->len, ETH_ZLEN);
		first->gso_segs = 1;
	}
	idpf_tx_singleq_map(tx_q, first, &offload);

	return NETDEV_TX_OK;

out_drop:
	return idpf_tx_drop_skb(tx_q, skb);
}

/**
 * idpf_tx_singleq_clean - Reclaim resources from queue
 * @tx_q: Tx queue to clean
 * @napi_budget: Used to determine if we are in netpoll
 * @cleaned: returns number of packets cleaned
 *
 */
static bool idpf_tx_singleq_clean(struct idpf_tx_queue *tx_q, int napi_budget,
				  int *cleaned)
{
	unsigned int total_bytes = 0, total_pkts = 0;
	struct idpf_base_tx_desc *tx_desc;
	u32 budget = tx_q->clean_budget;
	s16 ntc = tx_q->next_to_clean;
	struct idpf_netdev_priv *np;
	struct idpf_tx_buf *tx_buf;
	struct netdev_queue *nq;
	bool dont_wake;

	tx_desc = &tx_q->base_tx[ntc];
	tx_buf = &tx_q->tx_buf[ntc];
	ntc -= tx_q->desc_count;

	do {
		struct idpf_base_tx_desc *eop_desc;

		/* If this entry in the ring was used as a context descriptor,
		 * it's corresponding entry in the buffer ring will indicate as
		 * such. We can skip this descriptor since there is no buffer
		 * to clean.
		 */
		if (tx_buf->ctx_entry) {
			/* Clear this flag here to avoid stale flag values when
			 * this buffer is used for actual data in the future.
			 * There are cases where the tx_buf struct / the flags
			 * field will not be cleared before being reused.
			 */
			tx_buf->ctx_entry = false;
			goto fetch_next_txq_desc;
		}

		/* if next_to_watch is not set then no work pending */
		eop_desc = (struct idpf_base_tx_desc *)tx_buf->next_to_watch;
		if (!eop_desc)
			break;

		/* prevent any other reads prior to eop_desc */
		smp_rmb();

		/* if the descriptor isn't done, no work yet to do */
		if (!(eop_desc->qw1 &
		      cpu_to_le64(IDPF_TX_DESC_DTYPE_DESC_DONE)))
			break;

		/* clear next_to_watch to prevent false hangs */
		tx_buf->next_to_watch = NULL;

		/* update the statistics for this packet */
		total_bytes += tx_buf->bytecount;
		total_pkts += tx_buf->gso_segs;

		napi_consume_skb(tx_buf->skb, napi_budget);

		/* unmap skb header data */
		dma_unmap_single(tx_q->dev,
				 dma_unmap_addr(tx_buf, dma),
				 dma_unmap_len(tx_buf, len),
				 DMA_TO_DEVICE);

		/* clear tx_buf data */
		tx_buf->skb = NULL;
		dma_unmap_len_set(tx_buf, len, 0);

		/* unmap remaining buffers */
		while (tx_desc != eop_desc) {
			tx_buf++;
			tx_desc++;
			ntc++;
			if (unlikely(!ntc)) {
				ntc -= tx_q->desc_count;
				tx_buf = tx_q->tx_buf;
				tx_desc = &tx_q->base_tx[0];
			}

			/* unmap any remaining paged data */
			if (dma_unmap_len(tx_buf, len)) {
				dma_unmap_page(tx_q->dev,
					       dma_unmap_addr(tx_buf, dma),
					       dma_unmap_len(tx_buf, len),
					       DMA_TO_DEVICE);
				dma_unmap_len_set(tx_buf, len, 0);
			}
		}

		/* update budget only if we did something */
		budget--;

fetch_next_txq_desc:
		tx_buf++;
		tx_desc++;
		ntc++;
		if (unlikely(!ntc)) {
			ntc -= tx_q->desc_count;
			tx_buf = tx_q->tx_buf;
			tx_desc = &tx_q->base_tx[0];
		}
	} while (likely(budget));

	ntc += tx_q->desc_count;
	tx_q->next_to_clean = ntc;

	*cleaned += total_pkts;

	u64_stats_update_begin(&tx_q->stats_sync);
	u64_stats_add(&tx_q->q_stats.packets, total_pkts);
	u64_stats_add(&tx_q->q_stats.bytes, total_bytes);
	u64_stats_update_end(&tx_q->stats_sync);

	np = netdev_priv(tx_q->netdev);
	nq = netdev_get_tx_queue(tx_q->netdev, tx_q->idx);

	dont_wake = np->state != __IDPF_VPORT_UP ||
		    !netif_carrier_ok(tx_q->netdev);
	__netif_txq_completed_wake(nq, total_pkts, total_bytes,
				   IDPF_DESC_UNUSED(tx_q), IDPF_TX_WAKE_THRESH,
				   dont_wake);

	return !!budget;
}

/**
 * idpf_tx_singleq_clean_all - Clean all Tx queues
 * @q_vec: queue vector
 * @budget: Used to determine if we are in netpoll
 * @cleaned: returns number of packets cleaned
 *
 * Returns false if clean is not complete else returns true
 */
static bool idpf_tx_singleq_clean_all(struct idpf_q_vector *q_vec, int budget,
				      int *cleaned)
{
	u16 num_txq = q_vec->num_txq;
	bool clean_complete = true;
	int i, budget_per_q;

	budget_per_q = num_txq ? max(budget / num_txq, 1) : 0;
	for (i = 0; i < num_txq; i++) {
		struct idpf_tx_queue *q;

		q = q_vec->tx[i];
		clean_complete &= idpf_tx_singleq_clean(q, budget_per_q,
							cleaned);
	}

	return clean_complete;
}

/**
 * idpf_rx_singleq_test_staterr - tests bits in Rx descriptor
 * status and error fields
 * @rx_desc: pointer to receive descriptor (in le64 format)
 * @stat_err_bits: value to mask
 *
 * This function does some fast chicanery in order to return the
 * value of the mask which is really only used for boolean tests.
 * The status_error_ptype_len doesn't need to be shifted because it begins
 * at offset zero.
 */
static bool idpf_rx_singleq_test_staterr(const union virtchnl2_rx_desc *rx_desc,
					 const u64 stat_err_bits)
{
	return !!(rx_desc->base_wb.qword1.status_error_ptype_len &
		  cpu_to_le64(stat_err_bits));
}

/**
 * idpf_rx_singleq_is_non_eop - process handling of non-EOP buffers
 * @rx_desc: Rx descriptor for current buffer
 */
static bool idpf_rx_singleq_is_non_eop(const union virtchnl2_rx_desc *rx_desc)
{
	/* if we are the last buffer then there is nothing else to do */
	if (likely(idpf_rx_singleq_test_staterr(rx_desc, IDPF_RXD_EOF_SINGLEQ)))
		return false;

	return true;
}

/**
 * idpf_rx_singleq_csum - Indicate in skb if checksum is good
 * @rxq: Rx ring being processed
 * @skb: skb currently being received and modified
 * @csum_bits: checksum bits from descriptor
 * @decoded: the packet type decoded by hardware
 *
 * skb->protocol must be set before this function is called
 */
static void idpf_rx_singleq_csum(struct idpf_rx_queue *rxq,
				 struct sk_buff *skb,
				 struct idpf_rx_csum_decoded csum_bits,
				 struct libeth_rx_pt decoded)
{
	bool ipv4, ipv6;

	/* check if Rx checksum is enabled */
	if (!libeth_rx_pt_has_checksum(rxq->netdev, decoded))
		return;

	/* check if HW has decoded the packet and checksum */
	if (unlikely(!csum_bits.l3l4p))
		return;

	ipv4 = libeth_rx_pt_get_ip_ver(decoded) == LIBETH_RX_PT_OUTER_IPV4;
	ipv6 = libeth_rx_pt_get_ip_ver(decoded) == LIBETH_RX_PT_OUTER_IPV6;

	/* Check if there were any checksum errors */
	if (unlikely(ipv4 && (csum_bits.ipe || csum_bits.eipe)))
		goto checksum_fail;

	/* Device could not do any checksum offload for certain extension
	 * headers as indicated by setting IPV6EXADD bit
	 */
	if (unlikely(ipv6 && csum_bits.ipv6exadd))
		return;

	/* check for L4 errors and handle packets that were not able to be
	 * checksummed due to arrival speed
	 */
	if (unlikely(csum_bits.l4e))
		goto checksum_fail;

	if (unlikely(csum_bits.nat && csum_bits.eudpe))
		goto checksum_fail;

	/* Handle packets that were not able to be checksummed due to arrival
	 * speed, in this case the stack can compute the csum.
	 */
	if (unlikely(csum_bits.pprs))
		return;

	/* If there is an outer header present that might contain a checksum
	 * we need to bump the checksum level by 1 to reflect the fact that
	 * we are indicating we validated the inner checksum.
	 */
	if (decoded.tunnel_type >= LIBETH_RX_PT_TUNNEL_IP_GRENAT)
		skb->csum_level = 1;

	skb->ip_summed = CHECKSUM_UNNECESSARY;
	return;

checksum_fail:
	u64_stats_update_begin(&rxq->stats_sync);
	u64_stats_inc(&rxq->q_stats.hw_csum_err);
	u64_stats_update_end(&rxq->stats_sync);
}

/**
 * idpf_rx_singleq_base_csum - Indicate in skb if hw indicated a good cksum
 * @rx_desc: the receive descriptor
 *
 * This function only operates on the VIRTCHNL2_RXDID_1_32B_BASE_M base 32byte
 * descriptor writeback format.
 *
 * Return: parsed checksum status.
 **/
static struct idpf_rx_csum_decoded
idpf_rx_singleq_base_csum(const union virtchnl2_rx_desc *rx_desc)
{
	struct idpf_rx_csum_decoded csum_bits = { };
	u32 rx_error, rx_status;
	u64 qword;

	qword = le64_to_cpu(rx_desc->base_wb.qword1.status_error_ptype_len);

	rx_status = FIELD_GET(VIRTCHNL2_RX_BASE_DESC_QW1_STATUS_M, qword);
	rx_error = FIELD_GET(VIRTCHNL2_RX_BASE_DESC_QW1_ERROR_M, qword);

	csum_bits.ipe = FIELD_GET(VIRTCHNL2_RX_BASE_DESC_ERROR_IPE_M, rx_error);
	csum_bits.eipe = FIELD_GET(VIRTCHNL2_RX_BASE_DESC_ERROR_EIPE_M,
				   rx_error);
	csum_bits.l4e = FIELD_GET(VIRTCHNL2_RX_BASE_DESC_ERROR_L4E_M, rx_error);
	csum_bits.pprs = FIELD_GET(VIRTCHNL2_RX_BASE_DESC_ERROR_PPRS_M,
				   rx_error);
	csum_bits.l3l4p = FIELD_GET(VIRTCHNL2_RX_BASE_DESC_STATUS_L3L4P_M,
				    rx_status);
	csum_bits.ipv6exadd = FIELD_GET(VIRTCHNL2_RX_BASE_DESC_STATUS_IPV6EXADD_M,
					rx_status);

	return csum_bits;
}

/**
 * idpf_rx_singleq_flex_csum - Indicate in skb if hw indicated a good cksum
 * @rx_desc: the receive descriptor
 *
 * This function only operates on the VIRTCHNL2_RXDID_2_FLEX_SQ_NIC flexible
 * descriptor writeback format.
 *
 * Return: parsed checksum status.
 **/
static struct idpf_rx_csum_decoded
idpf_rx_singleq_flex_csum(const union virtchnl2_rx_desc *rx_desc)
{
	struct idpf_rx_csum_decoded csum_bits = { };
	u16 rx_status0, rx_status1;

	rx_status0 = le16_to_cpu(rx_desc->flex_nic_wb.status_error0);
	rx_status1 = le16_to_cpu(rx_desc->flex_nic_wb.status_error1);

	csum_bits.ipe = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_STATUS0_XSUM_IPE_M,
				  rx_status0);
	csum_bits.eipe = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_STATUS0_XSUM_EIPE_M,
				   rx_status0);
	csum_bits.l4e = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_STATUS0_XSUM_L4E_M,
				  rx_status0);
	csum_bits.eudpe = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_STATUS0_XSUM_EUDPE_M,
				    rx_status0);
	csum_bits.l3l4p = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_STATUS0_L3L4P_M,
				    rx_status0);
	csum_bits.ipv6exadd = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_STATUS0_IPV6EXADD_M,
					rx_status0);
	csum_bits.nat = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_STATUS1_NAT_M,
				  rx_status1);

	return csum_bits;
}

/**
 * idpf_rx_singleq_base_hash - set the hash value in the skb
 * @rx_q: Rx completion queue
 * @skb: skb currently being received and modified
 * @rx_desc: specific descriptor
 * @decoded: Decoded Rx packet type related fields
 *
 * This function only operates on the VIRTCHNL2_RXDID_1_32B_BASE_M base 32byte
 * descriptor writeback format.
 **/
static void idpf_rx_singleq_base_hash(struct idpf_rx_queue *rx_q,
				      struct sk_buff *skb,
				      const union virtchnl2_rx_desc *rx_desc,
				      struct libeth_rx_pt decoded)
{
	u64 mask, qw1;

	if (!libeth_rx_pt_has_hash(rx_q->netdev, decoded))
		return;

	mask = VIRTCHNL2_RX_BASE_DESC_FLTSTAT_RSS_HASH_M;
	qw1 = le64_to_cpu(rx_desc->base_wb.qword1.status_error_ptype_len);

	if (FIELD_GET(mask, qw1) == mask) {
		u32 hash = le32_to_cpu(rx_desc->base_wb.qword0.hi_dword.rss);

		libeth_rx_pt_set_hash(skb, hash, decoded);
	}
}

/**
 * idpf_rx_singleq_flex_hash - set the hash value in the skb
 * @rx_q: Rx completion queue
 * @skb: skb currently being received and modified
 * @rx_desc: specific descriptor
 * @decoded: Decoded Rx packet type related fields
 *
 * This function only operates on the VIRTCHNL2_RXDID_2_FLEX_SQ_NIC flexible
 * descriptor writeback format.
 **/
static void idpf_rx_singleq_flex_hash(struct idpf_rx_queue *rx_q,
				      struct sk_buff *skb,
				      const union virtchnl2_rx_desc *rx_desc,
				      struct libeth_rx_pt decoded)
{
	if (!libeth_rx_pt_has_hash(rx_q->netdev, decoded))
		return;

	if (FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_STATUS0_RSS_VALID_M,
		      le16_to_cpu(rx_desc->flex_nic_wb.status_error0))) {
		u32 hash = le32_to_cpu(rx_desc->flex_nic_wb.rss_hash);

		libeth_rx_pt_set_hash(skb, hash, decoded);
	}
}

/**
 * idpf_rx_singleq_process_skb_fields - Populate skb header fields from Rx
 * descriptor
 * @rx_q: Rx ring being processed
 * @skb: pointer to current skb being populated
 * @rx_desc: descriptor for skb
 * @ptype: packet type
 *
 * This function checks the ring, descriptor, and packet information in
 * order to populate the hash, checksum, VLAN, protocol, and
 * other fields within the skb.
 */
static void
idpf_rx_singleq_process_skb_fields(struct idpf_rx_queue *rx_q,
				   struct sk_buff *skb,
				   const union virtchnl2_rx_desc *rx_desc,
				   u16 ptype)
{
	struct libeth_rx_pt decoded = rx_q->rx_ptype_lkup[ptype];
	struct idpf_rx_csum_decoded csum_bits;

	/* modifies the skb - consumes the enet header */
	skb->protocol = eth_type_trans(skb, rx_q->netdev);

	/* Check if we're using base mode descriptor IDs */
	if (rx_q->rxdids == VIRTCHNL2_RXDID_1_32B_BASE_M) {
		idpf_rx_singleq_base_hash(rx_q, skb, rx_desc, decoded);
		csum_bits = idpf_rx_singleq_base_csum(rx_desc);
	} else {
		idpf_rx_singleq_flex_hash(rx_q, skb, rx_desc, decoded);
		csum_bits = idpf_rx_singleq_flex_csum(rx_desc);
	}

	idpf_rx_singleq_csum(rx_q, skb, csum_bits, decoded);
	skb_record_rx_queue(skb, rx_q->idx);
}

/**
 * idpf_rx_buf_hw_update - Store the new tail and head values
 * @rxq: queue to bump
 * @val: new head index
 */
static void idpf_rx_buf_hw_update(struct idpf_rx_queue *rxq, u32 val)
{
	rxq->next_to_use = val;

	if (unlikely(!rxq->tail))
		return;

	/* writel has an implicit memory barrier */
	writel(val, rxq->tail);
}

/**
 * idpf_rx_singleq_buf_hw_alloc_all - Replace used receive buffers
 * @rx_q: queue for which the hw buffers are allocated
 * @cleaned_count: number of buffers to replace
 *
 * Returns false if all allocations were successful, true if any fail
 */
bool idpf_rx_singleq_buf_hw_alloc_all(struct idpf_rx_queue *rx_q,
				      u16 cleaned_count)
{
	struct virtchnl2_singleq_rx_buf_desc *desc;
	const struct libeth_fq_fp fq = {
		.pp		= rx_q->pp,
		.fqes		= rx_q->rx_buf,
		.truesize	= rx_q->truesize,
		.count		= rx_q->desc_count,
	};
	u16 nta = rx_q->next_to_alloc;

	if (!cleaned_count)
		return false;

	desc = &rx_q->single_buf[nta];

	do {
		dma_addr_t addr;

		addr = libeth_rx_alloc(&fq, nta);
		if (addr == DMA_MAPPING_ERROR)
			break;

		/* Refresh the desc even if buffer_addrs didn't change
		 * because each write-back erases this info.
		 */
		desc->pkt_addr = cpu_to_le64(addr);
		desc->hdr_addr = 0;
		desc++;

		nta++;
		if (unlikely(nta == rx_q->desc_count)) {
			desc = &rx_q->single_buf[0];
			nta = 0;
		}

		cleaned_count--;
	} while (cleaned_count);

	if (rx_q->next_to_alloc != nta) {
		idpf_rx_buf_hw_update(rx_q, nta);
		rx_q->next_to_alloc = nta;
	}

	return !!cleaned_count;
}

/**
 * idpf_rx_singleq_extract_base_fields - Extract fields from the Rx descriptor
 * @rx_desc: the descriptor to process
 * @fields: storage for extracted values
 *
 * Decode the Rx descriptor and extract relevant information including the
 * size and Rx packet type.
 *
 * This function only operates on the VIRTCHNL2_RXDID_1_32B_BASE_M base 32byte
 * descriptor writeback format.
 */
static void
idpf_rx_singleq_extract_base_fields(const union virtchnl2_rx_desc *rx_desc,
				    struct idpf_rx_extracted *fields)
{
	u64 qword;

	qword = le64_to_cpu(rx_desc->base_wb.qword1.status_error_ptype_len);

	fields->size = FIELD_GET(VIRTCHNL2_RX_BASE_DESC_QW1_LEN_PBUF_M, qword);
	fields->rx_ptype = FIELD_GET(VIRTCHNL2_RX_BASE_DESC_QW1_PTYPE_M, qword);
}

/**
 * idpf_rx_singleq_extract_flex_fields - Extract fields from the Rx descriptor
 * @rx_desc: the descriptor to process
 * @fields: storage for extracted values
 *
 * Decode the Rx descriptor and extract relevant information including the
 * size and Rx packet type.
 *
 * This function only operates on the VIRTCHNL2_RXDID_2_FLEX_SQ_NIC flexible
 * descriptor writeback format.
 */
static void
idpf_rx_singleq_extract_flex_fields(const union virtchnl2_rx_desc *rx_desc,
				    struct idpf_rx_extracted *fields)
{
	fields->size = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_PKT_LEN_M,
				 le16_to_cpu(rx_desc->flex_nic_wb.pkt_len));
	fields->rx_ptype = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_PTYPE_M,
				     le16_to_cpu(rx_desc->flex_nic_wb.ptype_flex_flags0));
}

/**
 * idpf_rx_singleq_extract_fields - Extract fields from the Rx descriptor
 * @rx_q: Rx descriptor queue
 * @rx_desc: the descriptor to process
 * @fields: storage for extracted values
 *
 */
static void
idpf_rx_singleq_extract_fields(const struct idpf_rx_queue *rx_q,
			       const union virtchnl2_rx_desc *rx_desc,
			       struct idpf_rx_extracted *fields)
{
	if (rx_q->rxdids == VIRTCHNL2_RXDID_1_32B_BASE_M)
		idpf_rx_singleq_extract_base_fields(rx_desc, fields);
	else
		idpf_rx_singleq_extract_flex_fields(rx_desc, fields);
}

/**
 * idpf_rx_singleq_clean - Reclaim resources after receive completes
 * @rx_q: rx queue to clean
 * @budget: Total limit on number of packets to process
 *
 * Returns true if there's any budget left (e.g. the clean is finished)
 */
static int idpf_rx_singleq_clean(struct idpf_rx_queue *rx_q, int budget)
{
	unsigned int total_rx_bytes = 0, total_rx_pkts = 0;
	struct sk_buff *skb = rx_q->skb;
	u16 ntc = rx_q->next_to_clean;
	u16 cleaned_count = 0;
	bool failure = false;

	/* Process Rx packets bounded by budget */
	while (likely(total_rx_pkts < (unsigned int)budget)) {
		struct idpf_rx_extracted fields = { };
		union virtchnl2_rx_desc *rx_desc;
		struct idpf_rx_buf *rx_buf;

		/* get the Rx desc from Rx queue based on 'next_to_clean' */
		rx_desc = &rx_q->rx[ntc];

		/* status_error_ptype_len will always be zero for unused
		 * descriptors because it's cleared in cleanup, and overlaps
		 * with hdr_addr which is always zero because packet split
		 * isn't used, if the hardware wrote DD then the length will be
		 * non-zero
		 */
#define IDPF_RXD_DD VIRTCHNL2_RX_BASE_DESC_STATUS_DD_M
		if (!idpf_rx_singleq_test_staterr(rx_desc,
						  IDPF_RXD_DD))
			break;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc
		 */
		dma_rmb();

		idpf_rx_singleq_extract_fields(rx_q, rx_desc, &fields);

		rx_buf = &rx_q->rx_buf[ntc];
		if (!libeth_rx_sync_for_cpu(rx_buf, fields.size))
			goto skip_data;

		if (skb)
			idpf_rx_add_frag(rx_buf, skb, fields.size);
		else
			skb = idpf_rx_build_skb(rx_buf, fields.size);

		/* exit if we failed to retrieve a buffer */
		if (!skb)
			break;

skip_data:
		rx_buf->page = NULL;

		IDPF_SINGLEQ_BUMP_RING_IDX(rx_q, ntc);
		cleaned_count++;

		/* skip if it is non EOP desc */
		if (idpf_rx_singleq_is_non_eop(rx_desc) || unlikely(!skb))
			continue;

#define IDPF_RXD_ERR_S FIELD_PREP(VIRTCHNL2_RX_BASE_DESC_QW1_ERROR_M, \
				  VIRTCHNL2_RX_BASE_DESC_ERROR_RXE_M)
		if (unlikely(idpf_rx_singleq_test_staterr(rx_desc,
							  IDPF_RXD_ERR_S))) {
			dev_kfree_skb_any(skb);
			skb = NULL;
			continue;
		}

		/* pad skb if needed (to make valid ethernet frame) */
		if (eth_skb_pad(skb)) {
			skb = NULL;
			continue;
		}

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += skb->len;

		/* protocol */
		idpf_rx_singleq_process_skb_fields(rx_q, skb,
						   rx_desc, fields.rx_ptype);

		/* send completed skb up the stack */
		napi_gro_receive(rx_q->pp->p.napi, skb);
		skb = NULL;

		/* update budget accounting */
		total_rx_pkts++;
	}

	rx_q->skb = skb;

	rx_q->next_to_clean = ntc;

	page_pool_nid_changed(rx_q->pp, numa_mem_id());
	if (cleaned_count)
		failure = idpf_rx_singleq_buf_hw_alloc_all(rx_q, cleaned_count);

	u64_stats_update_begin(&rx_q->stats_sync);
	u64_stats_add(&rx_q->q_stats.packets, total_rx_pkts);
	u64_stats_add(&rx_q->q_stats.bytes, total_rx_bytes);
	u64_stats_update_end(&rx_q->stats_sync);

	/* guarantee a trip back through this routine if there was a failure */
	return failure ? budget : (int)total_rx_pkts;
}

/**
 * idpf_rx_singleq_clean_all - Clean all Rx queues
 * @q_vec: queue vector
 * @budget: Used to determine if we are in netpoll
 * @cleaned: returns number of packets cleaned
 *
 * Returns false if clean is not complete else returns true
 */
static bool idpf_rx_singleq_clean_all(struct idpf_q_vector *q_vec, int budget,
				      int *cleaned)
{
	u16 num_rxq = q_vec->num_rxq;
	bool clean_complete = true;
	int budget_per_q, i;

	/* We attempt to distribute budget to each Rx queue fairly, but don't
	 * allow the budget to go below 1 because that would exit polling early.
	 */
	budget_per_q = num_rxq ? max(budget / num_rxq, 1) : 0;
	for (i = 0; i < num_rxq; i++) {
		struct idpf_rx_queue *rxq = q_vec->rx[i];
		int pkts_cleaned_per_q;

		pkts_cleaned_per_q = idpf_rx_singleq_clean(rxq, budget_per_q);

		/* if we clean as many as budgeted, we must not be done */
		if (pkts_cleaned_per_q >= budget_per_q)
			clean_complete = false;
		*cleaned += pkts_cleaned_per_q;
	}

	return clean_complete;
}

/**
 * idpf_vport_singleq_napi_poll - NAPI handler
 * @napi: struct from which you get q_vector
 * @budget: budget provided by stack
 */
int idpf_vport_singleq_napi_poll(struct napi_struct *napi, int budget)
{
	struct idpf_q_vector *q_vector =
				container_of(napi, struct idpf_q_vector, napi);
	bool clean_complete;
	int work_done = 0;

	/* Handle case where we are called by netpoll with a budget of 0 */
	if (budget <= 0) {
		idpf_tx_singleq_clean_all(q_vector, budget, &work_done);

		return budget;
	}

	clean_complete = idpf_rx_singleq_clean_all(q_vector, budget,
						   &work_done);
	clean_complete &= idpf_tx_singleq_clean_all(q_vector, budget,
						    &work_done);

	/* If work not completed, return budget and polling will return */
	if (!clean_complete)
		return budget;

	work_done = min_t(int, work_done, budget - 1);

	/* Exit the polling mode, but don't re-enable interrupts if stack might
	 * poll us due to busy-polling
	 */
	if (likely(napi_complete_done(napi, work_done)))
		idpf_vport_intr_update_itr_ena_irq(q_vector);

	return work_done;
}
