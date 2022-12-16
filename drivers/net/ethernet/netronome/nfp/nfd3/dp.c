// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2019 Netronome Systems, Inc. */

#include <linux/bpf_trace.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>
#include <net/xfrm.h>

#include "../nfp_app.h"
#include "../nfp_net.h"
#include "../nfp_net_dp.h"
#include "../nfp_net_xsk.h"
#include "../crypto/crypto.h"
#include "../crypto/fw.h"
#include "nfd3.h"

/* Transmit processing
 *
 * One queue controller peripheral queue is used for transmit.  The
 * driver en-queues packets for transmit by advancing the write
 * pointer.  The device indicates that packets have transmitted by
 * advancing the read pointer.  The driver maintains a local copy of
 * the read and write pointer in @struct nfp_net_tx_ring.  The driver
 * keeps @wr_p in sync with the queue controller write pointer and can
 * determine how many packets have been transmitted by comparing its
 * copy of the read pointer @rd_p with the read pointer maintained by
 * the queue controller peripheral.
 */

/* Wrappers for deciding when to stop and restart TX queues */
static int nfp_nfd3_tx_ring_should_wake(struct nfp_net_tx_ring *tx_ring)
{
	return !nfp_net_tx_full(tx_ring, MAX_SKB_FRAGS * 4);
}

static int nfp_nfd3_tx_ring_should_stop(struct nfp_net_tx_ring *tx_ring)
{
	return nfp_net_tx_full(tx_ring, MAX_SKB_FRAGS + 1);
}

/**
 * nfp_nfd3_tx_ring_stop() - stop tx ring
 * @nd_q:    netdev queue
 * @tx_ring: driver tx queue structure
 *
 * Safely stop TX ring.  Remember that while we are running .start_xmit()
 * someone else may be cleaning the TX ring completions so we need to be
 * extra careful here.
 */
static void
nfp_nfd3_tx_ring_stop(struct netdev_queue *nd_q,
		      struct nfp_net_tx_ring *tx_ring)
{
	netif_tx_stop_queue(nd_q);

	/* We can race with the TX completion out of NAPI so recheck */
	smp_mb();
	if (unlikely(nfp_nfd3_tx_ring_should_wake(tx_ring)))
		netif_tx_start_queue(nd_q);
}

/**
 * nfp_nfd3_tx_tso() - Set up Tx descriptor for LSO
 * @r_vec: per-ring structure
 * @txbuf: Pointer to driver soft TX descriptor
 * @txd: Pointer to HW TX descriptor
 * @skb: Pointer to SKB
 * @md_bytes: Prepend length
 *
 * Set up Tx descriptor for LSO, do nothing for non-LSO skbs.
 * Return error on packet header greater than maximum supported LSO header size.
 */
static void
nfp_nfd3_tx_tso(struct nfp_net_r_vector *r_vec, struct nfp_nfd3_tx_buf *txbuf,
		struct nfp_nfd3_tx_desc *txd, struct sk_buff *skb, u32 md_bytes)
{
	u32 l3_offset, l4_offset, hdrlen;
	u16 mss;

	if (!skb_is_gso(skb))
		return;

	if (!skb->encapsulation) {
		l3_offset = skb_network_offset(skb);
		l4_offset = skb_transport_offset(skb);
		hdrlen = skb_tcp_all_headers(skb);
	} else {
		l3_offset = skb_inner_network_offset(skb);
		l4_offset = skb_inner_transport_offset(skb);
		hdrlen = skb_inner_tcp_all_headers(skb);
	}

	txbuf->pkt_cnt = skb_shinfo(skb)->gso_segs;
	txbuf->real_len += hdrlen * (txbuf->pkt_cnt - 1);

	mss = skb_shinfo(skb)->gso_size & NFD3_DESC_TX_MSS_MASK;
	txd->l3_offset = l3_offset - md_bytes;
	txd->l4_offset = l4_offset - md_bytes;
	txd->lso_hdrlen = hdrlen - md_bytes;
	txd->mss = cpu_to_le16(mss);
	txd->flags |= NFD3_DESC_TX_LSO;

	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_lso++;
	u64_stats_update_end(&r_vec->tx_sync);
}

/**
 * nfp_nfd3_tx_csum() - Set TX CSUM offload flags in TX descriptor
 * @dp:  NFP Net data path struct
 * @r_vec: per-ring structure
 * @txbuf: Pointer to driver soft TX descriptor
 * @txd: Pointer to TX descriptor
 * @skb: Pointer to SKB
 *
 * This function sets the TX checksum flags in the TX descriptor based
 * on the configuration and the protocol of the packet to be transmitted.
 */
static void
nfp_nfd3_tx_csum(struct nfp_net_dp *dp, struct nfp_net_r_vector *r_vec,
		 struct nfp_nfd3_tx_buf *txbuf, struct nfp_nfd3_tx_desc *txd,
		 struct sk_buff *skb)
{
	struct ipv6hdr *ipv6h;
	struct iphdr *iph;
	u8 l4_hdr;

	if (!(dp->ctrl & NFP_NET_CFG_CTRL_TXCSUM))
		return;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return;

	txd->flags |= NFD3_DESC_TX_CSUM;
	if (skb->encapsulation)
		txd->flags |= NFD3_DESC_TX_ENCAP;

	iph = skb->encapsulation ? inner_ip_hdr(skb) : ip_hdr(skb);
	ipv6h = skb->encapsulation ? inner_ipv6_hdr(skb) : ipv6_hdr(skb);

	if (iph->version == 4) {
		txd->flags |= NFD3_DESC_TX_IP4_CSUM;
		l4_hdr = iph->protocol;
	} else if (ipv6h->version == 6) {
		l4_hdr = ipv6h->nexthdr;
	} else {
		nn_dp_warn(dp, "partial checksum but ipv=%x!\n", iph->version);
		return;
	}

	switch (l4_hdr) {
	case IPPROTO_TCP:
		txd->flags |= NFD3_DESC_TX_TCP_CSUM;
		break;
	case IPPROTO_UDP:
		txd->flags |= NFD3_DESC_TX_UDP_CSUM;
		break;
	default:
		nn_dp_warn(dp, "partial checksum but l4 proto=%x!\n", l4_hdr);
		return;
	}

	u64_stats_update_begin(&r_vec->tx_sync);
	if (skb->encapsulation)
		r_vec->hw_csum_tx_inner += txbuf->pkt_cnt;
	else
		r_vec->hw_csum_tx += txbuf->pkt_cnt;
	u64_stats_update_end(&r_vec->tx_sync);
}

