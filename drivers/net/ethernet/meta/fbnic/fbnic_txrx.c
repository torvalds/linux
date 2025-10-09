// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/bitfield.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include <net/netdev_queues.h>
#include <net/page_pool/helpers.h>
#include <net/tcp.h>
#include <net/xdp.h>

#include "fbnic.h"
#include "fbnic_csr.h"
#include "fbnic_netdev.h"
#include "fbnic_txrx.h"

enum {
	FBNIC_XDP_PASS = 0,
	FBNIC_XDP_CONSUME,
	FBNIC_XDP_TX,
	FBNIC_XDP_LEN_ERR,
};

enum {
	FBNIC_XMIT_CB_TS	= 0x01,
};

struct fbnic_xmit_cb {
	u32 bytecount;
	u16 gso_segs;
	u8 desc_count;
	u8 flags;
	int hw_head;
};

#define FBNIC_XMIT_CB(__skb) ((struct fbnic_xmit_cb *)((__skb)->cb))

#define FBNIC_XMIT_NOUNMAP	((void *)1)

static u32 __iomem *fbnic_ring_csr_base(const struct fbnic_ring *ring)
{
	unsigned long csr_base = (unsigned long)ring->doorbell;

	csr_base &= ~(FBNIC_QUEUE_STRIDE * sizeof(u32) - 1);

	return (u32 __iomem *)csr_base;
}

static u32 fbnic_ring_rd32(struct fbnic_ring *ring, unsigned int csr)
{
	u32 __iomem *csr_base = fbnic_ring_csr_base(ring);

	return readl(csr_base + csr);
}

static void fbnic_ring_wr32(struct fbnic_ring *ring, unsigned int csr, u32 val)
{
	u32 __iomem *csr_base = fbnic_ring_csr_base(ring);

	writel(val, csr_base + csr);
}

/**
 * fbnic_ts40_to_ns() - convert descriptor timestamp to PHC time
 * @fbn: netdev priv of the FB NIC
 * @ts40: timestamp read from a descriptor
 *
 * Return: u64 value of PHC time in nanoseconds
 *
 * Convert truncated 40 bit device timestamp as read from a descriptor
 * to the full PHC time in nanoseconds.
 */
static __maybe_unused u64 fbnic_ts40_to_ns(struct fbnic_net *fbn, u64 ts40)
{
	unsigned int s;
	u64 time_ns;
	s64 offset;
	u8 ts_top;
	u32 high;

	do {
		s = u64_stats_fetch_begin(&fbn->time_seq);
		offset = READ_ONCE(fbn->time_offset);
	} while (u64_stats_fetch_retry(&fbn->time_seq, s));

	high = READ_ONCE(fbn->time_high);

	/* Bits 63..40 from periodic clock reads, 39..0 from ts40 */
	time_ns = (u64)(high >> 8) << 40 | ts40;

	/* Compare bits 32-39 between periodic reads and ts40,
	 * see if HW clock may have wrapped since last read. We are sure
	 * that periodic reads are always at least ~1 minute behind, so
	 * this logic works perfectly fine.
	 */
	ts_top = ts40 >> 32;
	if (ts_top < (u8)high && (u8)high - ts_top > U8_MAX / 2)
		time_ns += 1ULL << 40;

	return time_ns + offset;
}

static unsigned int fbnic_desc_unused(struct fbnic_ring *ring)
{
	return (ring->head - ring->tail - 1) & ring->size_mask;
}

static unsigned int fbnic_desc_used(struct fbnic_ring *ring)
{
	return (ring->tail - ring->head) & ring->size_mask;
}

static struct netdev_queue *txring_txq(const struct net_device *dev,
				       const struct fbnic_ring *ring)
{
	return netdev_get_tx_queue(dev, ring->q_idx);
}

static int fbnic_maybe_stop_tx(const struct net_device *dev,
			       struct fbnic_ring *ring,
			       const unsigned int size)
{
	struct netdev_queue *txq = txring_txq(dev, ring);
	int res;

	res = netif_txq_maybe_stop(txq, fbnic_desc_unused(ring), size,
				   FBNIC_TX_DESC_WAKEUP);
	if (!res) {
		u64_stats_update_begin(&ring->stats.syncp);
		ring->stats.twq.stop++;
		u64_stats_update_end(&ring->stats.syncp);
	}

	return !res;
}

static bool fbnic_tx_sent_queue(struct sk_buff *skb, struct fbnic_ring *ring)
{
	struct netdev_queue *dev_queue = txring_txq(skb->dev, ring);
	unsigned int bytecount = FBNIC_XMIT_CB(skb)->bytecount;
	bool xmit_more = netdev_xmit_more();

	/* TBD: Request completion more often if xmit_more becomes large */

	return __netdev_tx_sent_queue(dev_queue, bytecount, xmit_more);
}

static void fbnic_unmap_single_twd(struct device *dev, __le64 *twd)
{
	u64 raw_twd = le64_to_cpu(*twd);
	unsigned int len;
	dma_addr_t dma;

	dma = FIELD_GET(FBNIC_TWD_ADDR_MASK, raw_twd);
	len = FIELD_GET(FBNIC_TWD_LEN_MASK, raw_twd);

	dma_unmap_single(dev, dma, len, DMA_TO_DEVICE);
}

static void fbnic_unmap_page_twd(struct device *dev, __le64 *twd)
{
	u64 raw_twd = le64_to_cpu(*twd);
	unsigned int len;
	dma_addr_t dma;

	dma = FIELD_GET(FBNIC_TWD_ADDR_MASK, raw_twd);
	len = FIELD_GET(FBNIC_TWD_LEN_MASK, raw_twd);

	dma_unmap_page(dev, dma, len, DMA_TO_DEVICE);
}

#define FBNIC_TWD_TYPE(_type) \
	cpu_to_le64(FIELD_PREP(FBNIC_TWD_TYPE_MASK, FBNIC_TWD_TYPE_##_type))

static bool fbnic_tx_tstamp(struct sk_buff *skb)
{
	struct fbnic_net *fbn;

	if (!unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		return false;

	fbn = netdev_priv(skb->dev);
	if (fbn->hwtstamp_config.tx_type == HWTSTAMP_TX_OFF)
		return false;

	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
	FBNIC_XMIT_CB(skb)->flags |= FBNIC_XMIT_CB_TS;
	FBNIC_XMIT_CB(skb)->hw_head = -1;

	return true;
}

static bool
fbnic_tx_lso(struct fbnic_ring *ring, struct sk_buff *skb,
	     struct skb_shared_info *shinfo, __le64 *meta,
	     unsigned int *l2len, unsigned int *i3len)
{
	unsigned int l3_type, l4_type, l4len, hdrlen;
	unsigned char *l4hdr;
	__be16 payload_len;

	if (unlikely(skb_cow_head(skb, 0)))
		return true;

	if (shinfo->gso_type & SKB_GSO_PARTIAL) {
		l3_type = FBNIC_TWD_L3_TYPE_OTHER;
	} else if (!skb->encapsulation) {
		if (ip_hdr(skb)->version == 4)
			l3_type = FBNIC_TWD_L3_TYPE_IPV4;
		else
			l3_type = FBNIC_TWD_L3_TYPE_IPV6;
	} else {
		unsigned int o3len;

		o3len = skb_inner_network_header(skb) - skb_network_header(skb);
		*i3len -= o3len;
		*meta |= cpu_to_le64(FIELD_PREP(FBNIC_TWD_L3_OHLEN_MASK,
						o3len / 2));
		l3_type = FBNIC_TWD_L3_TYPE_V6V6;
	}

	l4hdr = skb_checksum_start(skb);
	payload_len = cpu_to_be16(skb->len - (l4hdr - skb->data));

	if (shinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6)) {
		struct tcphdr *tcph = (struct tcphdr *)l4hdr;

		l4_type = FBNIC_TWD_L4_TYPE_TCP;
		l4len = __tcp_hdrlen((struct tcphdr *)l4hdr);
		csum_replace_by_diff(&tcph->check, (__force __wsum)payload_len);
	} else {
		struct udphdr *udph = (struct udphdr *)l4hdr;

		l4_type = FBNIC_TWD_L4_TYPE_UDP;
		l4len = sizeof(struct udphdr);
		csum_replace_by_diff(&udph->check, (__force __wsum)payload_len);
	}

	hdrlen = (l4hdr - skb->data) + l4len;
	*meta |= cpu_to_le64(FIELD_PREP(FBNIC_TWD_L3_TYPE_MASK, l3_type) |
			     FIELD_PREP(FBNIC_TWD_L4_TYPE_MASK, l4_type) |
			     FIELD_PREP(FBNIC_TWD_L4_HLEN_MASK, l4len / 4) |
			     FIELD_PREP(FBNIC_TWD_MSS_MASK, shinfo->gso_size) |
			     FBNIC_TWD_FLAG_REQ_LSO);

	FBNIC_XMIT_CB(skb)->bytecount += (shinfo->gso_segs - 1) * hdrlen;
	FBNIC_XMIT_CB(skb)->gso_segs = shinfo->gso_segs;

	u64_stats_update_begin(&ring->stats.syncp);
	ring->stats.twq.lso += shinfo->gso_segs;
	u64_stats_update_end(&ring->stats.syncp);

	return false;
}

static bool
fbnic_tx_offloads(struct fbnic_ring *ring, struct sk_buff *skb, __le64 *meta)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	unsigned int l2len, i3len;

	if (fbnic_tx_tstamp(skb))
		*meta |= cpu_to_le64(FBNIC_TWD_FLAG_REQ_TS);

	if (unlikely(skb->ip_summed != CHECKSUM_PARTIAL))
		return false;

	l2len = skb_mac_header_len(skb);
	i3len = skb_checksum_start(skb) - skb_network_header(skb);

	*meta |= cpu_to_le64(FIELD_PREP(FBNIC_TWD_CSUM_OFFSET_MASK,
					skb->csum_offset / 2));

	if (shinfo->gso_size) {
		if (fbnic_tx_lso(ring, skb, shinfo, meta, &l2len, &i3len))
			return true;
	} else {
		*meta |= cpu_to_le64(FBNIC_TWD_FLAG_REQ_CSO);
		u64_stats_update_begin(&ring->stats.syncp);
		ring->stats.twq.csum_partial++;
		u64_stats_update_end(&ring->stats.syncp);
	}

	*meta |= cpu_to_le64(FIELD_PREP(FBNIC_TWD_L2_HLEN_MASK, l2len / 2) |
			     FIELD_PREP(FBNIC_TWD_L3_IHLEN_MASK, i3len / 2));
	return false;
}

static void
fbnic_rx_csum(u64 rcd, struct sk_buff *skb, struct fbnic_ring *rcq,
	      u64 *csum_cmpl, u64 *csum_none)
{
	skb_checksum_none_assert(skb);

	if (unlikely(!(skb->dev->features & NETIF_F_RXCSUM))) {
		(*csum_none)++;
		return;
	}

	if (FIELD_GET(FBNIC_RCD_META_L4_CSUM_UNNECESSARY, rcd)) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else {
		u16 csum = FIELD_GET(FBNIC_RCD_META_L2_CSUM_MASK, rcd);

		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->csum = (__force __wsum)csum;
		(*csum_cmpl)++;
	}
}

static bool
fbnic_tx_map(struct fbnic_ring *ring, struct sk_buff *skb, __le64 *meta)
{
	struct device *dev = skb->dev->dev.parent;
	unsigned int tail = ring->tail, first;
	unsigned int size, data_len;
	skb_frag_t *frag;
	bool is_net_iov;
	dma_addr_t dma;
	__le64 *twd;

	ring->tx_buf[tail] = skb;

	tail++;
	tail &= ring->size_mask;
	first = tail;

	size = skb_headlen(skb);
	data_len = skb->data_len;

	if (size > FIELD_MAX(FBNIC_TWD_LEN_MASK))
		goto dma_error;

	is_net_iov = false;
	dma = dma_map_single(dev, skb->data, size, DMA_TO_DEVICE);

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		twd = &ring->desc[tail];

		if (dma_mapping_error(dev, dma))
			goto dma_error;

		*twd = cpu_to_le64(FIELD_PREP(FBNIC_TWD_ADDR_MASK, dma) |
				   FIELD_PREP(FBNIC_TWD_LEN_MASK, size) |
				   FIELD_PREP(FBNIC_TWD_TYPE_MASK,
					      FBNIC_TWD_TYPE_AL));
		if (is_net_iov)
			ring->tx_buf[tail] = FBNIC_XMIT_NOUNMAP;

		tail++;
		tail &= ring->size_mask;

		if (!data_len)
			break;

		size = skb_frag_size(frag);
		data_len -= size;

		if (size > FIELD_MAX(FBNIC_TWD_LEN_MASK))
			goto dma_error;

		is_net_iov = skb_frag_is_net_iov(frag);
		dma = skb_frag_dma_map(dev, frag, 0, size, DMA_TO_DEVICE);
	}

	*twd |= FBNIC_TWD_TYPE(LAST_AL);

	FBNIC_XMIT_CB(skb)->desc_count = ((twd - meta) + 1) & ring->size_mask;

	ring->tail = tail;

	/* Record SW timestamp */
	skb_tx_timestamp(skb);

	/* Verify there is room for another packet */
	fbnic_maybe_stop_tx(skb->dev, ring, FBNIC_MAX_SKB_DESC);

	if (fbnic_tx_sent_queue(skb, ring)) {
		*meta |= cpu_to_le64(FBNIC_TWD_FLAG_REQ_COMPLETION);

		/* Force DMA writes to flush before writing to tail */
		dma_wmb();

		writel(tail, ring->doorbell);
	}

	return false;
dma_error:
	if (net_ratelimit())
		netdev_err(skb->dev, "TX DMA map failed\n");

	while (tail != first) {
		tail--;
		tail &= ring->size_mask;
		twd = &ring->desc[tail];
		if (tail == first)
			fbnic_unmap_single_twd(dev, twd);
		else if (ring->tx_buf[tail] == FBNIC_XMIT_NOUNMAP)
			ring->tx_buf[tail] = NULL;
		else
			fbnic_unmap_page_twd(dev, twd);
	}

	return true;
}

