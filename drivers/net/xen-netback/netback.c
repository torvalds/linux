/*
 * Back-end of the driver for virtual network devices. This portion of the
 * driver exports a 'unified' network-device interface that can be accessed
 * by any operating system that implements a compatible front end. A
 * reference front-end implementation can be found in:
 *  drivers/net/xen-netfront.c
 *
 * Copyright (c) 2002-2005, K A Fraser
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "common.h"

#include <linux/kthread.h>
#include <linux/if_vlan.h>
#include <linux/udp.h>
#include <linux/highmem.h>

#include <net/tcp.h>

#include <xen/xen.h>
#include <xen/events.h>
#include <xen/interface/memory.h>
#include <xen/page.h>

#include <asm/xen/hypercall.h>

/* Provide an option to disable split event channels at load time as
 * event channels are limited resource. Split event channels are
 * enabled by default.
 */
bool separate_tx_rx_irq = true;
module_param(separate_tx_rx_irq, bool, 0644);

/* The time that packets can stay on the guest Rx internal queue
 * before they are dropped.
 */
unsigned int rx_drain_timeout_msecs = 10000;
module_param(rx_drain_timeout_msecs, uint, 0444);

/* The length of time before the frontend is considered unresponsive
 * because it isn't providing Rx slots.
 */
unsigned int rx_stall_timeout_msecs = 60000;
module_param(rx_stall_timeout_msecs, uint, 0444);

unsigned int xenvif_max_queues;
module_param_named(max_queues, xenvif_max_queues, uint, 0644);
MODULE_PARM_DESC(max_queues,
		 "Maximum number of queues per virtual interface");

/*
 * This is the maximum slots a skb can have. If a guest sends a skb
 * which exceeds this limit it is considered malicious.
 */
#define FATAL_SKB_SLOTS_DEFAULT 20
static unsigned int fatal_skb_slots = FATAL_SKB_SLOTS_DEFAULT;
module_param(fatal_skb_slots, uint, 0444);

/* The amount to copy out of the first guest Tx slot into the skb's
 * linear area.  If the first slot has more data, it will be mapped
 * and put into the first frag.
 *
 * This is sized to avoid pulling headers from the frags for most
 * TCP/IP packets.
 */
#define XEN_NETBACK_TX_COPY_LEN 128

/* This is the maximum number of flows in the hash cache. */
#define XENVIF_HASH_CACHE_SIZE_DEFAULT 64
unsigned int xenvif_hash_cache_size = XENVIF_HASH_CACHE_SIZE_DEFAULT;
module_param_named(hash_cache_size, xenvif_hash_cache_size, uint, 0644);
MODULE_PARM_DESC(hash_cache_size, "Number of flows in the hash cache");

static void xenvif_idx_release(struct xenvif_queue *queue, u16 pending_idx,
			       u8 status);

static void make_tx_response(struct xenvif_queue *queue,
			     struct xen_netif_tx_request *txp,
			     unsigned int extra_count,
			     s8       st);
static void push_tx_responses(struct xenvif_queue *queue);

static inline int tx_work_todo(struct xenvif_queue *queue);

static struct xen_netif_rx_response *make_rx_response(struct xenvif_queue *queue,
					     u16      id,
					     s8       st,
					     u16      offset,
					     u16      size,
					     u16      flags);

static inline unsigned long idx_to_pfn(struct xenvif_queue *queue,
				       u16 idx)
{
	return page_to_pfn(queue->mmap_pages[idx]);
}

static inline unsigned long idx_to_kaddr(struct xenvif_queue *queue,
					 u16 idx)
{
	return (unsigned long)pfn_to_kaddr(idx_to_pfn(queue, idx));
}

#define callback_param(vif, pending_idx) \
	(vif->pending_tx_info[pending_idx].callback_struct)

/* Find the containing VIF's structure from a pointer in pending_tx_info array
 */
static inline struct xenvif_queue *ubuf_to_queue(const struct ubuf_info *ubuf)
{
	u16 pending_idx = ubuf->desc;
	struct pending_tx_info *temp =
		container_of(ubuf, struct pending_tx_info, callback_struct);
	return container_of(temp - pending_idx,
			    struct xenvif_queue,
			    pending_tx_info[0]);
}

static u16 frag_get_pending_idx(skb_frag_t *frag)
{
	return (u16)frag->page_offset;
}

static void frag_set_pending_idx(skb_frag_t *frag, u16 pending_idx)
{
	frag->page_offset = pending_idx;
}

static inline pending_ring_idx_t pending_index(unsigned i)
{
	return i & (MAX_PENDING_REQS-1);
}

static bool xenvif_rx_ring_slots_available(struct xenvif_queue *queue)
{
	RING_IDX prod, cons;
	struct sk_buff *skb;
	int needed;

	skb = skb_peek(&queue->rx_queue);
	if (!skb)
		return false;

	needed = DIV_ROUND_UP(skb->len, XEN_PAGE_SIZE);
	if (skb_is_gso(skb))
		needed++;
	if (skb->sw_hash)
		needed++;

	do {
		prod = queue->rx.sring->req_prod;
		cons = queue->rx.req_cons;

		if (prod - cons >= needed)
			return true;

		queue->rx.sring->req_event = prod + 1;

		/* Make sure event is visible before we check prod
		 * again.
		 */
		mb();
	} while (queue->rx.sring->req_prod != prod);

	return false;
}

void xenvif_rx_queue_tail(struct xenvif_queue *queue, struct sk_buff *skb)
{
	unsigned long flags;

	spin_lock_irqsave(&queue->rx_queue.lock, flags);

	__skb_queue_tail(&queue->rx_queue, skb);

	queue->rx_queue_len += skb->len;
	if (queue->rx_queue_len > queue->rx_queue_max)
		netif_tx_stop_queue(netdev_get_tx_queue(queue->vif->dev, queue->id));

	spin_unlock_irqrestore(&queue->rx_queue.lock, flags);
}

static struct sk_buff *xenvif_rx_dequeue(struct xenvif_queue *queue)
{
	struct sk_buff *skb;

	spin_lock_irq(&queue->rx_queue.lock);

	skb = __skb_dequeue(&queue->rx_queue);
	if (skb)
		queue->rx_queue_len -= skb->len;

	spin_unlock_irq(&queue->rx_queue.lock);

	return skb;
}

static void xenvif_rx_queue_maybe_wake(struct xenvif_queue *queue)
{
	spin_lock_irq(&queue->rx_queue.lock);

	if (queue->rx_queue_len < queue->rx_queue_max)
		netif_tx_wake_queue(netdev_get_tx_queue(queue->vif->dev, queue->id));

	spin_unlock_irq(&queue->rx_queue.lock);
}


static void xenvif_rx_queue_purge(struct xenvif_queue *queue)
{
	struct sk_buff *skb;
	while ((skb = xenvif_rx_dequeue(queue)) != NULL)
		kfree_skb(skb);
}

static void xenvif_rx_queue_drop_expired(struct xenvif_queue *queue)
{
	struct sk_buff *skb;

	for(;;) {
		skb = skb_peek(&queue->rx_queue);
		if (!skb)
			break;
		if (time_before(jiffies, XENVIF_RX_CB(skb)->expires))
			break;
		xenvif_rx_dequeue(queue);
		kfree_skb(skb);
	}
}

struct netrx_pending_operations {
	unsigned copy_prod, copy_cons;
	unsigned meta_prod, meta_cons;
	struct gnttab_copy *copy;
	struct xenvif_rx_meta *meta;
	int copy_off;
	grant_ref_t copy_gref;
};

static struct xenvif_rx_meta *get_next_rx_buffer(struct xenvif_queue *queue,
						 struct netrx_pending_operations *npo)
{
	struct xenvif_rx_meta *meta;
	struct xen_netif_rx_request req;

	RING_COPY_REQUEST(&queue->rx, queue->rx.req_cons++, &req);

	meta = npo->meta + npo->meta_prod++;
	meta->gso_type = XEN_NETIF_GSO_TYPE_NONE;
	meta->gso_size = 0;
	meta->size = 0;
	meta->id = req.id;

	npo->copy_off = 0;
	npo->copy_gref = req.gref;

	return meta;
}

struct gop_frag_copy {
	struct xenvif_queue *queue;
	struct netrx_pending_operations *npo;
	struct xenvif_rx_meta *meta;
	int head;
	int gso_type;
	int protocol;
	int hash_present;

	struct page *page;
};

static void xenvif_setup_copy_gop(unsigned long gfn,
				  unsigned int offset,
				  unsigned int *len,
				  struct gop_frag_copy *info)
{
	struct gnttab_copy *copy_gop;
	struct xen_page_foreign *foreign;
	/* Convenient aliases */
	struct xenvif_queue *queue = info->queue;
	struct netrx_pending_operations *npo = info->npo;
	struct page *page = info->page;

	BUG_ON(npo->copy_off > MAX_BUFFER_OFFSET);

	if (npo->copy_off == MAX_BUFFER_OFFSET)
		info->meta = get_next_rx_buffer(queue, npo);

	if (npo->copy_off + *len > MAX_BUFFER_OFFSET)
		*len = MAX_BUFFER_OFFSET - npo->copy_off;

	copy_gop = npo->copy + npo->copy_prod++;
	copy_gop->flags = GNTCOPY_dest_gref;
	copy_gop->len = *len;

	foreign = xen_page_foreign(page);
	if (foreign) {
		copy_gop->source.domid = foreign->domid;
		copy_gop->source.u.ref = foreign->gref;
		copy_gop->flags |= GNTCOPY_source_gref;
	} else {
		copy_gop->source.domid = DOMID_SELF;
		copy_gop->source.u.gmfn = gfn;
	}
	copy_gop->source.offset = offset;