static int nfp_nfd3_prep_tx_meta(struct nfp_net_dp *dp, struct sk_buff *skb,
				 u64 tls_handle, bool *ipsec)
{
	struct metadata_dst *md_dst = skb_metadata_dst(skb);
	struct nfp_ipsec_offload offload_info;
	unsigned char *data;
	bool vlan_insert;
	u32 meta_id = 0;
	int md_bytes;

#ifdef CONFIG_NFP_NET_IPSEC
	if (xfrm_offload(skb))
		*ipsec = nfp_net_ipsec_tx_prep(dp, skb, &offload_info);
#endif

	if (unlikely(md_dst && md_dst->type != METADATA_HW_PORT_MUX))
		md_dst = NULL;

	vlan_insert = skb_vlan_tag_present(skb) && (dp->ctrl & NFP_NET_CFG_CTRL_TXVLAN_V2);

	if (!(md_dst || tls_handle || vlan_insert || *ipsec))
		return 0;

	md_bytes = sizeof(meta_id) +
		   !!md_dst * NFP_NET_META_PORTID_SIZE +
		   !!tls_handle * NFP_NET_META_CONN_HANDLE_SIZE +
		   vlan_insert * NFP_NET_META_VLAN_SIZE +
		   *ipsec * NFP_NET_META_IPSEC_FIELD_SIZE; /* IPsec has 12 bytes of metadata */

	if (unlikely(skb_cow_head(skb, md_bytes)))
		return -ENOMEM;

	data = skb_push(skb, md_bytes) + md_bytes;
	if (md_dst) {
		data -= NFP_NET_META_PORTID_SIZE;
		put_unaligned_be32(md_dst->u.port_info.port_id, data);
		meta_id = NFP_NET_META_PORTID;
	}
	if (tls_handle) {
		/* conn handle is opaque, we just use u64 to be able to quickly
		 * compare it to zero
		 */
		data -= NFP_NET_META_CONN_HANDLE_SIZE;
		memcpy(data, &tls_handle, sizeof(tls_handle));
		meta_id <<= NFP_NET_META_FIELD_SIZE;
		meta_id |= NFP_NET_META_CONN_HANDLE;
	}
	if (vlan_insert) {
		data -= NFP_NET_META_VLAN_SIZE;
		/* data type of skb->vlan_proto is __be16
		 * so it fills metadata without calling put_unaligned_be16
		 */
		memcpy(data, &skb->vlan_proto, sizeof(skb->vlan_proto));
		put_unaligned_be16(skb_vlan_tag_get(skb), data + sizeof(skb->vlan_proto));
		meta_id <<= NFP_NET_META_FIELD_SIZE;
		meta_id |= NFP_NET_META_VLAN;
	}
	if (*ipsec) {
		/* IPsec has three consecutive 4-bit IPsec metadata types,
		 * so in total IPsec has three 4 bytes of metadata.
		 */
		data -= NFP_NET_META_IPSEC_SIZE;
		put_unaligned_be32(offload_info.seq_hi, data);
		data -= NFP_NET_META_IPSEC_SIZE;
		put_unaligned_be32(offload_info.seq_low, data);
		data -= NFP_NET_META_IPSEC_SIZE;
		put_unaligned_be32(offload_info.handle - 1, data);
		meta_id <<= NFP_NET_META_IPSEC_FIELD_SIZE;
		meta_id |= NFP_NET_META_IPSEC << 8 | NFP_NET_META_IPSEC << 4 | NFP_NET_META_IPSEC;
	}

	data -= sizeof(meta_id);
	put_unaligned_be32(meta_id, data);

	return md_bytes;
}

/**
 * nfp_nfd3_tx() - Main transmit entry point
 * @skb:    SKB to transmit
 * @netdev: netdev structure
 *
 * Return: NETDEV_TX_OK on success.
 */
