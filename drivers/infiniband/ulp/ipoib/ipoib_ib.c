/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
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

#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include <linux/ip.h>
#include <linux/tcp.h>

#include "ipoib.h"

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG_DATA
static int data_debug_level;

module_param(data_debug_level, int, 0644);
MODULE_PARM_DESC(data_debug_level,
		 "Enable data path debug tracing if > 0");
#endif

struct ipoib_ah *ipoib_create_ah(struct net_device *dev,
				 struct ib_pd *pd, struct rdma_ah_attr *attr)
{
	struct ipoib_ah *ah;
	struct ib_ah *vah;

	ah = kmalloc(sizeof *ah, GFP_KERNEL);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	ah->dev       = dev;
	ah->last_send = 0;
	kref_init(&ah->ref);

	vah = rdma_create_ah(pd, attr);
	if (IS_ERR(vah)) {
		kfree(ah);
		ah = (struct ipoib_ah *)vah;
	} else {
		ah->ah = vah;
		ipoib_dbg(ipoib_priv(dev), "Created ah %p\n", ah->ah);
	}

	return ah;
}

void ipoib_free_ah(struct kref *kref)
{
	struct ipoib_ah *ah = container_of(kref, struct ipoib_ah, ref);
	struct ipoib_dev_priv *priv = ipoib_priv(ah->dev);

	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	list_add_tail(&ah->list, &priv->dead_ahs);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void ipoib_ud_dma_unmap_rx(struct ipoib_dev_priv *priv,
				  u64 mapping[IPOIB_UD_RX_SG])
{
	ib_dma_unmap_single(priv->ca, mapping[0],
			    IPOIB_UD_BUF_SIZE(priv->max_ib_mtu),
			    DMA_FROM_DEVICE);
}

static int ipoib_ib_post_receive(struct net_device *dev, int id)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ib_recv_wr *bad_wr;
	int ret;

	priv->rx_wr.wr_id   = id | IPOIB_OP_RECV;
	priv->rx_sge[0].addr = priv->rx_ring[id].mapping[0];
	priv->rx_sge[1].addr = priv->rx_ring[id].mapping[1];


	ret = ib_post_recv(priv->qp, &priv->rx_wr, &bad_wr);
	if (unlikely(ret)) {
		ipoib_warn(priv, "receive failed for buf %d (%d)\n", id, ret);
		ipoib_ud_dma_unmap_rx(priv, priv->rx_ring[id].mapping);
		dev_kfree_skb_any(priv->rx_ring[id].skb);
		priv->rx_ring[id].skb = NULL;
	}

	return ret;
}

static struct sk_buff *ipoib_alloc_rx_skb(struct net_device *dev, int id)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct sk_buff *skb;
	int buf_size;
	u64 *mapping;

	buf_size = IPOIB_UD_BUF_SIZE(priv->max_ib_mtu);

	skb = dev_alloc_skb(buf_size + IPOIB_HARD_LEN);
	if (unlikely(!skb))
		return NULL;

	/*
	 * the IP header will be at IPOIP_HARD_LEN + IB_GRH_BYTES, that is
	 * 64 bytes aligned
	 */
	skb_reserve(skb, sizeof(struct ipoib_pseudo_header));

	mapping = priv->rx_ring[id].mapping;
	mapping[0] = ib_dma_map_single(priv->ca, skb->data, buf_size,
				       DMA_FROM_DEVICE);
	if (unlikely(ib_dma_mapping_error(priv->ca, mapping[0])))
		goto error;

	priv->rx_ring[id].skb = skb;
	return skb;
error:
	dev_kfree_skb_any(skb);
	return NULL;
}

static int ipoib_ib_post_receives(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int i;

	for (i = 0; i < ipoib_recvq_size; ++i) {
		if (!ipoib_alloc_rx_skb(dev, i)) {
			ipoib_warn(priv, "failed to allocate receive buffer %d\n", i);
			return -ENOMEM;
		}
		if (ipoib_ib_post_receive(dev, i)) {
			ipoib_warn(priv, "ipoib_ib_post_receive failed for buf %d\n", i);
			return -EIO;
		}
	}

	return 0;
}

