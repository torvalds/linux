// SPDX-License-Identifier: GPL-2.0-or-later
/* A network driver using virtio.
 *
 * Copyright 2007 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 */
//#define DEBUG
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/scatterlist.h>
#include <linux/if_vlan.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/average.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <net/route.h>
#include <net/xdp.h>
#include <net/net_failover.h>
#include <net/netdev_rx_queue.h>

static int napi_weight = NAPI_POLL_WEIGHT;
module_param(napi_weight, int, 0444);

static bool csum = true, gso = true, napi_tx = true;
module_param(csum, bool, 0444);
module_param(gso, bool, 0444);
module_param(napi_tx, bool, 0644);

/* FIXME: MTU in config. */
#define GOOD_PACKET_LEN (ETH_HLEN + VLAN_HLEN + ETH_DATA_LEN)
#define GOOD_COPY_LEN	128

#define VIRTNET_RX_PAD (NET_IP_ALIGN + NET_SKB_PAD)

/* Amount of XDP headroom to prepend to packets for use by xdp_adjust_head */
#define VIRTIO_XDP_HEADROOM 256

/* Separating two types of XDP xmit */
#define VIRTIO_XDP_TX		BIT(0)
#define VIRTIO_XDP_REDIR	BIT(1)

#define VIRTIO_XDP_FLAG	BIT(0)

/* RX packet size EWMA. The average packet size is used to determine the packet
 * buffer size when refilling RX rings. As the entire RX ring may be refilled
 * at once, the weight is chosen so that the EWMA will be insensitive to short-
 * term, transient changes in packet size.
 */
DECLARE_EWMA(pkt_len, 0, 64)

#define VIRTNET_DRIVER_VERSION "1.0.0"

static const unsigned long guest_offloads[] = {
	VIRTIO_NET_F_GUEST_TSO4,
	VIRTIO_NET_F_GUEST_TSO6,
	VIRTIO_NET_F_GUEST_ECN,
	VIRTIO_NET_F_GUEST_UFO,
	VIRTIO_NET_F_GUEST_CSUM,
	VIRTIO_NET_F_GUEST_USO4,
	VIRTIO_NET_F_GUEST_USO6,
	VIRTIO_NET_F_GUEST_HDRLEN
};

#define GUEST_OFFLOAD_GRO_HW_MASK ((1ULL << VIRTIO_NET_F_GUEST_TSO4) | \
				(1ULL << VIRTIO_NET_F_GUEST_TSO6) | \
				(1ULL << VIRTIO_NET_F_GUEST_ECN)  | \
				(1ULL << VIRTIO_NET_F_GUEST_UFO)  | \
				(1ULL << VIRTIO_NET_F_GUEST_USO4) | \
				(1ULL << VIRTIO_NET_F_GUEST_USO6))

struct virtnet_stat_desc {
	char desc[ETH_GSTRING_LEN];
	size_t offset;
};

struct virtnet_sq_stats {
	struct u64_stats_sync syncp;
	u64_stats_t packets;
	u64_stats_t bytes;
	u64_stats_t xdp_tx;
	u64_stats_t xdp_tx_drops;
	u64_stats_t kicks;
	u64_stats_t tx_timeouts;
};

struct virtnet_rq_stats {
	struct u64_stats_sync syncp;
	u64_stats_t packets;
	u64_stats_t bytes;
	u64_stats_t drops;
	u64_stats_t xdp_packets;
	u64_stats_t xdp_tx;
	u64_stats_t xdp_redirects;
	u64_stats_t xdp_drops;
	u64_stats_t kicks;
};

#define VIRTNET_SQ_STAT(m)	offsetof(struct virtnet_sq_stats, m)
#define VIRTNET_RQ_STAT(m)	offsetof(struct virtnet_rq_stats, m)

static const struct virtnet_stat_desc virtnet_sq_stats_desc[] = {
	{ "packets",		VIRTNET_SQ_STAT(packets) },
	{ "bytes",		VIRTNET_SQ_STAT(bytes) },
	{ "xdp_tx",		VIRTNET_SQ_STAT(xdp_tx) },
	{ "xdp_tx_drops",	VIRTNET_SQ_STAT(xdp_tx_drops) },
	{ "kicks",		VIRTNET_SQ_STAT(kicks) },
	{ "tx_timeouts",	VIRTNET_SQ_STAT(tx_timeouts) },
};

static const struct virtnet_stat_desc virtnet_rq_stats_desc[] = {
	{ "packets",		VIRTNET_RQ_STAT(packets) },
	{ "bytes",		VIRTNET_RQ_STAT(bytes) },
	{ "drops",		VIRTNET_RQ_STAT(drops) },
	{ "xdp_packets",	VIRTNET_RQ_STAT(xdp_packets) },
	{ "xdp_tx",		VIRTNET_RQ_STAT(xdp_tx) },
	{ "xdp_redirects",	VIRTNET_RQ_STAT(xdp_redirects) },
	{ "xdp_drops",		VIRTNET_RQ_STAT(xdp_drops) },
	{ "kicks",		VIRTNET_RQ_STAT(kicks) },
};

#define VIRTNET_SQ_STATS_LEN	ARRAY_SIZE(virtnet_sq_stats_desc)
#define VIRTNET_RQ_STATS_LEN	ARRAY_SIZE(virtnet_rq_stats_desc)

struct virtnet_interrupt_coalesce {
	u32 max_packets;
	u32 max_usecs;
};

/* The dma information of pages allocated at a time. */
struct virtnet_rq_dma {
	dma_addr_t addr;
	u32 ref;
	u16 len;
	u16 need_sync;
};

/* Internal representation of a send virtqueue */
struct send_queue {
	/* Virtqueue associated with this send _queue */
	struct virtqueue *vq;

	/* TX: fragments + linear part + virtio header */
	struct scatterlist sg[MAX_SKB_FRAGS + 2];

	/* Name of the send queue: output.$index */
	char name[16];

	struct virtnet_sq_stats stats;

	struct virtnet_interrupt_coalesce intr_coal;

	struct napi_struct napi;

	/* Record whether sq is in reset state. */
	bool reset;
};

/* Internal representation of a receive virtqueue */
struct receive_queue {
	/* Virtqueue associated with this receive_queue */
	struct virtqueue *vq;

	struct napi_struct napi;

	struct bpf_prog __rcu *xdp_prog;

	struct virtnet_rq_stats stats;

	struct virtnet_interrupt_coalesce intr_coal;

	/* Chain pages by the private ptr. */
	struct page *pages;

	/* Average packet length for mergeable receive buffers. */
	struct ewma_pkt_len mrg_avg_pkt_len;

	/* Page frag for packet buffer allocation. */
	struct page_frag alloc_frag;

	/* RX: fragments + linear part + virtio header */
	struct scatterlist sg[MAX_SKB_FRAGS + 2];

	/* Min single buffer size for mergeable buffers case. */
	unsigned int min_buf_len;

	/* Name of this receive queue: input.$index */
	char name[16];

	struct xdp_rxq_info xdp_rxq;

	/* Record the last dma info to free after new pages is allocated. */
	struct virtnet_rq_dma *last_dma;

	/* Do dma by self */
	bool do_dma;
};

/* This structure can contain rss message with maximum settings for indirection table and keysize
 * Note, that default structure that describes RSS configuration virtio_net_rss_config
 * contains same info but can't handle table values.
 * In any case, structure would be passed to virtio hw through sg_buf split by parts
 * because table sizes may be differ according to the device configuration.
 */
#define VIRTIO_NET_RSS_MAX_KEY_SIZE     40
#define VIRTIO_NET_RSS_MAX_TABLE_LEN    128
struct virtio_net_ctrl_rss {
	u32 hash_types;
	u16 indirection_table_mask;
	u16 unclassified_queue;
	u16 indirection_table[VIRTIO_NET_RSS_MAX_TABLE_LEN];
	u16 max_tx_vq;
	u8 hash_key_length;
	u8 key[VIRTIO_NET_RSS_MAX_KEY_SIZE];
};

/* Control VQ buffers: protected by the rtnl lock */
struct control_buf {
	struct virtio_net_ctrl_hdr hdr;
	virtio_net_ctrl_ack status;
	struct virtio_net_ctrl_mq mq;
	u8 promisc;
	u8 allmulti;
	__virtio16 vid;
	__virtio64 offloads;
	struct virtio_net_ctrl_rss rss;
	struct virtio_net_ctrl_coal_tx coal_tx;
	struct virtio_net_ctrl_coal_rx coal_rx;
	struct virtio_net_ctrl_coal_vq coal_vq;
};

struct virtnet_info {
	struct virtio_device *vdev;
	struct virtqueue *cvq;
	struct net_device *dev;
	struct send_queue *sq;
	struct receive_queue *rq;
	unsigned int status;

	/* Max # of queue pairs supported by the device */
	u16 max_queue_pairs;

	/* # of queue pairs currently used by the driver */
	u16 curr_queue_pairs;

	/* # of XDP queue pairs currently used by the driver */
	u16 xdp_queue_pairs;

	/* xdp_queue_pairs may be 0, when xdp is already loaded. So add this. */
	bool xdp_enabled;

	/* I like... big packets and I cannot lie! */
	bool big_packets;

	/* number of sg entries allocated for big packets */
	unsigned int big_packets_num_skbfrags;

	/* Host will merge rx buffers for big packets (shake it! shake it!) */
	bool mergeable_rx_bufs;

	/* Host supports rss and/or hash report */
	bool has_rss;
	bool has_rss_hash_report;
	u8 rss_key_size;
	u16 rss_indir_table_size;
	u32 rss_hash_types_supported;
	u32 rss_hash_types_saved;

	/* Has control virtqueue */
	bool has_cvq;

	/* Host can handle any s/g split between our header and packet data */
	bool any_header_sg;

	/* Packet virtio header size */
	u8 hdr_len;

	/* Work struct for delayed refilling if we run low on memory. */
	struct delayed_work refill;

	/* Is delayed refill enabled? */
	bool refill_enabled;

	/* The lock to synchronize the access to refill_enabled */
	spinlock_t refill_lock;

	/* Work struct for config space updates */
	struct work_struct config_work;

	/* Does the affinity hint is set for virtqueues? */
	bool affinity_hint_set;

	/* CPU hotplug instances for online & dead */
	struct hlist_node node;
	struct hlist_node node_dead;

	struct control_buf *ctrl;

	/* Ethtool settings */
	u8 duplex;
	u32 speed;

	/* Interrupt coalescing settings */
	struct virtnet_interrupt_coalesce intr_coal_tx;
	struct virtnet_interrupt_coalesce intr_coal_rx;

	unsigned long guest_offloads;
	unsigned long guest_offloads_capable;

	/* failover when STANDBY feature enabled */
	struct failover *failover;
};

struct padded_vnet_hdr {
	struct virtio_net_hdr_v1_hash hdr;
	/*
	 * hdr is in a separate sg buffer, and data sg buffer shares same page
	 * with this header sg. This padding makes next sg 16 byte aligned
	 * after the header.
	 */
	char padding[12];
};

struct virtio_net_common_hdr {
	union {
		struct virtio_net_hdr hdr;
		struct virtio_net_hdr_mrg_rxbuf	mrg_hdr;
		struct virtio_net_hdr_v1_hash hash_v1_hdr;
	};
};

static void virtnet_sq_free_unused_buf(struct virtqueue *vq, void *buf);

static bool is_xdp_frame(void *ptr)
{
	return (unsigned long)ptr & VIRTIO_XDP_FLAG;
}

static void *xdp_to_ptr(struct xdp_frame *ptr)
{
	return (void *)((unsigned long)ptr | VIRTIO_XDP_FLAG);
}

static struct xdp_frame *ptr_to_xdp(void *ptr)
{
	return (struct xdp_frame *)((unsigned long)ptr & ~VIRTIO_XDP_FLAG);
}

/* Converting between virtqueue no. and kernel tx/rx queue no.
 * 0:rx0 1:tx0 2:rx1 3:tx1 ... 2N:rxN 2N+1:txN 2N+2:cvq
 */
static int vq2txq(struct virtqueue *vq)
{
	return (vq->index - 1) / 2;
}

static int txq2vq(int txq)
{
	return txq * 2 + 1;
}

static int vq2rxq(struct virtqueue *vq)
{
	return vq->index / 2;
}

static int rxq2vq(int rxq)
{
	return rxq * 2;
}

static inline struct virtio_net_common_hdr *
skb_vnet_common_hdr(struct sk_buff *skb)
{
	return (struct virtio_net_common_hdr *)skb->cb;
}

/*
 * private is used to chain pages for big packets, put the whole
 * most recent used list in the beginning for reuse
 */
static void give_pages(struct receive_queue *rq, struct page *page)
{
	struct page *end;

	/* Find end of list, sew whole thing into vi->rq.pages. */
	for (end = page; end->private; end = (struct page *)end->private);
	end->private = (unsigned long)rq->pages;
	rq->pages = page;
}

static struct page *get_a_page(struct receive_queue *rq, gfp_t gfp_mask)
{
	struct page *p = rq->pages;

	if (p) {
		rq->pages = (struct page *)p->private;
		/* clear private here, it is used to chain pages */
		p->private = 0;
	} else
		p = alloc_page(gfp_mask);
	return p;
}

static void virtnet_rq_free_buf(struct virtnet_info *vi,
				struct receive_queue *rq, void *buf)
{
	if (vi->mergeable_rx_bufs)
		put_page(virt_to_head_page(buf));
	else if (vi->big_packets)
		give_pages(rq, buf);
	else
		put_page(virt_to_head_page(buf));
}

static void enable_delayed_refill(struct virtnet_info *vi)
{
	spin_lock_bh(&vi->refill_lock);
	vi->refill_enabled = true;
	spin_unlock_bh(&vi->refill_lock);
}

static void disable_delayed_refill(struct virtnet_info *vi)
{
	spin_lock_bh(&vi->refill_lock);
	vi->refill_enabled = false;
	spin_unlock_bh(&vi->refill_lock);
}

static void virtqueue_napi_schedule(struct napi_struct *napi,
				    struct virtqueue *vq)
{
	if (napi_schedule_prep(napi)) {
		virtqueue_disable_cb(vq);
		__napi_schedule(napi);
	}
}

static void virtqueue_napi_complete(struct napi_struct *napi,
				    struct virtqueue *vq, int processed)
{
	int opaque;

	opaque = virtqueue_enable_cb_prepare(vq);
	if (napi_complete_done(napi, processed)) {
		if (unlikely(virtqueue_poll(vq, opaque)))
			virtqueue_napi_schedule(napi, vq);
	} else {
		virtqueue_disable_cb(vq);
	}
}

static void skb_xmit_done(struct virtqueue *vq)
{
	struct virtnet_info *vi = vq->vdev->priv;
	struct napi_struct *napi = &vi->sq[vq2txq(vq)].napi;

	/* Suppress further interrupts. */
	virtqueue_disable_cb(vq);

	if (napi->weight)
		virtqueue_napi_schedule(napi, vq);
	else
		/* We were probably waiting for more output buffers. */
		netif_wake_subqueue(vi->dev, vq2txq(vq));
}

#define MRG_CTX_HEADER_SHIFT 22
static void *mergeable_len_to_ctx(unsigned int truesize,
				  unsigned int headroom)
{
	return (void *)(unsigned long)((headroom << MRG_CTX_HEADER_SHIFT) | truesize);
}

static unsigned int mergeable_ctx_to_headroom(void *mrg_ctx)
{
	return (unsigned long)mrg_ctx >> MRG_CTX_HEADER_SHIFT;
}

static unsigned int mergeable_ctx_to_truesize(void *mrg_ctx)
{
	return (unsigned long)mrg_ctx & ((1 << MRG_CTX_HEADER_SHIFT) - 1);
}

static struct sk_buff *virtnet_build_skb(void *buf, unsigned int buflen,
					 unsigned int headroom,
					 unsigned int len)
{
	struct sk_buff *skb;

	skb = build_skb(buf, buflen);
	if (unlikely(!skb))
		return NULL;

	skb_reserve(skb, headroom);
	skb_put(skb, len);

	return skb;
}

/* Called from bottom half context */
static struct sk_buff *page_to_skb(struct virtnet_info *vi,
				   struct receive_queue *rq,
				   struct page *page, unsigned int offset,
				   unsigned int len, unsigned int truesize,
				   unsigned int headroom)
{
	struct sk_buff *skb;
	struct virtio_net_common_hdr *hdr;
	unsigned int copy, hdr_len, hdr_padded_len;
	struct page *page_to_free = NULL;
	int tailroom, shinfo_size;
	char *p, *hdr_p, *buf;

	p = page_address(page) + offset;
	hdr_p = p;

	hdr_len = vi->hdr_len;
	if (vi->mergeable_rx_bufs)
		hdr_padded_len = hdr_len;
	else
		hdr_padded_len = sizeof(struct padded_vnet_hdr);

	buf = p - headroom;
	len -= hdr_len;
	offset += hdr_padded_len;
	p += hdr_padded_len;
	tailroom = truesize - headroom  - hdr_padded_len - len;

	shinfo_size = SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	/* copy small packet so we can reuse these pages */
	if (!NET_IP_ALIGN && len > GOOD_COPY_LEN && tailroom >= shinfo_size) {
		skb = virtnet_build_skb(buf, truesize, p - buf, len);
		if (unlikely(!skb))
			return NULL;

		page = (struct page *)page->private;
		if (page)
			give_pages(rq, page);
		goto ok;
	}

	/* copy small packet so we can reuse these pages for small data */
	skb = napi_alloc_skb(&rq->napi, GOOD_COPY_LEN);
	if (unlikely(!skb))
		return NULL;

	/* Copy all frame if it fits skb->head, otherwise
	 * we let virtio_net_hdr_to_skb() and GRO pull headers as needed.
	 */
	if (len <= skb_tailroom(skb))
		copy = len;
	else
		copy = ETH_HLEN;
	skb_put_data(skb, p, copy);

	len -= copy;
	offset += copy;

	if (vi->mergeable_rx_bufs) {
		if (len)
			skb_add_rx_frag(skb, 0, page, offset, len, truesize);
		else
			page_to_free = page;
		goto ok;
	}

	/*
	 * Verify that we can indeed put this data into a skb.
	 * This is here to handle cases when the device erroneously
	 * tries to receive more than is possible. This is usually
	 * the case of a broken device.
	 */
	if (unlikely(len > MAX_SKB_FRAGS * PAGE_SIZE)) {
		net_dbg_ratelimited("%s: too much data\n", skb->dev->name);
		dev_kfree_skb(skb);
		return NULL;
	}
	BUG_ON(offset >= PAGE_SIZE);
	while (len) {
		unsigned int frag_size = min((unsigned)PAGE_SIZE - offset, len);
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page, offset,
				frag_size, truesize);
		len -= frag_size;
		page = (struct page *)page->private;
		offset = 0;
	}

	if (page)
		give_pages(rq, page);

ok:
	hdr = skb_vnet_common_hdr(skb);
	memcpy(hdr, hdr_p, hdr_len);
	if (page_to_free)
		put_page(page_to_free);

	return skb;
}