	copy_gop->dest.domid = queue->vif->domid;
	copy_gop->dest.offset = npo->copy_off;
	copy_gop->dest.u.ref = npo->copy_gref;

	npo->copy_off += *len;
	info->meta->size += *len;

	if (!info->head)
		return;

	/* Leave a gap for the GSO descriptor. */
	if ((1 << info->gso_type) & queue->vif->gso_mask)
		queue->rx.req_cons++;

	/* Leave a gap for the hash extra segment. */
	if (info->hash_present)
		queue->rx.req_cons++;

	info->head = 0; /* There must be something in this buffer now */
}

static void xenvif_gop_frag_copy_grant(unsigned long gfn,
				       unsigned offset,
				       unsigned int len,
				       void *data)
{
	unsigned int bytes;

	while (len) {
		bytes = len;
		xenvif_setup_copy_gop(gfn, offset, &bytes, data);
		offset += bytes;
		len -= bytes;
	}
}

/*
 * Set up the grant operations for this fragment. If it's a flipping
 * interface, we also set up the unmap request from here.
 */
static void xenvif_gop_frag_copy(struct xenvif_queue *queue, struct sk_buff *skb,
				 struct netrx_pending_operations *npo,
				 struct page *page, unsigned long size,
				 unsigned long offset, int *head)
{
	struct gop_frag_copy info = {
		.queue = queue,
		.npo = npo,
		.head = *head,
		.gso_type = XEN_NETIF_GSO_TYPE_NONE,
		/* xenvif_set_skb_hash() will have either set a s/w
		 * hash or cleared the hash depending on
		 * whether the the frontend wants a hash for this skb.
		 */
		.hash_present = skb->sw_hash,
	};
	unsigned long bytes;

	if (skb_is_gso(skb)) {
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4)
			info.gso_type = XEN_NETIF_GSO_TYPE_TCPV4;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
			info.gso_type = XEN_NETIF_GSO_TYPE_TCPV6;
	}

	/* Data must not cross a page boundary. */
	BUG_ON(size + offset > PAGE_SIZE<<compound_order(page));

	info.meta = npo->meta + npo->meta_prod - 1;

	/* Skip unused frames from start of page */
	page += offset >> PAGE_SHIFT;
	offset &= ~PAGE_MASK;

	while (size > 0) {
		BUG_ON(offset >= PAGE_SIZE);

		bytes = PAGE_SIZE - offset;
		if (bytes > size)
			bytes = size;

		info.page = page;
		gnttab_foreach_grant_in_range(page, offset, bytes,
					      xenvif_gop_frag_copy_grant,
					      &info);
		size -= bytes;
		offset = 0;

		/* Next page */
		if (size) {
			BUG_ON(!PageCompound(page));
			page++;
		}
	}

	*head = info.head;
}

/*
 * Prepare an SKB to be transmitted to the frontend.
 *
 * This function is responsible for allocating grant operations, meta
 * structures, etc.
 *
 * It returns the number of meta structures consumed. The number of
 * ring slots used is always equal to the number of meta slots used
 * plus the number of GSO descriptors used. Currently, we use either
 * zero GSO descriptors (for non-GSO packets) or one descriptor (for
 * frontend-side LRO).
 */
static int xenvif_gop_skb(struct sk_buff *skb,
			  struct netrx_pending_operations *npo,
			  struct xenvif_queue *queue)
{
	struct xenvif *vif = netdev_priv(skb->dev);
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int i;
	struct xen_netif_rx_request req;
	struct xenvif_rx_meta *meta;
	unsigned char *data;
	int head = 1;
	int old_meta_prod;
	int gso_type;

	old_meta_prod = npo->meta_prod;

	gso_type = XEN_NETIF_GSO_TYPE_NONE;
	if (skb_is_gso(skb)) {
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4)
			gso_type = XEN_NETIF_GSO_TYPE_TCPV4;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
			gso_type = XEN_NETIF_GSO_TYPE_TCPV6;
	}

	/* Set up a GSO prefix descriptor, if necessary */
	if ((1 << gso_type) & vif->gso_prefix_mask) {
		RING_COPY_REQUEST(&queue->rx, queue->rx.req_cons++, &req);
		meta = npo->meta + npo->meta_prod++;
		meta->gso_type = gso_type;
		meta->gso_size = skb_shinfo(skb)->gso_size;
		meta->size = 0;
		meta->id = req.id;
	}

	RING_COPY_REQUEST(&queue->rx, queue->rx.req_cons++, &req);
	meta = npo->meta + npo->meta_prod++;

	if ((1 << gso_type) & vif->gso_mask) {
		meta->gso_type = gso_type;
		meta->gso_size = skb_shinfo(skb)->gso_size;
	} else {
		meta->gso_type = XEN_NETIF_GSO_TYPE_NONE;
		meta->gso_size = 0;
	}

	meta->size = 0;
	meta->id = req.id;
	npo->copy_off = 0;
	npo->copy_gref = req.gref;

	data = skb->data;
	while (data < skb_tail_pointer(skb)) {
		unsigned int offset = offset_in_page(data);
		unsigned int len = PAGE_SIZE - offset;

		if (data + len > skb_tail_pointer(skb))
			len = skb_tail_pointer(skb) - data;

		xenvif_gop_frag_copy(queue, skb, npo,
				     virt_to_page(data), len, offset, &head);
		data += len;
	}

	for (i = 0; i < nr_frags; i++) {
		xenvif_gop_frag_copy(queue, skb, npo,
				     skb_frag_page(&skb_shinfo(skb)->frags[i]),
				     skb_frag_size(&skb_shinfo(skb)->frags[i]),
				     skb_shinfo(skb)->frags[i].page_offset,
				     &head);
	}

	return npo->meta_prod - old_meta_prod;
}

/*
 * This is a twin to xenvif_gop_skb.  Assume that xenvif_gop_skb was
 * used to set up the operations on the top of
 * netrx_pending_operations, which have since been done.  Check that
 * they didn't give any errors and advance over them.
 */
static int xenvif_check_gop(struct xenvif *vif, int nr_meta_slots,
			    struct netrx_pending_operations *npo)
{
	struct gnttab_copy     *copy_op;
	int status = XEN_NETIF_RSP_OKAY;
	int i;

	for (i = 0; i < nr_meta_slots; i++) {
		copy_op = npo->copy + npo->copy_cons++;
		if (copy_op->status != GNTST_okay) {
			netdev_dbg(vif->dev,
				   "Bad status %d from copy to DOM%d.\n",
				   copy_op->status, vif->domid);
			status = XEN_NETIF_RSP_ERROR;
		}
	}

	return status;
}

static void xenvif_add_frag_responses(struct xenvif_queue *queue, int status,
				      struct xenvif_rx_meta *meta,
				      int nr_meta_slots)
{
	int i;
	unsigned long offset;

	/* No fragments used */
	if (nr_meta_slots <= 1)
		return;

	nr_meta_slots--;

	for (i = 0; i < nr_meta_slots; i++) {
		int flags;
		if (i == nr_meta_slots - 1)
			flags = 0;
		else
			flags = XEN_NETRXF_more_data;

		offset = 0;
		make_rx_response(queue, meta[i].id, status, offset,
				 meta[i].size, flags);
	}
}

void xenvif_kick_thread(struct xenvif_queue *queue)
{
	wake_up(&queue->wq);
}

