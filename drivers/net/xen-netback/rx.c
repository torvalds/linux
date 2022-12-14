/*
 * Copyright (c) 2016 Citrix Systems Inc.
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

#include <xen/xen.h>
#include <xen/events.h>

/*
 * Update the needed ring page slots for the first SKB queued.
 * Note that any call sequence outside the RX thread calling this function
 * needs to wake up the RX thread via a call of xenvif_kick_thread()
 * afterwards in order to avoid a race with putting the thread to sleep.
 */
static void xenvif_update_needed_slots(struct xenvif_queue *queue,
				       const struct sk_buff *skb)
{
	unsigned int needed = 0;

	if (skb) {
		needed = DIV_ROUND_UP(skb->len, XEN_PAGE_SIZE);
		if (skb_is_gso(skb))
			needed++;
		if (skb->sw_hash)
			needed++;
	}

	WRITE_ONCE(queue->rx_slots_needed, needed);
}

static bool xenvif_rx_ring_slots_available(struct xenvif_queue *queue)
{
	RING_IDX prod, cons;
	unsigned int needed;

	needed = READ_ONCE(queue->rx_slots_needed);
	if (!needed)
		return false;

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

bool xenvif_rx_queue_tail(struct xenvif_queue *queue, struct sk_buff *skb)
{
	unsigned long flags;
	bool ret = true;

	spin_lock_irqsave(&queue->rx_queue.lock, flags);

	if (queue->rx_queue_len >= queue->rx_queue_max) {
		struct net_device *dev = queue->vif->dev;

		netif_tx_stop_queue(netdev_get_tx_queue(dev, queue->id));
		ret = false;
	} else {
		if (skb_queue_empty(&queue->rx_queue))
			xenvif_update_needed_slots(queue, skb);

		__skb_queue_tail(&queue->rx_queue, skb);

		queue->rx_queue_len += skb->len;
	}

	spin_unlock_irqrestore(&queue->rx_queue.lock, flags);

	return ret;
}

static struct sk_buff *xenvif_rx_dequeue(struct xenvif_queue *queue)
{
	struct sk_buff *skb;

	spin_lock_irq(&queue->rx_queue.lock);

	skb = __skb_dequeue(&queue->rx_queue);
	if (skb) {
		xenvif_update_needed_slots(queue, skb_peek(&queue->rx_queue));

		queue->rx_queue_len -= skb->len;
		if (queue->rx_queue_len < queue->rx_queue_max) {
			struct netdev_queue *txq;

			txq = netdev_get_tx_queue(queue->vif->dev, queue->id);
			netif_tx_wake_queue(txq);
		}
	}

	spin_unlock_irq(&queue->rx_queue.lock);

	return skb;
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

	for (;;) {
		skb = skb_peek(&queue->rx_queue);
		if (!skb)
			break;
		if (time_before(jiffies, XENVIF_RX_CB(skb)->expires))
			break;
		xenvif_rx_dequeue(queue);
		kfree_skb(skb);
		queue->vif->dev->stats.rx_dropped++;
	}
}

static void xenvif_rx_copy_flush(struct xenvif_queue *queue)
{
	unsigned int i;
	int notify;

	gnttab_batch_copy(queue->rx_copy.op, queue->rx_copy.num);

	for (i = 0; i < queue->rx_copy.num; i++) {
		struct gnttab_copy *op;

		op = &queue->rx_copy.op[i];

		/* If the copy failed, overwrite the status field in
		 * the corresponding response.
		 */
		if (unlikely(op->status != GNTST_okay)) {
			struct xen_netif_rx_response *rsp;

			rsp = RING_GET_RESPONSE(&queue->rx,
						queue->rx_copy.idx[i]);
			rsp->status = op->status;
		}
	}

	queue->rx_copy.num = 0;

	/* Push responses for all completed packets. */
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&queue->rx, notify);
	if (notify)
		notify_remote_via_irq(queue->rx_irq);

	__skb_queue_purge(queue->rx_copy.completed);
}