static void virtnet_rq_unmap(struct receive_queue *rq, void *buf, u32 len)
{
	struct page *page = virt_to_head_page(buf);
	struct virtnet_rq_dma *dma;
	void *head;
	int offset;

	head = page_address(page);

	dma = head;

	--dma->ref;

	if (dma->need_sync && len) {
		offset = buf - (head + sizeof(*dma));

		virtqueue_dma_sync_single_range_for_cpu(rq->vq, dma->addr,
							offset, len,
							DMA_FROM_DEVICE);
	}

	if (dma->ref)
		return;

	virtqueue_dma_unmap_single_attrs(rq->vq, dma->addr, dma->len,
					 DMA_FROM_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
	put_page(page);
}

static void *virtnet_rq_get_buf(struct receive_queue *rq, u32 *len, void **ctx)
{
	void *buf;

	buf = virtqueue_get_buf_ctx(rq->vq, len, ctx);
	if (buf && rq->do_dma)
		virtnet_rq_unmap(rq, buf, *len);

	return buf;
}

static void virtnet_rq_init_one_sg(struct receive_queue *rq, void *buf, u32 len)
{
	struct virtnet_rq_dma *dma;
	dma_addr_t addr;
	u32 offset;
	void *head;

	if (!rq->do_dma) {
		sg_init_one(rq->sg, buf, len);
		return;
	}

	head = page_address(rq->alloc_frag.page);

	offset = buf - head;

	dma = head;

	addr = dma->addr - sizeof(*dma) + offset;

	sg_init_table(rq->sg, 1);
	rq->sg[0].dma_address = addr;
	rq->sg[0].length = len;
}

static void *virtnet_rq_alloc(struct receive_queue *rq, u32 size, gfp_t gfp)
{
	struct page_frag *alloc_frag = &rq->alloc_frag;
	struct virtnet_rq_dma *dma;
	void *buf, *head;
	dma_addr_t addr;

	if (unlikely(!skb_page_frag_refill(size, alloc_frag, gfp)))
		return NULL;

	head = page_address(alloc_frag->page);

	if (rq->do_dma) {
		dma = head;

		/* new pages */
		if (!alloc_frag->offset) {
			if (rq->last_dma) {
				/* Now, the new page is allocated, the last dma
				 * will not be used. So the dma can be unmapped
				 * if the ref is 0.
				 */
				virtnet_rq_unmap(rq, rq->last_dma, 0);
				rq->last_dma = NULL;
			}

			dma->len = alloc_frag->size - sizeof(*dma);

			addr = virtqueue_dma_map_single_attrs(rq->vq, dma + 1,
							      dma->len, DMA_FROM_DEVICE, 0);
			if (virtqueue_dma_mapping_error(rq->vq, addr))
				return NULL;

			dma->addr = addr;
			dma->need_sync = virtqueue_dma_need_sync(rq->vq, addr);

			/* Add a reference to dma to prevent the entire dma from
			 * being released during error handling. This reference
			 * will be freed after the pages are no longer used.
			 */
			get_page(alloc_frag->page);
			dma->ref = 1;
			alloc_frag->offset = sizeof(*dma);

			rq->last_dma = dma;
		}

		++dma->ref;
	}

	buf = head + alloc_frag->offset;

	get_page(alloc_frag->page);
	alloc_frag->offset += size;

	return buf;
}

static void virtnet_rq_set_premapped(struct virtnet_info *vi)
{
	int i;

	/* disable for big mode */
	if (!vi->mergeable_rx_bufs && vi->big_packets)
		return;

	for (i = 0; i < vi->max_queue_pairs; i++) {
		if (virtqueue_set_dma_premapped(vi->rq[i].vq))
			continue;

		vi->rq[i].do_dma = true;
	}
}

static void virtnet_rq_unmap_free_buf(struct virtqueue *vq, void *buf)
{
	struct virtnet_info *vi = vq->vdev->priv;
	struct receive_queue *rq;
	int i = vq2rxq(vq);

	rq = &vi->rq[i];

	if (rq->do_dma)
		virtnet_rq_unmap(rq, buf, 0);

	virtnet_rq_free_buf(vi, rq, buf);
}

static void free_old_xmit_skbs(struct send_queue *sq, bool in_napi)
{
	unsigned int len;
	unsigned int packets = 0;
	unsigned int bytes = 0;
	void *ptr;

	while ((ptr = virtqueue_get_buf(sq->vq, &len)) != NULL) {
		if (likely(!is_xdp_frame(ptr))) {
			struct sk_buff *skb = ptr;

			pr_debug("Sent skb %p\n", skb);

			bytes += skb->len;
			napi_consume_skb(skb, in_napi);
		} else {
			struct xdp_frame *frame = ptr_to_xdp(ptr);

			bytes += xdp_get_frame_len(frame);
			xdp_return_frame(frame);
		}
		packets++;
	}

	/* Avoid overhead when no packets have been processed
	 * happens when called speculatively from start_xmit.
	 */
	if (!packets)
		return;

	u64_stats_update_begin(&sq->stats.syncp);
	u64_stats_add(&sq->stats.bytes, bytes);
	u64_stats_add(&sq->stats.packets, packets);
	u64_stats_update_end(&sq->stats.syncp);
}

static bool is_xdp_raw_buffer_queue(struct virtnet_info *vi, int q)
{
	if (q < (vi->curr_queue_pairs - vi->xdp_queue_pairs))
		return false;
	else if (q < vi->curr_queue_pairs)
		return true;
	else
		return false;
}

static void check_sq_full_and_disable(struct virtnet_info *vi,
				      struct net_device *dev,
				      struct send_queue *sq)
{
	bool use_napi = sq->napi.weight;
	int qnum;

	qnum = sq - vi->sq;

	/* If running out of space, stop queue to avoid getting packets that we
	 * are then unable to transmit.
	 * An alternative would be to force queuing layer to requeue the skb by
	 * returning NETDEV_TX_BUSY. However, NETDEV_TX_BUSY should not be
	 * returned in a normal path of operation: it means that driver is not
	 * maintaining the TX queue stop/start state properly, and causes
	 * the stack to do a non-trivial amount of useless work.
	 * Since most packets only take 1 or 2 ring slots, stopping the queue
	 * early means 16 slots are typically wasted.
	 */
	if (sq->vq->num_free < 2+MAX_SKB_FRAGS) {
		netif_stop_subqueue(dev, qnum);
		if (use_napi) {
			if (unlikely(!virtqueue_enable_cb_delayed(sq->vq)))
				virtqueue_napi_schedule(&sq->napi, sq->vq);
		} else if (unlikely(!virtqueue_enable_cb_delayed(sq->vq))) {
			/* More just got used, free them then recheck. */
			free_old_xmit_skbs(sq, false);
			if (sq->vq->num_free >= 2+MAX_SKB_FRAGS) {
				netif_start_subqueue(dev, qnum);
				virtqueue_disable_cb(sq->vq);
			}
		}
	}
}

static int __virtnet_xdp_xmit_one(struct virtnet_info *vi,
				   struct send_queue *sq,
				   struct xdp_frame *xdpf)
{
	struct virtio_net_hdr_mrg_rxbuf *hdr;
	struct skb_shared_info *shinfo;
	u8 nr_frags = 0;
	int err, i;

	if (unlikely(xdpf->headroom < vi->hdr_len))
		return -EOVERFLOW;

	if (unlikely(xdp_frame_has_frags(xdpf))) {
		shinfo = xdp_get_shared_info_from_frame(xdpf);
		nr_frags = shinfo->nr_frags;
	}

	/* In wrapping function virtnet_xdp_xmit(), we need to free
	 * up the pending old buffers, where we need to calculate the
	 * position of skb_shared_info in xdp_get_frame_len() and
	 * xdp_return_frame(), which will involve to xdpf->data and
	 * xdpf->headroom. Therefore, we need to update the value of
	 * headroom synchronously here.
	 */
	xdpf->headroom -= vi->hdr_len;
	xdpf->data -= vi->hdr_len;
	/* Zero header and leave csum up to XDP layers */
	hdr = xdpf->data;
	memset(hdr, 0, vi->hdr_len);
	xdpf->len   += vi->hdr_len;

	sg_init_table(sq->sg, nr_frags + 1);
	sg_set_buf(sq->sg, xdpf->data, xdpf->len);
	for (i = 0; i < nr_frags; i++) {
		skb_frag_t *frag = &shinfo->frags[i];

		sg_set_page(&sq->sg[i + 1], skb_frag_page(frag),
			    skb_frag_size(frag), skb_frag_off(frag));
	}

	err = virtqueue_add_outbuf(sq->vq, sq->sg, nr_frags + 1,
				   xdp_to_ptr(xdpf), GFP_ATOMIC);
	if (unlikely(err))
		return -ENOSPC; /* Caller handle free/refcnt */

	return 0;
}

/* when vi->curr_queue_pairs > nr_cpu_ids, the txq/sq is only used for xdp tx on
 * the current cpu, so it does not need to be locked.
 *
 * Here we use marco instead of inline functions because we have to deal with
 * three issues at the same time: 1. the choice of sq. 2. judge and execute the
 * lock/unlock of txq 3. make sparse happy. It is difficult for two inline
 * functions to perfectly solve these three problems at the same time.
 */
#define virtnet_xdp_get_sq(vi) ({                                       \
	int cpu = smp_processor_id();                                   \
	struct netdev_queue *txq;                                       \
	typeof(vi) v = (vi);                                            \
	unsigned int qp;                                                \
									\
	if (v->curr_queue_pairs > nr_cpu_ids) {                         \
		qp = v->curr_queue_pairs - v->xdp_queue_pairs;          \
		qp += cpu;                                              \
		txq = netdev_get_tx_queue(v->dev, qp);                  \
		__netif_tx_acquire(txq);                                \
	} else {                                                        \
		qp = cpu % v->curr_queue_pairs;                         \
		txq = netdev_get_tx_queue(v->dev, qp);                  \
		__netif_tx_lock(txq, cpu);                              \
	}                                                               \
	v->sq + qp;                                                     \
})

#define virtnet_xdp_put_sq(vi, q) {                                     \
	struct netdev_queue *txq;                                       \
	typeof(vi) v = (vi);                                            \
									\
	txq = netdev_get_tx_queue(v->dev, (q) - v->sq);                 \
	if (v->curr_queue_pairs > nr_cpu_ids)                           \
		__netif_tx_release(txq);                                \
	else                                                            \
		__netif_tx_unlock(txq);                                 \
}

static int virtnet_xdp_xmit(struct net_device *dev,
			    int n, struct xdp_frame **frames, u32 flags)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct receive_queue *rq = vi->rq;
	struct bpf_prog *xdp_prog;
	struct send_queue *sq;
	unsigned int len;
	int packets = 0;
	int bytes = 0;
	int nxmit = 0;
	int kicks = 0;
	void *ptr;
	int ret;
	int i;

	/* Only allow ndo_xdp_xmit if XDP is loaded on dev, as this
	 * indicate XDP resources have been successfully allocated.
	 */
	xdp_prog = rcu_access_pointer(rq->xdp_prog);
	if (!xdp_prog)
		return -ENXIO;

	sq = virtnet_xdp_get_sq(vi);

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK)) {
		ret = -EINVAL;
		goto out;
	}

	/* Free up any pending old buffers before queueing new ones. */
	while ((ptr = virtqueue_get_buf(sq->vq, &len)) != NULL) {
		if (likely(is_xdp_frame(ptr))) {
			struct xdp_frame *frame = ptr_to_xdp(ptr);

			bytes += xdp_get_frame_len(frame);
			xdp_return_frame(frame);
		} else {
			struct sk_buff *skb = ptr;

			bytes += skb->len;
			napi_consume_skb(skb, false);
		}
		packets++;
	}

	for (i = 0; i < n; i++) {
		struct xdp_frame *xdpf = frames[i];

		if (__virtnet_xdp_xmit_one(vi, sq, xdpf))
			break;
		nxmit++;
	}
	ret = nxmit;

	if (!is_xdp_raw_buffer_queue(vi, sq - vi->sq))
		check_sq_full_and_disable(vi, dev, sq);

	if (flags & XDP_XMIT_FLUSH) {
		if (virtqueue_kick_prepare(sq->vq) && virtqueue_notify(sq->vq))
			kicks = 1;
	}
out:
	u64_stats_update_begin(&sq->stats.syncp);
	u64_stats_add(&sq->stats.bytes, bytes);
	u64_stats_add(&sq->stats.packets, packets);
	u64_stats_add(&sq->stats.xdp_tx, n);
	u64_stats_add(&sq->stats.xdp_tx_drops, n - nxmit);
	u64_stats_add(&sq->stats.kicks, kicks);
	u64_stats_update_end(&sq->stats.syncp);

	virtnet_xdp_put_sq(vi, sq);
	return ret;
}

static void put_xdp_frags(struct xdp_buff *xdp)
{
	struct skb_shared_info *shinfo;
	struct page *xdp_page;
	int i;

	if (xdp_buff_has_frags(xdp)) {
		shinfo = xdp_get_shared_info_from_buff(xdp);
		for (i = 0; i < shinfo->nr_frags; i++) {
			xdp_page = skb_frag_page(&shinfo->frags[i]);
			put_page(xdp_page);
		}
	}
}

static int virtnet_xdp_handler(struct bpf_prog *xdp_prog, struct xdp_buff *xdp,
			       struct net_device *dev,
			       unsigned int *xdp_xmit,
			       struct virtnet_rq_stats *stats)
{
	struct xdp_frame *xdpf;
	int err;
	u32 act;

	act = bpf_prog_run_xdp(xdp_prog, xdp);
	u64_stats_inc(&stats->xdp_packets);

