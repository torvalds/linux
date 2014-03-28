/* A network driver using virtio.
 *
 * Copyright 2007 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
//#define DEBUG
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/scatterlist.h>
#include <linux/if_vlan.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/average.h>

static int napi_weight = NAPI_POLL_WEIGHT;
module_param(napi_weight, int, 0444);

static bool csum = true, gso = true;
module_param(csum, bool, 0444);
module_param(gso, bool, 0444);

/* FIXME: MTU in config. */
#define GOOD_PACKET_LEN (ETH_HLEN + VLAN_HLEN + ETH_DATA_LEN)
#define GOOD_COPY_LEN	128

/* Weight used for the RX packet size EWMA. The average packet size is used to
 * determine the packet buffer size when refilling RX rings. As the entire RX
 * ring may be refilled at once, the weight is chosen so that the EWMA will be
 * insensitive to short-term, transient changes in packet size.
 */
#define RECEIVE_AVG_WEIGHT 64

/* Minimum alignment for mergeable packet buffers. */
#define MERGEABLE_BUFFER_ALIGN max(L1_CACHE_BYTES, 256)

#define VIRTNET_DRIVER_VERSION "1.0.0"

struct virtnet_stats {
	struct u64_stats_sync tx_syncp;
	struct u64_stats_sync rx_syncp;
	u64 tx_bytes;
	u64 tx_packets;

	u64 rx_bytes;
	u64 rx_packets;
};

/* Internal representation of a send virtqueue */
struct send_queue {
	/* Virtqueue associated with this send _queue */
	struct virtqueue *vq;

	/* TX: fragments + linear part + virtio header */
	struct scatterlist sg[MAX_SKB_FRAGS + 2];

	/* Name of the send queue: output.$index */
	char name[40];
};

/* Internal representation of a receive virtqueue */
struct receive_queue {
	/* Virtqueue associated with this receive_queue */
	struct virtqueue *vq;

	struct napi_struct napi;

	/* Chain pages by the private ptr. */
	struct page *pages;

	/* Average packet length for mergeable receive buffers. */
	struct ewma mrg_avg_pkt_len;

	/* Page frag for packet buffer allocation. */
	struct page_frag alloc_frag;

	/* RX: fragments + linear part + virtio header */
	struct scatterlist sg[MAX_SKB_FRAGS + 2];

	/* Name of this receive queue: input.$index */
	char name[40];
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

	/* I like... big packets and I cannot lie! */
	bool big_packets;

	/* Host will merge rx buffers for big packets (shake it! shake it!) */
	bool mergeable_rx_bufs;

	/* Has control virtqueue */
	bool has_cvq;

	/* Host can handle any s/g split between our header and packet data */
	bool any_header_sg;

	/* enable config space updates */
	bool config_enable;

	/* Active statistics */
	struct virtnet_stats __percpu *stats;

	/* Work struct for refilling if we run low on memory. */
	struct delayed_work refill;

	/* Work struct for config space updates */
	struct work_struct config_work;

	/* Lock for config space updates */
	struct mutex config_lock;

	/* Does the affinity hint is set for virtqueues? */
	bool affinity_hint_set;

	/* CPU hot plug notifier */
	struct notifier_block nb;
};

struct skb_vnet_hdr {
	union {
		struct virtio_net_hdr hdr;
		struct virtio_net_hdr_mrg_rxbuf mhdr;
	};
};

struct padded_vnet_hdr {
	struct virtio_net_hdr hdr;
	/*
	 * virtio_net_hdr should be in a separated sg buffer because of a
	 * QEMU bug, and data sg buffer shares same page with this header sg.
	 * This padding makes next sg 16 byte aligned after virtio_net_hdr.
	 */
	char padding[6];
};

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