static void xenvif_rx_action(struct xenvif_queue *queue)
{
	struct xenvif *vif = queue->vif;
	s8 status;
	u16 flags;
	struct xen_netif_rx_response *resp;
	struct sk_buff_head rxq;
	struct sk_buff *skb;
	LIST_HEAD(notify);
	int ret;
	unsigned long offset;
	bool need_to_notify = false;

	struct netrx_pending_operations npo = {
		.copy  = queue->grant_copy_op,
		.meta  = queue->meta,
	};

	skb_queue_head_init(&rxq);

	while (xenvif_rx_ring_slots_available(queue)
	       && (skb = xenvif_rx_dequeue(queue)) != NULL) {
		queue->last_rx_time = jiffies;

		XENVIF_RX_CB(skb)->meta_slots_used = xenvif_gop_skb(skb, &npo, queue);

		__skb_queue_tail(&rxq, skb);
	}

	BUG_ON(npo.meta_prod > ARRAY_SIZE(queue->meta));

	if (!npo.copy_prod)
		goto done;

	BUG_ON(npo.copy_prod > MAX_GRANT_COPY_OPS);
	gnttab_batch_copy(queue->grant_copy_op, npo.copy_prod);

	while ((skb = __skb_dequeue(&rxq)) != NULL) {
		struct xen_netif_extra_info *extra = NULL;

		if ((1 << queue->meta[npo.meta_cons].gso_type) &
		    vif->gso_prefix_mask) {
			resp = RING_GET_RESPONSE(&queue->rx,
						 queue->rx.rsp_prod_pvt++);

			resp->flags = XEN_NETRXF_gso_prefix | XEN_NETRXF_more_data;

			resp->offset = queue->meta[npo.meta_cons].gso_size;
			resp->id = queue->meta[npo.meta_cons].id;
			resp->status = XENVIF_RX_CB(skb)->meta_slots_used;

			npo.meta_cons++;
			XENVIF_RX_CB(skb)->meta_slots_used--;
		}


		queue->stats.tx_bytes += skb->len;
		queue->stats.tx_packets++;

		status = xenvif_check_gop(vif,
					  XENVIF_RX_CB(skb)->meta_slots_used,
					  &npo);

		if (XENVIF_RX_CB(skb)->meta_slots_used == 1)
			flags = 0;
		else
			flags = XEN_NETRXF_more_data;

		if (skb->ip_summed == CHECKSUM_PARTIAL) /* local packet? */
			flags |= XEN_NETRXF_csum_blank | XEN_NETRXF_data_validated;
		else if (skb->ip_summed == CHECKSUM_UNNECESSARY)
			/* remote but checksummed. */
			flags |= XEN_NETRXF_data_validated;

		offset = 0;
		resp = make_rx_response(queue, queue->meta[npo.meta_cons].id,
					status, offset,
					queue->meta[npo.meta_cons].size,
					flags);

		if ((1 << queue->meta[npo.meta_cons].gso_type) &
		    vif->gso_mask) {
			extra = (struct xen_netif_extra_info *)
				RING_GET_RESPONSE(&queue->rx,
						  queue->rx.rsp_prod_pvt++);

			resp->flags |= XEN_NETRXF_extra_info;

			extra->u.gso.type = queue->meta[npo.meta_cons].gso_type;
			extra->u.gso.size = queue->meta[npo.meta_cons].gso_size;
			extra->u.gso.pad = 0;
			extra->u.gso.features = 0;

			extra->type = XEN_NETIF_EXTRA_TYPE_GSO;
			extra->flags = 0;
		}

		if (skb->sw_hash) {
			/* Since the skb got here via xenvif_select_queue()
			 * we know that the hash has been re-calculated
			 * according to a configuration set by the frontend
			 * and therefore we know that it is legitimate to
			 * pass it to the frontend.
			 */
			if (resp->flags & XEN_NETRXF_extra_info)
				extra->flags |= XEN_NETIF_EXTRA_FLAG_MORE;
			else
				resp->flags |= XEN_NETRXF_extra_info;

			extra = (struct xen_netif_extra_info *)
				RING_GET_RESPONSE(&queue->rx,
						  queue->rx.rsp_prod_pvt++);

			extra->u.hash.algorithm =
				XEN_NETIF_CTRL_HASH_ALGORITHM_TOEPLITZ;

			if (skb->l4_hash)
				extra->u.hash.type =
					skb->protocol == htons(ETH_P_IP) ?
					_XEN_NETIF_CTRL_HASH_TYPE_IPV4_TCP :
					_XEN_NETIF_CTRL_HASH_TYPE_IPV6_TCP;
			else
				extra->u.hash.type =
					skb->protocol == htons(ETH_P_IP) ?
					_XEN_NETIF_CTRL_HASH_TYPE_IPV4 :
					_XEN_NETIF_CTRL_HASH_TYPE_IPV6;

			*(uint32_t *)extra->u.hash.value =
				skb_get_hash_raw(skb);

			extra->type = XEN_NETIF_EXTRA_TYPE_HASH;
			extra->flags = 0;
		}

		xenvif_add_frag_responses(queue, status,
					  queue->meta + npo.meta_cons + 1,
					  XENVIF_RX_CB(skb)->meta_slots_used);

		RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&queue->rx, ret);

		need_to_notify |= !!ret;

		npo.meta_cons += XENVIF_RX_CB(skb)->meta_slots_used;
		dev_kfree_skb(skb);
	}

done:
	if (need_to_notify)
		notify_remote_via_irq(queue->rx_irq);
}

void xenvif_napi_schedule_or_enable_events(struct xenvif_queue *queue)
{
	int more_to_do;

	RING_FINAL_CHECK_FOR_REQUESTS(&queue->tx, more_to_do);

	if (more_to_do)
		napi_schedule(&queue->napi);
}

static void tx_add_credit(struct xenvif_queue *queue)
{
	unsigned long max_burst, max_credit;

	/*
	 * Allow a burst big enough to transmit a jumbo packet of up to 128kB.
	 * Otherwise the interface can seize up due to insufficient credit.
	 */
	max_burst = max(131072UL, queue->credit_bytes);

	/* Take care that adding a new chunk of credit doesn't wrap to zero. */
	max_credit = queue->remaining_credit + queue->credit_bytes;
	if (max_credit < queue->remaining_credit)
		max_credit = ULONG_MAX; /* wrapped: clamp to ULONG_MAX */

	queue->remaining_credit = min(max_credit, max_burst);
}

void xenvif_tx_credit_callback(unsigned long data)
{
	struct xenvif_queue *queue = (struct xenvif_queue *)data;
	tx_add_credit(queue);
	xenvif_napi_schedule_or_enable_events(queue);
}

static void xenvif_tx_err(struct xenvif_queue *queue,
			  struct xen_netif_tx_request *txp,
			  unsigned int extra_count, RING_IDX end)
{
	RING_IDX cons = queue->tx.req_cons;
	unsigned long flags;

	do {
		spin_lock_irqsave(&queue->response_lock, flags);
		make_tx_response(queue, txp, extra_count, XEN_NETIF_RSP_ERROR);
		push_tx_responses(queue);
		spin_unlock_irqrestore(&queue->response_lock, flags);
		if (cons == end)
			break;
		RING_COPY_REQUEST(&queue->tx, cons++, txp);
		extra_count = 0; /* only the first frag can have extras */
	} while (1);
	queue->tx.req_cons = cons;
}

static void xenvif_fatal_tx_err(struct xenvif *vif)
{
	netdev_err(vif->dev, "fatal error; disabling device\n");
	vif->disabled = true;
	/* Disable the vif from queue 0's kthread */
	if (vif->queues)
		xenvif_kick_thread(&vif->queues[0]);
}

static int xenvif_count_requests(struct xenvif_queue *queue,
				 struct xen_netif_tx_request *first,
				 unsigned int extra_count,
				 struct xen_netif_tx_request *txp,
				 int work_to_do)
{
	RING_IDX cons = queue->tx.req_cons;
	int slots = 0;
	int drop_err = 0;
	int more_data;

	if (!(first->flags & XEN_NETTXF_more_data))
		return 0;

	do {
		struct xen_netif_tx_request dropped_tx = { 0 };

		if (slots >= work_to_do) {
			netdev_err(queue->vif->dev,
				   "Asked for %d slots but exceeds this limit\n",
				   work_to_do);
			xenvif_fatal_tx_err(queue->vif);
			return -ENODATA;
		}

		/* This guest is really using too many slots and
		 * considered malicious.
		 */
		if (unlikely(slots >= fatal_skb_slots)) {
			netdev_err(queue->vif->dev,
				   "Malicious frontend using %d slots, threshold %u\n",
				   slots, fatal_skb_slots);
			xenvif_fatal_tx_err(queue->vif);
			return -E2BIG;
		}

		/* Xen network protocol had implicit dependency on
		 * MAX_SKB_FRAGS. XEN_NETBK_LEGACY_SLOTS_MAX is set to
		 * the historical MAX_SKB_FRAGS value 18 to honor the
		 * same behavior as before. Any packet using more than
		 * 18 slots but less than fatal_skb_slots slots is
		 * dropped
		 */
		if (!drop_err && slots >= XEN_NETBK_LEGACY_SLOTS_MAX) {
			if (net_ratelimit())
				netdev_dbg(queue->vif->dev,
					   "Too many slots (%d) exceeding limit (%d), dropping packet\n",
					   slots, XEN_NETBK_LEGACY_SLOTS_MAX);
			drop_err = -E2BIG;
		}

		if (drop_err)
			txp = &dropped_tx;

		RING_COPY_REQUEST(&queue->tx, cons + slots, txp);

		/* If the guest submitted a frame >= 64 KiB then
		 * first->size overflowed and following slots will
		 * appear to be larger than the frame.
		 *
		 * This cannot be fatal error as there are buggy
		 * frontends that do this.
		 *
		 * Consume all slots and drop the packet.
		 */
		if (!drop_err && txp->size > first->size) {
			if (net_ratelimit())
				netdev_dbg(queue->vif->dev,
					   "Invalid tx request, slot size %u > remaining size %u\n",
					   txp->size, first->size);
			drop_err = -EIO;
		}

		first->size -= txp->size;
		slots++;

		if (unlikely((txp->offset + txp->size) > XEN_PAGE_SIZE)) {
			netdev_err(queue->vif->dev, "Cross page boundary, txp->offset: %u, size: %u\n",
				 txp->offset, txp->size);
			xenvif_fatal_tx_err(queue->vif);
			return -EINVAL;
		}

		more_data = txp->flags & XEN_NETTXF_more_data;

		if (!drop_err)
			txp++;

	} while (more_data);

	if (drop_err) {
		xenvif_tx_err(queue, first, extra_count, cons + slots);
		return drop_err;
	}

	return slots;
}


struct xenvif_tx_cb {
	u16 pending_idx;
};

#define XENVIF_TX_CB(skb) ((struct xenvif_tx_cb *)(skb)->cb)

static inline void xenvif_tx_create_map_op(struct xenvif_queue *queue,
					   u16 pending_idx,
					   struct xen_netif_tx_request *txp,
					   unsigned int extra_count,
					   struct gnttab_map_grant_ref *mop)
{
	queue->pages_to_map[mop-queue->tx_map_ops] = queue->mmap_pages[pending_idx];
	gnttab_set_map_op(mop, idx_to_kaddr(queue, pending_idx),
			  GNTMAP_host_map | GNTMAP_readonly,
			  txp->gref, queue->vif->domid);

	memcpy(&queue->pending_tx_info[pending_idx].req, txp,
	       sizeof(*txp));
	queue->pending_tx_info[pending_idx].extra_count = extra_count;
}

static inline struct sk_buff *xenvif_alloc_skb(unsigned int size)
{
	struct sk_buff *skb =
		alloc_skb(size + NET_SKB_PAD + NET_IP_ALIGN,
			  GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(skb == NULL))
		return NULL;

	/* Packets passed to netif_rx() must have some headroom. */
	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);

	/* Initialize it here to avoid later surprises */
	skb_shinfo(skb)->destructor_arg = NULL;

	return skb;
}