netdev_tx_t nfp_nfd3_tx(struct sk_buff *skb, struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int f, nr_frags, wr_idx, md_bytes;
	struct nfp_net_tx_ring *tx_ring;
	struct nfp_net_r_vector *r_vec;
	struct nfp_nfd3_tx_buf *txbuf;
	struct nfp_nfd3_tx_desc *txd;
	struct netdev_queue *nd_q;
	const skb_frag_t *frag;
	struct nfp_net_dp *dp;
	dma_addr_t dma_addr;
	unsigned int fsize;
	u64 tls_handle = 0;
	bool ipsec = false;
	u16 qidx;

	dp = &nn->dp;
	qidx = skb_get_queue_mapping(skb);
	tx_ring = &dp->tx_rings[qidx];
	r_vec = tx_ring->r_vec;

	nr_frags = skb_shinfo(skb)->nr_frags;

	if (unlikely(nfp_net_tx_full(tx_ring, nr_frags + 1))) {
		nn_dp_warn(dp, "TX ring %d busy. wrp=%u rdp=%u\n",
			   qidx, tx_ring->wr_p, tx_ring->rd_p);
		nd_q = netdev_get_tx_queue(dp->netdev, qidx);
		netif_tx_stop_queue(nd_q);
		nfp_net_tx_xmit_more_flush(tx_ring);
		u64_stats_update_begin(&r_vec->tx_sync);
		r_vec->tx_busy++;
		u64_stats_update_end(&r_vec->tx_sync);
		return NETDEV_TX_BUSY;
	}

	skb = nfp_net_tls_tx(dp, r_vec, skb, &tls_handle, &nr_frags);
	if (unlikely(!skb)) {
		nfp_net_tx_xmit_more_flush(tx_ring);
		return NETDEV_TX_OK;
	}

	md_bytes = nfp_nfd3_prep_tx_meta(dp, skb, tls_handle, &ipsec);
	if (unlikely(md_bytes < 0))
		goto err_flush;

	/* Start with the head skbuf */
	dma_addr = dma_map_single(dp->dev, skb->data, skb_headlen(skb),
				  DMA_TO_DEVICE);
	if (dma_mapping_error(dp->dev, dma_addr))
		goto err_dma_err;

	wr_idx = D_IDX(tx_ring, tx_ring->wr_p);

	/* Stash the soft descriptor of the head then initialize it */
	txbuf = &tx_ring->txbufs[wr_idx];
	txbuf->skb = skb;
	txbuf->dma_addr = dma_addr;
	txbuf->fidx = -1;
	txbuf->pkt_cnt = 1;
	txbuf->real_len = skb->len;

	/* Build TX descriptor */
	txd = &tx_ring->txds[wr_idx];
	txd->offset_eop = (nr_frags ? 0 : NFD3_DESC_TX_EOP) | md_bytes;
	txd->dma_len = cpu_to_le16(skb_headlen(skb));
	nfp_desc_set_dma_addr_40b(txd, dma_addr);
	txd->data_len = cpu_to_le16(skb->len);

	txd->flags = 0;
	txd->mss = 0;
	txd->lso_hdrlen = 0;

	/* Do not reorder - tso may adjust pkt cnt, vlan may override fields */
	nfp_nfd3_tx_tso(r_vec, txbuf, txd, skb, md_bytes);
	nfp_nfd3_tx_csum(dp, r_vec, txbuf, txd, skb);
	if (skb_vlan_tag_present(skb) && dp->ctrl & NFP_NET_CFG_CTRL_TXVLAN) {
		txd->flags |= NFD3_DESC_TX_VLAN;
		txd->vlan = cpu_to_le16(skb_vlan_tag_get(skb));
	}

	if (ipsec)
		nfp_nfd3_ipsec_tx(txd, skb);
	/* Gather DMA */
	if (nr_frags > 0) {
		__le64 second_half;

		/* all descs must match except for in addr, length and eop */
		second_half = txd->vals8[1];

		for (f = 0; f < nr_frags; f++) {
			frag = &skb_shinfo(skb)->frags[f];
			fsize = skb_frag_size(frag);

			dma_addr = skb_frag_dma_map(dp->dev, frag, 0,
						    fsize, DMA_TO_DEVICE);
			if (dma_mapping_error(dp->dev, dma_addr))
				goto err_unmap;

			wr_idx = D_IDX(tx_ring, wr_idx + 1);
			tx_ring->txbufs[wr_idx].skb = skb;
			tx_ring->txbufs[wr_idx].dma_addr = dma_addr;
			tx_ring->txbufs[wr_idx].fidx = f;

			txd = &tx_ring->txds[wr_idx];
			txd->dma_len = cpu_to_le16(fsize);
			nfp_desc_set_dma_addr_40b(txd, dma_addr);
			txd->offset_eop = md_bytes |
				((f == nr_frags - 1) ? NFD3_DESC_TX_EOP : 0);
			txd->vals8[1] = second_half;
		}

		u64_stats_update_begin(&r_vec->tx_sync);
		r_vec->tx_gather++;
		u64_stats_update_end(&r_vec->tx_sync);
	}

	skb_tx_timestamp(skb);

	nd_q = netdev_get_tx_queue(dp->netdev, tx_ring->idx);

	tx_ring->wr_p += nr_frags + 1;
	if (nfp_nfd3_tx_ring_should_stop(tx_ring))
		nfp_nfd3_tx_ring_stop(nd_q, tx_ring);

	tx_ring->wr_ptr_add += nr_frags + 1;
	if (__netdev_tx_sent_queue(nd_q, txbuf->real_len, netdev_xmit_more()))
		nfp_net_tx_xmit_more_flush(tx_ring);

	return NETDEV_TX_OK;

err_unmap:
	while (--f >= 0) {
		frag = &skb_shinfo(skb)->frags[f];
		dma_unmap_page(dp->dev, tx_ring->txbufs[wr_idx].dma_addr,
			       skb_frag_size(frag), DMA_TO_DEVICE);
		tx_ring->txbufs[wr_idx].skb = NULL;
		tx_ring->txbufs[wr_idx].dma_addr = 0;
		tx_ring->txbufs[wr_idx].fidx = -2;
		wr_idx = wr_idx - 1;
		if (wr_idx < 0)
			wr_idx += tx_ring->cnt;
	}
	dma_unmap_single(dp->dev, tx_ring->txbufs[wr_idx].dma_addr,
			 skb_headlen(skb), DMA_TO_DEVICE);
	tx_ring->txbufs[wr_idx].skb = NULL;
	tx_ring->txbufs[wr_idx].dma_addr = 0;
	tx_ring->txbufs[wr_idx].fidx = -2;
err_dma_err:
	nn_dp_warn(dp, "Failed to map DMA TX buffer\n");
err_flush:
	nfp_net_tx_xmit_more_flush(tx_ring);
	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_errors++;
	u64_stats_update_end(&r_vec->tx_sync);
	nfp_net_tls_tx_undo(skb, tls_handle);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/**
 * nfp_nfd3_tx_complete() - Handled completed TX packets
 * @tx_ring:	TX ring structure
 * @budget:	NAPI budget (only used as bool to determine if in NAPI context)
 */
void nfp_nfd3_tx_complete(struct nfp_net_tx_ring *tx_ring, int budget)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;
	u32 done_pkts = 0, done_bytes = 0;
	struct netdev_queue *nd_q;
	u32 qcp_rd_p;
	int todo;

	if (tx_ring->wr_p == tx_ring->rd_p)
		return;

	/* Work out how many descriptors have been transmitted */
	qcp_rd_p = nfp_net_read_tx_cmpl(tx_ring, dp);

	if (qcp_rd_p == tx_ring->qcp_rd_p)
		return;

	todo = D_IDX(tx_ring, qcp_rd_p - tx_ring->qcp_rd_p);

	while (todo--) {
		const skb_frag_t *frag;
		struct nfp_nfd3_tx_buf *tx_buf;
		struct sk_buff *skb;
		int fidx, nr_frags;
		int idx;

		idx = D_IDX(tx_ring, tx_ring->rd_p++);
		tx_buf = &tx_ring->txbufs[idx];

		skb = tx_buf->skb;
		if (!skb)
			continue;

		nr_frags = skb_shinfo(skb)->nr_frags;
		fidx = tx_buf->fidx;

		if (fidx == -1) {
			/* unmap head */
			dma_unmap_single(dp->dev, tx_buf->dma_addr,
					 skb_headlen(skb), DMA_TO_DEVICE);

			done_pkts += tx_buf->pkt_cnt;
			done_bytes += tx_buf->real_len;
		} else {
			/* unmap fragment */
			frag = &skb_shinfo(skb)->frags[fidx];
			dma_unmap_page(dp->dev, tx_buf->dma_addr,
				       skb_frag_size(frag), DMA_TO_DEVICE);
		}

		/* check for last gather fragment */
		if (fidx == nr_frags - 1)
			napi_consume_skb(skb, budget);

		tx_buf->dma_addr = 0;
		tx_buf->skb = NULL;
		tx_buf->fidx = -2;
	}

	tx_ring->qcp_rd_p = qcp_rd_p;

	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_bytes += done_bytes;
	r_vec->tx_pkts += done_pkts;
	u64_stats_update_end(&r_vec->tx_sync);

	if (!dp->netdev)
		return;

	nd_q = netdev_get_tx_queue(dp->netdev, tx_ring->idx);
	netdev_tx_completed_queue(nd_q, done_pkts, done_bytes);
	if (nfp_nfd3_tx_ring_should_wake(tx_ring)) {
		/* Make sure TX thread will see updated tx_ring->rd_p */
		smp_mb();

		if (unlikely(netif_tx_queue_stopped(nd_q)))
			netif_tx_wake_queue(nd_q);
	}

	WARN_ONCE(tx_ring->wr_p - tx_ring->rd_p > tx_ring->cnt,
		  "TX ring corruption rd_p=%u wr_p=%u cnt=%u\n",
		  tx_ring->rd_p, tx_ring->wr_p, tx_ring->cnt);
}

