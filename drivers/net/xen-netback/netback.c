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

#define MAX_QUEUES_DEFAULT 8
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

/* The module parameter tells that we have to put data
 * for xen-netfront with the XDP_PACKET_HEADROOM offset
 * needed for XDP processing
 */
bool provides_xdp_headroom = true;
module_param(provides_xdp_headroom, bool, 0644);

static void xenvif_idx_release(struct xenvif_queue *queue, u16 pending_idx,
			       u8 status);

static void make_tx_response(struct xenvif_queue *queue,
			     struct xen_netif_tx_request *txp,
			     unsigned int extra_count,
			     s8       st);
static void push_tx_responses(struct xenvif_queue *queue);

static void xenvif_idx_unmap(struct xenvif_queue *queue, u16 pending_idx);

static inline int tx_work_todo(struct xenvif_queue *queue);

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
static inline struct xenvif_queue *ubuf_to_queue(const struct ubuf_info_msgzc *ubuf)
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
	return (u16)skb_frag_off(frag);
}

static void frag_set_pending_idx(skb_frag_t *frag, u16 pending_idx)
{
	skb_frag_off_set(frag, pending_idx);
}

static inline pending_ring_idx_t pending_index(unsigned i)
{
	return i & (MAX_PENDING_REQS-1);
}

void xenvif_kick_thread(struct xenvif_queue *queue)
{
	wake_up(&queue->wq);
}

void xenvif_napi_schedule_or_enable_events(struct xenvif_queue *queue)
{
	int more_to_do;

	RING_FINAL_CHECK_FOR_REQUESTS(&queue->tx, more_to_do);

	if (more_to_do)
		napi_schedule(&queue->napi);
	else if (atomic_fetch_andnot(NETBK_TX_EOI | NETBK_COMMON_EOI,
				     &queue->eoi_pending) &
		 (NETBK_TX_EOI | NETBK_COMMON_EOI))
		xen_irq_lateeoi(queue->tx_irq, 0);
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
	queue->rate_limited = false;
}

void xenvif_tx_credit_callback(struct timer_list *t)
{
	struct xenvif_queue *queue = from_timer(queue, t, credit_timeout);
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
	if (vif->num_queues)
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
	u16 copy_pending_idx[XEN_NETBK_LEGACY_SLOTS_MAX + 1];
	u8 copy_count;
};

#define XENVIF_TX_CB(skb) ((struct xenvif_tx_cb *)(skb)->cb)
#define copy_pending_idx(skb, i) (XENVIF_TX_CB(skb)->copy_pending_idx[i])
#define copy_count(skb) (XENVIF_TX_CB(skb)->copy_count)

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

