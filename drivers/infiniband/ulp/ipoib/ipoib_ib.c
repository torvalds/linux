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
 *
 * $Id: ipoib_ib.c 1386 2004-12-27 16:23:17Z roland $
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>

#include <rdma/ib_cache.h>
#include <linux/ip.h>
#include <linux/tcp.h>

#include "ipoib.h"

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG_DATA
static int data_debug_level;

module_param(data_debug_level, int, 0644);
MODULE_PARM_DESC(data_debug_level,
		 "Enable data path debug tracing if > 0");
#endif

static DEFINE_MUTEX(pkey_mutex);

struct ipoib_ah *ipoib_create_ah(struct net_device *dev,
				 struct ib_pd *pd, struct ib_ah_attr *attr)
{
	struct ipoib_ah *ah;

	ah = kmalloc(sizeof *ah, GFP_KERNEL);
	if (!ah)
		return NULL;

	ah->dev       = dev;
	ah->last_send = 0;
	kref_init(&ah->ref);

	ah->ah = ib_create_ah(pd, attr);
	if (IS_ERR(ah->ah)) {
		kfree(ah);
		ah = NULL;
	} else
		ipoib_dbg(netdev_priv(dev), "Created ah %p\n", ah->ah);

	return ah;
}

void ipoib_free_ah(struct kref *kref)
{
	struct ipoib_ah *ah = container_of(kref, struct ipoib_ah, ref);
	struct ipoib_dev_priv *priv = netdev_priv(ah->dev);

	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	list_add_tail(&ah->list, &priv->dead_ahs);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void ipoib_ud_dma_unmap_rx(struct ipoib_dev_priv *priv,
				  u64 mapping[IPOIB_UD_RX_SG])
{
	if (ipoib_ud_need_sg(priv->max_ib_mtu)) {
		ib_dma_unmap_single(priv->ca, mapping[0], IPOIB_UD_HEAD_SIZE,
				    DMA_FROM_DEVICE);
		ib_dma_unmap_page(priv->ca, mapping[1], PAGE_SIZE,
				  DMA_FROM_DEVICE);
	} else
		ib_dma_unmap_single(priv->ca, mapping[0],
				    IPOIB_UD_BUF_SIZE(priv->max_ib_mtu),
				    DMA_FROM_DEVICE);
}

static void ipoib_ud_skb_put_frags(struct ipoib_dev_priv *priv,
				   struct sk_buff *skb,
				   unsigned int length)
{
	if (ipoib_ud_need_sg(priv->max_ib_mtu)) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[0];
		unsigned int size;
		/*
		 * There is only two buffers needed for max_payload = 4K,
		 * first buf size is IPOIB_UD_HEAD_SIZE
		 */
		skb->tail += IPOIB_UD_HEAD_SIZE;
		skb->len  += length;

		size = length - IPOIB_UD_HEAD_SIZE;

		frag->size     = size;
		skb->data_len += size;
		skb->truesize += size;
	} else
		skb_put(skb, length);

}

static int ipoib_ib_post_receive(struct net_device *dev, int id)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
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
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct sk_buff *skb;
	int buf_size;
	u64 *mapping;

	if (ipoib_ud_need_sg(priv->max_ib_mtu))
		buf_size = IPOIB_UD_HEAD_SIZE;
	else
		buf_size = IPOIB_UD_BUF_SIZE(priv->max_ib_mtu);

	skb = dev_alloc_skb(buf_size + 4);
	if (unlikely(!skb))
		return NULL;

	/*
	 * IB will leave a 40 byte gap for a GRH and IPoIB adds a 4 byte
	 * header.  So we need 4 more bytes to get to 48 and align the
	 * IP header to a multiple of 16.
	 */
	skb_reserve(skb, 4);

	mapping = priv->rx_ring[id].mapping;
	mapping[0] = ib_dma_map_single(priv->ca, skb->data, buf_size,
				       DMA_FROM_DEVICE);
	if (unlikely(ib_dma_mapping_error(priv->ca, mapping[0])))
		goto error;

	if (ipoib_ud_need_sg(priv->max_ib_mtu)) {
		struct page *page = alloc_page(GFP_ATOMIC);
		if (!page)
			goto partial_error;
		skb_fill_page_desc(skb, 0, page, 0, PAGE_SIZE);
		mapping[1] =
			ib_dma_map_page(priv->ca, skb_shinfo(skb)->frags[0].page,
					0, PAGE_SIZE, DMA_FROM_DEVICE);
		if (unlikely(ib_dma_mapping_error(priv->ca, mapping[1])))
			goto partial_error;
	}

	priv->rx_ring[id].skb = skb;
	return skb;