static bool nfp_nfd3_xdp_complete(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;
	u32 done_pkts = 0, done_bytes = 0;
	bool done_all;
	int idx, todo;
	u32 qcp_rd_p;

	/* Work out how many descriptors have been transmitted */
	qcp_rd_p = nfp_net_read_tx_cmpl(tx_ring, dp);

	if (qcp_rd_p == tx_ring->qcp_rd_p)
		return true;

	todo = D_IDX(tx_ring, qcp_rd_p - tx_ring->qcp_rd_p);

	done_all = todo <= NFP_NET_XDP_MAX_COMPLETE;
	todo = min(todo, NFP_NET_XDP_MAX_COMPLETE);

	tx_ring->qcp_rd_p = D_IDX(tx_ring, tx_ring->qcp_rd_p + todo);

	done_pkts = todo;
	while (todo--) {
		idx = D_IDX(tx_ring, tx_ring->rd_p);
		tx_ring->rd_p++;

		done_bytes += tx_ring->txbufs[idx].real_len;
	}

	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_bytes += done_bytes;
	r_vec->tx_pkts += done_pkts;
	u64_stats_update_end(&r_vec->tx_sync);

	WARN_ONCE(tx_ring->wr_p - tx_ring->rd_p > tx_ring->cnt,
		  "XDP TX ring corruption rd_p=%u wr_p=%u cnt=%u\n",
		  tx_ring->rd_p, tx_ring->wr_p, tx_ring->cnt);

	return done_all;
}

/* Receive processing
 */

static void *
nfp_nfd3_napi_alloc_one(struct nfp_net_dp *dp, dma_addr_t *dma_addr)
{
	void *frag;

	if (!dp->xdp_prog) {
		frag = napi_alloc_frag(dp->fl_bufsz);
		if (unlikely(!frag))
			return NULL;
	} else {
		struct page *page;

		page = dev_alloc_page();
		if (unlikely(!page))
			return NULL;
		frag = page_address(page);
	}

	*dma_addr = nfp_net_dma_map_rx(dp, frag);
	if (dma_mapping_error(dp->dev, *dma_addr)) {
		nfp_net_free_frag(frag, dp->xdp_prog);
		nn_dp_warn(dp, "Failed to map DMA RX buffer\n");
		return NULL;
	}

	return frag;
}

/**
 * nfp_nfd3_rx_give_one() - Put mapped skb on the software and hardware rings
 * @dp:		NFP Net data path struct
 * @rx_ring:	RX ring structure
 * @frag:	page fragment buffer
 * @dma_addr:	DMA address of skb mapping
 */
static void
nfp_nfd3_rx_give_one(const struct nfp_net_dp *dp,
		     struct nfp_net_rx_ring *rx_ring,
		     void *frag, dma_addr_t dma_addr)
{
	unsigned int wr_idx;

	wr_idx = D_IDX(rx_ring, rx_ring->wr_p);

	nfp_net_dma_sync_dev_rx(dp, dma_addr);

	/* Stash SKB and DMA address away */
	rx_ring->rxbufs[wr_idx].frag = frag;
	rx_ring->rxbufs[wr_idx].dma_addr = dma_addr;

	/* Fill freelist descriptor */
	rx_ring->rxds[wr_idx].fld.reserved = 0;
	rx_ring->rxds[wr_idx].fld.meta_len_dd = 0;
	/* DMA address is expanded to 48-bit width in freelist for NFP3800,
	 * so the *_48b macro is used accordingly, it's also OK to fill
	 * a 40-bit address since the top 8 bits are get set to 0.
	 */
	nfp_desc_set_dma_addr_48b(&rx_ring->rxds[wr_idx].fld,
				  dma_addr + dp->rx_dma_off);

	rx_ring->wr_p++;
	if (!(rx_ring->wr_p % NFP_NET_FL_BATCH)) {
		/* Update write pointer of the freelist queue. Make
		 * sure all writes are flushed before telling the hardware.
		 */
		wmb();
		nfp_qcp_wr_ptr_add(rx_ring->qcp_fl, NFP_NET_FL_BATCH);
	}
}

/**
 * nfp_nfd3_rx_ring_fill_freelist() - Give buffers from the ring to FW
 * @dp:	     NFP Net data path struct
 * @rx_ring: RX ring to fill
 */
void nfp_nfd3_rx_ring_fill_freelist(struct nfp_net_dp *dp,
				    struct nfp_net_rx_ring *rx_ring)
{
	unsigned int i;

	if (nfp_net_has_xsk_pool_slow(dp, rx_ring->idx))
		return nfp_net_xsk_rx_ring_fill_freelist(rx_ring);

	for (i = 0; i < rx_ring->cnt - 1; i++)
		nfp_nfd3_rx_give_one(dp, rx_ring, rx_ring->rxbufs[i].frag,
				     rx_ring->rxbufs[i].dma_addr);
}

/**
 * nfp_nfd3_rx_csum_has_errors() - group check if rxd has any csum errors
 * @flags: RX descriptor flags field in CPU byte order
 */
static int nfp_nfd3_rx_csum_has_errors(u16 flags)
{
	u16 csum_all_checked, csum_all_ok;

	csum_all_checked = flags & __PCIE_DESC_RX_CSUM_ALL;
	csum_all_ok = flags & __PCIE_DESC_RX_CSUM_ALL_OK;

	return csum_all_checked != (csum_all_ok << PCIE_DESC_RX_CSUM_OK_SHIFT);
}

/**
 * nfp_nfd3_rx_csum() - set SKB checksum field based on RX descriptor flags
 * @dp:  NFP Net data path struct
 * @r_vec: per-ring structure
 * @rxd: Pointer to RX descriptor
 * @meta: Parsed metadata prepend
 * @skb: Pointer to SKB
 */
void
nfp_nfd3_rx_csum(const struct nfp_net_dp *dp, struct nfp_net_r_vector *r_vec,
		 const struct nfp_net_rx_desc *rxd,
		 const struct nfp_meta_parsed *meta, struct sk_buff *skb)
{
	skb_checksum_none_assert(skb);

	if (!(dp->netdev->features & NETIF_F_RXCSUM))
		return;

	if (meta->csum_type) {
		skb->ip_summed = meta->csum_type;
		skb->csum = meta->csum;
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->hw_csum_rx_complete++;
		u64_stats_update_end(&r_vec->rx_sync);
		return;
	}

	if (nfp_nfd3_rx_csum_has_errors(le16_to_cpu(rxd->rxd.flags))) {
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->hw_csum_rx_error++;
		u64_stats_update_end(&r_vec->rx_sync);
		return;
	}

	/* Assume that the firmware will never report inner CSUM_OK unless outer
	 * L4 headers were successfully parsed. FW will always report zero UDP
	 * checksum as CSUM_OK.
	 */
	if (rxd->rxd.flags & PCIE_DESC_RX_TCP_CSUM_OK ||
	    rxd->rxd.flags & PCIE_DESC_RX_UDP_CSUM_OK) {
		__skb_incr_checksum_unnecessary(skb);
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->hw_csum_rx_ok++;
		u64_stats_update_end(&r_vec->rx_sync);
	}

	if (rxd->rxd.flags & PCIE_DESC_RX_I_TCP_CSUM_OK ||
	    rxd->rxd.flags & PCIE_DESC_RX_I_UDP_CSUM_OK) {
		__skb_incr_checksum_unnecessary(skb);
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->hw_csum_rx_inner_ok++;
		u64_stats_update_end(&r_vec->rx_sync);
	}
}

