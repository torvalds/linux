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

#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>

/* Provide an option to disable split event channels at load time as
 * event channels are limited resource. Split event channels are
 * enabled by default.
 */
bool separate_tx_rx_irq = 1;
module_param(separate_tx_rx_irq, bool, 0644);

/* When guest ring is filled up, qdisc queues the packets for us, but we have
 * to timeout them, otherwise other guests' packets can get stuck there
 */
unsigned int rx_drain_timeout_msecs = 10000;
module_param(rx_drain_timeout_msecs, uint, 0444);
unsigned int rx_drain_timeout_jiffies;

/*
 * This is the maximum slots a skb can have. If a guest sends a skb
 * which exceeds this limit it is considered malicious.
 */
#define FATAL_SKB_SLOTS_DEFAULT 20
static unsigned int fatal_skb_slots = FATAL_SKB_SLOTS_DEFAULT;
module_param(fatal_skb_slots, uint, 0444);

static void xenvif_idx_release(struct xenvif *vif, u16 pending_idx,
			       u8 status);

static void make_tx_response(struct xenvif *vif,
			     struct xen_netif_tx_request *txp,
			     s8       st);

static inline int tx_work_todo(struct xenvif *vif);
static inline int rx_work_todo(struct xenvif *vif);

static struct xen_netif_rx_response *make_rx_response(struct xenvif *vif,
					     u16      id,
					     s8       st,
					     u16      offset,
					     u16      size,
					     u16      flags);

static inline unsigned long idx_to_pfn(struct xenvif *vif,
				       u16 idx)
{
	return page_to_pfn(vif->mmap_pages[idx]);
}

static inline unsigned long idx_to_kaddr(struct xenvif *vif,
					 u16 idx)
{
	return (unsigned long)pfn_to_kaddr(idx_to_pfn(vif, idx));
}

#define callback_param(vif, pending_idx) \
	(vif->pending_tx_info[pending_idx].callback_struct)

/* Find the containing VIF's structure from a pointer in pending_tx_info array
 */
static inline struct xenvif *ubuf_to_vif(const struct ubuf_info *ubuf)
{
	u16 pending_idx = ubuf->desc;
	struct pending_tx_info *temp =
		container_of(ubuf, struct pending_tx_info, callback_struct);
	return container_of(temp - pending_idx,
			    struct xenvif,
			    pending_tx_info[0]);
}

/* This is a miniumum size for the linear area to avoid lots of
 * calls to __pskb_pull_tail() as we set up checksum offsets. The
 * value 128 was chosen as it covers all IPv4 and most likely
 * IPv6 headers.
 */
#define PKT_PROT_LEN 128

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

bool xenvif_rx_ring_slots_available(struct xenvif *vif, int needed)
{
	RING_IDX prod, cons;

	do {
		prod = vif->rx.sring->req_prod;
		cons = vif->rx.req_cons;

		if (prod - cons >= needed)
			return true;

		vif->rx.sring->req_event = prod + 1;

		/* Make sure event is visible before we check prod
		 * again.
		 */
		mb();
	} while (vif->rx.sring->req_prod != prod);

	return false;
}

/*
 * Returns true if we should start a new receive buffer instead of
 * adding 'size' bytes to a buffer which currently contains 'offset'
 * bytes.
 */
static bool start_new_rx_buffer(int offset, unsigned long size, int head)
{
	/* simple case: we have completely filled the current buffer. */
	if (offset == MAX_BUFFER_OFFSET)
		return true;

	/*
	 * complex case: start a fresh buffer if the current frag
	 * would overflow the current buffer but only if:
	 *     (i)   this frag would fit completely in the next buffer
	 * and (ii)  there is already some data in the current buffer
	 * and (iii) this is not the head buffer.
	 *
	 * Where:
	 * - (i) stops us splitting a frag into two copies
	 *   unless the frag is too large for a single buffer.
	 * - (ii) stops us from leaving a buffer pointlessly empty.
	 * - (iii) stops us leaving the first buffer
	 *   empty. Strictly speaking this is already covered
	 *   by (ii) but is explicitly checked because
	 *   netfront relies on the first buffer being
	 *   non-empty and can crash otherwise.
	 *
	 * This means we will effectively linearise small
	 * frags but do not needlessly split large buffers
	 * into multiple copies tend to give large frags their
	 * own buffers as before.
	 */
	BUG_ON(size > MAX_BUFFER_OFFSET);
	if ((offset + size > MAX_BUFFER_OFFSET) && offset && !head)
		return true;

	return false;
}

struct netrx_pending_operations {
	unsigned copy_prod, copy_cons;
	unsigned meta_prod, meta_cons;
	struct gnttab_copy *copy;
	struct xenvif_rx_meta *meta;
	int copy_off;
	grant_ref_t copy_gref;
};

static struct xenvif_rx_meta *get_next_rx_buffer(struct xenvif *vif,
						 struct netrx_pending_operations *npo)
{
	struct xenvif_rx_meta *meta;
	struct xen_netif_rx_request *req;

	req = RING_GET_REQUEST(&vif->rx, vif->rx.req_cons++);

	meta = npo->meta + npo->meta_prod++;
	meta->gso_type = XEN_NETIF_GSO_TYPE_NONE;
	meta->gso_size = 0;
	meta->size = 0;
	meta->id = req->id;

	npo->copy_off = 0;
	npo->copy_gref = req->gref;

	return meta;
}

/*
 * Set up the grant operations for this fragment. If it's a flipping
 * interface, we also set up the unmap request from here.
 */
static void xenvif_gop_frag_copy(struct xenvif *vif, struct sk_buff *skb,
				 struct netrx_pending_operations *npo,
				 struct page *page, unsigned long size,
				 unsigned long offset, int *head,
				 struct xenvif *foreign_vif,
				 grant_ref_t foreign_gref)
{
	struct gnttab_copy *copy_gop;
	struct xenvif_rx_meta *meta;
	unsigned long bytes;
	int gso_type = XEN_NETIF_GSO_TYPE_NONE;

	/* Data must not cross a page boundary. */
	BUG_ON(size + offset > PAGE_SIZE<<compound_order(page));

	meta = npo->meta + npo->meta_prod - 1;

	/* Skip unused frames from start of page */
	page += offset >> PAGE_SHIFT;
	offset &= ~PAGE_MASK;

	while (size > 0) {
		BUG_ON(offset >= PAGE_SIZE);
		BUG_ON(npo->copy_off > MAX_BUFFER_OFFSET);

		bytes = PAGE_SIZE - offset;

		if (bytes > size)
			bytes = size;

		if (start_new_rx_buffer(npo->copy_off, bytes, *head)) {
			/*
			 * Netfront requires there to be some data in the head
			 * buffer.
			 */
			BUG_ON(*head);

			meta = get_next_rx_buffer(vif, npo);
		}

		if (npo->copy_off + bytes > MAX_BUFFER_OFFSET)
			bytes = MAX_BUFFER_OFFSET - npo->copy_off;

		copy_gop = npo->copy + npo->copy_prod++;
		copy_gop->flags = GNTCOPY_dest_gref;
		copy_gop->len = bytes;

		if (foreign_vif) {
			copy_gop->source.domid = foreign_vif->domid;
			copy_gop->source.u.ref = foreign_gref;
			copy_gop->flags |= GNTCOPY_source_gref;
		} else {
			copy_gop->source.domid = DOMID_SELF;
			copy_gop->source.u.gmfn =
				virt_to_mfn(page_address(page));
		}
		copy_gop->source.offset = offset;

		copy_gop->dest.domid = vif->domid;
		copy_gop->dest.offset = npo->copy_off;
		copy_gop->dest.u.ref = npo->copy_gref;

		npo->copy_off += bytes;
		meta->size += bytes;

		offset += bytes;
		size -= bytes;

		/* Next frame */
		if (offset == PAGE_SIZE && size) {
			BUG_ON(!PageCompound(page));
			page++;
			offset = 0;
		}

		/* Leave a gap for the GSO descriptor. */
		if (skb_is_gso(skb)) {
			if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4)
				gso_type = XEN_NETIF_GSO_TYPE_TCPV4;
			else if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
				gso_type = XEN_NETIF_GSO_TYPE_TCPV6;
		}

		if (*head && ((1 << gso_type) & vif->gso_mask))
			vif->rx.req_cons++;

		*head = 0; /* There must be something in this buffer now. */

	}
}

