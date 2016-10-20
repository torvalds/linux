/*
 * Network-device interface management.
 *
 * Copyright (c) 2004-2005, Keir Fraser
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
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <linux/vmalloc.h>

#include <xen/events.h>
#include <asm/xen/hypercall.h>
#include <xen/balloon.h>

#define XENVIF_QUEUE_LENGTH 32
#define XENVIF_NAPI_WEIGHT  64

/* Number of bytes allowed on the internal guest Rx queue. */
#define XENVIF_RX_QUEUE_BYTES (XEN_NETIF_RX_RING_SIZE/2 * PAGE_SIZE)

/* This function is used to set SKBTX_DEV_ZEROCOPY as well as
 * increasing the inflight counter. We need to increase the inflight
 * counter because core driver calls into xenvif_zerocopy_callback
 * which calls xenvif_skb_zerocopy_complete.
 */
void xenvif_skb_zerocopy_prepare(struct xenvif_queue *queue,
				 struct sk_buff *skb)
{
	skb_shinfo(skb)->tx_flags |= SKBTX_DEV_ZEROCOPY;
	atomic_inc(&queue->inflight_packets);
}

void xenvif_skb_zerocopy_complete(struct xenvif_queue *queue)
{
	atomic_dec(&queue->inflight_packets);

	/* Wake the dealloc thread _after_ decrementing inflight_packets so
	 * that if kthread_stop() has already been called, the dealloc thread
	 * does not wait forever with nothing to wake it.
	 */
	wake_up(&queue->dealloc_wq);
}

int xenvif_schedulable(struct xenvif *vif)
{
	return netif_running(vif->dev) &&
		test_bit(VIF_STATUS_CONNECTED, &vif->status) &&
		!vif->disabled;
}

static irqreturn_t xenvif_tx_interrupt(int irq, void *dev_id)
{
	struct xenvif_queue *queue = dev_id;

	if (RING_HAS_UNCONSUMED_REQUESTS(&queue->tx))
		napi_schedule(&queue->napi);

	return IRQ_HANDLED;
}

static int xenvif_poll(struct napi_struct *napi, int budget)
{
	struct xenvif_queue *queue =
		container_of(napi, struct xenvif_queue, napi);
	int work_done;

	/* This vif is rogue, we pretend we've there is nothing to do
	 * for this vif to deschedule it from NAPI. But this interface
	 * will be turned off in thread context later.
	 */
	if (unlikely(queue->vif->disabled)) {
		napi_complete(napi);
		return 0;
	}

	work_done = xenvif_tx_action(queue, budget);

	if (work_done < budget) {
		napi_complete(napi);
		xenvif_napi_schedule_or_enable_events(queue);
	}

	return work_done;
}

static irqreturn_t xenvif_rx_interrupt(int irq, void *dev_id)
{
	struct xenvif_queue *queue = dev_id;

	xenvif_kick_thread(queue);

	return IRQ_HANDLED;
}

irqreturn_t xenvif_interrupt(int irq, void *dev_id)
{
	xenvif_tx_interrupt(irq, dev_id);
	xenvif_rx_interrupt(irq, dev_id);

	return IRQ_HANDLED;
}

int xenvif_queue_stopped(struct xenvif_queue *queue)
{
	struct net_device *dev = queue->vif->dev;
	unsigned int id = queue->id;
	return netif_tx_queue_stopped(netdev_get_tx_queue(dev, id));
}

void xenvif_wake_queue(struct xenvif_queue *queue)
{
	struct net_device *dev = queue->vif->dev;
	unsigned int id = queue->id;
	netif_tx_wake_queue(netdev_get_tx_queue(dev, id));
}

static u16 xenvif_select_queue(struct net_device *dev, struct sk_buff *skb,
			       void *accel_priv,
			       select_queue_fallback_t fallback)
{
	struct xenvif *vif = netdev_priv(dev);
	unsigned int size = vif->hash.size;

	if (vif->hash.alg == XEN_NETIF_CTRL_HASH_ALGORITHM_NONE)
		return fallback(dev, skb) % dev->real_num_tx_queues;

	xenvif_set_skb_hash(vif, skb);

	if (size == 0)
		return skb_get_hash_raw(skb) % dev->real_num_tx_queues;

	return vif->hash.mapping[skb_get_hash_raw(skb) % size];
}