static void xenvif_rx_copy_add(struct xenvif_queue *queue,
			       struct xen_netif_rx_request *req,
			       unsigned int offset, void *data, size_t len)
{
	struct gnttab_copy *op;
	struct page *page;
	struct xen_page_foreign *foreign;

	if (queue->rx_copy.num == COPY_BATCH_SIZE)
		xenvif_rx_copy_flush(queue);

	op = &queue->rx_copy.op[queue->rx_copy.num];

	page = virt_to_page(data);

	op->flags = GNTCOPY_dest_gref;

	foreign = xen_page_foreign(page);
	if (foreign) {
		op->source.domid = foreign->domid;
		op->source.u.ref = foreign->gref;
		op->flags |= GNTCOPY_source_gref;
	} else {
		op->source.u.gmfn = virt_to_gfn(data);
		op->source.domid  = DOMID_SELF;
	}

	op->source.offset = xen_offset_in_page(data);
	op->dest.u.ref    = req->gref;
	op->dest.domid    = queue->vif->domid;
	op->dest.offset   = offset;
	op->len           = len;

	queue->rx_copy.idx[queue->rx_copy.num] = queue->rx.req_cons;
	queue->rx_copy.num++;
}

static unsigned int xenvif_gso_type(struct sk_buff *skb)
{
	if (skb_is_gso(skb)) {
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4)
			return XEN_NETIF_GSO_TYPE_TCPV4;
		else
			return XEN_NETIF_GSO_TYPE_TCPV6;
	}
	return XEN_NETIF_GSO_TYPE_NONE;
}

struct xenvif_pkt_state {
	struct sk_buff *skb;
	size_t remaining_len;
	struct sk_buff *frag_iter;
	int frag; /* frag == -1 => frag_iter->head */
	unsigned int frag_offset;
	struct xen_netif_extra_info extras[XEN_NETIF_EXTRA_TYPE_MAX - 1];
	unsigned int extra_count;
	unsigned int slot;
};

static void xenvif_rx_next_skb(struct xenvif_queue *queue,
			       struct xenvif_pkt_state *pkt)
{
	struct sk_buff *skb;
	unsigned int gso_type;

	skb = xenvif_rx_dequeue(queue);

	queue->stats.tx_bytes += skb->len;
	queue->stats.tx_packets++;

	/* Reset packet state. */
	memset(pkt, 0, sizeof(struct xenvif_pkt_state));

	pkt->skb = skb;
	pkt->frag_iter = skb;
	pkt->remaining_len = skb->len;
	pkt->frag = -1;

	gso_type = xenvif_gso_type(skb);
	if ((1 << gso_type) & queue->vif->gso_mask) {
		struct xen_netif_extra_info *extra;

		extra = &pkt->extras[XEN_NETIF_EXTRA_TYPE_GSO - 1];

		extra->u.gso.type = gso_type;
		extra->u.gso.size = skb_shinfo(skb)->gso_size;
		extra->u.gso.pad = 0;
		extra->u.gso.features = 0;
		extra->type = XEN_NETIF_EXTRA_TYPE_GSO;
		extra->flags = 0;

		pkt->extra_count++;
	}

	if (queue->vif->xdp_headroom) {
		struct xen_netif_extra_info *extra;

		extra = &pkt->extras[XEN_NETIF_EXTRA_TYPE_XDP - 1];

		memset(extra, 0, sizeof(struct xen_netif_extra_info));
		extra->u.xdp.headroom = queue->vif->xdp_headroom;
		extra->type = XEN_NETIF_EXTRA_TYPE_XDP;
		extra->flags = 0;

		pkt->extra_count++;
	}

	if (skb->sw_hash) {
		struct xen_netif_extra_info *extra;

		extra = &pkt->extras[XEN_NETIF_EXTRA_TYPE_HASH - 1];

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

		*(uint32_t *)extra->u.hash.value = skb_get_hash_raw(skb);

		extra->type = XEN_NETIF_EXTRA_TYPE_HASH;
		extra->flags = 0;

		pkt->extra_count++;
	}
}