/*
 * Find the grant ref for a given frag in a chain of struct ubuf_info's
 * skb: the skb itself
 * i: the frag's number
 * ubuf: a pointer to an element in the chain. It should not be NULL
 *
 * Returns a pointer to the element in the chain where the page were found. If
 * not found, returns NULL.
 * See the definition of callback_struct in common.h for more details about
 * the chain.
 */
static const struct ubuf_info *xenvif_find_gref(const struct sk_buff *const skb,
						const int i,
						const struct ubuf_info *ubuf)
{
	struct xenvif *foreign_vif = ubuf_to_vif(ubuf);

	do {
		u16 pending_idx = ubuf->desc;

		if (skb_shinfo(skb)->frags[i].page.p ==
		    foreign_vif->mmap_pages[pending_idx])
			break;
		ubuf = (struct ubuf_info *) ubuf->ctx;
	} while (ubuf);

	return ubuf;
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
			  struct netrx_pending_operations *npo)
{
	struct xenvif *vif = netdev_priv(skb->dev);
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int i;
	struct xen_netif_rx_request *req;
	struct xenvif_rx_meta *meta;
	unsigned char *data;
	int head = 1;
	int old_meta_prod;
	int gso_type;
	const struct ubuf_info *ubuf = skb_shinfo(skb)->destructor_arg;
	const struct ubuf_info *const head_ubuf = ubuf;

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
		req = RING_GET_REQUEST(&vif->rx, vif->rx.req_cons++);
		meta = npo->meta + npo->meta_prod++;
		meta->gso_type = gso_type;
		meta->gso_size = skb_shinfo(skb)->gso_size;
		meta->size = 0;
		meta->id = req->id;
	}

	req = RING_GET_REQUEST(&vif->rx, vif->rx.req_cons++);
	meta = npo->meta + npo->meta_prod++;

	if ((1 << gso_type) & vif->gso_mask) {
		meta->gso_type = gso_type;
		meta->gso_size = skb_shinfo(skb)->gso_size;
	} else {
		meta->gso_type = XEN_NETIF_GSO_TYPE_NONE;
		meta->gso_size = 0;
	}

	meta->size = 0;
	meta->id = req->id;
	npo->copy_off = 0;
	npo->copy_gref = req->gref;

	data = skb->data;
	while (data < skb_tail_pointer(skb)) {
		unsigned int offset = offset_in_page(data);
		unsigned int len = PAGE_SIZE - offset;

		if (data + len > skb_tail_pointer(skb))
			len = skb_tail_pointer(skb) - data;

		xenvif_gop_frag_copy(vif, skb, npo,
				     virt_to_page(data), len, offset, &head,
				     NULL,
				     0);
		data += len;
	}

	for (i = 0; i < nr_frags; i++) {
		/* This variable also signals whether foreign_gref has a real
		 * value or not.
		 */
		struct xenvif *foreign_vif = NULL;
		grant_ref_t foreign_gref;

		if ((skb_shinfo(skb)->tx_flags & SKBTX_DEV_ZEROCOPY) &&
			(ubuf->callback == &xenvif_zerocopy_callback)) {
			const struct ubuf_info *const startpoint = ubuf;

			/* Ideally ubuf points to the chain element which
			 * belongs to this frag. Or if frags were removed from
			 * the beginning, then shortly before it.
			 */
			ubuf = xenvif_find_gref(skb, i, ubuf);

			/* Try again from the beginning of the list, if we
			 * haven't tried from there. This only makes sense in
			 * the unlikely event of reordering the original frags.
			 * For injected local pages it's an unnecessary second
			 * run.
			 */
			if (unlikely(!ubuf) && startpoint != head_ubuf)
				ubuf = xenvif_find_gref(skb, i, head_ubuf);

			if (likely(ubuf)) {
				u16 pending_idx = ubuf->desc;

				foreign_vif = ubuf_to_vif(ubuf);
				foreign_gref = foreign_vif->pending_tx_info[pending_idx].req.gref;
				/* Just a safety measure. If this was the last
				 * element on the list, the for loop will
				 * iterate again if a local page were added to
				 * the end. Using head_ubuf here prevents the
				 * second search on the chain. Or the original
				 * frags changed order, but that's less likely.
				 * In any way, ubuf shouldn't be NULL.
				 */
				ubuf = ubuf->ctx ?
					(struct ubuf_info *) ubuf->ctx :
					head_ubuf;
			} else
				/* This frag was a local page, added to the
				 * array after the skb left netback.
				 */
				ubuf = head_ubuf;
		}
		xenvif_gop_frag_copy(vif, skb, npo,
				     skb_frag_page(&skb_shinfo(skb)->frags[i]),
				     skb_frag_size(&skb_shinfo(skb)->frags[i]),
				     skb_shinfo(skb)->frags[i].page_offset,
				     &head,
				     foreign_vif,
				     foreign_vif ? foreign_gref : UINT_MAX);
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

static void xenvif_add_frag_responses(struct xenvif *vif, int status,
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
		make_rx_response(vif, meta[i].id, status, offset,
				 meta[i].size, flags);
	}
}

struct xenvif_rx_cb {
	int meta_slots_used;
};

#define XENVIF_RX_CB(skb) ((struct xenvif_rx_cb *)(skb)->cb)

void xenvif_kick_thread(struct xenvif *vif)
{
	wake_up(&vif->wq);
}