static void ipoib_ib_handle_rx_wc(struct net_device *dev, struct ib_wc *wc)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	unsigned int wr_id = wc->wr_id & ~IPOIB_OP_RECV;
	struct sk_buff *skb;
	u64 mapping[IPOIB_UD_RX_SG];
	union ib_gid *dgid;
	union ib_gid *sgid;

	ipoib_dbg_data(priv, "recv completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= ipoib_recvq_size)) {
		ipoib_warn(priv, "recv completion event with wrid %d (> %d)\n",
			   wr_id, ipoib_recvq_size);
		return;
	}

	skb  = priv->rx_ring[wr_id].skb;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		if (wc->status != IB_WC_WR_FLUSH_ERR)
			ipoib_warn(priv, "failed recv event "
				   "(status=%d, wrid=%d vend_err %x)\n",
				   wc->status, wr_id, wc->vendor_err);
		ipoib_ud_dma_unmap_rx(priv, priv->rx_ring[wr_id].mapping);
		dev_kfree_skb_any(skb);
		priv->rx_ring[wr_id].skb = NULL;
		return;
	}

	memcpy(mapping, priv->rx_ring[wr_id].mapping,
	       IPOIB_UD_RX_SG * sizeof *mapping);

	/*
	 * If we can't allocate a new RX buffer, dump
	 * this packet and reuse the old buffer.
	 */
	if (unlikely(!ipoib_alloc_rx_skb(dev, wr_id))) {
		++dev->stats.rx_dropped;
		goto repost;
	}

	ipoib_dbg_data(priv, "received %d bytes, SLID 0x%04x\n",
		       wc->byte_len, wc->slid);

	ipoib_ud_dma_unmap_rx(priv, mapping);

	skb_put(skb, wc->byte_len);

	/* First byte of dgid signals multicast when 0xff */
	dgid = &((struct ib_grh *)skb->data)->dgid;

	if (!(wc->wc_flags & IB_WC_GRH) || dgid->raw[0] != 0xff)
		skb->pkt_type = PACKET_HOST;
	else if (memcmp(dgid, dev->broadcast + 4, sizeof(union ib_gid)) == 0)
		skb->pkt_type = PACKET_BROADCAST;
	else
		skb->pkt_type = PACKET_MULTICAST;

	sgid = &((struct ib_grh *)skb->data)->sgid;

	/*
	 * Drop packets that this interface sent, ie multicast packets
	 * that the HCA has replicated.
	 */
	if (wc->slid == priv->local_lid && wc->src_qp == priv->qp->qp_num) {
		int need_repost = 1;

		if ((wc->wc_flags & IB_WC_GRH) &&
		    sgid->global.interface_id != priv->local_gid.global.interface_id)
			need_repost = 0;

		if (need_repost) {
			dev_kfree_skb_any(skb);
			goto repost;
		}
	}

	skb_pull(skb, IB_GRH_BYTES);

	skb->protocol = ((struct ipoib_header *) skb->data)->proto;
	skb_add_pseudo_hdr(skb);

	++dev->stats.rx_packets;
	dev->stats.rx_bytes += skb->len;
	if (skb->pkt_type == PACKET_MULTICAST)
		dev->stats.multicast++;

	skb->dev = dev;
	if ((dev->features & NETIF_F_RXCSUM) &&
			likely(wc->wc_flags & IB_WC_IP_CSUM_OK))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	napi_gro_receive(&priv->napi, skb);

repost:
	if (unlikely(ipoib_ib_post_receive(dev, wr_id)))
		ipoib_warn(priv, "ipoib_ib_post_receive failed "
			   "for buf %d\n", wr_id);
}

int ipoib_dma_map_tx(struct ib_device *ca, struct ipoib_tx_buf *tx_req)
{
	struct sk_buff *skb = tx_req->skb;
	u64 *mapping = tx_req->mapping;
	int i;
	int off;

	if (skb_headlen(skb)) {
		mapping[0] = ib_dma_map_single(ca, skb->data, skb_headlen(skb),
					       DMA_TO_DEVICE);
		if (unlikely(ib_dma_mapping_error(ca, mapping[0])))
			return -EIO;

		off = 1;
	} else
		off = 0;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; ++i) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		mapping[i + off] = ib_dma_map_page(ca,
						 skb_frag_page(frag),
						 frag->page_offset, skb_frag_size(frag),
						 DMA_TO_DEVICE);
		if (unlikely(ib_dma_mapping_error(ca, mapping[i + off])))
			goto partial_error;
	}
	return 0;

partial_error:
	for (; i > 0; --i) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i - 1];

		ib_dma_unmap_page(ca, mapping[i - !off], skb_frag_size(frag), DMA_TO_DEVICE);
	}

	if (off)
		ib_dma_unmap_single(ca, mapping[0], skb_headlen(skb), DMA_TO_DEVICE);

	return -EIO;
}

void ipoib_dma_unmap_tx(struct ipoib_dev_priv *priv,
			struct ipoib_tx_buf *tx_req)
{
	struct sk_buff *skb = tx_req->skb;
	u64 *mapping = tx_req->mapping;
	int i;
	int off;

	if (skb_headlen(skb)) {
		ib_dma_unmap_single(priv->ca, mapping[0], skb_headlen(skb),
				    DMA_TO_DEVICE);
		off = 1;
	} else
		off = 0;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; ++i) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		ib_dma_unmap_page(priv->ca, mapping[i + off],
				  skb_frag_size(frag), DMA_TO_DEVICE);
	}
}

/*
 * As the result of a completion error the QP Can be transferred to SQE states.
 * The function checks if the (send)QP is in SQE state and
 * moves it back to RTS state, that in order to have it functional again.
 */