static void xenvif_rx_complete(struct xenvif_queue *queue,
			       struct xenvif_pkt_state *pkt)
{
	/* All responses are ready to be pushed. */
	queue->rx.rsp_prod_pvt = queue->rx.req_cons;

	__skb_queue_tail(queue->rx_copy.completed, pkt->skb);
}

static void xenvif_rx_next_frag(struct xenvif_pkt_state *pkt)
{
	struct sk_buff *frag_iter = pkt->frag_iter;
	unsigned int nr_frags = skb_shinfo(frag_iter)->nr_frags;

	pkt->frag++;
	pkt->frag_offset = 0;

	if (pkt->frag >= nr_frags) {
		if (frag_iter == pkt->skb)
			pkt->frag_iter = skb_shinfo(frag_iter)->frag_list;
		else
			pkt->frag_iter = frag_iter->next;

		pkt->frag = -1;
	}
}

static void xenvif_rx_next_chunk(struct xenvif_queue *queue,
				 struct xenvif_pkt_state *pkt,
				 unsigned int offset, void **data,
				 size_t *len)
{
	struct sk_buff *frag_iter = pkt->frag_iter;
	void *frag_data;
	size_t frag_len, chunk_len;

	BUG_ON(!frag_iter);

	if (pkt->frag == -1) {
		frag_data = frag_iter->data;
		frag_len = skb_headlen(frag_iter);
	} else {
		skb_frag_t *frag = &skb_shinfo(frag_iter)->frags[pkt->frag];

		frag_data = skb_frag_address(frag);
		frag_len = skb_frag_size(frag);
	}

	frag_data += pkt->frag_offset;
	frag_len -= pkt->frag_offset;

	chunk_len = min_t(size_t, frag_len, XEN_PAGE_SIZE - offset);
	chunk_len = min_t(size_t, chunk_len, XEN_PAGE_SIZE -
					     xen_offset_in_page(frag_data));

	pkt->frag_offset += chunk_len;

	/* Advance to next frag? */
	if (frag_len == chunk_len)
		xenvif_rx_next_frag(pkt);

	*data = frag_data;
	*len = chunk_len;
}

static void xenvif_rx_data_slot(struct xenvif_queue *queue,
				struct xenvif_pkt_state *pkt,
				struct xen_netif_rx_request *req,
				struct xen_netif_rx_response *rsp)
{
	unsigned int offset = queue->vif->xdp_headroom;
	unsigned int flags;

	do {
		size_t len;
		void *data;

		xenvif_rx_next_chunk(queue, pkt, offset, &data, &len);
		xenvif_rx_copy_add(queue, req, offset, data, len);

		offset += len;
		pkt->remaining_len -= len;

	} while (offset < XEN_PAGE_SIZE && pkt->remaining_len > 0);

	if (pkt->remaining_len > 0)
		flags = XEN_NETRXF_more_data;
	else
		flags = 0;

	if (pkt->slot == 0) {
		struct sk_buff *skb = pkt->skb;

		if (skb->ip_summed == CHECKSUM_PARTIAL)
			flags |= XEN_NETRXF_csum_blank |
				 XEN_NETRXF_data_validated;
		else if (skb->ip_summed == CHECKSUM_UNNECESSARY)
			flags |= XEN_NETRXF_data_validated;

		if (pkt->extra_count != 0)
			flags |= XEN_NETRXF_extra_info;
	}

	rsp->offset = 0;
	rsp->flags = flags;
	rsp->id = req->id;
	rsp->status = (s16)offset;
}

static void xenvif_rx_extra_slot(struct xenvif_queue *queue,
				 struct xenvif_pkt_state *pkt,
				 struct xen_netif_rx_request *req,
				 struct xen_netif_rx_response *rsp)
{
	struct xen_netif_extra_info *extra = (void *)rsp;
	unsigned int i;

	pkt->extra_count--;