static struct gnttab_map_grant_ref *xenvif_get_requests(struct xenvif_queue *queue,
							struct sk_buff *skb,
							struct xen_netif_tx_request *txp,
							struct gnttab_map_grant_ref *gop,
							unsigned int frag_overflow,
							struct sk_buff *nskb)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	skb_frag_t *frags = shinfo->frags;
	u16 pending_idx = XENVIF_TX_CB(skb)->pending_idx;
	int start;
	pending_ring_idx_t index;
	unsigned int nr_slots;

	nr_slots = shinfo->nr_frags;

	/* Skip first skb fragment if it is on same page as header fragment. */
	start = (frag_get_pending_idx(&shinfo->frags[0]) == pending_idx);

	for (shinfo->nr_frags = start; shinfo->nr_frags < nr_slots;
	     shinfo->nr_frags++, txp++, gop++) {
		index = pending_index(queue->pending_cons++);
		pending_idx = queue->pending_ring[index];
		xenvif_tx_create_map_op(queue, pending_idx, txp, 0, gop);
		frag_set_pending_idx(&frags[shinfo->nr_frags], pending_idx);
	}

	if (frag_overflow) {

		shinfo = skb_shinfo(nskb);
		frags = shinfo->frags;

		for (shinfo->nr_frags = 0; shinfo->nr_frags < frag_overflow;
		     shinfo->nr_frags++, txp++, gop++) {
			index = pending_index(queue->pending_cons++);
			pending_idx = queue->pending_ring[index];
			xenvif_tx_create_map_op(queue, pending_idx, txp, 0,
						gop);
			frag_set_pending_idx(&frags[shinfo->nr_frags],
					     pending_idx);
		}

		skb_shinfo(skb)->frag_list = nskb;
	}

	return gop;
}

static inline void xenvif_grant_handle_set(struct xenvif_queue *queue,
					   u16 pending_idx,
					   grant_handle_t handle)
{
	if (unlikely(queue->grant_tx_handle[pending_idx] !=
		     NETBACK_INVALID_HANDLE)) {
		netdev_err(queue->vif->dev,
			   "Trying to overwrite active handle! pending_idx: 0x%x\n",
			   pending_idx);
		BUG();
	}
	queue->grant_tx_handle[pending_idx] = handle;
}

static inline void xenvif_grant_handle_reset(struct xenvif_queue *queue,
					     u16 pending_idx)
{
	if (unlikely(queue->grant_tx_handle[pending_idx] ==
		     NETBACK_INVALID_HANDLE)) {
		netdev_err(queue->vif->dev,
			   "Trying to unmap invalid handle! pending_idx: 0x%x\n",
			   pending_idx);
		BUG();
	}
	queue->grant_tx_handle[pending_idx] = NETBACK_INVALID_HANDLE;
}

static int xenvif_tx_check_gop(struct xenvif_queue *queue,
			       struct sk_buff *skb,
			       struct gnttab_map_grant_ref **gopp_map,
			       struct gnttab_copy **gopp_copy)
{
	struct gnttab_map_grant_ref *gop_map = *gopp_map;
	u16 pending_idx = XENVIF_TX_CB(skb)->pending_idx;
	/* This always points to the shinfo of the skb being checked, which
	 * could be either the first or the one on the frag_list
	 */
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	/* If this is non-NULL, we are currently checking the frag_list skb, and
	 * this points to the shinfo of the first one
	 */
	struct skb_shared_info *first_shinfo = NULL;
	int nr_frags = shinfo->nr_frags;
	const bool sharedslot = nr_frags &&
				frag_get_pending_idx(&shinfo->frags[0]) == pending_idx;
	int i, err;

	/* Check status of header. */
	err = (*gopp_copy)->status;
	if (unlikely(err)) {
		if (net_ratelimit())
			netdev_dbg(queue->vif->dev,
				   "Grant copy of header failed! status: %d pending_idx: %u ref: %u\n",
				   (*gopp_copy)->status,
				   pending_idx,
				   (*gopp_copy)->source.u.ref);
		/* The first frag might still have this slot mapped */
		if (!sharedslot)
			xenvif_idx_release(queue, pending_idx,
					   XEN_NETIF_RSP_ERROR);
	}
	(*gopp_copy)++;

check_frags:
	for (i = 0; i < nr_frags; i++, gop_map++) {
		int j, newerr;

		pending_idx = frag_get_pending_idx(&shinfo->frags[i]);

		/* Check error status: if okay then remember grant handle. */
		newerr = gop_map->status;

		if (likely(!newerr)) {
			xenvif_grant_handle_set(queue,
						pending_idx,
						gop_map->handle);
			/* Had a previous error? Invalidate this fragment. */
			if (unlikely(err)) {
				xenvif_idx_unmap(queue, pending_idx);
				/* If the mapping of the first frag was OK, but
				 * the header's copy failed, and they are
				 * sharing a slot, send an error
				 */
				if (i == 0 && sharedslot)
					xenvif_idx_release(queue, pending_idx,
							   XEN_NETIF_RSP_ERROR);
				else
					xenvif_idx_release(queue, pending_idx,
							   XEN_NETIF_RSP_OKAY);
			}
			continue;
		}

		/* Error on this fragment: respond to client with an error. */
		if (net_ratelimit())
			netdev_dbg(queue->vif->dev,
				   "Grant map of %d. frag failed! status: %d pending_idx: %u ref: %u\n",
				   i,
				   gop_map->status,
				   pending_idx,
				   gop_map->ref);

		xenvif_idx_release(queue, pending_idx, XEN_NETIF_RSP_ERROR);

		/* Not the first error? Preceding frags already invalidated. */
		if (err)
			continue;

		/* First error: if the header haven't shared a slot with the
		 * first frag, release it as well.
		 */
		if (!sharedslot)
			xenvif_idx_release(queue,
					   XENVIF_TX_CB(skb)->pending_idx,
					   XEN_NETIF_RSP_OKAY);

		/* Invalidate preceding fragments of this skb. */
		for (j = 0; j < i; j++) {
			pending_idx = frag_get_pending_idx(&shinfo->frags[j]);
			xenvif_idx_unmap(queue, pending_idx);
			xenvif_idx_release(queue, pending_idx,
					   XEN_NETIF_RSP_OKAY);
		}

		/* And if we found the error while checking the frag_list, unmap
		 * the first skb's frags
		 */
		if (first_shinfo) {
			for (j = 0; j < first_shinfo->nr_frags; j++) {
				pending_idx = frag_get_pending_idx(&first_shinfo->frags[j]);
				xenvif_idx_unmap(queue, pending_idx);
				xenvif_idx_release(queue, pending_idx,
						   XEN_NETIF_RSP_OKAY);
			}
		}

		/* Remember the error: invalidate all subsequent fragments. */
		err = newerr;
	}

	if (skb_has_frag_list(skb) && !first_shinfo) {
		first_shinfo = skb_shinfo(skb);
		shinfo = skb_shinfo(skb_shinfo(skb)->frag_list);
		nr_frags = shinfo->nr_frags;

		goto check_frags;
	}

	*gopp_map = gop_map;
	return err;
}

static void xenvif_fill_frags(struct xenvif_queue *queue, struct sk_buff *skb)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	int nr_frags = shinfo->nr_frags;
	int i;
	u16 prev_pending_idx = INVALID_PENDING_IDX;

	for (i = 0; i < nr_frags; i++) {
		skb_frag_t *frag = shinfo->frags + i;
		struct xen_netif_tx_request *txp;
		struct page *page;
		u16 pending_idx;

		pending_idx = frag_get_pending_idx(frag);

		/* If this is not the first frag, chain it to the previous*/
		if (prev_pending_idx == INVALID_PENDING_IDX)
			skb_shinfo(skb)->destructor_arg =
				&callback_param(queue, pending_idx);
		else
			callback_param(queue, prev_pending_idx).ctx =
				&callback_param(queue, pending_idx);

		callback_param(queue, pending_idx).ctx = NULL;
		prev_pending_idx = pending_idx;

		txp = &queue->pending_tx_info[pending_idx].req;
		page = virt_to_page(idx_to_kaddr(queue, pending_idx));
		__skb_fill_page_desc(skb, i, page, txp->offset, txp->size);
		skb->len += txp->size;
		skb->data_len += txp->size;
		skb->truesize += txp->size;

		/* Take an extra reference to offset network stack's put_page */
		get_page(queue->mmap_pages[pending_idx]);
	}
}

static int xenvif_get_extras(struct xenvif_queue *queue,
			     struct xen_netif_extra_info *extras,
			     unsigned int *extra_count,
			     int work_to_do)
{
	struct xen_netif_extra_info extra;
	RING_IDX cons = queue->tx.req_cons;

	do {
		if (unlikely(work_to_do-- <= 0)) {
			netdev_err(queue->vif->dev, "Missing extra info\n");
			xenvif_fatal_tx_err(queue->vif);
			return -EBADR;
		}

		RING_COPY_REQUEST(&queue->tx, cons, &extra);

		queue->tx.req_cons = ++cons;
		(*extra_count)++;

		if (unlikely(!extra.type ||
			     extra.type >= XEN_NETIF_EXTRA_TYPE_MAX)) {
			netdev_err(queue->vif->dev,
				   "Invalid extra type: %d\n", extra.type);
			xenvif_fatal_tx_err(queue->vif);
			return -EINVAL;
		}

		memcpy(&extras[extra.type - 1], &extra, sizeof(extra));
	} while (extra.flags & XEN_NETIF_EXTRA_FLAG_MORE);

	return work_to_do;
}

static int xenvif_set_skb_gso(struct xenvif *vif,
			      struct sk_buff *skb,
			      struct xen_netif_extra_info *gso)
{
	if (!gso->u.gso.size) {
		netdev_err(vif->dev, "GSO size must not be zero.\n");
		xenvif_fatal_tx_err(vif);
		return -EINVAL;
	}