static void ipoib_qp_state_validate_work(struct work_struct *work)
{
	struct ipoib_qp_state_validate *qp_work =
		container_of(work, struct ipoib_qp_state_validate, work);

	struct ipoib_dev_priv *priv = qp_work->priv;
	struct ib_qp_attr qp_attr;
	struct ib_qp_init_attr query_init_attr;
	int ret;

	ret = ib_query_qp(priv->qp, &qp_attr, IB_QP_STATE, &query_init_attr);
	if (ret) {
		ipoib_warn(priv, "%s: Failed to query QP ret: %d\n",
			   __func__, ret);
		goto free_res;
	}
	pr_info("%s: QP: 0x%x is in state: %d\n",
		__func__, priv->qp->qp_num, qp_attr.qp_state);

	/* currently support only in SQE->RTS transition*/
	if (qp_attr.qp_state == IB_QPS_SQE) {
		qp_attr.qp_state = IB_QPS_RTS;

		ret = ib_modify_qp(priv->qp, &qp_attr, IB_QP_STATE);
		if (ret) {
			pr_warn("failed(%d) modify QP:0x%x SQE->RTS\n",
				ret, priv->qp->qp_num);
			goto free_res;
		}
		pr_info("%s: QP: 0x%x moved from IB_QPS_SQE to IB_QPS_RTS\n",
			__func__, priv->qp->qp_num);
	} else {
		pr_warn("QP (%d) will stay in state: %d\n",
			priv->qp->qp_num, qp_attr.qp_state);
	}

free_res:
	kfree(qp_work);
}

static void ipoib_ib_handle_tx_wc(struct net_device *dev, struct ib_wc *wc)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	unsigned int wr_id = wc->wr_id;
	struct ipoib_tx_buf *tx_req;

	ipoib_dbg_data(priv, "send completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= ipoib_sendq_size)) {
		ipoib_warn(priv, "send completion event with wrid %d (> %d)\n",
			   wr_id, ipoib_sendq_size);
		return;
	}

	tx_req = &priv->tx_ring[wr_id];

	ipoib_dma_unmap_tx(priv, tx_req);

	++dev->stats.tx_packets;
	dev->stats.tx_bytes += tx_req->skb->len;

	dev_kfree_skb_any(tx_req->skb);

	++priv->tx_tail;
	if (unlikely(--priv->tx_outstanding == ipoib_sendq_size >> 1) &&
	    netif_queue_stopped(dev) &&
	    test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
		netif_wake_queue(dev);

	if (wc->status != IB_WC_SUCCESS &&
	    wc->status != IB_WC_WR_FLUSH_ERR) {
		struct ipoib_qp_state_validate *qp_work;
		ipoib_warn(priv, "failed send event "
			   "(status=%d, wrid=%d vend_err %x)\n",
			   wc->status, wr_id, wc->vendor_err);
		qp_work = kzalloc(sizeof(*qp_work), GFP_ATOMIC);
		if (!qp_work)
			return;

		INIT_WORK(&qp_work->work, ipoib_qp_state_validate_work);
		qp_work->priv = priv;
		queue_work(priv->wq, &qp_work->work);
	}
}

static int poll_tx(struct ipoib_dev_priv *priv)
{
	int n, i;

	n = ib_poll_cq(priv->send_cq, MAX_SEND_CQE, priv->send_wc);
	for (i = 0; i < n; ++i)
		ipoib_ib_handle_tx_wc(priv->dev, priv->send_wc + i);

	return n == MAX_SEND_CQE;
}

int ipoib_poll(struct napi_struct *napi, int budget)
{
	struct ipoib_dev_priv *priv = container_of(napi, struct ipoib_dev_priv, napi);
	struct net_device *dev = priv->dev;
	int done;
	int t;
	int n, i;

	done  = 0;

poll_more:
	while (done < budget) {
		int max = (budget - done);

		t = min(IPOIB_NUM_WC, max);
		n = ib_poll_cq(priv->recv_cq, t, priv->ibwc);

		for (i = 0; i < n; i++) {
			struct ib_wc *wc = priv->ibwc + i;

			if (wc->wr_id & IPOIB_OP_RECV) {
				++done;
				if (wc->wr_id & IPOIB_OP_CM)
					ipoib_cm_handle_rx_wc(dev, wc);
				else
					ipoib_ib_handle_rx_wc(dev, wc);
			} else
				ipoib_cm_handle_tx_wc(priv->dev, wc);
		}

		if (n != t)
			break;
	}

	if (done < budget) {
		napi_complete(napi);
		if (unlikely(ib_req_notify_cq(priv->recv_cq,
					      IB_CQ_NEXT_COMP |
					      IB_CQ_REPORT_MISSED_EVENTS)) &&
		    napi_reschedule(napi))
			goto poll_more;
	}

	return done;
}

void ipoib_ib_completion(struct ib_cq *cq, void *dev_ptr)
{
	struct net_device *dev = dev_ptr;
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	napi_schedule(&priv->napi);
}