#define FBNIC_MIN_FRAME_LEN	60

static netdev_tx_t
fbnic_xmit_frame_ring(struct sk_buff *skb, struct fbnic_ring *ring)
{
	__le64 *meta = &ring->desc[ring->tail];
	u16 desc_needed;

	if (skb_put_padto(skb, FBNIC_MIN_FRAME_LEN))
		goto err_count;

	/* Need: 1 descriptor per page,
	 *       + 1 desc for skb_head,
	 *       + 2 desc for metadata and timestamp metadata
	 *       + 7 desc gap to keep tail from touching head
	 * otherwise try next time
	 */
	desc_needed = skb_shinfo(skb)->nr_frags + 10;
	if (fbnic_maybe_stop_tx(skb->dev, ring, desc_needed))
		return NETDEV_TX_BUSY;

	*meta = cpu_to_le64(FBNIC_TWD_FLAG_DEST_MAC);

	/* Write all members within DWORD to condense this into 2 4B writes */
	FBNIC_XMIT_CB(skb)->bytecount = skb->len;
	FBNIC_XMIT_CB(skb)->gso_segs = 1;
	FBNIC_XMIT_CB(skb)->desc_count = 0;
	FBNIC_XMIT_CB(skb)->flags = 0;

	if (fbnic_tx_offloads(ring, skb, meta))
		goto err_free;

	if (fbnic_tx_map(ring, skb, meta))
		goto err_free;

	return NETDEV_TX_OK;

err_free:
	dev_kfree_skb_any(skb);
err_count:
	u64_stats_update_begin(&ring->stats.syncp);
	ring->stats.dropped++;
	u64_stats_update_end(&ring->stats.syncp);
	return NETDEV_TX_OK;
}

netdev_tx_t fbnic_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
	struct fbnic_net *fbn = netdev_priv(dev);
	unsigned int q_map = skb->queue_mapping;

	return fbnic_xmit_frame_ring(skb, fbn->tx[q_map]);
}

static netdev_features_t
fbnic_features_check_encap_gso(struct sk_buff *skb, struct net_device *dev,
			       netdev_features_t features, unsigned int l3len)
{
	netdev_features_t skb_gso_features;
	struct ipv6hdr *ip6_hdr;
	unsigned char l4_hdr;
	unsigned int start;
	__be16 frag_off;

	/* Require MANGLEID for GSO_PARTIAL of IPv4.
	 * In theory we could support TSO with single, innermost v4 header
	 * by pretending everything before it is L2, but that needs to be
	 * parsed case by case.. so leaving it for when the need arises.
	 */
	if (!(features & NETIF_F_TSO_MANGLEID))
		features &= ~NETIF_F_TSO;

	skb_gso_features = skb_shinfo(skb)->gso_type;
	skb_gso_features <<= NETIF_F_GSO_SHIFT;

	/* We'd only clear the native GSO features, so don't bother validating
	 * if the match can only be on those supported thru GSO_PARTIAL.
	 */
	if (!(skb_gso_features & FBNIC_TUN_GSO_FEATURES))
		return features;

	/* We can only do IPv6-in-IPv6, not v4-in-v6. It'd be nice
	 * to fall back to partial for this, or any failure below.
	 * This is just an optimization, UDPv4 will be caught later on.
	 */
	if (skb_gso_features & NETIF_F_TSO)
		return features & ~FBNIC_TUN_GSO_FEATURES;

	/* Inner headers multiple of 2 */
	if ((skb_inner_network_header(skb) - skb_network_header(skb)) % 2)
		return features & ~FBNIC_TUN_GSO_FEATURES;

	/* Encapsulated GSO packet, make 100% sure it's IPv6-in-IPv6. */
	ip6_hdr = ipv6_hdr(skb);
	if (ip6_hdr->version != 6)
		return features & ~FBNIC_TUN_GSO_FEATURES;

	l4_hdr = ip6_hdr->nexthdr;
	start = (unsigned char *)ip6_hdr - skb->data + sizeof(struct ipv6hdr);
	start = ipv6_skip_exthdr(skb, start, &l4_hdr, &frag_off);
	if (frag_off || l4_hdr != IPPROTO_IPV6 ||
	    skb->data + start != skb_inner_network_header(skb))
		return features & ~FBNIC_TUN_GSO_FEATURES;

	return features;
}

netdev_features_t
fbnic_features_check(struct sk_buff *skb, struct net_device *dev,
		     netdev_features_t features)
{
	unsigned int l2len, l3len;

	if (unlikely(skb->ip_summed != CHECKSUM_PARTIAL))
		return features;

	l2len = skb_mac_header_len(skb);
	l3len = skb_checksum_start(skb) - skb_network_header(skb);

	/* Check header lengths are multiple of 2.
	 * In case of 6in6 we support longer headers (IHLEN + OHLEN)
	 * but keep things simple for now, 512B is plenty.
	 */
	if ((l2len | l3len | skb->csum_offset) % 2 ||
	    !FIELD_FIT(FBNIC_TWD_L2_HLEN_MASK, l2len / 2) ||
	    !FIELD_FIT(FBNIC_TWD_L3_IHLEN_MASK, l3len / 2) ||
	    !FIELD_FIT(FBNIC_TWD_CSUM_OFFSET_MASK, skb->csum_offset / 2))
		return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);

	if (likely(!skb->encapsulation) || !skb_is_gso(skb))
		return features;

	return fbnic_features_check_encap_gso(skb, dev, features, l3len);
}

static void fbnic_clean_twq0(struct fbnic_napi_vector *nv, int napi_budget,
			     struct fbnic_ring *ring, bool discard,
			     unsigned int hw_head)
{
	u64 total_bytes = 0, total_packets = 0, ts_lost = 0;
	unsigned int head = ring->head;
	struct netdev_queue *txq;
	unsigned int clean_desc;

	clean_desc = (hw_head - head) & ring->size_mask;

	while (clean_desc) {
		struct sk_buff *skb = ring->tx_buf[head];
		unsigned int desc_cnt;

		desc_cnt = FBNIC_XMIT_CB(skb)->desc_count;
		if (desc_cnt > clean_desc)
			break;

		if (unlikely(FBNIC_XMIT_CB(skb)->flags & FBNIC_XMIT_CB_TS)) {
			FBNIC_XMIT_CB(skb)->hw_head = hw_head;
			if (likely(!discard))
				break;
			ts_lost++;
		}

		ring->tx_buf[head] = NULL;

		clean_desc -= desc_cnt;

		while (!(ring->desc[head] & FBNIC_TWD_TYPE(AL))) {
			head++;
			head &= ring->size_mask;
			desc_cnt--;
		}

		fbnic_unmap_single_twd(nv->dev, &ring->desc[head]);
		head++;
		head &= ring->size_mask;
		desc_cnt--;

		while (desc_cnt--) {
			if (ring->tx_buf[head] != FBNIC_XMIT_NOUNMAP)
				fbnic_unmap_page_twd(nv->dev,
						     &ring->desc[head]);
			else
				ring->tx_buf[head] = NULL;
			head++;
			head &= ring->size_mask;
		}

		total_bytes += FBNIC_XMIT_CB(skb)->bytecount;
		total_packets += FBNIC_XMIT_CB(skb)->gso_segs;

		napi_consume_skb(skb, napi_budget);
	}

	if (!total_bytes)
		return;

	ring->head = head;

	txq = txring_txq(nv->napi.dev, ring);

	if (unlikely(discard)) {
		u64_stats_update_begin(&ring->stats.syncp);
		ring->stats.dropped += total_packets;
		ring->stats.twq.ts_lost += ts_lost;
		u64_stats_update_end(&ring->stats.syncp);

		netdev_tx_completed_queue(txq, total_packets, total_bytes);
		return;
	}

	u64_stats_update_begin(&ring->stats.syncp);
	ring->stats.bytes += total_bytes;
	ring->stats.packets += total_packets;
	u64_stats_update_end(&ring->stats.syncp);

	if (!netif_txq_completed_wake(txq, total_packets, total_bytes,
				      fbnic_desc_unused(ring),
				      FBNIC_TX_DESC_WAKEUP)) {
		u64_stats_update_begin(&ring->stats.syncp);
		ring->stats.twq.wake++;
		u64_stats_update_end(&ring->stats.syncp);
	}
}

static void fbnic_clean_twq1(struct fbnic_napi_vector *nv, bool pp_allow_direct,
			     struct fbnic_ring *ring, bool discard,
			     unsigned int hw_head)
{
	u64 total_bytes = 0, total_packets = 0;
	unsigned int head = ring->head;

	while (hw_head != head) {
		struct page *page;
		u64 twd;

		if (unlikely(!(ring->desc[head] & FBNIC_TWD_TYPE(AL))))
			goto next_desc;

		twd = le64_to_cpu(ring->desc[head]);
		page = ring->tx_buf[head];

		/* TYPE_AL is 2, TYPE_LAST_AL is 3. So this trick gives
		 * us one increment per packet, with no branches.
		 */
		total_packets += FIELD_GET(FBNIC_TWD_TYPE_MASK, twd) -
				 FBNIC_TWD_TYPE_AL;
		total_bytes += FIELD_GET(FBNIC_TWD_LEN_MASK, twd);

		page_pool_put_page(page->pp, page, -1, pp_allow_direct);
next_desc:
		head++;
		head &= ring->size_mask;
	}

	if (!total_bytes)
		return;

	ring->head = head;

	if (discard) {
		u64_stats_update_begin(&ring->stats.syncp);
		ring->stats.dropped += total_packets;
		u64_stats_update_end(&ring->stats.syncp);
		return;
	}

	u64_stats_update_begin(&ring->stats.syncp);
	ring->stats.bytes += total_bytes;
	ring->stats.packets += total_packets;
	u64_stats_update_end(&ring->stats.syncp);
}

static void fbnic_clean_tsq(struct fbnic_napi_vector *nv,
			    struct fbnic_ring *ring,
			    u64 tcd, int *ts_head, int *head0)
{
	struct skb_shared_hwtstamps hwtstamp;
	struct fbnic_net *fbn;
	struct sk_buff *skb;
	int head;
	u64 ns;

	head = (*ts_head < 0) ? ring->head : *ts_head;

	do {
		unsigned int desc_cnt;

		if (head == ring->tail) {
			if (unlikely(net_ratelimit()))
				netdev_err(nv->napi.dev,
					   "Tx timestamp without matching packet\n");
			return;
		}

		skb = ring->tx_buf[head];
		desc_cnt = FBNIC_XMIT_CB(skb)->desc_count;

		head += desc_cnt;
		head &= ring->size_mask;
	} while (!(FBNIC_XMIT_CB(skb)->flags & FBNIC_XMIT_CB_TS));

	fbn = netdev_priv(nv->napi.dev);
	ns = fbnic_ts40_to_ns(fbn, FIELD_GET(FBNIC_TCD_TYPE1_TS_MASK, tcd));

	memset(&hwtstamp, 0, sizeof(hwtstamp));
	hwtstamp.hwtstamp = ns_to_ktime(ns);

	*ts_head = head;

	FBNIC_XMIT_CB(skb)->flags &= ~FBNIC_XMIT_CB_TS;
	if (*head0 < 0) {
		head = FBNIC_XMIT_CB(skb)->hw_head;
		if (head >= 0)
			*head0 = head;
	}

	skb_tstamp_tx(skb, &hwtstamp);
	u64_stats_update_begin(&ring->stats.syncp);
	ring->stats.twq.ts_packets++;
	u64_stats_update_end(&ring->stats.syncp);
}

static void fbnic_page_pool_init(struct fbnic_ring *ring, unsigned int idx,
				 netmem_ref netmem)
{
	struct fbnic_rx_buf *rx_buf = &ring->rx_buf[idx];

	page_pool_fragment_netmem(netmem, FBNIC_PAGECNT_BIAS_MAX);
	rx_buf->pagecnt_bias = FBNIC_PAGECNT_BIAS_MAX;
	rx_buf->netmem = netmem;
}