static inline struct skb_vnet_hdr *skb_vnet_hdr(struct sk_buff *skb)
{
	return (struct skb_vnet_hdr *)skb->cb;
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

static void skb_xmit_done(struct virtqueue *vq)
{
	struct virtnet_info *vi = vq->vdev->priv;

	/* Suppress further interrupts. */
	virtqueue_disable_cb(vq);

	/* We were probably waiting for more output buffers. */
	netif_wake_subqueue(vi->dev, vq2txq(vq));
}

static unsigned int mergeable_ctx_to_buf_truesize(unsigned long mrg_ctx)
{
	unsigned int truesize = mrg_ctx & (MERGEABLE_BUFFER_ALIGN - 1);
	return (truesize + 1) * MERGEABLE_BUFFER_ALIGN;
}

static void *mergeable_ctx_to_buf_address(unsigned long mrg_ctx)
{
	return (void *)(mrg_ctx & -MERGEABLE_BUFFER_ALIGN);

}

static unsigned long mergeable_buf_to_ctx(void *buf, unsigned int truesize)
{
	unsigned int size = truesize / MERGEABLE_BUFFER_ALIGN;
	return (unsigned long)buf | (size - 1);
}

/* Called from bottom half context */
static struct sk_buff *page_to_skb(struct receive_queue *rq,
				   struct page *page, unsigned int offset,
				   unsigned int len, unsigned int truesize)
{
	struct virtnet_info *vi = rq->vq->vdev->priv;
	struct sk_buff *skb;
	struct skb_vnet_hdr *hdr;
	unsigned int copy, hdr_len, hdr_padded_len;
	char *p;

	p = page_address(page) + offset;

	/* copy small packet so we can reuse these pages for small data */
	skb = netdev_alloc_skb_ip_align(vi->dev, GOOD_COPY_LEN);
	if (unlikely(!skb))
		return NULL;

	hdr = skb_vnet_hdr(skb);

	if (vi->mergeable_rx_bufs) {
		hdr_len = sizeof hdr->mhdr;
		hdr_padded_len = sizeof hdr->mhdr;
	} else {
		hdr_len = sizeof hdr->hdr;
		hdr_padded_len = sizeof(struct padded_vnet_hdr);
	}

	memcpy(hdr, p, hdr_len);

	len -= hdr_len;
	offset += hdr_padded_len;
	p += hdr_padded_len;

	copy = len;
	if (copy > skb_tailroom(skb))
		copy = skb_tailroom(skb);
	memcpy(skb_put(skb, copy), p, copy);

	len -= copy;
	offset += copy;

	if (vi->mergeable_rx_bufs) {
		if (len)
			skb_add_rx_frag(skb, 0, page, offset, len, truesize);
		else
			put_page(page);
		return skb;
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

	return skb;
}

static struct sk_buff *receive_small(void *buf, unsigned int len)
{
	struct sk_buff * skb = buf;

	len -= sizeof(struct virtio_net_hdr);
	skb_trim(skb, len);

	return skb;
}

static struct sk_buff *receive_big(struct net_device *dev,
				   struct receive_queue *rq,
				   void *buf,
				   unsigned int len)
{
	struct page *page = buf;
	struct sk_buff *skb = page_to_skb(rq, page, 0, len, PAGE_SIZE);

	if (unlikely(!skb))
		goto err;

	return skb;

err:
	dev->stats.rx_dropped++;
	give_pages(rq, page);
	return NULL;
}

static struct sk_buff *receive_mergeable(struct net_device *dev,
					 struct receive_queue *rq,
					 unsigned long ctx,
					 unsigned int len)
{
	void *buf = mergeable_ctx_to_buf_address(ctx);
	struct skb_vnet_hdr *hdr = buf;
	int num_buf = hdr->mhdr.num_buffers;
	struct page *page = virt_to_head_page(buf);
	int offset = buf - page_address(page);
	unsigned int truesize = max(len, mergeable_ctx_to_buf_truesize(ctx));

	struct sk_buff *head_skb = page_to_skb(rq, page, offset, len, truesize);
	struct sk_buff *curr_skb = head_skb;

	if (unlikely(!curr_skb))
		goto err_skb;
	while (--num_buf) {
		int num_skb_frags;

		ctx = (unsigned long)virtqueue_get_buf(rq->vq, &len);
		if (unlikely(!ctx)) {
			pr_debug("%s: rx error: %d buffers out of %d missing\n",
				 dev->name, num_buf, hdr->mhdr.num_buffers);
			dev->stats.rx_length_errors++;
			goto err_buf;
		}

		buf = mergeable_ctx_to_buf_address(ctx);
		page = virt_to_head_page(buf);

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
		truesize = max(len, mergeable_ctx_to_buf_truesize(ctx));
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

	ewma_add(&rq->mrg_avg_pkt_len, head_skb->len);
	return head_skb;

err_skb:
	put_page(page);
	while (--num_buf) {
		ctx = (unsigned long)virtqueue_get_buf(rq->vq, &len);
		if (unlikely(!ctx)) {
			pr_debug("%s: rx error: %d buffers missing\n",
				 dev->name, num_buf);
			dev->stats.rx_length_errors++;
			break;
		}
		page = virt_to_head_page(mergeable_ctx_to_buf_address(ctx));
		put_page(page);
	}
err_buf:
	dev->stats.rx_dropped++;
	dev_kfree_skb(head_skb);
	return NULL;
}

static void receive_buf(struct receive_queue *rq, void *buf, unsigned int len)
{
	struct virtnet_info *vi = rq->vq->vdev->priv;
	struct net_device *dev = vi->dev;
	struct virtnet_stats *stats = this_cpu_ptr(vi->stats);
	struct sk_buff *skb;
	struct skb_vnet_hdr *hdr;

	if (unlikely(len < sizeof(struct virtio_net_hdr) + ETH_HLEN)) {
		pr_debug("%s: short packet %i\n", dev->name, len);
		dev->stats.rx_length_errors++;
		if (vi->mergeable_rx_bufs) {
			unsigned long ctx = (unsigned long)buf;
			void *base = mergeable_ctx_to_buf_address(ctx);
			put_page(virt_to_head_page(base));
		} else if (vi->big_packets) {
			give_pages(rq, buf);
		} else {
			dev_kfree_skb(buf);
		}
		return;
	}

	if (vi->mergeable_rx_bufs)
		skb = receive_mergeable(dev, rq, (unsigned long)buf, len);
	else if (vi->big_packets)
		skb = receive_big(dev, rq, buf, len);
	else
		skb = receive_small(buf, len);

	if (unlikely(!skb))
		return;

	hdr = skb_vnet_hdr(skb);

	u64_stats_update_begin(&stats->rx_syncp);
	stats->rx_bytes += skb->len;
	stats->rx_packets++;
	u64_stats_update_end(&stats->rx_syncp);

	if (hdr->hdr.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
		pr_debug("Needs csum!\n");
		if (!skb_partial_csum_set(skb,
					  hdr->hdr.csum_start,
					  hdr->hdr.csum_offset))
			goto frame_err;
	} else if (hdr->hdr.flags & VIRTIO_NET_HDR_F_DATA_VALID) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	skb->protocol = eth_type_trans(skb, dev);
	pr_debug("Receiving skb proto 0x%04x len %i type %i\n",
		 ntohs(skb->protocol), skb->len, skb->pkt_type);

	if (hdr->hdr.gso_type != VIRTIO_NET_HDR_GSO_NONE) {
		pr_debug("GSO!\n");
		switch (hdr->hdr.gso_type & ~VIRTIO_NET_HDR_GSO_ECN) {
		case VIRTIO_NET_HDR_GSO_TCPV4:
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
			break;
		case VIRTIO_NET_HDR_GSO_UDP:
			skb_shinfo(skb)->gso_type = SKB_GSO_UDP;
			break;
		case VIRTIO_NET_HDR_GSO_TCPV6:
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
			break;
		default:
			net_warn_ratelimited("%s: bad gso type %u.\n",
					     dev->name, hdr->hdr.gso_type);
			goto frame_err;
		}

		if (hdr->hdr.gso_type & VIRTIO_NET_HDR_GSO_ECN)
			skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_ECN;

		skb_shinfo(skb)->gso_size = hdr->hdr.gso_size;
		if (skb_shinfo(skb)->gso_size == 0) {
			net_warn_ratelimited("%s: zero gso size.\n", dev->name);
			goto frame_err;
		}

		/* Header must be checked, and gso_segs computed. */
		skb_shinfo(skb)->gso_type |= SKB_GSO_DODGY;
		skb_shinfo(skb)->gso_segs = 0;
	}

	netif_receive_skb(skb);
	return;

frame_err:
	dev->stats.rx_frame_errors++;
	dev_kfree_skb(skb);
}

static int add_recvbuf_small(struct receive_queue *rq, gfp_t gfp)
{
	struct virtnet_info *vi = rq->vq->vdev->priv;
	struct sk_buff *skb;
	struct skb_vnet_hdr *hdr;
	int err;

	skb = __netdev_alloc_skb_ip_align(vi->dev, GOOD_PACKET_LEN, gfp);
	if (unlikely(!skb))
		return -ENOMEM;

	skb_put(skb, GOOD_PACKET_LEN);

	hdr = skb_vnet_hdr(skb);
	sg_set_buf(rq->sg, &hdr->hdr, sizeof hdr->hdr);

	skb_to_sgvec(skb, rq->sg + 1, 0, skb->len);

	err = virtqueue_add_inbuf(rq->vq, rq->sg, 2, skb, gfp);
	if (err < 0)
		dev_kfree_skb(skb);

	return err;
}

static int add_recvbuf_big(struct receive_queue *rq, gfp_t gfp)
{
	struct page *first, *list = NULL;
	char *p;
	int i, err, offset;

	/* page in rq->sg[MAX_SKB_FRAGS + 1] is list tail */
	for (i = MAX_SKB_FRAGS + 1; i > 1; --i) {
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
	/* a separated rq->sg[0] for virtio_net_hdr only due to QEMU bug */
	sg_set_buf(&rq->sg[0], p, sizeof(struct virtio_net_hdr));

	/* rq->sg[1] for data packet, from offset */
	offset = sizeof(struct padded_vnet_hdr);
	sg_set_buf(&rq->sg[1], p + offset, PAGE_SIZE - offset);

	/* chain first in list head */
	first->private = (unsigned long)list;
	err = virtqueue_add_inbuf(rq->vq, rq->sg, MAX_SKB_FRAGS + 2,
				  first, gfp);
	if (err < 0)
		give_pages(rq, first);

	return err;
}

static unsigned int get_mergeable_buf_len(struct ewma *avg_pkt_len)
{
	const size_t hdr_len = sizeof(struct virtio_net_hdr_mrg_rxbuf);
	unsigned int len;

	len = hdr_len + clamp_t(unsigned int, ewma_read(avg_pkt_len),
			GOOD_PACKET_LEN, PAGE_SIZE - hdr_len);
	return ALIGN(len, MERGEABLE_BUFFER_ALIGN);
}

static int add_recvbuf_mergeable(struct receive_queue *rq, gfp_t gfp)
{
	struct page_frag *alloc_frag = &rq->alloc_frag;
	char *buf;
	unsigned long ctx;
	int err;
	unsigned int len, hole;

	len = get_mergeable_buf_len(&rq->mrg_avg_pkt_len);
	if (unlikely(!skb_page_frag_refill(len, alloc_frag, gfp)))
		return -ENOMEM;

	buf = (char *)page_address(alloc_frag->page) + alloc_frag->offset;
	ctx = mergeable_buf_to_ctx(buf, len);
	get_page(alloc_frag->page);
	alloc_frag->offset += len;
	hole = alloc_frag->size - alloc_frag->offset;
	if (hole < len) {
		/* To avoid internal fragmentation, if there is very likely not
		 * enough space for another buffer, add the remaining space to
		 * the current buffer. This extra space is not included in
		 * the truesize stored in ctx.
		 */
		len += hole;
		alloc_frag->offset += hole;
	}

	sg_init_one(rq->sg, buf, len);
	err = virtqueue_add_inbuf(rq->vq, rq->sg, 1, (void *)ctx, gfp);
	if (err < 0)
		put_page(virt_to_head_page(buf));

	return err;
}

/*
 * Returns false if we couldn't fill entirely (OOM).
 *
 * Normally run in the receive path, but can also be run from ndo_open
 * before we're receiving packets, or from refill_work which is
 * careful to disable receiving (using napi_disable).
 */
static bool try_fill_recv(struct receive_queue *rq, gfp_t gfp)
{
	struct virtnet_info *vi = rq->vq->vdev->priv;
	int err;
	bool oom;

	gfp |= __GFP_COLD;
	do {
		if (vi->mergeable_rx_bufs)
			err = add_recvbuf_mergeable(rq, gfp);
		else if (vi->big_packets)
			err = add_recvbuf_big(rq, gfp);
		else
			err = add_recvbuf_small(rq, gfp);

		oom = err == -ENOMEM;
		if (err)
			break;
	} while (rq->vq->num_free);
	virtqueue_kick(rq->vq);
	return !oom;
}

static void skb_recv_done(struct virtqueue *rvq)
{
	struct virtnet_info *vi = rvq->vdev->priv;
	struct receive_queue *rq = &vi->rq[vq2rxq(rvq)];

	/* Schedule NAPI, Suppress further interrupts if successful. */
	if (napi_schedule_prep(&rq->napi)) {
		virtqueue_disable_cb(rvq);
		__napi_schedule(&rq->napi);
	}
}

static void virtnet_napi_enable(struct receive_queue *rq)
{
	napi_enable(&rq->napi);

	/* If all buffers were filled by other side before we napi_enabled, we
	 * won't get another interrupt, so process any outstanding packets
	 * now.  virtnet_poll wants re-enable the queue, so we disable here.
	 * We synchronize against interrupts via NAPI_STATE_SCHED */
	if (napi_schedule_prep(&rq->napi)) {
		virtqueue_disable_cb(rq->vq);
		local_bh_disable();
		__napi_schedule(&rq->napi);
		local_bh_enable();
	}
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
		still_empty = !try_fill_recv(rq, GFP_KERNEL);
		virtnet_napi_enable(rq);

		/* In theory, this can happen: if we don't get any buffers in
		 * we will *never* try to fill again.
		 */
		if (still_empty)
			schedule_delayed_work(&vi->refill, HZ/2);
	}
}

static int virtnet_poll(struct napi_struct *napi, int budget)
{
	struct receive_queue *rq =
		container_of(napi, struct receive_queue, napi);
	struct virtnet_info *vi = rq->vq->vdev->priv;
	void *buf;
	unsigned int r, len, received = 0;

again:
	while (received < budget &&
	       (buf = virtqueue_get_buf(rq->vq, &len)) != NULL) {
		receive_buf(rq, buf, len);
		received++;
	}

	if (rq->vq->num_free > virtqueue_get_vring_size(rq->vq) / 2) {
		if (!try_fill_recv(rq, GFP_ATOMIC))
			schedule_delayed_work(&vi->refill, 0);
	}

	/* Out of packets? */
	if (received < budget) {
		r = virtqueue_enable_cb_prepare(rq->vq);
		napi_complete(napi);
		if (unlikely(virtqueue_poll(rq->vq, r)) &&
		    napi_schedule_prep(napi)) {
			virtqueue_disable_cb(rq->vq);
			__napi_schedule(napi);
			goto again;
		}
	}

	return received;
}

static int virtnet_open(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int i;

	for (i = 0; i < vi->max_queue_pairs; i++) {
		if (i < vi->curr_queue_pairs)
			/* Make sure we have some buffers: if oom use wq. */
			if (!try_fill_recv(&vi->rq[i], GFP_KERNEL))
				schedule_delayed_work(&vi->refill, 0);
		virtnet_napi_enable(&vi->rq[i]);
	}

	return 0;
}

static void free_old_xmit_skbs(struct send_queue *sq)
{
	struct sk_buff *skb;
	unsigned int len;
	struct virtnet_info *vi = sq->vq->vdev->priv;
	struct virtnet_stats *stats = this_cpu_ptr(vi->stats);

	while ((skb = virtqueue_get_buf(sq->vq, &len)) != NULL) {
		pr_debug("Sent skb %p\n", skb);

		u64_stats_update_begin(&stats->tx_syncp);
		stats->tx_bytes += skb->len;
		stats->tx_packets++;
		u64_stats_update_end(&stats->tx_syncp);

		dev_kfree_skb_any(skb);
	}
}

static int xmit_skb(struct send_queue *sq, struct sk_buff *skb)
{
	struct skb_vnet_hdr *hdr;
	const unsigned char *dest = ((struct ethhdr *)skb->data)->h_dest;
	struct virtnet_info *vi = sq->vq->vdev->priv;
	unsigned num_sg;
	unsigned hdr_len;
	bool can_push;

	pr_debug("%s: xmit %p %pM\n", vi->dev->name, skb, dest);
	if (vi->mergeable_rx_bufs)
		hdr_len = sizeof hdr->mhdr;
	else
		hdr_len = sizeof hdr->hdr;

	can_push = vi->any_header_sg &&
		!((unsigned long)skb->data & (__alignof__(*hdr) - 1)) &&
		!skb_header_cloned(skb) && skb_headroom(skb) >= hdr_len;
	/* Even if we can, don't push here yet as this would skew
	 * csum_start offset below. */
	if (can_push)
		hdr = (struct skb_vnet_hdr *)(skb->data - hdr_len);
	else
		hdr = skb_vnet_hdr(skb);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		hdr->hdr.flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
		hdr->hdr.csum_start = skb_checksum_start_offset(skb);
		hdr->hdr.csum_offset = skb->csum_offset;
	} else {
		hdr->hdr.flags = 0;
		hdr->hdr.csum_offset = hdr->hdr.csum_start = 0;
	}

	if (skb_is_gso(skb)) {
		hdr->hdr.hdr_len = skb_headlen(skb);
		hdr->hdr.gso_size = skb_shinfo(skb)->gso_size;
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_TCPV6;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_UDP;
		else
			BUG();
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCP_ECN)
			hdr->hdr.gso_type |= VIRTIO_NET_HDR_GSO_ECN;
	} else {
		hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_NONE;
		hdr->hdr.gso_size = hdr->hdr.hdr_len = 0;
	}

	if (vi->mergeable_rx_bufs)
		hdr->mhdr.num_buffers = 0;

	if (can_push) {
		__skb_push(skb, hdr_len);
		num_sg = skb_to_sgvec(skb, sq->sg, 0, skb->len);
		/* Pull header back to avoid skew in tx bytes calculations. */
		__skb_pull(skb, hdr_len);
	} else {
		sg_set_buf(sq->sg, hdr, hdr_len);
		num_sg = skb_to_sgvec(skb, sq->sg + 1, 0, skb->len) + 1;
	}
	return virtqueue_add_outbuf(sq->vq, sq->sg, num_sg, skb, GFP_ATOMIC);
}

static netdev_tx_t start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int qnum = skb_get_queue_mapping(skb);
	struct send_queue *sq = &vi->sq[qnum];
	int err;

	/* Free up any pending old buffers before queueing new ones. */
	free_old_xmit_skbs(sq);

	/* Try to transmit */
	err = xmit_skb(sq, skb);

	/* This should not happen! */
	if (unlikely(err)) {
		dev->stats.tx_fifo_errors++;
		if (net_ratelimit())
			dev_warn(&dev->dev,
				 "Unexpected TXQ (%d) queue failure: %d\n", qnum, err);
		dev->stats.tx_dropped++;
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	virtqueue_kick(sq->vq);

	/* Don't wait up for transmitted skbs to be freed. */
	skb_orphan(skb);
	nf_reset(skb);

	/* Apparently nice girls don't return TX_BUSY; stop the queue
	 * before it gets out of hand.  Naturally, this wastes entries. */
	if (sq->vq->num_free < 2+MAX_SKB_FRAGS) {
		netif_stop_subqueue(dev, qnum);
		if (unlikely(!virtqueue_enable_cb_delayed(sq->vq))) {
			/* More just got used, free them then recheck. */
			free_old_xmit_skbs(sq);
			if (sq->vq->num_free >= 2+MAX_SKB_FRAGS) {
				netif_start_subqueue(dev, qnum);
				virtqueue_disable_cb(sq->vq);
			}
		}
	}

	return NETDEV_TX_OK;
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
	struct virtio_net_ctrl_hdr ctrl;
	virtio_net_ctrl_ack status = ~0;
	unsigned out_num = 0, tmp;

	/* Caller should know better */
	BUG_ON(!virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VQ));

	ctrl.class = class;
	ctrl.cmd = cmd;
	/* Add header */
	sg_init_one(&hdr, &ctrl, sizeof(ctrl));
	sgs[out_num++] = &hdr;

	if (out)
		sgs[out_num++] = out;

	/* Add return status. */
	sg_init_one(&stat, &status, sizeof(status));
	sgs[out_num] = &stat;

	BUG_ON(out_num + 1 > ARRAY_SIZE(sgs));
	BUG_ON(virtqueue_add_sgs(vi->cvq, sgs, out_num, 1, vi, GFP_ATOMIC) < 0);

	if (unlikely(!virtqueue_kick(vi->cvq)))
		return status == VIRTIO_NET_OK;

	/* Spin for a response, the kick causes an ioport write, trapping
	 * into the hypervisor, so the request should be handled immediately.
	 */
	while (!virtqueue_get_buf(vi->cvq, &tmp) &&
	       !virtqueue_is_broken(vi->cvq))
		cpu_relax();

	return status == VIRTIO_NET_OK;
}