static void drain_tx_cq(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	netif_tx_lock(dev);
	while (poll_tx(priv))
		; /* nothing */

	if (netif_queue_stopped(dev))
		mod_timer(&priv->poll_timer, jiffies + 1);

	netif_tx_unlock(dev);
}

void ipoib_send_comp_handler(struct ib_cq *cq, void *dev_ptr)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev_ptr);

	mod_timer(&priv->poll_timer, jiffies);
}

static inline int post_send(struct ipoib_dev_priv *priv,
			    unsigned int wr_id,
			    struct ib_ah *address, u32 dqpn,
			    struct ipoib_tx_buf *tx_req,
			    void *head, int hlen)
{
	struct ib_send_wr *bad_wr;
	struct sk_buff *skb = tx_req->skb;

	ipoib_build_sge(priv, tx_req);

	priv->tx_wr.wr.wr_id	= wr_id;
	priv->tx_wr.remote_qpn	= dqpn;
	priv->tx_wr.ah		= address;

	if (head) {
		priv->tx_wr.mss		= skb_shinfo(skb)->gso_size;
		priv->tx_wr.header	= head;
		priv->tx_wr.hlen	= hlen;
		priv->tx_wr.wr.opcode	= IB_WR_LSO;
	} else
		priv->tx_wr.wr.opcode	= IB_WR_SEND;

	return ib_post_send(priv->qp, &priv->tx_wr.wr, &bad_wr);
}

int ipoib_send(struct net_device *dev, struct sk_buff *skb,
	       struct ib_ah *address, u32 dqpn)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_tx_buf *tx_req;
	int hlen, rc;
	void *phead;
	unsigned usable_sge = priv->max_send_sge - !!skb_headlen(skb);

	if (skb_is_gso(skb)) {
		hlen = skb_transport_offset(skb) + tcp_hdrlen(skb);
		phead = skb->data;
		if (unlikely(!skb_pull(skb, hlen))) {
			ipoib_warn(priv, "linear data too small\n");
			++dev->stats.tx_dropped;
			++dev->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return -1;
		}
	} else {
		if (unlikely(skb->len > priv->mcast_mtu + IPOIB_ENCAP_LEN)) {
			ipoib_warn(priv, "packet len %d (> %d) too long to send, dropping\n",
				   skb->len, priv->mcast_mtu + IPOIB_ENCAP_LEN);
			++dev->stats.tx_dropped;
			++dev->stats.tx_errors;
			ipoib_cm_skb_too_long(dev, skb, priv->mcast_mtu);
			return -1;
		}
		phead = NULL;
		hlen  = 0;
	}
	if (skb_shinfo(skb)->nr_frags > usable_sge) {
		if (skb_linearize(skb) < 0) {
			ipoib_warn(priv, "skb could not be linearized\n");
			++dev->stats.tx_dropped;
			++dev->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return -1;
		}
		/* Does skb_linearize return ok without reducing nr_frags? */
		if (skb_shinfo(skb)->nr_frags > usable_sge) {
			ipoib_warn(priv, "too many frags after skb linearize\n");
			++dev->stats.tx_dropped;
			++dev->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return -1;
		}
	}

	ipoib_dbg_data(priv,
		       "sending packet, length=%d address=%p dqpn=0x%06x\n",
		       skb->len, address, dqpn);

	/*
	 * We put the skb into the tx_ring _before_ we call post_send()
	 * because it's entirely possible that the completion handler will
	 * run before we execute anything after the post_send().  That
	 * means we have to make sure everything is properly recorded and
	 * our state is consistent before we call post_send().
	 */
	tx_req = &priv->tx_ring[priv->tx_head & (ipoib_sendq_size - 1)];
	tx_req->skb = skb;
	if (unlikely(ipoib_dma_map_tx(priv->ca, tx_req))) {
		++dev->stats.tx_errors;
		dev_kfree_skb_any(skb);
		return -1;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		priv->tx_wr.wr.send_flags |= IB_SEND_IP_CSUM;
	else
		priv->tx_wr.wr.send_flags &= ~IB_SEND_IP_CSUM;

	if (++priv->tx_outstanding == ipoib_sendq_size) {
		ipoib_dbg(priv, "TX ring full, stopping kernel net queue\n");
		if (ib_req_notify_cq(priv->send_cq, IB_CQ_NEXT_COMP))
			ipoib_warn(priv, "request notify on send CQ failed\n");
		netif_stop_queue(dev);
	}

	skb_orphan(skb);
	skb_dst_drop(skb);

	rc = post_send(priv, priv->tx_head & (ipoib_sendq_size - 1),
		       address, dqpn, tx_req, phead, hlen);
	if (unlikely(rc)) {
		ipoib_warn(priv, "post_send failed, error %d\n", rc);
		++dev->stats.tx_errors;
		--priv->tx_outstanding;
		ipoib_dma_unmap_tx(priv, tx_req);
		dev_kfree_skb_any(skb);
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
		rc = 0;
	} else {
		netif_trans_update(dev);

		rc = priv->tx_head;
		++priv->tx_head;
	}

	if (unlikely(priv->tx_outstanding > MAX_SEND_CQE))
		while (poll_tx(priv))
			; /* nothing */

	return rc;
}

