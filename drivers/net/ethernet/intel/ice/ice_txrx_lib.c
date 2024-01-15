// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Intel Corporation. */

#include <linux/filter.h>

#include "ice_txrx_lib.h"
#include "ice_eswitch.h"
#include "ice_lib.h"

/**
 * ice_release_rx_desc - Store the new tail and head values
 * @rx_ring: ring to bump
 * @val: new head index
 */
void ice_release_rx_desc(struct ice_rx_ring *rx_ring, u16 val)
{
	u16 prev_ntu = rx_ring->next_to_use & ~0x7;

	rx_ring->next_to_use = val;

	/* update next to alloc since we have filled the ring */
	rx_ring->next_to_alloc = val;

	/* QRX_TAIL will be updated with any tail value, but hardware ignores
	 * the lower 3 bits. This makes it so we only bump tail on meaningful
	 * boundaries. Also, this allows us to bump tail on intervals of 8 up to
	 * the budget depending on the current traffic load.
	 */
	val &= ~0x7;
	if (prev_ntu != val) {
		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch. (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();
		writel(val, rx_ring->tail);
	}
}

/**
 * ice_ptype_to_htype - get a hash type
 * @ptype: the ptype value from the descriptor
 *
 * Returns appropriate hash type (such as PKT_HASH_TYPE_L2/L3/L4) to be used by
 * skb_set_hash based on PTYPE as parsed by HW Rx pipeline and is part of
 * Rx desc.
 */
static enum pkt_hash_types ice_ptype_to_htype(u16 ptype)
{
	struct ice_rx_ptype_decoded decoded = ice_decode_rx_desc_ptype(ptype);

	if (!decoded.known)
		return PKT_HASH_TYPE_NONE;
	if (decoded.payload_layer == ICE_RX_PTYPE_PAYLOAD_LAYER_PAY4)
		return PKT_HASH_TYPE_L4;
	if (decoded.payload_layer == ICE_RX_PTYPE_PAYLOAD_LAYER_PAY3)
		return PKT_HASH_TYPE_L3;
	if (decoded.outer_ip == ICE_RX_PTYPE_OUTER_L2)
		return PKT_HASH_TYPE_L2;

	return PKT_HASH_TYPE_NONE;
}

/**
 * ice_rx_hash - set the hash value in the skb
 * @rx_ring: descriptor ring
 * @rx_desc: specific descriptor
 * @skb: pointer to current skb
 * @rx_ptype: the ptype value from the descriptor
 */
static void
ice_rx_hash(struct ice_rx_ring *rx_ring, union ice_32b_rx_flex_desc *rx_desc,
	    struct sk_buff *skb, u16 rx_ptype)
{
	struct ice_32b_rx_flex_desc_nic *nic_mdid;
	u32 hash;

	if (!(rx_ring->netdev->features & NETIF_F_RXHASH))
		return;

	if (rx_desc->wb.rxdid != ICE_RXDID_FLEX_NIC)
		return;

	nic_mdid = (struct ice_32b_rx_flex_desc_nic *)rx_desc;
	hash = le32_to_cpu(nic_mdid->rss_hash);
	skb_set_hash(skb, hash, ice_ptype_to_htype(rx_ptype));
}

/**
 * ice_rx_csum - Indicate in skb if checksum is good
 * @ring: the ring we care about
 * @skb: skb currently being received and modified
 * @rx_desc: the receive descriptor
 * @ptype: the packet type decoded by hardware
 *
 * skb->protocol must be set before this function is called
 */
static void
ice_rx_csum(struct ice_rx_ring *ring, struct sk_buff *skb,
	    union ice_32b_rx_flex_desc *rx_desc, u16 ptype)
{
	struct ice_rx_ptype_decoded decoded;
	u16 rx_status0, rx_status1;
	bool ipv4, ipv6;

	rx_status0 = le16_to_cpu(rx_desc->wb.status_error0);
	rx_status1 = le16_to_cpu(rx_desc->wb.status_error1);

	decoded = ice_decode_rx_desc_ptype(ptype);

	/* Start with CHECKSUM_NONE and by default csum_level = 0 */
	skb->ip_summed = CHECKSUM_NONE;
	skb_checksum_none_assert(skb);

	/* check if Rx checksum is enabled */
	if (!(ring->netdev->features & NETIF_F_RXCSUM))
		return;

	/* check if HW has decoded the packet and checksum */
	if (!(rx_status0 & BIT(ICE_RX_FLEX_DESC_STATUS0_L3L4P_S)))
		return;

	if (!(decoded.known && decoded.outer_ip))
		return;

	ipv4 = (decoded.outer_ip == ICE_RX_PTYPE_OUTER_IP) &&
	       (decoded.outer_ip_ver == ICE_RX_PTYPE_OUTER_IPV4);
	ipv6 = (decoded.outer_ip == ICE_RX_PTYPE_OUTER_IP) &&
	       (decoded.outer_ip_ver == ICE_RX_PTYPE_OUTER_IPV6);

	if (ipv4 && (rx_status0 & (BIT(ICE_RX_FLEX_DESC_STATUS0_XSUM_IPE_S) |
				   BIT(ICE_RX_FLEX_DESC_STATUS0_XSUM_EIPE_S))))
		goto checksum_fail;

	if (ipv6 && (rx_status0 & (BIT(ICE_RX_FLEX_DESC_STATUS0_IPV6EXADD_S))))
		goto checksum_fail;

	/* check for L4 errors and handle packets that were not able to be
	 * checksummed due to arrival speed
	 */
	if (rx_status0 & BIT(ICE_RX_FLEX_DESC_STATUS0_XSUM_L4E_S))
		goto checksum_fail;

	/* check for outer UDP checksum error in tunneled packets */
	if ((rx_status1 & BIT(ICE_RX_FLEX_DESC_STATUS1_NAT_S)) &&
	    (rx_status0 & BIT(ICE_RX_FLEX_DESC_STATUS0_XSUM_EUDPE_S)))
		goto checksum_fail;

	/* If there is an outer header present that might contain a checksum
	 * we need to bump the checksum level by 1 to reflect the fact that
	 * we are indicating we validated the inner checksum.
	 */
	if (decoded.tunnel_type >= ICE_RX_PTYPE_TUNNEL_IP_GRENAT)
		skb->csum_level = 1;

	/* Only report checksum unnecessary for TCP, UDP, or SCTP */
	switch (decoded.inner_prot) {
	case ICE_RX_PTYPE_INNER_PROT_TCP:
	case ICE_RX_PTYPE_INNER_PROT_UDP:
	case ICE_RX_PTYPE_INNER_PROT_SCTP:
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		break;
	default:
		break;
	}
	return;

checksum_fail:
	ring->vsi->back->hw_csum_rx_error++;
}

/**
 * ice_process_skb_fields - Populate skb header fields from Rx descriptor
 * @rx_ring: Rx descriptor ring packet is being transacted on
 * @rx_desc: pointer to the EOP Rx descriptor
 * @skb: pointer to current skb being populated
 * @ptype: the packet type decoded by hardware
 *
 * This function checks the ring, descriptor, and packet information in
 * order to populate the hash, checksum, VLAN, protocol, and
 * other fields within the skb.
 */
void
ice_process_skb_fields(struct ice_rx_ring *rx_ring,
		       union ice_32b_rx_flex_desc *rx_desc,
		       struct sk_buff *skb, u16 ptype)
{
	ice_rx_hash(rx_ring, rx_desc, skb, ptype);

	/* modifies the skb - consumes the enet header */
	skb->protocol = eth_type_trans(skb, rx_ring->netdev);

	ice_rx_csum(rx_ring, skb, rx_desc, ptype);

	if (rx_ring->ptp_rx)
		ice_ptp_rx_hwtstamp(rx_ring, rx_desc, skb);
}

/**
 * ice_receive_skb - Send a completed packet up the stack
 * @rx_ring: Rx ring in play
 * @skb: packet to send up
 * @vlan_tag: VLAN tag for packet
 *
 * This function sends the completed packet (via. skb) up the stack using
 * gro receive functions (with/without VLAN tag)
 */
void
ice_receive_skb(struct ice_rx_ring *rx_ring, struct sk_buff *skb, u16 vlan_tag)
{
	netdev_features_t features = rx_ring->netdev->features;
	bool non_zero_vlan = !!(vlan_tag & VLAN_VID_MASK);

	if ((features & NETIF_F_HW_VLAN_CTAG_RX) && non_zero_vlan)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan_tag);
	else if ((features & NETIF_F_HW_VLAN_STAG_RX) && non_zero_vlan)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021AD), vlan_tag);

	napi_gro_receive(&rx_ring->q_vector->napi, skb);
}

