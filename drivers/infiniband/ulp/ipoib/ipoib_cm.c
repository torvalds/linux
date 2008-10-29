/*
 * Copyright (c) 2006 Mellanox Technologies. All rights reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <rdma/ib_cm.h>
#include <rdma/ib_cache.h>
#include <net/dst.h>
#include <net/icmp.h>
#include <linux/icmpv6.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>

#include "ipoib.h"

int ipoib_max_conn_qp = 128;

module_param_named(max_nonsrq_conn_qp, ipoib_max_conn_qp, int, 0444);
MODULE_PARM_DESC(max_nonsrq_conn_qp,
		 "Max number of connected-mode QPs per interface "
		 "(applied only if shared receive queue is not available)");

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG_DATA
static int data_debug_level;

module_param_named(cm_data_debug_level, data_debug_level, int, 0644);
MODULE_PARM_DESC(cm_data_debug_level,
		 "Enable data path debug tracing for connected mode if > 0");
#endif

#define IPOIB_CM_IETF_ID 0x1000000000000000ULL

#define IPOIB_CM_RX_UPDATE_TIME (256 * HZ)
#define IPOIB_CM_RX_TIMEOUT     (2 * 256 * HZ)
#define IPOIB_CM_RX_DELAY       (3 * 256 * HZ)
#define IPOIB_CM_RX_UPDATE_MASK (0x3)

static struct ib_qp_attr ipoib_cm_err_attr = {
	.qp_state = IB_QPS_ERR
};

#define IPOIB_CM_RX_DRAIN_WRID 0xffffffff

static struct ib_send_wr ipoib_cm_rx_drain_wr = {
	.wr_id = IPOIB_CM_RX_DRAIN_WRID,
	.opcode = IB_WR_SEND,
};

static int ipoib_cm_tx_handler(struct ib_cm_id *cm_id,
			       struct ib_cm_event *event);

static void ipoib_cm_dma_unmap_rx(struct ipoib_dev_priv *priv, int frags,
				  u64 mapping[IPOIB_CM_RX_SG])
{
	int i;

	ib_dma_unmap_single(priv->ca, mapping[0], IPOIB_CM_HEAD_SIZE, DMA_FROM_DEVICE);

	for (i = 0; i < frags; ++i)
		ib_dma_unmap_single(priv->ca, mapping[i + 1], PAGE_SIZE, DMA_FROM_DEVICE);
}

static int ipoib_cm_post_receive_srq(struct net_device *dev, int id)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_recv_wr *bad_wr;
	int i, ret;

	priv->cm.rx_wr.wr_id = id | IPOIB_OP_CM | IPOIB_OP_RECV;

	for (i = 0; i < priv->cm.num_frags; ++i)
		priv->cm.rx_sge[i].addr = priv->cm.srq_ring[id].mapping[i];

	ret = ib_post_srq_recv(priv->cm.srq, &priv->cm.rx_wr, &bad_wr);
	if (unlikely(ret)) {
		ipoib_warn(priv, "post srq failed for buf %d (%d)\n", id, ret);
		ipoib_cm_dma_unmap_rx(priv, priv->cm.num_frags - 1,
				      priv->cm.srq_ring[id].mapping);
		dev_kfree_skb_any(priv->cm.srq_ring[id].skb);
		priv->cm.srq_ring[id].skb = NULL;
	}

	return ret;
}

static int ipoib_cm_post_receive_nonsrq(struct net_device *dev,
					struct ipoib_cm_rx *rx,
					struct ib_recv_wr *wr,
					struct ib_sge *sge, int id)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_recv_wr *bad_wr;
	int i, ret;

	wr->wr_id = id | IPOIB_OP_CM | IPOIB_OP_RECV;

	for (i = 0; i < IPOIB_CM_RX_SG; ++i)
		sge[i].addr = rx->rx_ring[id].mapping[i];

	ret = ib_post_recv(rx->qp, wr, &bad_wr);
	if (unlikely(ret)) {
		ipoib_warn(priv, "post recv failed for buf %d (%d)\n", id, ret);
		ipoib_cm_dma_unmap_rx(priv, IPOIB_CM_RX_SG - 1,
				      rx->rx_ring[id].mapping);
		dev_kfree_skb_any(rx->rx_ring[id].skb);
		rx->rx_ring[id].skb = NULL;
	}

	return ret;
}

static struct sk_buff *ipoib_cm_alloc_rx_skb(struct net_device *dev,
					     struct ipoib_cm_rx_buf *rx_ring,
					     int id, int frags,
					     u64 mapping[IPOIB_CM_RX_SG])
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct sk_buff *skb;
	int i;

	skb = dev_alloc_skb(IPOIB_CM_HEAD_SIZE + 12);
	if (unlikely(!skb))
		return NULL;

	/*
	 * IPoIB adds a 4 byte header. So we need 12 more bytes to align the
	 * IP header to a multiple of 16.
	 */
	skb_reserve(skb, 12);

	mapping[0] = ib_dma_map_single(priv->ca, skb->data, IPOIB_CM_HEAD_SIZE,
				       DMA_FROM_DEVICE);
	if (unlikely(ib_dma_mapping_error(priv->ca, mapping[0]))) {
		dev_kfree_skb_any(skb);
		return NULL;
	}

	for (i = 0; i < frags; i++) {
		struct page *page = alloc_page(GFP_ATOMIC);

		if (!page)
			goto partial_error;
		skb_fill_page_desc(skb, i, page, 0, PAGE_SIZE);

		mapping[i + 1] = ib_dma_map_page(priv->ca, skb_shinfo(skb)->frags[i].page,
						 0, PAGE_SIZE, DMA_FROM_DEVICE);
		if (unlikely(ib_dma_mapping_error(priv->ca, mapping[i + 1])))
			goto partial_error;
	}

	rx_ring[id].skb = skb;
	return skb;

partial_error:

	ib_dma_unmap_single(priv->ca, mapping[0], IPOIB_CM_HEAD_SIZE, DMA_FROM_DEVICE);

	for (; i > 0; --i)
		ib_dma_unmap_single(priv->ca, mapping[i], PAGE_SIZE, DMA_FROM_DEVICE);

	dev_kfree_skb_any(skb);
	return NULL;
}

static void ipoib_cm_free_rx_ring(struct net_device *dev,
				  struct ipoib_cm_rx_buf *rx_ring)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int i;

	for (i = 0; i < ipoib_recvq_size; ++i)
		if (rx_ring[i].skb) {
			ipoib_cm_dma_unmap_rx(priv, IPOIB_CM_RX_SG - 1,
					      rx_ring[i].mapping);
			dev_kfree_skb_any(rx_ring[i].skb);
		}

	vfree(rx_ring);
}