partial_error:
	ib_dma_unmap_single(priv->ca, mapping[0], buf_size, DMA_FROM_DEVICE);
error:
	dev_kfree_skb_any(skb);
	return NULL;
}

static int ipoib_ib_post_receives(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
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
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	unsigned int wr_id = wc->wr_id & ~IPOIB_OP_RECV;
	struct sk_buff *skb;
	u64 mapping[IPOIB_UD_RX_SG];

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

	/*
	 * Drop packets that this interface sent, ie multicast packets
	 * that the HCA has replicated.
	 */
	if (wc->slid == priv->local_lid && wc->src_qp == priv->qp->qp_num)
		goto repost;

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
	ipoib_ud_skb_put_frags(priv, skb, wc->byte_len);

	skb_pull(skb, IB_GRH_BYTES);

	skb->protocol = ((struct ipoib_header *) skb->data)->proto;
	skb_reset_mac_header(skb);
	skb_pull(skb, IPOIB_ENCAP_LEN);

	dev->last_rx = jiffies;
	++dev->stats.rx_packets;
	dev->stats.rx_bytes += skb->len;

	skb->dev = dev;
	/* XXX get correct PACKET_ type here */
	skb->pkt_type = PACKET_HOST;

	if (test_bit(IPOIB_FLAG_CSUM, &priv->flags) && likely(wc->csum_ok))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	netif_receive_skb(skb);

repost:
	if (unlikely(ipoib_ib_post_receive(dev, wr_id)))
		ipoib_warn(priv, "ipoib_ib_post_receive failed "
			   "for buf %d\n", wr_id);
}

static int ipoib_dma_map_tx(struct ib_device *ca,
			    struct ipoib_tx_buf *tx_req)
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
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		mapping[i + off] = ib_dma_map_page(ca, frag->page,
						 frag->page_offset, frag->size,
						 DMA_TO_DEVICE);
		if (unlikely(ib_dma_mapping_error(ca, mapping[i + off])))
			goto partial_error;
	}
	return 0;

partial_error:
	for (; i > 0; --i) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i - 1];
		ib_dma_unmap_page(ca, mapping[i - !off], frag->size, DMA_TO_DEVICE);
	}

	if (off)
		ib_dma_unmap_single(ca, mapping[0], skb_headlen(skb), DMA_TO_DEVICE);

	return -EIO;
}

static void ipoib_dma_unmap_tx(struct ib_device *ca,
			       struct ipoib_tx_buf *tx_req)
{
	struct sk_buff *skb = tx_req->skb;
	u64 *mapping = tx_req->mapping;
	int i;
	int off;

	if (skb_headlen(skb)) {
		ib_dma_unmap_single(ca, mapping[0], skb_headlen(skb), DMA_TO_DEVICE);
		off = 1;
	} else
		off = 0;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; ++i) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		ib_dma_unmap_page(ca, mapping[i + off], frag->size,
				  DMA_TO_DEVICE);
	}
}