static void
nfp_nfd3_set_hash(struct net_device *netdev, struct nfp_meta_parsed *meta,
		  unsigned int type, __be32 *hash)
{
	if (!(netdev->features & NETIF_F_RXHASH))
		return;

	switch (type) {
	case NFP_NET_RSS_IPV4:
	case NFP_NET_RSS_IPV6:
	case NFP_NET_RSS_IPV6_EX:
		meta->hash_type = PKT_HASH_TYPE_L3;
		break;
	default:
		meta->hash_type = PKT_HASH_TYPE_L4;
		break;
	}

	meta->hash = get_unaligned_be32(hash);
}

static void
nfp_nfd3_set_hash_desc(struct net_device *netdev, struct nfp_meta_parsed *meta,
		       void *data, struct nfp_net_rx_desc *rxd)
{
	struct nfp_net_rx_hash *rx_hash = data;

	if (!(rxd->rxd.flags & PCIE_DESC_RX_RSS))
		return;

	nfp_nfd3_set_hash(netdev, meta, get_unaligned_be32(&rx_hash->hash_type),
			  &rx_hash->hash);
}

bool
nfp_nfd3_parse_meta(struct net_device *netdev, struct nfp_meta_parsed *meta,
		    void *data, void *pkt, unsigned int pkt_len, int meta_len)
{
	u32 meta_info, vlan_info;

	meta_info = get_unaligned_be32(data);
	data += 4;

	while (meta_info) {
		switch (meta_info & NFP_NET_META_FIELD_MASK) {
		case NFP_NET_META_HASH:
			meta_info >>= NFP_NET_META_FIELD_SIZE;
			nfp_nfd3_set_hash(netdev, meta,
					  meta_info & NFP_NET_META_FIELD_MASK,
					  (__be32 *)data);
			data += 4;
			break;
		case NFP_NET_META_MARK:
			meta->mark = get_unaligned_be32(data);
			data += 4;
			break;
		case NFP_NET_META_VLAN:
			vlan_info = get_unaligned_be32(data);
			if (FIELD_GET(NFP_NET_META_VLAN_STRIP, vlan_info)) {
				meta->vlan.stripped = true;
				meta->vlan.tpid = FIELD_GET(NFP_NET_META_VLAN_TPID_MASK,
							    vlan_info);
				meta->vlan.tci = FIELD_GET(NFP_NET_META_VLAN_TCI_MASK,
							   vlan_info);
			}
			data += 4;
			break;
		case NFP_NET_META_PORTID:
			meta->portid = get_unaligned_be32(data);
			data += 4;
			break;
		case NFP_NET_META_CSUM:
			meta->csum_type = CHECKSUM_COMPLETE;
			meta->csum =
				(__force __wsum)__get_unaligned_cpu32(data);
			data += 4;
			break;
		case NFP_NET_META_RESYNC_INFO:
			if (nfp_net_tls_rx_resync_req(netdev, data, pkt,
						      pkt_len))
				return false;
			data += sizeof(struct nfp_net_tls_resync_req);
			break;
#ifdef CONFIG_NFP_NET_IPSEC
		case NFP_NET_META_IPSEC:
			/* Note: IPsec packet will have zero saidx, so need add 1
			 * to indicate packet is IPsec packet within driver.
			 */
			meta->ipsec_saidx = get_unaligned_be32(data) + 1;
			data += 4;
			break;
#endif
		default:
			return true;
		}

		meta_info >>= NFP_NET_META_FIELD_SIZE;
	}

	return data != pkt;
}

static void
nfp_nfd3_rx_drop(const struct nfp_net_dp *dp, struct nfp_net_r_vector *r_vec,
		 struct nfp_net_rx_ring *rx_ring, struct nfp_net_rx_buf *rxbuf,
		 struct sk_buff *skb)
{
	u64_stats_update_begin(&r_vec->rx_sync);
	r_vec->rx_drops++;
	/* If we have both skb and rxbuf the replacement buffer allocation
	 * must have failed, count this as an alloc failure.
	 */
	if (skb && rxbuf)
		r_vec->rx_replace_buf_alloc_fail++;
	u64_stats_update_end(&r_vec->rx_sync);

	/* skb is build based on the frag, free_skb() would free the frag
	 * so to be able to reuse it we need an extra ref.
	 */
	if (skb && rxbuf && skb->head == rxbuf->frag)
		page_ref_inc(virt_to_head_page(rxbuf->frag));
	if (rxbuf)
		nfp_nfd3_rx_give_one(dp, rx_ring, rxbuf->frag, rxbuf->dma_addr);
	if (skb)
		dev_kfree_skb_any(skb);
}

static bool
nfp_nfd3_tx_xdp_buf(struct nfp_net_dp *dp, struct nfp_net_rx_ring *rx_ring,
		    struct nfp_net_tx_ring *tx_ring,
		    struct nfp_net_rx_buf *rxbuf, unsigned int dma_off,
		    unsigned int pkt_len, bool *completed)
{
	unsigned int dma_map_sz = dp->fl_bufsz - NFP_NET_RX_BUF_NON_DATA;
	struct nfp_nfd3_tx_buf *txbuf;
	struct nfp_nfd3_tx_desc *txd;
	int wr_idx;

	/* Reject if xdp_adjust_tail grow packet beyond DMA area */
	if (pkt_len + dma_off > dma_map_sz)
		return false;

	if (unlikely(nfp_net_tx_full(tx_ring, 1))) {
		if (!*completed) {
			nfp_nfd3_xdp_complete(tx_ring);
			*completed = true;
		}

		if (unlikely(nfp_net_tx_full(tx_ring, 1))) {
			nfp_nfd3_rx_drop(dp, rx_ring->r_vec, rx_ring, rxbuf,
					 NULL);
			return false;
		}
	}

	wr_idx = D_IDX(tx_ring, tx_ring->wr_p);

	/* Stash the soft descriptor of the head then initialize it */
	txbuf = &tx_ring->txbufs[wr_idx];

	nfp_nfd3_rx_give_one(dp, rx_ring, txbuf->frag, txbuf->dma_addr);

	txbuf->frag = rxbuf->frag;
	txbuf->dma_addr = rxbuf->dma_addr;
	txbuf->fidx = -1;
	txbuf->pkt_cnt = 1;
	txbuf->real_len = pkt_len;

	dma_sync_single_for_device(dp->dev, rxbuf->dma_addr + dma_off,
				   pkt_len, DMA_BIDIRECTIONAL);

	/* Build TX descriptor */
	txd = &tx_ring->txds[wr_idx];
	txd->offset_eop = NFD3_DESC_TX_EOP;
	txd->dma_len = cpu_to_le16(pkt_len);
	nfp_desc_set_dma_addr_40b(txd, rxbuf->dma_addr + dma_off);
	txd->data_len = cpu_to_le16(pkt_len);

	txd->flags = 0;
	txd->mss = 0;
	txd->lso_hdrlen = 0;

	tx_ring->wr_p++;
	tx_ring->wr_ptr_add++;
	return true;
}