static void xenvif_rx_action(struct xenvif *vif)
{
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
		.copy  = vif->grant_copy_op,
		.meta  = vif->meta,
	};

	skb_queue_head_init(&rxq);

	while ((skb = skb_dequeue(&vif->rx_queue)) != NULL) {
		RING_IDX max_slots_needed;
		RING_IDX old_req_cons;
		RING_IDX ring_slots_used;
		int i;

		/* We need a cheap worse case estimate for the number of
		 * slots we'll use.
		 */

		max_slots_needed = DIV_ROUND_UP(offset_in_page(skb->data) +
						skb_headlen(skb),
						PAGE_SIZE);
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			unsigned int size;
			unsigned int offset;

			size = skb_frag_size(&skb_shinfo(skb)->frags[i]);
			offset = skb_shinfo(skb)->frags[i].page_offset;

			/* For a worse-case estimate we need to factor in
			 * the fragment page offset as this will affect the
			 * number of times xenvif_gop_frag_copy() will
			 * call start_new_rx_buffer().
			 */
			max_slots_needed += DIV_ROUND_UP(offset + size,
							 PAGE_SIZE);
		}

		/* To avoid the estimate becoming too pessimal for some
		 * frontends that limit posted rx requests, cap the estimate
		 * at MAX_SKB_FRAGS.
		 */
		if (max_slots_needed > MAX_SKB_FRAGS)
			max_slots_needed = MAX_SKB_FRAGS;

		/* We may need one more slot for GSO metadata */
		if (skb_is_gso(skb) &&
		   (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4 ||
		    skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6))
			max_slots_needed++;

		/* If the skb may not fit then bail out now */
		if (!xenvif_rx_ring_slots_available(vif, max_slots_needed)) {
			skb_queue_head(&vif->rx_queue, skb);
			need_to_notify = true;
			vif->rx_last_skb_slots = max_slots_needed;
			break;
		} else
			vif->rx_last_skb_slots = 0;

		old_req_cons = vif->rx.req_cons;
		XENVIF_RX_CB(skb)->meta_slots_used = xenvif_gop_skb(skb, &npo);
		ring_slots_used = vif->rx.req_cons - old_req_cons;

		BUG_ON(ring_slots_used > max_slots_needed);

		__skb_queue_tail(&rxq, skb);
	}

	BUG_ON(npo.meta_prod > ARRAY_SIZE(vif->meta));

	if (!npo.copy_prod)
		goto done;

	BUG_ON(npo.copy_prod > MAX_GRANT_COPY_OPS);
	gnttab_batch_copy(vif->grant_copy_op, npo.copy_prod);

	while ((skb = __skb_dequeue(&rxq)) != NULL) {

		if ((1 << vif->meta[npo.meta_cons].gso_type) &
		    vif->gso_prefix_mask) {
			resp = RING_GET_RESPONSE(&vif->rx,
						 vif->rx.rsp_prod_pvt++);

			resp->flags = XEN_NETRXF_gso_prefix | XEN_NETRXF_more_data;

			resp->offset = vif->meta[npo.meta_cons].gso_size;
			resp->id = vif->meta[npo.meta_cons].id;
			resp->status = XENVIF_RX_CB(skb)->meta_slots_used;

			npo.meta_cons++;
			XENVIF_RX_CB(skb)->meta_slots_used--;
		}


		vif->dev->stats.tx_bytes += skb->len;
		vif->dev->stats.tx_packets++;

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
		resp = make_rx_response(vif, vif->meta[npo.meta_cons].id,
					status, offset,
					vif->meta[npo.meta_cons].size,
					flags);

		if ((1 << vif->meta[npo.meta_cons].gso_type) &
		    vif->gso_mask) {
			struct xen_netif_extra_info *gso =
				(struct xen_netif_extra_info *)
				RING_GET_RESPONSE(&vif->rx,
						  vif->rx.rsp_prod_pvt++);

			resp->flags |= XEN_NETRXF_extra_info;

			gso->u.gso.type = vif->meta[npo.meta_cons].gso_type;
			gso->u.gso.size = vif->meta[npo.meta_cons].gso_size;
			gso->u.gso.pad = 0;
			gso->u.gso.features = 0;

			gso->type = XEN_NETIF_EXTRA_TYPE_GSO;
			gso->flags = 0;
		}

		xenvif_add_frag_responses(vif, status,
					  vif->meta + npo.meta_cons + 1,
					  XENVIF_RX_CB(skb)->meta_slots_used);

		RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&vif->rx, ret);

		need_to_notify |= !!ret;

		npo.meta_cons += XENVIF_RX_CB(skb)->meta_slots_used;
		dev_kfree_skb(skb);
	}

done:
	if (need_to_notify)
		notify_remote_via_irq(vif->rx_irq);
}

void xenvif_napi_schedule_or_enable_events(struct xenvif *vif)
{
	int more_to_do;

	RING_FINAL_CHECK_FOR_REQUESTS(&vif->tx, more_to_do);

	if (more_to_do)
		napi_schedule(&vif->napi);
}

static void tx_add_credit(struct xenvif *vif)
{
	unsigned long max_burst, max_credit;

	/*
	 * Allow a burst big enough to transmit a jumbo packet of up to 128kB.
	 * Otherwise the interface can seize up due to insufficient credit.
	 */
	max_burst = RING_GET_REQUEST(&vif->tx, vif->tx.req_cons)->size;
	max_burst = min(max_burst, 131072UL);
	max_burst = max(max_burst, vif->credit_bytes);

	/* Take care that adding a new chunk of credit doesn't wrap to zero. */
	max_credit = vif->remaining_credit + vif->credit_bytes;
	if (max_credit < vif->remaining_credit)
		max_credit = ULONG_MAX; /* wrapped: clamp to ULONG_MAX */

	vif->remaining_credit = min(max_credit, max_burst);
}

static void tx_credit_callback(unsigned long data)
{
	struct xenvif *vif = (struct xenvif *)data;
	tx_add_credit(vif);
	xenvif_napi_schedule_or_enable_events(vif);
}

static void xenvif_tx_err(struct xenvif *vif,
			  struct xen_netif_tx_request *txp, RING_IDX end)
{
	RING_IDX cons = vif->tx.req_cons;
	unsigned long flags;

	do {
		spin_lock_irqsave(&vif->response_lock, flags);
		make_tx_response(vif, txp, XEN_NETIF_RSP_ERROR);
		spin_unlock_irqrestore(&vif->response_lock, flags);
		if (cons == end)
			break;
		txp = RING_GET_REQUEST(&vif->tx, cons++);
	} while (1);
	vif->tx.req_cons = cons;
}

static void xenvif_fatal_tx_err(struct xenvif *vif)
{
	netdev_err(vif->dev, "fatal error; disabling device\n");
	vif->disabled = true;
	xenvif_kick_thread(vif);
}