static void ipoib_cm_start_rx_drain(struct ipoib_dev_priv *priv)
{
	struct ib_send_wr *bad_wr;
	struct ipoib_cm_rx *p;

	/* We only reserved 1 extra slot in CQ for drain WRs, so
	 * make sure we have at most 1 outstanding WR. */
	if (list_empty(&priv->cm.rx_flush_list) ||
	    !list_empty(&priv->cm.rx_drain_list))
		return;

	/*
	 * QPs on flush list are error state.  This way, a "flush
	 * error" WC will be immediately generated for each WR we post.
	 */
	p = list_entry(priv->cm.rx_flush_list.next, typeof(*p), list);
	if (ib_post_send(p->qp, &ipoib_cm_rx_drain_wr, &bad_wr))
		ipoib_warn(priv, "failed to post drain wr\n");

	list_splice_init(&priv->cm.rx_flush_list, &priv->cm.rx_drain_list);
}

static void ipoib_cm_rx_event_handler(struct ib_event *event, void *ctx)
{
	struct ipoib_cm_rx *p = ctx;
	struct ipoib_dev_priv *priv = netdev_priv(p->dev);
	unsigned long flags;

	if (event->event != IB_EVENT_QP_LAST_WQE_REACHED)
		return;

	spin_lock_irqsave(&priv->lock, flags);
	list_move(&p->list, &priv->cm.rx_flush_list);
	p->state = IPOIB_CM_RX_FLUSH;
	ipoib_cm_start_rx_drain(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static struct ib_qp *ipoib_cm_create_rx_qp(struct net_device *dev,
					   struct ipoib_cm_rx *p)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_qp_init_attr attr = {
		.event_handler = ipoib_cm_rx_event_handler,
		.send_cq = priv->recv_cq, /* For drain WR */
		.recv_cq = priv->recv_cq,
		.srq = priv->cm.srq,
		.cap.max_send_wr = 1, /* For drain WR */
		.cap.max_send_sge = 1, /* FIXME: 0 Seems not to work */
		.sq_sig_type = IB_SIGNAL_ALL_WR,
		.qp_type = IB_QPT_RC,
		.qp_context = p,
	};

	if (!ipoib_cm_has_srq(dev)) {
		attr.cap.max_recv_wr  = ipoib_recvq_size;
		attr.cap.max_recv_sge = IPOIB_CM_RX_SG;
	}

	return ib_create_qp(priv->pd, &attr);
}

static int ipoib_cm_modify_rx_qp(struct net_device *dev,
				 struct ib_cm_id *cm_id, struct ib_qp *qp,
				 unsigned psn)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;

	qp_attr.qp_state = IB_QPS_INIT;
	ret = ib_cm_init_qp_attr(cm_id, &qp_attr, &qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to init QP attr for INIT: %d\n", ret);
		return ret;
	}
	ret = ib_modify_qp(qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to INIT: %d\n", ret);
		return ret;
	}
	qp_attr.qp_state = IB_QPS_RTR;
	ret = ib_cm_init_qp_attr(cm_id, &qp_attr, &qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to init QP attr for RTR: %d\n", ret);
		return ret;
	}
	qp_attr.rq_psn = psn;
	ret = ib_modify_qp(qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTR: %d\n", ret);
		return ret;
	}

	/*
	 * Current Mellanox HCA firmware won't generate completions
	 * with error for drain WRs unless the QP has been moved to
	 * RTS first. This work-around leaves a window where a QP has
	 * moved to error asynchronously, but this will eventually get
	 * fixed in firmware, so let's not error out if modify QP
	 * fails.
	 */
	qp_attr.qp_state = IB_QPS_RTS;
	ret = ib_cm_init_qp_attr(cm_id, &qp_attr, &qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to init QP attr for RTS: %d\n", ret);
		return 0;
	}
	ret = ib_modify_qp(qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTS: %d\n", ret);
		return 0;
	}

	return 0;
}

static void ipoib_cm_init_rx_wr(struct net_device *dev,
				struct ib_recv_wr *wr,
				struct ib_sge *sge)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int i;

	for (i = 0; i < priv->cm.num_frags; ++i)
		sge[i].lkey = priv->mr->lkey;

	sge[0].length = IPOIB_CM_HEAD_SIZE;
	for (i = 1; i < priv->cm.num_frags; ++i)
		sge[i].length = PAGE_SIZE;

	wr->next    = NULL;
	wr->sg_list = sge;
	wr->num_sge = priv->cm.num_frags;
}

static int ipoib_cm_nonsrq_init_rx(struct net_device *dev, struct ib_cm_id *cm_id,
				   struct ipoib_cm_rx *rx)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct {
		struct ib_recv_wr wr;
		struct ib_sge sge[IPOIB_CM_RX_SG];
	} *t;
	int ret;
	int i;

	rx->rx_ring = vmalloc(ipoib_recvq_size * sizeof *rx->rx_ring);
	if (!rx->rx_ring) {
		printk(KERN_WARNING "%s: failed to allocate CM non-SRQ ring (%d entries)\n",
		       priv->ca->name, ipoib_recvq_size);
		return -ENOMEM;
	}

	memset(rx->rx_ring, 0, ipoib_recvq_size * sizeof *rx->rx_ring);

	t = kmalloc(sizeof *t, GFP_KERNEL);
	if (!t) {
		ret = -ENOMEM;
		goto err_free;
	}

	ipoib_cm_init_rx_wr(dev, &t->wr, t->sge);

	spin_lock_irq(&priv->lock);

	if (priv->cm.nonsrq_conn_qp >= ipoib_max_conn_qp) {
		spin_unlock_irq(&priv->lock);
		ib_send_cm_rej(cm_id, IB_CM_REJ_NO_QP, NULL, 0, NULL, 0);
		ret = -EINVAL;
		goto err_free;
	} else
		++priv->cm.nonsrq_conn_qp;

	spin_unlock_irq(&priv->lock);

	for (i = 0; i < ipoib_recvq_size; ++i) {
		if (!ipoib_cm_alloc_rx_skb(dev, rx->rx_ring, i, IPOIB_CM_RX_SG - 1,
					   rx->rx_ring[i].mapping)) {
			ipoib_warn(priv, "failed to allocate receive buffer %d\n", i);
				ret = -ENOMEM;
				goto err_count;
		}
		ret = ipoib_cm_post_receive_nonsrq(dev, rx, &t->wr, t->sge, i);
		if (ret) {
			ipoib_warn(priv, "ipoib_cm_post_receive_nonsrq "
				   "failed for buf %d\n", i);
			ret = -EIO;
			goto err_count;
		}
	}

	rx->recv_count = ipoib_recvq_size;

	kfree(t);

	return 0;

err_count:
	spin_lock_irq(&priv->lock);
	--priv->cm.nonsrq_conn_qp;
	spin_unlock_irq(&priv->lock);

err_free:
	kfree(t);
	ipoib_cm_free_rx_ring(dev, rx->rx_ring);

	return ret;
}

static int ipoib_cm_send_rep(struct net_device *dev, struct ib_cm_id *cm_id,
			     struct ib_qp *qp, struct ib_cm_req_event_param *req,
			     unsigned psn)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_cm_data data = {};
	struct ib_cm_rep_param rep = {};

	data.qpn = cpu_to_be32(priv->qp->qp_num);
	data.mtu = cpu_to_be32(IPOIB_CM_BUF_SIZE);

	rep.private_data = &data;
	rep.private_data_len = sizeof data;
	rep.flow_control = 0;
	rep.rnr_retry_count = req->rnr_retry_count;
	rep.srq = ipoib_cm_has_srq(dev);
	rep.qp_num = qp->qp_num;
	rep.starting_psn = psn;
	return ib_send_cm_rep(cm_id, &rep);
}