static int xenvif_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct xenvif *vif = netdev_priv(dev);
	struct xenvif_queue *queue = NULL;
	unsigned int num_queues = vif->num_queues;
	u16 index;
	struct xenvif_rx_cb *cb;

	BUG_ON(skb->dev != dev);

	/* Drop the packet if queues are not set up */
	if (num_queues < 1)
		goto drop;

	/* Obtain the queue to be used to transmit this packet */
	index = skb_get_queue_mapping(skb);
	if (index >= num_queues) {
		pr_warn_ratelimited("Invalid queue %hu for packet on interface %s\n.",
				    index, vif->dev->name);
		index %= num_queues;
	}
	queue = &vif->queues[index];

	/* Drop the packet if queue is not ready */
	if (queue->task == NULL ||
	    queue->dealloc_task == NULL ||
	    !xenvif_schedulable(vif))
		goto drop;

	if (vif->multicast_control && skb->pkt_type == PACKET_MULTICAST) {
		struct ethhdr *eth = (struct ethhdr *)skb->data;

		if (!xenvif_mcast_match(vif, eth->h_dest))
			goto drop;
	}

	cb = XENVIF_RX_CB(skb);
	cb->expires = jiffies + vif->drain_timeout;

	/* If there is no hash algorithm configured then make sure there
	 * is no hash information in the socket buffer otherwise it
	 * would be incorrectly forwarded to the frontend.
	 */
	if (vif->hash.alg == XEN_NETIF_CTRL_HASH_ALGORITHM_NONE)
		skb_clear_hash(skb);

	xenvif_rx_queue_tail(queue, skb);
	xenvif_kick_thread(queue);

	return NETDEV_TX_OK;

 drop:
	vif->dev->stats.tx_dropped++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static struct net_device_stats *xenvif_get_stats(struct net_device *dev)
{
	struct xenvif *vif = netdev_priv(dev);
	struct xenvif_queue *queue = NULL;
	unsigned int num_queues = vif->num_queues;
	unsigned long rx_bytes = 0;
	unsigned long rx_packets = 0;
	unsigned long tx_bytes = 0;
	unsigned long tx_packets = 0;
	unsigned int index;

	if (vif->queues == NULL)
		goto out;

	/* Aggregate tx and rx stats from each queue */
	for (index = 0; index < num_queues; ++index) {
		queue = &vif->queues[index];
		rx_bytes += queue->stats.rx_bytes;
		rx_packets += queue->stats.rx_packets;
		tx_bytes += queue->stats.tx_bytes;
		tx_packets += queue->stats.tx_packets;
	}

out:
	vif->dev->stats.rx_bytes = rx_bytes;
	vif->dev->stats.rx_packets = rx_packets;
	vif->dev->stats.tx_bytes = tx_bytes;
	vif->dev->stats.tx_packets = tx_packets;

	return &vif->dev->stats;
}

static void xenvif_up(struct xenvif *vif)
{
	struct xenvif_queue *queue = NULL;
	unsigned int num_queues = vif->num_queues;
	unsigned int queue_index;

	for (queue_index = 0; queue_index < num_queues; ++queue_index) {
		queue = &vif->queues[queue_index];
		napi_enable(&queue->napi);
		enable_irq(queue->tx_irq);
		if (queue->tx_irq != queue->rx_irq)
			enable_irq(queue->rx_irq);
		xenvif_napi_schedule_or_enable_events(queue);
	}
}

static void xenvif_down(struct xenvif *vif)
{
	struct xenvif_queue *queue = NULL;
	unsigned int num_queues = vif->num_queues;
	unsigned int queue_index;

	for (queue_index = 0; queue_index < num_queues; ++queue_index) {
		queue = &vif->queues[queue_index];
		disable_irq(queue->tx_irq);
		if (queue->tx_irq != queue->rx_irq)
			disable_irq(queue->rx_irq);
		napi_disable(&queue->napi);
		del_timer_sync(&queue->credit_timeout);
	}
}