static void __ipoib_reap_ah(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_ah *ah, *tah;
	LIST_HEAD(remove_list);
	unsigned long flags;

	netif_tx_lock_bh(dev);
	spin_lock_irqsave(&priv->lock, flags);

	list_for_each_entry_safe(ah, tah, &priv->dead_ahs, list)
		if ((int) priv->tx_tail - (int) ah->last_send >= 0) {
			list_del(&ah->list);
			rdma_destroy_ah(ah->ah);
			kfree(ah);
		}

	spin_unlock_irqrestore(&priv->lock, flags);
	netif_tx_unlock_bh(dev);
}

void ipoib_reap_ah(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, ah_reap_task.work);
	struct net_device *dev = priv->dev;

	__ipoib_reap_ah(dev);

	if (!test_bit(IPOIB_STOP_REAPER, &priv->flags))
		queue_delayed_work(priv->wq, &priv->ah_reap_task,
				   round_jiffies_relative(HZ));
}

static void ipoib_flush_ah(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	cancel_delayed_work(&priv->ah_reap_task);
	flush_workqueue(priv->wq);
	ipoib_reap_ah(&priv->ah_reap_task.work);
}

static void ipoib_stop_ah(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	set_bit(IPOIB_STOP_REAPER, &priv->flags);
	ipoib_flush_ah(dev);
}

static int recvs_pending(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int pending = 0;
	int i;

	for (i = 0; i < ipoib_recvq_size; ++i)
		if (priv->rx_ring[i].skb)
			++pending;

	return pending;
}

static void check_qp_movement_and_print(struct ipoib_dev_priv *priv,
					struct ib_qp *qp,
					enum ib_qp_state new_state)
{
	struct ib_qp_attr qp_attr;
	struct ib_qp_init_attr query_init_attr;
	int ret;

	ret = ib_query_qp(qp, &qp_attr, IB_QP_STATE, &query_init_attr);
	if (ret) {
		ipoib_warn(priv, "%s: Failed to query QP\n", __func__);
		return;
	}
	/* print according to the new-state and the previous state.*/
	if (new_state == IB_QPS_ERR && qp_attr.qp_state == IB_QPS_RESET)
		ipoib_dbg(priv, "Failed modify QP, IB_QPS_RESET to IB_QPS_ERR, acceptable\n");
	else
		ipoib_warn(priv, "Failed to modify QP to state: %d from state: %d\n",
			   new_state, qp_attr.qp_state);
}

int ipoib_ib_dev_stop_default(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ib_qp_attr qp_attr;
	unsigned long begin;
	struct ipoib_tx_buf *tx_req;
	int i;

	if (test_bit(IPOIB_FLAG_INITIALIZED, &priv->flags))
		napi_disable(&priv->napi);

	ipoib_cm_dev_stop(dev);

	/*
	 * Move our QP to the error state and then reinitialize in
	 * when all work requests have completed or have been flushed.
	 */
	qp_attr.qp_state = IB_QPS_ERR;
	if (ib_modify_qp(priv->qp, &qp_attr, IB_QP_STATE))
		check_qp_movement_and_print(priv, priv->qp, IB_QPS_ERR);

	/* Wait for all sends and receives to complete */
	begin = jiffies;

	while (priv->tx_head != priv->tx_tail || recvs_pending(dev)) {
		if (time_after(jiffies, begin + 5 * HZ)) {
			ipoib_warn(priv,
				   "timing out; %d sends %d receives not completed\n",
				   priv->tx_head - priv->tx_tail,
				   recvs_pending(dev));

			/*
			 * assume the HW is wedged and just free up
			 * all our pending work requests.
			 */
			while ((int)priv->tx_tail - (int)priv->tx_head < 0) {
				tx_req = &priv->tx_ring[priv->tx_tail &
							(ipoib_sendq_size - 1)];
				ipoib_dma_unmap_tx(priv, tx_req);
				dev_kfree_skb_any(tx_req->skb);
				++priv->tx_tail;
				--priv->tx_outstanding;
			}

			for (i = 0; i < ipoib_recvq_size; ++i) {
				struct ipoib_rx_buf *rx_req;

				rx_req = &priv->rx_ring[i];
				if (!rx_req->skb)
					continue;
				ipoib_ud_dma_unmap_rx(priv,
						      priv->rx_ring[i].mapping);
				dev_kfree_skb_any(rx_req->skb);
				rx_req->skb = NULL;
			}

			goto timeout;
		}

		ipoib_drain_cq(dev);

		usleep_range(1000, 2000);
	}

	ipoib_dbg(priv, "All sends and receives done.\n");

timeout:
	del_timer_sync(&priv->poll_timer);
	qp_attr.qp_state = IB_QPS_RESET;
	if (ib_modify_qp(priv->qp, &qp_attr, IB_QP_STATE))
		ipoib_warn(priv, "Failed to modify QP to RESET state\n");

	ib_req_notify_cq(priv->recv_cq, IB_CQ_NEXT_COMP);

	return 0;
}