/**
 * nfp_nfd3_rx() - receive up to @budget packets on @rx_ring
 * @rx_ring:   RX ring to receive from
 * @budget:    NAPI budget
 *
 * Note, this function is separated out from the napi poll function to
 * more cleanly separate packet receive code from other bookkeeping
 * functions performed in the napi poll function.
 *
 * Return: Number of packets received.
 */
static int nfp_nfd3_rx(struct nfp_net_rx_ring *rx_ring, int budget)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;
	struct nfp_net_tx_ring *tx_ring;
	struct bpf_prog *xdp_prog;
	int idx, pkts_polled = 0;
	bool xdp_tx_cmpl = false;
	unsigned int true_bufsz;
	struct sk_buff *skb;
	struct xdp_buff xdp;

	xdp_prog = READ_ONCE(dp->xdp_prog);
	true_bufsz = xdp_prog ? PAGE_SIZE : dp->fl_bufsz;
	xdp_init_buff(&xdp, PAGE_SIZE - NFP_NET_RX_BUF_HEADROOM,
		      &rx_ring->xdp_rxq);
	tx_ring = r_vec->xdp_ring;

	while (pkts_polled < budget) {
		unsigned int meta_len, data_len, meta_off, pkt_len, pkt_off;
		struct nfp_net_rx_buf *rxbuf;
		struct nfp_net_rx_desc *rxd;
		struct nfp_meta_parsed meta;
		bool redir_egress = false;
		struct net_device *netdev;
		dma_addr_t new_dma_addr;
		u32 meta_len_xdp = 0;
		void *new_frag;

		idx = D_IDX(rx_ring, rx_ring->rd_p);

		rxd = &rx_ring->rxds[idx];
		if (!(rxd->rxd.meta_len_dd & PCIE_DESC_RX_DD))
			break;

		/* Memory barrier to ensure that we won't do other reads
		 * before the DD bit.
		 */
		dma_rmb();

		memset(&meta, 0, sizeof(meta));

		rx_ring->rd_p++;
		pkts_polled++;

		rxbuf =	&rx_ring->rxbufs[idx];
		/*         < meta_len >
		 *  <-- [rx_offset] -->
		 *  ---------------------------------------------------------
		 * | [XX] |  metadata  |             packet           | XXXX |
		 *  ---------------------------------------------------------
		 *         <---------------- data_len --------------->
		 *
		 * The rx_offset is fixed for all packets, the meta_len can vary
		 * on a packet by packet basis. If rx_offset is set to zero
		 * (_RX_OFFSET_DYNAMIC) metadata starts at the beginning of the
		 * buffer and is immediately followed by the packet (no [XX]).
		 */
		meta_len = rxd->rxd.meta_len_dd & PCIE_DESC_RX_META_LEN_MASK;
		data_len = le16_to_cpu(rxd->rxd.data_len);
		pkt_len = data_len - meta_len;

		pkt_off = NFP_NET_RX_BUF_HEADROOM + dp->rx_dma_off;
		if (dp->rx_offset == NFP_NET_CFG_RX_OFFSET_DYNAMIC)
			pkt_off += meta_len;
		else
			pkt_off += dp->rx_offset;
		meta_off = pkt_off - meta_len;

		/* Stats update */
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->rx_pkts++;
		r_vec->rx_bytes += pkt_len;
		u64_stats_update_end(&r_vec->rx_sync);

		if (unlikely(meta_len > NFP_NET_MAX_PREPEND ||
			     (dp->rx_offset && meta_len > dp->rx_offset))) {
			nn_dp_warn(dp, "oversized RX packet metadata %u\n",
				   meta_len);
			nfp_nfd3_rx_drop(dp, r_vec, rx_ring, rxbuf, NULL);
			continue;
		}

		nfp_net_dma_sync_cpu_rx(dp, rxbuf->dma_addr + meta_off,
					data_len);

		if (!dp->chained_metadata_format) {
			nfp_nfd3_set_hash_desc(dp->netdev, &meta,
					       rxbuf->frag + meta_off, rxd);
		} else if (meta_len) {
			if (unlikely(nfp_nfd3_parse_meta(dp->netdev, &meta,
							 rxbuf->frag + meta_off,
							 rxbuf->frag + pkt_off,
							 pkt_len, meta_len))) {
				nn_dp_warn(dp, "invalid RX packet metadata\n");
				nfp_nfd3_rx_drop(dp, r_vec, rx_ring, rxbuf,
						 NULL);
				continue;
			}
		}

		if (xdp_prog && !meta.portid) {
			void *orig_data = rxbuf->frag + pkt_off;
			unsigned int dma_off;
			int act;

			xdp_prepare_buff(&xdp,
					 rxbuf->frag + NFP_NET_RX_BUF_HEADROOM,
					 pkt_off - NFP_NET_RX_BUF_HEADROOM,
					 pkt_len, true);

			act = bpf_prog_run_xdp(xdp_prog, &xdp);

			pkt_len = xdp.data_end - xdp.data;
			pkt_off += xdp.data - orig_data;

			switch (act) {
			case XDP_PASS:
				meta_len_xdp = xdp.data - xdp.data_meta;
				break;
			case XDP_TX:
				dma_off = pkt_off - NFP_NET_RX_BUF_HEADROOM;
				if (unlikely(!nfp_nfd3_tx_xdp_buf(dp, rx_ring,
								  tx_ring,
								  rxbuf,
								  dma_off,
								  pkt_len,
								  &xdp_tx_cmpl)))
					trace_xdp_exception(dp->netdev,
							    xdp_prog, act);
				continue;
			default:
				bpf_warn_invalid_xdp_action(dp->netdev, xdp_prog, act);
				fallthrough;
			case XDP_ABORTED:
				trace_xdp_exception(dp->netdev, xdp_prog, act);
				fallthrough;
			case XDP_DROP:
				nfp_nfd3_rx_give_one(dp, rx_ring, rxbuf->frag,
						     rxbuf->dma_addr);
				continue;
			}
		}

		if (likely(!meta.portid)) {
			netdev = dp->netdev;
		} else if (meta.portid == NFP_META_PORT_ID_CTRL) {
			struct nfp_net *nn = netdev_priv(dp->netdev);

			nfp_app_ctrl_rx_raw(nn->app, rxbuf->frag + pkt_off,
					    pkt_len);
			nfp_nfd3_rx_give_one(dp, rx_ring, rxbuf->frag,
					     rxbuf->dma_addr);
			continue;
		} else {
			struct nfp_net *nn;

			nn = netdev_priv(dp->netdev);
			netdev = nfp_app_dev_get(nn->app, meta.portid,
						 &redir_egress);
			if (unlikely(!netdev)) {
				nfp_nfd3_rx_drop(dp, r_vec, rx_ring, rxbuf,
						 NULL);
				continue;
			}

			if (nfp_netdev_is_nfp_repr(netdev))
				nfp_repr_inc_rx_stats(netdev, pkt_len);
		}

		skb = build_skb(rxbuf->frag, true_bufsz);
		if (unlikely(!skb)) {
			nfp_nfd3_rx_drop(dp, r_vec, rx_ring, rxbuf, NULL);
			continue;
		}
		new_frag = nfp_nfd3_napi_alloc_one(dp, &new_dma_addr);
		if (unlikely(!new_frag)) {
			nfp_nfd3_rx_drop(dp, r_vec, rx_ring, rxbuf, skb);
			continue;
		}

		nfp_net_dma_unmap_rx(dp, rxbuf->dma_addr);

		nfp_nfd3_rx_give_one(dp, rx_ring, new_frag, new_dma_addr);

		skb_reserve(skb, pkt_off);
		skb_put(skb, pkt_len);

		skb->mark = meta.mark;
		skb_set_hash(skb, meta.hash, meta.hash_type);

		skb_record_rx_queue(skb, rx_ring->idx);
		skb->protocol = eth_type_trans(skb, netdev);

		nfp_nfd3_rx_csum(dp, r_vec, rxd, &meta, skb);

#ifdef CONFIG_TLS_DEVICE
		if (rxd->rxd.flags & PCIE_DESC_RX_DECRYPTED) {
			skb->decrypted = true;
			u64_stats_update_begin(&r_vec->rx_sync);
			r_vec->hw_tls_rx++;
			u64_stats_update_end(&r_vec->rx_sync);
		}
#endif

		if (unlikely(!nfp_net_vlan_strip(skb, rxd, &meta))) {
			nfp_nfd3_rx_drop(dp, r_vec, rx_ring, NULL, skb);
			continue;
		}

#ifdef CONFIG_NFP_NET_IPSEC
		if (meta.ipsec_saidx != 0 && unlikely(nfp_net_ipsec_rx(&meta, skb))) {
			nfp_nfd3_rx_drop(dp, r_vec, rx_ring, NULL, skb);
			continue;
		}
#endif

		if (meta_len_xdp)
			skb_metadata_set(skb, meta_len_xdp);

		if (likely(!redir_egress)) {
			napi_gro_receive(&rx_ring->r_vec->napi, skb);
		} else {
			skb->dev = netdev;
			skb_reset_network_header(skb);
			__skb_push(skb, ETH_HLEN);
			dev_queue_xmit(skb);
		}
	}

	if (xdp_prog) {
		if (tx_ring->wr_ptr_add)
			nfp_net_tx_xmit_more_flush(tx_ring);
		else if (unlikely(tx_ring->wr_p != tx_ring->rd_p) &&
			 !xdp_tx_cmpl)
			if (!nfp_nfd3_xdp_complete(tx_ring))
				pkts_polled = budget;
	}

	return pkts_polled;
}