static int xenvif_open(struct net_device *dev)
{
	struct xenvif *vif = netdev_priv(dev);
	if (test_bit(VIF_STATUS_CONNECTED, &vif->status))
		xenvif_up(vif);
	netif_tx_start_all_queues(dev);
	return 0;
}

static int xenvif_close(struct net_device *dev)
{
	struct xenvif *vif = netdev_priv(dev);
	if (test_bit(VIF_STATUS_CONNECTED, &vif->status))
		xenvif_down(vif);
	netif_tx_stop_all_queues(dev);
	return 0;
}

static int xenvif_change_mtu(struct net_device *dev, int mtu)
{
	struct xenvif *vif = netdev_priv(dev);
	int max = vif->can_sg ? 65535 - VLAN_ETH_HLEN : ETH_DATA_LEN;

	if (mtu > max)
		return -EINVAL;
	dev->mtu = mtu;
	return 0;
}

static netdev_features_t xenvif_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	struct xenvif *vif = netdev_priv(dev);

	if (!vif->can_sg)
		features &= ~NETIF_F_SG;
	if (~(vif->gso_mask) & GSO_BIT(TCPV4))
		features &= ~NETIF_F_TSO;
	if (~(vif->gso_mask) & GSO_BIT(TCPV6))
		features &= ~NETIF_F_TSO6;
	if (!vif->ip_csum)
		features &= ~NETIF_F_IP_CSUM;
	if (!vif->ipv6_csum)
		features &= ~NETIF_F_IPV6_CSUM;

	return features;
}

static const struct xenvif_stat {
	char name[ETH_GSTRING_LEN];
	u16 offset;
} xenvif_stats[] = {
	{
		"rx_gso_checksum_fixup",
		offsetof(struct xenvif_stats, rx_gso_checksum_fixup)
	},
	/* If (sent != success + fail), there are probably packets never
	 * freed up properly!
	 */
	{
		"tx_zerocopy_sent",
		offsetof(struct xenvif_stats, tx_zerocopy_sent),
	},
	{
		"tx_zerocopy_success",
		offsetof(struct xenvif_stats, tx_zerocopy_success),
	},
	{
		"tx_zerocopy_fail",
		offsetof(struct xenvif_stats, tx_zerocopy_fail)
	},
	/* Number of packets exceeding MAX_SKB_FRAG slots. You should use
	 * a guest with the same MAX_SKB_FRAG
	 */
	{
		"tx_frag_overflow",
		offsetof(struct xenvif_stats, tx_frag_overflow)
	},
};

static int xenvif_get_sset_count(struct net_device *dev, int string_set)
{
	switch (string_set) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(xenvif_stats);
	default:
		return -EINVAL;
	}
}

static void xenvif_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats, u64 * data)
{
	struct xenvif *vif = netdev_priv(dev);
	unsigned int num_queues = vif->num_queues;
	int i;
	unsigned int queue_index;

	for (i = 0; i < ARRAY_SIZE(xenvif_stats); i++) {
		unsigned long accum = 0;
		for (queue_index = 0; queue_index < num_queues; ++queue_index) {
			void *vif_stats = &vif->queues[queue_index].stats;
			accum += *(unsigned long *)(vif_stats + xenvif_stats[i].offset);
		}
		data[i] = accum;
	}
}

static void xenvif_get_strings(struct net_device *dev, u32 stringset, u8 * data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(xenvif_stats); i++)
			memcpy(data + i * ETH_GSTRING_LEN,
			       xenvif_stats[i].name, ETH_GSTRING_LEN);
		break;
	}
}

static const struct ethtool_ops xenvif_ethtool_ops = {
	.get_link	= ethtool_op_get_link,

	.get_sset_count = xenvif_get_sset_count,
	.get_ethtool_stats = xenvif_get_ethtool_stats,
	.get_strings = xenvif_get_strings,
};

static const struct net_device_ops xenvif_netdev_ops = {
	.ndo_select_queue = xenvif_select_queue,
	.ndo_start_xmit	= xenvif_start_xmit,
	.ndo_get_stats	= xenvif_get_stats,
	.ndo_open	= xenvif_open,
	.ndo_stop	= xenvif_close,
	.ndo_change_mtu	= xenvif_change_mtu,
	.ndo_fix_features = xenvif_fix_features,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr   = eth_validate_addr,
};