static int virtnet_set_mac_address(struct net_device *dev, void *p)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct virtio_device *vdev = vi->vdev;
	int ret;
	struct sockaddr *addr = p;
	struct scatterlist sg;

	ret = eth_prepare_mac_addr_change(dev, p);
	if (ret)
		return ret;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_CTRL_MAC_ADDR)) {
		sg_init_one(&sg, addr->sa_data, dev->addr_len);
		if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_MAC,
					  VIRTIO_NET_CTRL_MAC_ADDR_SET, &sg)) {
			dev_warn(&vdev->dev,
				 "Failed to set mac address by vq command.\n");
			return -EINVAL;
		}
	} else if (virtio_has_feature(vdev, VIRTIO_NET_F_MAC)) {
		unsigned int i;

		/* Naturally, this has an atomicity problem. */
		for (i = 0; i < dev->addr_len; i++)
			virtio_cwrite8(vdev,
				       offsetof(struct virtio_net_config, mac) +
				       i, addr->sa_data[i]);
	}

	eth_commit_mac_addr_change(dev, p);

	return 0;
}

static struct rtnl_link_stats64 *virtnet_stats(struct net_device *dev,
					       struct rtnl_link_stats64 *tot)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int cpu;
	unsigned int start;

	for_each_possible_cpu(cpu) {
		struct virtnet_stats *stats = per_cpu_ptr(vi->stats, cpu);
		u64 tpackets, tbytes, rpackets, rbytes;

		do {
			start = u64_stats_fetch_begin_bh(&stats->tx_syncp);
			tpackets = stats->tx_packets;
			tbytes   = stats->tx_bytes;
		} while (u64_stats_fetch_retry_bh(&stats->tx_syncp, start));

		do {
			start = u64_stats_fetch_begin_bh(&stats->rx_syncp);
			rpackets = stats->rx_packets;
			rbytes   = stats->rx_bytes;
		} while (u64_stats_fetch_retry_bh(&stats->rx_syncp, start));

		tot->rx_packets += rpackets;
		tot->tx_packets += tpackets;
		tot->rx_bytes   += rbytes;
		tot->tx_bytes   += tbytes;
	}

	tot->tx_dropped = dev->stats.tx_dropped;
	tot->tx_fifo_errors = dev->stats.tx_fifo_errors;
	tot->rx_dropped = dev->stats.rx_dropped;
	tot->rx_length_errors = dev->stats.rx_length_errors;
	tot->rx_frame_errors = dev->stats.rx_frame_errors;

	return tot;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void virtnet_netpoll(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int i;

	for (i = 0; i < vi->curr_queue_pairs; i++)
		napi_schedule(&vi->rq[i].napi);
}
#endif