/**
 * nfp_nfd3_poll() - napi poll function
 * @napi:    NAPI structure
 * @budget:  NAPI budget
 *
 * Return: number of packets polled.
 */
int nfp_nfd3_poll(struct napi_struct *napi, int budget)
{
	struct nfp_net_r_vector *r_vec =
		container_of(napi, struct nfp_net_r_vector, napi);
	unsigned int pkts_polled = 0;

	if (r_vec->tx_ring)
		nfp_nfd3_tx_complete(r_vec->tx_ring, budget);
	if (r_vec->rx_ring)
		pkts_polled = nfp_nfd3_rx(r_vec->rx_ring, budget);

	if (pkts_polled < budget)
		if (napi_complete_done(napi, pkts_polled))
			nfp_net_irq_unmask(r_vec->nfp_net, r_vec->irq_entry);

	if (r_vec->nfp_net->rx_coalesce_adapt_on && r_vec->rx_ring) {
		struct dim_sample dim_sample = {};
		unsigned int start;
		u64 pkts, bytes;

		do {
			start = u64_stats_fetch_begin(&r_vec->rx_sync);
			pkts = r_vec->rx_pkts;
			bytes = r_vec->rx_bytes;
		} while (u64_stats_fetch_retry(&r_vec->rx_sync, start));

		dim_update_sample(r_vec->event_ctr, pkts, bytes, &dim_sample);
		net_dim(&r_vec->rx_dim, dim_sample);
	}

	if (r_vec->nfp_net->tx_coalesce_adapt_on && r_vec->tx_ring) {
		struct dim_sample dim_sample = {};
		unsigned int start;
		u64 pkts, bytes;

		do {
			start = u64_stats_fetch_begin(&r_vec->tx_sync);
			pkts = r_vec->tx_pkts;
			bytes = r_vec->tx_bytes;
		} while (u64_stats_fetch_retry(&r_vec->tx_sync, start));

		dim_update_sample(r_vec->event_ctr, pkts, bytes, &dim_sample);
		net_dim(&r_vec->tx_dim, dim_sample);
	}

	return pkts_polled;
}

/* Control device data path
 */

bool
nfp_nfd3_ctrl_tx_one(struct nfp_net *nn, struct nfp_net_r_vector *r_vec,
		     struct sk_buff *skb, bool old)
{
	unsigned int real_len = skb->len, meta_len = 0;
	struct nfp_net_tx_ring *tx_ring;
	struct nfp_nfd3_tx_buf *txbuf;
	struct nfp_nfd3_tx_desc *txd;
	struct nfp_net_dp *dp;
	dma_addr_t dma_addr;
	int wr_idx;

	dp = &r_vec->nfp_net->dp;
	tx_ring = r_vec->tx_ring;

	if (WARN_ON_ONCE(skb_shinfo(skb)->nr_frags)) {
		nn_dp_warn(dp, "Driver's CTRL TX does not implement gather\n");
		goto err_free;
	}

	if (unlikely(nfp_net_tx_full(tx_ring, 1))) {
		u64_stats_update_begin(&r_vec->tx_sync);
		r_vec->tx_busy++;
		u64_stats_update_end(&r_vec->tx_sync);
		if (!old)
			__skb_queue_tail(&r_vec->queue, skb);
		else
			__skb_queue_head(&r_vec->queue, skb);
		return true;
	}