	for (i = 0; i < ARRAY_SIZE(pkt->extras); i++) {
		if (pkt->extras[i].type) {
			*extra = pkt->extras[i];

			if (pkt->extra_count != 0)
				extra->flags |= XEN_NETIF_EXTRA_FLAG_MORE;

			pkt->extras[i].type = 0;
			return;
		}
	}
	BUG();
}

static void xenvif_rx_skb(struct xenvif_queue *queue)
{
	struct xenvif_pkt_state pkt;

	xenvif_rx_next_skb(queue, &pkt);

	queue->last_rx_time = jiffies;

	do {
		struct xen_netif_rx_request *req;
		struct xen_netif_rx_response *rsp;

		req = RING_GET_REQUEST(&queue->rx, queue->rx.req_cons);
		rsp = RING_GET_RESPONSE(&queue->rx, queue->rx.req_cons);

		/* Extras must go after the first data slot */
		if (pkt.slot != 0 && pkt.extra_count != 0)
			xenvif_rx_extra_slot(queue, &pkt, req, rsp);
		else
			xenvif_rx_data_slot(queue, &pkt, req, rsp);

		queue->rx.req_cons++;
		pkt.slot++;
	} while (pkt.remaining_len > 0 || pkt.extra_count != 0);

	xenvif_rx_complete(queue, &pkt);
}

#define RX_BATCH_SIZE 64

static void xenvif_rx_action(struct xenvif_queue *queue)
{
	struct sk_buff_head completed_skbs;
	unsigned int work_done = 0;

	__skb_queue_head_init(&completed_skbs);
	queue->rx_copy.completed = &completed_skbs;

	while (xenvif_rx_ring_slots_available(queue) &&
	       !skb_queue_empty(&queue->rx_queue) &&
	       work_done < RX_BATCH_SIZE) {
		xenvif_rx_skb(queue);
		work_done++;
	}

	/* Flush any pending copies and complete all skbs. */
	xenvif_rx_copy_flush(queue);
}

static RING_IDX xenvif_rx_queue_slots(const struct xenvif_queue *queue)
{
	RING_IDX prod, cons;

	prod = queue->rx.sring->req_prod;
	cons = queue->rx.req_cons;

	return prod - cons;
}

static bool xenvif_rx_queue_stalled(const struct xenvif_queue *queue)
{
	unsigned int needed = READ_ONCE(queue->rx_slots_needed);

	return !queue->stalled &&
		xenvif_rx_queue_slots(queue) < needed &&
		time_after(jiffies,
			   queue->last_rx_time + queue->vif->stall_timeout);
}

static bool xenvif_rx_queue_ready(struct xenvif_queue *queue)
{
	unsigned int needed = READ_ONCE(queue->rx_slots_needed);

	return queue->stalled && xenvif_rx_queue_slots(queue) >= needed;
}

bool xenvif_have_rx_work(struct xenvif_queue *queue, bool test_kthread)
{
	return xenvif_rx_ring_slots_available(queue) ||
		(queue->vif->stall_timeout &&
		 (xenvif_rx_queue_stalled(queue) ||
		  xenvif_rx_queue_ready(queue))) ||
		(test_kthread && kthread_should_stop()) ||
		queue->vif->disabled;
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

	if (xenvif_have_rx_work(queue, true))
		return;

	for (;;) {
		long ret;

		prepare_to_wait(&queue->wq, &wait, TASK_INTERRUPTIBLE);
		if (xenvif_have_rx_work(queue, true))
			break;
		if (atomic_fetch_andnot(NETBK_RX_EOI | NETBK_COMMON_EOI,
					&queue->eoi_pending) &
		    (NETBK_RX_EOI | NETBK_COMMON_EOI))
			xen_irq_lateeoi(queue->rx_irq, 0);

		ret = schedule_timeout(xenvif_rx_queue_timeout(queue));
		if (!ret)
			break;
	}
	finish_wait(&queue->wq, &wait);
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

		cond_resched();
	}

	/* Bin any remaining skbs */
	xenvif_rx_queue_purge(queue);

	return 0;
}