int ipoib_ib_dev_stop(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	priv->rn_ops->ndo_stop(dev);

	clear_bit(IPOIB_FLAG_INITIALIZED, &priv->flags);
	ipoib_flush_ah(dev);

	return 0;
}

void ipoib_ib_tx_timer_func(unsigned long ctx)
{
	drain_tx_cq((struct net_device *)ctx);
}

int ipoib_ib_dev_open_default(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int ret;

	ret = ipoib_init_qp(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_init_qp returned %d\n", ret);
		return -1;
	}

	ret = ipoib_ib_post_receives(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_ib_post_receives returned %d\n", ret);
		goto out;
	}

	ret = ipoib_cm_dev_open(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_cm_dev_open returned %d\n", ret);
		goto out;
	}

	if (!test_bit(IPOIB_FLAG_INITIALIZED, &priv->flags))
		napi_enable(&priv->napi);

	return 0;
out:
	return -1;
}

int ipoib_ib_dev_open(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_pkey_dev_check_presence(dev);

	if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags)) {
		ipoib_warn(priv, "P_Key 0x%04x is %s\n", priv->pkey,
			   (!(priv->pkey & 0x7fff) ? "Invalid" : "not found"));
		return -1;
	}

	clear_bit(IPOIB_STOP_REAPER, &priv->flags);
	queue_delayed_work(priv->wq, &priv->ah_reap_task,
			   round_jiffies_relative(HZ));

	if (priv->rn_ops->ndo_open(dev)) {
		pr_warn("%s: Failed to open dev\n", dev->name);
		goto dev_stop;
	}

	set_bit(IPOIB_FLAG_INITIALIZED, &priv->flags);

	return 0;

dev_stop:
	set_bit(IPOIB_STOP_REAPER, &priv->flags);
	cancel_delayed_work(&priv->ah_reap_task);
	set_bit(IPOIB_FLAG_INITIALIZED, &priv->flags);
	ipoib_ib_dev_stop(dev);
	return -1;
}

void ipoib_pkey_dev_check_presence(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	if (!(priv->pkey & 0x7fff) ||
	    ib_find_pkey(priv->ca, priv->port, priv->pkey,
			 &priv->pkey_index))
		clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
	else
		set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
}

void ipoib_ib_dev_up(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_pkey_dev_check_presence(dev);

	if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags)) {
		ipoib_dbg(priv, "PKEY is not assigned.\n");
		return;
	}

	set_bit(IPOIB_FLAG_OPER_UP, &priv->flags);

	ipoib_mcast_start_thread(dev);
}

void ipoib_ib_dev_down(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_dbg(priv, "downing ib_dev\n");

	clear_bit(IPOIB_FLAG_OPER_UP, &priv->flags);
	netif_carrier_off(dev);

	ipoib_mcast_stop_thread(dev);
	ipoib_mcast_dev_flush(dev);

	ipoib_flush_paths(dev);
}

void ipoib_drain_cq(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int i, n;

	/*
	 * We call completion handling routines that expect to be
	 * called from the BH-disabled NAPI poll context, so disable
	 * BHs here too.
	 */
	local_bh_disable();

	do {
		n = ib_poll_cq(priv->recv_cq, IPOIB_NUM_WC, priv->ibwc);
		for (i = 0; i < n; ++i) {
			/*
			 * Convert any successful completions to flush
			 * errors to avoid passing packets up the
			 * stack after bringing the device down.
			 */
			if (priv->ibwc[i].status == IB_WC_SUCCESS)
				priv->ibwc[i].status = IB_WC_WR_FLUSH_ERR;

			if (priv->ibwc[i].wr_id & IPOIB_OP_RECV) {
				if (priv->ibwc[i].wr_id & IPOIB_OP_CM)
					ipoib_cm_handle_rx_wc(dev, priv->ibwc + i);
				else
					ipoib_ib_handle_rx_wc(dev, priv->ibwc + i);
			} else
				ipoib_cm_handle_tx_wc(dev, priv->ibwc + i);
		}
	} while (n == IPOIB_NUM_WC);

	while (poll_tx(priv))
		; /* nothing */

	local_bh_enable();
}

/*
 * Takes whatever value which is in pkey index 0 and updates priv->pkey
 * returns 0 if the pkey value was changed.
 */
