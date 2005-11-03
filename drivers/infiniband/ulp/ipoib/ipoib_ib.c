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

#include "ipoib.h"

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG_DATA
static int data_debug_level;

module_param(data_debug_level, int, 0644);
MODULE_PARM_DESC(data_debug_level,
		 "Enable data path debug tracing if > 0");
#endif

#define	IPOIB_OP_RECV	(1ul << 31)

static DECLARE_MUTEX(pkey_sem);

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

	if ((int) priv->tx_tail - (int) ah->last_send >= 0) {
		ipoib_dbg(priv, "Freeing ah %p\n", ah->ah);
		ib_destroy_ah(ah->ah);
		kfree(ah);
	} else {
		spin_lock_irqsave(&priv->lock, flags);
		list_add_tail(&ah->list, &priv->dead_ahs);
		spin_unlock_irqrestore(&priv->lock, flags);
	}
}

static int ipoib_ib_post_receive(struct net_device *dev, int id)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_sge list;
	struct ib_recv_wr param;
	struct ib_recv_wr *bad_wr;
	int ret;

	list.addr     = priv->rx_ring[id].mapping;
	list.length   = IPOIB_BUF_SIZE;
	list.lkey     = priv->mr->lkey;

	param.next    = NULL;
	param.wr_id   = id | IPOIB_OP_RECV;
	param.sg_list = &list;
	param.num_sge = 1;

	ret = ib_post_recv(priv->qp, &param, &bad_wr);
	if (unlikely(ret)) {
		ipoib_warn(priv, "receive failed for buf %d (%d)\n", id, ret);
		dma_unmap_single(priv->ca->dma_device,
				 priv->rx_ring[id].mapping,
				 IPOIB_BUF_SIZE, DMA_FROM_DEVICE);
		dev_kfree_skb_any(priv->rx_ring[id].skb);
		priv->rx_ring[id].skb = NULL;
	}

	return ret;
}

static int ipoib_alloc_rx_skb(struct net_device *dev, int id)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct sk_buff *skb;
	dma_addr_t addr;

	skb = dev_alloc_skb(IPOIB_BUF_SIZE + 4);
	if (!skb)
		return -ENOMEM;

	/*
	 * IB will leave a 40 byte gap for a GRH and IPoIB adds a 4 byte
	 * header.  So we need 4 more bytes to get to 48 and align the
	 * IP header to a multiple of 16.
	 */
	skb_reserve(skb, 4);

	addr = dma_map_single(priv->ca->dma_device,
			      skb->data, IPOIB_BUF_SIZE,
			      DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(addr))) {
		dev_kfree_skb_any(skb);
		return -EIO;
	}

	priv->rx_ring[id].skb     = skb;
	priv->rx_ring[id].mapping = addr;

	return 0;
}