/**
 * ice_clean_xdp_tx_buf - Free and unmap XDP Tx buffer
 * @dev: device for DMA mapping
 * @tx_buf: Tx buffer to clean
 * @bq: XDP bulk flush struct
 */
static void
ice_clean_xdp_tx_buf(struct device *dev, struct ice_tx_buf *tx_buf,
		     struct xdp_frame_bulk *bq)
{
	dma_unmap_single(dev, dma_unmap_addr(tx_buf, dma),
			 dma_unmap_len(tx_buf, len), DMA_TO_DEVICE);
	dma_unmap_len_set(tx_buf, len, 0);

	switch (tx_buf->type) {
	case ICE_TX_BUF_XDP_TX:
		page_frag_free(tx_buf->raw_buf);
		break;
	case ICE_TX_BUF_XDP_XMIT:
		xdp_return_frame_bulk(tx_buf->xdpf, bq);
		break;
	}

	tx_buf->type = ICE_TX_BUF_EMPTY;
}

/**
 * ice_clean_xdp_irq - Reclaim resources after transmit completes on XDP ring
 * @xdp_ring: XDP ring to clean
 */
static u32 ice_clean_xdp_irq(struct ice_tx_ring *xdp_ring)
{
	int total_bytes = 0, total_pkts = 0;
	struct device *dev = xdp_ring->dev;
	u32 ntc = xdp_ring->next_to_clean;
	struct ice_tx_desc *tx_desc;
	u32 cnt = xdp_ring->count;
	struct xdp_frame_bulk bq;
	u32 frags, xdp_tx = 0;
	u32 ready_frames = 0;
	u32 idx;
	u32 ret;

	idx = xdp_ring->tx_buf[ntc].rs_idx;
	tx_desc = ICE_TX_DESC(xdp_ring, idx);
	if (tx_desc->cmd_type_offset_bsz &
	    cpu_to_le64(ICE_TX_DESC_DTYPE_DESC_DONE)) {
		if (idx >= ntc)
			ready_frames = idx - ntc + 1;
		else
			ready_frames = idx + cnt - ntc + 1;
	}

	if (unlikely(!ready_frames))
		return 0;
	ret = ready_frames;

	xdp_frame_bulk_init(&bq);
	rcu_read_lock(); /* xdp_return_frame_bulk() */

	while (ready_frames) {
		struct ice_tx_buf *tx_buf = &xdp_ring->tx_buf[ntc];
		struct ice_tx_buf *head = tx_buf;

		/* bytecount holds size of head + frags */
		total_bytes += tx_buf->bytecount;
		frags = tx_buf->nr_frags;
		total_pkts++;
		/* count head + frags */
		ready_frames -= frags + 1;
		xdp_tx++;

		ntc++;
		if (ntc == cnt)
			ntc = 0;

		for (int i = 0; i < frags; i++) {
			tx_buf = &xdp_ring->tx_buf[ntc];

			ice_clean_xdp_tx_buf(dev, tx_buf, &bq);
			ntc++;
			if (ntc == cnt)
				ntc = 0;
		}

		ice_clean_xdp_tx_buf(dev, head, &bq);
	}

	xdp_flush_frame_bulk(&bq);
	rcu_read_unlock();

	tx_desc->cmd_type_offset_bsz = 0;
	xdp_ring->next_to_clean = ntc;
	xdp_ring->xdp_tx_active -= xdp_tx;
	ice_update_tx_ring_stats(xdp_ring, total_pkts, total_bytes);

	return ret;
}