	switch (act) {
	case XDP_PASS:
		return act;

	case XDP_TX:
		u64_stats_inc(&stats->xdp_tx);
		xdpf = xdp_convert_buff_to_frame(xdp);
		if (unlikely(!xdpf)) {
			netdev_dbg(dev, "convert buff to frame failed for xdp\n");
			return XDP_DROP;
		}

		err = virtnet_xdp_xmit(dev, 1, &xdpf, 0);
		if (unlikely(!err)) {
			xdp_return_frame_rx_napi(xdpf);
		} else if (unlikely(err < 0)) {
			trace_xdp_exception(dev, xdp_prog, act);
			return XDP_DROP;
		}
		*xdp_xmit |= VIRTIO_XDP_TX;
		return act;

	case XDP_REDIRECT:
		u64_stats_inc(&stats->xdp_redirects);
		err = xdp_do_redirect(dev, xdp, xdp_prog);
		if (err)
			return XDP_DROP;

		*xdp_xmit |= VIRTIO_XDP_REDIR;
		return act;

	default:
		bpf_warn_invalid_xdp_action(dev, xdp_prog, act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(dev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
		return XDP_DROP;
	}
}

static unsigned int virtnet_get_headroom(struct virtnet_info *vi)
{
	return vi->xdp_enabled ? VIRTIO_XDP_HEADROOM : 0;
}

/* We copy the packet for XDP in the following cases:
 *
 * 1) Packet is scattered across multiple rx buffers.
 * 2) Headroom space is insufficient.
 *
 * This is inefficient but it's a temporary condition that
 * we hit right after XDP is enabled and until queue is refilled
 * with large buffers with sufficient headroom - so it should affect
 * at most queue size packets.
 * Afterwards, the conditions to enable
 * XDP should preclude the underlying device from sending packets
 * across multiple buffers (num_buf > 1), and we make sure buffers
 * have enough headroom.
 */
static struct page *xdp_linearize_page(struct receive_queue *rq,
				       int *num_buf,
				       struct page *p,
				       int offset,
				       int page_off,
				       unsigned int *len)
{
	int tailroom = SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	struct page *page;

	if (page_off + *len + tailroom > PAGE_SIZE)
		return NULL;

	page = alloc_page(GFP_ATOMIC);
	if (!page)
		return NULL;

	memcpy(page_address(page) + page_off, page_address(p) + offset, *len);
	page_off += *len;

	while (--*num_buf) {
		unsigned int buflen;
		void *buf;
		int off;

		buf = virtnet_rq_get_buf(rq, &buflen, NULL);
		if (unlikely(!buf))
			goto err_buf;

		p = virt_to_head_page(buf);
		off = buf - page_address(p);

		/* guard against a misconfigured or uncooperative backend that
		 * is sending packet larger than the MTU.
		 */
		if ((page_off + buflen + tailroom) > PAGE_SIZE) {
			put_page(p);
			goto err_buf;
		}

		memcpy(page_address(page) + page_off,
		       page_address(p) + off, buflen);
		page_off += buflen;
		put_page(p);
	}

	/* Headroom does not contribute to packet length */
	*len = page_off - VIRTIO_XDP_HEADROOM;
	return page;
err_buf:
	__free_pages(page, 0);
	return NULL;
}

static struct sk_buff *receive_small_build_skb(struct virtnet_info *vi,
					       unsigned int xdp_headroom,
					       void *buf,
					       unsigned int len)
{
	unsigned int header_offset;
	unsigned int headroom;
	unsigned int buflen;
	struct sk_buff *skb;

	header_offset = VIRTNET_RX_PAD + xdp_headroom;
	headroom = vi->hdr_len + header_offset;
	buflen = SKB_DATA_ALIGN(GOOD_PACKET_LEN + headroom) +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	skb = virtnet_build_skb(buf, buflen, headroom, len);
	if (unlikely(!skb))
		return NULL;

	buf += header_offset;
	memcpy(skb_vnet_common_hdr(skb), buf, vi->hdr_len);

	return skb;
}

static struct sk_buff *receive_small_xdp(struct net_device *dev,
					 struct virtnet_info *vi,
					 struct receive_queue *rq,
					 struct bpf_prog *xdp_prog,
					 void *buf,
					 unsigned int xdp_headroom,
					 unsigned int len,
					 unsigned int *xdp_xmit,
					 struct virtnet_rq_stats *stats)
{
	unsigned int header_offset = VIRTNET_RX_PAD + xdp_headroom;
	unsigned int headroom = vi->hdr_len + header_offset;
	struct virtio_net_hdr_mrg_rxbuf *hdr = buf + header_offset;
	struct page *page = virt_to_head_page(buf);
	struct page *xdp_page;
	unsigned int buflen;
	struct xdp_buff xdp;
	struct sk_buff *skb;
	unsigned int metasize = 0;
	u32 act;

	if (unlikely(hdr->hdr.gso_type))
		goto err_xdp;

	buflen = SKB_DATA_ALIGN(GOOD_PACKET_LEN + headroom) +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	if (unlikely(xdp_headroom < virtnet_get_headroom(vi))) {
		int offset = buf - page_address(page) + header_offset;
		unsigned int tlen = len + vi->hdr_len;
		int num_buf = 1;

		xdp_headroom = virtnet_get_headroom(vi);
		header_offset = VIRTNET_RX_PAD + xdp_headroom;
		headroom = vi->hdr_len + header_offset;
		buflen = SKB_DATA_ALIGN(GOOD_PACKET_LEN + headroom) +
			SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
		xdp_page = xdp_linearize_page(rq, &num_buf, page,
					      offset, header_offset,
					      &tlen);
		if (!xdp_page)
			goto err_xdp;

		buf = page_address(xdp_page);
		put_page(page);
		page = xdp_page;
	}

	xdp_init_buff(&xdp, buflen, &rq->xdp_rxq);
	xdp_prepare_buff(&xdp, buf + VIRTNET_RX_PAD + vi->hdr_len,
			 xdp_headroom, len, true);

	act = virtnet_xdp_handler(xdp_prog, &xdp, dev, xdp_xmit, stats);

	switch (act) {
	case XDP_PASS:
		/* Recalculate length in case bpf program changed it */
		len = xdp.data_end - xdp.data;
		metasize = xdp.data - xdp.data_meta;
		break;

	case XDP_TX:
	case XDP_REDIRECT:
		goto xdp_xmit;

	default:
		goto err_xdp;
	}

	skb = virtnet_build_skb(buf, buflen, xdp.data - buf, len);
	if (unlikely(!skb))
		goto err;

	if (metasize)
		skb_metadata_set(skb, metasize);

	return skb;

err_xdp:
	u64_stats_inc(&stats->xdp_drops);
err:
	u64_stats_inc(&stats->drops);
	put_page(page);
xdp_xmit:
	return NULL;
}

static struct sk_buff *receive_small(struct net_device *dev,
				     struct virtnet_info *vi,
				     struct receive_queue *rq,
				     void *buf, void *ctx,
				     unsigned int len,
				     unsigned int *xdp_xmit,
				     struct virtnet_rq_stats *stats)
{
	unsigned int xdp_headroom = (unsigned long)ctx;
	struct page *page = virt_to_head_page(buf);
	struct sk_buff *skb;

	len -= vi->hdr_len;
	u64_stats_add(&stats->bytes, len);

	if (unlikely(len > GOOD_PACKET_LEN)) {
		pr_debug("%s: rx error: len %u exceeds max size %d\n",
			 dev->name, len, GOOD_PACKET_LEN);
		DEV_STATS_INC(dev, rx_length_errors);
		goto err;
	}

	if (unlikely(vi->xdp_enabled)) {
		struct bpf_prog *xdp_prog;

		rcu_read_lock();
		xdp_prog = rcu_dereference(rq->xdp_prog);
		if (xdp_prog) {
			skb = receive_small_xdp(dev, vi, rq, xdp_prog, buf,
						xdp_headroom, len, xdp_xmit,
						stats);
			rcu_read_unlock();
			return skb;
		}
		rcu_read_unlock();
	}

	skb = receive_small_build_skb(vi, xdp_headroom, buf, len);
	if (likely(skb))
		return skb;

err:
	u64_stats_inc(&stats->drops);
	put_page(page);
	return NULL;
}

static struct sk_buff *receive_big(struct net_device *dev,
				   struct virtnet_info *vi,
				   struct receive_queue *rq,
				   void *buf,
				   unsigned int len,
				   struct virtnet_rq_stats *stats)
{
	struct page *page = buf;
	struct sk_buff *skb =
		page_to_skb(vi, rq, page, 0, len, PAGE_SIZE, 0);

	u64_stats_add(&stats->bytes, len - vi->hdr_len);
	if (unlikely(!skb))
		goto err;

	return skb;

err:
	u64_stats_inc(&stats->drops);
	give_pages(rq, page);
	return NULL;
}

static void mergeable_buf_free(struct receive_queue *rq, int num_buf,
			       struct net_device *dev,
			       struct virtnet_rq_stats *stats)
{
	struct page *page;
	void *buf;
	int len;

	while (num_buf-- > 1) {
		buf = virtnet_rq_get_buf(rq, &len, NULL);
		if (unlikely(!buf)) {
			pr_debug("%s: rx error: %d buffers missing\n",
				 dev->name, num_buf);
			DEV_STATS_INC(dev, rx_length_errors);
			break;
		}
		u64_stats_add(&stats->bytes, len);
		page = virt_to_head_page(buf);
		put_page(page);
	}
}

/* Why not use xdp_build_skb_from_frame() ?
 * XDP core assumes that xdp frags are PAGE_SIZE in length, while in
 * virtio-net there are 2 points that do not match its requirements:
 *  1. The size of the prefilled buffer is not fixed before xdp is set.
 *  2. xdp_build_skb_from_frame() does more checks that we don't need,
 *     like eth_type_trans() (which virtio-net does in receive_buf()).
 */
static struct sk_buff *build_skb_from_xdp_buff(struct net_device *dev,
					       struct virtnet_info *vi,
					       struct xdp_buff *xdp,
					       unsigned int xdp_frags_truesz)
{
	struct skb_shared_info *sinfo = xdp_get_shared_info_from_buff(xdp);
	unsigned int headroom, data_len;
	struct sk_buff *skb;
	int metasize;
	u8 nr_frags;

	if (unlikely(xdp->data_end > xdp_data_hard_end(xdp))) {
		pr_debug("Error building skb as missing reserved tailroom for xdp");
		return NULL;
	}

	if (unlikely(xdp_buff_has_frags(xdp)))
		nr_frags = sinfo->nr_frags;

	skb = build_skb(xdp->data_hard_start, xdp->frame_sz);
	if (unlikely(!skb))
		return NULL;

	headroom = xdp->data - xdp->data_hard_start;
	data_len = xdp->data_end - xdp->data;
	skb_reserve(skb, headroom);
	__skb_put(skb, data_len);

	metasize = xdp->data - xdp->data_meta;
	metasize = metasize > 0 ? metasize : 0;
	if (metasize)
		skb_metadata_set(skb, metasize);

	if (unlikely(xdp_buff_has_frags(xdp)))
		xdp_update_skb_shared_info(skb, nr_frags,
					   sinfo->xdp_frags_size,
					   xdp_frags_truesz,
					   xdp_buff_is_frag_pfmemalloc(xdp));

	return skb;
}

/* TODO: build xdp in big mode */
static int virtnet_build_xdp_buff_mrg(struct net_device *dev,
				      struct virtnet_info *vi,
				      struct receive_queue *rq,
				      struct xdp_buff *xdp,
				      void *buf,
				      unsigned int len,
				      unsigned int frame_sz,
				      int *num_buf,
				      unsigned int *xdp_frags_truesize,
				      struct virtnet_rq_stats *stats)
{
	struct virtio_net_hdr_mrg_rxbuf *hdr = buf;
	unsigned int headroom, tailroom, room;
	unsigned int truesize, cur_frag_size;
	struct skb_shared_info *shinfo;
	unsigned int xdp_frags_truesz = 0;
	struct page *page;
	skb_frag_t *frag;
	int offset;
	void *ctx;

	xdp_init_buff(xdp, frame_sz, &rq->xdp_rxq);
	xdp_prepare_buff(xdp, buf - VIRTIO_XDP_HEADROOM,
			 VIRTIO_XDP_HEADROOM + vi->hdr_len, len - vi->hdr_len, true);

	if (!*num_buf)
		return 0;

	if (*num_buf > 1) {
		/* If we want to build multi-buffer xdp, we need
		 * to specify that the flags of xdp_buff have the
		 * XDP_FLAGS_HAS_FRAG bit.
		 */
		if (!xdp_buff_has_frags(xdp))
			xdp_buff_set_frags_flag(xdp);

		shinfo = xdp_get_shared_info_from_buff(xdp);
		shinfo->nr_frags = 0;
		shinfo->xdp_frags_size = 0;
	}

	if (*num_buf > MAX_SKB_FRAGS + 1)
		return -EINVAL;

	while (--*num_buf > 0) {
		buf = virtnet_rq_get_buf(rq, &len, &ctx);
		if (unlikely(!buf)) {
			pr_debug("%s: rx error: %d buffers out of %d missing\n",
				 dev->name, *num_buf,
				 virtio16_to_cpu(vi->vdev, hdr->num_buffers));
			DEV_STATS_INC(dev, rx_length_errors);
			goto err;
		}

		u64_stats_add(&stats->bytes, len);
		page = virt_to_head_page(buf);
		offset = buf - page_address(page);

		truesize = mergeable_ctx_to_truesize(ctx);
		headroom = mergeable_ctx_to_headroom(ctx);
		tailroom = headroom ? sizeof(struct skb_shared_info) : 0;
		room = SKB_DATA_ALIGN(headroom + tailroom);

		cur_frag_size = truesize;
		xdp_frags_truesz += cur_frag_size;
		if (unlikely(len > truesize - room || cur_frag_size > PAGE_SIZE)) {
			put_page(page);
			pr_debug("%s: rx error: len %u exceeds truesize %lu\n",
				 dev->name, len, (unsigned long)(truesize - room));
			DEV_STATS_INC(dev, rx_length_errors);
			goto err;
		}

		frag = &shinfo->frags[shinfo->nr_frags++];
		skb_frag_fill_page_desc(frag, page, offset, len);
		if (page_is_pfmemalloc(page))
			xdp_buff_set_frag_pfmemalloc(xdp);

		shinfo->xdp_frags_size += len;
	}

	*xdp_frags_truesize = xdp_frags_truesz;
	return 0;

err:
	put_xdp_frags(xdp);
	return -EINVAL;
}

static void *mergeable_xdp_get_buf(struct virtnet_info *vi,
				   struct receive_queue *rq,
				   struct bpf_prog *xdp_prog,
				   void *ctx,
				   unsigned int *frame_sz,
				   int *num_buf,
				   struct page **page,
				   int offset,
				   unsigned int *len,
				   struct virtio_net_hdr_mrg_rxbuf *hdr)
{
	unsigned int truesize = mergeable_ctx_to_truesize(ctx);
	unsigned int headroom = mergeable_ctx_to_headroom(ctx);
	struct page *xdp_page;
	unsigned int xdp_room;

	/* Transient failure which in theory could occur if
	 * in-flight packets from before XDP was enabled reach
	 * the receive path after XDP is loaded.
	 */
	if (unlikely(hdr->hdr.gso_type))
		return NULL;

	/* Now XDP core assumes frag size is PAGE_SIZE, but buffers
	 * with headroom may add hole in truesize, which
	 * make their length exceed PAGE_SIZE. So we disabled the
	 * hole mechanism for xdp. See add_recvbuf_mergeable().
	 */
	*frame_sz = truesize;

	if (likely(headroom >= virtnet_get_headroom(vi) &&
		   (*num_buf == 1 || xdp_prog->aux->xdp_has_frags))) {
		return page_address(*page) + offset;
	}

	/* This happens when headroom is not enough because
	 * of the buffer was prefilled before XDP is set.
	 * This should only happen for the first several packets.
	 * In fact, vq reset can be used here to help us clean up
	 * the prefilled buffers, but many existing devices do not
	 * support it, and we don't want to bother users who are
	 * using xdp normally.
	 */
	if (!xdp_prog->aux->xdp_has_frags) {
		/* linearize data for XDP */
		xdp_page = xdp_linearize_page(rq, num_buf,
					      *page, offset,
					      VIRTIO_XDP_HEADROOM,
					      len);
		if (!xdp_page)
			return NULL;
	} else {
		xdp_room = SKB_DATA_ALIGN(VIRTIO_XDP_HEADROOM +
					  sizeof(struct skb_shared_info));
		if (*len + xdp_room > PAGE_SIZE)
			return NULL;

		xdp_page = alloc_page(GFP_ATOMIC);
		if (!xdp_page)
			return NULL;

		memcpy(page_address(xdp_page) + VIRTIO_XDP_HEADROOM,
		       page_address(*page) + offset, *len);
	}

	*frame_sz = PAGE_SIZE;

	put_page(*page);

	*page = xdp_page;

	return page_address(*page) + VIRTIO_XDP_HEADROOM;
}

static struct sk_buff *receive_mergeable_xdp(struct net_device *dev,
					     struct virtnet_info *vi,
					     struct receive_queue *rq,
					     struct bpf_prog *xdp_prog,
					     void *buf,
					     void *ctx,
					     unsigned int len,
					     unsigned int *xdp_xmit,
					     struct virtnet_rq_stats *stats)
{
	struct virtio_net_hdr_mrg_rxbuf *hdr = buf;
	int num_buf = virtio16_to_cpu(vi->vdev, hdr->num_buffers);
	struct page *page = virt_to_head_page(buf);
	int offset = buf - page_address(page);
	unsigned int xdp_frags_truesz = 0;
	struct sk_buff *head_skb;
	unsigned int frame_sz;
	struct xdp_buff xdp;
	void *data;
	u32 act;
	int err;

	data = mergeable_xdp_get_buf(vi, rq, xdp_prog, ctx, &frame_sz, &num_buf, &page,
				     offset, &len, hdr);
	if (unlikely(!data))
		goto err_xdp;

	err = virtnet_build_xdp_buff_mrg(dev, vi, rq, &xdp, data, len, frame_sz,
					 &num_buf, &xdp_frags_truesz, stats);
	if (unlikely(err))
		goto err_xdp;

	act = virtnet_xdp_handler(xdp_prog, &xdp, dev, xdp_xmit, stats);

	switch (act) {
	case XDP_PASS:
		head_skb = build_skb_from_xdp_buff(dev, vi, &xdp, xdp_frags_truesz);
		if (unlikely(!head_skb))
			break;
		return head_skb;

	case XDP_TX:
	case XDP_REDIRECT:
		return NULL;

	default:
		break;
	}

	put_xdp_frags(&xdp);

err_xdp:
	put_page(page);
	mergeable_buf_free(rq, num_buf, dev, stats);

	u64_stats_inc(&stats->xdp_drops);
	u64_stats_inc(&stats->drops);
	return NULL;
}

static struct sk_buff *receive_mergeable(struct net_device *dev,
					 struct virtnet_info *vi,
					 struct receive_queue *rq,
					 void *buf,
					 void *ctx,
					 unsigned int len,
					 unsigned int *xdp_xmit,
					 struct virtnet_rq_stats *stats)
{
	struct virtio_net_hdr_mrg_rxbuf *hdr = buf;
	int num_buf = virtio16_to_cpu(vi->vdev, hdr->num_buffers);
	struct page *page = virt_to_head_page(buf);
	int offset = buf - page_address(page);
	struct sk_buff *head_skb, *curr_skb;
	unsigned int truesize = mergeable_ctx_to_truesize(ctx);
	unsigned int headroom = mergeable_ctx_to_headroom(ctx);
	unsigned int tailroom = headroom ? sizeof(struct skb_shared_info) : 0;
	unsigned int room = SKB_DATA_ALIGN(headroom + tailroom);

	head_skb = NULL;
	u64_stats_add(&stats->bytes, len - vi->hdr_len);

	if (unlikely(len > truesize - room)) {
		pr_debug("%s: rx error: len %u exceeds truesize %lu\n",
			 dev->name, len, (unsigned long)(truesize - room));
		DEV_STATS_INC(dev, rx_length_errors);
		goto err_skb;
	}

	if (unlikely(vi->xdp_enabled)) {
		struct bpf_prog *xdp_prog;

		rcu_read_lock();
		xdp_prog = rcu_dereference(rq->xdp_prog);
		if (xdp_prog) {
			head_skb = receive_mergeable_xdp(dev, vi, rq, xdp_prog, buf, ctx,
							 len, xdp_xmit, stats);
			rcu_read_unlock();
			return head_skb;
		}
		rcu_read_unlock();
	}

	head_skb = page_to_skb(vi, rq, page, offset, len, truesize, headroom);
	curr_skb = head_skb;

	if (unlikely(!curr_skb))
		goto err_skb;
	while (--num_buf) {
		int num_skb_frags;

		buf = virtnet_rq_get_buf(rq, &len, &ctx);
		if (unlikely(!buf)) {
			pr_debug("%s: rx error: %d buffers out of %d missing\n",
				 dev->name, num_buf,
				 virtio16_to_cpu(vi->vdev,
						 hdr->num_buffers));
			DEV_STATS_INC(dev, rx_length_errors);
			goto err_buf;
		}

		u64_stats_add(&stats->bytes, len);
		page = virt_to_head_page(buf);

		truesize = mergeable_ctx_to_truesize(ctx);
		headroom = mergeable_ctx_to_headroom(ctx);
		tailroom = headroom ? sizeof(struct skb_shared_info) : 0;
		room = SKB_DATA_ALIGN(headroom + tailroom);
		if (unlikely(len > truesize - room)) {
			pr_debug("%s: rx error: len %u exceeds truesize %lu\n",
				 dev->name, len, (unsigned long)(truesize - room));
			DEV_STATS_INC(dev, rx_length_errors);
			goto err_skb;
		}

		num_skb_frags = skb_shinfo(curr_skb)->nr_frags;
		if (unlikely(num_skb_frags == MAX_SKB_FRAGS)) {
			struct sk_buff *nskb = alloc_skb(0, GFP_ATOMIC);

			if (unlikely(!nskb))
				goto err_skb;
			if (curr_skb == head_skb)
				skb_shinfo(curr_skb)->frag_list = nskb;
			else
				curr_skb->next = nskb;
			curr_skb = nskb;
			head_skb->truesize += nskb->truesize;
			num_skb_frags = 0;
		}
		if (curr_skb != head_skb) {
			head_skb->data_len += len;
			head_skb->len += len;
			head_skb->truesize += truesize;
		}
		offset = buf - page_address(page);
		if (skb_can_coalesce(curr_skb, num_skb_frags, page, offset)) {
			put_page(page);
			skb_coalesce_rx_frag(curr_skb, num_skb_frags - 1,
					     len, truesize);
		} else {
			skb_add_rx_frag(curr_skb, num_skb_frags, page,
					offset, len, truesize);
		}
	}

	ewma_pkt_len_add(&rq->mrg_avg_pkt_len, head_skb->len);
	return head_skb;

err_skb:
	put_page(page);
	mergeable_buf_free(rq, num_buf, dev, stats);

err_buf:
	u64_stats_inc(&stats->drops);
	dev_kfree_skb(head_skb);
	return NULL;
}

static void virtio_skb_set_hash(const struct virtio_net_hdr_v1_hash *hdr_hash,
				struct sk_buff *skb)
{
	enum pkt_hash_types rss_hash_type;

	if (!hdr_hash || !skb)
		return;

	switch (__le16_to_cpu(hdr_hash->hash_report)) {
	case VIRTIO_NET_HASH_REPORT_TCPv4:
	case VIRTIO_NET_HASH_REPORT_UDPv4:
	case VIRTIO_NET_HASH_REPORT_TCPv6:
	case VIRTIO_NET_HASH_REPORT_UDPv6:
	case VIRTIO_NET_HASH_REPORT_TCPv6_EX:
	case VIRTIO_NET_HASH_REPORT_UDPv6_EX:
		rss_hash_type = PKT_HASH_TYPE_L4;
		break;
	case VIRTIO_NET_HASH_REPORT_IPv4:
	case VIRTIO_NET_HASH_REPORT_IPv6:
	case VIRTIO_NET_HASH_REPORT_IPv6_EX:
		rss_hash_type = PKT_HASH_TYPE_L3;
		break;
	case VIRTIO_NET_HASH_REPORT_NONE:
	default:
		rss_hash_type = PKT_HASH_TYPE_NONE;
	}
	skb_set_hash(skb, __le32_to_cpu(hdr_hash->hash_value), rss_hash_type);
}

static void receive_buf(struct virtnet_info *vi, struct receive_queue *rq,
			void *buf, unsigned int len, void **ctx,
			unsigned int *xdp_xmit,
			struct virtnet_rq_stats *stats)
{
	struct net_device *dev = vi->dev;
	struct sk_buff *skb;
	struct virtio_net_common_hdr *hdr;

	if (unlikely(len < vi->hdr_len + ETH_HLEN)) {
		pr_debug("%s: short packet %i\n", dev->name, len);
		DEV_STATS_INC(dev, rx_length_errors);
		virtnet_rq_free_buf(vi, rq, buf);
		return;
	}

	if (vi->mergeable_rx_bufs)
		skb = receive_mergeable(dev, vi, rq, buf, ctx, len, xdp_xmit,
					stats);
	else if (vi->big_packets)
		skb = receive_big(dev, vi, rq, buf, len, stats);
	else
		skb = receive_small(dev, vi, rq, buf, ctx, len, xdp_xmit, stats);

	if (unlikely(!skb))
		return;

	hdr = skb_vnet_common_hdr(skb);
	if (dev->features & NETIF_F_RXHASH && vi->has_rss_hash_report)
		virtio_skb_set_hash(&hdr->hash_v1_hdr, skb);

	if (hdr->hdr.flags & VIRTIO_NET_HDR_F_DATA_VALID)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (virtio_net_hdr_to_skb(skb, &hdr->hdr,
				  virtio_is_little_endian(vi->vdev))) {
		net_warn_ratelimited("%s: bad gso: type: %u, size: %u\n",
				     dev->name, hdr->hdr.gso_type,
				     hdr->hdr.gso_size);
		goto frame_err;
	}

	skb_record_rx_queue(skb, vq2rxq(rq->vq));
	skb->protocol = eth_type_trans(skb, dev);
	pr_debug("Receiving skb proto 0x%04x len %i type %i\n",
		 ntohs(skb->protocol), skb->len, skb->pkt_type);

	napi_gro_receive(&rq->napi, skb);
	return;

frame_err:
	DEV_STATS_INC(dev, rx_frame_errors);
	dev_kfree_skb(skb);
}

/* Unlike mergeable buffers, all buffers are allocated to the
 * same size, except for the headroom. For this reason we do
 * not need to use  mergeable_len_to_ctx here - it is enough
 * to store the headroom as the context ignoring the truesize.
 */
static int add_recvbuf_small(struct virtnet_info *vi, struct receive_queue *rq,
			     gfp_t gfp)
{
	char *buf;
	unsigned int xdp_headroom = virtnet_get_headroom(vi);
	void *ctx = (void *)(unsigned long)xdp_headroom;
	int len = vi->hdr_len + VIRTNET_RX_PAD + GOOD_PACKET_LEN + xdp_headroom;
	int err;

	len = SKB_DATA_ALIGN(len) +
	      SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	buf = virtnet_rq_alloc(rq, len, gfp);
	if (unlikely(!buf))
		return -ENOMEM;

	virtnet_rq_init_one_sg(rq, buf + VIRTNET_RX_PAD + xdp_headroom,
			       vi->hdr_len + GOOD_PACKET_LEN);

	err = virtqueue_add_inbuf_ctx(rq->vq, rq->sg, 1, buf, ctx, gfp);
	if (err < 0) {
		if (rq->do_dma)
			virtnet_rq_unmap(rq, buf, 0);
		put_page(virt_to_head_page(buf));
	}

	return err;
}

static int add_recvbuf_big(struct virtnet_info *vi, struct receive_queue *rq,
			   gfp_t gfp)
{
	struct page *first, *list = NULL;
	char *p;
	int i, err, offset;

	sg_init_table(rq->sg, vi->big_packets_num_skbfrags + 2);

	/* page in rq->sg[vi->big_packets_num_skbfrags + 1] is list tail */
	for (i = vi->big_packets_num_skbfrags + 1; i > 1; --i) {
		first = get_a_page(rq, gfp);
		if (!first) {
			if (list)
				give_pages(rq, list);
			return -ENOMEM;
		}
		sg_set_buf(&rq->sg[i], page_address(first), PAGE_SIZE);

		/* chain new page in list head to match sg */
		first->private = (unsigned long)list;
		list = first;
	}

	first = get_a_page(rq, gfp);
	if (!first) {
		give_pages(rq, list);
		return -ENOMEM;
	}
	p = page_address(first);

	/* rq->sg[0], rq->sg[1] share the same page */
	/* a separated rq->sg[0] for header - required in case !any_header_sg */
	sg_set_buf(&rq->sg[0], p, vi->hdr_len);

	/* rq->sg[1] for data packet, from offset */
	offset = sizeof(struct padded_vnet_hdr);
	sg_set_buf(&rq->sg[1], p + offset, PAGE_SIZE - offset);

	/* chain first in list head */
	first->private = (unsigned long)list;
	err = virtqueue_add_inbuf(rq->vq, rq->sg, vi->big_packets_num_skbfrags + 2,
				  first, gfp);
	if (err < 0)
		give_pages(rq, first);

	return err;
}

static unsigned int get_mergeable_buf_len(struct receive_queue *rq,
					  struct ewma_pkt_len *avg_pkt_len,
					  unsigned int room)
{
	struct virtnet_info *vi = rq->vq->vdev->priv;
	const size_t hdr_len = vi->hdr_len;
	unsigned int len;

	if (room)
		return PAGE_SIZE - room;

	len = hdr_len +	clamp_t(unsigned int, ewma_pkt_len_read(avg_pkt_len),
				rq->min_buf_len, PAGE_SIZE - hdr_len);

	return ALIGN(len, L1_CACHE_BYTES);
}

static int add_recvbuf_mergeable(struct virtnet_info *vi,
				 struct receive_queue *rq, gfp_t gfp)
{
	struct page_frag *alloc_frag = &rq->alloc_frag;
	unsigned int headroom = virtnet_get_headroom(vi);
	unsigned int tailroom = headroom ? sizeof(struct skb_shared_info) : 0;
	unsigned int room = SKB_DATA_ALIGN(headroom + tailroom);
	unsigned int len, hole;
	void *ctx;
	char *buf;
	int err;

	/* Extra tailroom is needed to satisfy XDP's assumption. This
	 * means rx frags coalescing won't work, but consider we've
	 * disabled GSO for XDP, it won't be a big issue.
	 */
	len = get_mergeable_buf_len(rq, &rq->mrg_avg_pkt_len, room);

	buf = virtnet_rq_alloc(rq, len + room, gfp);
	if (unlikely(!buf))
		return -ENOMEM;

	buf += headroom; /* advance address leaving hole at front of pkt */
	hole = alloc_frag->size - alloc_frag->offset;
	if (hole < len + room) {
		/* To avoid internal fragmentation, if there is very likely not
		 * enough space for another buffer, add the remaining space to
		 * the current buffer.
		 * XDP core assumes that frame_size of xdp_buff and the length
		 * of the frag are PAGE_SIZE, so we disable the hole mechanism.
		 */
		if (!headroom)
			len += hole;
		alloc_frag->offset += hole;
	}

	virtnet_rq_init_one_sg(rq, buf, len);

	ctx = mergeable_len_to_ctx(len + room, headroom);
	err = virtqueue_add_inbuf_ctx(rq->vq, rq->sg, 1, buf, ctx, gfp);
	if (err < 0) {
		if (rq->do_dma)
			virtnet_rq_unmap(rq, buf, 0);
		put_page(virt_to_head_page(buf));
	}

	return err;
}

/*
 * Returns false if we couldn't fill entirely (OOM).
 *
 * Normally run in the receive path, but can also be run from ndo_open
 * before we're receiving packets, or from refill_work which is
 * careful to disable receiving (using napi_disable).
 */
static bool try_fill_recv(struct virtnet_info *vi, struct receive_queue *rq,
			  gfp_t gfp)
{
	int err;
	bool oom;

	do {
		if (vi->mergeable_rx_bufs)
			err = add_recvbuf_mergeable(vi, rq, gfp);
		else if (vi->big_packets)
			err = add_recvbuf_big(vi, rq, gfp);
		else
			err = add_recvbuf_small(vi, rq, gfp);

		oom = err == -ENOMEM;
		if (err)
			break;
	} while (rq->vq->num_free);
	if (virtqueue_kick_prepare(rq->vq) && virtqueue_notify(rq->vq)) {
		unsigned long flags;

		flags = u64_stats_update_begin_irqsave(&rq->stats.syncp);
		u64_stats_inc(&rq->stats.kicks);
		u64_stats_update_end_irqrestore(&rq->stats.syncp, flags);
	}

	return !oom;
}

static void skb_recv_done(struct virtqueue *rvq)
{
	struct virtnet_info *vi = rvq->vdev->priv;
	struct receive_queue *rq = &vi->rq[vq2rxq(rvq)];

	virtqueue_napi_schedule(&rq->napi, rvq);
}

static void virtnet_napi_enable(struct virtqueue *vq, struct napi_struct *napi)
{
	napi_enable(napi);

	/* If all buffers were filled by other side before we napi_enabled, we
	 * won't get another interrupt, so process any outstanding packets now.
	 * Call local_bh_enable after to trigger softIRQ processing.
	 */
	local_bh_disable();
	virtqueue_napi_schedule(napi, vq);
	local_bh_enable();
}

static void virtnet_napi_tx_enable(struct virtnet_info *vi,
				   struct virtqueue *vq,
				   struct napi_struct *napi)
{
	if (!napi->weight)
		return;

	/* Tx napi touches cachelines on the cpu handling tx interrupts. Only
	 * enable the feature if this is likely affine with the transmit path.
	 */
	if (!vi->affinity_hint_set) {
		napi->weight = 0;
		return;
	}

	return virtnet_napi_enable(vq, napi);
}

static void virtnet_napi_tx_disable(struct napi_struct *napi)
{
	if (napi->weight)
		napi_disable(napi);
}

static void refill_work(struct work_struct *work)
{
	struct virtnet_info *vi =
		container_of(work, struct virtnet_info, refill.work);
	bool still_empty;
	int i;

	for (i = 0; i < vi->curr_queue_pairs; i++) {
		struct receive_queue *rq = &vi->rq[i];

		napi_disable(&rq->napi);
		still_empty = !try_fill_recv(vi, rq, GFP_KERNEL);
		virtnet_napi_enable(rq->vq, &rq->napi);

		/* In theory, this can happen: if we don't get any buffers in
		 * we will *never* try to fill again.
		 */
		if (still_empty)
			schedule_delayed_work(&vi->refill, HZ/2);
	}
}

static int virtnet_receive(struct receive_queue *rq, int budget,
			   unsigned int *xdp_xmit)
{
	struct virtnet_info *vi = rq->vq->vdev->priv;
	struct virtnet_rq_stats stats = {};
	unsigned int len;
	int packets = 0;
	void *buf;
	int i;

	if (!vi->big_packets || vi->mergeable_rx_bufs) {
		void *ctx;

		while (packets < budget &&
		       (buf = virtnet_rq_get_buf(rq, &len, &ctx))) {
			receive_buf(vi, rq, buf, len, ctx, xdp_xmit, &stats);
			packets++;
		}
	} else {
		while (packets < budget &&
		       (buf = virtnet_rq_get_buf(rq, &len, NULL)) != NULL) {
			receive_buf(vi, rq, buf, len, NULL, xdp_xmit, &stats);
			packets++;
		}
	}

	if (rq->vq->num_free > min((unsigned int)budget, virtqueue_get_vring_size(rq->vq)) / 2) {
		if (!try_fill_recv(vi, rq, GFP_ATOMIC)) {
			spin_lock(&vi->refill_lock);
			if (vi->refill_enabled)
				schedule_delayed_work(&vi->refill, 0);
			spin_unlock(&vi->refill_lock);
		}
	}

	u64_stats_set(&stats.packets, packets);
	u64_stats_update_begin(&rq->stats.syncp);
	for (i = 0; i < VIRTNET_RQ_STATS_LEN; i++) {
		size_t offset = virtnet_rq_stats_desc[i].offset;
		u64_stats_t *item, *src;

		item = (u64_stats_t *)((u8 *)&rq->stats + offset);
		src = (u64_stats_t *)((u8 *)&stats + offset);
		u64_stats_add(item, u64_stats_read(src));
	}
	u64_stats_update_end(&rq->stats.syncp);

	return packets;
}

static void virtnet_poll_cleantx(struct receive_queue *rq)
{
	struct virtnet_info *vi = rq->vq->vdev->priv;
	unsigned int index = vq2rxq(rq->vq);
	struct send_queue *sq = &vi->sq[index];
	struct netdev_queue *txq = netdev_get_tx_queue(vi->dev, index);

	if (!sq->napi.weight || is_xdp_raw_buffer_queue(vi, index))
		return;

	if (__netif_tx_trylock(txq)) {
		if (sq->reset) {
			__netif_tx_unlock(txq);
			return;
		}

		do {
			virtqueue_disable_cb(sq->vq);
			free_old_xmit_skbs(sq, true);
		} while (unlikely(!virtqueue_enable_cb_delayed(sq->vq)));

		if (sq->vq->num_free >= 2 + MAX_SKB_FRAGS)
			netif_tx_wake_queue(txq);

		__netif_tx_unlock(txq);
	}
}

static int virtnet_poll(struct napi_struct *napi, int budget)
{
	struct receive_queue *rq =
		container_of(napi, struct receive_queue, napi);
	struct virtnet_info *vi = rq->vq->vdev->priv;
	struct send_queue *sq;
	unsigned int received;
	unsigned int xdp_xmit = 0;

	virtnet_poll_cleantx(rq);

	received = virtnet_receive(rq, budget, &xdp_xmit);

	if (xdp_xmit & VIRTIO_XDP_REDIR)
		xdp_do_flush();

	/* Out of packets? */
	if (received < budget)
		virtqueue_napi_complete(napi, rq->vq, received);

	if (xdp_xmit & VIRTIO_XDP_TX) {
		sq = virtnet_xdp_get_sq(vi);
		if (virtqueue_kick_prepare(sq->vq) && virtqueue_notify(sq->vq)) {
			u64_stats_update_begin(&sq->stats.syncp);
			u64_stats_inc(&sq->stats.kicks);
			u64_stats_update_end(&sq->stats.syncp);
		}
		virtnet_xdp_put_sq(vi, sq);
	}

	return received;
}

static void virtnet_disable_queue_pair(struct virtnet_info *vi, int qp_index)
{
	virtnet_napi_tx_disable(&vi->sq[qp_index].napi);
	napi_disable(&vi->rq[qp_index].napi);
	xdp_rxq_info_unreg(&vi->rq[qp_index].xdp_rxq);
}

static int virtnet_enable_queue_pair(struct virtnet_info *vi, int qp_index)
{
	struct net_device *dev = vi->dev;
	int err;

	err = xdp_rxq_info_reg(&vi->rq[qp_index].xdp_rxq, dev, qp_index,
			       vi->rq[qp_index].napi.napi_id);
	if (err < 0)
		return err;

	err = xdp_rxq_info_reg_mem_model(&vi->rq[qp_index].xdp_rxq,
					 MEM_TYPE_PAGE_SHARED, NULL);
	if (err < 0)
		goto err_xdp_reg_mem_model;

	virtnet_napi_enable(vi->rq[qp_index].vq, &vi->rq[qp_index].napi);
	virtnet_napi_tx_enable(vi, vi->sq[qp_index].vq, &vi->sq[qp_index].napi);

	return 0;

err_xdp_reg_mem_model:
	xdp_rxq_info_unreg(&vi->rq[qp_index].xdp_rxq);
	return err;
}

static int virtnet_open(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int i, err;

	enable_delayed_refill(vi);

	for (i = 0; i < vi->max_queue_pairs; i++) {
		if (i < vi->curr_queue_pairs)
			/* Make sure we have some buffers: if oom use wq. */
			if (!try_fill_recv(vi, &vi->rq[i], GFP_KERNEL))
				schedule_delayed_work(&vi->refill, 0);

		err = virtnet_enable_queue_pair(vi, i);
		if (err < 0)
			goto err_enable_qp;
	}

	return 0;

err_enable_qp:
	disable_delayed_refill(vi);
	cancel_delayed_work_sync(&vi->refill);

	for (i--; i >= 0; i--)
		virtnet_disable_queue_pair(vi, i);
	return err;
}

static int virtnet_poll_tx(struct napi_struct *napi, int budget)
{
	struct send_queue *sq = container_of(napi, struct send_queue, napi);
	struct virtnet_info *vi = sq->vq->vdev->priv;
	unsigned int index = vq2txq(sq->vq);
	struct netdev_queue *txq;
	int opaque;
	bool done;

	if (unlikely(is_xdp_raw_buffer_queue(vi, index))) {
		/* We don't need to enable cb for XDP */
		napi_complete_done(napi, 0);
		return 0;
	}

	txq = netdev_get_tx_queue(vi->dev, index);
	__netif_tx_lock(txq, raw_smp_processor_id());
	virtqueue_disable_cb(sq->vq);
	free_old_xmit_skbs(sq, true);

	if (sq->vq->num_free >= 2 + MAX_SKB_FRAGS)
		netif_tx_wake_queue(txq);

	opaque = virtqueue_enable_cb_prepare(sq->vq);

	done = napi_complete_done(napi, 0);

	if (!done)
		virtqueue_disable_cb(sq->vq);

	__netif_tx_unlock(txq);

	if (done) {
		if (unlikely(virtqueue_poll(sq->vq, opaque))) {
			if (napi_schedule_prep(napi)) {
				__netif_tx_lock(txq, raw_smp_processor_id());
				virtqueue_disable_cb(sq->vq);
				__netif_tx_unlock(txq);
				__napi_schedule(napi);
			}
		}
	}

	return 0;
}

static int xmit_skb(struct send_queue *sq, struct sk_buff *skb)
{
	struct virtio_net_hdr_mrg_rxbuf *hdr;
	const unsigned char *dest = ((struct ethhdr *)skb->data)->h_dest;
	struct virtnet_info *vi = sq->vq->vdev->priv;
	int num_sg;
	unsigned hdr_len = vi->hdr_len;
	bool can_push;

	pr_debug("%s: xmit %p %pM\n", vi->dev->name, skb, dest);

	can_push = vi->any_header_sg &&
		!((unsigned long)skb->data & (__alignof__(*hdr) - 1)) &&
		!skb_header_cloned(skb) && skb_headroom(skb) >= hdr_len;
	/* Even if we can, don't push here yet as this would skew
	 * csum_start offset below. */
	if (can_push)
		hdr = (struct virtio_net_hdr_mrg_rxbuf *)(skb->data - hdr_len);
	else
		hdr = &skb_vnet_common_hdr(skb)->mrg_hdr;

	if (virtio_net_hdr_from_skb(skb, &hdr->hdr,
				    virtio_is_little_endian(vi->vdev), false,
				    0))
		return -EPROTO;

	if (vi->mergeable_rx_bufs)
		hdr->num_buffers = 0;

	sg_init_table(sq->sg, skb_shinfo(skb)->nr_frags + (can_push ? 1 : 2));
	if (can_push) {
		__skb_push(skb, hdr_len);
		num_sg = skb_to_sgvec(skb, sq->sg, 0, skb->len);
		if (unlikely(num_sg < 0))
			return num_sg;
		/* Pull header back to avoid skew in tx bytes calculations. */
		__skb_pull(skb, hdr_len);
	} else {
		sg_set_buf(sq->sg, hdr, hdr_len);
		num_sg = skb_to_sgvec(skb, sq->sg + 1, 0, skb->len);
		if (unlikely(num_sg < 0))
			return num_sg;
		num_sg++;
	}
	return virtqueue_add_outbuf(sq->vq, sq->sg, num_sg, skb, GFP_ATOMIC);
}

static netdev_tx_t start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int qnum = skb_get_queue_mapping(skb);
	struct send_queue *sq = &vi->sq[qnum];
	int err;
	struct netdev_queue *txq = netdev_get_tx_queue(dev, qnum);
	bool kick = !netdev_xmit_more();
	bool use_napi = sq->napi.weight;

	/* Free up any pending old buffers before queueing new ones. */
	do {
		if (use_napi)
			virtqueue_disable_cb(sq->vq);

		free_old_xmit_skbs(sq, false);

	} while (use_napi && kick &&
	       unlikely(!virtqueue_enable_cb_delayed(sq->vq)));

	/* timestamp packet in software */
	skb_tx_timestamp(skb);

	/* Try to transmit */
	err = xmit_skb(sq, skb);

	/* This should not happen! */
	if (unlikely(err)) {
		DEV_STATS_INC(dev, tx_fifo_errors);
		if (net_ratelimit())
			dev_warn(&dev->dev,
				 "Unexpected TXQ (%d) queue failure: %d\n",
				 qnum, err);
		DEV_STATS_INC(dev, tx_dropped);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* Don't wait up for transmitted skbs to be freed. */
	if (!use_napi) {
		skb_orphan(skb);
		nf_reset_ct(skb);
	}

	check_sq_full_and_disable(vi, dev, sq);

	if (kick || netif_xmit_stopped(txq)) {
		if (virtqueue_kick_prepare(sq->vq) && virtqueue_notify(sq->vq)) {
			u64_stats_update_begin(&sq->stats.syncp);
			u64_stats_inc(&sq->stats.kicks);
			u64_stats_update_end(&sq->stats.syncp);
		}
	}

	return NETDEV_TX_OK;
}

static int virtnet_rx_resize(struct virtnet_info *vi,
			     struct receive_queue *rq, u32 ring_num)
{
	bool running = netif_running(vi->dev);
	int err, qindex;

	qindex = rq - vi->rq;

	if (running)
		napi_disable(&rq->napi);

	err = virtqueue_resize(rq->vq, ring_num, virtnet_rq_unmap_free_buf);
	if (err)
		netdev_err(vi->dev, "resize rx fail: rx queue index: %d err: %d\n", qindex, err);

	if (!try_fill_recv(vi, rq, GFP_KERNEL))
		schedule_delayed_work(&vi->refill, 0);

	if (running)
		virtnet_napi_enable(rq->vq, &rq->napi);
	return err;
}

static int virtnet_tx_resize(struct virtnet_info *vi,
			     struct send_queue *sq, u32 ring_num)
{
	bool running = netif_running(vi->dev);
	struct netdev_queue *txq;
	int err, qindex;

	qindex = sq - vi->sq;

	if (running)
		virtnet_napi_tx_disable(&sq->napi);

	txq = netdev_get_tx_queue(vi->dev, qindex);

	/* 1. wait all ximt complete
	 * 2. fix the race of netif_stop_subqueue() vs netif_start_subqueue()
	 */
	__netif_tx_lock_bh(txq);

	/* Prevent rx poll from accessing sq. */
	sq->reset = true;

	/* Prevent the upper layer from trying to send packets. */
	netif_stop_subqueue(vi->dev, qindex);

	__netif_tx_unlock_bh(txq);

	err = virtqueue_resize(sq->vq, ring_num, virtnet_sq_free_unused_buf);
	if (err)
		netdev_err(vi->dev, "resize tx fail: tx queue index: %d err: %d\n", qindex, err);

	__netif_tx_lock_bh(txq);
	sq->reset = false;
	netif_tx_wake_queue(txq);
	__netif_tx_unlock_bh(txq);

	if (running)
		virtnet_napi_tx_enable(vi, sq->vq, &sq->napi);
	return err;
}

/*
 * Send command via the control virtqueue and check status.  Commands
 * supported by the hypervisor, as indicated by feature bits, should
 * never fail unless improperly formatted.
 */
static bool virtnet_send_command(struct virtnet_info *vi, u8 class, u8 cmd,
				 struct scatterlist *out)
{
	struct scatterlist *sgs[4], hdr, stat;
	unsigned out_num = 0, tmp;
	int ret;

	/* Caller should know better */
	BUG_ON(!virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VQ));

	vi->ctrl->status = ~0;
	vi->ctrl->hdr.class = class;
	vi->ctrl->hdr.cmd = cmd;
	/* Add header */
	sg_init_one(&hdr, &vi->ctrl->hdr, sizeof(vi->ctrl->hdr));
	sgs[out_num++] = &hdr;

	if (out)
		sgs[out_num++] = out;

	/* Add return status. */
	sg_init_one(&stat, &vi->ctrl->status, sizeof(vi->ctrl->status));
	sgs[out_num] = &stat;

	BUG_ON(out_num + 1 > ARRAY_SIZE(sgs));
	ret = virtqueue_add_sgs(vi->cvq, sgs, out_num, 1, vi, GFP_ATOMIC);
	if (ret < 0) {
		dev_warn(&vi->vdev->dev,
			 "Failed to add sgs for command vq: %d\n.", ret);
		return false;
	}

	if (unlikely(!virtqueue_kick(vi->cvq)))
		return vi->ctrl->status == VIRTIO_NET_OK;

	/* Spin for a response, the kick causes an ioport write, trapping
	 * into the hypervisor, so the request should be handled immediately.
	 */
	while (!virtqueue_get_buf(vi->cvq, &tmp) &&
	       !virtqueue_is_broken(vi->cvq))
		cpu_relax();

	return vi->ctrl->status == VIRTIO_NET_OK;
}

static int virtnet_set_mac_address(struct net_device *dev, void *p)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct virtio_device *vdev = vi->vdev;
	int ret;
	struct sockaddr *addr;
	struct scatterlist sg;