static int xenvif_count_requests(struct xenvif *vif,
				 struct xen_netif_tx_request *first,
				 struct xen_netif_tx_request *txp,
				 int work_to_do)
{
	RING_IDX cons = vif->tx.req_cons;
	int slots = 0;
	int drop_err = 0;
	int more_data;

	if (!(first->flags & XEN_NETTXF_more_data))
		return 0;

	do {
		struct xen_netif_tx_request dropped_tx = { 0 };

		if (slots >= work_to_do) {
			netdev_err(vif->dev,
				   "Asked for %d slots but exceeds this limit\n",
				   work_to_do);
			xenvif_fatal_tx_err(vif);
			return -ENODATA;
		}

		/* This guest is really using too many slots and
		 * considered malicious.
		 */
		if (unlikely(slots >= fatal_skb_slots)) {
			netdev_err(vif->dev,
				   "Malicious frontend using %d slots, threshold %u\n",
				   slots, fatal_skb_slots);
			xenvif_fatal_tx_err(vif);
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
				netdev_dbg(vif->dev,
					   "Too many slots (%d) exceeding limit (%d), dropping packet\n",
					   slots, XEN_NETBK_LEGACY_SLOTS_MAX);
			drop_err = -E2BIG;
		}

		if (drop_err)
			txp = &dropped_tx;

		memcpy(txp, RING_GET_REQUEST(&vif->tx, cons + slots),
		       sizeof(*txp));

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
				netdev_dbg(vif->dev,
					   "Invalid tx request, slot size %u > remaining size %u\n",
					   txp->size, first->size);
			drop_err = -EIO;
		}

		first->size -= txp->size;
		slots++;

		if (unlikely((txp->offset + txp->size) > PAGE_SIZE)) {
			netdev_err(vif->dev, "Cross page boundary, txp->offset: %x, size: %u\n",
				 txp->offset, txp->size);
			xenvif_fatal_tx_err(vif);
			return -EINVAL;
		}

		more_data = txp->flags & XEN_NETTXF_more_data;

		if (!drop_err)
			txp++;

	} while (more_data);

	if (drop_err) {
		xenvif_tx_err(vif, first, cons + slots);
		return drop_err;
	}

	return slots;
}


struct xenvif_tx_cb {
	u16 pending_idx;
};

#define XENVIF_TX_CB(skb) ((struct xenvif_tx_cb *)(skb)->cb)

static inline void xenvif_tx_create_map_op(struct xenvif *vif,
					  u16 pending_idx,
					  struct xen_netif_tx_request *txp,
					  struct gnttab_map_grant_ref *mop)
{
	vif->pages_to_map[mop-vif->tx_map_ops] = vif->mmap_pages[pending_idx];
	gnttab_set_map_op(mop, idx_to_kaddr(vif, pending_idx),
			  GNTMAP_host_map | GNTMAP_readonly,
			  txp->gref, vif->domid);

	memcpy(&vif->pending_tx_info[pending_idx].req, txp,
	       sizeof(*txp));
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

static struct gnttab_map_grant_ref *xenvif_get_requests(struct xenvif *vif,
							struct sk_buff *skb,
							struct xen_netif_tx_request *txp,
							struct gnttab_map_grant_ref *gop)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	skb_frag_t *frags = shinfo->frags;
	u16 pending_idx = XENVIF_TX_CB(skb)->pending_idx;
	int start;
	pending_ring_idx_t index;
	unsigned int nr_slots, frag_overflow = 0;

	/* At this point shinfo->nr_frags is in fact the number of
	 * slots, which can be as large as XEN_NETBK_LEGACY_SLOTS_MAX.
	 */
	if (shinfo->nr_frags > MAX_SKB_FRAGS) {
		frag_overflow = shinfo->nr_frags - MAX_SKB_FRAGS;
		BUG_ON(frag_overflow > MAX_SKB_FRAGS);
		shinfo->nr_frags = MAX_SKB_FRAGS;
	}
	nr_slots = shinfo->nr_frags;

	/* Skip first skb fragment if it is on same page as header fragment. */
	start = (frag_get_pending_idx(&shinfo->frags[0]) == pending_idx);

	for (shinfo->nr_frags = start; shinfo->nr_frags < nr_slots;
	     shinfo->nr_frags++, txp++, gop++) {
		index = pending_index(vif->pending_cons++);
		pending_idx = vif->pending_ring[index];
		xenvif_tx_create_map_op(vif, pending_idx, txp, gop);
		frag_set_pending_idx(&frags[shinfo->nr_frags], pending_idx);
	}

	if (frag_overflow) {
		struct sk_buff *nskb = xenvif_alloc_skb(0);
		if (unlikely(nskb == NULL)) {
			if (net_ratelimit())
				netdev_err(vif->dev,
					   "Can't allocate the frag_list skb.\n");
			return NULL;
		}

		shinfo = skb_shinfo(nskb);
		frags = shinfo->frags;

		for (shinfo->nr_frags = 0; shinfo->nr_frags < frag_overflow;
		     shinfo->nr_frags++, txp++, gop++) {
			index = pending_index(vif->pending_cons++);
			pending_idx = vif->pending_ring[index];
			xenvif_tx_create_map_op(vif, pending_idx, txp, gop);
			frag_set_pending_idx(&frags[shinfo->nr_frags],
					     pending_idx);
		}

		skb_shinfo(skb)->frag_list = nskb;
	}

	return gop;
}

static inline void xenvif_grant_handle_set(struct xenvif *vif,
					   u16 pending_idx,
					   grant_handle_t handle)
{
	if (unlikely(vif->grant_tx_handle[pending_idx] !=
		     NETBACK_INVALID_HANDLE)) {
		netdev_err(vif->dev,
			   "Trying to overwrite active handle! pending_idx: %x\n",
			   pending_idx);
		BUG();
	}
	vif->grant_tx_handle[pending_idx] = handle;
}

static inline void xenvif_grant_handle_reset(struct xenvif *vif,
					     u16 pending_idx)
{
	if (unlikely(vif->grant_tx_handle[pending_idx] ==
		     NETBACK_INVALID_HANDLE)) {
		netdev_err(vif->dev,
			   "Trying to unmap invalid handle! pending_idx: %x\n",
			   pending_idx);
		BUG();
	}
	vif->grant_tx_handle[pending_idx] = NETBACK_INVALID_HANDLE;
}

static int xenvif_tx_check_gop(struct xenvif *vif,
			       struct sk_buff *skb,
			       struct gnttab_map_grant_ref **gopp_map,
			       struct gnttab_copy **gopp_copy)
{
	struct gnttab_map_grant_ref *gop_map = *gopp_map;
	u16 pending_idx = XENVIF_TX_CB(skb)->pending_idx;
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	int nr_frags = shinfo->nr_frags;
	int i, err;
	struct sk_buff *first_skb = NULL;

	/* Check status of header. */
	err = (*gopp_copy)->status;
	(*gopp_copy)++;
	if (unlikely(err)) {
		if (net_ratelimit())
			netdev_dbg(vif->dev,
				   "Grant copy of header failed! status: %d pending_idx: %u ref: %u\n",
				   (*gopp_copy)->status,
				   pending_idx,
				   (*gopp_copy)->source.u.ref);
		xenvif_idx_release(vif, pending_idx, XEN_NETIF_RSP_ERROR);
	}

check_frags:
	for (i = 0; i < nr_frags; i++, gop_map++) {
		int j, newerr;

		pending_idx = frag_get_pending_idx(&shinfo->frags[i]);

		/* Check error status: if okay then remember grant handle. */
		newerr = gop_map->status;

		if (likely(!newerr)) {
			xenvif_grant_handle_set(vif,
						pending_idx,
						gop_map->handle);
			/* Had a previous error? Invalidate this fragment. */
			if (unlikely(err))
				xenvif_idx_unmap(vif, pending_idx);
			continue;
		}

		/* Error on this fragment: respond to client with an error. */
		if (net_ratelimit())
			netdev_dbg(vif->dev,
				   "Grant map of %d. frag failed! status: %d pending_idx: %u ref: %u\n",
				   i,
				   gop_map->status,
				   pending_idx,
				   gop_map->ref);
		xenvif_idx_release(vif, pending_idx, XEN_NETIF_RSP_ERROR);

		/* Not the first error? Preceding frags already invalidated. */
		if (err)
			continue;
		/* First error: invalidate preceding fragments. */
		for (j = 0; j < i; j++) {
			pending_idx = frag_get_pending_idx(&shinfo->frags[j]);
			xenvif_idx_unmap(vif, pending_idx);
		}

		/* Remember the error: invalidate all subsequent fragments. */
		err = newerr;
	}