	if (nfp_app_ctrl_has_meta(nn->app)) {
		if (unlikely(skb_headroom(skb) < 8)) {
			nn_dp_warn(dp, "CTRL TX on skb without headroom\n");
			goto err_free;
		}
		meta_len = 8;
		put_unaligned_be32(NFP_META_PORT_ID_CTRL, skb_push(skb, 4));
		put_unaligned_be32(NFP_NET_META_PORTID, skb_push(skb, 4));
	}

	/* Start with the head skbuf */
	dma_addr = dma_map_single(dp->dev, skb->data, skb_headlen(skb),
				  DMA_TO_DEVICE);
	if (dma_mapping_error(dp->dev, dma_addr))
		goto err_dma_warn;

	wr_idx = D_IDX(tx_ring, tx_ring->wr_p);

	/* Stash the soft descriptor of the head then initialize it */
	txbuf = &tx_ring->txbufs[wr_idx];
	txbuf->skb = skb;
	txbuf->dma_addr = dma_addr;
	txbuf->fidx = -1;
	txbuf->pkt_cnt = 1;
	txbuf->real_len = real_len;

	/* Build TX descriptor */
	txd = &tx_ring->txds[wr_idx];
	txd->offset_eop = meta_len | NFD3_DESC_TX_EOP;
	txd->dma_len = cpu_to_le16(skb_headlen(skb));
	nfp_desc_set_dma_addr_40b(txd, dma_addr);
	txd->data_len = cpu_to_le16(skb->len);

	txd->flags = 0;
	txd->mss = 0;
	txd->lso_hdrlen = 0;

	tx_ring->wr_p++;
	tx_ring->wr_ptr_add++;
	nfp_net_tx_xmit_more_flush(tx_ring);

	return false;

err_dma_warn:
	nn_dp_warn(dp, "Failed to DMA map TX CTRL buffer\n");
err_free:
	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_errors++;
	u64_stats_update_end(&r_vec->tx_sync);
	dev_kfree_skb_any(skb);
	return false;
}

static void __nfp_ctrl_tx_queued(struct nfp_net_r_vector *r_vec)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&r_vec->queue)))
		if (nfp_nfd3_ctrl_tx_one(r_vec->nfp_net, r_vec, skb, true))
			return;
}

static bool
nfp_ctrl_meta_ok(struct nfp_net *nn, void *data, unsigned int meta_len)
{
	u32 meta_type, meta_tag;

	if (!nfp_app_ctrl_has_meta(nn->app))
		return !meta_len;

	if (meta_len != 8)
		return false;

	meta_type = get_unaligned_be32(data);
	meta_tag = get_unaligned_be32(data + 4);

	return (meta_type == NFP_NET_META_PORTID &&
		meta_tag == NFP_META_PORT_ID_CTRL);
}

static bool
nfp_ctrl_rx_one(struct nfp_net *nn, struct nfp_net_dp *dp,
		struct nfp_net_r_vector *r_vec, struct nfp_net_rx_ring *rx_ring)
{
	unsigned int meta_len, data_len, meta_off, pkt_len, pkt_off;
	struct nfp_net_rx_buf *rxbuf;
	struct nfp_net_rx_desc *rxd;
	dma_addr_t new_dma_addr;
	struct sk_buff *skb;
	void *new_frag;
	int idx;

	idx = D_IDX(rx_ring, rx_ring->rd_p);

	rxd = &rx_ring->rxds[idx];
	if (!(rxd->rxd.meta_len_dd & PCIE_DESC_RX_DD))
		return false;

	/* Memory barrier to ensure that we won't do other reads
	 * before the DD bit.
	 */
	dma_rmb();

	rx_ring->rd_p++;

	rxbuf =	&rx_ring->rxbufs[idx];
	meta_len = rxd->rxd.meta_len_dd & PCIE_DESC_RX_META_LEN_MASK;
	data_len = le16_to_cpu(rxd->rxd.data_len);
	pkt_len = data_len - meta_len;

	pkt_off = NFP_NET_RX_BUF_HEADROOM + dp->rx_dma_off;
	if (dp->rx_offset == NFP_NET_CFG_RX_OFFSET_DYNAMIC)
		pkt_off += meta_len;
	else
		pkt_off += dp->rx_offset;
	meta_off = pkt_off - meta_len;

	/* Stats update */
	u64_stats_update_begin(&r_vec->rx_sync);
	r_vec->rx_pkts++;
	r_vec->rx_bytes += pkt_len;
	u64_stats_update_end(&r_vec->rx_sync);

	nfp_net_dma_sync_cpu_rx(dp, rxbuf->dma_addr + meta_off,	data_len);

	if (unlikely(!nfp_ctrl_meta_ok(nn, rxbuf->frag + meta_off, meta_len))) {
		nn_dp_warn(dp, "incorrect metadata for ctrl packet (%d)\n",
			   meta_len);
		nfp_nfd3_rx_drop(dp, r_vec, rx_ring, rxbuf, NULL);
		return true;
	}

	skb = build_skb(rxbuf->frag, dp->fl_bufsz);
	if (unlikely(!skb)) {
		nfp_nfd3_rx_drop(dp, r_vec, rx_ring, rxbuf, NULL);
		return true;
	}
	new_frag = nfp_nfd3_napi_alloc_one(dp, &new_dma_addr);
	if (unlikely(!new_frag)) {
		nfp_nfd3_rx_drop(dp, r_vec, rx_ring, rxbuf, skb);
		return true;
	}

	nfp_net_dma_unmap_rx(dp, rxbuf->dma_addr);

	nfp_nfd3_rx_give_one(dp, rx_ring, new_frag, new_dma_addr);

	skb_reserve(skb, pkt_off);
	skb_put(skb, pkt_len);

	nfp_app_ctrl_rx(nn->app, skb);

	return true;
}

static bool nfp_ctrl_rx(struct nfp_net_r_vector *r_vec)
{
	struct nfp_net_rx_ring *rx_ring = r_vec->rx_ring;
	struct nfp_net *nn = r_vec->nfp_net;
	struct nfp_net_dp *dp = &nn->dp;
	unsigned int budget = 512;

	while (nfp_ctrl_rx_one(nn, dp, r_vec, rx_ring) && budget--)
		continue;

	return budget;
}

void nfp_nfd3_ctrl_poll(struct tasklet_struct *t)
{
	struct nfp_net_r_vector *r_vec = from_tasklet(r_vec, t, tasklet);

	spin_lock(&r_vec->lock);
	nfp_nfd3_tx_complete(r_vec->tx_ring, 0);
	__nfp_ctrl_tx_queued(r_vec);
	spin_unlock(&r_vec->lock);

	if (nfp_ctrl_rx(r_vec)) {
		nfp_net_irq_unmask(r_vec->nfp_net, r_vec->irq_entry);
	} else {
		tasklet_schedule(&r_vec->tasklet);
		nn_dp_warn(&r_vec->nfp_net->dp,
			   "control message budget exceeded!\n");
	}
}