	if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_STANDBY))
		return -EOPNOTSUPP;

	addr = kmemdup(p, sizeof(*addr), GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	ret = eth_prepare_mac_addr_change(dev, addr);
	if (ret)
		goto out;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_CTRL_MAC_ADDR)) {
		sg_init_one(&sg, addr->sa_data, dev->addr_len);
		if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_MAC,
					  VIRTIO_NET_CTRL_MAC_ADDR_SET, &sg)) {
			dev_warn(&vdev->dev,
				 "Failed to set mac address by vq command.\n");
			ret = -EINVAL;
			goto out;
		}
	} else if (virtio_has_feature(vdev, VIRTIO_NET_F_MAC) &&
		   !virtio_has_feature(vdev, VIRTIO_F_VERSION_1)) {
		unsigned int i;

		/* Naturally, this has an atomicity problem. */
		for (i = 0; i < dev->addr_len; i++)
			virtio_cwrite8(vdev,
				       offsetof(struct virtio_net_config, mac) +
				       i, addr->sa_data[i]);
	}

	eth_commit_mac_addr_change(dev, p);
	ret = 0;

out:
	kfree(addr);
	return ret;
}

static void virtnet_stats(struct net_device *dev,
			  struct rtnl_link_stats64 *tot)
{
	struct virtnet_info *vi = netdev_priv(dev);
	unsigned int start;
	int i;