static int ipoib_cm_req_handler(struct ib_cm_id *cm_id, struct ib_cm_event *event)
{
	struct net_device *dev = cm_id->context;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_cm_rx *p;
	unsigned psn;
	int ret;

	ipoib_dbg(priv, "REQ arrived\n");
	p = kzalloc(sizeof *p, GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->dev = dev;
	p->id = cm_id;
	cm_id->context = p;
	p->state = IPOIB_CM_RX_LIVE;
	p->jiffies = jiffies;
	INIT_LIST_HEAD(&p->list);

	p->qp = ipoib_cm_create_rx_qp(dev, p);
	if (IS_ERR(p->qp)) {
		ret = PTR_ERR(p->qp);
		goto err_qp;
	}

	psn = random32() & 0xffffff;
	ret = ipoib_cm_modify_rx_qp(dev, cm_id, p->qp, psn);
	if (ret)
		goto err_modify;

	if (!ipoib_cm_has_srq(dev)) {
		ret = ipoib_cm_nonsrq_init_rx(dev, cm_id, p);
		if (ret)
			goto err_modify;
	}

	spin_lock_irq(&priv->lock);
	queue_delayed_work(ipoib_workqueue,
			   &priv->cm.stale_task, IPOIB_CM_RX_DELAY);
	/* Add this entry to passive ids list head, but do not re-add it
	 * if IB_EVENT_QP_LAST_WQE_REACHED has moved it to flush list. */
	p->jiffies = jiffies;
	if (p->state == IPOIB_CM_RX_LIVE)
		list_move(&p->list, &priv->cm.passive_ids);
	spin_unlock_irq(&priv->lock);

	ret = ipoib_cm_send_rep(dev, cm_id, p->qp, &event->param.req_rcvd, psn);
	if (ret) {
		ipoib_warn(priv, "failed to send REP: %d\n", ret);
		if (ib_modify_qp(p->qp, &ipoib_cm_err_attr, IB_QP_STATE))
			ipoib_warn(priv, "unable to move qp to error state\n");
	}
	return 0;

err_modify:
	ib_destroy_qp(p->qp);
err_qp:
	kfree(p);
	return ret;
}

static int ipoib_cm_rx_handler(struct ib_cm_id *cm_id,
			       struct ib_cm_event *event)
{
	struct ipoib_cm_rx *p;
	struct ipoib_dev_priv *priv;

	switch (event->event) {
	case IB_CM_REQ_RECEIVED:
		return ipoib_cm_req_handler(cm_id, event);
	case IB_CM_DREQ_RECEIVED:
		p = cm_id->context;
		ib_send_cm_drep(cm_id, NULL, 0);
		/* Fall through */
	case IB_CM_REJ_RECEIVED:
		p = cm_id->context;
		priv = netdev_priv(p->dev);
		if (ib_modify_qp(p->qp, &ipoib_cm_err_attr, IB_QP_STATE))
			ipoib_warn(priv, "unable to move qp to error state\n");
		/* Fall through */
	default:
		return 0;
	}
}
/* Adjust length of skb with fragments to match received data */
static void skb_put_frags(struct sk_buff *skb, unsigned int hdr_space,
			  unsigned int length, struct sk_buff *toskb)
{
	int i, num_frags;
	unsigned int size;

	/* put header into skb */
	size = min(length, hdr_space);
	skb->tail += size;
	skb->len += size;
	length -= size;

	num_frags = skb_shinfo(skb)->nr_frags;
	for (i = 0; i < num_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		if (length == 0) {
			/* don't need this page */
			skb_fill_page_desc(toskb, i, frag->page, 0, PAGE_SIZE);
			--skb_shinfo(skb)->nr_frags;
		} else {
			size = min(length, (unsigned) PAGE_SIZE);

			frag->size = size;
			skb->data_len += size;
			skb->truesize += size;
			skb->len += size;
			length -= size;
		}
	}
}

void ipoib_cm_handle_rx_wc(struct net_device *dev, struct ib_wc *wc)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_cm_rx_buf *rx_ring;
	unsigned int wr_id = wc->wr_id & ~(IPOIB_OP_CM | IPOIB_OP_RECV);
	struct sk_buff *skb, *newskb;
	struct ipoib_cm_rx *p;
	unsigned long flags;
	u64 mapping[IPOIB_CM_RX_SG];
	int frags;
	int has_srq;
	struct sk_buff *small_skb;

	ipoib_dbg_data(priv, "cm recv completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= ipoib_recvq_size)) {
		if (wr_id == (IPOIB_CM_RX_DRAIN_WRID & ~(IPOIB_OP_CM | IPOIB_OP_RECV))) {
			spin_lock_irqsave(&priv->lock, flags);
			list_splice_init(&priv->cm.rx_drain_list, &priv->cm.rx_reap_list);
			ipoib_cm_start_rx_drain(priv);
			queue_work(ipoib_workqueue, &priv->cm.rx_reap_task);
			spin_unlock_irqrestore(&priv->lock, flags);
		} else
			ipoib_warn(priv, "cm recv completion event with wrid %d (> %d)\n",
				   wr_id, ipoib_recvq_size);
		return;
	}

	p = wc->qp->qp_context;

	has_srq = ipoib_cm_has_srq(dev);
	rx_ring = has_srq ? priv->cm.srq_ring : p->rx_ring;

	skb = rx_ring[wr_id].skb;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		ipoib_dbg(priv, "cm recv error "
			   "(status=%d, wrid=%d vend_err %x)\n",
			   wc->status, wr_id, wc->vendor_err);
		++dev->stats.rx_dropped;
		if (has_srq)
			goto repost;
		else {
			if (!--p->recv_count) {
				spin_lock_irqsave(&priv->lock, flags);
				list_move(&p->list, &priv->cm.rx_reap_list);
				spin_unlock_irqrestore(&priv->lock, flags);
				queue_work(ipoib_workqueue, &priv->cm.rx_reap_task);
			}
			return;
		}
	}

	if (unlikely(!(wr_id & IPOIB_CM_RX_UPDATE_MASK))) {
		if (p && time_after_eq(jiffies, p->jiffies + IPOIB_CM_RX_UPDATE_TIME)) {
			spin_lock_irqsave(&priv->lock, flags);
			p->jiffies = jiffies;
			/* Move this entry to list head, but do not re-add it
			 * if it has been moved out of list. */
			if (p->state == IPOIB_CM_RX_LIVE)
				list_move(&p->list, &priv->cm.passive_ids);
			spin_unlock_irqrestore(&priv->lock, flags);
		}
	}

	if (wc->byte_len < IPOIB_CM_COPYBREAK) {
		int dlen = wc->byte_len;

		small_skb = dev_alloc_skb(dlen + 12);
		if (small_skb) {
			skb_reserve(small_skb, 12);
			ib_dma_sync_single_for_cpu(priv->ca, rx_ring[wr_id].mapping[0],
						   dlen, DMA_FROM_DEVICE);
			skb_copy_from_linear_data(skb, small_skb->data, dlen);
			ib_dma_sync_single_for_device(priv->ca, rx_ring[wr_id].mapping[0],
						      dlen, DMA_FROM_DEVICE);
			skb_put(small_skb, dlen);
			skb = small_skb;
			goto copied;
		}
	}

	frags = PAGE_ALIGN(wc->byte_len - min(wc->byte_len,
					      (unsigned)IPOIB_CM_HEAD_SIZE)) / PAGE_SIZE;

	newskb = ipoib_cm_alloc_rx_skb(dev, rx_ring, wr_id, frags, mapping);
	if (unlikely(!newskb)) {
		/*
		 * If we can't allocate a new RX buffer, dump
		 * this packet and reuse the old buffer.
		 */
		ipoib_dbg(priv, "failed to allocate receive buffer %d\n", wr_id);
		++dev->stats.rx_dropped;
		goto repost;
	}

	ipoib_cm_dma_unmap_rx(priv, frags, rx_ring[wr_id].mapping);
	memcpy(rx_ring[wr_id].mapping, mapping, (frags + 1) * sizeof *mapping);

	ipoib_dbg_data(priv, "received %d bytes, SLID 0x%04x\n",
		       wc->byte_len, wc->slid);

	skb_put_frags(skb, IPOIB_CM_HEAD_SIZE, wc->byte_len, newskb);