static int ipoib_ib_post_receives(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int i;

	for (i = 0; i < IPOIB_RX_RING_SIZE; ++i) {
		if (ipoib_alloc_rx_skb(dev, i)) {
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

static void ipoib_ib_handle_wc(struct net_device *dev,
			       struct ib_wc *wc)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	unsigned int wr_id = wc->wr_id;

	ipoib_dbg_data(priv, "called: id %d, op %d, status: %d\n",
		       wr_id, wc->opcode, wc->status);

	if (wr_id & IPOIB_OP_RECV) {
		wr_id &= ~IPOIB_OP_RECV;

		if (wr_id < IPOIB_RX_RING_SIZE) {
			struct sk_buff *skb  = priv->rx_ring[wr_id].skb;
			dma_addr_t      addr = priv->rx_ring[wr_id].mapping;

			if (unlikely(wc->status != IB_WC_SUCCESS)) {
				if (wc->status != IB_WC_WR_FLUSH_ERR)
					ipoib_warn(priv, "failed recv event "
						   "(status=%d, wrid=%d vend_err %x)\n",
						   wc->status, wr_id, wc->vendor_err);
				dma_unmap_single(priv->ca->dma_device, addr,
						 IPOIB_BUF_SIZE, DMA_FROM_DEVICE);
				dev_kfree_skb_any(skb);
				priv->rx_ring[wr_id].skb = NULL;
				return;
			}

			/*
			 * If we can't allocate a new RX buffer, dump
			 * this packet and reuse the old buffer.
			 */
			if (unlikely(ipoib_alloc_rx_skb(dev, wr_id))) {
				++priv->stats.rx_dropped;
				goto repost;
			}

			ipoib_dbg_data(priv, "received %d bytes, SLID 0x%04x\n",
				       wc->byte_len, wc->slid);

			dma_unmap_single(priv->ca->dma_device, addr,
					 IPOIB_BUF_SIZE, DMA_FROM_DEVICE);

			skb_put(skb, wc->byte_len);
			skb_pull(skb, IB_GRH_BYTES);

			if (wc->slid != priv->local_lid ||
			    wc->src_qp != priv->qp->qp_num) {
				skb->protocol = ((struct ipoib_header *) skb->data)->proto;
				skb->mac.raw = skb->data;
				skb_pull(skb, IPOIB_ENCAP_LEN);

				dev->last_rx = jiffies;
				++priv->stats.rx_packets;
				priv->stats.rx_bytes += skb->len;

				skb->dev = dev;
				/* XXX get correct PACKET_ type here */
				skb->pkt_type = PACKET_HOST;
				netif_rx_ni(skb);
			} else {
				ipoib_dbg_data(priv, "dropping loopback packet\n");
				dev_kfree_skb_any(skb);
			}

		repost:
			if (unlikely(ipoib_ib_post_receive(dev, wr_id)))
				ipoib_warn(priv, "ipoib_ib_post_receive failed "
					   "for buf %d\n", wr_id);
		} else
			ipoib_warn(priv, "completion event with wrid %d\n",
				   wr_id);

	} else {
		struct ipoib_tx_buf *tx_req;
		unsigned long flags;

		if (wr_id >= IPOIB_TX_RING_SIZE) {
			ipoib_warn(priv, "completion event with wrid %d (> %d)\n",
				   wr_id, IPOIB_TX_RING_SIZE);
			return;
		}

		ipoib_dbg_data(priv, "send complete, wrid %d\n", wr_id);

		tx_req = &priv->tx_ring[wr_id];

		dma_unmap_single(priv->ca->dma_device,
				 pci_unmap_addr(tx_req, mapping),
				 tx_req->skb->len,
				 DMA_TO_DEVICE);

		++priv->stats.tx_packets;
		priv->stats.tx_bytes += tx_req->skb->len;

		dev_kfree_skb_any(tx_req->skb);

		spin_lock_irqsave(&priv->tx_lock, flags);
		++priv->tx_tail;
		if (netif_queue_stopped(dev) &&
		    priv->tx_head - priv->tx_tail <= IPOIB_TX_RING_SIZE / 2)
			netif_wake_queue(dev);
		spin_unlock_irqrestore(&priv->tx_lock, flags);

		if (wc->status != IB_WC_SUCCESS &&
		    wc->status != IB_WC_WR_FLUSH_ERR)
			ipoib_warn(priv, "failed send event "
				   "(status=%d, wrid=%d vend_err %x)\n",
				   wc->status, wr_id, wc->vendor_err);
	}
}

void ipoib_ib_completion(struct ib_cq *cq, void *dev_ptr)
{
	struct net_device *dev = (struct net_device *) dev_ptr;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int n, i;

	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
	do {
		n = ib_poll_cq(cq, IPOIB_NUM_WC, priv->ibwc);
		for (i = 0; i < n; ++i)
			ipoib_ib_handle_wc(dev, priv->ibwc + i);
	} while (n == IPOIB_NUM_WC);
}

static inline int post_send(struct ipoib_dev_priv *priv,
			    unsigned int wr_id,
			    struct ib_ah *address, u32 qpn,
			    dma_addr_t addr, int len)
{
	struct ib_send_wr *bad_wr;

	priv->tx_sge.addr             = addr;
	priv->tx_sge.length           = len;

	priv->tx_wr.wr_id 	      = wr_id;
	priv->tx_wr.wr.ud.remote_qpn  = qpn;
	priv->tx_wr.wr.ud.ah 	      = address;

	return ib_post_send(priv->qp, &priv->tx_wr, &bad_wr);
}

void ipoib_send(struct net_device *dev, struct sk_buff *skb,
		struct ipoib_ah *address, u32 qpn)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_tx_buf *tx_req;
	dma_addr_t addr;

	if (skb->len > dev->mtu + INFINIBAND_ALEN) {
		ipoib_warn(priv, "packet len %d (> %d) too long to send, dropping\n",
			   skb->len, dev->mtu + INFINIBAND_ALEN);
		++priv->stats.tx_dropped;
		++priv->stats.tx_errors;
		dev_kfree_skb_any(skb);
		return;
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
	tx_req = &priv->tx_ring[priv->tx_head & (IPOIB_TX_RING_SIZE - 1)];
	tx_req->skb = skb;
	addr = dma_map_single(priv->ca->dma_device, skb->data, skb->len,
			      DMA_TO_DEVICE);
	pci_unmap_addr_set(tx_req, mapping, addr);

	if (unlikely(post_send(priv, priv->tx_head & (IPOIB_TX_RING_SIZE - 1),
			       address->ah, qpn, addr, skb->len))) {
		ipoib_warn(priv, "post_send failed\n");
		++priv->stats.tx_errors;
		dma_unmap_single(priv->ca->dma_device, addr, skb->len,
				 DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
	} else {
		dev->trans_start = jiffies;

		address->last_send = priv->tx_head;
		++priv->tx_head;

		if (priv->tx_head - priv->tx_tail == IPOIB_TX_RING_SIZE) {
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

	spin_lock_irq(&priv->lock);
	list_for_each_entry_safe(ah, tah, &priv->dead_ahs, list)
		if ((int) priv->tx_tail - (int) ah->last_send >= 0) {
			list_del(&ah->list);
			list_add_tail(&ah->list, &remove_list);
		}
	spin_unlock_irq(&priv->lock);

	list_for_each_entry_safe(ah, tah, &remove_list, list) {
		ipoib_dbg(priv, "Reaping ah %p\n", ah->ah);
		ib_destroy_ah(ah->ah);
		kfree(ah);
	}
}

void ipoib_reap_ah(void *dev_ptr)
{
	struct net_device *dev = dev_ptr;
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	__ipoib_reap_ah(dev);

	if (!test_bit(IPOIB_STOP_REAPER, &priv->flags))
		queue_delayed_work(ipoib_workqueue, &priv->ah_reap_task, HZ);
}

int ipoib_ib_dev_open(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int ret;

	ret = ipoib_init_qp(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_init_qp returned %d\n", ret);
		return -1;
	}

	ret = ipoib_ib_post_receives(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_ib_post_receives returned %d\n", ret);
		return -1;
	}

	clear_bit(IPOIB_STOP_REAPER, &priv->flags);
	queue_delayed_work(ipoib_workqueue, &priv->ah_reap_task, HZ);

	return 0;
}

int ipoib_ib_dev_up(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	set_bit(IPOIB_FLAG_OPER_UP, &priv->flags);

	return ipoib_mcast_start_thread(dev);
}

int ipoib_ib_dev_down(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg(priv, "downing ib_dev\n");

	clear_bit(IPOIB_FLAG_OPER_UP, &priv->flags);
	netif_carrier_off(dev);

	/* Shutdown the P_Key thread if still active */
	if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags)) {
		down(&pkey_sem);
		set_bit(IPOIB_PKEY_STOP, &priv->flags);
		cancel_delayed_work(&priv->pkey_task);
		up(&pkey_sem);
		flush_workqueue(ipoib_workqueue);
	}

	ipoib_mcast_stop_thread(dev, 1);

	/*
	 * Flush the multicast groups first so we stop any multicast joins. The
	 * completion thread may have already died and we may deadlock waiting
	 * for the completion thread to finish some multicast joins.
	 */
	ipoib_mcast_dev_flush(dev);

	/* Delete broadcast and local addresses since they will be recreated */
	ipoib_mcast_dev_down(dev);

	ipoib_flush_paths(dev);

	return 0;
}

static int recvs_pending(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int pending = 0;
	int i;

	for (i = 0; i < IPOIB_RX_RING_SIZE; ++i)
		if (priv->rx_ring[i].skb)
			++pending;

	return pending;
}

int ipoib_ib_dev_stop(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_qp_attr qp_attr;
	unsigned long begin;
	struct ipoib_tx_buf *tx_req;
	int i;

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
							(IPOIB_TX_RING_SIZE - 1)];
				dma_unmap_single(priv->ca->dma_device,
						 pci_unmap_addr(tx_req, mapping),
						 tx_req->skb->len,
						 DMA_TO_DEVICE);
				dev_kfree_skb_any(tx_req->skb);
				++priv->tx_tail;
			}

			for (i = 0; i < IPOIB_RX_RING_SIZE; ++i)
				if (priv->rx_ring[i].skb) {
					dma_unmap_single(priv->ca->dma_device,
							 pci_unmap_addr(&priv->rx_ring[i],
									mapping),
							 IPOIB_BUF_SIZE,
							 DMA_FROM_DEVICE);
					dev_kfree_skb_any(priv->rx_ring[i].skb);
					priv->rx_ring[i].skb = NULL;
				}

			goto timeout;
		}

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

void ipoib_ib_dev_flush(void *_dev)
{
	struct net_device *dev = (struct net_device *)_dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev), *cpriv;

	if (!test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
		return;

	ipoib_dbg(priv, "flushing\n");

	ipoib_ib_dev_down(dev);

	/*
	 * The device could have been brought down between the start and when
	 * we get here, don't bring it back up if it's not configured up
	 */
	if (test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
		ipoib_ib_dev_up(dev);

	/* Flush any child interfaces too */
	list_for_each_entry(cpriv, &priv->child_intfs, list)
		ipoib_ib_dev_flush(&cpriv->dev);
}

void ipoib_ib_dev_cleanup(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg(priv, "cleaning up ib_dev\n");

	ipoib_mcast_stop_thread(dev, 1);

	/* Delete the broadcast address and the local address */
	ipoib_mcast_dev_down(dev);

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

static void ipoib_pkey_dev_check_presence(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	u16 pkey_index = 0;

	if (ib_find_cached_pkey(priv->ca, priv->port, priv->pkey, &pkey_index))
		clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
	else
		set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
}

void ipoib_pkey_poll(void *dev_ptr)
{
	struct net_device *dev = dev_ptr;
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_pkey_dev_check_presence(dev);

	if (test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags))
		ipoib_open(dev);
	else {
		down(&pkey_sem);
		if (!test_bit(IPOIB_PKEY_STOP, &priv->flags))
			queue_delayed_work(ipoib_workqueue,
					   &priv->pkey_task,
					   HZ);
		up(&pkey_sem);
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
		down(&pkey_sem);
		clear_bit(IPOIB_PKEY_STOP, &priv->flags);
		queue_delayed_work(ipoib_workqueue,
				   &priv->pkey_task,
				   HZ);
		up(&pkey_sem);
		return 1;
	}

	return 0;
}