	for (i = 0; i < vi->max_queue_pairs; i++) {
		u64 tpackets, tbytes, terrors, rpackets, rbytes, rdrops;
		struct receive_queue *rq = &vi->rq[i];
		struct send_queue *sq = &vi->sq[i];

		do {
			start = u64_stats_fetch_begin(&sq->stats.syncp);
			tpackets = u64_stats_read(&sq->stats.packets);
			tbytes   = u64_stats_read(&sq->stats.bytes);
			terrors  = u64_stats_read(&sq->stats.tx_timeouts);
		} while (u64_stats_fetch_retry(&sq->stats.syncp, start));

		do {
			start = u64_stats_fetch_begin(&rq->stats.syncp);
			rpackets = u64_stats_read(&rq->stats.packets);
			rbytes   = u64_stats_read(&rq->stats.bytes);
			rdrops   = u64_stats_read(&rq->stats.drops);
		} while (u64_stats_fetch_retry(&rq->stats.syncp, start));

		tot->rx_packets += rpackets;
		tot->tx_packets += tpackets;
		tot->rx_bytes   += rbytes;
		tot->tx_bytes   += tbytes;
		tot->rx_dropped += rdrops;
		tot->tx_errors  += terrors;
	}

	tot->tx_dropped = DEV_STATS_READ(dev, tx_dropped);
	tot->tx_fifo_errors = DEV_STATS_READ(dev, tx_fifo_errors);
	tot->rx_length_errors = DEV_STATS_READ(dev, rx_length_errors);
	tot->rx_frame_errors = DEV_STATS_READ(dev, rx_frame_errors);
}

static void virtnet_ack_link_announce(struct virtnet_info *vi)
{
	rtnl_lock();
	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_ANNOUNCE,
				  VIRTIO_NET_CTRL_ANNOUNCE_ACK, NULL))
		dev_warn(&vi->dev->dev, "Failed to ack link announce.\n");
	rtnl_unlock();
}

static int _virtnet_set_queues(struct virtnet_info *vi, u16 queue_pairs)
{
	struct scatterlist sg;
	struct net_device *dev = vi->dev;

	if (!vi->has_cvq || !virtio_has_feature(vi->vdev, VIRTIO_NET_F_MQ))
		return 0;

	vi->ctrl->mq.virtqueue_pairs = cpu_to_virtio16(vi->vdev, queue_pairs);
	sg_init_one(&sg, &vi->ctrl->mq, sizeof(vi->ctrl->mq));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_MQ,
				  VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET, &sg)) {
		dev_warn(&dev->dev, "Fail to set num of queue pairs to %d\n",
			 queue_pairs);
		return -EINVAL;
	} else {
		vi->curr_queue_pairs = queue_pairs;
		/* virtnet_open() will refill when device is going to up. */
		if (dev->flags & IFF_UP)
			schedule_delayed_work(&vi->refill, 0);
	}

	return 0;
}

static int virtnet_set_queues(struct virtnet_info *vi, u16 queue_pairs)
{
	int err;

	rtnl_lock();
	err = _virtnet_set_queues(vi, queue_pairs);
	rtnl_unlock();
	return err;
}

static int virtnet_close(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int i;

	/* Make sure NAPI doesn't schedule refill work */
	disable_delayed_refill(vi);
	/* Make sure refill_work doesn't re-enable napi! */
	cancel_delayed_work_sync(&vi->refill);

	for (i = 0; i < vi->max_queue_pairs; i++)
		virtnet_disable_queue_pair(vi, i);

	return 0;
}

static void virtnet_set_rx_mode(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct scatterlist sg[2];
	struct virtio_net_ctrl_mac *mac_data;
	struct netdev_hw_addr *ha;
	int uc_count;
	int mc_count;
	void *buf;
	int i;

	/* We can't dynamically set ndo_set_rx_mode, so return gracefully */
	if (!virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_RX))
		return;

	vi->ctrl->promisc = ((dev->flags & IFF_PROMISC) != 0);
	vi->ctrl->allmulti = ((dev->flags & IFF_ALLMULTI) != 0);

	sg_init_one(sg, &vi->ctrl->promisc, sizeof(vi->ctrl->promisc));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_RX,
				  VIRTIO_NET_CTRL_RX_PROMISC, sg))
		dev_warn(&dev->dev, "Failed to %sable promisc mode.\n",
			 vi->ctrl->promisc ? "en" : "dis");

	sg_init_one(sg, &vi->ctrl->allmulti, sizeof(vi->ctrl->allmulti));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_RX,
				  VIRTIO_NET_CTRL_RX_ALLMULTI, sg))
		dev_warn(&dev->dev, "Failed to %sable allmulti mode.\n",
			 vi->ctrl->allmulti ? "en" : "dis");

	uc_count = netdev_uc_count(dev);
	mc_count = netdev_mc_count(dev);
	/* MAC filter - use one buffer for both lists */
	buf = kzalloc(((uc_count + mc_count) * ETH_ALEN) +
		      (2 * sizeof(mac_data->entries)), GFP_ATOMIC);
	mac_data = buf;
	if (!buf)
		return;

	sg_init_table(sg, 2);

	/* Store the unicast list and count in the front of the buffer */
	mac_data->entries = cpu_to_virtio32(vi->vdev, uc_count);
	i = 0;
	netdev_for_each_uc_addr(ha, dev)
		memcpy(&mac_data->macs[i++][0], ha->addr, ETH_ALEN);

	sg_set_buf(&sg[0], mac_data,
		   sizeof(mac_data->entries) + (uc_count * ETH_ALEN));

	/* multicast list and count fill the end */
	mac_data = (void *)&mac_data->macs[uc_count][0];

	mac_data->entries = cpu_to_virtio32(vi->vdev, mc_count);
	i = 0;
	netdev_for_each_mc_addr(ha, dev)
		memcpy(&mac_data->macs[i++][0], ha->addr, ETH_ALEN);

	sg_set_buf(&sg[1], mac_data,
		   sizeof(mac_data->entries) + (mc_count * ETH_ALEN));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_MAC,
				  VIRTIO_NET_CTRL_MAC_TABLE_SET, sg))
		dev_warn(&dev->dev, "Failed to set MAC filter table.\n");

	kfree(buf);
}

static int virtnet_vlan_rx_add_vid(struct net_device *dev,
				   __be16 proto, u16 vid)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct scatterlist sg;

	vi->ctrl->vid = cpu_to_virtio16(vi->vdev, vid);
	sg_init_one(&sg, &vi->ctrl->vid, sizeof(vi->ctrl->vid));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_VLAN,
				  VIRTIO_NET_CTRL_VLAN_ADD, &sg))
		dev_warn(&dev->dev, "Failed to add VLAN ID %d.\n", vid);
	return 0;
}

static int virtnet_vlan_rx_kill_vid(struct net_device *dev,
				    __be16 proto, u16 vid)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct scatterlist sg;

	vi->ctrl->vid = cpu_to_virtio16(vi->vdev, vid);
	sg_init_one(&sg, &vi->ctrl->vid, sizeof(vi->ctrl->vid));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_VLAN,
				  VIRTIO_NET_CTRL_VLAN_DEL, &sg))
		dev_warn(&dev->dev, "Failed to kill VLAN ID %d.\n", vid);
	return 0;
}

static void virtnet_clean_affinity(struct virtnet_info *vi)
{
	int i;

	if (vi->affinity_hint_set) {
		for (i = 0; i < vi->max_queue_pairs; i++) {
			virtqueue_set_affinity(vi->rq[i].vq, NULL);
			virtqueue_set_affinity(vi->sq[i].vq, NULL);
		}

		vi->affinity_hint_set = false;
	}
}

static void virtnet_set_affinity(struct virtnet_info *vi)
{
	cpumask_var_t mask;
	int stragglers;
	int group_size;
	int i, j, cpu;
	int num_cpu;
	int stride;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL)) {
		virtnet_clean_affinity(vi);
		return;
	}

	num_cpu = num_online_cpus();
	stride = max_t(int, num_cpu / vi->curr_queue_pairs, 1);
	stragglers = num_cpu >= vi->curr_queue_pairs ?
			num_cpu % vi->curr_queue_pairs :
			0;
	cpu = cpumask_first(cpu_online_mask);

	for (i = 0; i < vi->curr_queue_pairs; i++) {
		group_size = stride + (i < stragglers ? 1 : 0);

		for (j = 0; j < group_size; j++) {
			cpumask_set_cpu(cpu, mask);
			cpu = cpumask_next_wrap(cpu, cpu_online_mask,
						nr_cpu_ids, false);
		}
		virtqueue_set_affinity(vi->rq[i].vq, mask);
		virtqueue_set_affinity(vi->sq[i].vq, mask);
		__netif_set_xps_queue(vi->dev, cpumask_bits(mask), i, XPS_CPUS);
		cpumask_clear(mask);
	}

	vi->affinity_hint_set = true;
	free_cpumask_var(mask);
}

static int virtnet_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct virtnet_info *vi = hlist_entry_safe(node, struct virtnet_info,
						   node);
	virtnet_set_affinity(vi);
	return 0;
}

static int virtnet_cpu_dead(unsigned int cpu, struct hlist_node *node)
{
	struct virtnet_info *vi = hlist_entry_safe(node, struct virtnet_info,
						   node_dead);
	virtnet_set_affinity(vi);
	return 0;
}

static int virtnet_cpu_down_prep(unsigned int cpu, struct hlist_node *node)
{
	struct virtnet_info *vi = hlist_entry_safe(node, struct virtnet_info,
						   node);

	virtnet_clean_affinity(vi);
	return 0;
}

static enum cpuhp_state virtionet_online;

static int virtnet_cpu_notif_add(struct virtnet_info *vi)
{
	int ret;

	ret = cpuhp_state_add_instance_nocalls(virtionet_online, &vi->node);
	if (ret)
		return ret;
	ret = cpuhp_state_add_instance_nocalls(CPUHP_VIRT_NET_DEAD,
					       &vi->node_dead);
	if (!ret)
		return ret;
	cpuhp_state_remove_instance_nocalls(virtionet_online, &vi->node);
	return ret;
}

static void virtnet_cpu_notif_remove(struct virtnet_info *vi)
{
	cpuhp_state_remove_instance_nocalls(virtionet_online, &vi->node);
	cpuhp_state_remove_instance_nocalls(CPUHP_VIRT_NET_DEAD,
					    &vi->node_dead);
}

static void virtnet_get_ringparam(struct net_device *dev,
				  struct ethtool_ringparam *ring,
				  struct kernel_ethtool_ringparam *kernel_ring,
				  struct netlink_ext_ack *extack)
{
	struct virtnet_info *vi = netdev_priv(dev);

	ring->rx_max_pending = vi->rq[0].vq->num_max;
	ring->tx_max_pending = vi->sq[0].vq->num_max;
	ring->rx_pending = virtqueue_get_vring_size(vi->rq[0].vq);
	ring->tx_pending = virtqueue_get_vring_size(vi->sq[0].vq);
}

static int virtnet_send_ctrl_coal_vq_cmd(struct virtnet_info *vi,
					 u16 vqn, u32 max_usecs, u32 max_packets);

static int virtnet_set_ringparam(struct net_device *dev,
				 struct ethtool_ringparam *ring,
				 struct kernel_ethtool_ringparam *kernel_ring,
				 struct netlink_ext_ack *extack)
{
	struct virtnet_info *vi = netdev_priv(dev);
	u32 rx_pending, tx_pending;
	struct receive_queue *rq;
	struct send_queue *sq;
	int i, err;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	rx_pending = virtqueue_get_vring_size(vi->rq[0].vq);
	tx_pending = virtqueue_get_vring_size(vi->sq[0].vq);

	if (ring->rx_pending == rx_pending &&
	    ring->tx_pending == tx_pending)
		return 0;

	if (ring->rx_pending > vi->rq[0].vq->num_max)
		return -EINVAL;

	if (ring->tx_pending > vi->sq[0].vq->num_max)
		return -EINVAL;

	for (i = 0; i < vi->max_queue_pairs; i++) {
		rq = vi->rq + i;
		sq = vi->sq + i;

		if (ring->tx_pending != tx_pending) {
			err = virtnet_tx_resize(vi, sq, ring->tx_pending);
			if (err)
				return err;

			/* Upon disabling and re-enabling a transmit virtqueue, the device must
			 * set the coalescing parameters of the virtqueue to those configured
			 * through the VIRTIO_NET_CTRL_NOTF_COAL_TX_SET command, or, if the driver
			 * did not set any TX coalescing parameters, to 0.
			 */
			err = virtnet_send_ctrl_coal_vq_cmd(vi, txq2vq(i),
							    vi->intr_coal_tx.max_usecs,
							    vi->intr_coal_tx.max_packets);
			if (err)
				return err;

			vi->sq[i].intr_coal.max_usecs = vi->intr_coal_tx.max_usecs;
			vi->sq[i].intr_coal.max_packets = vi->intr_coal_tx.max_packets;
		}

		if (ring->rx_pending != rx_pending) {
			err = virtnet_rx_resize(vi, rq, ring->rx_pending);
			if (err)
				return err;

			/* The reason is same as the transmit virtqueue reset */
			err = virtnet_send_ctrl_coal_vq_cmd(vi, rxq2vq(i),
							    vi->intr_coal_rx.max_usecs,
							    vi->intr_coal_rx.max_packets);
			if (err)
				return err;

			vi->rq[i].intr_coal.max_usecs = vi->intr_coal_rx.max_usecs;
			vi->rq[i].intr_coal.max_packets = vi->intr_coal_rx.max_packets;
		}
	}

	return 0;
}

static bool virtnet_commit_rss_command(struct virtnet_info *vi)
{
	struct net_device *dev = vi->dev;
	struct scatterlist sgs[4];
	unsigned int sg_buf_size;

	/* prepare sgs */
	sg_init_table(sgs, 4);

	sg_buf_size = offsetof(struct virtio_net_ctrl_rss, indirection_table);
	sg_set_buf(&sgs[0], &vi->ctrl->rss, sg_buf_size);

	sg_buf_size = sizeof(uint16_t) * (vi->ctrl->rss.indirection_table_mask + 1);
	sg_set_buf(&sgs[1], vi->ctrl->rss.indirection_table, sg_buf_size);

	sg_buf_size = offsetof(struct virtio_net_ctrl_rss, key)
			- offsetof(struct virtio_net_ctrl_rss, max_tx_vq);
	sg_set_buf(&sgs[2], &vi->ctrl->rss.max_tx_vq, sg_buf_size);

	sg_buf_size = vi->rss_key_size;
	sg_set_buf(&sgs[3], vi->ctrl->rss.key, sg_buf_size);

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_MQ,
				  vi->has_rss ? VIRTIO_NET_CTRL_MQ_RSS_CONFIG
				  : VIRTIO_NET_CTRL_MQ_HASH_CONFIG, sgs)) {
		dev_warn(&dev->dev, "VIRTIONET issue with committing RSS sgs\n");
		return false;
	}
	return true;
}

static void virtnet_init_default_rss(struct virtnet_info *vi)
{
	u32 indir_val = 0;
	int i = 0;

	vi->ctrl->rss.hash_types = vi->rss_hash_types_supported;
	vi->rss_hash_types_saved = vi->rss_hash_types_supported;
	vi->ctrl->rss.indirection_table_mask = vi->rss_indir_table_size
						? vi->rss_indir_table_size - 1 : 0;
	vi->ctrl->rss.unclassified_queue = 0;

	for (; i < vi->rss_indir_table_size; ++i) {
		indir_val = ethtool_rxfh_indir_default(i, vi->curr_queue_pairs);
		vi->ctrl->rss.indirection_table[i] = indir_val;
	}

	vi->ctrl->rss.max_tx_vq = vi->has_rss ? vi->curr_queue_pairs : 0;
	vi->ctrl->rss.hash_key_length = vi->rss_key_size;

	netdev_rss_key_fill(vi->ctrl->rss.key, vi->rss_key_size);
}

static void virtnet_get_hashflow(const struct virtnet_info *vi, struct ethtool_rxnfc *info)
{
	info->data = 0;
	switch (info->flow_type) {
	case TCP_V4_FLOW:
		if (vi->rss_hash_types_saved & VIRTIO_NET_RSS_HASH_TYPE_TCPv4) {
			info->data = RXH_IP_SRC | RXH_IP_DST |
						 RXH_L4_B_0_1 | RXH_L4_B_2_3;
		} else if (vi->rss_hash_types_saved & VIRTIO_NET_RSS_HASH_TYPE_IPv4) {
			info->data = RXH_IP_SRC | RXH_IP_DST;
		}
		break;
	case TCP_V6_FLOW:
		if (vi->rss_hash_types_saved & VIRTIO_NET_RSS_HASH_TYPE_TCPv6) {
			info->data = RXH_IP_SRC | RXH_IP_DST |
						 RXH_L4_B_0_1 | RXH_L4_B_2_3;
		} else if (vi->rss_hash_types_saved & VIRTIO_NET_RSS_HASH_TYPE_IPv6) {
			info->data = RXH_IP_SRC | RXH_IP_DST;
		}
		break;
	case UDP_V4_FLOW:
		if (vi->rss_hash_types_saved & VIRTIO_NET_RSS_HASH_TYPE_UDPv4) {
			info->data = RXH_IP_SRC | RXH_IP_DST |
						 RXH_L4_B_0_1 | RXH_L4_B_2_3;
		} else if (vi->rss_hash_types_saved & VIRTIO_NET_RSS_HASH_TYPE_IPv4) {
			info->data = RXH_IP_SRC | RXH_IP_DST;
		}
		break;
	case UDP_V6_FLOW:
		if (vi->rss_hash_types_saved & VIRTIO_NET_RSS_HASH_TYPE_UDPv6) {
			info->data = RXH_IP_SRC | RXH_IP_DST |
						 RXH_L4_B_0_1 | RXH_L4_B_2_3;
		} else if (vi->rss_hash_types_saved & VIRTIO_NET_RSS_HASH_TYPE_IPv6) {
			info->data = RXH_IP_SRC | RXH_IP_DST;
		}
		break;
	case IPV4_FLOW:
		if (vi->rss_hash_types_saved & VIRTIO_NET_RSS_HASH_TYPE_IPv4)
			info->data = RXH_IP_SRC | RXH_IP_DST;

		break;
	case IPV6_FLOW:
		if (vi->rss_hash_types_saved & VIRTIO_NET_RSS_HASH_TYPE_IPv6)
			info->data = RXH_IP_SRC | RXH_IP_DST;

		break;
	default:
		info->data = 0;
		break;
	}
}