struct xenvif *xenvif_alloc(struct device *parent, domid_t domid,
			    unsigned int handle)
{
	int err;
	struct net_device *dev;
	struct xenvif *vif;
	char name[IFNAMSIZ] = {};

	snprintf(name, IFNAMSIZ - 1, "vif%u.%u", domid, handle);
	/* Allocate a netdev with the max. supported number of queues.
	 * When the guest selects the desired number, it will be updated
	 * via netif_set_real_num_*_queues().
	 */
	dev = alloc_netdev_mq(sizeof(struct xenvif), name, NET_NAME_UNKNOWN,
			      ether_setup, xenvif_max_queues);
	if (dev == NULL) {
		pr_warn("Could not allocate netdev for %s\n", name);
		return ERR_PTR(-ENOMEM);
	}

	SET_NETDEV_DEV(dev, parent);

	vif = netdev_priv(dev);

	vif->domid  = domid;
	vif->handle = handle;
	vif->can_sg = 1;
	vif->ip_csum = 1;
	vif->dev = dev;
	vif->disabled = false;
	vif->drain_timeout = msecs_to_jiffies(rx_drain_timeout_msecs);
	vif->stall_timeout = msecs_to_jiffies(rx_stall_timeout_msecs);

	/* Start out with no queues. */
	vif->queues = NULL;
	vif->num_queues = 0;

	spin_lock_init(&vif->lock);
	INIT_LIST_HEAD(&vif->fe_mcast_addr);

	dev->netdev_ops	= &xenvif_netdev_ops;
	dev->hw_features = NETIF_F_SG |
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_FRAGLIST;
	dev->features = dev->hw_features | NETIF_F_RXCSUM;
	dev->ethtool_ops = &xenvif_ethtool_ops;

	dev->tx_queue_len = XENVIF_QUEUE_LENGTH;

	/*
	 * Initialise a dummy MAC address. We choose the numerically
	 * largest non-broadcast address to prevent the address getting
	 * stolen by an Ethernet bridge for STP purposes.
	 * (FE:FF:FF:FF:FF:FF)
	 */
	eth_broadcast_addr(dev->dev_addr);
	dev->dev_addr[0] &= ~0x01;

	netif_carrier_off(dev);

	err = register_netdev(dev);
	if (err) {
		netdev_warn(dev, "Could not register device: err=%d\n", err);
		free_netdev(dev);
		return ERR_PTR(err);
	}

	netdev_dbg(dev, "Successfully created xenvif\n");

	__module_get(THIS_MODULE);

	return vif;
}

int xenvif_init_queue(struct xenvif_queue *queue)
{
	int err, i;

	queue->credit_bytes = queue->remaining_credit = ~0UL;
	queue->credit_usec  = 0UL;
	init_timer(&queue->credit_timeout);
	queue->credit_timeout.function = xenvif_tx_credit_callback;
	queue->credit_window_start = get_jiffies_64();

	queue->rx_queue_max = XENVIF_RX_QUEUE_BYTES;

	skb_queue_head_init(&queue->rx_queue);
	skb_queue_head_init(&queue->tx_queue);

	queue->pending_cons = 0;
	queue->pending_prod = MAX_PENDING_REQS;
	for (i = 0; i < MAX_PENDING_REQS; ++i)
		queue->pending_ring[i] = i;

	spin_lock_init(&queue->callback_lock);
	spin_lock_init(&queue->response_lock);

	/* If ballooning is disabled, this will consume real memory, so you
	 * better enable it. The long term solution would be to use just a
	 * bunch of valid page descriptors, without dependency on ballooning
	 */
	err = gnttab_alloc_pages(MAX_PENDING_REQS,
				 queue->mmap_pages);
	if (err) {
		netdev_err(queue->vif->dev, "Could not reserve mmap_pages\n");
		return -ENOMEM;
	}

	for (i = 0; i < MAX_PENDING_REQS; i++) {
		queue->pending_tx_info[i].callback_struct = (struct ubuf_info)
			{ .callback = xenvif_zerocopy_callback,
			  .ctx = NULL,
			  .desc = i };
		queue->grant_tx_handle[i] = NETBACK_INVALID_HANDLE;
	}

	return 0;
}