copied:
	skb->protocol = ((struct ipoib_header *) skb->data)->proto;
	skb_reset_mac_header(skb);
	skb_pull(skb, IPOIB_ENCAP_LEN);

	dev->last_rx = jiffies;
	++dev->stats.rx_packets;
	dev->stats.rx_bytes += skb->len;

	skb->dev = dev;
	/* XXX get correct PACKET_ type here */
	skb->pkt_type = PACKET_HOST;
	netif_receive_skb(skb);

repost:
	if (has_srq) {
		if (unlikely(ipoib_cm_post_receive_srq(dev, wr_id)))
			ipoib_warn(priv, "ipoib_cm_post_receive_srq failed "
				   "for buf %d\n", wr_id);
	} else {
		if (unlikely(ipoib_cm_post_receive_nonsrq(dev, p,
							  &priv->cm.rx_wr,
							  priv->cm.rx_sge,
							  wr_id))) {
			--p->recv_count;
			ipoib_warn(priv, "ipoib_cm_post_receive_nonsrq failed "
				   "for buf %d\n", wr_id);
		}
	}
}

static inline int post_send(struct ipoib_dev_priv *priv,
			    struct ipoib_cm_tx *tx,
			    unsigned int wr_id,
			    u64 addr, int len)
{
	struct ib_send_wr *bad_wr;

	priv->tx_sge[0].addr          = addr;
	priv->tx_sge[0].length        = len;

	priv->tx_wr.num_sge	= 1;
	priv->tx_wr.wr_id	= wr_id | IPOIB_OP_CM;

	return ib_post_send(tx->qp, &priv->tx_wr, &bad_wr);
}

void ipoib_cm_send(struct net_device *dev, struct sk_buff *skb, struct ipoib_cm_tx *tx)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_cm_tx_buf *tx_req;
	u64 addr;

	if (unlikely(skb->len > tx->mtu)) {
		ipoib_warn(priv, "packet len %d (> %d) too long to send, dropping\n",
			   skb->len, tx->mtu);
		++dev->stats.tx_dropped;
		++dev->stats.tx_errors;
		ipoib_cm_skb_too_long(dev, skb, tx->mtu - IPOIB_ENCAP_LEN);
		return;
	}

	ipoib_dbg_data(priv, "sending packet: head 0x%x length %d connection 0x%x\n",
		       tx->tx_head, skb->len, tx->qp->qp_num);

	/*
	 * We put the skb into the tx_ring _before_ we call post_send()
	 * because it's entirely possible that the completion handler will
	 * run before we execute anything after the post_send().  That
	 * means we have to make sure everything is properly recorded and
	 * our state is consistent before we call post_send().
	 */
	tx_req = &tx->tx_ring[tx->tx_head & (ipoib_sendq_size - 1)];
	tx_req->skb = skb;
	addr = ib_dma_map_single(priv->ca, skb->data, skb->len, DMA_TO_DEVICE);
	if (unlikely(ib_dma_mapping_error(priv->ca, addr))) {
		++dev->stats.tx_errors;
		dev_kfree_skb_any(skb);
		return;
	}

	tx_req->mapping = addr;

	if (unlikely(post_send(priv, tx, tx->tx_head & (ipoib_sendq_size - 1),
			       addr, skb->len))) {
		ipoib_warn(priv, "post_send failed\n");
		++dev->stats.tx_errors;
		ib_dma_unmap_single(priv->ca, addr, skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
	} else {
		dev->trans_start = jiffies;
		++tx->tx_head;

		if (++priv->tx_outstanding == ipoib_sendq_size) {
			ipoib_dbg(priv, "TX ring 0x%x full, stopping kernel net queue\n",
				  tx->qp->qp_num);
			netif_stop_queue(dev);
		}
	}
}

void ipoib_cm_handle_tx_wc(struct net_device *dev, struct ib_wc *wc)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_cm_tx *tx = wc->qp->qp_context;
	unsigned int wr_id = wc->wr_id & ~IPOIB_OP_CM;
	struct ipoib_cm_tx_buf *tx_req;
	unsigned long flags;

	ipoib_dbg_data(priv, "cm send completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= ipoib_sendq_size)) {
		ipoib_warn(priv, "cm send completion event with wrid %d (> %d)\n",
			   wr_id, ipoib_sendq_size);
		return;
	}

	tx_req = &tx->tx_ring[wr_id];

	ib_dma_unmap_single(priv->ca, tx_req->mapping, tx_req->skb->len, DMA_TO_DEVICE);

	/* FIXME: is this right? Shouldn't we only increment on success? */
	++dev->stats.tx_packets;
	dev->stats.tx_bytes += tx_req->skb->len;

	dev_kfree_skb_any(tx_req->skb);

	netif_tx_lock(dev);

	++tx->tx_tail;
	if (unlikely(--priv->tx_outstanding == ipoib_sendq_size >> 1) &&
	    netif_queue_stopped(dev) &&
	    test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
		netif_wake_queue(dev);

	if (wc->status != IB_WC_SUCCESS &&
	    wc->status != IB_WC_WR_FLUSH_ERR) {
		struct ipoib_neigh *neigh;

		ipoib_dbg(priv, "failed cm send event "
			   "(status=%d, wrid=%d vend_err %x)\n",
			   wc->status, wr_id, wc->vendor_err);

		spin_lock_irqsave(&priv->lock, flags);
		neigh = tx->neigh;

		if (neigh) {
			neigh->cm = NULL;
			list_del(&neigh->list);
			if (neigh->ah)
				ipoib_put_ah(neigh->ah);
			ipoib_neigh_free(dev, neigh);

			tx->neigh = NULL;
		}

		if (test_and_clear_bit(IPOIB_FLAG_INITIALIZED, &tx->flags)) {
			list_move(&tx->list, &priv->cm.reap_list);
			queue_work(ipoib_workqueue, &priv->cm.reap_task);
		}

		clear_bit(IPOIB_FLAG_OPER_UP, &tx->flags);

		spin_unlock_irqrestore(&priv->lock, flags);
	}

	netif_tx_unlock(dev);
}