	if (skb_has_frag_list(skb)) {
		first_skb = skb;
		skb = shinfo->frag_list;
		shinfo = skb_shinfo(skb);
		nr_frags = shinfo->nr_frags;

		goto check_frags;
	}

	/* There was a mapping error in the frag_list skb. We have to unmap
	 * the first skb's frags
	 */
	if (first_skb && err) {
		int j;
		shinfo = skb_shinfo(first_skb);
		for (j = 0; j < shinfo->nr_frags; j++) {
			pending_idx = frag_get_pending_idx(&shinfo->frags[j]);
			xenvif_idx_unmap(vif, pending_idx);
		}
	}

	*gopp_map = gop_map;
	return err;
}

static void xenvif_fill_frags(struct xenvif *vif, struct sk_buff *skb)
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
				&callback_param(vif, pending_idx);
		else
			callback_param(vif, prev_pending_idx).ctx =
				&callback_param(vif, pending_idx);

		callback_param(vif, pending_idx).ctx = NULL;
		prev_pending_idx = pending_idx;

		txp = &vif->pending_tx_info[pending_idx].req;
		page = virt_to_page(idx_to_kaddr(vif, pending_idx));
		__skb_fill_page_desc(skb, i, page, txp->offset, txp->size);
		skb->len += txp->size;
		skb->data_len += txp->size;
		skb->truesize += txp->size;

		/* Take an extra reference to offset network stack's put_page */
		get_page(vif->mmap_pages[pending_idx]);
	}
	/* FIXME: __skb_fill_page_desc set this to true because page->pfmemalloc
	 * overlaps with "index", and "mapping" is not set. I think mapping
	 * should be set. If delivered to local stack, it would drop this
	 * skb in sk_filter unless the socket has the right to use it.
	 */
	skb->pfmemalloc	= false;
}