	switch (gso->u.gso.type) {
	case XEN_NETIF_GSO_TYPE_TCPV4:
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
		break;
	case XEN_NETIF_GSO_TYPE_TCPV6:
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
		break;
	default:
		netdev_err(vif->dev, "Bad GSO type %d.\n", gso->u.gso.type);
		xenvif_fatal_tx_err(vif);
		return -EINVAL;
	}

	skb_shinfo(skb)->gso_size = gso->u.gso.size;
	/* gso_segs will be calculated later */

	return 0;
}

static int checksum_setup(struct xenvif_queue *queue, struct sk_buff *skb)
{
	bool recalculate_partial_csum = false;

	/* A GSO SKB must be CHECKSUM_PARTIAL. However some buggy
	 * peers can fail to set NETRXF_csum_blank when sending a GSO
	 * frame. In this case force the SKB to CHECKSUM_PARTIAL and
	 * recalculate the partial checksum.
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL && skb_is_gso(skb)) {
		queue->stats.rx_gso_checksum_fixup++;
		skb->ip_summed = CHECKSUM_PARTIAL;
		recalculate_partial_csum = true;
	}

	/* A non-CHECKSUM_PARTIAL SKB does not require setup. */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	return skb_checksum_setup(skb, recalculate_partial_csum);
}

static bool tx_credit_exceeded(struct xenvif_queue *queue, unsigned size)
{
	u64 now = get_jiffies_64();
	u64 next_credit = queue->credit_window_start +
		msecs_to_jiffies(queue->credit_usec / 1000);

	/* Timer could already be pending in rare cases. */
	if (timer_pending(&queue->credit_timeout))
		return true;

	/* Passed the point where we can replenish credit? */
	if (time_after_eq64(now, next_credit)) {
		queue->credit_window_start = now;
		tx_add_credit(queue);
	}

	/* Still too big to send right now? Set a callback. */
	if (size > queue->remaining_credit) {
		queue->credit_timeout.data     =
			(unsigned long)queue;
		mod_timer(&queue->credit_timeout,
			  next_credit);
		queue->credit_window_start = next_credit;

		return true;
	}

	return false;
}

/* No locking is required in xenvif_mcast_add/del() as they are
 * only ever invoked from NAPI poll. An RCU list is used because
 * xenvif_mcast_match() is called asynchronously, during start_xmit.
 */

static int xenvif_mcast_add(struct xenvif *vif, const u8 *addr)
{
	struct xenvif_mcast_addr *mcast;

	if (vif->fe_mcast_count == XEN_NETBK_MCAST_MAX) {
		if (net_ratelimit())
			netdev_err(vif->dev,
				   "Too many multicast addresses\n");
		return -ENOSPC;
	}

	mcast = kzalloc(sizeof(*mcast), GFP_ATOMIC);
	if (!mcast)
		return -ENOMEM;

	ether_addr_copy(mcast->addr, addr);
	list_add_tail_rcu(&mcast->entry, &vif->fe_mcast_addr);
	vif->fe_mcast_count++;

	return 0;
}

static void xenvif_mcast_del(struct xenvif *vif, const u8 *addr)
{
	struct xenvif_mcast_addr *mcast;

	list_for_each_entry_rcu(mcast, &vif->fe_mcast_addr, entry) {
		if (ether_addr_equal(addr, mcast->addr)) {
			--vif->fe_mcast_count;
			list_del_rcu(&mcast->entry);
			kfree_rcu(mcast, rcu);
			break;
		}
	}
}