static struct page *
fbnic_page_pool_get_head(struct fbnic_q_triad *qt, unsigned int idx)
{
	struct fbnic_rx_buf *rx_buf = &qt->sub0.rx_buf[idx];

	rx_buf->pagecnt_bias--;

	/* sub0 is always fed system pages, from the NAPI-level page_pool */
	return netmem_to_page(rx_buf->netmem);
}

static netmem_ref
fbnic_page_pool_get_data(struct fbnic_q_triad *qt, unsigned int idx)
{
	struct fbnic_rx_buf *rx_buf = &qt->sub1.rx_buf[idx];

	rx_buf->pagecnt_bias--;

	return rx_buf->netmem;
}

static void fbnic_page_pool_drain(struct fbnic_ring *ring, unsigned int idx,
				  int budget)
{
	struct fbnic_rx_buf *rx_buf = &ring->rx_buf[idx];
	netmem_ref netmem = rx_buf->netmem;

	if (!page_pool_unref_netmem(netmem, rx_buf->pagecnt_bias))
		page_pool_put_unrefed_netmem(ring->page_pool, netmem, -1,
					     !!budget);

	rx_buf->netmem = 0;
}

static void fbnic_clean_twq(struct fbnic_napi_vector *nv, int napi_budget,
			    struct fbnic_q_triad *qt, s32 ts_head, s32 head0,
			    s32 head1)
{
	if (head0 >= 0)
		fbnic_clean_twq0(nv, napi_budget, &qt->sub0, false, head0);
	else if (ts_head >= 0)
		fbnic_clean_twq0(nv, napi_budget, &qt->sub0, false, ts_head);

	if (head1 >= 0) {
		qt->cmpl.deferred_head = -1;
		if (napi_budget)
			fbnic_clean_twq1(nv, true, &qt->sub1, false, head1);
		else
			qt->cmpl.deferred_head = head1;
	}
}

static void
fbnic_clean_tcq(struct fbnic_napi_vector *nv, struct fbnic_q_triad *qt,
		int napi_budget)
{
	struct fbnic_ring *cmpl = &qt->cmpl;
	s32 head1 = cmpl->deferred_head;
	s32 head0 = -1, ts_head = -1;
	__le64 *raw_tcd, done;
	u32 head = cmpl->head;

	done = (head & (cmpl->size_mask + 1)) ? 0 : cpu_to_le64(FBNIC_TCD_DONE);
	raw_tcd = &cmpl->desc[head & cmpl->size_mask];

	/* Walk the completion queue collecting the heads reported by NIC */
	while ((*raw_tcd & cpu_to_le64(FBNIC_TCD_DONE)) == done) {
		u64 tcd;

		dma_rmb();

		tcd = le64_to_cpu(*raw_tcd);

		switch (FIELD_GET(FBNIC_TCD_TYPE_MASK, tcd)) {
		case FBNIC_TCD_TYPE_0:
			if (tcd & FBNIC_TCD_TWQ1)
				head1 = FIELD_GET(FBNIC_TCD_TYPE0_HEAD1_MASK,
						  tcd);
			else
				head0 = FIELD_GET(FBNIC_TCD_TYPE0_HEAD0_MASK,
						  tcd);
			/* Currently all err status bits are related to
			 * timestamps and as those have yet to be added
			 * they are skipped for now.
			 */
			break;
		case FBNIC_TCD_TYPE_1:
			if (WARN_ON_ONCE(tcd & FBNIC_TCD_TWQ1))
				break;

			fbnic_clean_tsq(nv, &qt->sub0, tcd, &ts_head, &head0);
			break;
		default:
			break;
		}

		raw_tcd++;
		head++;
		if (!(head & cmpl->size_mask)) {
			done ^= cpu_to_le64(FBNIC_TCD_DONE);
			raw_tcd = &cmpl->desc[0];
		}
	}

	/* Record the current head/tail of the queue */
	if (cmpl->head != head) {
		cmpl->head = head;
		writel(head & cmpl->size_mask, cmpl->doorbell);
	}

	/* Unmap and free processed buffers */
	fbnic_clean_twq(nv, napi_budget, qt, ts_head, head0, head1);
}

static void fbnic_clean_bdq(struct fbnic_ring *ring, unsigned int hw_head,
			    int napi_budget)
{
	unsigned int head = ring->head;

	if (head == hw_head)
		return;

	do {
		fbnic_page_pool_drain(ring, head, napi_budget);

		head++;
		head &= ring->size_mask;
	} while (head != hw_head);

	ring->head = head;
}

static void fbnic_bd_prep(struct fbnic_ring *bdq, u16 id, netmem_ref netmem)
{
	__le64 *bdq_desc = &bdq->desc[id * FBNIC_BD_FRAG_COUNT];
	dma_addr_t dma = page_pool_get_dma_addr_netmem(netmem);
	u64 bd, i = FBNIC_BD_FRAG_COUNT;

	bd = (FBNIC_BD_PAGE_ADDR_MASK & dma) |
	     FIELD_PREP(FBNIC_BD_PAGE_ID_MASK, id);

	/* In the case that a page size is larger than 4K we will map a
	 * single page to multiple fragments. The fragments will be
	 * FBNIC_BD_FRAG_COUNT in size and the lower n bits will be use
	 * to indicate the individual fragment IDs.
	 */
	do {
		*bdq_desc = cpu_to_le64(bd);
		bd += FIELD_PREP(FBNIC_BD_DESC_ADDR_MASK, 1) |
		      FIELD_PREP(FBNIC_BD_DESC_ID_MASK, 1);
	} while (--i);
}

static void fbnic_fill_bdq(struct fbnic_ring *bdq)
{
	unsigned int count = fbnic_desc_unused(bdq);
	unsigned int i = bdq->tail;

	if (!count)
		return;

	do {
		netmem_ref netmem;

		netmem = page_pool_dev_alloc_netmems(bdq->page_pool);
		if (!netmem) {
			u64_stats_update_begin(&bdq->stats.syncp);
			bdq->stats.bdq.alloc_failed++;
			u64_stats_update_end(&bdq->stats.syncp);

			break;
		}

		fbnic_page_pool_init(bdq, i, netmem);
		fbnic_bd_prep(bdq, i, netmem);

		i++;
		i &= bdq->size_mask;

		count--;
	} while (count);

	if (bdq->tail != i) {
		bdq->tail = i;

		/* Force DMA writes to flush before writing to tail */
		dma_wmb();

		writel(i, bdq->doorbell);
	}
}

static unsigned int fbnic_hdr_pg_start(unsigned int pg_off)
{
	/* The headroom of the first header may be larger than FBNIC_RX_HROOM
	 * due to alignment. So account for that by just making the page
	 * offset 0 if we are starting at the first header.
	 */
	if (ALIGN(FBNIC_RX_HROOM, 128) > FBNIC_RX_HROOM &&
	    pg_off == ALIGN(FBNIC_RX_HROOM, 128))
		return 0;

	return pg_off - FBNIC_RX_HROOM;
}

static unsigned int fbnic_hdr_pg_end(unsigned int pg_off, unsigned int len)
{
	/* Determine the end of the buffer by finding the start of the next
	 * and then subtracting the headroom from that frame.
	 */
	pg_off += len + FBNIC_RX_TROOM + FBNIC_RX_HROOM;

	return ALIGN(pg_off, 128) - FBNIC_RX_HROOM;
}

static void fbnic_pkt_prepare(struct fbnic_napi_vector *nv, u64 rcd,
			      struct fbnic_pkt_buff *pkt,
			      struct fbnic_q_triad *qt)
{
	unsigned int hdr_pg_idx = FIELD_GET(FBNIC_RCD_AL_BUFF_PAGE_MASK, rcd);
	unsigned int hdr_pg_off = FIELD_GET(FBNIC_RCD_AL_BUFF_OFF_MASK, rcd);
	struct page *page = fbnic_page_pool_get_head(qt, hdr_pg_idx);
	unsigned int len = FIELD_GET(FBNIC_RCD_AL_BUFF_LEN_MASK, rcd);
	unsigned int frame_sz, hdr_pg_start, hdr_pg_end, headroom;
	unsigned char *hdr_start;

	/* data_hard_start should always be NULL when this is called */
	WARN_ON_ONCE(pkt->buff.data_hard_start);

	/* Short-cut the end calculation if we know page is fully consumed */
	hdr_pg_end = FIELD_GET(FBNIC_RCD_AL_PAGE_FIN, rcd) ?
		     FBNIC_BD_FRAG_SIZE : fbnic_hdr_pg_end(hdr_pg_off, len);
	hdr_pg_start = fbnic_hdr_pg_start(hdr_pg_off);

	headroom = hdr_pg_off - hdr_pg_start + FBNIC_RX_PAD;
	frame_sz = hdr_pg_end - hdr_pg_start;
	xdp_init_buff(&pkt->buff, frame_sz, &qt->xdp_rxq);
	hdr_pg_start += (FBNIC_RCD_AL_BUFF_FRAG_MASK & rcd) *
			FBNIC_BD_FRAG_SIZE;

	/* Sync DMA buffer */
	dma_sync_single_range_for_cpu(nv->dev, page_pool_get_dma_addr(page),
				      hdr_pg_start, frame_sz,
				      DMA_BIDIRECTIONAL);

	/* Build frame around buffer */
	hdr_start = page_address(page) + hdr_pg_start;
	net_prefetch(pkt->buff.data);
	xdp_prepare_buff(&pkt->buff, hdr_start, headroom,
			 len - FBNIC_RX_PAD, true);

	pkt->hwtstamp = 0;
	pkt->add_frag_failed = false;
}

static void fbnic_add_rx_frag(struct fbnic_napi_vector *nv, u64 rcd,
			      struct fbnic_pkt_buff *pkt,
			      struct fbnic_q_triad *qt)
{
	unsigned int pg_idx = FIELD_GET(FBNIC_RCD_AL_BUFF_PAGE_MASK, rcd);
	unsigned int pg_off = FIELD_GET(FBNIC_RCD_AL_BUFF_OFF_MASK, rcd);
	unsigned int len = FIELD_GET(FBNIC_RCD_AL_BUFF_LEN_MASK, rcd);
	netmem_ref netmem = fbnic_page_pool_get_data(qt, pg_idx);
	unsigned int truesize;
	bool added;

	truesize = FIELD_GET(FBNIC_RCD_AL_PAGE_FIN, rcd) ?
		   FBNIC_BD_FRAG_SIZE - pg_off : ALIGN(len, 128);

	pg_off += (FBNIC_RCD_AL_BUFF_FRAG_MASK & rcd) *
		  FBNIC_BD_FRAG_SIZE;

	/* Sync DMA buffer */
	page_pool_dma_sync_netmem_for_cpu(qt->sub1.page_pool, netmem,
					  pg_off, truesize);

	added = xdp_buff_add_frag(&pkt->buff, netmem, pg_off, len, truesize);
	if (unlikely(!added)) {
		pkt->add_frag_failed = true;
		netdev_err_once(nv->napi.dev,
				"Failed to add fragment to xdp_buff\n");
	}
}

static void fbnic_put_pkt_buff(struct fbnic_q_triad *qt,
			       struct fbnic_pkt_buff *pkt, int budget)
{
	struct page *page;

	if (!pkt->buff.data_hard_start)
		return;

	if (xdp_buff_has_frags(&pkt->buff)) {
		struct skb_shared_info *shinfo;
		netmem_ref netmem;
		int nr_frags;

		shinfo = xdp_get_shared_info_from_buff(&pkt->buff);
		nr_frags = shinfo->nr_frags;

		while (nr_frags--) {
			netmem = skb_frag_netmem(&shinfo->frags[nr_frags]);
			page_pool_put_full_netmem(qt->sub1.page_pool, netmem,
						  !!budget);
		}
	}

	page = virt_to_page(pkt->buff.data_hard_start);
	page_pool_put_full_page(qt->sub0.page_pool, page, !!budget);
}

static struct sk_buff *fbnic_build_skb(struct fbnic_napi_vector *nv,
				       struct fbnic_pkt_buff *pkt)
{
	struct sk_buff *skb;

	skb = xdp_build_skb_from_buff(&pkt->buff);
	if (!skb)
		return NULL;

	/* Add timestamp if present */
	if (pkt->hwtstamp)
		skb_hwtstamps(skb)->hwtstamp = pkt->hwtstamp;

	return skb;
}

static long fbnic_pkt_tx(struct fbnic_napi_vector *nv,
			 struct fbnic_pkt_buff *pkt)
{
	struct fbnic_ring *ring = &nv->qt[0].sub1;
	int size, offset, nsegs = 1, data_len = 0;
	unsigned int tail = ring->tail;
	struct skb_shared_info *shinfo;
	skb_frag_t *frag = NULL;
	struct page *page;
	dma_addr_t dma;
	__le64 *twd;

	if (unlikely(xdp_buff_has_frags(&pkt->buff))) {
		shinfo = xdp_get_shared_info_from_buff(&pkt->buff);
		nsegs += shinfo->nr_frags;
		data_len = shinfo->xdp_frags_size;
		frag = &shinfo->frags[0];
	}