static void ipoib_ib_handle_tx_wc(struct net_device *dev, struct ib_wc *wc)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	unsigned int wr_id = wc->wr_id;
	struct ipoib_tx_buf *tx_req;
	unsigned long flags;

	ipoib_dbg_data(priv, "send completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= ipoib_sendq_size)) {
		ipoib_warn(priv, "send completion event with wrid %d (> %d)\n",
			   wr_id, ipoib_sendq_size);
		return;
	}

	tx_req = &priv->tx_ring[wr_id];

	ipoib_dma_unmap_tx(priv->ca, tx_req);

	++dev->stats.tx_packets;
	dev->stats.tx_bytes += tx_req->skb->len;

	dev_kfree_skb_any(tx_req->skb);

	spin_lock_irqsave(&priv->tx_lock, flags);
	++priv->tx_tail;
	if (unlikely(--priv->tx_outstanding == ipoib_sendq_size >> 1) &&
	    netif_queue_stopped(dev) &&
	    test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
		netif_wake_queue(dev);
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	if (wc->status != IB_WC_SUCCESS &&
	    wc->status != IB_WC_WR_FLUSH_ERR)
		ipoib_warn(priv, "failed send event "
			   "(status=%d, wrid=%d vend_err %x)\n",
			   wc->status, wr_id, wc->vendor_err);
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
		n = ib_poll_cq(priv->cq, t, priv->ibwc);

		for (i = 0; i < n; i++) {
			struct ib_wc *wc = priv->ibwc + i;

			if (wc->wr_id & IPOIB_OP_RECV) {
				++done;
				if (wc->wr_id & IPOIB_OP_CM)
					ipoib_cm_handle_rx_wc(dev, wc);
				else
					ipoib_ib_handle_rx_wc(dev, wc);
			} else {
				if (wc->wr_id & IPOIB_OP_CM)
					ipoib_cm_handle_tx_wc(dev, wc);
				else
					ipoib_ib_handle_tx_wc(dev, wc);
			}
		}

		if (n != t)
			break;
	}

	if (done < budget) {
		netif_rx_complete(dev, napi);
		if (unlikely(ib_req_notify_cq(priv->cq,
					      IB_CQ_NEXT_COMP |
					      IB_CQ_REPORT_MISSED_EVENTS)) &&
		    netif_rx_reschedule(dev, napi))
			goto poll_more;
	}

	return done;
}

void ipoib_ib_completion(struct ib_cq *cq, void *dev_ptr)
{
	struct net_device *dev = dev_ptr;
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	netif_rx_schedule(dev, &priv->napi);
}

static inline int post_send(struct ipoib_dev_priv *priv,
			    unsigned int wr_id,
			    struct ib_ah *address, u32 qpn,
			    struct ipoib_tx_buf *tx_req,
			    void *head, int hlen)
{
	struct ib_send_wr *bad_wr;
	int i, off;
	struct sk_buff *skb = tx_req->skb;
	skb_frag_t *frags = skb_shinfo(skb)->frags;
	int nr_frags = skb_shinfo(skb)->nr_frags;
	u64 *mapping = tx_req->mapping;

	if (skb_headlen(skb)) {
		priv->tx_sge[0].addr         = mapping[0];
		priv->tx_sge[0].length       = skb_headlen(skb);
		off = 1;
	} else
		off = 0;

	for (i = 0; i < nr_frags; ++i) {
		priv->tx_sge[i + off].addr = mapping[i + off];
		priv->tx_sge[i + off].length = frags[i].size;
	}
	priv->tx_wr.num_sge	     = nr_frags + off;
	priv->tx_wr.wr_id 	     = wr_id;
	priv->tx_wr.wr.ud.remote_qpn = qpn;
	priv->tx_wr.wr.ud.ah 	     = address;

	if (head) {
		priv->tx_wr.wr.ud.mss	 = skb_shinfo(skb)->gso_size;
		priv->tx_wr.wr.ud.header = head;
		priv->tx_wr.wr.ud.hlen	 = hlen;
		priv->tx_wr.opcode	 = IB_WR_LSO;
	} else
		priv->tx_wr.opcode	 = IB_WR_SEND;

	return ib_post_send(priv->qp, &priv->tx_wr, &bad_wr);
}