/**
 * __ice_xmit_xdp_ring - submit frame to XDP ring for transmission
 * @xdp: XDP buffer to be placed onto Tx descriptors
 * @xdp_ring: XDP ring for transmission
 * @frame: whether this comes from .ndo_xdp_xmit()
 */
int __ice_xmit_xdp_ring(struct xdp_buff *xdp, struct ice_tx_ring *xdp_ring,
			bool frame)
{
	struct skb_shared_info *sinfo = NULL;
	u32 size = xdp->data_end - xdp->data;
	struct device *dev = xdp_ring->dev;
	u32 ntu = xdp_ring->next_to_use;
	struct ice_tx_desc *tx_desc;
	struct ice_tx_buf *tx_head;
	struct ice_tx_buf *tx_buf;
	u32 cnt = xdp_ring->count;
	void *data = xdp->data;
	u32 nr_frags = 0;
	u32 free_space;
	u32 frag = 0;

	free_space = ICE_DESC_UNUSED(xdp_ring);
	if (free_space < ICE_RING_QUARTER(xdp_ring))
		free_space += ice_clean_xdp_irq(xdp_ring);

	if (unlikely(!free_space))
		goto busy;

	if (unlikely(xdp_buff_has_frags(xdp))) {
		sinfo = xdp_get_shared_info_from_buff(xdp);
		nr_frags = sinfo->nr_frags;
		if (free_space < nr_frags + 1)
			goto busy;
	}

	tx_desc = ICE_TX_DESC(xdp_ring, ntu);
	tx_head = &xdp_ring->tx_buf[ntu];
	tx_buf = tx_head;

	for (;;) {
		dma_addr_t dma;

		dma = dma_map_single(dev, data, size, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma))
			goto dma_unmap;

		/* record length, and DMA address */
		dma_unmap_len_set(tx_buf, len, size);
		dma_unmap_addr_set(tx_buf, dma, dma);

		if (frame) {
			tx_buf->type = ICE_TX_BUF_FRAG;
		} else {
			tx_buf->type = ICE_TX_BUF_XDP_TX;
			tx_buf->raw_buf = data;
		}

		tx_desc->buf_addr = cpu_to_le64(dma);
		tx_desc->cmd_type_offset_bsz = ice_build_ctob(0, 0, size, 0);

		ntu++;
		if (ntu == cnt)
			ntu = 0;

		if (frag == nr_frags)
			break;

		tx_desc = ICE_TX_DESC(xdp_ring, ntu);
		tx_buf = &xdp_ring->tx_buf[ntu];

		data = skb_frag_address(&sinfo->frags[frag]);
		size = skb_frag_size(&sinfo->frags[frag]);
		frag++;
	}

	/* store info about bytecount and frag count in first desc */
	tx_head->bytecount = xdp_get_buff_len(xdp);
	tx_head->nr_frags = nr_frags;

	if (frame) {
		tx_head->type = ICE_TX_BUF_XDP_XMIT;
		tx_head->xdpf = xdp->data_hard_start;
	}

	/* update last descriptor from a frame with EOP */
	tx_desc->cmd_type_offset_bsz |=
		cpu_to_le64(ICE_TX_DESC_CMD_EOP << ICE_TXD_QW1_CMD_S);

	xdp_ring->xdp_tx_active++;
	xdp_ring->next_to_use = ntu;

	return ICE_XDP_TX;