int ipoib_cm_dev_open(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int ret;

	if (!IPOIB_CM_SUPPORTED(dev->dev_addr))
		return 0;

	priv->cm.id = ib_create_cm_id(priv->ca, ipoib_cm_rx_handler, dev);
	if (IS_ERR(priv->cm.id)) {
		printk(KERN_WARNING "%s: failed to create CM ID\n", priv->ca->name);
		ret = PTR_ERR(priv->cm.id);
		goto err_cm;
	}

	ret = ib_cm_listen(priv->cm.id, cpu_to_be64(IPOIB_CM_IETF_ID | priv->qp->qp_num),
			   0, NULL);
	if (ret) {
		printk(KERN_WARNING "%s: failed to listen on ID 0x%llx\n", priv->ca->name,
		       IPOIB_CM_IETF_ID | priv->qp->qp_num);
		goto err_listen;
	}

	return 0;

err_listen:
	ib_destroy_cm_id(priv->cm.id);
err_cm:
	priv->cm.id = NULL;
	return ret;
}

static void ipoib_cm_free_rx_reap_list(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_cm_rx *rx, *n;
	LIST_HEAD(list);

	spin_lock_irq(&priv->lock);
	list_splice_init(&priv->cm.rx_reap_list, &list);
	spin_unlock_irq(&priv->lock);

	list_for_each_entry_safe(rx, n, &list, list) {
		ib_destroy_cm_id(rx->id);
		ib_destroy_qp(rx->qp);
		if (!ipoib_cm_has_srq(dev)) {
			ipoib_cm_free_rx_ring(priv->dev, rx->rx_ring);
			spin_lock_irq(&priv->lock);
			--priv->cm.nonsrq_conn_qp;
			spin_unlock_irq(&priv->lock);
		}
		kfree(rx);
	}
}

void ipoib_cm_dev_stop(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_cm_rx *p;
	unsigned long begin;
	int ret;

	if (!IPOIB_CM_SUPPORTED(dev->dev_addr) || !priv->cm.id)
		return;

	ib_destroy_cm_id(priv->cm.id);
	priv->cm.id = NULL;

	spin_lock_irq(&priv->lock);
	while (!list_empty(&priv->cm.passive_ids)) {
		p = list_entry(priv->cm.passive_ids.next, typeof(*p), list);
		list_move(&p->list, &priv->cm.rx_error_list);
		p->state = IPOIB_CM_RX_ERROR;
		spin_unlock_irq(&priv->lock);
		ret = ib_modify_qp(p->qp, &ipoib_cm_err_attr, IB_QP_STATE);
		if (ret)
			ipoib_warn(priv, "unable to move qp to error state: %d\n", ret);
		spin_lock_irq(&priv->lock);
	}

	/* Wait for all RX to be drained */
	begin = jiffies;

	while (!list_empty(&priv->cm.rx_error_list) ||
	       !list_empty(&priv->cm.rx_flush_list) ||
	       !list_empty(&priv->cm.rx_drain_list)) {
		if (time_after(jiffies, begin + 5 * HZ)) {
			ipoib_warn(priv, "RX drain timing out\n");

			/*
			 * assume the HW is wedged and just free up everything.
			 */
			list_splice_init(&priv->cm.rx_flush_list,
					 &priv->cm.rx_reap_list);
			list_splice_init(&priv->cm.rx_error_list,
					 &priv->cm.rx_reap_list);
			list_splice_init(&priv->cm.rx_drain_list,
					 &priv->cm.rx_reap_list);
			break;
		}
		spin_unlock_irq(&priv->lock);
		msleep(1);
		ipoib_drain_cq(dev);
		spin_lock_irq(&priv->lock);
	}

	spin_unlock_irq(&priv->lock);

	ipoib_cm_free_rx_reap_list(dev);

	cancel_delayed_work(&priv->cm.stale_task);
}

static int ipoib_cm_rep_handler(struct ib_cm_id *cm_id, struct ib_cm_event *event)
{
	struct ipoib_cm_tx *p = cm_id->context;
	struct ipoib_dev_priv *priv = netdev_priv(p->dev);
	struct ipoib_cm_data *data = event->private_data;
	struct sk_buff_head skqueue;
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;
	struct sk_buff *skb;

	p->mtu = be32_to_cpu(data->mtu);

	if (p->mtu <= IPOIB_ENCAP_LEN) {
		ipoib_warn(priv, "Rejecting connection: mtu %d <= %d\n",
			   p->mtu, IPOIB_ENCAP_LEN);
		return -EINVAL;
	}

	qp_attr.qp_state = IB_QPS_RTR;
	ret = ib_cm_init_qp_attr(cm_id, &qp_attr, &qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to init QP attr for RTR: %d\n", ret);
		return ret;
	}

	qp_attr.rq_psn = 0 /* FIXME */;
	ret = ib_modify_qp(p->qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTR: %d\n", ret);
		return ret;
	}

	qp_attr.qp_state = IB_QPS_RTS;
	ret = ib_cm_init_qp_attr(cm_id, &qp_attr, &qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to init QP attr for RTS: %d\n", ret);
		return ret;
	}
	ret = ib_modify_qp(p->qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTS: %d\n", ret);
		return ret;
	}

	skb_queue_head_init(&skqueue);

	spin_lock_irq(&priv->lock);
	set_bit(IPOIB_FLAG_OPER_UP, &p->flags);
	if (p->neigh)
		while ((skb = __skb_dequeue(&p->neigh->queue)))
			__skb_queue_tail(&skqueue, skb);
	spin_unlock_irq(&priv->lock);

	while ((skb = __skb_dequeue(&skqueue))) {
		skb->dev = p->dev;
		if (dev_queue_xmit(skb))
			ipoib_warn(priv, "dev_queue_xmit failed "
				   "to requeue packet\n");
	}

	ret = ib_send_cm_rtu(cm_id, NULL, 0);
	if (ret) {
		ipoib_warn(priv, "failed to send RTU: %d\n", ret);
		return ret;
	}
	return 0;
}

static struct ib_qp *ipoib_cm_create_tx_qp(struct net_device *dev, struct ipoib_cm_tx *tx)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_qp_init_attr attr = {
		.send_cq		= priv->recv_cq,
		.recv_cq		= priv->recv_cq,
		.srq			= priv->cm.srq,
		.cap.max_send_wr	= ipoib_sendq_size,
		.cap.max_send_sge	= 1,
		.sq_sig_type		= IB_SIGNAL_ALL_WR,
		.qp_type		= IB_QPT_RC,
		.qp_context		= tx
	};

	return ib_create_qp(priv->pd, &attr);
}