static bool virtnet_set_hashflow(struct virtnet_info *vi, struct ethtool_rxnfc *info)
{
	u32 new_hashtypes = vi->rss_hash_types_saved;
	bool is_disable = info->data & RXH_DISCARD;
	bool is_l4 = info->data == (RXH_IP_SRC | RXH_IP_DST | RXH_L4_B_0_1 | RXH_L4_B_2_3);

	/* supports only 'sd', 'sdfn' and 'r' */
	if (!((info->data == (RXH_IP_SRC | RXH_IP_DST)) | is_l4 | is_disable))
		return false;

	switch (info->flow_type) {
	case TCP_V4_FLOW:
		new_hashtypes &= ~(VIRTIO_NET_RSS_HASH_TYPE_IPv4 | VIRTIO_NET_RSS_HASH_TYPE_TCPv4);
		if (!is_disable)
			new_hashtypes |= VIRTIO_NET_RSS_HASH_TYPE_IPv4
				| (is_l4 ? VIRTIO_NET_RSS_HASH_TYPE_TCPv4 : 0);
		break;
	case UDP_V4_FLOW:
		new_hashtypes &= ~(VIRTIO_NET_RSS_HASH_TYPE_IPv4 | VIRTIO_NET_RSS_HASH_TYPE_UDPv4);
		if (!is_disable)
			new_hashtypes |= VIRTIO_NET_RSS_HASH_TYPE_IPv4
				| (is_l4 ? VIRTIO_NET_RSS_HASH_TYPE_UDPv4 : 0);
		break;
	case IPV4_FLOW:
		new_hashtypes &= ~VIRTIO_NET_RSS_HASH_TYPE_IPv4;
		if (!is_disable)
			new_hashtypes = VIRTIO_NET_RSS_HASH_TYPE_IPv4;
		break;
	case TCP_V6_FLOW:
		new_hashtypes &= ~(VIRTIO_NET_RSS_HASH_TYPE_IPv6 | VIRTIO_NET_RSS_HASH_TYPE_TCPv6);
		if (!is_disable)
			new_hashtypes |= VIRTIO_NET_RSS_HASH_TYPE_IPv6
				| (is_l4 ? VIRTIO_NET_RSS_HASH_TYPE_TCPv6 : 0);
		break;
	case UDP_V6_FLOW:
		new_hashtypes &= ~(VIRTIO_NET_RSS_HASH_TYPE_IPv6 | VIRTIO_NET_RSS_HASH_TYPE_UDPv6);
		if (!is_disable)
			new_hashtypes |= VIRTIO_NET_RSS_HASH_TYPE_IPv6
				| (is_l4 ? VIRTIO_NET_RSS_HASH_TYPE_UDPv6 : 0);
		break;
	case IPV6_FLOW:
		new_hashtypes &= ~VIRTIO_NET_RSS_HASH_TYPE_IPv6;
		if (!is_disable)
			new_hashtypes = VIRTIO_NET_RSS_HASH_TYPE_IPv6;
		break;
	default:
		/* unsupported flow */
		return false;
	}

	/* if unsupported hashtype was set */
	if (new_hashtypes != (new_hashtypes & vi->rss_hash_types_supported))
		return false;

	if (new_hashtypes != vi->rss_hash_types_saved) {
		vi->rss_hash_types_saved = new_hashtypes;
		vi->ctrl->rss.hash_types = vi->rss_hash_types_saved;
		if (vi->dev->features & NETIF_F_RXHASH)
			return virtnet_commit_rss_command(vi);
	}

	return true;
}

static void virtnet_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct virtio_device *vdev = vi->vdev;

	strscpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strscpy(info->version, VIRTNET_DRIVER_VERSION, sizeof(info->version));
	strscpy(info->bus_info, virtio_bus_name(vdev), sizeof(info->bus_info));

}

/* TODO: Eliminate OOO packets during switching */
static int virtnet_set_channels(struct net_device *dev,
				struct ethtool_channels *channels)
{
	struct virtnet_info *vi = netdev_priv(dev);
	u16 queue_pairs = channels->combined_count;
	int err;

	/* We don't support separate rx/tx channels.
	 * We don't allow setting 'other' channels.
	 */
	if (channels->rx_count || channels->tx_count || channels->other_count)
		return -EINVAL;

	if (queue_pairs > vi->max_queue_pairs || queue_pairs == 0)
		return -EINVAL;

	/* For now we don't support modifying channels while XDP is loaded
	 * also when XDP is loaded all RX queues have XDP programs so we only
	 * need to check a single RX queue.
	 */
	if (vi->rq[0].xdp_prog)
		return -EINVAL;

	cpus_read_lock();
	err = _virtnet_set_queues(vi, queue_pairs);
	if (err) {
		cpus_read_unlock();
		goto err;
	}
	virtnet_set_affinity(vi);
	cpus_read_unlock();

	netif_set_real_num_tx_queues(dev, queue_pairs);
	netif_set_real_num_rx_queues(dev, queue_pairs);
 err:
	return err;
}

static void virtnet_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	struct virtnet_info *vi = netdev_priv(dev);
	unsigned int i, j;
	u8 *p = data;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < vi->curr_queue_pairs; i++) {
			for (j = 0; j < VIRTNET_RQ_STATS_LEN; j++)
				ethtool_sprintf(&p, "rx_queue_%u_%s", i,
						virtnet_rq_stats_desc[j].desc);
		}

		for (i = 0; i < vi->curr_queue_pairs; i++) {
			for (j = 0; j < VIRTNET_SQ_STATS_LEN; j++)
				ethtool_sprintf(&p, "tx_queue_%u_%s", i,
						virtnet_sq_stats_desc[j].desc);
		}
		break;
	}
}

static int virtnet_get_sset_count(struct net_device *dev, int sset)
{
	struct virtnet_info *vi = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		return vi->curr_queue_pairs * (VIRTNET_RQ_STATS_LEN +
					       VIRTNET_SQ_STATS_LEN);
	default:
		return -EOPNOTSUPP;
	}
}

static void virtnet_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats, u64 *data)
{
	struct virtnet_info *vi = netdev_priv(dev);
	unsigned int idx = 0, start, i, j;
	const u8 *stats_base;
	const u64_stats_t *p;
	size_t offset;

	for (i = 0; i < vi->curr_queue_pairs; i++) {
		struct receive_queue *rq = &vi->rq[i];

		stats_base = (const u8 *)&rq->stats;
		do {
			start = u64_stats_fetch_begin(&rq->stats.syncp);
			for (j = 0; j < VIRTNET_RQ_STATS_LEN; j++) {
				offset = virtnet_rq_stats_desc[j].offset;
				p = (const u64_stats_t *)(stats_base + offset);
				data[idx + j] = u64_stats_read(p);
			}
		} while (u64_stats_fetch_retry(&rq->stats.syncp, start));
		idx += VIRTNET_RQ_STATS_LEN;
	}

	for (i = 0; i < vi->curr_queue_pairs; i++) {
		struct send_queue *sq = &vi->sq[i];

		stats_base = (const u8 *)&sq->stats;
		do {
			start = u64_stats_fetch_begin(&sq->stats.syncp);
			for (j = 0; j < VIRTNET_SQ_STATS_LEN; j++) {
				offset = virtnet_sq_stats_desc[j].offset;
				p = (const u64_stats_t *)(stats_base + offset);
				data[idx + j] = u64_stats_read(p);
			}
		} while (u64_stats_fetch_retry(&sq->stats.syncp, start));
		idx += VIRTNET_SQ_STATS_LEN;
	}
}

static void virtnet_get_channels(struct net_device *dev,
				 struct ethtool_channels *channels)
{
	struct virtnet_info *vi = netdev_priv(dev);

	channels->combined_count = vi->curr_queue_pairs;
	channels->max_combined = vi->max_queue_pairs;
	channels->max_other = 0;
	channels->rx_count = 0;
	channels->tx_count = 0;
	channels->other_count = 0;
}

static int virtnet_set_link_ksettings(struct net_device *dev,
				      const struct ethtool_link_ksettings *cmd)
{
	struct virtnet_info *vi = netdev_priv(dev);

	return ethtool_virtdev_set_link_ksettings(dev, cmd,
						  &vi->speed, &vi->duplex);
}

static int virtnet_get_link_ksettings(struct net_device *dev,
				      struct ethtool_link_ksettings *cmd)
{
	struct virtnet_info *vi = netdev_priv(dev);

	cmd->base.speed = vi->speed;
	cmd->base.duplex = vi->duplex;
	cmd->base.port = PORT_OTHER;

	return 0;
}

static int virtnet_send_notf_coal_cmds(struct virtnet_info *vi,
				       struct ethtool_coalesce *ec)
{
	struct scatterlist sgs_tx, sgs_rx;
	int i;

	vi->ctrl->coal_tx.tx_usecs = cpu_to_le32(ec->tx_coalesce_usecs);
	vi->ctrl->coal_tx.tx_max_packets = cpu_to_le32(ec->tx_max_coalesced_frames);
	sg_init_one(&sgs_tx, &vi->ctrl->coal_tx, sizeof(vi->ctrl->coal_tx));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_NOTF_COAL,
				  VIRTIO_NET_CTRL_NOTF_COAL_TX_SET,
				  &sgs_tx))
		return -EINVAL;

	/* Save parameters */
	vi->intr_coal_tx.max_usecs = ec->tx_coalesce_usecs;
	vi->intr_coal_tx.max_packets = ec->tx_max_coalesced_frames;
	for (i = 0; i < vi->max_queue_pairs; i++) {
		vi->sq[i].intr_coal.max_usecs = ec->tx_coalesce_usecs;
		vi->sq[i].intr_coal.max_packets = ec->tx_max_coalesced_frames;
	}

	vi->ctrl->coal_rx.rx_usecs = cpu_to_le32(ec->rx_coalesce_usecs);
	vi->ctrl->coal_rx.rx_max_packets = cpu_to_le32(ec->rx_max_coalesced_frames);
	sg_init_one(&sgs_rx, &vi->ctrl->coal_rx, sizeof(vi->ctrl->coal_rx));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_NOTF_COAL,
				  VIRTIO_NET_CTRL_NOTF_COAL_RX_SET,
				  &sgs_rx))
		return -EINVAL;

	/* Save parameters */
	vi->intr_coal_rx.max_usecs = ec->rx_coalesce_usecs;
	vi->intr_coal_rx.max_packets = ec->rx_max_coalesced_frames;
	for (i = 0; i < vi->max_queue_pairs; i++) {
		vi->rq[i].intr_coal.max_usecs = ec->rx_coalesce_usecs;
		vi->rq[i].intr_coal.max_packets = ec->rx_max_coalesced_frames;
	}

	return 0;
}

static int virtnet_send_ctrl_coal_vq_cmd(struct virtnet_info *vi,
					 u16 vqn, u32 max_usecs, u32 max_packets)
{
	struct scatterlist sgs;

	vi->ctrl->coal_vq.vqn = cpu_to_le16(vqn);
	vi->ctrl->coal_vq.coal.max_usecs = cpu_to_le32(max_usecs);
	vi->ctrl->coal_vq.coal.max_packets = cpu_to_le32(max_packets);
	sg_init_one(&sgs, &vi->ctrl->coal_vq, sizeof(vi->ctrl->coal_vq));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_NOTF_COAL,
				  VIRTIO_NET_CTRL_NOTF_COAL_VQ_SET,
				  &sgs))
		return -EINVAL;

	return 0;
}

static int virtnet_send_notf_coal_vq_cmds(struct virtnet_info *vi,
					  struct ethtool_coalesce *ec,
					  u16 queue)
{
	int err;

	err = virtnet_send_ctrl_coal_vq_cmd(vi, rxq2vq(queue),
					    ec->rx_coalesce_usecs,
					    ec->rx_max_coalesced_frames);
	if (err)
		return err;

	vi->rq[queue].intr_coal.max_usecs = ec->rx_coalesce_usecs;
	vi->rq[queue].intr_coal.max_packets = ec->rx_max_coalesced_frames;

	err = virtnet_send_ctrl_coal_vq_cmd(vi, txq2vq(queue),
					    ec->tx_coalesce_usecs,
					    ec->tx_max_coalesced_frames);
	if (err)
		return err;

	vi->sq[queue].intr_coal.max_usecs = ec->tx_coalesce_usecs;
	vi->sq[queue].intr_coal.max_packets = ec->tx_max_coalesced_frames;

	return 0;
}

static int virtnet_coal_params_supported(struct ethtool_coalesce *ec)
{
	/* usecs coalescing is supported only if VIRTIO_NET_F_NOTF_COAL
	 * feature is negotiated.
	 */
	if (ec->rx_coalesce_usecs || ec->tx_coalesce_usecs)
		return -EOPNOTSUPP;

	if (ec->tx_max_coalesced_frames > 1 ||
	    ec->rx_max_coalesced_frames != 1)
		return -EINVAL;

	return 0;
}

static int virtnet_should_update_vq_weight(int dev_flags, int weight,
					   int vq_weight, bool *should_update)
{
	if (weight ^ vq_weight) {
		if (dev_flags & IFF_UP)
			return -EBUSY;
		*should_update = true;
	}

	return 0;
}

static int virtnet_set_coalesce(struct net_device *dev,
				struct ethtool_coalesce *ec,
				struct kernel_ethtool_coalesce *kernel_coal,
				struct netlink_ext_ack *extack)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int ret, queue_number, napi_weight;
	bool update_napi = false;

	/* Can't change NAPI weight if the link is up */
	napi_weight = ec->tx_max_coalesced_frames ? NAPI_POLL_WEIGHT : 0;
	for (queue_number = 0; queue_number < vi->max_queue_pairs; queue_number++) {
		ret = virtnet_should_update_vq_weight(dev->flags, napi_weight,
						      vi->sq[queue_number].napi.weight,
						      &update_napi);
		if (ret)
			return ret;

		if (update_napi) {
			/* All queues that belong to [queue_number, vi->max_queue_pairs] will be
			 * updated for the sake of simplicity, which might not be necessary
			 */
			break;
		}
	}

	if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_NOTF_COAL))
		ret = virtnet_send_notf_coal_cmds(vi, ec);
	else
		ret = virtnet_coal_params_supported(ec);

	if (ret)
		return ret;

	if (update_napi) {
		for (; queue_number < vi->max_queue_pairs; queue_number++)
			vi->sq[queue_number].napi.weight = napi_weight;
	}

	return ret;
}

static int virtnet_get_coalesce(struct net_device *dev,
				struct ethtool_coalesce *ec,
				struct kernel_ethtool_coalesce *kernel_coal,
				struct netlink_ext_ack *extack)
{
	struct virtnet_info *vi = netdev_priv(dev);

	if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_NOTF_COAL)) {
		ec->rx_coalesce_usecs = vi->intr_coal_rx.max_usecs;
		ec->tx_coalesce_usecs = vi->intr_coal_tx.max_usecs;
		ec->tx_max_coalesced_frames = vi->intr_coal_tx.max_packets;
		ec->rx_max_coalesced_frames = vi->intr_coal_rx.max_packets;
	} else {
		ec->rx_max_coalesced_frames = 1;

		if (vi->sq[0].napi.weight)
			ec->tx_max_coalesced_frames = 1;
	}

	return 0;
}

static int virtnet_set_per_queue_coalesce(struct net_device *dev,
					  u32 queue,
					  struct ethtool_coalesce *ec)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int ret, napi_weight;
	bool update_napi = false;

	if (queue >= vi->max_queue_pairs)
		return -EINVAL;

	/* Can't change NAPI weight if the link is up */
	napi_weight = ec->tx_max_coalesced_frames ? NAPI_POLL_WEIGHT : 0;
	ret = virtnet_should_update_vq_weight(dev->flags, napi_weight,
					      vi->sq[queue].napi.weight,
					      &update_napi);
	if (ret)
		return ret;

	if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_VQ_NOTF_COAL))
		ret = virtnet_send_notf_coal_vq_cmds(vi, ec, queue);
	else
		ret = virtnet_coal_params_supported(ec);

	if (ret)
		return ret;

	if (update_napi)
		vi->sq[queue].napi.weight = napi_weight;

	return 0;
}

static int virtnet_get_per_queue_coalesce(struct net_device *dev,
					  u32 queue,
					  struct ethtool_coalesce *ec)
{
	struct virtnet_info *vi = netdev_priv(dev);

	if (queue >= vi->max_queue_pairs)
		return -EINVAL;

	if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_VQ_NOTF_COAL)) {
		ec->rx_coalesce_usecs = vi->rq[queue].intr_coal.max_usecs;
		ec->tx_coalesce_usecs = vi->sq[queue].intr_coal.max_usecs;
		ec->tx_max_coalesced_frames = vi->sq[queue].intr_coal.max_packets;
		ec->rx_max_coalesced_frames = vi->rq[queue].intr_coal.max_packets;
	} else {
		ec->rx_max_coalesced_frames = 1;

		if (vi->sq[queue].napi.weight)
			ec->tx_max_coalesced_frames = 1;
	}

	return 0;
}

static void virtnet_init_settings(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);

	vi->speed = SPEED_UNKNOWN;
	vi->duplex = DUPLEX_UNKNOWN;
}

static void virtnet_update_settings(struct virtnet_info *vi)
{
	u32 speed;
	u8 duplex;

	if (!virtio_has_feature(vi->vdev, VIRTIO_NET_F_SPEED_DUPLEX))
		return;

	virtio_cread_le(vi->vdev, struct virtio_net_config, speed, &speed);

	if (ethtool_validate_speed(speed))
		vi->speed = speed;

	virtio_cread_le(vi->vdev, struct virtio_net_config, duplex, &duplex);

	if (ethtool_validate_duplex(duplex))
		vi->duplex = duplex;
}

static u32 virtnet_get_rxfh_key_size(struct net_device *dev)
{
	return ((struct virtnet_info *)netdev_priv(dev))->rss_key_size;
}

static u32 virtnet_get_rxfh_indir_size(struct net_device *dev)
{
	return ((struct virtnet_info *)netdev_priv(dev))->rss_indir_table_size;
}

static int virtnet_get_rxfh(struct net_device *dev, u32 *indir, u8 *key, u8 *hfunc)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int i;

	if (indir) {
		for (i = 0; i < vi->rss_indir_table_size; ++i)
			indir[i] = vi->ctrl->rss.indirection_table[i];
	}

	if (key)
		memcpy(key, vi->ctrl->rss.key, vi->rss_key_size);

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	return 0;
}

static int virtnet_set_rxfh(struct net_device *dev, const u32 *indir, const u8 *key, const u8 hfunc)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int i;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (indir) {
		for (i = 0; i < vi->rss_indir_table_size; ++i)
			vi->ctrl->rss.indirection_table[i] = indir[i];
	}
	if (key)
		memcpy(vi->ctrl->rss.key, key, vi->rss_key_size);

	virtnet_commit_rss_command(vi);

	return 0;
}

static int virtnet_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info, u32 *rule_locs)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int rc = 0;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = vi->curr_queue_pairs;
		break;
	case ETHTOOL_GRXFH:
		virtnet_get_hashflow(vi, info);
		break;
	default:
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static int virtnet_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int rc = 0;

	switch (info->cmd) {
	case ETHTOOL_SRXFH:
		if (!virtnet_set_hashflow(vi, info))
			rc = -EINVAL;

		break;
	default:
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static const struct ethtool_ops virtnet_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_MAX_FRAMES |
		ETHTOOL_COALESCE_USECS,
	.get_drvinfo = virtnet_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_ringparam = virtnet_get_ringparam,
	.set_ringparam = virtnet_set_ringparam,
	.get_strings = virtnet_get_strings,
	.get_sset_count = virtnet_get_sset_count,
	.get_ethtool_stats = virtnet_get_ethtool_stats,
	.set_channels = virtnet_set_channels,
	.get_channels = virtnet_get_channels,
	.get_ts_info = ethtool_op_get_ts_info,
	.get_link_ksettings = virtnet_get_link_ksettings,
	.set_link_ksettings = virtnet_set_link_ksettings,
	.set_coalesce = virtnet_set_coalesce,
	.get_coalesce = virtnet_get_coalesce,
	.set_per_queue_coalesce = virtnet_set_per_queue_coalesce,
	.get_per_queue_coalesce = virtnet_get_per_queue_coalesce,
	.get_rxfh_key_size = virtnet_get_rxfh_key_size,
	.get_rxfh_indir_size = virtnet_get_rxfh_indir_size,
	.get_rxfh = virtnet_get_rxfh,
	.set_rxfh = virtnet_set_rxfh,
	.get_rxnfc = virtnet_get_rxnfc,
	.set_rxnfc = virtnet_set_rxnfc,
};