void ipoib_send(struct net_device *dev, struct sk_buff *skb,
		struct ipoib_ah *address, u32 qpn)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_tx_buf *tx_req;
	int hlen;
	void *phead;

	if (skb_is_gso(skb)) {
		hlen = skb_transport_offset(skb) + tcp_hdrlen(skb);
		phead = skb->data;
		if (unlikely(!skb_pull(skb, hlen))) {
			ipoib_warn(priv, "linear data too small\n");
			++dev->stats.tx_dropped;
			++dev->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return;
		}
	} else {
		if (unlikely(skb->len > priv->mcast_mtu + IPOIB_ENCAP_LEN)) {
			ipoib_warn(priv, "packet len %d (> %d) too long to send, dropping\n",
				   skb->len, priv->mcast_mtu + IPOIB_ENCAP_LEN);
			++dev->stats.tx_dropped;
			++dev->stats.tx_errors;
			ipoib_cm_skb_too_long(dev, skb, priv->mcast_mtu);
			return;
		}
		phead = NULL;
		hlen  = 0;
	}

	ipoib_dbg_data(priv, "sending packet, length=%d address=%p qpn=0x%06x\n",
		       skb->len, address, qpn);

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
		return;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		priv->tx_wr.send_flags |= IB_SEND_IP_CSUM;
	else
		priv->tx_wr.send_flags &= ~IB_SEND_IP_CSUM;

	if (unlikely(post_send(priv, priv->tx_head & (ipoib_sendq_size - 1),
			       address->ah, qpn, tx_req, phead, hlen))) {
		ipoib_warn(priv, "post_send failed\n");
		++dev->stats.tx_errors;
		ipoib_dma_unmap_tx(priv->ca, tx_req);
		dev_kfree_skb_any(skb);
	} else {
		dev->trans_start = jiffies;

		address->last_send = priv->tx_head;
		++priv->tx_head;

		if (++priv->tx_outstanding == ipoib_sendq_size) {
			ipoib_dbg(priv, "TX ring full, stopping kernel net queue\n");
			netif_stop_queue(dev);
		}
	}
}

static void __ipoib_reap_ah(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_ah *ah, *tah;
	LIST_HEAD(remove_list);

	spin_lock_irq(&priv->tx_lock);
	spin_lock(&priv->lock);
	list_for_each_entry_safe(ah, tah, &priv->dead_ahs, list)
		if ((int) priv->tx_tail - (int) ah->last_send >= 0) {
			list_del(&ah->list);
			ib_destroy_ah(ah->ah);
			kfree(ah);
		}
	spin_unlock(&priv->lock);
	spin_unlock_irq(&priv->tx_lock);
}

void ipoib_reap_ah(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, ah_reap_task.work);
	struct net_device *dev = priv->dev;

	__ipoib_reap_ah(dev);

	if (!test_bit(IPOIB_STOP_REAPER, &priv->flags))
		queue_delayed_work(ipoib_workqueue, &priv->ah_reap_task,
				   round_jiffies_relative(HZ));
}

int ipoib_ib_dev_open(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int ret;

	if (ib_find_pkey(priv->ca, priv->port, priv->pkey, &priv->pkey_index)) {
		ipoib_warn(priv, "P_Key 0x%04x not found\n", priv->pkey);
		clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
		return -1;
	}
	set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);

	ret = ipoib_init_qp(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_init_qp returned %d\n", ret);
		return -1;
	}

	ret = ipoib_ib_post_receives(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_ib_post_receives returned %d\n", ret);
		ipoib_ib_dev_stop(dev, 1);
		return -1;
	}

	ret = ipoib_cm_dev_open(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_cm_dev_open returned %d\n", ret);
		ipoib_ib_dev_stop(dev, 1);
		return -1;
	}

	clear_bit(IPOIB_STOP_REAPER, &priv->flags);
	queue_delayed_work(ipoib_workqueue, &priv->ah_reap_task,
			   round_jiffies_relative(HZ));

	set_bit(IPOIB_FLAG_INITIALIZED, &priv->flags);

	return 0;
}

static void ipoib_pkey_dev_check_presence(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	u16 pkey_index = 0;

	if (ib_find_pkey(priv->ca, priv->port, priv->pkey, &pkey_index))
		clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
	else
		set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
}

int ipoib_ib_dev_up(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_pkey_dev_check_presence(dev);

	if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags)) {
		ipoib_dbg(priv, "PKEY is not assigned.\n");
		return 0;
	}

	set_bit(IPOIB_FLAG_OPER_UP, &priv->flags);

	return ipoib_mcast_start_thread(dev);
}