static int ipoib_cm_send_req(struct net_device *dev,
			     struct ib_cm_id *id, struct ib_qp *qp,
			     u32 qpn,
			     struct ib_sa_path_rec *pathrec)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_cm_data data = {};
	struct ib_cm_req_param req = {};

	data.qpn = cpu_to_be32(priv->qp->qp_num);
	data.mtu = cpu_to_be32(IPOIB_CM_BUF_SIZE);

	req.primary_path		= pathrec;
	req.alternate_path		= NULL;
	req.service_id			= cpu_to_be64(IPOIB_CM_IETF_ID | qpn);
	req.qp_num			= qp->qp_num;
	req.qp_type			= qp->qp_type;
	req.private_data		= &data;
	req.private_data_len		= sizeof data;
	req.flow_control		= 0;

	req.starting_psn		= 0; /* FIXME */

	/*
	 * Pick some arbitrary defaults here; we could make these
	 * module parameters if anyone cared about setting them.
	 */
	req.responder_resources		= 4;
	req.remote_cm_response_timeout	= 20;
	req.local_cm_response_timeout	= 20;
	req.retry_count			= 0; /* RFC draft warns against retries */
	req.rnr_retry_count		= 0; /* RFC draft warns against retries */
	req.max_cm_retries		= 15;
	req.srq				= ipoib_cm_has_srq(dev);
	return ib_send_cm_req(id, &req);
}

static int ipoib_cm_modify_tx_init(struct net_device *dev,
				  struct ib_cm_id *cm_id, struct ib_qp *qp)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;
	ret = ib_find_pkey(priv->ca, priv->port, priv->pkey, &qp_attr.pkey_index);
	if (ret) {
		ipoib_warn(priv, "pkey 0x%x not found: %d\n", priv->pkey, ret);
		return ret;
	}

	qp_attr.qp_state = IB_QPS_INIT;
	qp_attr.qp_access_flags = IB_ACCESS_LOCAL_WRITE;
	qp_attr.port_num = priv->port;
	qp_attr_mask = IB_QP_STATE | IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX | IB_QP_PORT;

	ret = ib_modify_qp(qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify tx QP to INIT: %d\n", ret);
		return ret;
	}
	return 0;
}

static int ipoib_cm_tx_init(struct ipoib_cm_tx *p, u32 qpn,
			    struct ib_sa_path_rec *pathrec)
{
	struct ipoib_dev_priv *priv = netdev_priv(p->dev);
	int ret;

	p->tx_ring = vmalloc(ipoib_sendq_size * sizeof *p->tx_ring);
	if (!p->tx_ring) {
		ipoib_warn(priv, "failed to allocate tx ring\n");
		ret = -ENOMEM;
		goto err_tx;
	}
	memset(p->tx_ring, 0, ipoib_sendq_size * sizeof *p->tx_ring);

	p->qp = ipoib_cm_create_tx_qp(p->dev, p);
	if (IS_ERR(p->qp)) {
		ret = PTR_ERR(p->qp);
		ipoib_warn(priv, "failed to allocate tx qp: %d\n", ret);
		goto err_qp;
	}

	p->id = ib_create_cm_id(priv->ca, ipoib_cm_tx_handler, p);
	if (IS_ERR(p->id)) {
		ret = PTR_ERR(p->id);
		ipoib_warn(priv, "failed to create tx cm id: %d\n", ret);
		goto err_id;
	}

	ret = ipoib_cm_modify_tx_init(p->dev, p->id,  p->qp);
	if (ret) {
		ipoib_warn(priv, "failed to modify tx qp to rtr: %d\n", ret);
		goto err_modify;
	}

	ret = ipoib_cm_send_req(p->dev, p->id, p->qp, qpn, pathrec);
	if (ret) {
		ipoib_warn(priv, "failed to send cm req: %d\n", ret);
		goto err_send_cm;
	}

	ipoib_dbg(priv, "Request connection 0x%x for gid %pI6 qpn 0x%x\n",
		  p->qp->qp_num, pathrec->dgid.raw, qpn);

	return 0;

err_send_cm:
err_modify:
	ib_destroy_cm_id(p->id);
err_id:
	p->id = NULL;
	ib_destroy_qp(p->qp);
err_qp:
	p->qp = NULL;
	vfree(p->tx_ring);
err_tx:
	return ret;
}

static void ipoib_cm_tx_destroy(struct ipoib_cm_tx *p)
{
	struct ipoib_dev_priv *priv = netdev_priv(p->dev);
	struct ipoib_cm_tx_buf *tx_req;
	unsigned long begin;

	ipoib_dbg(priv, "Destroy active connection 0x%x head 0x%x tail 0x%x\n",
		  p->qp ? p->qp->qp_num : 0, p->tx_head, p->tx_tail);

	if (p->id)
		ib_destroy_cm_id(p->id);

	if (p->tx_ring) {
		/* Wait for all sends to complete */
		begin = jiffies;
		while ((int) p->tx_tail - (int) p->tx_head < 0) {
			if (time_after(jiffies, begin + 5 * HZ)) {
				ipoib_warn(priv, "timing out; %d sends not completed\n",
					   p->tx_head - p->tx_tail);
				goto timeout;
			}

			msleep(1);
		}
	}

timeout:

	while ((int) p->tx_tail - (int) p->tx_head < 0) {
		tx_req = &p->tx_ring[p->tx_tail & (ipoib_sendq_size - 1)];
		ib_dma_unmap_single(priv->ca, tx_req->mapping, tx_req->skb->len,
				    DMA_TO_DEVICE);
		dev_kfree_skb_any(tx_req->skb);
		++p->tx_tail;
		netif_tx_lock_bh(p->dev);
		if (unlikely(--priv->tx_outstanding == ipoib_sendq_size >> 1) &&
		    netif_queue_stopped(p->dev) &&
		    test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
			netif_wake_queue(p->dev);
		netif_tx_unlock_bh(p->dev);
	}

	if (p->qp)
		ib_destroy_qp(p->qp);

	vfree(p->tx_ring);
	kfree(p);
}

static int ipoib_cm_tx_handler(struct ib_cm_id *cm_id,
			       struct ib_cm_event *event)
{
	struct ipoib_cm_tx *tx = cm_id->context;
	struct ipoib_dev_priv *priv = netdev_priv(tx->dev);
	struct net_device *dev = priv->dev;
	struct ipoib_neigh *neigh;
	unsigned long flags;
	int ret;

	switch (event->event) {
	case IB_CM_DREQ_RECEIVED:
		ipoib_dbg(priv, "DREQ received.\n");
		ib_send_cm_drep(cm_id, NULL, 0);
		break;
	case IB_CM_REP_RECEIVED:
		ipoib_dbg(priv, "REP received.\n");
		ret = ipoib_cm_rep_handler(cm_id, event);
		if (ret)
			ib_send_cm_rej(cm_id, IB_CM_REJ_CONSUMER_DEFINED,
				       NULL, 0, NULL, 0);
		break;
	case IB_CM_REQ_ERROR:
	case IB_CM_REJ_RECEIVED:
	case IB_CM_TIMEWAIT_EXIT:
		ipoib_dbg(priv, "CM error %d.\n", event->event);
		netif_tx_lock_bh(dev);
		spin_lock_irqsave(&priv->lock, flags);
		neigh = tx->neigh;

		if (neigh) {
			neigh->cm = NULL;
			list_del(&neigh->list);
			if (neigh->ah)
				ipoib_put_ah(neigh->ah);
			ipoib_neigh_free(dev, neigh);

			tx->neigh = NULL;
		}

		if (test_and_clear_bit(IPOIB_FLAG_INITIALIZED, &tx->flags)) {
			list_move(&tx->list, &priv->cm.reap_list);
			queue_work(ipoib_workqueue, &priv->cm.reap_task);
		}

		spin_unlock_irqrestore(&priv->lock, flags);
		netif_tx_unlock_bh(dev);
		break;
	default:
		break;
	}

	return 0;
}

struct ipoib_cm_tx *ipoib_cm_create_tx(struct net_device *dev, struct ipoib_path *path,
				       struct ipoib_neigh *neigh)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_cm_tx *tx;

	tx = kzalloc(sizeof *tx, GFP_ATOMIC);
	if (!tx)
		return NULL;

	neigh->cm = tx;
	tx->neigh = neigh;
	tx->path = path;
	tx->dev = dev;
	list_add(&tx->list, &priv->cm.start_list);
	set_bit(IPOIB_FLAG_INITIALIZED, &tx->flags);
	queue_work(ipoib_workqueue, &priv->cm.start_task);
	return tx;
}