static void virtnet_freeze_down(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;

	/* Make sure no work handler is accessing the device */
	flush_work(&vi->config_work);

	netif_tx_lock_bh(vi->dev);
	netif_device_detach(vi->dev);
	netif_tx_unlock_bh(vi->dev);
	if (netif_running(vi->dev))
		virtnet_close(vi->dev);
}

static int init_vqs(struct virtnet_info *vi);

static int virtnet_restore_up(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;
	int err;

	err = init_vqs(vi);
	if (err)
		return err;

	virtio_device_ready(vdev);

	enable_delayed_refill(vi);

	if (netif_running(vi->dev)) {
		err = virtnet_open(vi->dev);
		if (err)
			return err;
	}

	netif_tx_lock_bh(vi->dev);
	netif_device_attach(vi->dev);
	netif_tx_unlock_bh(vi->dev);
	return err;
}

static int virtnet_set_guest_offloads(struct virtnet_info *vi, u64 offloads)
{
	struct scatterlist sg;
	vi->ctrl->offloads = cpu_to_virtio64(vi->vdev, offloads);

	sg_init_one(&sg, &vi->ctrl->offloads, sizeof(vi->ctrl->offloads));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_GUEST_OFFLOADS,
				  VIRTIO_NET_CTRL_GUEST_OFFLOADS_SET, &sg)) {
		dev_warn(&vi->dev->dev, "Fail to set guest offload.\n");
		return -EINVAL;
	}

	return 0;
}

static int virtnet_clear_guest_offloads(struct virtnet_info *vi)
{
	u64 offloads = 0;

	if (!vi->guest_offloads)
		return 0;

	return virtnet_set_guest_offloads(vi, offloads);
}

static int virtnet_restore_guest_offloads(struct virtnet_info *vi)
{
	u64 offloads = vi->guest_offloads;

	if (!vi->guest_offloads)
		return 0;

	return virtnet_set_guest_offloads(vi, offloads);
}

static int virtnet_xdp_set(struct net_device *dev, struct bpf_prog *prog,
			   struct netlink_ext_ack *extack)
{
	unsigned int room = SKB_DATA_ALIGN(VIRTIO_XDP_HEADROOM +
					   sizeof(struct skb_shared_info));
	unsigned int max_sz = PAGE_SIZE - room - ETH_HLEN;
	struct virtnet_info *vi = netdev_priv(dev);
	struct bpf_prog *old_prog;
	u16 xdp_qp = 0, curr_qp;
	int i, err;

	if (!virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_GUEST_OFFLOADS)
	    && (virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_TSO4) ||
	        virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_TSO6) ||
	        virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_ECN) ||
		virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_UFO) ||
		virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_CSUM) ||
		virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_USO4) ||
		virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_USO6))) {
		NL_SET_ERR_MSG_MOD(extack, "Can't set XDP while host is implementing GRO_HW/CSUM, disable GRO_HW/CSUM first");
		return -EOPNOTSUPP;
	}

	if (vi->mergeable_rx_bufs && !vi->any_header_sg) {
		NL_SET_ERR_MSG_MOD(extack, "XDP expects header/data in single page, any_header_sg required");
		return -EINVAL;
	}

	if (prog && !prog->aux->xdp_has_frags && dev->mtu > max_sz) {
		NL_SET_ERR_MSG_MOD(extack, "MTU too large to enable XDP without frags");
		netdev_warn(dev, "single-buffer XDP requires MTU less than %u\n", max_sz);
		return -EINVAL;
	}

	curr_qp = vi->curr_queue_pairs - vi->xdp_queue_pairs;
	if (prog)
		xdp_qp = nr_cpu_ids;

	/* XDP requires extra queues for XDP_TX */
	if (curr_qp + xdp_qp > vi->max_queue_pairs) {
		netdev_warn_once(dev, "XDP request %i queues but max is %i. XDP_TX and XDP_REDIRECT will operate in a slower locked tx mode.\n",
				 curr_qp + xdp_qp, vi->max_queue_pairs);
		xdp_qp = 0;
	}

	old_prog = rtnl_dereference(vi->rq[0].xdp_prog);
	if (!prog && !old_prog)
		return 0;

	if (prog)
		bpf_prog_add(prog, vi->max_queue_pairs - 1);

	/* Make sure NAPI is not using any XDP TX queues for RX. */
	if (netif_running(dev)) {
		for (i = 0; i < vi->max_queue_pairs; i++) {
			napi_disable(&vi->rq[i].napi);
			virtnet_napi_tx_disable(&vi->sq[i].napi);
		}
	}

	if (!prog) {
		for (i = 0; i < vi->max_queue_pairs; i++) {
			rcu_assign_pointer(vi->rq[i].xdp_prog, prog);
			if (i == 0)
				virtnet_restore_guest_offloads(vi);
		}
		synchronize_net();
	}

	err = _virtnet_set_queues(vi, curr_qp + xdp_qp);
	if (err)
		goto err;
	netif_set_real_num_rx_queues(dev, curr_qp + xdp_qp);
	vi->xdp_queue_pairs = xdp_qp;

	if (prog) {
		vi->xdp_enabled = true;
		for (i = 0; i < vi->max_queue_pairs; i++) {
			rcu_assign_pointer(vi->rq[i].xdp_prog, prog);
			if (i == 0 && !old_prog)
				virtnet_clear_guest_offloads(vi);
		}
		if (!old_prog)
			xdp_features_set_redirect_target(dev, true);
	} else {
		xdp_features_clear_redirect_target(dev);
		vi->xdp_enabled = false;
	}

	for (i = 0; i < vi->max_queue_pairs; i++) {
		if (old_prog)
			bpf_prog_put(old_prog);
		if (netif_running(dev)) {
			virtnet_napi_enable(vi->rq[i].vq, &vi->rq[i].napi);
			virtnet_napi_tx_enable(vi, vi->sq[i].vq,
					       &vi->sq[i].napi);
		}
	}

	return 0;

err:
	if (!prog) {
		virtnet_clear_guest_offloads(vi);
		for (i = 0; i < vi->max_queue_pairs; i++)
			rcu_assign_pointer(vi->rq[i].xdp_prog, old_prog);
	}

	if (netif_running(dev)) {
		for (i = 0; i < vi->max_queue_pairs; i++) {
			virtnet_napi_enable(vi->rq[i].vq, &vi->rq[i].napi);
			virtnet_napi_tx_enable(vi, vi->sq[i].vq,
					       &vi->sq[i].napi);
		}
	}
	if (prog)
		bpf_prog_sub(prog, vi->max_queue_pairs - 1);
	return err;
}

static int virtnet_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return virtnet_xdp_set(dev, xdp->prog, xdp->extack);
	default:
		return -EINVAL;
	}
}

static int virtnet_get_phys_port_name(struct net_device *dev, char *buf,
				      size_t len)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int ret;

	if (!virtio_has_feature(vi->vdev, VIRTIO_NET_F_STANDBY))
		return -EOPNOTSUPP;

	ret = snprintf(buf, len, "sby");
	if (ret >= len)
		return -EOPNOTSUPP;

	return 0;
}

static int virtnet_set_features(struct net_device *dev,
				netdev_features_t features)
{
	struct virtnet_info *vi = netdev_priv(dev);
	u64 offloads;
	int err;

	if ((dev->features ^ features) & NETIF_F_GRO_HW) {
		if (vi->xdp_enabled)
			return -EBUSY;

		if (features & NETIF_F_GRO_HW)
			offloads = vi->guest_offloads_capable;
		else
			offloads = vi->guest_offloads_capable &
				   ~GUEST_OFFLOAD_GRO_HW_MASK;

		err = virtnet_set_guest_offloads(vi, offloads);
		if (err)
			return err;
		vi->guest_offloads = offloads;
	}

	if ((dev->features ^ features) & NETIF_F_RXHASH) {
		if (features & NETIF_F_RXHASH)
			vi->ctrl->rss.hash_types = vi->rss_hash_types_saved;
		else
			vi->ctrl->rss.hash_types = VIRTIO_NET_HASH_REPORT_NONE;

		if (!virtnet_commit_rss_command(vi))
			return -EINVAL;
	}

	return 0;
}

static void virtnet_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct virtnet_info *priv = netdev_priv(dev);
	struct send_queue *sq = &priv->sq[txqueue];
	struct netdev_queue *txq = netdev_get_tx_queue(dev, txqueue);

	u64_stats_update_begin(&sq->stats.syncp);
	u64_stats_inc(&sq->stats.tx_timeouts);
	u64_stats_update_end(&sq->stats.syncp);

	netdev_err(dev, "TX timeout on queue: %u, sq: %s, vq: 0x%x, name: %s, %u usecs ago\n",
		   txqueue, sq->name, sq->vq->index, sq->vq->name,
		   jiffies_to_usecs(jiffies - READ_ONCE(txq->trans_start)));
}

static const struct net_device_ops virtnet_netdev = {
	.ndo_open            = virtnet_open,
	.ndo_stop   	     = virtnet_close,
	.ndo_start_xmit      = start_xmit,
	.ndo_validate_addr   = eth_validate_addr,
	.ndo_set_mac_address = virtnet_set_mac_address,
	.ndo_set_rx_mode     = virtnet_set_rx_mode,
	.ndo_get_stats64     = virtnet_stats,
	.ndo_vlan_rx_add_vid = virtnet_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = virtnet_vlan_rx_kill_vid,
	.ndo_bpf		= virtnet_xdp,
	.ndo_xdp_xmit		= virtnet_xdp_xmit,
	.ndo_features_check	= passthru_features_check,
	.ndo_get_phys_port_name	= virtnet_get_phys_port_name,
	.ndo_set_features	= virtnet_set_features,
	.ndo_tx_timeout		= virtnet_tx_timeout,
};

static void virtnet_config_changed_work(struct work_struct *work)
{
	struct virtnet_info *vi =
		container_of(work, struct virtnet_info, config_work);
	u16 v;

	if (virtio_cread_feature(vi->vdev, VIRTIO_NET_F_STATUS,
				 struct virtio_net_config, status, &v) < 0)
		return;

	if (v & VIRTIO_NET_S_ANNOUNCE) {
		netdev_notify_peers(vi->dev);
		virtnet_ack_link_announce(vi);
	}

	/* Ignore unknown (future) status bits */
	v &= VIRTIO_NET_S_LINK_UP;

	if (vi->status == v)
		return;

	vi->status = v;

	if (vi->status & VIRTIO_NET_S_LINK_UP) {
		virtnet_update_settings(vi);
		netif_carrier_on(vi->dev);
		netif_tx_wake_all_queues(vi->dev);
	} else {
		netif_carrier_off(vi->dev);
		netif_tx_stop_all_queues(vi->dev);
	}
}

static void virtnet_config_changed(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;

	schedule_work(&vi->config_work);
}

static void virtnet_free_queues(struct virtnet_info *vi)
{
	int i;

	for (i = 0; i < vi->max_queue_pairs; i++) {
		__netif_napi_del(&vi->rq[i].napi);
		__netif_napi_del(&vi->sq[i].napi);
	}

	/* We called __netif_napi_del(),
	 * we need to respect an RCU grace period before freeing vi->rq
	 */
	synchronize_net();

	kfree(vi->rq);
	kfree(vi->sq);
	kfree(vi->ctrl);
}

static void _free_receive_bufs(struct virtnet_info *vi)
{
	struct bpf_prog *old_prog;
	int i;

	for (i = 0; i < vi->max_queue_pairs; i++) {
		while (vi->rq[i].pages)
			__free_pages(get_a_page(&vi->rq[i], GFP_KERNEL), 0);

		old_prog = rtnl_dereference(vi->rq[i].xdp_prog);
		RCU_INIT_POINTER(vi->rq[i].xdp_prog, NULL);
		if (old_prog)
			bpf_prog_put(old_prog);
	}
}

static void free_receive_bufs(struct virtnet_info *vi)
{
	rtnl_lock();
	_free_receive_bufs(vi);
	rtnl_unlock();
}

static void free_receive_page_frags(struct virtnet_info *vi)
{
	int i;
	for (i = 0; i < vi->max_queue_pairs; i++)
		if (vi->rq[i].alloc_frag.page) {
			if (vi->rq[i].do_dma && vi->rq[i].last_dma)
				virtnet_rq_unmap(&vi->rq[i], vi->rq[i].last_dma, 0);
			put_page(vi->rq[i].alloc_frag.page);
		}
}

static void virtnet_sq_free_unused_buf(struct virtqueue *vq, void *buf)
{
	if (!is_xdp_frame(buf))
		dev_kfree_skb(buf);
	else
		xdp_return_frame(ptr_to_xdp(buf));
}

static void free_unused_bufs(struct virtnet_info *vi)
{
	void *buf;
	int i;

	for (i = 0; i < vi->max_queue_pairs; i++) {
		struct virtqueue *vq = vi->sq[i].vq;
		while ((buf = virtqueue_detach_unused_buf(vq)) != NULL)
			virtnet_sq_free_unused_buf(vq, buf);
		cond_resched();
	}

	for (i = 0; i < vi->max_queue_pairs; i++) {
		struct virtqueue *vq = vi->rq[i].vq;

		while ((buf = virtqueue_detach_unused_buf(vq)) != NULL)
			virtnet_rq_unmap_free_buf(vq, buf);
		cond_resched();
	}
}

static void virtnet_del_vqs(struct virtnet_info *vi)
{
	struct virtio_device *vdev = vi->vdev;

	virtnet_clean_affinity(vi);

	vdev->config->del_vqs(vdev);

	virtnet_free_queues(vi);
}

/* How large should a single buffer be so a queue full of these can fit at
 * least one full packet?
 * Logic below assumes the mergeable buffer header is used.
 */
static unsigned int mergeable_min_buf_len(struct virtnet_info *vi, struct virtqueue *vq)
{
	const unsigned int hdr_len = vi->hdr_len;
	unsigned int rq_size = virtqueue_get_vring_size(vq);
	unsigned int packet_len = vi->big_packets ? IP_MAX_MTU : vi->dev->max_mtu;
	unsigned int buf_len = hdr_len + ETH_HLEN + VLAN_HLEN + packet_len;
	unsigned int min_buf_len = DIV_ROUND_UP(buf_len, rq_size);

	return max(max(min_buf_len, hdr_len) - hdr_len,
		   (unsigned int)GOOD_PACKET_LEN);
}

static int virtnet_find_vqs(struct virtnet_info *vi)
{
	vq_callback_t **callbacks;
	struct virtqueue **vqs;
	int ret = -ENOMEM;
	int i, total_vqs;
	const char **names;
	bool *ctx;

	/* We expect 1 RX virtqueue followed by 1 TX virtqueue, followed by
	 * possible N-1 RX/TX queue pairs used in multiqueue mode, followed by
	 * possible control vq.
	 */
	total_vqs = vi->max_queue_pairs * 2 +
		    virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VQ);

	/* Allocate space for find_vqs parameters */
	vqs = kcalloc(total_vqs, sizeof(*vqs), GFP_KERNEL);
	if (!vqs)
		goto err_vq;
	callbacks = kmalloc_array(total_vqs, sizeof(*callbacks), GFP_KERNEL);
	if (!callbacks)
		goto err_callback;
	names = kmalloc_array(total_vqs, sizeof(*names), GFP_KERNEL);
	if (!names)
		goto err_names;
	if (!vi->big_packets || vi->mergeable_rx_bufs) {
		ctx = kcalloc(total_vqs, sizeof(*ctx), GFP_KERNEL);
		if (!ctx)
			goto err_ctx;
	} else {
		ctx = NULL;
	}

	/* Parameters for control virtqueue, if any */
	if (vi->has_cvq) {
		callbacks[total_vqs - 1] = NULL;
		names[total_vqs - 1] = "control";
	}

	/* Allocate/initialize parameters for send/receive virtqueues */
	for (i = 0; i < vi->max_queue_pairs; i++) {
		callbacks[rxq2vq(i)] = skb_recv_done;
		callbacks[txq2vq(i)] = skb_xmit_done;
		sprintf(vi->rq[i].name, "input.%d", i);
		sprintf(vi->sq[i].name, "output.%d", i);
		names[rxq2vq(i)] = vi->rq[i].name;
		names[txq2vq(i)] = vi->sq[i].name;
		if (ctx)
			ctx[rxq2vq(i)] = true;
	}

	ret = virtio_find_vqs_ctx(vi->vdev, total_vqs, vqs, callbacks,
				  names, ctx, NULL);
	if (ret)
		goto err_find;

	if (vi->has_cvq) {
		vi->cvq = vqs[total_vqs - 1];
		if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VLAN))
			vi->dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
	}

	for (i = 0; i < vi->max_queue_pairs; i++) {
		vi->rq[i].vq = vqs[rxq2vq(i)];
		vi->rq[i].min_buf_len = mergeable_min_buf_len(vi, vi->rq[i].vq);
		vi->sq[i].vq = vqs[txq2vq(i)];
	}

	/* run here: ret == 0. */


err_find:
	kfree(ctx);
err_ctx:
	kfree(names);
err_names:
	kfree(callbacks);
err_callback:
	kfree(vqs);
err_vq:
	return ret;
}

static int virtnet_alloc_queues(struct virtnet_info *vi)
{
	int i;

	if (vi->has_cvq) {
		vi->ctrl = kzalloc(sizeof(*vi->ctrl), GFP_KERNEL);
		if (!vi->ctrl)
			goto err_ctrl;
	} else {
		vi->ctrl = NULL;
	}
	vi->sq = kcalloc(vi->max_queue_pairs, sizeof(*vi->sq), GFP_KERNEL);
	if (!vi->sq)
		goto err_sq;
	vi->rq = kcalloc(vi->max_queue_pairs, sizeof(*vi->rq), GFP_KERNEL);
	if (!vi->rq)
		goto err_rq;

	INIT_DELAYED_WORK(&vi->refill, refill_work);
	for (i = 0; i < vi->max_queue_pairs; i++) {
		vi->rq[i].pages = NULL;
		netif_napi_add_weight(vi->dev, &vi->rq[i].napi, virtnet_poll,
				      napi_weight);
		netif_napi_add_tx_weight(vi->dev, &vi->sq[i].napi,
					 virtnet_poll_tx,
					 napi_tx ? napi_weight : 0);

		sg_init_table(vi->rq[i].sg, ARRAY_SIZE(vi->rq[i].sg));
		ewma_pkt_len_init(&vi->rq[i].mrg_avg_pkt_len);
		sg_init_table(vi->sq[i].sg, ARRAY_SIZE(vi->sq[i].sg));

		u64_stats_init(&vi->rq[i].stats.syncp);
		u64_stats_init(&vi->sq[i].stats.syncp);
	}

	return 0;

err_rq:
	kfree(vi->sq);
err_sq:
	kfree(vi->ctrl);
err_ctrl:
	return -ENOMEM;
}

static int init_vqs(struct virtnet_info *vi)
{
	int ret;

	/* Allocate send & receive queues */
	ret = virtnet_alloc_queues(vi);
	if (ret)
		goto err;

	ret = virtnet_find_vqs(vi);
	if (ret)
		goto err_free;

	virtnet_rq_set_premapped(vi);

	cpus_read_lock();
	virtnet_set_affinity(vi);
	cpus_read_unlock();

	return 0;

err_free:
	virtnet_free_queues(vi);
err:
	return ret;
}