static void virtnet_ack_link_announce(struct virtnet_info *vi)
{
	rtnl_lock();
	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_ANNOUNCE,
				  VIRTIO_NET_CTRL_ANNOUNCE_ACK, NULL))
		dev_warn(&vi->dev->dev, "Failed to ack link announce.\n");
	rtnl_unlock();
}

static int virtnet_set_queues(struct virtnet_info *vi, u16 queue_pairs)
{
	struct scatterlist sg;
	struct virtio_net_ctrl_mq s;
	struct net_device *dev = vi->dev;

	if (!vi->has_cvq || !virtio_has_feature(vi->vdev, VIRTIO_NET_F_MQ))
		return 0;

	s.virtqueue_pairs = queue_pairs;
	sg_init_one(&sg, &s, sizeof(s));

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

static int virtnet_close(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int i;

	/* Make sure refill_work doesn't re-enable napi! */
	cancel_delayed_work_sync(&vi->refill);

	for (i = 0; i < vi->max_queue_pairs; i++)
		napi_disable(&vi->rq[i].napi);

	return 0;
}

static void virtnet_set_rx_mode(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct scatterlist sg[2];
	u8 promisc, allmulti;
	struct virtio_net_ctrl_mac *mac_data;
	struct netdev_hw_addr *ha;
	int uc_count;
	int mc_count;
	void *buf;
	int i;

	/* We can't dynamically set ndo_set_rx_mode, so return gracefully */
	if (!virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_RX))
		return;

	promisc = ((dev->flags & IFF_PROMISC) != 0);
	allmulti = ((dev->flags & IFF_ALLMULTI) != 0);

	sg_init_one(sg, &promisc, sizeof(promisc));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_RX,
				  VIRTIO_NET_CTRL_RX_PROMISC, sg))
		dev_warn(&dev->dev, "Failed to %sable promisc mode.\n",
			 promisc ? "en" : "dis");

	sg_init_one(sg, &allmulti, sizeof(allmulti));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_RX,
				  VIRTIO_NET_CTRL_RX_ALLMULTI, sg))
		dev_warn(&dev->dev, "Failed to %sable allmulti mode.\n",
			 allmulti ? "en" : "dis");

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
	mac_data->entries = uc_count;
	i = 0;
	netdev_for_each_uc_addr(ha, dev)
		memcpy(&mac_data->macs[i++][0], ha->addr, ETH_ALEN);

	sg_set_buf(&sg[0], mac_data,
		   sizeof(mac_data->entries) + (uc_count * ETH_ALEN));

	/* multicast list and count fill the end */
	mac_data = (void *)&mac_data->macs[uc_count][0];

	mac_data->entries = mc_count;
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

	sg_init_one(&sg, &vid, sizeof(vid));

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

	sg_init_one(&sg, &vid, sizeof(vid));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_VLAN,
				  VIRTIO_NET_CTRL_VLAN_DEL, &sg))
		dev_warn(&dev->dev, "Failed to kill VLAN ID %d.\n", vid);
	return 0;
}