static inline int update_parent_pkey(struct ipoib_dev_priv *priv)
{
	int result;
	u16 prev_pkey;

	prev_pkey = priv->pkey;
	result = ib_query_pkey(priv->ca, priv->port, 0, &priv->pkey);
	if (result) {
		ipoib_warn(priv, "ib_query_pkey port %d failed (ret = %d)\n",
			   priv->port, result);
		return result;
	}

	priv->pkey |= 0x8000;

	if (prev_pkey != priv->pkey) {
		ipoib_dbg(priv, "pkey changed from 0x%x to 0x%x\n",
			  prev_pkey, priv->pkey);
		/*
		 * Update the pkey in the broadcast address, while making sure to set
		 * the full membership bit, so that we join the right broadcast group.
		 */
		priv->dev->broadcast[8] = priv->pkey >> 8;
		priv->dev->broadcast[9] = priv->pkey & 0xff;

		/*
		 * Update the broadcast address in the priv->broadcast object,
		 * in case it already exists, otherwise no one will do that.
		 */
		if (priv->broadcast) {
			spin_lock_irq(&priv->lock);
			memcpy(priv->broadcast->mcmember.mgid.raw,
			       priv->dev->broadcast + 4,
			sizeof(union ib_gid));
			spin_unlock_irq(&priv->lock);
		}

		return 0;
	}

	return 1;
}
/*
 * returns 0 if pkey value was found in a different slot.
 */
static inline int update_child_pkey(struct ipoib_dev_priv *priv)
{
	u16 old_index = priv->pkey_index;

	priv->pkey_index = 0;
	ipoib_pkey_dev_check_presence(priv->dev);

	if (test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags) &&
	    (old_index == priv->pkey_index))
		return 1;
	return 0;
}

/*
 * returns true if the device address of the ipoib interface has changed and the
 * new address is a valid one (i.e in the gid table), return false otherwise.
 */
static bool ipoib_dev_addr_changed_valid(struct ipoib_dev_priv *priv)
{
	union ib_gid search_gid;
	union ib_gid gid0;
	union ib_gid *netdev_gid;
	int err;
	u16 index;
	u8 port;
	bool ret = false;

	netdev_gid = (union ib_gid *)(priv->dev->dev_addr + 4);
	if (ib_query_gid(priv->ca, priv->port, 0, &gid0, NULL))
		return false;

	netif_addr_lock_bh(priv->dev);

	/* The subnet prefix may have changed, update it now so we won't have
	 * to do it later
	 */
	priv->local_gid.global.subnet_prefix = gid0.global.subnet_prefix;
	netdev_gid->global.subnet_prefix = gid0.global.subnet_prefix;
	search_gid.global.subnet_prefix = gid0.global.subnet_prefix;

	search_gid.global.interface_id = priv->local_gid.global.interface_id;

	netif_addr_unlock_bh(priv->dev);

	err = ib_find_gid(priv->ca, &search_gid, IB_GID_TYPE_IB,
			  priv->dev, &port, &index);

	netif_addr_lock_bh(priv->dev);

	if (search_gid.global.interface_id !=
	    priv->local_gid.global.interface_id)
		/* There was a change while we were looking up the gid, bail
		 * here and let the next work sort this out
		 */
		goto out;

	/* The next section of code needs some background:
	 * Per IB spec the port GUID can't change if the HCA is powered on.
	 * port GUID is the basis for GID at index 0 which is the basis for
	 * the default device address of a ipoib interface.
	 *
	 * so it seems the flow should be:
	 * if user_changed_dev_addr && gid in gid tbl
	 *	set bit dev_addr_set
	 *	return true
	 * else
	 *	return false
	 *
	 * The issue is that there are devices that don't follow the spec,
	 * they change the port GUID when the HCA is powered, so in order
	 * not to break userspace applications, We need to check if the
	 * user wanted to control the device address and we assume that
	 * if he sets the device address back to be based on GID index 0,
	 * he no longer wishs to control it.
	 *
	 * If the user doesn't control the the device address,
	 * IPOIB_FLAG_DEV_ADDR_SET is set and ib_find_gid failed it means
	 * the port GUID has changed and GID at index 0 has changed
	 * so we need to change priv->local_gid and priv->dev->dev_addr
	 * to reflect the new GID.
	 */
	if (!test_bit(IPOIB_FLAG_DEV_ADDR_SET, &priv->flags)) {
		if (!err && port == priv->port) {
			set_bit(IPOIB_FLAG_DEV_ADDR_SET, &priv->flags);
			if (index == 0)
				clear_bit(IPOIB_FLAG_DEV_ADDR_CTRL,
					  &priv->flags);
			else
				set_bit(IPOIB_FLAG_DEV_ADDR_CTRL, &priv->flags);
			ret = true;
		} else {
			ret = false;
		}
	} else {
		if (!err && port == priv->port) {
			ret = true;
		} else {
			if (!test_bit(IPOIB_FLAG_DEV_ADDR_CTRL, &priv->flags)) {
				memcpy(&priv->local_gid, &gid0,
				       sizeof(priv->local_gid));
				memcpy(priv->dev->dev_addr + 4, &gid0,
				       sizeof(priv->local_gid));
				ret = true;
			}
		}
	}

out:
	netif_addr_unlock_bh(priv->dev);

	return ret;
}