void ipoib_cm_destroy_tx(struct ipoib_cm_tx *tx)
{
	struct ipoib_dev_priv *priv = netdev_priv(tx->dev);
	if (test_and_clear_bit(IPOIB_FLAG_INITIALIZED, &tx->flags)) {
		list_move(&tx->list, &priv->cm.reap_list);
		queue_work(ipoib_workqueue, &priv->cm.reap_task);
		ipoib_dbg(priv, "Reap connection for gid %pI6\n",
			  tx->neigh->dgid.raw);
		tx->neigh = NULL;
	}
}

static void ipoib_cm_tx_start(struct work_struct *work)
{
	struct ipoib_dev_priv *priv = container_of(work, struct ipoib_dev_priv,
						   cm.start_task);
	struct net_device *dev = priv->dev;
	struct ipoib_neigh *neigh;
	struct ipoib_cm_tx *p;
	unsigned long flags;
	int ret;

	struct ib_sa_path_rec pathrec;
	u32 qpn;

	netif_tx_lock_bh(dev);
	spin_lock_irqsave(&priv->lock, flags);

	while (!list_empty(&priv->cm.start_list)) {
		p = list_entry(priv->cm.start_list.next, typeof(*p), list);
		list_del_init(&p->list);
		neigh = p->neigh;
		qpn = IPOIB_QPN(neigh->neighbour->ha);
		memcpy(&pathrec, &p->path->pathrec, sizeof pathrec);

		spin_unlock_irqrestore(&priv->lock, flags);
		netif_tx_unlock_bh(dev);

		ret = ipoib_cm_tx_init(p, qpn, &pathrec);

		netif_tx_lock_bh(dev);
		spin_lock_irqsave(&priv->lock, flags);

		if (ret) {
			neigh = p->neigh;
			if (neigh) {
				neigh->cm = NULL;
				list_del(&neigh->list);
				if (neigh->ah)
					ipoib_put_ah(neigh->ah);
				ipoib_neigh_free(dev, neigh);
			}
			list_del(&p->list);
			kfree(p);
		}
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	netif_tx_unlock_bh(dev);
}

static void ipoib_cm_tx_reap(struct work_struct *work)
{
	struct ipoib_dev_priv *priv = container_of(work, struct ipoib_dev_priv,
						   cm.reap_task);
	struct net_device *dev = priv->dev;
	struct ipoib_cm_tx *p;
	unsigned long flags;

	netif_tx_lock_bh(dev);
	spin_lock_irqsave(&priv->lock, flags);

	while (!list_empty(&priv->cm.reap_list)) {
		p = list_entry(priv->cm.reap_list.next, typeof(*p), list);
		list_del(&p->list);
		spin_unlock_irqrestore(&priv->lock, flags);
		netif_tx_unlock_bh(dev);
		ipoib_cm_tx_destroy(p);
		netif_tx_lock_bh(dev);
		spin_lock_irqsave(&priv->lock, flags);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	netif_tx_unlock_bh(dev);
}

static void ipoib_cm_skb_reap(struct work_struct *work)
{
	struct ipoib_dev_priv *priv = container_of(work, struct ipoib_dev_priv,
						   cm.skb_task);
	struct net_device *dev = priv->dev;
	struct sk_buff *skb;
	unsigned long flags;
	unsigned mtu = priv->mcast_mtu;

	netif_tx_lock_bh(dev);
	spin_lock_irqsave(&priv->lock, flags);

	while ((skb = skb_dequeue(&priv->cm.skb_queue))) {
		spin_unlock_irqrestore(&priv->lock, flags);
		netif_tx_unlock_bh(dev);

		if (skb->protocol == htons(ETH_P_IP))
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		else if (skb->protocol == htons(ETH_P_IPV6))
			icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, priv->dev);
#endif
		dev_kfree_skb_any(skb);

		netif_tx_lock_bh(dev);
		spin_lock_irqsave(&priv->lock, flags);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	netif_tx_unlock_bh(dev);
}

void ipoib_cm_skb_too_long(struct net_device *dev, struct sk_buff *skb,
			   unsigned int mtu)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int e = skb_queue_empty(&priv->cm.skb_queue);

	if (skb->dst)
		skb->dst->ops->update_pmtu(skb->dst, mtu);

	skb_queue_tail(&priv->cm.skb_queue, skb);
	if (e)
		queue_work(ipoib_workqueue, &priv->cm.skb_task);
}

static void ipoib_cm_rx_reap(struct work_struct *work)
{
	ipoib_cm_free_rx_reap_list(container_of(work, struct ipoib_dev_priv,
						cm.rx_reap_task)->dev);
}

static void ipoib_cm_stale_task(struct work_struct *work)
{
	struct ipoib_dev_priv *priv = container_of(work, struct ipoib_dev_priv,
						   cm.stale_task.work);
	struct ipoib_cm_rx *p;
	int ret;

	spin_lock_irq(&priv->lock);
	while (!list_empty(&priv->cm.passive_ids)) {
		/* List is sorted by LRU, start from tail,
		 * stop when we see a recently used entry */
		p = list_entry(priv->cm.passive_ids.prev, typeof(*p), list);
		if (time_before_eq(jiffies, p->jiffies + IPOIB_CM_RX_TIMEOUT))
			break;
		list_move(&p->list, &priv->cm.rx_error_list);
		p->state = IPOIB_CM_RX_ERROR;
		spin_unlock_irq(&priv->lock);
		ret = ib_modify_qp(p->qp, &ipoib_cm_err_attr, IB_QP_STATE);
		if (ret)
			ipoib_warn(priv, "unable to move qp to error state: %d\n", ret);
		spin_lock_irq(&priv->lock);
	}

	if (!list_empty(&priv->cm.passive_ids))
		queue_delayed_work(ipoib_workqueue,
				   &priv->cm.stale_task, IPOIB_CM_RX_DELAY);
	spin_unlock_irq(&priv->lock);
}


static ssize_t show_mode(struct device *d, struct device_attribute *attr,
			 char *buf)
{
	struct ipoib_dev_priv *priv = netdev_priv(to_net_dev(d));

	if (test_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags))
		return sprintf(buf, "connected\n");
	else
		return sprintf(buf, "datagram\n");
}