int ipoib_ib_dev_down(struct net_device *dev, int flush)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg(priv, "downing ib_dev\n");

	clear_bit(IPOIB_FLAG_OPER_UP, &priv->flags);
	netif_carrier_off(dev);

	/* Shutdown the P_Key thread if still active */
	if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags)) {
		mutex_lock(&pkey_mutex);
		set_bit(IPOIB_PKEY_STOP, &priv->flags);
		cancel_delayed_work(&priv->pkey_poll_task);
		mutex_unlock(&pkey_mutex);
		if (flush)
			flush_workqueue(ipoib_workqueue);
	}

	ipoib_mcast_stop_thread(dev, flush);
	ipoib_mcast_dev_flush(dev);

	ipoib_flush_paths(dev);

	return 0;
}

static int recvs_pending(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int pending = 0;
	int i;

	for (i = 0; i < ipoib_recvq_size; ++i)
		if (priv->rx_ring[i].skb)
			++pending;

	return pending;
}

void ipoib_drain_cq(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int i, n;
	do {
		n = ib_poll_cq(priv->cq, IPOIB_NUM_WC, priv->ibwc);
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
			} else {
				if (priv->ibwc[i].wr_id & IPOIB_OP_CM)
					ipoib_cm_handle_tx_wc(dev, priv->ibwc + i);
				else
					ipoib_ib_handle_tx_wc(dev, priv->ibwc + i);
			}
		}
	} while (n == IPOIB_NUM_WC);
}

int ipoib_ib_dev_stop(struct net_device *dev, int flush)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_qp_attr qp_attr;
	unsigned long begin;
	struct ipoib_tx_buf *tx_req;
	int i;

	clear_bit(IPOIB_FLAG_INITIALIZED, &priv->flags);

	ipoib_cm_dev_stop(dev);

	/*
	 * Move our QP to the error state and then reinitialize in
	 * when all work requests have completed or have been flushed.
	 */
	qp_attr.qp_state = IB_QPS_ERR;
	if (ib_modify_qp(priv->qp, &qp_attr, IB_QP_STATE))
		ipoib_warn(priv, "Failed to modify QP to ERROR state\n");

	/* Wait for all sends and receives to complete */
	begin = jiffies;

	while (priv->tx_head != priv->tx_tail || recvs_pending(dev)) {
		if (time_after(jiffies, begin + 5 * HZ)) {
			ipoib_warn(priv, "timing out; %d sends %d receives not completed\n",
				   priv->tx_head - priv->tx_tail, recvs_pending(dev));

			/*
			 * assume the HW is wedged and just free up
			 * all our pending work requests.
			 */
			while ((int) priv->tx_tail - (int) priv->tx_head < 0) {
				tx_req = &priv->tx_ring[priv->tx_tail &
							(ipoib_sendq_size - 1)];
				ipoib_dma_unmap_tx(priv->ca, tx_req);
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

		msleep(1);
	}

	ipoib_dbg(priv, "All sends and receives done.\n");

timeout:
	qp_attr.qp_state = IB_QPS_RESET;
	if (ib_modify_qp(priv->qp, &qp_attr, IB_QP_STATE))
		ipoib_warn(priv, "Failed to modify QP to RESET state\n");

	/* Wait for all AHs to be reaped */
	set_bit(IPOIB_STOP_REAPER, &priv->flags);
	cancel_delayed_work(&priv->ah_reap_task);
	if (flush)
		flush_workqueue(ipoib_workqueue);

	begin = jiffies;

	while (!list_empty(&priv->dead_ahs)) {
		__ipoib_reap_ah(dev);

		if (time_after(jiffies, begin + HZ)) {
			ipoib_warn(priv, "timing out; will leak address handles\n");
			break;
		}

		msleep(1);
	}

	ib_req_notify_cq(priv->cq, IB_CQ_NEXT_COMP);

	return 0;
}

int ipoib_ib_dev_init(struct net_device *dev, struct ib_device *ca, int port)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	priv->ca = ca;
	priv->port = port;
	priv->qp = NULL;

	if (ipoib_transport_dev_init(dev, ca)) {
		printk(KERN_WARNING "%s: ipoib_transport_dev_init failed\n", ca->name);
		return -ENODEV;
	}

	if (dev->flags & IFF_UP) {
		if (ipoib_ib_dev_open(dev)) {
			ipoib_transport_dev_cleanup(dev);
			return -ENODEV;
		}
	}

	return 0;
}