static void __ipoib_ib_dev_flush(struct ipoib_dev_priv *priv,
				enum ipoib_flush_level level,
				int nesting)
{
	struct ipoib_dev_priv *cpriv;
	struct net_device *dev = priv->dev;
	int result;

	down_read_nested(&priv->vlan_rwsem, nesting);

	/*
	 * Flush any child interfaces too -- they might be up even if
	 * the parent is down.
	 */
	list_for_each_entry(cpriv, &priv->child_intfs, list)
		__ipoib_ib_dev_flush(cpriv, level, nesting + 1);

	up_read(&priv->vlan_rwsem);

	if (!test_bit(IPOIB_FLAG_INITIALIZED, &priv->flags) &&
	    level != IPOIB_FLUSH_HEAVY) {
		/* Make sure the dev_addr is set even if not flushing */
		if (level == IPOIB_FLUSH_LIGHT)
			ipoib_dev_addr_changed_valid(priv);
		ipoib_dbg(priv, "Not flushing - IPOIB_FLAG_INITIALIZED not set.\n");
		return;
	}

	if (!test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags)) {
		/* interface is down. update pkey and leave. */
		if (level == IPOIB_FLUSH_HEAVY) {
			if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags))
				update_parent_pkey(priv);
			else
				update_child_pkey(priv);
		} else if (level == IPOIB_FLUSH_LIGHT)
			ipoib_dev_addr_changed_valid(priv);
		ipoib_dbg(priv, "Not flushing - IPOIB_FLAG_ADMIN_UP not set.\n");
		return;
	}

	if (level == IPOIB_FLUSH_HEAVY) {
		/* child devices chase their origin pkey value, while non-child
		 * (parent) devices should always takes what present in pkey index 0
		 */
		if (test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
			result = update_child_pkey(priv);
			if (result) {
				/* restart QP only if P_Key index is changed */
				ipoib_dbg(priv, "Not flushing - P_Key index not changed.\n");
				return;
			}

		} else {
			result = update_parent_pkey(priv);
			/* restart QP only if P_Key value changed */
			if (result) {
				ipoib_dbg(priv, "Not flushing - P_Key value not changed.\n");
				return;
			}
		}
	}

	if (level == IPOIB_FLUSH_LIGHT) {
		int oper_up;
		ipoib_mark_paths_invalid(dev);
		/* Set IPoIB operation as down to prevent races between:
		 * the flush flow which leaves MCG and on the fly joins
		 * which can happen during that time. mcast restart task
		 * should deal with join requests we missed.
		 */
		oper_up = test_and_clear_bit(IPOIB_FLAG_OPER_UP, &priv->flags);
		ipoib_mcast_dev_flush(dev);
		if (oper_up)
			set_bit(IPOIB_FLAG_OPER_UP, &priv->flags);
		ipoib_flush_ah(dev);
	}

	if (level >= IPOIB_FLUSH_NORMAL)
		ipoib_ib_dev_down(dev);

	if (level == IPOIB_FLUSH_HEAVY) {
		if (test_bit(IPOIB_FLAG_INITIALIZED, &priv->flags))
			ipoib_ib_dev_stop(dev);
		if (ipoib_ib_dev_open(dev) != 0)
			return;
		if (netif_queue_stopped(dev))
			netif_start_queue(dev);
	}

	/*
	 * The device could have been brought down between the start and when
	 * we get here, don't bring it back up if it's not configured up
	 */
	if (test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags)) {
		if (level >= IPOIB_FLUSH_NORMAL)
			ipoib_ib_dev_up(dev);
		if (ipoib_dev_addr_changed_valid(priv))
			ipoib_mcast_restart_task(&priv->restart_task);
	}
}

void ipoib_ib_dev_flush_light(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, flush_light);

	__ipoib_ib_dev_flush(priv, IPOIB_FLUSH_LIGHT, 0);
}

void ipoib_ib_dev_flush_normal(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, flush_normal);

	__ipoib_ib_dev_flush(priv, IPOIB_FLUSH_NORMAL, 0);
}

void ipoib_ib_dev_flush_heavy(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, flush_heavy);

	__ipoib_ib_dev_flush(priv, IPOIB_FLUSH_HEAVY, 0);
}

void ipoib_ib_dev_cleanup(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_dbg(priv, "cleaning up ib_dev\n");
	/*
	 * We must make sure there are no more (path) completions
	 * that may wish to touch priv fields that are no longer valid
	 */
	ipoib_flush_paths(dev);

	ipoib_mcast_stop_thread(dev);
	ipoib_mcast_dev_flush(dev);

	/*
	 * All of our ah references aren't free until after
	 * ipoib_mcast_dev_flush(), ipoib_flush_paths, and
	 * the neighbor garbage collection is stopped and reaped.
	 * That should all be done now, so make a final ah flush.
	 */
	ipoib_stop_ah(dev);

	clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);

	priv->rn_ops->ndo_uninit(dev);

	if (priv->pd) {
		ib_dealloc_pd(priv->pd);
		priv->pd = NULL;
	}
}