static int xenvif_get_extras(struct xenvif *vif,
				struct xen_netif_extra_info *extras,
				int work_to_do)
{
	struct xen_netif_extra_info extra;
	RING_IDX cons = vif->tx.req_cons;

	do {
		if (unlikely(work_to_do-- <= 0)) {
			netdev_err(vif->dev, "Missing extra info\n");
			xenvif_fatal_tx_err(vif);
			return -EBADR;
		}

		memcpy(&extra, RING_GET_REQUEST(&vif->tx, cons),
		       sizeof(extra));
		if (unlikely(!extra.type ||
			     extra.type >= XEN_NETIF_EXTRA_TYPE_MAX)) {
			vif->tx.req_cons = ++cons;
			netdev_err(vif->dev,
				   "Invalid extra type: %d\n", extra.type);
			xenvif_fatal_tx_err(vif);
			return -EINVAL;
		}

		memcpy(&extras[extra.type - 1], &extra, sizeof(extra));
		vif->tx.req_cons = ++cons;
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

static int checksum_setup(struct xenvif *vif, struct sk_buff *skb)
{
	bool recalculate_partial_csum = false;

	/* A GSO SKB must be CHECKSUM_PARTIAL. However some buggy
	 * peers can fail to set NETRXF_csum_blank when sending a GSO
	 * frame. In this case force the SKB to CHECKSUM_PARTIAL and
	 * recalculate the partial checksum.
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL && skb_is_gso(skb)) {
		vif->rx_gso_checksum_fixup++;
		skb->ip_summed = CHECKSUM_PARTIAL;
		recalculate_partial_csum = true;
	}

	/* A non-CHECKSUM_PARTIAL SKB does not require setup. */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	return skb_checksum_setup(skb, recalculate_partial_csum);
}

static bool tx_credit_exceeded(struct xenvif *vif, unsigned size)
{
	u64 now = get_jiffies_64();
	u64 next_credit = vif->credit_window_start +
		msecs_to_jiffies(vif->credit_usec / 1000);

	/* Timer could already be pending in rare cases. */
	if (timer_pending(&vif->credit_timeout))
		return true;

	/* Passed the point where we can replenish credit? */
	if (time_after_eq64(now, next_credit)) {
		vif->credit_window_start = now;
		tx_add_credit(vif);
	}

	/* Still too big to send right now? Set a callback. */
	if (size > vif->remaining_credit) {
		vif->credit_timeout.data     =
			(unsigned long)vif;
		vif->credit_timeout.function =
			tx_credit_callback;
		mod_timer(&vif->credit_timeout,
			  next_credit);
		vif->credit_window_start = next_credit;

		return true;
	}

	return false;
}

static void xenvif_tx_build_gops(struct xenvif *vif,
				     int budget,
				     unsigned *copy_ops,
				     unsigned *map_ops)
{
	struct gnttab_map_grant_ref *gop = vif->tx_map_ops, *request_gop;
	struct sk_buff *skb;
	int ret;

	while (skb_queue_len(&vif->tx_queue) < budget) {
		struct xen_netif_tx_request txreq;
		struct xen_netif_tx_request txfrags[XEN_NETBK_LEGACY_SLOTS_MAX];
		struct xen_netif_extra_info extras[XEN_NETIF_EXTRA_TYPE_MAX-1];
		u16 pending_idx;
		RING_IDX idx;
		int work_to_do;
		unsigned int data_len;
		pending_ring_idx_t index;

		if (vif->tx.sring->req_prod - vif->tx.req_cons >
		    XEN_NETIF_TX_RING_SIZE) {
			netdev_err(vif->dev,
				   "Impossible number of requests. "
				   "req_prod %d, req_cons %d, size %ld\n",
				   vif->tx.sring->req_prod, vif->tx.req_cons,
				   XEN_NETIF_TX_RING_SIZE);
			xenvif_fatal_tx_err(vif);
			break;
		}

		work_to_do = RING_HAS_UNCONSUMED_REQUESTS(&vif->tx);
		if (!work_to_do)
			break;

		idx = vif->tx.req_cons;
		rmb(); /* Ensure that we see the request before we copy it. */
		memcpy(&txreq, RING_GET_REQUEST(&vif->tx, idx), sizeof(txreq));

		/* Credit-based scheduling. */
		if (txreq.size > vif->remaining_credit &&
		    tx_credit_exceeded(vif, txreq.size))
			break;

		vif->remaining_credit -= txreq.size;

		work_to_do--;
		vif->tx.req_cons = ++idx;

		memset(extras, 0, sizeof(extras));
		if (txreq.flags & XEN_NETTXF_extra_info) {
			work_to_do = xenvif_get_extras(vif, extras,
						       work_to_do);
			idx = vif->tx.req_cons;
			if (unlikely(work_to_do < 0))
				break;
		}

		ret = xenvif_count_requests(vif, &txreq, txfrags, work_to_do);
		if (unlikely(ret < 0))
			break;

		idx += ret;

		if (unlikely(txreq.size < ETH_HLEN)) {
			netdev_dbg(vif->dev,
				   "Bad packet size: %d\n", txreq.size);
			xenvif_tx_err(vif, &txreq, idx);
			break;
		}

		/* No crossing a page as the payload mustn't fragment. */
		if (unlikely((txreq.offset + txreq.size) > PAGE_SIZE)) {
			netdev_err(vif->dev,
				   "txreq.offset: %x, size: %u, end: %lu\n",
				   txreq.offset, txreq.size,
				   (txreq.offset&~PAGE_MASK) + txreq.size);
			xenvif_fatal_tx_err(vif);
			break;
		}

		index = pending_index(vif->pending_cons);
		pending_idx = vif->pending_ring[index];

		data_len = (txreq.size > PKT_PROT_LEN &&
			    ret < XEN_NETBK_LEGACY_SLOTS_MAX) ?
			PKT_PROT_LEN : txreq.size;

		skb = xenvif_alloc_skb(data_len);
		if (unlikely(skb == NULL)) {
			netdev_dbg(vif->dev,
				   "Can't allocate a skb in start_xmit.\n");
			xenvif_tx_err(vif, &txreq, idx);
			break;
		}

		if (extras[XEN_NETIF_EXTRA_TYPE_GSO - 1].type) {
			struct xen_netif_extra_info *gso;
			gso = &extras[XEN_NETIF_EXTRA_TYPE_GSO - 1];

			if (xenvif_set_skb_gso(vif, skb, gso)) {
				/* Failure in xenvif_set_skb_gso is fatal. */
				kfree_skb(skb);
				break;
			}
		}

		XENVIF_TX_CB(skb)->pending_idx = pending_idx;

		__skb_put(skb, data_len);
		vif->tx_copy_ops[*copy_ops].source.u.ref = txreq.gref;
		vif->tx_copy_ops[*copy_ops].source.domid = vif->domid;
		vif->tx_copy_ops[*copy_ops].source.offset = txreq.offset;

		vif->tx_copy_ops[*copy_ops].dest.u.gmfn =
			virt_to_mfn(skb->data);
		vif->tx_copy_ops[*copy_ops].dest.domid = DOMID_SELF;
		vif->tx_copy_ops[*copy_ops].dest.offset =
			offset_in_page(skb->data);

		vif->tx_copy_ops[*copy_ops].len = data_len;
		vif->tx_copy_ops[*copy_ops].flags = GNTCOPY_source_gref;

		(*copy_ops)++;

		skb_shinfo(skb)->nr_frags = ret;
		if (data_len < txreq.size) {
			skb_shinfo(skb)->nr_frags++;
			frag_set_pending_idx(&skb_shinfo(skb)->frags[0],
					     pending_idx);
			xenvif_tx_create_map_op(vif, pending_idx, &txreq, gop);
			gop++;
		} else {
			frag_set_pending_idx(&skb_shinfo(skb)->frags[0],
					     INVALID_PENDING_IDX);
			memcpy(&vif->pending_tx_info[pending_idx].req, &txreq,
			       sizeof(txreq));
		}

		vif->pending_cons++;

		request_gop = xenvif_get_requests(vif, skb, txfrags, gop);
		if (request_gop == NULL) {
			kfree_skb(skb);
			xenvif_tx_err(vif, &txreq, idx);
			break;
		}
		gop = request_gop;

		__skb_queue_tail(&vif->tx_queue, skb);

		vif->tx.req_cons = idx;

		if (((gop-vif->tx_map_ops) >= ARRAY_SIZE(vif->tx_map_ops)) ||
		    (*copy_ops >= ARRAY_SIZE(vif->tx_copy_ops)))
			break;
	}

	(*map_ops) = gop - vif->tx_map_ops;
	return;
}

/* Consolidate skb with a frag_list into a brand new one with local pages on
 * frags. Returns 0 or -ENOMEM if can't allocate new pages.
 */
static int xenvif_handle_frag_list(struct xenvif *vif, struct sk_buff *skb)
{
	unsigned int offset = skb_headlen(skb);
	skb_frag_t frags[MAX_SKB_FRAGS];
	int i;
	struct ubuf_info *uarg;
	struct sk_buff *nskb = skb_shinfo(skb)->frag_list;

	vif->tx_zerocopy_sent += 2;
	vif->tx_frag_overflow++;

	xenvif_fill_frags(vif, nskb);
	/* Subtract frags size, we will correct it later */
	skb->truesize -= skb->data_len;
	skb->len += nskb->len;
	skb->data_len += nskb->len;

	/* create a brand new frags array and coalesce there */
	for (i = 0; offset < skb->len; i++) {
		struct page *page;
		unsigned int len;

		BUG_ON(i >= MAX_SKB_FRAGS);
		page = alloc_page(GFP_ATOMIC|__GFP_COLD);
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
	/* swap out with old one */
	memcpy(skb_shinfo(skb)->frags,
	       frags,
	       i * sizeof(skb_frag_t));
	skb_shinfo(skb)->nr_frags = i;
	skb->truesize += i * PAGE_SIZE;

	/* remove traces of mapped pages and frag_list */
	skb_frag_list_init(skb);
	uarg = skb_shinfo(skb)->destructor_arg;
	uarg->callback(uarg, true);
	skb_shinfo(skb)->destructor_arg = NULL;

	skb_shinfo(nskb)->tx_flags |= SKBTX_DEV_ZEROCOPY;
	kfree_skb(nskb);

	return 0;
}

static int xenvif_tx_submit(struct xenvif *vif)
{
	struct gnttab_map_grant_ref *gop_map = vif->tx_map_ops;
	struct gnttab_copy *gop_copy = vif->tx_copy_ops;
	struct sk_buff *skb;
	int work_done = 0;

	while ((skb = __skb_dequeue(&vif->tx_queue)) != NULL) {
		struct xen_netif_tx_request *txp;
		u16 pending_idx;
		unsigned data_len;

		pending_idx = XENVIF_TX_CB(skb)->pending_idx;
		txp = &vif->pending_tx_info[pending_idx].req;

		/* Check the remap error code. */
		if (unlikely(xenvif_tx_check_gop(vif, skb, &gop_map, &gop_copy))) {
			skb_shinfo(skb)->nr_frags = 0;
			kfree_skb(skb);
			continue;
		}

		data_len = skb->len;
		callback_param(vif, pending_idx).ctx = NULL;
		if (data_len < txp->size) {
			/* Append the packet payload as a fragment. */
			txp->offset += data_len;
			txp->size -= data_len;
		} else {
			/* Schedule a response immediately. */
			xenvif_idx_release(vif, pending_idx,
					   XEN_NETIF_RSP_OKAY);
		}

		if (txp->flags & XEN_NETTXF_csum_blank)
			skb->ip_summed = CHECKSUM_PARTIAL;
		else if (txp->flags & XEN_NETTXF_data_validated)
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		xenvif_fill_frags(vif, skb);

		if (unlikely(skb_has_frag_list(skb))) {
			if (xenvif_handle_frag_list(vif, skb)) {
				if (net_ratelimit())
					netdev_err(vif->dev,
						   "Not enough memory to consolidate frag_list!\n");
				skb_shinfo(skb)->tx_flags |= SKBTX_DEV_ZEROCOPY;
				kfree_skb(skb);
				continue;
			}
		}

		if (skb_is_nonlinear(skb) && skb_headlen(skb) < PKT_PROT_LEN) {
			int target = min_t(int, skb->len, PKT_PROT_LEN);
			__pskb_pull_tail(skb, target - skb_headlen(skb));
		}

		skb->dev      = vif->dev;
		skb->protocol = eth_type_trans(skb, skb->dev);
		skb_reset_network_header(skb);

		if (checksum_setup(vif, skb)) {
			netdev_dbg(vif->dev,
				   "Can't setup checksum in net_tx_action\n");
			/* We have to set this flag to trigger the callback */
			if (skb_shinfo(skb)->destructor_arg)
				skb_shinfo(skb)->tx_flags |= SKBTX_DEV_ZEROCOPY;
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

		vif->dev->stats.rx_bytes += skb->len;
		vif->dev->stats.rx_packets++;

		work_done++;

		/* Set this flag right before netif_receive_skb, otherwise
		 * someone might think this packet already left netback, and
		 * do a skb_copy_ubufs while we are still in control of the
		 * skb. E.g. the __pskb_pull_tail earlier can do such thing.
		 */
		if (skb_shinfo(skb)->destructor_arg) {
			skb_shinfo(skb)->tx_flags |= SKBTX_DEV_ZEROCOPY;
			vif->tx_zerocopy_sent++;
		}

		netif_receive_skb(skb);
	}

	return work_done;
}

void xenvif_zerocopy_callback(struct ubuf_info *ubuf, bool zerocopy_success)
{
	unsigned long flags;
	pending_ring_idx_t index;
	struct xenvif *vif = ubuf_to_vif(ubuf);

	/* This is the only place where we grab this lock, to protect callbacks
	 * from each other.
	 */
	spin_lock_irqsave(&vif->callback_lock, flags);
	do {
		u16 pending_idx = ubuf->desc;
		ubuf = (struct ubuf_info *) ubuf->ctx;
		BUG_ON(vif->dealloc_prod - vif->dealloc_cons >=
			MAX_PENDING_REQS);
		index = pending_index(vif->dealloc_prod);
		vif->dealloc_ring[index] = pending_idx;
		/* Sync with xenvif_tx_dealloc_action:
		 * insert idx then incr producer.
		 */
		smp_wmb();
		vif->dealloc_prod++;
	} while (ubuf);
	wake_up(&vif->dealloc_wq);
	spin_unlock_irqrestore(&vif->callback_lock, flags);

	if (likely(zerocopy_success))
		vif->tx_zerocopy_success++;
	else
		vif->tx_zerocopy_fail++;
}

static inline void xenvif_tx_dealloc_action(struct xenvif *vif)
{
	struct gnttab_unmap_grant_ref *gop;
	pending_ring_idx_t dc, dp;
	u16 pending_idx, pending_idx_release[MAX_PENDING_REQS];
	unsigned int i = 0;

	dc = vif->dealloc_cons;
	gop = vif->tx_unmap_ops;

	/* Free up any grants we have finished using */
	do {
		dp = vif->dealloc_prod;

		/* Ensure we see all indices enqueued by all
		 * xenvif_zerocopy_callback().
		 */
		smp_rmb();

		while (dc != dp) {
			BUG_ON(gop - vif->tx_unmap_ops > MAX_PENDING_REQS);
			pending_idx =
				vif->dealloc_ring[pending_index(dc++)];

			pending_idx_release[gop-vif->tx_unmap_ops] =
				pending_idx;
			vif->pages_to_unmap[gop-vif->tx_unmap_ops] =
				vif->mmap_pages[pending_idx];
			gnttab_set_unmap_op(gop,
					    idx_to_kaddr(vif, pending_idx),
					    GNTMAP_host_map,
					    vif->grant_tx_handle[pending_idx]);
			xenvif_grant_handle_reset(vif, pending_idx);
			++gop;
		}

	} while (dp != vif->dealloc_prod);

	vif->dealloc_cons = dc;

	if (gop - vif->tx_unmap_ops > 0) {
		int ret;
		ret = gnttab_unmap_refs(vif->tx_unmap_ops,
					NULL,
					vif->pages_to_unmap,
					gop - vif->tx_unmap_ops);
		if (ret) {
			netdev_err(vif->dev, "Unmap fail: nr_ops %tx ret %d\n",
				   gop - vif->tx_unmap_ops, ret);
			for (i = 0; i < gop - vif->tx_unmap_ops; ++i) {
				if (gop[i].status != GNTST_okay)
					netdev_err(vif->dev,
						   " host_addr: %llx handle: %x status: %d\n",
						   gop[i].host_addr,
						   gop[i].handle,
						   gop[i].status);
			}
			BUG();
		}
	}

	for (i = 0; i < gop - vif->tx_unmap_ops; ++i)
		xenvif_idx_release(vif, pending_idx_release[i],
				   XEN_NETIF_RSP_OKAY);
}


/* Called after netfront has transmitted */
int xenvif_tx_action(struct xenvif *vif, int budget)
{
	unsigned nr_mops, nr_cops = 0;
	int work_done, ret;

	if (unlikely(!tx_work_todo(vif)))
		return 0;

	xenvif_tx_build_gops(vif, budget, &nr_cops, &nr_mops);

	if (nr_cops == 0)
		return 0;

	gnttab_batch_copy(vif->tx_copy_ops, nr_cops);
	if (nr_mops != 0) {
		ret = gnttab_map_refs(vif->tx_map_ops,
				      NULL,
				      vif->pages_to_map,
				      nr_mops);
		BUG_ON(ret);
	}

	work_done = xenvif_tx_submit(vif);

	return work_done;
}

static void xenvif_idx_release(struct xenvif *vif, u16 pending_idx,
			       u8 status)
{
	struct pending_tx_info *pending_tx_info;
	pending_ring_idx_t index;
	unsigned long flags;

	pending_tx_info = &vif->pending_tx_info[pending_idx];
	spin_lock_irqsave(&vif->response_lock, flags);
	make_tx_response(vif, &pending_tx_info->req, status);
	index = pending_index(vif->pending_prod);
	vif->pending_ring[index] = pending_idx;
	/* TX shouldn't use the index before we give it back here */
	mb();
	vif->pending_prod++;
	spin_unlock_irqrestore(&vif->response_lock, flags);
}


static void make_tx_response(struct xenvif *vif,
			     struct xen_netif_tx_request *txp,
			     s8       st)
{
	RING_IDX i = vif->tx.rsp_prod_pvt;
	struct xen_netif_tx_response *resp;
	int notify;

	resp = RING_GET_RESPONSE(&vif->tx, i);
	resp->id     = txp->id;
	resp->status = st;

	if (txp->flags & XEN_NETTXF_extra_info)
		RING_GET_RESPONSE(&vif->tx, ++i)->status = XEN_NETIF_RSP_NULL;

	vif->tx.rsp_prod_pvt = ++i;
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&vif->tx, notify);
	if (notify)
		notify_remote_via_irq(vif->tx_irq);
}

static struct xen_netif_rx_response *make_rx_response(struct xenvif *vif,
					     u16      id,
					     s8       st,
					     u16      offset,
					     u16      size,
					     u16      flags)
{
	RING_IDX i = vif->rx.rsp_prod_pvt;
	struct xen_netif_rx_response *resp;

	resp = RING_GET_RESPONSE(&vif->rx, i);
	resp->offset     = offset;
	resp->flags      = flags;
	resp->id         = id;
	resp->status     = (s16)size;
	if (st < 0)
		resp->status = (s16)st;

	vif->rx.rsp_prod_pvt = ++i;

	return resp;
}

void xenvif_idx_unmap(struct xenvif *vif, u16 pending_idx)
{
	int ret;
	struct gnttab_unmap_grant_ref tx_unmap_op;

	gnttab_set_unmap_op(&tx_unmap_op,
			    idx_to_kaddr(vif, pending_idx),
			    GNTMAP_host_map,
			    vif->grant_tx_handle[pending_idx]);
	xenvif_grant_handle_reset(vif, pending_idx);

	ret = gnttab_unmap_refs(&tx_unmap_op, NULL,
				&vif->mmap_pages[pending_idx], 1);
	if (ret) {
		netdev_err(vif->dev,
			   "Unmap fail: ret: %d pending_idx: %d host_addr: %llx handle: %x status: %d\n",
			   ret,
			   pending_idx,
			   tx_unmap_op.host_addr,
			   tx_unmap_op.handle,
			   tx_unmap_op.status);
		BUG();
	}

	xenvif_idx_release(vif, pending_idx, XEN_NETIF_RSP_OKAY);
}

static inline int rx_work_todo(struct xenvif *vif)
{
	return (!skb_queue_empty(&vif->rx_queue) &&
	       xenvif_rx_ring_slots_available(vif, vif->rx_last_skb_slots)) ||
	       vif->rx_queue_purge;
}

static inline int tx_work_todo(struct xenvif *vif)
{

	if (likely(RING_HAS_UNCONSUMED_REQUESTS(&vif->tx)))
		return 1;

	return 0;
}

static inline bool tx_dealloc_work_todo(struct xenvif *vif)
{
	return vif->dealloc_cons != vif->dealloc_prod;
}

void xenvif_unmap_frontend_rings(struct xenvif *vif)
{
	if (vif->tx.sring)
		xenbus_unmap_ring_vfree(xenvif_to_xenbus_device(vif),
					vif->tx.sring);
	if (vif->rx.sring)
		xenbus_unmap_ring_vfree(xenvif_to_xenbus_device(vif),
					vif->rx.sring);
}

int xenvif_map_frontend_rings(struct xenvif *vif,
			      grant_ref_t tx_ring_ref,
			      grant_ref_t rx_ring_ref)
{
	void *addr;
	struct xen_netif_tx_sring *txs;
	struct xen_netif_rx_sring *rxs;

	int err = -ENOMEM;

	err = xenbus_map_ring_valloc(xenvif_to_xenbus_device(vif),
				     tx_ring_ref, &addr);
	if (err)
		goto err;

	txs = (struct xen_netif_tx_sring *)addr;
	BACK_RING_INIT(&vif->tx, txs, PAGE_SIZE);

	err = xenbus_map_ring_valloc(xenvif_to_xenbus_device(vif),
				     rx_ring_ref, &addr);
	if (err)
		goto err;

	rxs = (struct xen_netif_rx_sring *)addr;
	BACK_RING_INIT(&vif->rx, rxs, PAGE_SIZE);

	return 0;

err:
	xenvif_unmap_frontend_rings(vif);
	return err;
}

void xenvif_stop_queue(struct xenvif *vif)
{
	if (!vif->can_queue)
		return;

	netif_stop_queue(vif->dev);
}

static void xenvif_start_queue(struct xenvif *vif)
{
	if (xenvif_schedulable(vif))
		netif_wake_queue(vif->dev);
}

int xenvif_kthread_guest_rx(void *data)
{
	struct xenvif *vif = data;
	struct sk_buff *skb;

	while (!kthread_should_stop()) {
		wait_event_interruptible(vif->wq,
					 rx_work_todo(vif) ||
					 vif->disabled ||
					 kthread_should_stop());

		/* This frontend is found to be rogue, disable it in
		 * kthread context. Currently this is only set when
		 * netback finds out frontend sends malformed packet,
		 * but we cannot disable the interface in softirq
		 * context so we defer it here.
		 */
		if (unlikely(vif->disabled && netif_carrier_ok(vif->dev)))
			xenvif_carrier_off(vif);

		if (kthread_should_stop())
			break;

		if (vif->rx_queue_purge) {
			skb_queue_purge(&vif->rx_queue);
			vif->rx_queue_purge = false;
		}

		if (!skb_queue_empty(&vif->rx_queue))
			xenvif_rx_action(vif);

		if (skb_queue_empty(&vif->rx_queue) &&
		    netif_queue_stopped(vif->dev)) {
			del_timer_sync(&vif->wake_queue);
			xenvif_start_queue(vif);
		}

		cond_resched();
	}

	/* Bin any remaining skbs */
	while ((skb = skb_dequeue(&vif->rx_queue)) != NULL)
		dev_kfree_skb(skb);

	return 0;
}

int xenvif_dealloc_kthread(void *data)
{
	struct xenvif *vif = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(vif->dealloc_wq,
					 tx_dealloc_work_todo(vif) ||
					 kthread_should_stop());
		if (kthread_should_stop())
			break;

		xenvif_tx_dealloc_action(vif);
		cond_resched();
	}

	/* Unmap anything remaining*/
	if (tx_dealloc_work_todo(vif))
		xenvif_tx_dealloc_action(vif);

	return 0;
}

static int __init netback_init(void)
{
	int rc = 0;

	if (!xen_domain())
		return -ENODEV;

	if (fatal_skb_slots < XEN_NETBK_LEGACY_SLOTS_MAX) {
		pr_info("fatal_skb_slots too small (%d), bump it to XEN_NETBK_LEGACY_SLOTS_MAX (%d)\n",
			fatal_skb_slots, XEN_NETBK_LEGACY_SLOTS_MAX);
		fatal_skb_slots = XEN_NETBK_LEGACY_SLOTS_MAX;
	}

	rc = xenvif_xenbus_init();
	if (rc)
		goto failed_init;

	rx_drain_timeout_jiffies = msecs_to_jiffies(rx_drain_timeout_msecs);

	return 0;

failed_init:
	return rc;
}

module_init(netback_init);

static void __exit netback_fini(void)
{
	xenvif_xenbus_fini();
}
module_exit(netback_fini);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("xen-backend:vif");