	if (fbnic_desc_unused(ring) < nsegs) {
		u64_stats_update_begin(&ring->stats.syncp);
		ring->stats.dropped++;
		u64_stats_update_end(&ring->stats.syncp);
		return -FBNIC_XDP_CONSUME;
	}

	page = virt_to_page(pkt->buff.data_hard_start);
	offset = offset_in_page(pkt->buff.data);
	dma = page_pool_get_dma_addr(page);

	size = pkt->buff.data_end - pkt->buff.data;

	while (nsegs--) {
		dma_sync_single_range_for_device(nv->dev, dma, offset, size,
						 DMA_BIDIRECTIONAL);
		dma += offset;

		ring->tx_buf[tail] = page;

		twd = &ring->desc[tail];
		*twd = cpu_to_le64(FIELD_PREP(FBNIC_TWD_ADDR_MASK, dma) |
				   FIELD_PREP(FBNIC_TWD_LEN_MASK, size) |
				   FIELD_PREP(FBNIC_TWD_TYPE_MASK,
					      FBNIC_TWD_TYPE_AL));

		tail++;
		tail &= ring->size_mask;

		if (!data_len)
			break;

		offset = skb_frag_off(frag);
		page = skb_frag_page(frag);
		dma = page_pool_get_dma_addr(page);

		size = skb_frag_size(frag);
		data_len -= size;
		frag++;
	}

	*twd |= FBNIC_TWD_TYPE(LAST_AL);

	ring->tail = tail;

	return -FBNIC_XDP_TX;
}

static void fbnic_pkt_commit_tail(struct fbnic_napi_vector *nv,
				  unsigned int pkt_tail)
{
	struct fbnic_ring *ring = &nv->qt[0].sub1;

	/* Force DMA writes to flush before writing to tail */
	dma_wmb();

	writel(pkt_tail, ring->doorbell);
}

static struct sk_buff *fbnic_run_xdp(struct fbnic_napi_vector *nv,
				     struct fbnic_pkt_buff *pkt)
{
	struct fbnic_net *fbn = netdev_priv(nv->napi.dev);
	struct bpf_prog *xdp_prog;
	int act;

	xdp_prog = READ_ONCE(fbn->xdp_prog);
	if (!xdp_prog)
		goto xdp_pass;

	/* Should never happen, config paths enforce HDS threshold > MTU */
	if (xdp_buff_has_frags(&pkt->buff) && !xdp_prog->aux->xdp_has_frags)
		return ERR_PTR(-FBNIC_XDP_LEN_ERR);