static void virtnet_clean_affinity(struct virtnet_info *vi, long hcpu)
{
	int i;

	if (vi->affinity_hint_set) {
		for (i = 0; i < vi->max_queue_pairs; i++) {
			virtqueue_set_affinity(vi->rq[i].vq, -1);
			virtqueue_set_affinity(vi->sq[i].vq, -1);
		}

		vi->affinity_hint_set = false;
	}
}

static void virtnet_set_affinity(struct virtnet_info *vi)
{
	int i;
	int cpu;

	/* In multiqueue mode, when the number of cpu is equal to the number of
	 * queue pairs, we let the queue pairs to be private to one cpu by
	 * setting the affinity hint to eliminate the contention.
	 */
	if (vi->curr_queue_pairs == 1 ||
	    vi->max_queue_pairs != num_online_cpus()) {
		virtnet_clean_affinity(vi, -1);
		return;
	}

	i = 0;
	for_each_online_cpu(cpu) {
		virtqueue_set_affinity(vi->rq[i].vq, cpu);
		virtqueue_set_affinity(vi->sq[i].vq, cpu);
		netif_set_xps_queue(vi->dev, cpumask_of(cpu), i);
		i++;
	}

	vi->affinity_hint_set = true;
}

static int virtnet_cpu_callback(struct notifier_block *nfb,
			        unsigned long action, void *hcpu)
{
	struct virtnet_info *vi = container_of(nfb, struct virtnet_info, nb);

	switch(action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
	case CPU_DEAD:
		virtnet_set_affinity(vi);
		break;
	case CPU_DOWN_PREPARE:
		virtnet_clean_affinity(vi, (long)hcpu);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static void virtnet_get_ringparam(struct net_device *dev,
				struct ethtool_ringparam *ring)
{
	struct virtnet_info *vi = netdev_priv(dev);

	ring->rx_max_pending = virtqueue_get_vring_size(vi->rq[0].vq);
	ring->tx_max_pending = virtqueue_get_vring_size(vi->sq[0].vq);
	ring->rx_pending = ring->rx_max_pending;
	ring->tx_pending = ring->tx_max_pending;
}


static void virtnet_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct virtio_device *vdev = vi->vdev;

	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->version, VIRTNET_DRIVER_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, virtio_bus_name(vdev), sizeof(info->bus_info));

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

	if (queue_pairs > vi->max_queue_pairs)
		return -EINVAL;

	get_online_cpus();
	err = virtnet_set_queues(vi, queue_pairs);
	if (!err) {
		netif_set_real_num_tx_queues(dev, queue_pairs);
		netif_set_real_num_rx_queues(dev, queue_pairs);

		virtnet_set_affinity(vi);
	}
	put_online_cpus();

	return err;
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

static const struct ethtool_ops virtnet_ethtool_ops = {
	.get_drvinfo = virtnet_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_ringparam = virtnet_get_ringparam,
	.set_channels = virtnet_set_channels,
	.get_channels = virtnet_get_channels,
};

#define MIN_MTU 68
#define MAX_MTU 65535

static int virtnet_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < MIN_MTU || new_mtu > MAX_MTU)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops virtnet_netdev = {
	.ndo_open            = virtnet_open,
	.ndo_stop   	     = virtnet_close,
	.ndo_start_xmit      = start_xmit,
	.ndo_validate_addr   = eth_validate_addr,
	.ndo_set_mac_address = virtnet_set_mac_address,
	.ndo_set_rx_mode     = virtnet_set_rx_mode,
	.ndo_change_mtu	     = virtnet_change_mtu,
	.ndo_get_stats64     = virtnet_stats,
	.ndo_vlan_rx_add_vid = virtnet_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = virtnet_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = virtnet_netpoll,
#endif
};