static void xenvif_get_requests(struct xenvif_queue *queue,
				struct sk_buff *skb,
				struct xen_netif_tx_request *first,
				struct xen_netif_tx_request *txfrags,
			        unsigned *copy_ops,
			        unsigned *map_ops,
				unsigned int frag_overflow,
				struct sk_buff *nskb,
				unsigned int extra_count,
				unsigned int data_len)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	skb_frag_t *frags = shinfo->frags;
	u16 pending_idx;
	pending_ring_idx_t index;
	unsigned int nr_slots;
	struct gnttab_copy *cop = queue->tx_copy_ops + *copy_ops;
	struct gnttab_map_grant_ref *gop = queue->tx_map_ops + *map_ops;
	struct xen_netif_tx_request *txp = first;

	nr_slots = shinfo->nr_frags + 1;

	copy_count(skb) = 0;

	/* Create copy ops for exactly data_len bytes into the skb head. */
	__skb_put(skb, data_len);
	while (data_len > 0) {
		int amount = data_len > txp->size ? txp->size : data_len;

		cop->source.u.ref = txp->gref;
		cop->source.domid = queue->vif->domid;
		cop->source.offset = txp->offset;

		cop->dest.domid = DOMID_SELF;
		cop->dest.offset = (offset_in_page(skb->data +
						   skb_headlen(skb) -
						   data_len)) & ~XEN_PAGE_MASK;
		cop->dest.u.gmfn = virt_to_gfn(skb->data + skb_headlen(skb)
				               - data_len);

		cop->len = amount;
		cop->flags = GNTCOPY_source_gref;

		index = pending_index(queue->pending_cons);
		pending_idx = queue->pending_ring[index];
		callback_param(queue, pending_idx).ctx = NULL;
		copy_pending_idx(skb, copy_count(skb)) = pending_idx;
		copy_count(skb)++;

		cop++;
		data_len -= amount;

		if (amount == txp->size) {
			/* The copy op covered the full tx_request */

			memcpy(&queue->pending_tx_info[pending_idx].req,
			       txp, sizeof(*txp));
			queue->pending_tx_info[pending_idx].extra_count =
				(txp == first) ? extra_count : 0;

			if (txp == first)
				txp = txfrags;
			else
				txp++;
			queue->pending_cons++;
			nr_slots--;
		} else {
			/* The copy op partially covered the tx_request.
			 * The remainder will be mapped.
			 */
			txp->offset += amount;
			txp->size -= amount;
		}
	}

	for (shinfo->nr_frags = 0; shinfo->nr_frags < nr_slots;
	     shinfo->nr_frags++, gop++) {
		index = pending_index(queue->pending_cons++);
		pending_idx = queue->pending_ring[index];
		xenvif_tx_create_map_op(queue, pending_idx, txp,
				        txp == first ? extra_count : 0, gop);
		frag_set_pending_idx(&frags[shinfo->nr_frags], pending_idx);

		if (txp == first)
			txp = txfrags;
		else
			txp++;
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

	(*copy_ops) = cop - queue->tx_copy_ops;
	(*map_ops) = gop - queue->tx_map_ops;
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
	u16 pending_idx;
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
				frag_get_pending_idx(&shinfo->frags[0]) ==
				    copy_pending_idx(skb, copy_count(skb) - 1);
	int i, err = 0;

	for (i = 0; i < copy_count(skb); i++) {
		int newerr;

		/* Check status of header. */
		pending_idx = copy_pending_idx(skb, i);

		newerr = (*gopp_copy)->status;
		if (likely(!newerr)) {
			/* The first frag might still have this slot mapped */
			if (i < copy_count(skb) - 1 || !sharedslot)
				xenvif_idx_release(queue, pending_idx,
						   XEN_NETIF_RSP_OKAY);
		} else {
			err = newerr;
			if (net_ratelimit())
				netdev_dbg(queue->vif->dev,
					   "Grant copy of header failed! status: %d pending_idx: %u ref: %u\n",
					   (*gopp_copy)->status,
					   pending_idx,
					   (*gopp_copy)->source.u.ref);
			/* The first frag might still have this slot mapped */
			if (i < copy_count(skb) - 1 || !sharedslot)
				xenvif_idx_release(queue, pending_idx,
						   XEN_NETIF_RSP_ERROR);
		}
		(*gopp_copy)++;
	}

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
				if (i == 0 && !first_shinfo && sharedslot)
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
		first_shinfo = shinfo;
		shinfo = skb_shinfo(shinfo->frag_list);
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
	if (timer_pending(&queue->credit_timeout)) {
		queue->rate_limited = true;
		return true;
	}

	/* Passed the point where we can replenish credit? */
	if (time_after_eq64(now, next_credit)) {
		queue->credit_window_start = now;
		tx_add_credit(queue);
	}

	/* Still too big to send right now? Set a callback. */
	if (size > queue->remaining_credit) {
		mod_timer(&queue->credit_timeout,
			  next_credit);
		queue->credit_window_start = next_credit;
		queue->rate_limited = true;

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

		work_to_do = XEN_RING_NR_UNCONSUMED_REQUESTS(&queue->tx);
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

		data_len = (txreq.size > XEN_NETBACK_TX_COPY_LEN) ?
			XEN_NETBACK_TX_COPY_LEN : txreq.size;

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

		if (ret >= XEN_NETBK_LEGACY_SLOTS_MAX - 1 && data_len < txreq.size)
			data_len = txreq.size;

		skb = xenvif_alloc_skb(data_len);
		if (unlikely(skb == NULL)) {
			netdev_dbg(queue->vif->dev,
				   "Can't allocate a skb in start_xmit.\n");
			xenvif_tx_err(queue, &txreq, extra_count, idx);
			break;
		}

		skb_shinfo(skb)->nr_frags = ret;
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
				skb_shinfo(skb)->nr_frags = 0;
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
				skb_shinfo(skb)->nr_frags = 0;
				kfree_skb(skb);
				kfree_skb(nskb);
				break;
			}
		}

		if (extras[XEN_NETIF_EXTRA_TYPE_HASH - 1].type) {
			struct xen_netif_extra_info *extra;
			enum pkt_hash_types type = PKT_HASH_TYPE_NONE;

			extra = &extras[XEN_NETIF_EXTRA_TYPE_HASH - 1];

			switch (extra->u.hash.type) {
			case _XEN_NETIF_CTRL_HASH_TYPE_IPV4:
			case _XEN_NETIF_CTRL_HASH_TYPE_IPV6:
				type = PKT_HASH_TYPE_L3;
				break;

			case _XEN_NETIF_CTRL_HASH_TYPE_IPV4_TCP:
			case _XEN_NETIF_CTRL_HASH_TYPE_IPV6_TCP:
				type = PKT_HASH_TYPE_L4;
				break;

			default:
				break;
			}

			if (type != PKT_HASH_TYPE_NONE)
				skb_set_hash(skb,
					     *(u32 *)extra->u.hash.value,
					     type);
		}

		xenvif_get_requests(queue, skb, &txreq, txfrags, copy_ops,
				    map_ops, frag_overflow, nskb, extra_count,
				    data_len);

		__skb_queue_tail(&queue->tx_queue, skb);

		queue->tx.req_cons = idx;

		if ((*map_ops >= ARRAY_SIZE(queue->tx_map_ops)) ||
		    (*copy_ops >= ARRAY_SIZE(queue->tx_copy_ops)))
			break;
	}

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
				put_page(skb_frag_page(&frags[j]));
			return -ENOMEM;
		}

		if (offset + PAGE_SIZE < skb->len)
			len = PAGE_SIZE;
		else
			len = skb->len - offset;
		if (skb_copy_bits(skb, offset, page_address(page), len))
			BUG();

		offset += len;
		__skb_frag_set_page(&frags[i], page);
		skb_frag_off_set(&frags[i], 0);
		skb_frag_size_set(&frags[i], len);
	}

	/* Release all the original (foreign) frags. */
	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
		skb_frag_unref(skb, f);
	uarg = skb_shinfo(skb)->destructor_arg;
	/* increase inflight counter to offset decrement in callback */
	atomic_inc(&queue->inflight_packets);
	uarg->callback(NULL, uarg, true);
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

		pending_idx = copy_pending_idx(skb, 0);
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

		if (txp->flags & XEN_NETTXF_csum_blank)
			skb->ip_summed = CHECKSUM_PARTIAL;
		else if (txp->flags & XEN_NETTXF_data_validated)
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		xenvif_fill_frags(queue, skb);

		if (unlikely(skb_has_frag_list(skb))) {
			struct sk_buff *nskb = skb_shinfo(skb)->frag_list;
			xenvif_skb_zerocopy_prepare(queue, nskb);
			if (xenvif_handle_frag_list(queue, skb)) {
				if (net_ratelimit())
					netdev_err(queue->vif->dev,
						   "Not enough memory to consolidate frag_list!\n");
				xenvif_skb_zerocopy_prepare(queue, skb);
				kfree_skb(skb);
				continue;
			}
			/* Copied all the bits from the frag list -- free it. */
			skb_frag_list_init(skb);
			kfree_skb(nskb);
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

		skb_probe_transport_header(skb);

		/* If the packet is GSO then we will have just set up the
		 * transport header offset in checksum_setup so it's now
		 * straightforward to calculate gso_segs.
		 */
		if (skb_is_gso(skb)) {
			int mss, hdrlen;

			/* GSO implies having the L4 header. */
			WARN_ON_ONCE(!skb_transport_header_was_set(skb));
			if (unlikely(!skb_transport_header_was_set(skb))) {
				kfree_skb(skb);
				continue;
			}

			mss = skb_shinfo(skb)->gso_size;
			hdrlen = skb_tcp_all_headers(skb);

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

void xenvif_zerocopy_callback(struct sk_buff *skb, struct ubuf_info *ubuf_base,
			      bool zerocopy_success)
{
	unsigned long flags;
	pending_ring_idx_t index;
	struct ubuf_info_msgzc *ubuf = uarg_to_msgzc(ubuf_base);
	struct xenvif_queue *queue = ubuf_to_queue(ubuf);

	/* This is the only place where we grab this lock, to protect callbacks
	 * from each other.
	 */
	spin_lock_irqsave(&queue->callback_lock, flags);
	do {
		u16 pending_idx = ubuf->desc;
		ubuf = (struct ubuf_info_msgzc *) ubuf->ctx;
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
	unsigned nr_mops = 0, nr_cops = 0;
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
		if (ret) {
			unsigned int i;

			netdev_err(queue->vif->dev, "Map fail: nr %u ret %d\n",
				   nr_mops, ret);
			for (i = 0; i < nr_mops; ++i)
				WARN_ON_ONCE(queue->tx_map_ops[i].status ==
				             GNTST_okay);
		}
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

static void xenvif_idx_unmap(struct xenvif_queue *queue, u16 pending_idx)
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
	RING_IDX rsp_prod, req_prod;
	int err;

	err = xenbus_map_ring_valloc(xenvif_to_xenbus_device(queue->vif),
				     &tx_ring_ref, 1, &addr);
	if (err)
		goto err;

	txs = (struct xen_netif_tx_sring *)addr;
	rsp_prod = READ_ONCE(txs->rsp_prod);
	req_prod = READ_ONCE(txs->req_prod);

	BACK_RING_ATTACH(&queue->tx, txs, rsp_prod, XEN_PAGE_SIZE);

	err = -EIO;
	if (req_prod - rsp_prod > RING_SIZE(&queue->tx))
		goto err;

	err = xenbus_map_ring_valloc(xenvif_to_xenbus_device(queue->vif),
				     &rx_ring_ref, 1, &addr);
	if (err)
		goto err;

	rxs = (struct xen_netif_rx_sring *)addr;
	rsp_prod = READ_ONCE(rxs->rsp_prod);
	req_prod = READ_ONCE(rxs->req_prod);

	BACK_RING_ATTACH(&queue->rx, rxs, rsp_prod, XEN_PAGE_SIZE);

	err = -EIO;
	if (req_prod - rsp_prod > RING_SIZE(&queue->rx))
		goto err;

	return 0;

err:
	xenvif_unmap_frontend_data_rings(queue);
	return err;
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
		return true;

	return false;
}

irqreturn_t xenvif_ctrl_irq_fn(int irq, void *data)
{
	struct xenvif *vif = data;
	unsigned int eoi_flag = XEN_EOI_FLAG_SPURIOUS;

	while (xenvif_ctrl_work_todo(vif)) {
		xenvif_ctrl_action(vif);
		eoi_flag = 0;
	}

	xen_irq_lateeoi(irq, eoi_flag);

	return IRQ_HANDLED;
}

static int __init netback_init(void)
{
	int rc = 0;

	if (!xen_domain())
		return -ENODEV;

	/* Allow as many queues as there are CPUs but max. 8 if user has not
	 * specified a value.
	 */
	if (xenvif_max_queues == 0)
		xenvif_max_queues = min_t(unsigned int, MAX_QUEUES_DEFAULT,
					  num_online_cpus());

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
#endif /* CONFIG_DEBUG_FS */

	return 0;

failed_init:
	return rc;
}

module_init(netback_init);

static void __exit netback_fini(void)
{
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(xen_netback_dbg_root);
#endif /* CONFIG_DEBUG_FS */
	xenvif_xenbus_fini();
}
module_exit(netback_fini);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("xen-backend:vif");