	act = bpf_prog_run_xdp(xdp_prog, &pkt->buff);
	switch (act) {
	case XDP_PASS:
xdp_pass:
		return fbnic_build_skb(nv, pkt);
	case XDP_TX:
		return ERR_PTR(fbnic_pkt_tx(nv, pkt));
	default:
		bpf_warn_invalid_xdp_action(nv->napi.dev, xdp_prog, act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(nv->napi.dev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
		break;
	}

	return ERR_PTR(-FBNIC_XDP_CONSUME);
}

static enum pkt_hash_types fbnic_skb_hash_type(u64 rcd)
{
	return (FBNIC_RCD_META_L4_TYPE_MASK & rcd) ? PKT_HASH_TYPE_L4 :
	       (FBNIC_RCD_META_L3_TYPE_MASK & rcd) ? PKT_HASH_TYPE_L3 :
						     PKT_HASH_TYPE_L2;
}

static void fbnic_rx_tstamp(struct fbnic_napi_vector *nv, u64 rcd,
			    struct fbnic_pkt_buff *pkt)
{
	struct fbnic_net *fbn;
	u64 ns, ts;

	if (!FIELD_GET(FBNIC_RCD_OPT_META_TS, rcd))
		return;

	fbn = netdev_priv(nv->napi.dev);
	ts = FIELD_GET(FBNIC_RCD_OPT_META_TS_MASK, rcd);
	ns = fbnic_ts40_to_ns(fbn, ts);

	/* Add timestamp to shared info */
	pkt->hwtstamp = ns_to_ktime(ns);
}

static void fbnic_populate_skb_fields(struct fbnic_napi_vector *nv,
				      u64 rcd, struct sk_buff *skb,
				      struct fbnic_q_triad *qt,
				      u64 *csum_cmpl, u64 *csum_none)
{
	struct net_device *netdev = nv->napi.dev;
	struct fbnic_ring *rcq = &qt->cmpl;

	fbnic_rx_csum(rcd, skb, rcq, csum_cmpl, csum_none);

	if (netdev->features & NETIF_F_RXHASH)
		skb_set_hash(skb,
			     FIELD_GET(FBNIC_RCD_META_RSS_HASH_MASK, rcd),
			     fbnic_skb_hash_type(rcd));

	skb_record_rx_queue(skb, rcq->q_idx);
}

static bool fbnic_rcd_metadata_err(u64 rcd)
{
	return !!(FBNIC_RCD_META_UNCORRECTABLE_ERR_MASK & rcd);
}

static int fbnic_clean_rcq(struct fbnic_napi_vector *nv,
			   struct fbnic_q_triad *qt, int budget)
{
	unsigned int packets = 0, bytes = 0, dropped = 0, alloc_failed = 0;
	u64 csum_complete = 0, csum_none = 0, length_errors = 0;
	s32 head0 = -1, head1 = -1, pkt_tail = -1;
	struct fbnic_ring *rcq = &qt->cmpl;
	struct fbnic_pkt_buff *pkt;
	__le64 *raw_rcd, done;
	u32 head = rcq->head;

	done = (head & (rcq->size_mask + 1)) ? cpu_to_le64(FBNIC_RCD_DONE) : 0;
	raw_rcd = &rcq->desc[head & rcq->size_mask];
	pkt = rcq->pkt;

	/* Walk the completion queue collecting the heads reported by NIC */
	while (likely(packets < budget)) {
		struct sk_buff *skb = ERR_PTR(-EINVAL);
		u32 pkt_bytes;
		u64 rcd;

		if ((*raw_rcd & cpu_to_le64(FBNIC_RCD_DONE)) == done)
			break;

		dma_rmb();

		rcd = le64_to_cpu(*raw_rcd);

		switch (FIELD_GET(FBNIC_RCD_TYPE_MASK, rcd)) {
		case FBNIC_RCD_TYPE_HDR_AL:
			head0 = FIELD_GET(FBNIC_RCD_AL_BUFF_PAGE_MASK, rcd);
			fbnic_pkt_prepare(nv, rcd, pkt, qt);

			break;
		case FBNIC_RCD_TYPE_PAY_AL:
			head1 = FIELD_GET(FBNIC_RCD_AL_BUFF_PAGE_MASK, rcd);
			fbnic_add_rx_frag(nv, rcd, pkt, qt);

			break;
		case FBNIC_RCD_TYPE_OPT_META:
			/* Only type 0 is currently supported */
			if (FIELD_GET(FBNIC_RCD_OPT_META_TYPE_MASK, rcd))
				break;

			fbnic_rx_tstamp(nv, rcd, pkt);

			/* We currently ignore the action table index */
			break;
		case FBNIC_RCD_TYPE_META:
			if (likely(!fbnic_rcd_metadata_err(rcd) &&
				   !pkt->add_frag_failed)) {
				pkt_bytes = xdp_get_buff_len(&pkt->buff);
				skb = fbnic_run_xdp(nv, pkt);
			}

			/* Populate skb and invalidate XDP */
			if (!IS_ERR_OR_NULL(skb)) {
				fbnic_populate_skb_fields(nv, rcd, skb, qt,
							  &csum_complete,
							  &csum_none);
				napi_gro_receive(&nv->napi, skb);
			} else if (skb == ERR_PTR(-FBNIC_XDP_TX)) {
				pkt_tail = nv->qt[0].sub1.tail;
			} else if (PTR_ERR(skb) == -FBNIC_XDP_CONSUME) {
				fbnic_put_pkt_buff(qt, pkt, 1);
			} else {
				if (!skb)
					alloc_failed++;

				if (skb == ERR_PTR(-FBNIC_XDP_LEN_ERR))
					length_errors++;
				else
					dropped++;

				fbnic_put_pkt_buff(qt, pkt, 1);
				goto next_dont_count;
			}

			packets++;
			bytes += pkt_bytes;
next_dont_count:
			pkt->buff.data_hard_start = NULL;

			break;
		}

		raw_rcd++;
		head++;
		if (!(head & rcq->size_mask)) {
			done ^= cpu_to_le64(FBNIC_RCD_DONE);
			raw_rcd = &rcq->desc[0];
		}
	}

	u64_stats_update_begin(&rcq->stats.syncp);
	rcq->stats.packets += packets;
	rcq->stats.bytes += bytes;
	rcq->stats.dropped += dropped;
	rcq->stats.rx.alloc_failed += alloc_failed;
	rcq->stats.rx.csum_complete += csum_complete;
	rcq->stats.rx.csum_none += csum_none;
	rcq->stats.rx.length_errors += length_errors;
	u64_stats_update_end(&rcq->stats.syncp);

	if (pkt_tail >= 0)
		fbnic_pkt_commit_tail(nv, pkt_tail);

	/* Unmap and free processed buffers */
	if (head0 >= 0)
		fbnic_clean_bdq(&qt->sub0, head0, budget);
	fbnic_fill_bdq(&qt->sub0);

	if (head1 >= 0)
		fbnic_clean_bdq(&qt->sub1, head1, budget);
	fbnic_fill_bdq(&qt->sub1);

	/* Record the current head/tail of the queue */
	if (rcq->head != head) {
		rcq->head = head;
		writel(head & rcq->size_mask, rcq->doorbell);
	}

	return packets;
}

static void fbnic_nv_irq_disable(struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	u32 v_idx = nv->v_idx;

	fbnic_wr32(fbd, FBNIC_INTR_MASK_SET(v_idx / 32), 1 << (v_idx % 32));
}

static void fbnic_nv_irq_rearm(struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	u32 v_idx = nv->v_idx;

	fbnic_wr32(fbd, FBNIC_INTR_CQ_REARM(v_idx),
		   FBNIC_INTR_CQ_REARM_INTR_UNMASK);
}

static int fbnic_poll(struct napi_struct *napi, int budget)
{
	struct fbnic_napi_vector *nv = container_of(napi,
						    struct fbnic_napi_vector,
						    napi);
	int i, j, work_done = 0;

	for (i = 0; i < nv->txt_count; i++)
		fbnic_clean_tcq(nv, &nv->qt[i], budget);

	for (j = 0; j < nv->rxt_count; j++, i++)
		work_done += fbnic_clean_rcq(nv, &nv->qt[i], budget);

	if (work_done >= budget)
		return budget;

	if (likely(napi_complete_done(napi, work_done)))
		fbnic_nv_irq_rearm(nv);

	return work_done;
}

irqreturn_t fbnic_msix_clean_rings(int __always_unused irq, void *data)
{
	struct fbnic_napi_vector *nv = *(void **)data;

	napi_schedule_irqoff(&nv->napi);

	return IRQ_HANDLED;
}

void fbnic_aggregate_ring_rx_counters(struct fbnic_net *fbn,
				      struct fbnic_ring *rxr)
{
	struct fbnic_queue_stats *stats = &rxr->stats;

	/* Capture stats from queues before dissasociating them */
	fbn->rx_stats.bytes += stats->bytes;
	fbn->rx_stats.packets += stats->packets;
	fbn->rx_stats.dropped += stats->dropped;
	fbn->rx_stats.rx.alloc_failed += stats->rx.alloc_failed;
	fbn->rx_stats.rx.csum_complete += stats->rx.csum_complete;
	fbn->rx_stats.rx.csum_none += stats->rx.csum_none;
	fbn->rx_stats.rx.length_errors += stats->rx.length_errors;
	/* Remember to add new stats here */
	BUILD_BUG_ON(sizeof(fbn->rx_stats.rx) / 8 != 4);
}

void fbnic_aggregate_ring_bdq_counters(struct fbnic_net *fbn,
				       struct fbnic_ring *bdq)
{
	struct fbnic_queue_stats *stats = &bdq->stats;

	/* Capture stats from queues before dissasociating them */
	fbn->bdq_stats.bdq.alloc_failed += stats->bdq.alloc_failed;
	/* Remember to add new stats here */
	BUILD_BUG_ON(sizeof(fbn->rx_stats.bdq) / 8 != 1);
}

void fbnic_aggregate_ring_tx_counters(struct fbnic_net *fbn,
				      struct fbnic_ring *txr)
{
	struct fbnic_queue_stats *stats = &txr->stats;

	/* Capture stats from queues before dissasociating them */
	fbn->tx_stats.bytes += stats->bytes;
	fbn->tx_stats.packets += stats->packets;
	fbn->tx_stats.dropped += stats->dropped;
	fbn->tx_stats.twq.csum_partial += stats->twq.csum_partial;
	fbn->tx_stats.twq.lso += stats->twq.lso;
	fbn->tx_stats.twq.ts_lost += stats->twq.ts_lost;
	fbn->tx_stats.twq.ts_packets += stats->twq.ts_packets;
	fbn->tx_stats.twq.stop += stats->twq.stop;
	fbn->tx_stats.twq.wake += stats->twq.wake;
	/* Remember to add new stats here */
	BUILD_BUG_ON(sizeof(fbn->tx_stats.twq) / 8 != 6);
}

void fbnic_aggregate_ring_xdp_counters(struct fbnic_net *fbn,
				       struct fbnic_ring *xdpr)
{
	struct fbnic_queue_stats *stats = &xdpr->stats;

	if (!(xdpr->flags & FBNIC_RING_F_STATS))
		return;

	/* Capture stats from queues before dissasociating them */
	fbn->tx_stats.dropped += stats->dropped;
	fbn->tx_stats.bytes += stats->bytes;
	fbn->tx_stats.packets += stats->packets;
}

static void fbnic_remove_tx_ring(struct fbnic_net *fbn,
				 struct fbnic_ring *txr)
{
	if (!(txr->flags & FBNIC_RING_F_STATS))
		return;

	fbnic_aggregate_ring_tx_counters(fbn, txr);

	/* Remove pointer to the Tx ring */
	WARN_ON(fbn->tx[txr->q_idx] && fbn->tx[txr->q_idx] != txr);
	fbn->tx[txr->q_idx] = NULL;
}

static void fbnic_remove_xdp_ring(struct fbnic_net *fbn,
				  struct fbnic_ring *xdpr)
{
	if (!(xdpr->flags & FBNIC_RING_F_STATS))
		return;

	fbnic_aggregate_ring_xdp_counters(fbn, xdpr);

	/* Remove pointer to the Tx ring */
	WARN_ON(fbn->tx[xdpr->q_idx] && fbn->tx[xdpr->q_idx] != xdpr);
	fbn->tx[xdpr->q_idx] = NULL;
}

static void fbnic_remove_rx_ring(struct fbnic_net *fbn,
				 struct fbnic_ring *rxr)
{
	if (!(rxr->flags & FBNIC_RING_F_STATS))
		return;

	fbnic_aggregate_ring_rx_counters(fbn, rxr);

	/* Remove pointer to the Rx ring */
	WARN_ON(fbn->rx[rxr->q_idx] && fbn->rx[rxr->q_idx] != rxr);
	fbn->rx[rxr->q_idx] = NULL;
}

static void fbnic_remove_bdq_ring(struct fbnic_net *fbn,
				  struct fbnic_ring *bdq)
{
	if (!(bdq->flags & FBNIC_RING_F_STATS))
		return;

	fbnic_aggregate_ring_bdq_counters(fbn, bdq);
}

static void fbnic_free_qt_page_pools(struct fbnic_q_triad *qt)
{
	page_pool_destroy(qt->sub0.page_pool);
	page_pool_destroy(qt->sub1.page_pool);
}

static void fbnic_free_napi_vector(struct fbnic_net *fbn,
				   struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	int i, j;

	for (i = 0; i < nv->txt_count; i++) {
		fbnic_remove_tx_ring(fbn, &nv->qt[i].sub0);
		fbnic_remove_xdp_ring(fbn, &nv->qt[i].sub1);
		fbnic_remove_tx_ring(fbn, &nv->qt[i].cmpl);
	}

	for (j = 0; j < nv->rxt_count; j++, i++) {
		fbnic_remove_bdq_ring(fbn, &nv->qt[i].sub0);
		fbnic_remove_bdq_ring(fbn, &nv->qt[i].sub1);
		fbnic_remove_rx_ring(fbn, &nv->qt[i].cmpl);
	}

	fbnic_napi_free_irq(fbd, nv);
	netif_napi_del_locked(&nv->napi);
	fbn->napi[fbnic_napi_idx(nv)] = NULL;
	kfree(nv);
}

void fbnic_free_napi_vectors(struct fbnic_net *fbn)
{
	int i;

	for (i = 0; i < fbn->num_napi; i++)
		if (fbn->napi[i])
			fbnic_free_napi_vector(fbn, fbn->napi[i]);
}

static int
fbnic_alloc_qt_page_pools(struct fbnic_net *fbn, struct fbnic_q_triad *qt,
			  unsigned int rxq_idx)
{
	struct page_pool_params pp_params = {
		.order = 0,
		.flags = PP_FLAG_DMA_MAP |
			 PP_FLAG_DMA_SYNC_DEV,
		.pool_size = fbn->hpq_size + fbn->ppq_size,
		.nid = NUMA_NO_NODE,
		.dev = fbn->netdev->dev.parent,
		.dma_dir = DMA_BIDIRECTIONAL,
		.offset = 0,
		.max_len = PAGE_SIZE,
		.netdev	= fbn->netdev,
		.queue_idx = rxq_idx,
	};
	struct page_pool *pp;

	/* Page pool cannot exceed a size of 32768. This doesn't limit the
	 * pages on the ring but the number we can have cached waiting on
	 * the next use.
	 *
	 * TBD: Can this be reduced further? Would a multiple of
	 * NAPI_POLL_WEIGHT possibly make more sense? The question is how
	 * may pages do we need to hold in reserve to get the best return
	 * without hogging too much system memory.
	 */
	if (pp_params.pool_size > 32768)
		pp_params.pool_size = 32768;

	pp = page_pool_create(&pp_params);
	if (IS_ERR(pp))
		return PTR_ERR(pp);

	qt->sub0.page_pool = pp;
	if (netif_rxq_has_unreadable_mp(fbn->netdev, rxq_idx)) {
		pp_params.flags |= PP_FLAG_ALLOW_UNREADABLE_NETMEM;
		pp_params.dma_dir = DMA_FROM_DEVICE;

		pp = page_pool_create(&pp_params);
		if (IS_ERR(pp))
			goto err_destroy_sub0;
	} else {
		page_pool_get(pp);
	}
	qt->sub1.page_pool = pp;

	return 0;

err_destroy_sub0:
	page_pool_destroy(pp);
	return PTR_ERR(pp);
}

static void fbnic_ring_init(struct fbnic_ring *ring, u32 __iomem *doorbell,
			    int q_idx, u8 flags)
{
	u64_stats_init(&ring->stats.syncp);
	ring->doorbell = doorbell;
	ring->q_idx = q_idx;
	ring->flags = flags;
	ring->deferred_head = -1;
}

static int fbnic_alloc_napi_vector(struct fbnic_dev *fbd, struct fbnic_net *fbn,
				   unsigned int v_count, unsigned int v_idx,
				   unsigned int txq_count, unsigned int txq_idx,
				   unsigned int rxq_count, unsigned int rxq_idx)
{
	int txt_count = txq_count, rxt_count = rxq_count;
	u32 __iomem *uc_addr = fbd->uc_addr0;
	int xdp_count = 0, qt_count, err;
	struct fbnic_napi_vector *nv;
	struct fbnic_q_triad *qt;
	u32 __iomem *db;

	/* We need to reserve at least one Tx Queue Triad for an XDP ring */
	if (rxq_count) {
		xdp_count = 1;
		if (!txt_count)
			txt_count = 1;
	}

	qt_count = txt_count + rxq_count;
	if (!qt_count)
		return -EINVAL;

	/* If MMIO has already failed there are no rings to initialize */
	if (!uc_addr)
		return -EIO;

	/* Allocate NAPI vector and queue triads */
	nv = kzalloc(struct_size(nv, qt, qt_count), GFP_KERNEL);
	if (!nv)
		return -ENOMEM;

	/* Record queue triad counts */
	nv->txt_count = txt_count;
	nv->rxt_count = rxt_count;

	/* Provide pointer back to fbnic and MSI-X vectors */
	nv->fbd = fbd;
	nv->v_idx = v_idx;

	/* Tie napi to netdev */
	fbn->napi[fbnic_napi_idx(nv)] = nv;
	netif_napi_add_config_locked(fbn->netdev, &nv->napi, fbnic_poll,
				     fbnic_napi_idx(nv));

	/* Record IRQ to NAPI struct */
	netif_napi_set_irq_locked(&nv->napi,
				  pci_irq_vector(to_pci_dev(fbd->dev),
						 nv->v_idx));

	/* Tie nv back to PCIe dev */
	nv->dev = fbd->dev;

	/* Request the IRQ for napi vector */
	err = fbnic_napi_request_irq(fbd, nv);
	if (err)
		goto napi_del;

	/* Initialize queue triads */
	qt = nv->qt;

	while (txt_count) {
		u8 flags = FBNIC_RING_F_CTX | FBNIC_RING_F_STATS;

		/* Configure Tx queue */
		db = &uc_addr[FBNIC_QUEUE(txq_idx) + FBNIC_QUEUE_TWQ0_TAIL];

		/* Assign Tx queue to netdev if applicable */
		if (txq_count > 0) {

			fbnic_ring_init(&qt->sub0, db, txq_idx, flags);
			fbn->tx[txq_idx] = &qt->sub0;
			txq_count--;
		} else {
			fbnic_ring_init(&qt->sub0, db, 0,
					FBNIC_RING_F_DISABLED);
		}

		/* Configure XDP queue */
		db = &uc_addr[FBNIC_QUEUE(txq_idx) + FBNIC_QUEUE_TWQ1_TAIL];

		/* Assign XDP queue to netdev if applicable
		 *
		 * The setup for this is in itself a bit different.
		 * 1. We only need one XDP Tx queue per NAPI vector.
		 * 2. We associate it to the first Rx queue index.
		 * 3. The hardware side is associated based on the Tx Queue.
		 * 4. The netdev queue is offset by FBNIC_MAX_TXQs.
		 */
		if (xdp_count > 0) {
			unsigned int xdp_idx = FBNIC_MAX_TXQS + rxq_idx;

			fbnic_ring_init(&qt->sub1, db, xdp_idx, flags);
			fbn->tx[xdp_idx] = &qt->sub1;
			xdp_count--;
		} else {
			fbnic_ring_init(&qt->sub1, db, 0,
					FBNIC_RING_F_DISABLED);
		}

		/* Configure Tx completion queue */
		db = &uc_addr[FBNIC_QUEUE(txq_idx) + FBNIC_QUEUE_TCQ_HEAD];
		fbnic_ring_init(&qt->cmpl, db, 0, 0);

		/* Update Tx queue index */
		txt_count--;
		txq_idx += v_count;

		/* Move to next queue triad */
		qt++;
	}

	while (rxt_count) {
		/* Configure header queue */
		db = &uc_addr[FBNIC_QUEUE(rxq_idx) + FBNIC_QUEUE_BDQ_HPQ_TAIL];
		fbnic_ring_init(&qt->sub0, db, 0,
				FBNIC_RING_F_CTX | FBNIC_RING_F_STATS);

		/* Configure payload queue */
		db = &uc_addr[FBNIC_QUEUE(rxq_idx) + FBNIC_QUEUE_BDQ_PPQ_TAIL];
		fbnic_ring_init(&qt->sub1, db, 0,
				FBNIC_RING_F_CTX | FBNIC_RING_F_STATS);

		/* Configure Rx completion queue */
		db = &uc_addr[FBNIC_QUEUE(rxq_idx) + FBNIC_QUEUE_RCQ_HEAD];
		fbnic_ring_init(&qt->cmpl, db, rxq_idx, FBNIC_RING_F_STATS);
		fbn->rx[rxq_idx] = &qt->cmpl;

		/* Update Rx queue index */
		rxt_count--;
		rxq_idx += v_count;

		/* Move to next queue triad */
		qt++;
	}

	return 0;

napi_del:
	netif_napi_del_locked(&nv->napi);
	fbn->napi[fbnic_napi_idx(nv)] = NULL;
	kfree(nv);
	return err;
}

int fbnic_alloc_napi_vectors(struct fbnic_net *fbn)
{
	unsigned int txq_idx = 0, rxq_idx = 0, v_idx = FBNIC_NON_NAPI_VECTORS;
	unsigned int num_tx = fbn->num_tx_queues;
	unsigned int num_rx = fbn->num_rx_queues;
	unsigned int num_napi = fbn->num_napi;
	struct fbnic_dev *fbd = fbn->fbd;
	int err;

	/* Allocate 1 Tx queue per napi vector */
	if (num_napi < FBNIC_MAX_TXQS && num_napi == num_tx + num_rx) {
		while (num_tx) {
			err = fbnic_alloc_napi_vector(fbd, fbn,
						      num_napi, v_idx,
						      1, txq_idx, 0, 0);
			if (err)
				goto free_vectors;

			/* Update counts and index */
			num_tx--;
			txq_idx++;

			v_idx++;
		}
	}

	/* Allocate Tx/Rx queue pairs per vector, or allocate remaining Rx */
	while (num_rx | num_tx) {
		int tqpv = DIV_ROUND_UP(num_tx, num_napi - txq_idx);
		int rqpv = DIV_ROUND_UP(num_rx, num_napi - rxq_idx);

		err = fbnic_alloc_napi_vector(fbd, fbn, num_napi, v_idx,
					      tqpv, txq_idx, rqpv, rxq_idx);
		if (err)
			goto free_vectors;

		/* Update counts and index */
		num_tx -= tqpv;
		txq_idx++;

		num_rx -= rqpv;
		rxq_idx++;

		v_idx++;
	}

	return 0;

free_vectors:
	fbnic_free_napi_vectors(fbn);

	return -ENOMEM;
}

static void fbnic_free_ring_resources(struct device *dev,
				      struct fbnic_ring *ring)
{
	kvfree(ring->buffer);
	ring->buffer = NULL;

	/* If size is not set there are no descriptors present */
	if (!ring->size)
		return;

	dma_free_coherent(dev, ring->size, ring->desc, ring->dma);
	ring->size_mask = 0;
	ring->size = 0;
}

static int fbnic_alloc_tx_ring_desc(struct fbnic_net *fbn,
				    struct fbnic_ring *txr)
{
	struct device *dev = fbn->netdev->dev.parent;
	size_t size;

	/* Round size up to nearest 4K */
	size = ALIGN(array_size(sizeof(*txr->desc), fbn->txq_size), 4096);

	txr->desc = dma_alloc_coherent(dev, size, &txr->dma,
				       GFP_KERNEL | __GFP_NOWARN);
	if (!txr->desc)
		return -ENOMEM;

	/* txq_size should be a power of 2, so mask is just that -1 */
	txr->size_mask = fbn->txq_size - 1;
	txr->size = size;

	return 0;
}

static int fbnic_alloc_tx_ring_buffer(struct fbnic_ring *txr)
{
	size_t size = array_size(sizeof(*txr->tx_buf), txr->size_mask + 1);

	txr->tx_buf = kvzalloc(size, GFP_KERNEL | __GFP_NOWARN);

	return txr->tx_buf ? 0 : -ENOMEM;
}

static int fbnic_alloc_tx_ring_resources(struct fbnic_net *fbn,
					 struct fbnic_ring *txr)
{
	struct device *dev = fbn->netdev->dev.parent;
	int err;

	if (txr->flags & FBNIC_RING_F_DISABLED)
		return 0;

	err = fbnic_alloc_tx_ring_desc(fbn, txr);
	if (err)
		return err;

	if (!(txr->flags & FBNIC_RING_F_CTX))
		return 0;

	err = fbnic_alloc_tx_ring_buffer(txr);
	if (err)
		goto free_desc;

	return 0;

free_desc:
	fbnic_free_ring_resources(dev, txr);
	return err;
}

static int fbnic_alloc_rx_ring_desc(struct fbnic_net *fbn,
				    struct fbnic_ring *rxr)
{
	struct device *dev = fbn->netdev->dev.parent;
	size_t desc_size = sizeof(*rxr->desc);
	u32 rxq_size;
	size_t size;

	switch (rxr->doorbell - fbnic_ring_csr_base(rxr)) {
	case FBNIC_QUEUE_BDQ_HPQ_TAIL:
		rxq_size = fbn->hpq_size / FBNIC_BD_FRAG_COUNT;
		desc_size *= FBNIC_BD_FRAG_COUNT;
		break;
	case FBNIC_QUEUE_BDQ_PPQ_TAIL:
		rxq_size = fbn->ppq_size / FBNIC_BD_FRAG_COUNT;
		desc_size *= FBNIC_BD_FRAG_COUNT;
		break;
	case FBNIC_QUEUE_RCQ_HEAD:
		rxq_size = fbn->rcq_size;
		break;
	default:
		return -EINVAL;
	}

	/* Round size up to nearest 4K */
	size = ALIGN(array_size(desc_size, rxq_size), 4096);

	rxr->desc = dma_alloc_coherent(dev, size, &rxr->dma,
				       GFP_KERNEL | __GFP_NOWARN);
	if (!rxr->desc)
		return -ENOMEM;

	/* rxq_size should be a power of 2, so mask is just that -1 */
	rxr->size_mask = rxq_size - 1;
	rxr->size = size;

	return 0;
}

static int fbnic_alloc_rx_ring_buffer(struct fbnic_ring *rxr)
{
	size_t size = array_size(sizeof(*rxr->rx_buf), rxr->size_mask + 1);

	if (rxr->flags & FBNIC_RING_F_CTX)
		size = sizeof(*rxr->rx_buf) * (rxr->size_mask + 1);
	else
		size = sizeof(*rxr->pkt);

	rxr->rx_buf = kvzalloc(size, GFP_KERNEL | __GFP_NOWARN);

	return rxr->rx_buf ? 0 : -ENOMEM;
}

static int fbnic_alloc_rx_ring_resources(struct fbnic_net *fbn,
					 struct fbnic_ring *rxr)
{
	struct device *dev = fbn->netdev->dev.parent;
	int err;

	err = fbnic_alloc_rx_ring_desc(fbn, rxr);
	if (err)
		return err;

	err = fbnic_alloc_rx_ring_buffer(rxr);
	if (err)
		goto free_desc;

	return 0;

free_desc:
	fbnic_free_ring_resources(dev, rxr);
	return err;
}

static void fbnic_free_qt_resources(struct fbnic_net *fbn,
				    struct fbnic_q_triad *qt)
{
	struct device *dev = fbn->netdev->dev.parent;

	fbnic_free_ring_resources(dev, &qt->cmpl);
	fbnic_free_ring_resources(dev, &qt->sub1);
	fbnic_free_ring_resources(dev, &qt->sub0);

	if (xdp_rxq_info_is_reg(&qt->xdp_rxq)) {
		xdp_rxq_info_unreg_mem_model(&qt->xdp_rxq);
		xdp_rxq_info_unreg(&qt->xdp_rxq);
		fbnic_free_qt_page_pools(qt);
	}
}

static int fbnic_alloc_tx_qt_resources(struct fbnic_net *fbn,
				       struct fbnic_q_triad *qt)
{
	struct device *dev = fbn->netdev->dev.parent;
	int err;

	err = fbnic_alloc_tx_ring_resources(fbn, &qt->sub0);
	if (err)
		return err;

	err = fbnic_alloc_tx_ring_resources(fbn, &qt->sub1);
	if (err)
		goto free_sub0;

	err = fbnic_alloc_tx_ring_resources(fbn, &qt->cmpl);
	if (err)
		goto free_sub1;

	return 0;

free_sub1:
	fbnic_free_ring_resources(dev, &qt->sub1);
free_sub0:
	fbnic_free_ring_resources(dev, &qt->sub0);
	return err;
}

static int fbnic_alloc_rx_qt_resources(struct fbnic_net *fbn,
				       struct fbnic_napi_vector *nv,
				       struct fbnic_q_triad *qt)
{
	struct device *dev = fbn->netdev->dev.parent;
	int err;

	err = fbnic_alloc_qt_page_pools(fbn, qt, qt->cmpl.q_idx);
	if (err)
		return err;

	err = xdp_rxq_info_reg(&qt->xdp_rxq, fbn->netdev, qt->sub0.q_idx,
			       nv->napi.napi_id);
	if (err)
		goto free_page_pools;

	err = xdp_rxq_info_reg_mem_model(&qt->xdp_rxq, MEM_TYPE_PAGE_POOL,
					 qt->sub0.page_pool);
	if (err)
		goto unreg_rxq;

	err = fbnic_alloc_rx_ring_resources(fbn, &qt->sub0);
	if (err)
		goto unreg_mm;

	err = fbnic_alloc_rx_ring_resources(fbn, &qt->sub1);
	if (err)
		goto free_sub0;

	err = fbnic_alloc_rx_ring_resources(fbn, &qt->cmpl);
	if (err)
		goto free_sub1;

	return 0;

free_sub1:
	fbnic_free_ring_resources(dev, &qt->sub1);
free_sub0:
	fbnic_free_ring_resources(dev, &qt->sub0);
unreg_mm:
	xdp_rxq_info_unreg_mem_model(&qt->xdp_rxq);
unreg_rxq:
	xdp_rxq_info_unreg(&qt->xdp_rxq);
free_page_pools:
	fbnic_free_qt_page_pools(qt);
	return err;
}

static void fbnic_free_nv_resources(struct fbnic_net *fbn,
				    struct fbnic_napi_vector *nv)
{
	int i;

	for (i = 0; i < nv->txt_count + nv->rxt_count; i++)
		fbnic_free_qt_resources(fbn, &nv->qt[i]);
}

static int fbnic_alloc_nv_resources(struct fbnic_net *fbn,
				    struct fbnic_napi_vector *nv)
{
	int i, j, err;

	/* Allocate Tx Resources */
	for (i = 0; i < nv->txt_count; i++) {
		err = fbnic_alloc_tx_qt_resources(fbn, &nv->qt[i]);
		if (err)
			goto free_qt_resources;
	}

	/* Allocate Rx Resources */
	for (j = 0; j < nv->rxt_count; j++, i++) {
		err = fbnic_alloc_rx_qt_resources(fbn, nv, &nv->qt[i]);
		if (err)
			goto free_qt_resources;
	}

	return 0;

free_qt_resources:
	while (i--)
		fbnic_free_qt_resources(fbn, &nv->qt[i]);
	return err;
}

void fbnic_free_resources(struct fbnic_net *fbn)
{
	int i;

	for (i = 0; i < fbn->num_napi; i++)
		fbnic_free_nv_resources(fbn, fbn->napi[i]);
}

int fbnic_alloc_resources(struct fbnic_net *fbn)
{
	int i, err = -ENODEV;

	for (i = 0; i < fbn->num_napi; i++) {
		err = fbnic_alloc_nv_resources(fbn, fbn->napi[i]);
		if (err)
			goto free_resources;
	}

	return 0;

free_resources:
	while (i--)
		fbnic_free_nv_resources(fbn, fbn->napi[i]);

	return err;
}

static void fbnic_set_netif_napi(struct fbnic_napi_vector *nv)
{
	int i, j;

	/* Associate Tx queue with NAPI */
	for (i = 0; i < nv->txt_count; i++) {
		struct fbnic_q_triad *qt = &nv->qt[i];

		netif_queue_set_napi(nv->napi.dev, qt->sub0.q_idx,
				     NETDEV_QUEUE_TYPE_TX, &nv->napi);
	}

	/* Associate Rx queue with NAPI */
	for (j = 0; j < nv->rxt_count; j++, i++) {
		struct fbnic_q_triad *qt = &nv->qt[i];

		netif_queue_set_napi(nv->napi.dev, qt->cmpl.q_idx,
				     NETDEV_QUEUE_TYPE_RX, &nv->napi);
	}
}

static void fbnic_reset_netif_napi(struct fbnic_napi_vector *nv)
{
	int i, j;

	/* Disassociate Tx queue from NAPI */
	for (i = 0; i < nv->txt_count; i++) {
		struct fbnic_q_triad *qt = &nv->qt[i];

		netif_queue_set_napi(nv->napi.dev, qt->sub0.q_idx,
				     NETDEV_QUEUE_TYPE_TX, NULL);
	}

	/* Disassociate Rx queue from NAPI */
	for (j = 0; j < nv->rxt_count; j++, i++) {
		struct fbnic_q_triad *qt = &nv->qt[i];

		netif_queue_set_napi(nv->napi.dev, qt->cmpl.q_idx,
				     NETDEV_QUEUE_TYPE_RX, NULL);
	}
}

int fbnic_set_netif_queues(struct fbnic_net *fbn)
{
	int i, err;

	err = netif_set_real_num_queues(fbn->netdev, fbn->num_tx_queues,
					fbn->num_rx_queues);
	if (err)
		return err;

	for (i = 0; i < fbn->num_napi; i++)
		fbnic_set_netif_napi(fbn->napi[i]);

	return 0;
}

void fbnic_reset_netif_queues(struct fbnic_net *fbn)
{
	int i;

	for (i = 0; i < fbn->num_napi; i++)
		fbnic_reset_netif_napi(fbn->napi[i]);
}

static void fbnic_disable_twq0(struct fbnic_ring *txr)
{
	u32 twq_ctl = fbnic_ring_rd32(txr, FBNIC_QUEUE_TWQ0_CTL);

	twq_ctl &= ~FBNIC_QUEUE_TWQ_CTL_ENABLE;

	fbnic_ring_wr32(txr, FBNIC_QUEUE_TWQ0_CTL, twq_ctl);
}

static void fbnic_disable_twq1(struct fbnic_ring *txr)
{
	u32 twq_ctl = fbnic_ring_rd32(txr, FBNIC_QUEUE_TWQ1_CTL);

	twq_ctl &= ~FBNIC_QUEUE_TWQ_CTL_ENABLE;

	fbnic_ring_wr32(txr, FBNIC_QUEUE_TWQ1_CTL, twq_ctl);
}

static void fbnic_disable_tcq(struct fbnic_ring *txr)
{
	fbnic_ring_wr32(txr, FBNIC_QUEUE_TCQ_CTL, 0);
	fbnic_ring_wr32(txr, FBNIC_QUEUE_TIM_MASK, FBNIC_QUEUE_TIM_MASK_MASK);
}

static void fbnic_disable_bdq(struct fbnic_ring *hpq, struct fbnic_ring *ppq)
{
	u32 bdq_ctl = fbnic_ring_rd32(hpq, FBNIC_QUEUE_BDQ_CTL);

	bdq_ctl &= ~FBNIC_QUEUE_BDQ_CTL_ENABLE;

	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_CTL, bdq_ctl);
}

static void fbnic_disable_rcq(struct fbnic_ring *rxr)
{
	fbnic_ring_wr32(rxr, FBNIC_QUEUE_RCQ_CTL, 0);
	fbnic_ring_wr32(rxr, FBNIC_QUEUE_RIM_MASK, FBNIC_QUEUE_RIM_MASK_MASK);
}

void fbnic_napi_disable(struct fbnic_net *fbn)
{
	int i;

	for (i = 0; i < fbn->num_napi; i++) {
		napi_disable_locked(&fbn->napi[i]->napi);

		fbnic_nv_irq_disable(fbn->napi[i]);
	}
}

static void __fbnic_nv_disable(struct fbnic_napi_vector *nv)
{
	int i, t;

	/* Disable Tx queue triads */
	for (t = 0; t < nv->txt_count; t++) {
		struct fbnic_q_triad *qt = &nv->qt[t];

		fbnic_disable_twq0(&qt->sub0);
		fbnic_disable_twq1(&qt->sub1);
		fbnic_disable_tcq(&qt->cmpl);
	}

	/* Disable Rx queue triads */
	for (i = 0; i < nv->rxt_count; i++, t++) {
		struct fbnic_q_triad *qt = &nv->qt[t];

		fbnic_disable_bdq(&qt->sub0, &qt->sub1);
		fbnic_disable_rcq(&qt->cmpl);
	}
}

static void
fbnic_nv_disable(struct fbnic_net *fbn, struct fbnic_napi_vector *nv)
{
	__fbnic_nv_disable(nv);
	fbnic_wrfl(fbn->fbd);
}

void fbnic_disable(struct fbnic_net *fbn)
{
	struct fbnic_dev *fbd = fbn->fbd;
	int i;

	for (i = 0; i < fbn->num_napi; i++)
		__fbnic_nv_disable(fbn->napi[i]);

	fbnic_wrfl(fbd);
}

static void fbnic_tx_flush(struct fbnic_dev *fbd)
{
	netdev_warn(fbd->netdev, "triggering Tx flush\n");

	fbnic_rmw32(fbd, FBNIC_TMI_DROP_CTRL, FBNIC_TMI_DROP_CTRL_EN,
		    FBNIC_TMI_DROP_CTRL_EN);
}

static void fbnic_tx_flush_off(struct fbnic_dev *fbd)
{
	fbnic_rmw32(fbd, FBNIC_TMI_DROP_CTRL, FBNIC_TMI_DROP_CTRL_EN, 0);
}

struct fbnic_idle_regs {
	u32 reg_base;
	u8 reg_cnt;
};

static bool fbnic_all_idle(struct fbnic_dev *fbd,
			   const struct fbnic_idle_regs *regs,
			   unsigned int nregs)
{
	unsigned int i, j;

	for (i = 0; i < nregs; i++) {
		for (j = 0; j < regs[i].reg_cnt; j++) {
			if (fbnic_rd32(fbd, regs[i].reg_base + j) != ~0U)
				return false;
		}
	}
	return true;
}

static void fbnic_idle_dump(struct fbnic_dev *fbd,
			    const struct fbnic_idle_regs *regs,
			    unsigned int nregs, const char *dir, int err)
{
	unsigned int i, j;

	netdev_err(fbd->netdev, "error waiting for %s idle %d\n", dir, err);
	for (i = 0; i < nregs; i++)
		for (j = 0; j < regs[i].reg_cnt; j++)
			netdev_err(fbd->netdev, "0x%04x: %08x\n",
				   regs[i].reg_base + j,
				   fbnic_rd32(fbd, regs[i].reg_base + j));
}

int fbnic_wait_all_queues_idle(struct fbnic_dev *fbd, bool may_fail)
{
	static const struct fbnic_idle_regs tx[] = {
		{ FBNIC_QM_TWQ_IDLE(0),	FBNIC_QM_TWQ_IDLE_CNT, },
		{ FBNIC_QM_TQS_IDLE(0),	FBNIC_QM_TQS_IDLE_CNT, },
		{ FBNIC_QM_TDE_IDLE(0),	FBNIC_QM_TDE_IDLE_CNT, },
		{ FBNIC_QM_TCQ_IDLE(0),	FBNIC_QM_TCQ_IDLE_CNT, },
	}, rx[] = {
		{ FBNIC_QM_HPQ_IDLE(0),	FBNIC_QM_HPQ_IDLE_CNT, },
		{ FBNIC_QM_PPQ_IDLE(0),	FBNIC_QM_PPQ_IDLE_CNT, },
		{ FBNIC_QM_RCQ_IDLE(0),	FBNIC_QM_RCQ_IDLE_CNT, },
	};
	bool idle;
	int err;

	err = read_poll_timeout_atomic(fbnic_all_idle, idle, idle, 2, 500000,
				       false, fbd, tx, ARRAY_SIZE(tx));
	if (err == -ETIMEDOUT) {
		fbnic_tx_flush(fbd);
		err = read_poll_timeout_atomic(fbnic_all_idle, idle, idle,
					       2, 500000, false,
					       fbd, tx, ARRAY_SIZE(tx));
		fbnic_tx_flush_off(fbd);
	}
	if (err) {
		fbnic_idle_dump(fbd, tx, ARRAY_SIZE(tx), "Tx", err);
		if (may_fail)
			return err;
	}

	err = read_poll_timeout_atomic(fbnic_all_idle, idle, idle, 2, 500000,
				       false, fbd, rx, ARRAY_SIZE(rx));
	if (err)
		fbnic_idle_dump(fbd, rx, ARRAY_SIZE(rx), "Rx", err);
	return err;
}

static int
fbnic_wait_queue_idle(struct fbnic_net *fbn, bool rx, unsigned int idx)
{
	static const unsigned int tx_regs[] = {
		FBNIC_QM_TWQ_IDLE(0), FBNIC_QM_TQS_IDLE(0),
		FBNIC_QM_TDE_IDLE(0), FBNIC_QM_TCQ_IDLE(0),
	}, rx_regs[] = {
		FBNIC_QM_HPQ_IDLE(0), FBNIC_QM_PPQ_IDLE(0),
		FBNIC_QM_RCQ_IDLE(0),
	};
	struct fbnic_dev *fbd = fbn->fbd;
	unsigned int val, mask, off;
	const unsigned int *regs;
	unsigned int reg_cnt;
	int i, err;

	regs = rx ? rx_regs : tx_regs;
	reg_cnt = rx ? ARRAY_SIZE(rx_regs) : ARRAY_SIZE(tx_regs);

	off = idx / 32;
	mask = BIT(idx % 32);

	for (i = 0; i < reg_cnt; i++) {
		err = read_poll_timeout_atomic(fbnic_rd32, val, val & mask,
					       2, 500000, false,
					       fbd, regs[i] + off);
		if (err) {
			netdev_err(fbd->netdev,
				   "wait for queue %s%d idle failed 0x%04x(%d): %08x (mask: %08x)\n",
				   rx ? "Rx" : "Tx", idx, regs[i] + off, i,
				   val, mask);
			return err;
		}
	}

	return 0;
}

static void fbnic_nv_flush(struct fbnic_napi_vector *nv)
{
	int j, t;

	/* Flush any processed Tx Queue Triads and drop the rest */
	for (t = 0; t < nv->txt_count; t++) {
		struct fbnic_q_triad *qt = &nv->qt[t];
		struct netdev_queue *tx_queue;

		/* Clean the work queues of unprocessed work */
		fbnic_clean_twq0(nv, 0, &qt->sub0, true, qt->sub0.tail);
		fbnic_clean_twq1(nv, false, &qt->sub1, true,
				 qt->sub1.tail);

		/* Reset completion queue descriptor ring */
		memset(qt->cmpl.desc, 0, qt->cmpl.size);

		/* Nothing else to do if Tx queue is disabled */
		if (qt->sub0.flags & FBNIC_RING_F_DISABLED)
			continue;

		/* Reset BQL associated with Tx queue */
		tx_queue = netdev_get_tx_queue(nv->napi.dev,
					       qt->sub0.q_idx);
		netdev_tx_reset_queue(tx_queue);
	}

	/* Flush any processed Rx Queue Triads and drop the rest */
	for (j = 0; j < nv->rxt_count; j++, t++) {
		struct fbnic_q_triad *qt = &nv->qt[t];

		/* Clean the work queues of unprocessed work */
		fbnic_clean_bdq(&qt->sub0, qt->sub0.tail, 0);
		fbnic_clean_bdq(&qt->sub1, qt->sub1.tail, 0);

		/* Reset completion queue descriptor ring */
		memset(qt->cmpl.desc, 0, qt->cmpl.size);

		fbnic_put_pkt_buff(qt, qt->cmpl.pkt, 0);
		memset(qt->cmpl.pkt, 0, sizeof(struct fbnic_pkt_buff));
	}
}

void fbnic_flush(struct fbnic_net *fbn)
{
	int i;

	for (i = 0; i < fbn->num_napi; i++)
		fbnic_nv_flush(fbn->napi[i]);
}

static void fbnic_nv_fill(struct fbnic_napi_vector *nv)
{
	int j, t;

	/* Configure NAPI mapping and populate pages
	 * in the BDQ rings to use for Rx
	 */
	for (j = 0, t = nv->txt_count; j < nv->rxt_count; j++, t++) {
		struct fbnic_q_triad *qt = &nv->qt[t];

		/* Populate the header and payload BDQs */
		fbnic_fill_bdq(&qt->sub0);
		fbnic_fill_bdq(&qt->sub1);
	}
}

void fbnic_fill(struct fbnic_net *fbn)
{
	int i;

	for (i = 0; i < fbn->num_napi; i++)
		fbnic_nv_fill(fbn->napi[i]);
}

static void fbnic_enable_twq0(struct fbnic_ring *twq)
{
	u32 log_size = fls(twq->size_mask);

	if (!twq->size_mask)
		return;

	/* Reset head/tail */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_CTL, FBNIC_QUEUE_TWQ_CTL_RESET);
	twq->tail = 0;
	twq->head = 0;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_BAL, lower_32_bits(twq->dma));
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_BAH, upper_32_bits(twq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_SIZE, log_size & 0xf);

	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_CTL, FBNIC_QUEUE_TWQ_CTL_ENABLE);
}