static void virtnet_config_changed_work(struct work_struct *work)
{
	struct virtnet_info *vi =
		container_of(work, struct virtnet_info, config_work);
	u16 v;

	mutex_lock(&vi->config_lock);
	if (!vi->config_enable)
		goto done;

	if (virtio_cread_feature(vi->vdev, VIRTIO_NET_F_STATUS,
				 struct virtio_net_config, status, &v) < 0)
		goto done;

	if (v & VIRTIO_NET_S_ANNOUNCE) {
		netdev_notify_peers(vi->dev);
		virtnet_ack_link_announce(vi);
	}

	/* Ignore unknown (future) status bits */
	v &= VIRTIO_NET_S_LINK_UP;

	if (vi->status == v)
		goto done;

	vi->status = v;

	if (vi->status & VIRTIO_NET_S_LINK_UP) {
		netif_carrier_on(vi->dev);
		netif_tx_wake_all_queues(vi->dev);
	} else {
		netif_carrier_off(vi->dev);
		netif_tx_stop_all_queues(vi->dev);
	}
done:
	mutex_unlock(&vi->config_lock);
}

static void virtnet_config_changed(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;

	schedule_work(&vi->config_work);
}

static void virtnet_free_queues(struct virtnet_info *vi)
{
	int i;

	for (i = 0; i < vi->max_queue_pairs; i++)
		netif_napi_del(&vi->rq[i].napi);

	kfree(vi->rq);
	kfree(vi->sq);
}

static void free_receive_bufs(struct virtnet_info *vi)
{
	int i;

	for (i = 0; i < vi->max_queue_pairs; i++) {
		while (vi->rq[i].pages)
			__free_pages(get_a_page(&vi->rq[i], GFP_KERNEL), 0);
	}
}

static void free_receive_page_frags(struct virtnet_info *vi)
{
	int i;
	for (i = 0; i < vi->max_queue_pairs; i++)
		if (vi->rq[i].alloc_frag.page)
			put_page(vi->rq[i].alloc_frag.page);
}

static void free_unused_bufs(struct virtnet_info *vi)
{
	void *buf;
	int i;

	for (i = 0; i < vi->max_queue_pairs; i++) {
		struct virtqueue *vq = vi->sq[i].vq;
		while ((buf = virtqueue_detach_unused_buf(vq)) != NULL)
			dev_kfree_skb(buf);
	}

	for (i = 0; i < vi->max_queue_pairs; i++) {
		struct virtqueue *vq = vi->rq[i].vq;

		while ((buf = virtqueue_detach_unused_buf(vq)) != NULL) {
			if (vi->mergeable_rx_bufs) {
				unsigned long ctx = (unsigned long)buf;
				void *base = mergeable_ctx_to_buf_address(ctx);
				put_page(virt_to_head_page(base));
			} else if (vi->big_packets) {
				give_pages(&vi->rq[i], buf);
			} else {
				dev_kfree_skb(buf);
			}
		}
	}
}

static void virtnet_del_vqs(struct virtnet_info *vi)
{
	struct virtio_device *vdev = vi->vdev;

	virtnet_clean_affinity(vi, -1);

	vdev->config->del_vqs(vdev);

	virtnet_free_queues(vi);
}

static int virtnet_find_vqs(struct virtnet_info *vi)
{
	vq_callback_t **callbacks;
	struct virtqueue **vqs;
	int ret = -ENOMEM;
	int i, total_vqs;
	const char **names;

	/* We expect 1 RX virtqueue followed by 1 TX virtqueue, followed by
	 * possible N-1 RX/TX queue pairs used in multiqueue mode, followed by
	 * possible control vq.
	 */
	total_vqs = vi->max_queue_pairs * 2 +
		    virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VQ);

	/* Allocate space for find_vqs parameters */
	vqs = kzalloc(total_vqs * sizeof(*vqs), GFP_KERNEL);
	if (!vqs)
		goto err_vq;
	callbacks = kmalloc(total_vqs * sizeof(*callbacks), GFP_KERNEL);
	if (!callbacks)
		goto err_callback;
	names = kmalloc(total_vqs * sizeof(*names), GFP_KERNEL);
	if (!names)
		goto err_names;

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
	}

	ret = vi->vdev->config->find_vqs(vi->vdev, total_vqs, vqs, callbacks,
					 names);
	if (ret)
		goto err_find;

	if (vi->has_cvq) {
		vi->cvq = vqs[total_vqs - 1];
		if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VLAN))
			vi->dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
	}

	for (i = 0; i < vi->max_queue_pairs; i++) {
		vi->rq[i].vq = vqs[rxq2vq(i)];
		vi->sq[i].vq = vqs[txq2vq(i)];
	}

	kfree(names);
	kfree(callbacks);
	kfree(vqs);

	return 0;