bool xenvif_mcast_match(struct xenvif *vif, const u8 *addr)
{
	struct xenvif_mcast_addr *mcast;

	rcu_read_lock();
	list_for_each_entry_rcu(mcast, &vif->fe_mcast_addr, entry) {
		if (ether_addr_equal(addr, mcast->addr)) {
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();

	return false;
}

void xenvif_mcast_addr_list_free(struct xenvif *vif)
{
	/* No need for locking or RCU here. NAPI poll and TX queue
	 * are stopped.
	 */
	while (!list_empty(&vif->fe_mcast_addr)) {
		struct xenvif_mcast_addr *mcast;

		mcast = list_first_entry(&vif->fe_mcast_addr,
					 struct xenvif_mcast_addr,
					 entry);
		--vif->fe_mcast_count;
		list_del(&mcast->entry);
		kfree(mcast);
	}
}

static void xenvif_tx_build_gops(struct xenvif_queue *queue,
				     int budget,
				     unsigned *copy_ops,
				     unsigned *map_ops)
{
	struct gnttab_map_grant_ref *gop = queue->tx_map_ops;
	struct sk_buff *skb, *nskb;
	int ret;
	unsigned int frag_overflow;

	while (skb_queue_len(&queue->tx_queue) < budget) {
		struct xen_netif_tx_request txreq;
		struct xen_netif_tx_request txfrags[XEN_NETBK_LEGACY_SLOTS_MAX];
		struct xen_netif_extra_info extras[XEN_NETIF_EXTRA_TYPE_MAX-1];
		unsigned int extra_count;
		u16 pending_idx;
		RING_IDX idx;
		int work_to_do;
		unsigned int data_len;
		pending_ring_idx_t index;

		if (queue->tx.sring->req_prod - queue->tx.req_cons >
		    XEN_NETIF_TX_RING_SIZE) {
			netdev_err(queue->vif->dev,
				   "Impossible number of requests. "
				   "req_prod %d, req_cons %d, size %ld\n",
				   queue->tx.sring->req_prod, queue->tx.req_cons,
				   XEN_NETIF_TX_RING_SIZE);
			xenvif_fatal_tx_err(queue->vif);
			break;
		}

		work_to_do = RING_HAS_UNCONSUMED_REQUESTS(&queue->tx);
		if (!work_to_do)
			break;

		idx = queue->tx.req_cons;
		rmb(); /* Ensure that we see the request before we copy it. */
		RING_COPY_REQUEST(&queue->tx, idx, &txreq);

		/* Credit-based scheduling. */
		if (txreq.size > queue->remaining_credit &&
		    tx_credit_exceeded(queue, txreq.size))
			break;

		queue->remaining_credit -= txreq.size;

		work_to_do--;
		queue->tx.req_cons = ++idx;

		memset(extras, 0, sizeof(extras));
		extra_count = 0;
		if (txreq.flags & XEN_NETTXF_extra_info) {
			work_to_do = xenvif_get_extras(queue, extras,
						       &extra_count,
						       work_to_do);
			idx = queue->tx.req_cons;
			if (unlikely(work_to_do < 0))
				break;
		}

		if (extras[XEN_NETIF_EXTRA_TYPE_MCAST_ADD - 1].type) {
			struct xen_netif_extra_info *extra;

			extra = &extras[XEN_NETIF_EXTRA_TYPE_MCAST_ADD - 1];
			ret = xenvif_mcast_add(queue->vif, extra->u.mcast.addr);

			make_tx_response(queue, &txreq, extra_count,
					 (ret == 0) ?
					 XEN_NETIF_RSP_OKAY :
					 XEN_NETIF_RSP_ERROR);
			push_tx_responses(queue);
			continue;
		}

		if (extras[XEN_NETIF_EXTRA_TYPE_MCAST_DEL - 1].type) {
			struct xen_netif_extra_info *extra;

			extra = &extras[XEN_NETIF_EXTRA_TYPE_MCAST_DEL - 1];
			xenvif_mcast_del(queue->vif, extra->u.mcast.addr);

			make_tx_response(queue, &txreq, extra_count,
					 XEN_NETIF_RSP_OKAY);
			push_tx_responses(queue);
			continue;
		}

		ret = xenvif_count_requests(queue, &txreq, extra_count,
					    txfrags, work_to_do);
		if (unlikely(ret < 0))
			break;

		idx += ret;

		if (unlikely(txreq.size < ETH_HLEN)) {
			netdev_dbg(queue->vif->dev,
				   "Bad packet size: %d\n", txreq.size);
			xenvif_tx_err(queue, &txreq, extra_count, idx);
			break;
		}

		/* No crossing a page as the payload mustn't fragment. */
		if (unlikely((txreq.offset + txreq.size) > XEN_PAGE_SIZE)) {
			netdev_err(queue->vif->dev,
				   "txreq.offset: %u, size: %u, end: %lu\n",
				   txreq.offset, txreq.size,
				   (unsigned long)(txreq.offset&~XEN_PAGE_MASK) + txreq.size);
			xenvif_fatal_tx_err(queue->vif);
			break;
		}

		index = pending_index(queue->pending_cons);
		pending_idx = queue->pending_ring[index];

		data_len = (txreq.size > XEN_NETBACK_TX_COPY_LEN &&
			    ret < XEN_NETBK_LEGACY_SLOTS_MAX) ?
			XEN_NETBACK_TX_COPY_LEN : txreq.size;

		skb = xenvif_alloc_skb(data_len);
		if (unlikely(skb == NULL)) {
			netdev_dbg(queue->vif->dev,
				   "Can't allocate a skb in start_xmit.\n");
			xenvif_tx_err(queue, &txreq, extra_count, idx);
			break;
		}

		skb_shinfo(skb)->nr_frags = ret;
		if (data_len < txreq.size)
			skb_shinfo(skb)->nr_frags++;
		/* At this point shinfo->nr_frags is in fact the number of
		 * slots, which can be as large as XEN_NETBK_LEGACY_SLOTS_MAX.
		 */
		frag_overflow = 0;
		nskb = NULL;
		if (skb_shinfo(skb)->nr_frags > MAX_SKB_FRAGS) {
			frag_overflow = skb_shinfo(skb)->nr_frags - MAX_SKB_FRAGS;
			BUG_ON(frag_overflow > MAX_SKB_FRAGS);
			skb_shinfo(skb)->nr_frags = MAX_SKB_FRAGS;
			nskb = xenvif_alloc_skb(0);
			if (unlikely(nskb == NULL)) {
				kfree_skb(skb);
				xenvif_tx_err(queue, &txreq, extra_count, idx);
				if (net_ratelimit())
					netdev_err(queue->vif->dev,
						   "Can't allocate the frag_list skb.\n");
				break;
			}
		}

		if (extras[XEN_NETIF_EXTRA_TYPE_GSO - 1].type) {
			struct xen_netif_extra_info *gso;
			gso = &extras[XEN_NETIF_EXTRA_TYPE_GSO - 1];

			if (xenvif_set_skb_gso(queue->vif, skb, gso)) {
				/* Failure in xenvif_set_skb_gso is fatal. */
				kfree_skb(skb);
				kfree_skb(nskb);
				break;
			}
		}

		XENVIF_TX_CB(skb)->pending_idx = pending_idx;

		__skb_put(skb, data_len);
		queue->tx_copy_ops[*copy_ops].source.u.ref = txreq.gref;
		queue->tx_copy_ops[*copy_ops].source.domid = queue->vif->domid;
		queue->tx_copy_ops[*copy_ops].source.offset = txreq.offset;

		queue->tx_copy_ops[*copy_ops].dest.u.gmfn =
			virt_to_gfn(skb->data);
		queue->tx_copy_ops[*copy_ops].dest.domid = DOMID_SELF;
		queue->tx_copy_ops[*copy_ops].dest.offset =
			offset_in_page(skb->data) & ~XEN_PAGE_MASK;

		queue->tx_copy_ops[*copy_ops].len = data_len;
		queue->tx_copy_ops[*copy_ops].flags = GNTCOPY_source_gref;

		(*copy_ops)++;

		if (data_len < txreq.size) {
			frag_set_pending_idx(&skb_shinfo(skb)->frags[0],
					     pending_idx);
			xenvif_tx_create_map_op(queue, pending_idx, &txreq,
						extra_count, gop);
			gop++;
		} else {
			frag_set_pending_idx(&skb_shinfo(skb)->frags[0],
					     INVALID_PENDING_IDX);
			memcpy(&queue->pending_tx_info[pending_idx].req,
			       &txreq, sizeof(txreq));
			queue->pending_tx_info[pending_idx].extra_count =
				extra_count;
		}

		queue->pending_cons++;

		gop = xenvif_get_requests(queue, skb, txfrags, gop,
				          frag_overflow, nskb);

		__skb_queue_tail(&queue->tx_queue, skb);

		queue->tx.req_cons = idx;

		if (((gop-queue->tx_map_ops) >= ARRAY_SIZE(queue->tx_map_ops)) ||
		    (*copy_ops >= ARRAY_SIZE(queue->tx_copy_ops)))
			break;
	}

	(*map_ops) = gop - queue->tx_map_ops;
	return;
}

/* Consolidate skb with a frag_list into a brand new one with local pages on
 * frags. Returns 0 or -ENOMEM if can't allocate new pages.
 */
static int xenvif_handle_frag_list(struct xenvif_queue *queue, struct sk_buff *skb)
{
	unsigned int offset = skb_headlen(skb);
	skb_frag_t frags[MAX_SKB_FRAGS];
	int i, f;
	struct ubuf_info *uarg;
	struct sk_buff *nskb = skb_shinfo(skb)->frag_list;

	queue->stats.tx_zerocopy_sent += 2;
	queue->stats.tx_frag_overflow++;

	xenvif_fill_frags(queue, nskb);
	/* Subtract frags size, we will correct it later */
	skb->truesize -= skb->data_len;
	skb->len += nskb->len;
	skb->data_len += nskb->len;

	/* create a brand new frags array and coalesce there */
	for (i = 0; offset < skb->len; i++) {
		struct page *page;
		unsigned int len;

		BUG_ON(i >= MAX_SKB_FRAGS);
		page = alloc_page(GFP_ATOMIC);
		if (!page) {
			int j;
			skb->truesize += skb->data_len;
			for (j = 0; j < i; j++)
				put_page(frags[j].page.p);
			return -ENOMEM;
		}

		if (offset + PAGE_SIZE < skb->len)
			len = PAGE_SIZE;
		else
			len = skb->len - offset;
		if (skb_copy_bits(skb, offset, page_address(page), len))
			BUG();

		offset += len;
		frags[i].page.p = page;
		frags[i].page_offset = 0;
		skb_frag_size_set(&frags[i], len);
	}

	/* Copied all the bits from the frag list -- free it. */
	skb_frag_list_init(skb);
	xenvif_skb_zerocopy_prepare(queue, nskb);
	kfree_skb(nskb);

	/* Release all the original (foreign) frags. */
	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
		skb_frag_unref(skb, f);
	uarg = skb_shinfo(skb)->destructor_arg;
	/* increase inflight counter to offset decrement in callback */
	atomic_inc(&queue->inflight_packets);
	uarg->callback(uarg, true);
	skb_shinfo(skb)->destructor_arg = NULL;

	/* Fill the skb with the new (local) frags. */
	memcpy(skb_shinfo(skb)->frags, frags, i * sizeof(skb_frag_t));
	skb_shinfo(skb)->nr_frags = i;
	skb->truesize += i * PAGE_SIZE;

	return 0;
}

static int xenvif_tx_submit(struct xenvif_queue *queue)
{
	struct gnttab_map_grant_ref *gop_map = queue->tx_map_ops;
	struct gnttab_copy *gop_copy = queue->tx_copy_ops;
	struct sk_buff *skb;
	int work_done = 0;

	while ((skb = __skb_dequeue(&queue->tx_queue)) != NULL) {
		struct xen_netif_tx_request *txp;
		u16 pending_idx;
		unsigned data_len;

		pending_idx = XENVIF_TX_CB(skb)->pending_idx;
		txp = &queue->pending_tx_info[pending_idx].req;

		/* Check the remap error code. */
		if (unlikely(xenvif_tx_check_gop(queue, skb, &gop_map, &gop_copy))) {
			/* If there was an error, xenvif_tx_check_gop is
			 * expected to release all the frags which were mapped,
			 * so kfree_skb shouldn't do it again
			 */
			skb_shinfo(skb)->nr_frags = 0;
			if (skb_has_frag_list(skb)) {
				struct sk_buff *nskb =
						skb_shinfo(skb)->frag_list;
				skb_shinfo(nskb)->nr_frags = 0;
			}
			kfree_skb(skb);
			continue;
		}

		data_len = skb->len;
		callback_param(queue, pending_idx).ctx = NULL;
		if (data_len < txp->size) {
			/* Append the packet payload as a fragment. */
			txp->offset += data_len;
			txp->size -= data_len;
		} else {
			/* Schedule a response immediately. */
			xenvif_idx_release(queue, pending_idx,
					   XEN_NETIF_RSP_OKAY);
		}

		if (txp->flags & XEN_NETTXF_csum_blank)
			skb->ip_summed = CHECKSUM_PARTIAL;
		else if (txp->flags & XEN_NETTXF_data_validated)
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		xenvif_fill_frags(queue, skb);

		if (unlikely(skb_has_frag_list(skb))) {
			if (xenvif_handle_frag_list(queue, skb)) {
				if (net_ratelimit())
					netdev_err(queue->vif->dev,
						   "Not enough memory to consolidate frag_list!\n");
				xenvif_skb_zerocopy_prepare(queue, skb);
				kfree_skb(skb);
				continue;
			}
		}

		skb->dev      = queue->vif->dev;
		skb->protocol = eth_type_trans(skb, skb->dev);
		skb_reset_network_header(skb);

		if (checksum_setup(queue, skb)) {
			netdev_dbg(queue->vif->dev,
				   "Can't setup checksum in net_tx_action\n");
			/* We have to set this flag to trigger the callback */
			if (skb_shinfo(skb)->destructor_arg)
				xenvif_skb_zerocopy_prepare(queue, skb);
			kfree_skb(skb);
			continue;
		}

		skb_probe_transport_header(skb, 0);

		/* If the packet is GSO then we will have just set up the
		 * transport header offset in checksum_setup so it's now
		 * straightforward to calculate gso_segs.
		 */
		if (skb_is_gso(skb)) {
			int mss = skb_shinfo(skb)->gso_size;
			int hdrlen = skb_transport_header(skb) -
				skb_mac_header(skb) +
				tcp_hdrlen(skb);

			skb_shinfo(skb)->gso_segs =
				DIV_ROUND_UP(skb->len - hdrlen, mss);
		}

		queue->stats.rx_bytes += skb->len;
		queue->stats.rx_packets++;

		work_done++;

		/* Set this flag right before netif_receive_skb, otherwise
		 * someone might think this packet already left netback, and
		 * do a skb_copy_ubufs while we are still in control of the
		 * skb. E.g. the __pskb_pull_tail earlier can do such thing.
		 */
		if (skb_shinfo(skb)->destructor_arg) {
			xenvif_skb_zerocopy_prepare(queue, skb);
			queue->stats.tx_zerocopy_sent++;
		}

		netif_receive_skb(skb);
	}

	return work_done;
}

void xenvif_zerocopy_callback(struct ubuf_info *ubuf, bool zerocopy_success)
{
	unsigned long flags;
	pending_ring_idx_t index;
	struct xenvif_queue *queue = ubuf_to_queue(ubuf);

	/* This is the only place where we grab this lock, to protect callbacks
	 * from each other.
	 */
	spin_lock_irqsave(&queue->callback_lock, flags);
	do {
		u16 pending_idx = ubuf->desc;
		ubuf = (struct ubuf_info *) ubuf->ctx;
		BUG_ON(queue->dealloc_prod - queue->dealloc_cons >=
			MAX_PENDING_REQS);
		index = pending_index(queue->dealloc_prod);
		queue->dealloc_ring[index] = pending_idx;
		/* Sync with xenvif_tx_dealloc_action:
		 * insert idx then incr producer.
		 */
		smp_wmb();
		queue->dealloc_prod++;
	} while (ubuf);
	spin_unlock_irqrestore(&queue->callback_lock, flags);

	if (likely(zerocopy_success))
		queue->stats.tx_zerocopy_success++;
	else
		queue->stats.tx_zerocopy_fail++;
	xenvif_skb_zerocopy_complete(queue);
}

static inline void xenvif_tx_dealloc_action(struct xenvif_queue *queue)
{
	struct gnttab_unmap_grant_ref *gop;
	pending_ring_idx_t dc, dp;
	u16 pending_idx, pending_idx_release[MAX_PENDING_REQS];
	unsigned int i = 0;

	dc = queue->dealloc_cons;
	gop = queue->tx_unmap_ops;

	/* Free up any grants we have finished using */
	do {
		dp = queue->dealloc_prod;

		/* Ensure we see all indices enqueued by all
		 * xenvif_zerocopy_callback().
		 */
		smp_rmb();

		while (dc != dp) {
			BUG_ON(gop - queue->tx_unmap_ops >= MAX_PENDING_REQS);
			pending_idx =
				queue->dealloc_ring[pending_index(dc++)];

			pending_idx_release[gop - queue->tx_unmap_ops] =
				pending_idx;
			queue->pages_to_unmap[gop - queue->tx_unmap_ops] =
				queue->mmap_pages[pending_idx];
			gnttab_set_unmap_op(gop,
					    idx_to_kaddr(queue, pending_idx),
					    GNTMAP_host_map,
					    queue->grant_tx_handle[pending_idx]);
			xenvif_grant_handle_reset(queue, pending_idx);
			++gop;
		}

	} while (dp != queue->dealloc_prod);

	queue->dealloc_cons = dc;

	if (gop - queue->tx_unmap_ops > 0) {
		int ret;
		ret = gnttab_unmap_refs(queue->tx_unmap_ops,
					NULL,
					queue->pages_to_unmap,
					gop - queue->tx_unmap_ops);
		if (ret) {
			netdev_err(queue->vif->dev, "Unmap fail: nr_ops %tu ret %d\n",
				   gop - queue->tx_unmap_ops, ret);
			for (i = 0; i < gop - queue->tx_unmap_ops; ++i) {
				if (gop[i].status != GNTST_okay)
					netdev_err(queue->vif->dev,
						   " host_addr: 0x%llx handle: 0x%x status: %d\n",
						   gop[i].host_addr,
						   gop[i].handle,
						   gop[i].status);
			}
			BUG();
		}
	}

	for (i = 0; i < gop - queue->tx_unmap_ops; ++i)
		xenvif_idx_release(queue, pending_idx_release[i],
				   XEN_NETIF_RSP_OKAY);
}


/* Called after netfront has transmitted */
int xenvif_tx_action(struct xenvif_queue *queue, int budget)
{
	unsigned nr_mops, nr_cops = 0;
	int work_done, ret;

	if (unlikely(!tx_work_todo(queue)))
		return 0;

	xenvif_tx_build_gops(queue, budget, &nr_cops, &nr_mops);

	if (nr_cops == 0)
		return 0;

	gnttab_batch_copy(queue->tx_copy_ops, nr_cops);
	if (nr_mops != 0) {
		ret = gnttab_map_refs(queue->tx_map_ops,
				      NULL,
				      queue->pages_to_map,
				      nr_mops);
		BUG_ON(ret);
	}

	work_done = xenvif_tx_submit(queue);

	return work_done;
}

static void xenvif_idx_release(struct xenvif_queue *queue, u16 pending_idx,
			       u8 status)
{
	struct pending_tx_info *pending_tx_info;
	pending_ring_idx_t index;
	unsigned long flags;

	pending_tx_info = &queue->pending_tx_info[pending_idx];

	spin_lock_irqsave(&queue->response_lock, flags);

	make_tx_response(queue, &pending_tx_info->req,
			 pending_tx_info->extra_count, status);

	/* Release the pending index before pusing the Tx response so
	 * its available before a new Tx request is pushed by the
	 * frontend.
	 */
	index = pending_index(queue->pending_prod++);
	queue->pending_ring[index] = pending_idx;

	push_tx_responses(queue);

	spin_unlock_irqrestore(&queue->response_lock, flags);
}


static void make_tx_response(struct xenvif_queue *queue,
			     struct xen_netif_tx_request *txp,
			     unsigned int extra_count,
			     s8       st)
{
	RING_IDX i = queue->tx.rsp_prod_pvt;
	struct xen_netif_tx_response *resp;

	resp = RING_GET_RESPONSE(&queue->tx, i);
	resp->id     = txp->id;
	resp->status = st;

	while (extra_count-- != 0)
		RING_GET_RESPONSE(&queue->tx, ++i)->status = XEN_NETIF_RSP_NULL;

	queue->tx.rsp_prod_pvt = ++i;
}

static void push_tx_responses(struct xenvif_queue *queue)
{
	int notify;

	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&queue->tx, notify);
	if (notify)
		notify_remote_via_irq(queue->tx_irq);
}

static struct xen_netif_rx_response *make_rx_response(struct xenvif_queue *queue,
					     u16      id,
					     s8       st,
					     u16      offset,
					     u16      size,
					     u16      flags)
{
	RING_IDX i = queue->rx.rsp_prod_pvt;
	struct xen_netif_rx_response *resp;

	resp = RING_GET_RESPONSE(&queue->rx, i);
	resp->offset     = offset;
	resp->flags      = flags;
	resp->id         = id;
	resp->status     = (s16)size;
	if (st < 0)
		resp->status = (s16)st;

	queue->rx.rsp_prod_pvt = ++i;

	return resp;
}

void xenvif_idx_unmap(struct xenvif_queue *queue, u16 pending_idx)
{
	int ret;
	struct gnttab_unmap_grant_ref tx_unmap_op;

	gnttab_set_unmap_op(&tx_unmap_op,
			    idx_to_kaddr(queue, pending_idx),
			    GNTMAP_host_map,
			    queue->grant_tx_handle[pending_idx]);
	xenvif_grant_handle_reset(queue, pending_idx);

	ret = gnttab_unmap_refs(&tx_unmap_op, NULL,
				&queue->mmap_pages[pending_idx], 1);
	if (ret) {
		netdev_err(queue->vif->dev,
			   "Unmap fail: ret: %d pending_idx: %d host_addr: %llx handle: 0x%x status: %d\n",
			   ret,
			   pending_idx,
			   tx_unmap_op.host_addr,
			   tx_unmap_op.handle,
			   tx_unmap_op.status);
		BUG();
	}
}

static inline int tx_work_todo(struct xenvif_queue *queue)
{
	if (likely(RING_HAS_UNCONSUMED_REQUESTS(&queue->tx)))
		return 1;

	return 0;
}

static inline bool tx_dealloc_work_todo(struct xenvif_queue *queue)
{
	return queue->dealloc_cons != queue->dealloc_prod;
}

void xenvif_unmap_frontend_data_rings(struct xenvif_queue *queue)
{
	if (queue->tx.sring)
		xenbus_unmap_ring_vfree(xenvif_to_xenbus_device(queue->vif),
					queue->tx.sring);
	if (queue->rx.sring)
		xenbus_unmap_ring_vfree(xenvif_to_xenbus_device(queue->vif),
					queue->rx.sring);
}

int xenvif_map_frontend_data_rings(struct xenvif_queue *queue,
				   grant_ref_t tx_ring_ref,
				   grant_ref_t rx_ring_ref)
{
	void *addr;
	struct xen_netif_tx_sring *txs;
	struct xen_netif_rx_sring *rxs;

	int err = -ENOMEM;

	err = xenbus_map_ring_valloc(xenvif_to_xenbus_device(queue->vif),
				     &tx_ring_ref, 1, &addr);
	if (err)
		goto err;

	txs = (struct xen_netif_tx_sring *)addr;
	BACK_RING_INIT(&queue->tx, txs, XEN_PAGE_SIZE);

	err = xenbus_map_ring_valloc(xenvif_to_xenbus_device(queue->vif),
				     &rx_ring_ref, 1, &addr);
	if (err)
		goto err;

	rxs = (struct xen_netif_rx_sring *)addr;
	BACK_RING_INIT(&queue->rx, rxs, XEN_PAGE_SIZE);

	return 0;

err:
	xenvif_unmap_frontend_data_rings(queue);
	return err;
}

static void xenvif_queue_carrier_off(struct xenvif_queue *queue)
{
	struct xenvif *vif = queue->vif;

	queue->stalled = true;

	/* At least one queue has stalled? Disable the carrier. */
	spin_lock(&vif->lock);
	if (vif->stalled_queues++ == 0) {
		netdev_info(vif->dev, "Guest Rx stalled");
		netif_carrier_off(vif->dev);
	}
	spin_unlock(&vif->lock);
}

static void xenvif_queue_carrier_on(struct xenvif_queue *queue)
{
	struct xenvif *vif = queue->vif;

	queue->last_rx_time = jiffies; /* Reset Rx stall detection. */
	queue->stalled = false;

	/* All queues are ready? Enable the carrier. */
	spin_lock(&vif->lock);
	if (--vif->stalled_queues == 0) {
		netdev_info(vif->dev, "Guest Rx ready");
		netif_carrier_on(vif->dev);
	}
	spin_unlock(&vif->lock);
}

static bool xenvif_rx_queue_stalled(struct xenvif_queue *queue)
{
	RING_IDX prod, cons;

	prod = queue->rx.sring->req_prod;
	cons = queue->rx.req_cons;

	return !queue->stalled && prod - cons < 1
		&& time_after(jiffies,
			      queue->last_rx_time + queue->vif->stall_timeout);
}

static bool xenvif_rx_queue_ready(struct xenvif_queue *queue)
{
	RING_IDX prod, cons;

	prod = queue->rx.sring->req_prod;
	cons = queue->rx.req_cons;

	return queue->stalled && prod - cons >= 1;
}

static bool xenvif_have_rx_work(struct xenvif_queue *queue)
{
	return xenvif_rx_ring_slots_available(queue)
		|| (queue->vif->stall_timeout &&
		    (xenvif_rx_queue_stalled(queue)
		     || xenvif_rx_queue_ready(queue)))
		|| kthread_should_stop()
		|| queue->vif->disabled;
}

static long xenvif_rx_queue_timeout(struct xenvif_queue *queue)
{
	struct sk_buff *skb;
	long timeout;

	skb = skb_peek(&queue->rx_queue);
	if (!skb)
		return MAX_SCHEDULE_TIMEOUT;

	timeout = XENVIF_RX_CB(skb)->expires - jiffies;
	return timeout < 0 ? 0 : timeout;
}

/* Wait until the guest Rx thread has work.
 *
 * The timeout needs to be adjusted based on the current head of the
 * queue (and not just the head at the beginning).  In particular, if
 * the queue is initially empty an infinite timeout is used and this
 * needs to be reduced when a skb is queued.
 *
 * This cannot be done with wait_event_timeout() because it only
 * calculates the timeout once.
 */
static void xenvif_wait_for_rx_work(struct xenvif_queue *queue)
{
	DEFINE_WAIT(wait);

	if (xenvif_have_rx_work(queue))
		return;

	for (;;) {
		long ret;

		prepare_to_wait(&queue->wq, &wait, TASK_INTERRUPTIBLE);
		if (xenvif_have_rx_work(queue))
			break;
		ret = schedule_timeout(xenvif_rx_queue_timeout(queue));
		if (!ret)
			break;
	}
	finish_wait(&queue->wq, &wait);
}

int xenvif_kthread_guest_rx(void *data)
{
	struct xenvif_queue *queue = data;
	struct xenvif *vif = queue->vif;

	if (!vif->stall_timeout)
		xenvif_queue_carrier_on(queue);

	for (;;) {
		xenvif_wait_for_rx_work(queue);

		if (kthread_should_stop())
			break;

		/* This frontend is found to be rogue, disable it in
		 * kthread context. Currently this is only set when
		 * netback finds out frontend sends malformed packet,
		 * but we cannot disable the interface in softirq
		 * context so we defer it here, if this thread is
		 * associated with queue 0.
		 */
		if (unlikely(vif->disabled && queue->id == 0)) {
			xenvif_carrier_off(vif);
			break;
		}

		if (!skb_queue_empty(&queue->rx_queue))
			xenvif_rx_action(queue);

		/* If the guest hasn't provided any Rx slots for a
		 * while it's probably not responsive, drop the
		 * carrier so packets are dropped earlier.
		 */
		if (vif->stall_timeout) {
			if (xenvif_rx_queue_stalled(queue))
				xenvif_queue_carrier_off(queue);
			else if (xenvif_rx_queue_ready(queue))
				xenvif_queue_carrier_on(queue);
		}

		/* Queued packets may have foreign pages from other
		 * domains.  These cannot be queued indefinitely as
		 * this would starve guests of grant refs and transmit
		 * slots.
		 */
		xenvif_rx_queue_drop_expired(queue);

		xenvif_rx_queue_maybe_wake(queue);

		cond_resched();
	}

	/* Bin any remaining skbs */
	xenvif_rx_queue_purge(queue);

	return 0;
}

static bool xenvif_dealloc_kthread_should_stop(struct xenvif_queue *queue)
{
	/* Dealloc thread must remain running until all inflight
	 * packets complete.
	 */
	return kthread_should_stop() &&
		!atomic_read(&queue->inflight_packets);
}

int xenvif_dealloc_kthread(void *data)
{
	struct xenvif_queue *queue = data;

	for (;;) {
		wait_event_interruptible(queue->dealloc_wq,
					 tx_dealloc_work_todo(queue) ||
					 xenvif_dealloc_kthread_should_stop(queue));
		if (xenvif_dealloc_kthread_should_stop(queue))
			break;

		xenvif_tx_dealloc_action(queue);
		cond_resched();
	}

	/* Unmap anything remaining*/
	if (tx_dealloc_work_todo(queue))
		xenvif_tx_dealloc_action(queue);

	return 0;
}

static void make_ctrl_response(struct xenvif *vif,
			       const struct xen_netif_ctrl_request *req,
			       u32 status, u32 data)
{
	RING_IDX idx = vif->ctrl.rsp_prod_pvt;
	struct xen_netif_ctrl_response rsp = {
		.id = req->id,
		.type = req->type,
		.status = status,
		.data = data,
	};

	*RING_GET_RESPONSE(&vif->ctrl, idx) = rsp;
	vif->ctrl.rsp_prod_pvt = ++idx;
}

static void push_ctrl_response(struct xenvif *vif)
{
	int notify;

	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&vif->ctrl, notify);
	if (notify)
		notify_remote_via_irq(vif->ctrl_irq);
}