static void fbnic_enable_twq1(struct fbnic_ring *twq)
{
	u32 log_size = fls(twq->size_mask);

	if (!twq->size_mask)
		return;

	/* Reset head/tail */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ1_CTL, FBNIC_QUEUE_TWQ_CTL_RESET);
	twq->tail = 0;
	twq->head = 0;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ1_BAL, lower_32_bits(twq->dma));
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ1_BAH, upper_32_bits(twq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ1_SIZE, log_size & 0xf);

	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ1_CTL, FBNIC_QUEUE_TWQ_CTL_ENABLE);
}

static void fbnic_enable_tcq(struct fbnic_napi_vector *nv,
			     struct fbnic_ring *tcq)
{
	u32 log_size = fls(tcq->size_mask);

	if (!tcq->size_mask)
		return;

	/* Reset head/tail */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_CTL, FBNIC_QUEUE_TCQ_CTL_RESET);
	tcq->tail = 0;
	tcq->head = 0;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_BAL, lower_32_bits(tcq->dma));
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_BAH, upper_32_bits(tcq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_SIZE, log_size & 0xf);

	/* Store interrupt information for the completion queue */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TIM_CTL, nv->v_idx);
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TIM_THRESHOLD, tcq->size_mask / 2);
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TIM_MASK, 0);

	/* Enable queue */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_CTL, FBNIC_QUEUE_TCQ_CTL_ENABLE);
}