err_find:
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

	vi->sq = kzalloc(sizeof(*vi->sq) * vi->max_queue_pairs, GFP_KERNEL);
	if (!vi->sq)
		goto err_sq;
	vi->rq = kzalloc(sizeof(*vi->rq) * vi->max_queue_pairs, GFP_KERNEL);
	if (!vi->rq)
		goto err_rq;

	INIT_DELAYED_WORK(&vi->refill, refill_work);
	for (i = 0; i < vi->max_queue_pairs; i++) {
		vi->rq[i].pages = NULL;
		netif_napi_add(vi->dev, &vi->rq[i].napi, virtnet_poll,
			       napi_weight);

		sg_init_table(vi->rq[i].sg, ARRAY_SIZE(vi->rq[i].sg));
		ewma_init(&vi->rq[i].mrg_avg_pkt_len, 1, RECEIVE_AVG_WEIGHT);
		sg_init_table(vi->sq[i].sg, ARRAY_SIZE(vi->sq[i].sg));
	}

	return 0;

err_rq:
	kfree(vi->sq);
err_sq:
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

	get_online_cpus();
	virtnet_set_affinity(vi);
	put_online_cpus();

	return 0;

err_free:
	virtnet_free_queues(vi);
err:
	return ret;
}

#ifdef CONFIG_SYSFS
static ssize_t mergeable_rx_buffer_size_show(struct netdev_rx_queue *queue,
		struct rx_queue_attribute *attribute, char *buf)
{
	struct virtnet_info *vi = netdev_priv(queue->dev);
	unsigned int queue_index = get_netdev_rx_queue_index(queue);
	struct ewma *avg;

	BUG_ON(queue_index >= vi->max_queue_pairs);
	avg = &vi->rq[queue_index].mrg_avg_pkt_len;
	return sprintf(buf, "%u\n", get_mergeable_buf_len(avg));
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

static int virtnet_probe(struct virtio_device *vdev)
{
	int i, err;
	struct net_device *dev;
	struct virtnet_info *vi;
	u16 max_queue_pairs;

	/* Find if host supports multiqueue virtio_net device */
	err = virtio_cread_feature(vdev, VIRTIO_NET_F_MQ,
				   struct virtio_net_config,
				   max_virtqueue_pairs, &max_queue_pairs);

	/* We need at least 2 queue's */
	if (err || max_queue_pairs < VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MIN ||
	    max_queue_pairs > VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX ||
	    !virtio_has_feature(vdev, VIRTIO_NET_F_CTRL_VQ))
		max_queue_pairs = 1;

	/* Allocate ourselves a network device with room for our info */
	dev = alloc_etherdev_mq(sizeof(struct virtnet_info), max_queue_pairs);
	if (!dev)
		return -ENOMEM;

	/* Set up network device as normal. */
	dev->priv_flags |= IFF_UNICAST_FLT | IFF_LIVE_ADDR_CHANGE;
	dev->netdev_ops = &virtnet_netdev;
	dev->features = NETIF_F_HIGHDMA;

	SET_ETHTOOL_OPS(dev, &virtnet_ethtool_ops);
	SET_NETDEV_DEV(dev, &vdev->dev);

	/* Do we support "hardware" checksums? */
	if (virtio_has_feature(vdev, VIRTIO_NET_F_CSUM)) {
		/* This opens up the world of extra features. */
		dev->hw_features |= NETIF_F_HW_CSUM|NETIF_F_SG|NETIF_F_FRAGLIST;
		if (csum)
			dev->features |= NETIF_F_HW_CSUM|NETIF_F_SG|NETIF_F_FRAGLIST;

		if (virtio_has_feature(vdev, VIRTIO_NET_F_GSO)) {
			dev->hw_features |= NETIF_F_TSO | NETIF_F_UFO
				| NETIF_F_TSO_ECN | NETIF_F_TSO6;
		}
		/* Individual feature bits: what can host handle? */
		if (virtio_has_feature(vdev, VIRTIO_NET_F_HOST_TSO4))
			dev->hw_features |= NETIF_F_TSO;
		if (virtio_has_feature(vdev, VIRTIO_NET_F_HOST_TSO6))
			dev->hw_features |= NETIF_F_TSO6;
		if (virtio_has_feature(vdev, VIRTIO_NET_F_HOST_ECN))
			dev->hw_features |= NETIF_F_TSO_ECN;
		if (virtio_has_feature(vdev, VIRTIO_NET_F_HOST_UFO))
			dev->hw_features |= NETIF_F_UFO;

		if (gso)
			dev->features |= dev->hw_features & (NETIF_F_ALL_TSO|NETIF_F_UFO);
		/* (!csum && gso) case will be fixed by register_netdev() */
	}
	if (virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_CSUM))
		dev->features |= NETIF_F_RXCSUM;

	dev->vlan_features = dev->features;

	/* Configuration may specify what MAC to use.  Otherwise random. */
	if (virtio_has_feature(vdev, VIRTIO_NET_F_MAC))
		virtio_cread_bytes(vdev,
				   offsetof(struct virtio_net_config, mac),
				   dev->dev_addr, dev->addr_len);
	else
		eth_hw_addr_random(dev);

	/* Set up our device-specific information */
	vi = netdev_priv(dev);
	vi->dev = dev;
	vi->vdev = vdev;
	vdev->priv = vi;
	vi->stats = alloc_percpu(struct virtnet_stats);
	err = -ENOMEM;
	if (vi->stats == NULL)
		goto free;

	for_each_possible_cpu(i) {
		struct virtnet_stats *virtnet_stats;
		virtnet_stats = per_cpu_ptr(vi->stats, i);
		u64_stats_init(&virtnet_stats->tx_syncp);
		u64_stats_init(&virtnet_stats->rx_syncp);
	}

	mutex_init(&vi->config_lock);
	vi->config_enable = true;
	INIT_WORK(&vi->config_work, virtnet_config_changed_work);