static void process_ctrl_request(struct xenvif *vif,
				 const struct xen_netif_ctrl_request *req)
{
	u32 status = XEN_NETIF_CTRL_STATUS_NOT_SUPPORTED;
	u32 data = 0;

	switch (req->type) {
	case XEN_NETIF_CTRL_TYPE_SET_HASH_ALGORITHM:
		status = xenvif_set_hash_alg(vif, req->data[0]);
		break;

	case XEN_NETIF_CTRL_TYPE_GET_HASH_FLAGS:
		status = xenvif_get_hash_flags(vif, &data);
		break;

	case XEN_NETIF_CTRL_TYPE_SET_HASH_FLAGS:
		status = xenvif_set_hash_flags(vif, req->data[0]);
		break;

	case XEN_NETIF_CTRL_TYPE_SET_HASH_KEY:
		status = xenvif_set_hash_key(vif, req->data[0],
					     req->data[1]);
		break;

	case XEN_NETIF_CTRL_TYPE_GET_HASH_MAPPING_SIZE:
		status = XEN_NETIF_CTRL_STATUS_SUCCESS;
		data = XEN_NETBK_MAX_HASH_MAPPING_SIZE;
		break;

	case XEN_NETIF_CTRL_TYPE_SET_HASH_MAPPING_SIZE:
		status = xenvif_set_hash_mapping_size(vif,
						      req->data[0]);
		break;

	case XEN_NETIF_CTRL_TYPE_SET_HASH_MAPPING:
		status = xenvif_set_hash_mapping(vif, req->data[0],
						 req->data[1],
						 req->data[2]);
		break;

	default:
		break;
	}

	make_ctrl_response(vif, req, status, data);
	push_ctrl_response(vif);
}