static void fbnic_enable_bdq(struct fbnic_ring *hpq, struct fbnic_ring *ppq)
{
	u32 bdq_ctl = FBNIC_QUEUE_BDQ_CTL_ENABLE;
	u32 log_size;

	/* Reset head/tail */
	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_CTL, FBNIC_QUEUE_BDQ_CTL_RESET);
	ppq->tail = 0;
	ppq->head = 0;
	hpq->tail = 0;
	hpq->head = 0;

	log_size = fls(hpq->size_mask);

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_HPQ_BAL, lower_32_bits(hpq->dma));
	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_HPQ_BAH, upper_32_bits(hpq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_HPQ_SIZE, log_size & 0xf);

	if (!ppq->size_mask)
		goto write_ctl;

	log_size = fls(ppq->size_mask);

	/* Add enabling of PPQ to BDQ control */
	bdq_ctl |= FBNIC_QUEUE_BDQ_CTL_PPQ_ENABLE;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(ppq, FBNIC_QUEUE_BDQ_PPQ_BAL, lower_32_bits(ppq->dma));
	fbnic_ring_wr32(ppq, FBNIC_QUEUE_BDQ_PPQ_BAH, upper_32_bits(ppq->dma));
	fbnic_ring_wr32(ppq, FBNIC_QUEUE_BDQ_PPQ_SIZE, log_size & 0xf);

write_ctl:
	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_CTL, bdq_ctl);
}

static void fbnic_config_drop_mode_rcq(struct fbnic_napi_vector *nv,
				       struct fbnic_ring *rcq)
{
	u32 drop_mode, rcq_ctl;