#ifdef CONFIG_SYSFS
static ssize_t mergeable_rx_buffer_size_show(struct netdev_rx_queue *queue,
		char *buf)
{
	struct virtnet_info *vi = netdev_priv(queue->dev);
	unsigned int queue_index = get_netdev_rx_queue_index(queue);
	unsigned int headroom = virtnet_get_headroom(vi);
	unsigned int tailroom = headroom ? sizeof(struct skb_shared_info) : 0;
	struct ewma_pkt_len *avg;

	BUG_ON(queue_index >= vi->max_queue_pairs);
	avg = &vi->rq[queue_index].mrg_avg_pkt_len;
	return sprintf(buf, "%u\n",
		       get_mergeable_buf_len(&vi->rq[queue_index], avg,
				       SKB_DATA_ALIGN(headroom + tailroom)));
}

static struct rx_queue_attribute mergeable_rx_buffer_size_attribute =
	__ATTR_RO(mergeable_rx_buffer_size);

static struct attribute *virtio_net_mrg_rx_attrs[] = {
	&mergeable_rx_buffer_size_attribute.attr,
	NULL
};

static const struct attribute_group virtio_net_mrg_rx_group = {
	.name = "virtio_net",
	.attrs = virtio_net_mrg_rx_attrs
};
#endif

static bool virtnet_fail_on_feature(struct virtio_device *vdev,
				    unsigned int fbit,
				    const char *fname, const char *dname)
{
	if (!virtio_has_feature(vdev, fbit))
		return false;

	dev_err(&vdev->dev, "device advertises feature %s but not %s",
		fname, dname);

	return true;
}

#define VIRTNET_FAIL_ON(vdev, fbit, dbit)			\
	virtnet_fail_on_feature(vdev, fbit, #fbit, dbit)

static bool virtnet_validate_features(struct virtio_device *vdev)
{
	if (!virtio_has_feature(vdev, VIRTIO_NET_F_CTRL_VQ) &&
	    (VIRTNET_FAIL_ON(vdev, VIRTIO_NET_F_CTRL_RX,
			     "VIRTIO_NET_F_CTRL_VQ") ||
	     VIRTNET_FAIL_ON(vdev, VIRTIO_NET_F_CTRL_VLAN,
			     "VIRTIO_NET_F_CTRL_VQ") ||
	     VIRTNET_FAIL_ON(vdev, VIRTIO_NET_F_GUEST_ANNOUNCE,
			     "VIRTIO_NET_F_CTRL_VQ") ||
	     VIRTNET_FAIL_ON(vdev, VIRTIO_NET_F_MQ, "VIRTIO_NET_F_CTRL_VQ") ||
	     VIRTNET_FAIL_ON(vdev, VIRTIO_NET_F_CTRL_MAC_ADDR,
			     "VIRTIO_NET_F_CTRL_VQ") ||
	     VIRTNET_FAIL_ON(vdev, VIRTIO_NET_F_RSS,
			     "VIRTIO_NET_F_CTRL_VQ") ||
	     VIRTNET_FAIL_ON(vdev, VIRTIO_NET_F_HASH_REPORT,
			     "VIRTIO_NET_F_CTRL_VQ") ||
	     VIRTNET_FAIL_ON(vdev, VIRTIO_NET_F_NOTF_COAL,
			     "VIRTIO_NET_F_CTRL_VQ") ||
	     VIRTNET_FAIL_ON(vdev, VIRTIO_NET_F_VQ_NOTF_COAL,
			     "VIRTIO_NET_F_CTRL_VQ"))) {
		return false;
	}

	return true;
}

#define MIN_MTU ETH_MIN_MTU
#define MAX_MTU ETH_MAX_MTU

static int virtnet_validate(struct virtio_device *vdev)
{
	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	if (!virtnet_validate_features(vdev))
		return -EINVAL;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_MTU)) {
		int mtu = virtio_cread16(vdev,
					 offsetof(struct virtio_net_config,
						  mtu));
		if (mtu < MIN_MTU)
			__virtio_clear_bit(vdev, VIRTIO_NET_F_MTU);
	}

	if (virtio_has_feature(vdev, VIRTIO_NET_F_STANDBY) &&
	    !virtio_has_feature(vdev, VIRTIO_NET_F_MAC)) {
		dev_warn(&vdev->dev, "device advertises feature VIRTIO_NET_F_STANDBY but not VIRTIO_NET_F_MAC, disabling standby");
		__virtio_clear_bit(vdev, VIRTIO_NET_F_STANDBY);
	}

	return 0;
}

static bool virtnet_check_guest_gso(const struct virtnet_info *vi)
{
	return virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_TSO4) ||
		virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_TSO6) ||
		virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_ECN) ||
		virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_UFO) ||
		(virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_USO4) &&
		virtio_has_feature(vi->vdev, VIRTIO_NET_F_GUEST_USO6));
}

static void virtnet_set_big_packets(struct virtnet_info *vi, const int mtu)
{
	bool guest_gso = virtnet_check_guest_gso(vi);

	/* If device can receive ANY guest GSO packets, regardless of mtu,
	 * allocate packets of maximum size, otherwise limit it to only
	 * mtu size worth only.
	 */
	if (mtu > ETH_DATA_LEN || guest_gso) {
		vi->big_packets = true;
		vi->big_packets_num_skbfrags = guest_gso ? MAX_SKB_FRAGS : DIV_ROUND_UP(mtu, PAGE_SIZE);
	}
}

static int virtnet_probe(struct virtio_device *vdev)
{
	int i, err = -ENOMEM;
	struct net_device *dev;
	struct virtnet_info *vi;
	u16 max_queue_pairs;
	int mtu = 0;

	/* Find if host supports multiqueue/rss virtio_net device */
	max_queue_pairs = 1;
	if (virtio_has_feature(vdev, VIRTIO_NET_F_MQ) || virtio_has_feature(vdev, VIRTIO_NET_F_RSS))
		max_queue_pairs =
		     virtio_cread16(vdev, offsetof(struct virtio_net_config, max_virtqueue_pairs));

	/* We need at least 2 queue's */
	if (max_queue_pairs < VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MIN ||
	    max_queue_pairs > VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX ||
	    !virtio_has_feature(vdev, VIRTIO_NET_F_CTRL_VQ))
		max_queue_pairs = 1;

	/* Allocate ourselves a network device with room for our info */
	dev = alloc_etherdev_mq(sizeof(struct virtnet_info), max_queue_pairs);
	if (!dev)
		return -ENOMEM;

	/* Set up network device as normal. */
	dev->priv_flags |= IFF_UNICAST_FLT | IFF_LIVE_ADDR_CHANGE |
			   IFF_TX_SKB_NO_LINEAR;
	dev->netdev_ops = &virtnet_netdev;
	dev->features = NETIF_F_HIGHDMA;

	dev->ethtool_ops = &virtnet_ethtool_ops;
	SET_NETDEV_DEV(dev, &vdev->dev);

	/* Do we support "hardware" checksums? */
	if (virtio_has_feature(vdev, VIRTIO_NET_F_CSUM)) {
		/* This opens up the world of extra features. */
		dev->hw_features |= NETIF_F_HW_CSUM | NETIF_F_SG;
		if (csum)
			dev->features |= NETIF_F_HW_CSUM | NETIF_F_SG;

		if (virtio_has_feature(vdev, VIRTIO_NET_F_GSO)) {
			dev->hw_features |= NETIF_F_TSO
				| NETIF_F_TSO_ECN | NETIF_F_TSO6;
		}
		/* Individual feature bits: what can host handle? */
		if (virtio_has_feature(vdev, VIRTIO_NET_F_HOST_TSO4))
			dev->hw_features |= NETIF_F_TSO;
		if (virtio_has_feature(vdev, VIRTIO_NET_F_HOST_TSO6))
			dev->hw_features |= NETIF_F_TSO6;
		if (virtio_has_feature(vdev, VIRTIO_NET_F_HOST_ECN))
			dev->hw_features |= NETIF_F_TSO_ECN;
		if (virtio_has_feature(vdev, VIRTIO_NET_F_HOST_USO))
			dev->hw_features |= NETIF_F_GSO_UDP_L4;

		dev->features |= NETIF_F_GSO_ROBUST;

		if (gso)
			dev->features |= dev->hw_features & NETIF_F_ALL_TSO;
		/* (!csum && gso) case will be fixed by register_netdev() */
	}
	if (virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_CSUM))
		dev->features |= NETIF_F_RXCSUM;
	if (virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_TSO4) ||
	    virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_TSO6))
		dev->features |= NETIF_F_GRO_HW;
	if (virtio_has_feature(vdev, VIRTIO_NET_F_CTRL_GUEST_OFFLOADS))
		dev->hw_features |= NETIF_F_GRO_HW;

	dev->vlan_features = dev->features;
	dev->xdp_features = NETDEV_XDP_ACT_BASIC | NETDEV_XDP_ACT_REDIRECT;

	/* MTU range: 68 - 65535 */
	dev->min_mtu = MIN_MTU;
	dev->max_mtu = MAX_MTU;

	/* Configuration may specify what MAC to use.  Otherwise random. */
	if (virtio_has_feature(vdev, VIRTIO_NET_F_MAC)) {
		u8 addr[ETH_ALEN];

		virtio_cread_bytes(vdev,
				   offsetof(struct virtio_net_config, mac),
				   addr, ETH_ALEN);
		eth_hw_addr_set(dev, addr);
	} else {
		eth_hw_addr_random(dev);
		dev_info(&vdev->dev, "Assigned random MAC address %pM\n",
			 dev->dev_addr);
	}

	/* Set up our device-specific information */
	vi = netdev_priv(dev);
	vi->dev = dev;
	vi->vdev = vdev;
	vdev->priv = vi;

	INIT_WORK(&vi->config_work, virtnet_config_changed_work);
	spin_lock_init(&vi->refill_lock);

	if (virtio_has_feature(vdev, VIRTIO_NET_F_MRG_RXBUF)) {
		vi->mergeable_rx_bufs = true;
		dev->xdp_features |= NETDEV_XDP_ACT_RX_SG;
	}

	if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_NOTF_COAL)) {
		vi->intr_coal_rx.max_usecs = 0;
		vi->intr_coal_tx.max_usecs = 0;
		vi->intr_coal_tx.max_packets = 0;
		vi->intr_coal_rx.max_packets = 0;
	}

	if (virtio_has_feature(vdev, VIRTIO_NET_F_HASH_REPORT))
		vi->has_rss_hash_report = true;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_RSS))
		vi->has_rss = true;

	if (vi->has_rss || vi->has_rss_hash_report) {
		vi->rss_indir_table_size =
			virtio_cread16(vdev, offsetof(struct virtio_net_config,
				rss_max_indirection_table_length));
		vi->rss_key_size =
			virtio_cread8(vdev, offsetof(struct virtio_net_config, rss_max_key_size));

		vi->rss_hash_types_supported =
		    virtio_cread32(vdev, offsetof(struct virtio_net_config, supported_hash_types));
		vi->rss_hash_types_supported &=
				~(VIRTIO_NET_RSS_HASH_TYPE_IP_EX |
				  VIRTIO_NET_RSS_HASH_TYPE_TCP_EX |
				  VIRTIO_NET_RSS_HASH_TYPE_UDP_EX);

		dev->hw_features |= NETIF_F_RXHASH;
	}

	if (vi->has_rss_hash_report)
		vi->hdr_len = sizeof(struct virtio_net_hdr_v1_hash);
	else if (virtio_has_feature(vdev, VIRTIO_NET_F_MRG_RXBUF) ||
		 virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		vi->hdr_len = sizeof(struct virtio_net_hdr_mrg_rxbuf);
	else
		vi->hdr_len = sizeof(struct virtio_net_hdr);

	if (virtio_has_feature(vdev, VIRTIO_F_ANY_LAYOUT) ||
	    virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		vi->any_header_sg = true;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_CTRL_VQ))
		vi->has_cvq = true;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_MTU)) {
		mtu = virtio_cread16(vdev,
				     offsetof(struct virtio_net_config,
					      mtu));
		if (mtu < dev->min_mtu) {
			/* Should never trigger: MTU was previously validated
			 * in virtnet_validate.
			 */
			dev_err(&vdev->dev,
				"device MTU appears to have changed it is now %d < %d",
				mtu, dev->min_mtu);
			err = -EINVAL;
			goto free;
		}

		dev->mtu = mtu;
		dev->max_mtu = mtu;
	}

	virtnet_set_big_packets(vi, mtu);

	if (vi->any_header_sg)
		dev->needed_headroom = vi->hdr_len;

	/* Enable multiqueue by default */
	if (num_online_cpus() >= max_queue_pairs)
		vi->curr_queue_pairs = max_queue_pairs;
	else
		vi->curr_queue_pairs = num_online_cpus();
	vi->max_queue_pairs = max_queue_pairs;

	/* Allocate/initialize the rx/tx queues, and invoke find_vqs */
	err = init_vqs(vi);
	if (err)
		goto free;

#ifdef CONFIG_SYSFS
	if (vi->mergeable_rx_bufs)
		dev->sysfs_rx_queue_group = &virtio_net_mrg_rx_group;
#endif
	netif_set_real_num_tx_queues(dev, vi->curr_queue_pairs);
	netif_set_real_num_rx_queues(dev, vi->curr_queue_pairs);

	virtnet_init_settings(dev);

	if (virtio_has_feature(vdev, VIRTIO_NET_F_STANDBY)) {
		vi->failover = net_failover_create(vi->dev);
		if (IS_ERR(vi->failover)) {
			err = PTR_ERR(vi->failover);
			goto free_vqs;
		}
	}

	if (vi->has_rss || vi->has_rss_hash_report)
		virtnet_init_default_rss(vi);

	/* serialize netdev register + virtio_device_ready() with ndo_open() */
	rtnl_lock();

	err = register_netdevice(dev);
	if (err) {
		pr_debug("virtio_net: registering device failed\n");
		rtnl_unlock();
		goto free_failover;
	}

	virtio_device_ready(vdev);

	_virtnet_set_queues(vi, vi->curr_queue_pairs);

	/* a random MAC address has been assigned, notify the device.
	 * We don't fail probe if VIRTIO_NET_F_CTRL_MAC_ADDR is not there
	 * because many devices work fine without getting MAC explicitly
	 */
	if (!virtio_has_feature(vdev, VIRTIO_NET_F_MAC) &&
	    virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_MAC_ADDR)) {
		struct scatterlist sg;

		sg_init_one(&sg, dev->dev_addr, dev->addr_len);
		if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_MAC,
					  VIRTIO_NET_CTRL_MAC_ADDR_SET, &sg)) {
			pr_debug("virtio_net: setting MAC address failed\n");
			rtnl_unlock();
			err = -EINVAL;
			goto free_unregister_netdev;
		}
	}

	rtnl_unlock();

	err = virtnet_cpu_notif_add(vi);
	if (err) {
		pr_debug("virtio_net: registering cpu notifier failed\n");
		goto free_unregister_netdev;
	}

	/* Assume link up if device can't report link status,
	   otherwise get link status from config. */
	netif_carrier_off(dev);
	if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_STATUS)) {
		schedule_work(&vi->config_work);
	} else {
		vi->status = VIRTIO_NET_S_LINK_UP;
		virtnet_update_settings(vi);
		netif_carrier_on(dev);
	}

	for (i = 0; i < ARRAY_SIZE(guest_offloads); i++)
		if (virtio_has_feature(vi->vdev, guest_offloads[i]))
			set_bit(guest_offloads[i], &vi->guest_offloads);
	vi->guest_offloads_capable = vi->guest_offloads;

	pr_debug("virtnet: registered device %s with %d RX and TX vq's\n",
		 dev->name, max_queue_pairs);

	return 0;

free_unregister_netdev:
	unregister_netdev(dev);
free_failover:
	net_failover_destroy(vi->failover);
free_vqs:
	virtio_reset_device(vdev);
	cancel_delayed_work_sync(&vi->refill);
	free_receive_page_frags(vi);
	virtnet_del_vqs(vi);
free:
	free_netdev(dev);
	return err;
}

static void remove_vq_common(struct virtnet_info *vi)
{
	virtio_reset_device(vi->vdev);

	/* Free unused buffers in both send and recv, if any. */
	free_unused_bufs(vi);

	free_receive_bufs(vi);

	free_receive_page_frags(vi);

	virtnet_del_vqs(vi);
}

static void virtnet_remove(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;

	virtnet_cpu_notif_remove(vi);

	/* Make sure no work handler is accessing the device. */
	flush_work(&vi->config_work);

	unregister_netdev(vi->dev);

	net_failover_destroy(vi->failover);

	remove_vq_common(vi);

	free_netdev(vi->dev);
}

static __maybe_unused int virtnet_freeze(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;

	virtnet_cpu_notif_remove(vi);
	virtnet_freeze_down(vdev);
	remove_vq_common(vi);

	return 0;
}

static __maybe_unused int virtnet_restore(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;
	int err;

	err = virtnet_restore_up(vdev);
	if (err)
		return err;
	virtnet_set_queues(vi, vi->curr_queue_pairs);

	err = virtnet_cpu_notif_add(vi);
	if (err) {
		virtnet_freeze_down(vdev);
		remove_vq_common(vi);
		return err;
	}

	return 0;
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_NET, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

#define VIRTNET_FEATURES \
	VIRTIO_NET_F_CSUM, VIRTIO_NET_F_GUEST_CSUM, \
	VIRTIO_NET_F_MAC, \
	VIRTIO_NET_F_HOST_TSO4, VIRTIO_NET_F_HOST_UFO, VIRTIO_NET_F_HOST_TSO6, \
	VIRTIO_NET_F_HOST_ECN, VIRTIO_NET_F_GUEST_TSO4, VIRTIO_NET_F_GUEST_TSO6, \
	VIRTIO_NET_F_GUEST_ECN, VIRTIO_NET_F_GUEST_UFO, \
	VIRTIO_NET_F_HOST_USO, VIRTIO_NET_F_GUEST_USO4, VIRTIO_NET_F_GUEST_USO6, \
	VIRTIO_NET_F_MRG_RXBUF, VIRTIO_NET_F_STATUS, VIRTIO_NET_F_CTRL_VQ, \
	VIRTIO_NET_F_CTRL_RX, VIRTIO_NET_F_CTRL_VLAN, \
	VIRTIO_NET_F_GUEST_ANNOUNCE, VIRTIO_NET_F_MQ, \
	VIRTIO_NET_F_CTRL_MAC_ADDR, \
	VIRTIO_NET_F_MTU, VIRTIO_NET_F_CTRL_GUEST_OFFLOADS, \
	VIRTIO_NET_F_SPEED_DUPLEX, VIRTIO_NET_F_STANDBY, \
	VIRTIO_NET_F_RSS, VIRTIO_NET_F_HASH_REPORT, VIRTIO_NET_F_NOTF_COAL, \
	VIRTIO_NET_F_VQ_NOTF_COAL, \
	VIRTIO_NET_F_GUEST_HDRLEN

static unsigned int features[] = {
	VIRTNET_FEATURES,
};

static unsigned int features_legacy[] = {
	VIRTNET_FEATURES,
	VIRTIO_NET_F_GSO,
	VIRTIO_F_ANY_LAYOUT,
};

static struct virtio_driver virtio_net_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.feature_table_legacy = features_legacy,
	.feature_table_size_legacy = ARRAY_SIZE(features_legacy),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.validate =	virtnet_validate,
	.probe =	virtnet_probe,
	.remove =	virtnet_remove,
	.config_changed = virtnet_config_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze =	virtnet_freeze,
	.restore =	virtnet_restore,
#endif
};

static __init int virtio_net_driver_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, "virtio/net:online",
				      virtnet_cpu_online,
				      virtnet_cpu_down_prep);
	if (ret < 0)
		goto out;
	virtionet_online = ret;
	ret = cpuhp_setup_state_multi(CPUHP_VIRT_NET_DEAD, "virtio/net:dead",
				      NULL, virtnet_cpu_dead);
	if (ret)
		goto err_dead;
	ret = register_virtio_driver(&virtio_net_driver);
	if (ret)
		goto err_virtio;
	return 0;
err_virtio:
	cpuhp_remove_multi_state(CPUHP_VIRT_NET_DEAD);
err_dead:
	cpuhp_remove_multi_state(virtionet_online);
out:
	return ret;
}
module_init(virtio_net_driver_init);

static __exit void virtio_net_driver_exit(void)
{
	unregister_virtio_driver(&virtio_net_driver);
	cpuhp_remove_multi_state(CPUHP_VIRT_NET_DEAD);
	cpuhp_remove_multi_state(virtionet_online);
}
module_exit(virtio_net_driver_exit);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio network driver");
MODULE_LICENSE("GPL");