	/* If we can receive ANY GSO packets, we must allocate large ones. */
	if (virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_TSO4) ||
	    virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_TSO6) ||
	    virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_ECN) ||
	    virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_UFO))
		vi->big_packets = true;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_MRG_RXBUF))
		vi->mergeable_rx_bufs = true;

	if (virtio_has_feature(vdev, VIRTIO_F_ANY_LAYOUT))
		vi->any_header_sg = true;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_CTRL_VQ))
		vi->has_cvq = true;

	/* Use single tx/rx queue pair as default */
	vi->curr_queue_pairs = 1;
	vi->max_queue_pairs = max_queue_pairs;

	/* Allocate/initialize the rx/tx queues, and invoke find_vqs */
	err = init_vqs(vi);
	if (err)
		goto free_stats;

#ifdef CONFIG_SYSFS
	if (vi->mergeable_rx_bufs)
		dev->sysfs_rx_queue_group = &virtio_net_mrg_rx_group;
#endif
	netif_set_real_num_tx_queues(dev, vi->curr_queue_pairs);
	netif_set_real_num_rx_queues(dev, vi->curr_queue_pairs);

	err = register_netdev(dev);
	if (err) {
		pr_debug("virtio_net: registering device failed\n");
		goto free_vqs;
	}

	/* Last of all, set up some receive buffers. */
	for (i = 0; i < vi->curr_queue_pairs; i++) {
		try_fill_recv(&vi->rq[i], GFP_KERNEL);

		/* If we didn't even get one input buffer, we're useless. */
		if (vi->rq[i].vq->num_free ==
		    virtqueue_get_vring_size(vi->rq[i].vq)) {
			free_unused_bufs(vi);
			err = -ENOMEM;
			goto free_recv_bufs;
		}
	}

	vi->nb.notifier_call = &virtnet_cpu_callback;
	err = register_hotcpu_notifier(&vi->nb);
	if (err) {
		pr_debug("virtio_net: registering cpu notifier failed\n");
		goto free_recv_bufs;
	}

	/* Assume link up if device can't report link status,
	   otherwise get link status from config. */
	if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_STATUS)) {
		netif_carrier_off(dev);
		schedule_work(&vi->config_work);
	} else {
		vi->status = VIRTIO_NET_S_LINK_UP;
		netif_carrier_on(dev);
	}

	pr_debug("virtnet: registered device %s with %d RX and TX vq's\n",
		 dev->name, max_queue_pairs);

	return 0;

free_recv_bufs:
	free_receive_bufs(vi);
	unregister_netdev(dev);
free_vqs:
	cancel_delayed_work_sync(&vi->refill);
	free_receive_page_frags(vi);
	virtnet_del_vqs(vi);
free_stats:
	free_percpu(vi->stats);
free:
	free_netdev(dev);
	return err;
}

static void remove_vq_common(struct virtnet_info *vi)
{
	vi->vdev->config->reset(vi->vdev);

	/* Free unused buffers in both send and recv, if any. */
	free_unused_bufs(vi);

	free_receive_bufs(vi);

	free_receive_page_frags(vi);

	virtnet_del_vqs(vi);
}

static void virtnet_remove(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;

	unregister_hotcpu_notifier(&vi->nb);

	/* Prevent config work handler from accessing the device. */
	mutex_lock(&vi->config_lock);
	vi->config_enable = false;
	mutex_unlock(&vi->config_lock);

	unregister_netdev(vi->dev);

	remove_vq_common(vi);

	flush_work(&vi->config_work);

	free_percpu(vi->stats);
	free_netdev(vi->dev);
}

#ifdef CONFIG_PM_SLEEP
static int virtnet_freeze(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;
	int i;

	unregister_hotcpu_notifier(&vi->nb);

	/* Prevent config work handler from accessing the device */
	mutex_lock(&vi->config_lock);
	vi->config_enable = false;
	mutex_unlock(&vi->config_lock);

	netif_device_detach(vi->dev);
	cancel_delayed_work_sync(&vi->refill);

	if (netif_running(vi->dev))
		for (i = 0; i < vi->max_queue_pairs; i++) {
			napi_disable(&vi->rq[i].napi);
			netif_napi_del(&vi->rq[i].napi);
		}

	remove_vq_common(vi);

	flush_work(&vi->config_work);

	return 0;
}

static int virtnet_restore(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;
	int err, i;

	err = init_vqs(vi);
	if (err)
		return err;

	if (netif_running(vi->dev)) {
		for (i = 0; i < vi->curr_queue_pairs; i++)
			if (!try_fill_recv(&vi->rq[i], GFP_KERNEL))
				schedule_delayed_work(&vi->refill, 0);

		for (i = 0; i < vi->max_queue_pairs; i++)
			virtnet_napi_enable(&vi->rq[i]);
	}

	netif_device_attach(vi->dev);

	mutex_lock(&vi->config_lock);
	vi->config_enable = true;
	mutex_unlock(&vi->config_lock);

	rtnl_lock();
	virtnet_set_queues(vi, vi->curr_queue_pairs);
	rtnl_unlock();

	err = register_hotcpu_notifier(&vi->nb);
	if (err)
		return err;

	return 0;
}
#endif

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_NET, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_NET_F_CSUM, VIRTIO_NET_F_GUEST_CSUM,
	VIRTIO_NET_F_GSO, VIRTIO_NET_F_MAC,
	VIRTIO_NET_F_HOST_TSO4, VIRTIO_NET_F_HOST_UFO, VIRTIO_NET_F_HOST_TSO6,
	VIRTIO_NET_F_HOST_ECN, VIRTIO_NET_F_GUEST_TSO4, VIRTIO_NET_F_GUEST_TSO6,
	VIRTIO_NET_F_GUEST_ECN, VIRTIO_NET_F_GUEST_UFO,
	VIRTIO_NET_F_MRG_RXBUF, VIRTIO_NET_F_STATUS, VIRTIO_NET_F_CTRL_VQ,
	VIRTIO_NET_F_CTRL_RX, VIRTIO_NET_F_CTRL_VLAN,
	VIRTIO_NET_F_GUEST_ANNOUNCE, VIRTIO_NET_F_MQ,
	VIRTIO_NET_F_CTRL_MAC_ADDR,
	VIRTIO_F_ANY_LAYOUT,
};

static struct virtio_driver virtio_net_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtnet_probe,
	.remove =	virtnet_remove,
	.config_changed = virtnet_config_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze =	virtnet_freeze,
	.restore =	virtnet_restore,
#endif
};

module_virtio_driver(virtio_net_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio network driver");
MODULE_LICENSE("GPL");