	drop_mode = FBNIC_QUEUE_RDE_CTL0_DROP_IMMEDIATE;

	/* Specify packet layout */
	rcq_ctl = FIELD_PREP(FBNIC_QUEUE_RDE_CTL0_DROP_MODE_MASK, drop_mode) |
	    FIELD_PREP(FBNIC_QUEUE_RDE_CTL0_MIN_HROOM_MASK, FBNIC_RX_HROOM) |
	    FIELD_PREP(FBNIC_QUEUE_RDE_CTL0_MIN_TROOM_MASK, FBNIC_RX_TROOM);

	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RDE_CTL0, rcq_ctl);
}

static void fbnic_config_rim_threshold(struct fbnic_ring *rcq, u16 nv_idx, u32 rx_desc)
{
	u32 threshold;

	/* Set the threhsold to half the ring size if rx_frames
	 * is not configured
	 */
	threshold = rx_desc ? : rcq->size_mask / 2;

	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RIM_CTL, nv_idx);
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RIM_THRESHOLD, threshold);
}

void fbnic_config_txrx_usecs(struct fbnic_napi_vector *nv, u32 arm)
{
	struct fbnic_net *fbn = netdev_priv(nv->napi.dev);
	struct fbnic_dev *fbd = nv->fbd;
	u32 val = arm;

	val |= FIELD_PREP(FBNIC_INTR_CQ_REARM_RCQ_TIMEOUT, fbn->rx_usecs) |
	       FBNIC_INTR_CQ_REARM_RCQ_TIMEOUT_UPD_EN;
	val |= FIELD_PREP(FBNIC_INTR_CQ_REARM_TCQ_TIMEOUT, fbn->tx_usecs) |
	       FBNIC_INTR_CQ_REARM_TCQ_TIMEOUT_UPD_EN;

	fbnic_wr32(fbd, FBNIC_INTR_CQ_REARM(nv->v_idx), val);
}

void fbnic_config_rx_frames(struct fbnic_napi_vector *nv)
{
	struct fbnic_net *fbn = netdev_priv(nv->napi.dev);
	int i;

	for (i = nv->txt_count; i < nv->rxt_count + nv->txt_count; i++) {
		struct fbnic_q_triad *qt = &nv->qt[i];

		fbnic_config_rim_threshold(&qt->cmpl, nv->v_idx,
					   fbn->rx_max_frames *
					   FBNIC_MIN_RXD_PER_FRAME);
	}
}

static void fbnic_enable_rcq(struct fbnic_napi_vector *nv,
			     struct fbnic_ring *rcq)
{
	struct fbnic_net *fbn = netdev_priv(nv->napi.dev);
	u32 log_size = fls(rcq->size_mask);
	u32 hds_thresh = fbn->hds_thresh;
	u32 rcq_ctl = 0;

	fbnic_config_drop_mode_rcq(nv, rcq);

	/* Force lower bound on MAX_HEADER_BYTES. Below this, all frames should
	 * be split at L4. It would also result in the frames being split at
	 * L2/L3 depending on the frame size.
	 */
	if (fbn->hds_thresh < FBNIC_HDR_BYTES_MIN) {
		rcq_ctl = FBNIC_QUEUE_RDE_CTL0_EN_HDR_SPLIT;
		hds_thresh = FBNIC_HDR_BYTES_MIN;
	}

	rcq_ctl |= FIELD_PREP(FBNIC_QUEUE_RDE_CTL1_PADLEN_MASK, FBNIC_RX_PAD) |
		   FIELD_PREP(FBNIC_QUEUE_RDE_CTL1_MAX_HDR_MASK, hds_thresh) |
		   FIELD_PREP(FBNIC_QUEUE_RDE_CTL1_PAYLD_OFF_MASK,
			      FBNIC_RX_PAYLD_OFFSET) |
		   FIELD_PREP(FBNIC_QUEUE_RDE_CTL1_PAYLD_PG_CL_MASK,
			      FBNIC_RX_PAYLD_PG_CL);
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RDE_CTL1, rcq_ctl);

	/* Reset head/tail */
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RCQ_CTL, FBNIC_QUEUE_RCQ_CTL_RESET);
	rcq->head = 0;
	rcq->tail = 0;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RCQ_BAL, lower_32_bits(rcq->dma));
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RCQ_BAH, upper_32_bits(rcq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RCQ_SIZE, log_size & 0xf);

	/* Store interrupt information for the completion queue */
	fbnic_config_rim_threshold(rcq, nv->v_idx, fbn->rx_max_frames *
						   FBNIC_MIN_RXD_PER_FRAME);
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RIM_MASK, 0);

	/* Enable queue */
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RCQ_CTL, FBNIC_QUEUE_RCQ_CTL_ENABLE);
}

static void __fbnic_nv_enable(struct fbnic_napi_vector *nv)
{
	int j, t;

	/* Setup Tx Queue Triads */
	for (t = 0; t < nv->txt_count; t++) {
		struct fbnic_q_triad *qt = &nv->qt[t];

		fbnic_enable_twq0(&qt->sub0);
		fbnic_enable_twq1(&qt->sub1);
		fbnic_enable_tcq(nv, &qt->cmpl);
	}

	/* Setup Rx Queue Triads */
	for (j = 0; j < nv->rxt_count; j++, t++) {
		struct fbnic_q_triad *qt = &nv->qt[t];

		page_pool_enable_direct_recycling(qt->sub0.page_pool,
						  &nv->napi);
		page_pool_enable_direct_recycling(qt->sub1.page_pool,
						  &nv->napi);

		fbnic_enable_bdq(&qt->sub0, &qt->sub1);
		fbnic_config_drop_mode_rcq(nv, &qt->cmpl);
		fbnic_enable_rcq(nv, &qt->cmpl);
	}
}

static void fbnic_nv_enable(struct fbnic_net *fbn, struct fbnic_napi_vector *nv)
{
	__fbnic_nv_enable(nv);
	fbnic_wrfl(fbn->fbd);
}

void fbnic_enable(struct fbnic_net *fbn)
{
	struct fbnic_dev *fbd = fbn->fbd;
	int i;

	for (i = 0; i < fbn->num_napi; i++)
		__fbnic_nv_enable(fbn->napi[i]);

	fbnic_wrfl(fbd);
}

static void fbnic_nv_irq_enable(struct fbnic_napi_vector *nv)
{
	fbnic_config_txrx_usecs(nv, FBNIC_INTR_CQ_REARM_INTR_UNMASK);
}

void fbnic_napi_enable(struct fbnic_net *fbn)
{
	u32 irqs[FBNIC_MAX_MSIX_VECS / 32] = {};
	struct fbnic_dev *fbd = fbn->fbd;
	int i;

	for (i = 0; i < fbn->num_napi; i++) {
		struct fbnic_napi_vector *nv = fbn->napi[i];

		napi_enable_locked(&nv->napi);

		fbnic_nv_irq_enable(nv);

		/* Record bit used for NAPI IRQs so we can
		 * set the mask appropriately
		 */
		irqs[nv->v_idx / 32] |= BIT(nv->v_idx % 32);
	}

	/* Force the first interrupt on the device to guarantee
	 * that any packets that may have been enqueued during the
	 * bringup are processed.
	 */
	for (i = 0; i < ARRAY_SIZE(irqs); i++) {
		if (!irqs[i])
			continue;
		fbnic_wr32(fbd, FBNIC_INTR_SET(i), irqs[i]);
	}

	fbnic_wrfl(fbd);
}

void fbnic_napi_depletion_check(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	u32 irqs[FBNIC_MAX_MSIX_VECS / 32] = {};
	struct fbnic_dev *fbd = fbn->fbd;
	int i, j, t;

	for (i = 0; i < fbn->num_napi; i++) {
		struct fbnic_napi_vector *nv = fbn->napi[i];

		/* Find RQs which are completely out of pages */
		for (t = nv->txt_count, j = 0; j < nv->rxt_count; j++, t++) {
			/* Assume 4 pages is always enough to fit a packet
			 * and therefore generate a completion and an IRQ.
			 */
			if (fbnic_desc_used(&nv->qt[t].sub0) < 4 ||
			    fbnic_desc_used(&nv->qt[t].sub1) < 4)
				irqs[nv->v_idx / 32] |= BIT(nv->v_idx % 32);
		}
	}

	for (i = 0; i < ARRAY_SIZE(irqs); i++) {
		if (!irqs[i])
			continue;
		fbnic_wr32(fbd, FBNIC_INTR_MASK_CLEAR(i), irqs[i]);
		fbnic_wr32(fbd, FBNIC_INTR_SET(i), irqs[i]);
	}

	fbnic_wrfl(fbd);
}

static int fbnic_queue_mem_alloc(struct net_device *dev, void *qmem, int idx)
{
	struct fbnic_net *fbn = netdev_priv(dev);
	const struct fbnic_q_triad *real;
	struct fbnic_q_triad *qt = qmem;
	struct fbnic_napi_vector *nv;

	if (!netif_running(dev))
		return fbnic_alloc_qt_page_pools(fbn, qt, idx);

	real = container_of(fbn->rx[idx], struct fbnic_q_triad, cmpl);
	nv = fbn->napi[idx % fbn->num_napi];

	fbnic_ring_init(&qt->sub0, real->sub0.doorbell, real->sub0.q_idx,
			real->sub0.flags);
	fbnic_ring_init(&qt->sub1, real->sub1.doorbell, real->sub1.q_idx,
			real->sub1.flags);
	fbnic_ring_init(&qt->cmpl, real->cmpl.doorbell, real->cmpl.q_idx,
			real->cmpl.flags);

	return fbnic_alloc_rx_qt_resources(fbn, nv, qt);
}

static void fbnic_queue_mem_free(struct net_device *dev, void *qmem)
{
	struct fbnic_net *fbn = netdev_priv(dev);
	struct fbnic_q_triad *qt = qmem;

	if (!netif_running(dev))
		fbnic_free_qt_page_pools(qt);
	else
		fbnic_free_qt_resources(fbn, qt);
}

static void __fbnic_nv_restart(struct fbnic_net *fbn,
			       struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = fbn->fbd;
	int i;

	fbnic_nv_enable(fbn, nv);
	fbnic_nv_fill(nv);

	napi_enable_locked(&nv->napi);
	fbnic_nv_irq_enable(nv);
	fbnic_wr32(fbd, FBNIC_INTR_SET(nv->v_idx / 32), BIT(nv->v_idx % 32));
	fbnic_wrfl(fbd);

	for (i = 0; i < nv->txt_count; i++)
		netif_wake_subqueue(fbn->netdev, nv->qt[i].sub0.q_idx);
}

static int fbnic_queue_start(struct net_device *dev, void *qmem, int idx)
{
	struct fbnic_net *fbn = netdev_priv(dev);
	struct fbnic_napi_vector *nv;
	struct fbnic_q_triad *real;

	real = container_of(fbn->rx[idx], struct fbnic_q_triad, cmpl);
	nv = fbn->napi[idx % fbn->num_napi];

	fbnic_aggregate_ring_bdq_counters(fbn, &real->sub0);
	fbnic_aggregate_ring_bdq_counters(fbn, &real->sub1);
	fbnic_aggregate_ring_rx_counters(fbn, &real->cmpl);

	memcpy(real, qmem, sizeof(*real));

	__fbnic_nv_restart(fbn, nv);

	return 0;
}

static int fbnic_queue_stop(struct net_device *dev, void *qmem, int idx)
{
	struct fbnic_net *fbn = netdev_priv(dev);
	const struct fbnic_q_triad *real;
	struct fbnic_napi_vector *nv;
	int i, t;
	int err;

	real = container_of(fbn->rx[idx], struct fbnic_q_triad, cmpl);
	nv = fbn->napi[idx % fbn->num_napi];

	napi_disable_locked(&nv->napi);
	fbnic_nv_irq_disable(nv);

	for (i = 0; i < nv->txt_count; i++)
		netif_stop_subqueue(dev, nv->qt[i].sub0.q_idx);
	fbnic_nv_disable(fbn, nv);

	for (t = 0; t < nv->txt_count + nv->rxt_count; t++) {
		err = fbnic_wait_queue_idle(fbn, t >= nv->txt_count,
					    nv->qt[t].sub0.q_idx);
		if (err)
			goto err_restart;
	}

	fbnic_synchronize_irq(fbn->fbd, nv->v_idx);
	fbnic_nv_flush(nv);

	page_pool_disable_direct_recycling(real->sub0.page_pool);
	page_pool_disable_direct_recycling(real->sub1.page_pool);

	memcpy(qmem, real, sizeof(*real));

	return 0;

err_restart:
	__fbnic_nv_restart(fbn, nv);
	return err;
}

const struct netdev_queue_mgmt_ops fbnic_queue_mgmt_ops = {
	.ndo_queue_mem_size	= sizeof(struct fbnic_q_triad),
	.ndo_queue_mem_alloc	= fbnic_queue_mem_alloc,
	.ndo_queue_mem_free	= fbnic_queue_mem_free,
	.ndo_queue_start	= fbnic_queue_start,
	.ndo_queue_stop		= fbnic_queue_stop,
};