dma_unmap:
	for (;;) {
		tx_buf = &xdp_ring->tx_buf[ntu];
		dma_unmap_page(dev, dma_unmap_addr(tx_buf, dma),
			       dma_unmap_len(tx_buf, len), DMA_TO_DEVICE);
		dma_unmap_len_set(tx_buf, len, 0);
		if (tx_buf == tx_head)
			break;

		if (!ntu)
			ntu += cnt;
		ntu--;
	}
	return ICE_XDP_CONSUMED;

busy:
	xdp_ring->ring_stats->tx_stats.tx_busy++;

	return ICE_XDP_CONSUMED;
}

/**
 * ice_finalize_xdp_rx - Bump XDP Tx tail and/or flush redirect map
 * @xdp_ring: XDP ring
 * @xdp_res: Result of the receive batch
 * @first_idx: index to write from caller
 *
 * This function bumps XDP Tx tail and/or flush redirect map, and
 * should be called when a batch of packets has been processed in the
 * napi loop.
 */
void ice_finalize_xdp_rx(struct ice_tx_ring *xdp_ring, unsigned int xdp_res,
			 u32 first_idx)
{
	struct ice_tx_buf *tx_buf = &xdp_ring->tx_buf[first_idx];

	if (xdp_res & ICE_XDP_REDIR)
		xdp_do_flush();

	if (xdp_res & ICE_XDP_TX) {
		if (static_branch_unlikely(&ice_xdp_locking_key))
			spin_lock(&xdp_ring->tx_lock);
		/* store index of descriptor with RS bit set in the first
		 * ice_tx_buf of given NAPI batch
		 */
		tx_buf->rs_idx = ice_set_rs_bit(xdp_ring);
		ice_xdp_ring_update_tail(xdp_ring);
		if (static_branch_unlikely(&ice_xdp_locking_key))
			spin_unlock(&xdp_ring->tx_lock);
	}
}