void xenvif_carrier_on(struct xenvif *vif)
{
	rtnl_lock();
	if (!vif->can_sg && vif->dev->mtu > ETH_DATA_LEN)
		dev_set_mtu(vif->dev, ETH_DATA_LEN);
	netdev_update_features(vif->dev);
	set_bit(VIF_STATUS_CONNECTED, &vif->status);
	if (netif_running(vif->dev))
		xenvif_up(vif);
	rtnl_unlock();
}

int xenvif_connect_ctrl(struct xenvif *vif, grant_ref_t ring_ref,
			unsigned int evtchn)
{
	struct net_device *dev = vif->dev;
	void *addr;
	struct xen_netif_ctrl_sring *shared;
	int err;

	err = xenbus_map_ring_valloc(xenvif_to_xenbus_device(vif),
				     &ring_ref, 1, &addr);
	if (err)
		goto err;

	shared = (struct xen_netif_ctrl_sring *)addr;
	BACK_RING_INIT(&vif->ctrl, shared, XEN_PAGE_SIZE);

	err = bind_interdomain_evtchn_to_irq(vif->domid, evtchn);
	if (err < 0)
		goto err_unmap;

	vif->ctrl_irq = err;

	xenvif_init_hash(vif);

	err = request_threaded_irq(vif->ctrl_irq, NULL, xenvif_ctrl_irq_fn,
				   IRQF_ONESHOT, "xen-netback-ctrl", vif);
	if (err) {
		pr_warn("Could not setup irq handler for %s\n", dev->name);
		goto err_deinit;
	}

	return 0;

err_deinit:
	xenvif_deinit_hash(vif);
	unbind_from_irqhandler(vif->ctrl_irq, vif);
	vif->ctrl_irq = 0;

err_unmap:
	xenbus_unmap_ring_vfree(xenvif_to_xenbus_device(vif),
				vif->ctrl.sring);
	vif->ctrl.sring = NULL;

err:
	return err;
}

int xenvif_connect_data(struct xenvif_queue *queue,
			unsigned long tx_ring_ref,
			unsigned long rx_ring_ref,
			unsigned int tx_evtchn,
			unsigned int rx_evtchn)
{
	struct task_struct *task;
	int err = -ENOMEM;

	BUG_ON(queue->tx_irq);
	BUG_ON(queue->task);
	BUG_ON(queue->dealloc_task);

	err = xenvif_map_frontend_data_rings(queue, tx_ring_ref,
					     rx_ring_ref);
	if (err < 0)
		goto err;

	init_waitqueue_head(&queue->wq);
	init_waitqueue_head(&queue->dealloc_wq);
	atomic_set(&queue->inflight_packets, 0);

	netif_napi_add(queue->vif->dev, &queue->napi, xenvif_poll,
			XENVIF_NAPI_WEIGHT);

	if (tx_evtchn == rx_evtchn) {
		/* feature-split-event-channels == 0 */
		err = bind_interdomain_evtchn_to_irqhandler(
			queue->vif->domid, tx_evtchn, xenvif_interrupt, 0,
			queue->name, queue);
		if (err < 0)
			goto err_unmap;
		queue->tx_irq = queue->rx_irq = err;
		disable_irq(queue->tx_irq);
	} else {
		/* feature-split-event-channels == 1 */
		snprintf(queue->tx_irq_name, sizeof(queue->tx_irq_name),
			 "%s-tx", queue->name);
		err = bind_interdomain_evtchn_to_irqhandler(
			queue->vif->domid, tx_evtchn, xenvif_tx_interrupt, 0,
			queue->tx_irq_name, queue);
		if (err < 0)
			goto err_unmap;
		queue->tx_irq = err;
		disable_irq(queue->tx_irq);

		snprintf(queue->rx_irq_name, sizeof(queue->rx_irq_name),
			 "%s-rx", queue->name);
		err = bind_interdomain_evtchn_to_irqhandler(
			queue->vif->domid, rx_evtchn, xenvif_rx_interrupt, 0,
			queue->rx_irq_name, queue);
		if (err < 0)
			goto err_tx_unbind;
		queue->rx_irq = err;
		disable_irq(queue->rx_irq);
	}

	queue->stalled = true;

	task = kthread_create(xenvif_kthread_guest_rx,
			      (void *)queue, "%s-guest-rx", queue->name);
	if (IS_ERR(task)) {
		pr_warn("Could not allocate kthread for %s\n", queue->name);
		err = PTR_ERR(task);
		goto err_rx_unbind;
	}
	queue->task = task;
	get_task_struct(task);

	task = kthread_create(xenvif_dealloc_kthread,
			      (void *)queue, "%s-dealloc", queue->name);
	if (IS_ERR(task)) {
		pr_warn("Could not allocate kthread for %s\n", queue->name);
		err = PTR_ERR(task);
		goto err_rx_unbind;
	}
	queue->dealloc_task = task;

	wake_up_process(queue->task);
	wake_up_process(queue->dealloc_task);

	return 0;

err_rx_unbind:
	unbind_from_irqhandler(queue->rx_irq, queue);
	queue->rx_irq = 0;
err_tx_unbind:
	unbind_from_irqhandler(queue->tx_irq, queue);
	queue->tx_irq = 0;
err_unmap:
	xenvif_unmap_frontend_data_rings(queue);
	netif_napi_del(&queue->napi);
err:
	module_put(THIS_MODULE);
	return err;
}