static ssize_t set_mode(struct device *d, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(d);
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	/* flush paths if we switch modes so that connections are restarted */
	if (IPOIB_CM_SUPPORTED(dev->dev_addr) && !strcmp(buf, "connected\n")) {
		set_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags);
		ipoib_warn(priv, "enabling connected mode "
			   "will cause multicast packet drops\n");

		rtnl_lock();
		dev->features &= ~(NETIF_F_IP_CSUM | NETIF_F_SG | NETIF_F_TSO);
		rtnl_unlock();
		priv->tx_wr.send_flags &= ~IB_SEND_IP_CSUM;

		ipoib_flush_paths(dev);
		return count;
	}

	if (!strcmp(buf, "datagram\n")) {
		clear_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags);

		rtnl_lock();
		if (test_bit(IPOIB_FLAG_CSUM, &priv->flags)) {
			dev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
			if (priv->hca_caps & IB_DEVICE_UD_TSO)
				dev->features |= NETIF_F_TSO;
		}
		dev_set_mtu(dev, min(priv->mcast_mtu, dev->mtu));
		rtnl_unlock();
		ipoib_flush_paths(dev);

		return count;
	}

	return -EINVAL;
}

static DEVICE_ATTR(mode, S_IWUSR | S_IRUGO, show_mode, set_mode);

int ipoib_cm_add_mode_attr(struct net_device *dev)
{
	return device_create_file(&dev->dev, &dev_attr_mode);
}

static void ipoib_cm_create_srq(struct net_device *dev, int max_sge)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_srq_init_attr srq_init_attr = {
		.attr = {
			.max_wr  = ipoib_recvq_size,
			.max_sge = max_sge
		}
	};

	priv->cm.srq = ib_create_srq(priv->pd, &srq_init_attr);
	if (IS_ERR(priv->cm.srq)) {
		if (PTR_ERR(priv->cm.srq) != -ENOSYS)
			printk(KERN_WARNING "%s: failed to allocate SRQ, error %ld\n",
			       priv->ca->name, PTR_ERR(priv->cm.srq));
		priv->cm.srq = NULL;
		return;
	}

	priv->cm.srq_ring = vmalloc(ipoib_recvq_size * sizeof *priv->cm.srq_ring);
	if (!priv->cm.srq_ring) {
		printk(KERN_WARNING "%s: failed to allocate CM SRQ ring (%d entries)\n",
		       priv->ca->name, ipoib_recvq_size);
		ib_destroy_srq(priv->cm.srq);
		priv->cm.srq = NULL;
		return;
	}

	memset(priv->cm.srq_ring, 0, ipoib_recvq_size * sizeof *priv->cm.srq_ring);
}

int ipoib_cm_dev_init(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int i, ret;
	struct ib_device_attr attr;

	INIT_LIST_HEAD(&priv->cm.passive_ids);
	INIT_LIST_HEAD(&priv->cm.reap_list);
	INIT_LIST_HEAD(&priv->cm.start_list);
	INIT_LIST_HEAD(&priv->cm.rx_error_list);
	INIT_LIST_HEAD(&priv->cm.rx_flush_list);
	INIT_LIST_HEAD(&priv->cm.rx_drain_list);
	INIT_LIST_HEAD(&priv->cm.rx_reap_list);
	INIT_WORK(&priv->cm.start_task, ipoib_cm_tx_start);
	INIT_WORK(&priv->cm.reap_task, ipoib_cm_tx_reap);
	INIT_WORK(&priv->cm.skb_task, ipoib_cm_skb_reap);
	INIT_WORK(&priv->cm.rx_reap_task, ipoib_cm_rx_reap);
	INIT_DELAYED_WORK(&priv->cm.stale_task, ipoib_cm_stale_task);

	skb_queue_head_init(&priv->cm.skb_queue);

	ret = ib_query_device(priv->ca, &attr);
	if (ret) {
		printk(KERN_WARNING "ib_query_device() failed with %d\n", ret);
		return ret;
	}

	ipoib_dbg(priv, "max_srq_sge=%d\n", attr.max_srq_sge);

	attr.max_srq_sge = min_t(int, IPOIB_CM_RX_SG, attr.max_srq_sge);
	ipoib_cm_create_srq(dev, attr.max_srq_sge);
	if (ipoib_cm_has_srq(dev)) {
		priv->cm.max_cm_mtu = attr.max_srq_sge * PAGE_SIZE - 0x10;
		priv->cm.num_frags  = attr.max_srq_sge;
		ipoib_dbg(priv, "max_cm_mtu = 0x%x, num_frags=%d\n",
			  priv->cm.max_cm_mtu, priv->cm.num_frags);
	} else {
		priv->cm.max_cm_mtu = IPOIB_CM_MTU;
		priv->cm.num_frags  = IPOIB_CM_RX_SG;
	}

	ipoib_cm_init_rx_wr(dev, &priv->cm.rx_wr, priv->cm.rx_sge);

	if (ipoib_cm_has_srq(dev)) {
		for (i = 0; i < ipoib_recvq_size; ++i) {
			if (!ipoib_cm_alloc_rx_skb(dev, priv->cm.srq_ring, i,
						   priv->cm.num_frags - 1,
						   priv->cm.srq_ring[i].mapping)) {
				ipoib_warn(priv, "failed to allocate "
					   "receive buffer %d\n", i);
				ipoib_cm_dev_cleanup(dev);
				return -ENOMEM;
			}

			if (ipoib_cm_post_receive_srq(dev, i)) {
				ipoib_warn(priv, "ipoib_cm_post_receive_srq "
					   "failed for buf %d\n", i);
				ipoib_cm_dev_cleanup(dev);
				return -EIO;
			}
		}
	}

	priv->dev->dev_addr[0] = IPOIB_FLAGS_RC;
	return 0;
}

void ipoib_cm_dev_cleanup(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int ret;

	if (!priv->cm.srq)
		return;

	ipoib_dbg(priv, "Cleanup ipoib connected mode.\n");

	ret = ib_destroy_srq(priv->cm.srq);
	if (ret)
		ipoib_warn(priv, "ib_destroy_srq failed: %d\n", ret);

	priv->cm.srq = NULL;
	if (!priv->cm.srq_ring)
		return;

	ipoib_cm_free_rx_ring(dev, priv->cm.srq_ring);
	priv->cm.srq_ring = NULL;
}