static void xenvif_ctrl_action(struct xenvif *vif)
{
	for (;;) {
		RING_IDX req_prod, req_cons;

		req_prod = vif->ctrl.sring->req_prod;
		req_cons = vif->ctrl.req_cons;

		/* Make sure we can see requests before we process them. */
		rmb();

		if (req_cons == req_prod)
			break;

		while (req_cons != req_prod) {
			struct xen_netif_ctrl_request req;

			RING_COPY_REQUEST(&vif->ctrl, req_cons, &req);
			req_cons++;

			process_ctrl_request(vif, &req);
		}

		vif->ctrl.req_cons = req_cons;
		vif->ctrl.sring->req_event = req_cons + 1;
	}
}

static bool xenvif_ctrl_work_todo(struct xenvif *vif)
{
	if (likely(RING_HAS_UNCONSUMED_REQUESTS(&vif->ctrl)))
		return 1;

	return 0;
}

int xenvif_ctrl_kthread(void *data)
{
	struct xenvif *vif = data;

	for (;;) {
		wait_event_interruptible(vif->ctrl_wq,
					 xenvif_ctrl_work_todo(vif) ||
					 kthread_should_stop());
		if (kthread_should_stop())
			break;

		while (xenvif_ctrl_work_todo(vif))
			xenvif_ctrl_action(vif);

		cond_resched();
	}

	return 0;
}

static int __init netback_init(void)
{
	int rc = 0;

	if (!xen_domain())
		return -ENODEV;

	/* Allow as many queues as there are CPUs if user has not
	 * specified a value.
	 */
	if (xenvif_max_queues == 0)
		xenvif_max_queues = num_online_cpus();

	if (fatal_skb_slots < XEN_NETBK_LEGACY_SLOTS_MAX) {
		pr_info("fatal_skb_slots too small (%d), bump it to XEN_NETBK_LEGACY_SLOTS_MAX (%d)\n",
			fatal_skb_slots, XEN_NETBK_LEGACY_SLOTS_MAX);
		fatal_skb_slots = XEN_NETBK_LEGACY_SLOTS_MAX;
	}

	rc = xenvif_xenbus_init();
	if (rc)
		goto failed_init;

#ifdef CONFIG_DEBUG_FS
	xen_netback_dbg_root = debugfs_create_dir("xen-netback", NULL);
	if (IS_ERR_OR_NULL(xen_netback_dbg_root))
		pr_warn("Init of debugfs returned %ld!\n",
			PTR_ERR(xen_netback_dbg_root));
#endif /* CONFIG_DEBUG_FS */

	return 0;

failed_init:
	return rc;
}

module_init(netback_init);

static void __exit netback_fini(void)
{
#ifdef CONFIG_DEBUG_FS
	if (!IS_ERR_OR_NULL(xen_netback_dbg_root))
		debugfs_remove_recursive(xen_netback_dbg_root);
#endif /* CONFIG_DEBUG_FS */
	xenvif_xenbus_fini();
}
module_exit(netback_fini);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("xen-backend:vif");