void xenvif_carrier_off(struct xenvif *vif)
{
	struct net_device *dev = vif->dev;

	rtnl_lock();
	if (test_and_clear_bit(VIF_STATUS_CONNECTED, &vif->status)) {
		netif_carrier_off(dev); /* discard queued packets */
		if (netif_running(dev))
			xenvif_down(vif);
	}
	rtnl_unlock();
}

void xenvif_disconnect_data(struct xenvif *vif)
{
	struct xenvif_queue *queue = NULL;
	unsigned int num_queues = vif->num_queues;
	unsigned int queue_index;

	xenvif_carrier_off(vif);

	for (queue_index = 0; queue_index < num_queues; ++queue_index) {
		queue = &vif->queues[queue_index];

		netif_napi_del(&queue->napi);

		if (queue->task) {
			kthread_stop(queue->task);
			put_task_struct(queue->task);
			queue->task = NULL;
		}

		if (queue->dealloc_task) {
			kthread_stop(queue->dealloc_task);
			queue->dealloc_task = NULL;
		}

		if (queue->tx_irq) {
			if (queue->tx_irq == queue->rx_irq)
				unbind_from_irqhandler(queue->tx_irq, queue);
			else {
				unbind_from_irqhandler(queue->tx_irq, queue);
				unbind_from_irqhandler(queue->rx_irq, queue);
			}
			queue->tx_irq = 0;
		}

		xenvif_unmap_frontend_data_rings(queue);
	}

	xenvif_mcast_addr_list_free(vif);
}

void xenvif_disconnect_ctrl(struct xenvif *vif)
{
	if (vif->ctrl_irq) {
		xenvif_deinit_hash(vif);
		unbind_from_irqhandler(vif->ctrl_irq, vif);
		vif->ctrl_irq = 0;
	}

	if (vif->ctrl.sring) {
		xenbus_unmap_ring_vfree(xenvif_to_xenbus_device(vif),
					vif->ctrl.sring);
		vif->ctrl.sring = NULL;
	}
}

/* Reverse the relevant parts of xenvif_init_queue().
 * Used for queue teardown from xenvif_free(), and on the
 * error handling paths in xenbus.c:connect().
 */
void xenvif_deinit_queue(struct xenvif_queue *queue)
{
	gnttab_free_pages(MAX_PENDING_REQS, queue->mmap_pages);
}

void xenvif_free(struct xenvif *vif)
{
	struct xenvif_queue *queues = vif->queues;
	unsigned int num_queues = vif->num_queues;
	unsigned int queue_index;

	unregister_netdev(vif->dev);
	free_netdev(vif->dev);

	for (queue_index = 0; queue_index < num_queues; ++queue_index)
		xenvif_deinit_queue(&queues[queue_index]);
	vfree(queues);

	module_put(THIS_MODULE);
}