static void __ipoib_ib_dev_flush(struct ipoib_dev_priv *priv, int pkey_event)
{
	struct ipoib_dev_priv *cpriv;
	struct net_device *dev = priv->dev;
	u16 new_index;

	mutex_lock(&priv->vlan_mutex);

	/*
	 * Flush any child interfaces too -- they might be up even if
	 * the parent is down.
	 */
	list_for_each_entry(cpriv, &priv->child_intfs, list)
		__ipoib_ib_dev_flush(cpriv, pkey_event);

	mutex_unlock(&priv->vlan_mutex);

	if (!test_bit(IPOIB_FLAG_INITIALIZED, &priv->flags)) {
		ipoib_dbg(priv, "Not flushing - IPOIB_FLAG_INITIALIZED not set.\n");
		return;
	}

	if (!test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags)) {
		ipoib_dbg(priv, "Not flushing - IPOIB_FLAG_ADMIN_UP not set.\n");
		return;
	}

	if (pkey_event) {
		if (ib_find_pkey(priv->ca, priv->port, priv->pkey, &new_index)) {
			clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
			ipoib_ib_dev_down(dev, 0);
			ipoib_ib_dev_stop(dev, 0);
			if (ipoib_pkey_dev_delay_open(dev))
				return;
		}

		/* restart QP only if P_Key index is changed */
		if (test_and_set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags) &&
		    new_index == priv->pkey_index) {
			ipoib_dbg(priv, "Not flushing - P_Key index not changed.\n");
			return;
		}
		priv->pkey_index = new_index;
	}

	ipoib_dbg(priv, "flushing\n");

	ipoib_ib_dev_down(dev, 0);

	if (pkey_event) {
		ipoib_ib_dev_stop(dev, 0);
		ipoib_ib_dev_open(dev);
	}

	/*
	 * The device could have been brought down between the start and when
	 * we get here, don't bring it back up if it's not configured up
	 */
	if (test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags)) {
		ipoib_ib_dev_up(dev);
		ipoib_mcast_restart_task(&priv->restart_task);
	}
}

void ipoib_ib_dev_flush(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, flush_task);

	ipoib_dbg(priv, "Flushing %s\n", priv->dev->name);
	__ipoib_ib_dev_flush(priv, 0);
}

void ipoib_pkey_event(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, pkey_event_task);

	ipoib_dbg(priv, "Flushing %s and restarting its QP\n", priv->dev->name);
	__ipoib_ib_dev_flush(priv, 1);
}

void ipoib_ib_dev_cleanup(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg(priv, "cleaning up ib_dev\n");

	ipoib_mcast_stop_thread(dev, 1);
	ipoib_mcast_dev_flush(dev);

	ipoib_transport_dev_cleanup(dev);
}

/*
 * Delayed P_Key Assigment Interim Support
 *
 * The following is initial implementation of delayed P_Key assigment
 * mechanism. It is using the same approach implemented for the multicast
 * group join. The single goal of this implementation is to quickly address
 * Bug #2507. This implementation will probably be removed when the P_Key
 * change async notification is available.
 */

void ipoib_pkey_poll(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, pkey_poll_task.work);
	struct net_device *dev = priv->dev;

	ipoib_pkey_dev_check_presence(dev);

	if (test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags))
		ipoib_open(dev);
	else {
		mutex_lock(&pkey_mutex);
		if (!test_bit(IPOIB_PKEY_STOP, &priv->flags))
			queue_delayed_work(ipoib_workqueue,
					   &priv->pkey_poll_task,
					   HZ);
		mutex_unlock(&pkey_mutex);
	}
}

int ipoib_pkey_dev_delay_open(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	/* Look for the interface pkey value in the IB Port P_Key table and */
	/* set the interface pkey assigment flag                            */
	ipoib_pkey_dev_check_presence(dev);

	/* P_Key value not assigned yet - start polling */
	if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags)) {
		mutex_lock(&pkey_mutex);
		clear_bit(IPOIB_PKEY_STOP, &priv->flags);
		queue_delayed_work(ipoib_workqueue,
				   &priv->pkey_poll_task,
				   HZ);
		mutex_unlock(&pkey_mutex);
		return 1;
	}

	return 0;
}
