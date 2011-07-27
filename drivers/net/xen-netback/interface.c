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

#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>

#include <xen/events.h>
#include <asm/xen/hypercall.h>

#define XENVIF_QUEUE_LENGTH 32

void xenvif_get(struct xenvif *vif)
{
	atomic_inc(&vif->refcnt);
}

void xenvif_put(struct xenvif *vif)
{
	if (atomic_dec_and_test(&vif->refcnt))
		wake_up(&vif->waiting_to_free);
}

int xenvif_schedulable(struct xenvif *vif)
{
	return netif_running(vif->dev) && netif_carrier_ok(vif->dev);
}

static int xenvif_rx_schedulable(struct xenvif *vif)
{
	return xenvif_schedulable(vif) && !xen_netbk_rx_ring_full(vif);
}

static irqreturn_t xenvif_interrupt(int irq, void *dev_id)
{
	struct xenvif *vif = dev_id;

	if (vif->netbk == NULL)
		return IRQ_NONE;

	xen_netbk_schedule_xenvif(vif);

	if (xenvif_rx_schedulable(vif))
		netif_wake_queue(vif->dev);

	return IRQ_HANDLED;
}

static int xenvif_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct xenvif *vif = netdev_priv(dev);

	BUG_ON(skb->dev != dev);

	if (vif->netbk == NULL)
		goto drop;

	/* Drop the packet if the target domain has no receive buffers. */
	if (!xenvif_rx_schedulable(vif))
		goto drop;

	/* Reserve ring slots for the worst-case number of fragments. */
	vif->rx_req_cons_peek += xen_netbk_count_skb_slots(vif, skb);
	xenvif_get(vif);

	if (vif->can_queue && xen_netbk_must_stop_queue(vif))
		netif_stop_queue(dev);

	xen_netbk_queue_tx_skb(vif, skb);

	return NETDEV_TX_OK;

 drop:
	vif->dev->stats.tx_dropped++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

void xenvif_receive_skb(struct xenvif *vif, struct sk_buff *skb)
{
	netif_rx_ni(skb);
}

void xenvif_notify_tx_completion(struct xenvif *vif)
{
	if (netif_queue_stopped(vif->dev) && xenvif_rx_schedulable(vif))
		netif_wake_queue(vif->dev);
}

static struct net_device_stats *xenvif_get_stats(struct net_device *dev)
{
	struct xenvif *vif = netdev_priv(dev);
	return &vif->dev->stats;
}

static void xenvif_up(struct xenvif *vif)
{
	xen_netbk_add_xenvif(vif);
	enable_irq(vif->irq);
	xen_netbk_check_rx_xenvif(vif);
}

static void xenvif_down(struct xenvif *vif)
{
	disable_irq(vif->irq);
	xen_netbk_deschedule_xenvif(vif);
	xen_netbk_remove_xenvif(vif);
}

static int xenvif_open(struct net_device *dev)
{
	struct xenvif *vif = netdev_priv(dev);
	if (netif_carrier_ok(dev))
		xenvif_up(vif);
	netif_start_queue(dev);
	return 0;
}

static int xenvif_close(struct net_device *dev)
{
	struct xenvif *vif = netdev_priv(dev);
	if (netif_carrier_ok(dev))
		xenvif_down(vif);
	netif_stop_queue(dev);
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

static u32 xenvif_fix_features(struct net_device *dev, u32 features)
{
	struct xenvif *vif = netdev_priv(dev);

	if (!vif->can_sg)
		features &= ~NETIF_F_SG;
	if (!vif->gso && !vif->gso_prefix)
		features &= ~NETIF_F_TSO;
	if (!vif->csum)
		features &= ~NETIF_F_IP_CSUM;

	return features;
}

static const struct xenvif_stat {
	char name[ETH_GSTRING_LEN];
	u16 offset;
} xenvif_stats[] = {
	{
		"rx_gso_checksum_fixup",
		offsetof(struct xenvif, rx_gso_checksum_fixup)
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
	void *vif = netdev_priv(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(xenvif_stats); i++)
		data[i] = *(unsigned long *)(vif + xenvif_stats[i].offset);
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

static struct ethtool_ops xenvif_ethtool_ops = {
	.get_link	= ethtool_op_get_link,

	.get_sset_count = xenvif_get_sset_count,
	.get_ethtool_stats = xenvif_get_ethtool_stats,
	.get_strings = xenvif_get_strings,
};

static struct net_device_ops xenvif_netdev_ops = {
	.ndo_start_xmit	= xenvif_start_xmit,
	.ndo_get_stats	= xenvif_get_stats,
	.ndo_open	= xenvif_open,
	.ndo_stop	= xenvif_close,
	.ndo_change_mtu	= xenvif_change_mtu,
	.ndo_fix_features = xenvif_fix_features,
};

struct xenvif *xenvif_alloc(struct device *parent, domid_t domid,
			    unsigned int handle)
{
	int err;
	struct net_device *dev;
	struct xenvif *vif;
	char name[IFNAMSIZ] = {};

	snprintf(name, IFNAMSIZ - 1, "vif%u.%u", domid, handle);
	dev = alloc_netdev(sizeof(struct xenvif), name, ether_setup);
	if (dev == NULL) {
		pr_warn("Could not allocate netdev\n");
		return ERR_PTR(-ENOMEM);
	}

	SET_NETDEV_DEV(dev, parent);

	vif = netdev_priv(dev);
	vif->domid  = domid;
	vif->handle = handle;
	vif->netbk  = NULL;
	vif->can_sg = 1;
	vif->csum = 1;
	atomic_set(&vif->refcnt, 1);
	init_waitqueue_head(&vif->waiting_to_free);
	vif->dev = dev;
	INIT_LIST_HEAD(&vif->schedule_list);
	INIT_LIST_HEAD(&vif->notify_list);

	vif->credit_bytes = vif->remaining_credit = ~0UL;
	vif->credit_usec  = 0UL;
	init_timer(&vif->credit_timeout);
	/* Initialize 'expires' now: it's used to track the credit window. */
	vif->credit_timeout.expires = jiffies;

	dev->netdev_ops	= &xenvif_netdev_ops;
	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO;
	dev->features = dev->hw_features;
	SET_ETHTOOL_OPS(dev, &xenvif_ethtool_ops);

	dev->tx_queue_len = XENVIF_QUEUE_LENGTH;

	/*
	 * Initialise a dummy MAC address. We choose the numerically
	 * largest non-broadcast address to prevent the address getting
	 * stolen by an Ethernet bridge for STP purposes.
	 * (FE:FF:FF:FF:FF:FF)
	 */
	memset(dev->dev_addr, 0xFF, ETH_ALEN);
	dev->dev_addr[0] &= ~0x01;

	netif_carrier_off(dev);

	err = register_netdev(dev);
	if (err) {
		netdev_warn(dev, "Could not register device: err=%d\n", err);
		free_netdev(dev);
		return ERR_PTR(err);
	}

	netdev_dbg(dev, "Successfully created xenvif\n");
	return vif;
}

int xenvif_connect(struct xenvif *vif, unsigned long tx_ring_ref,
		   unsigned long rx_ring_ref, unsigned int evtchn)
{
	int err = -ENOMEM;

	/* Already connected through? */
	if (vif->irq)
		return 0;

	err = xen_netbk_map_frontend_rings(vif, tx_ring_ref, rx_ring_ref);
	if (err < 0)
		goto err;

	err = bind_interdomain_evtchn_to_irqhandler(
		vif->domid, evtchn, xenvif_interrupt, 0,
		vif->dev->name, vif);
	if (err < 0)
		goto err_unmap;
	vif->irq = err;
	disable_irq(vif->irq);

	xenvif_get(vif);

	rtnl_lock();
	if (netif_running(vif->dev))
		xenvif_up(vif);
	if (!vif->can_sg && vif->dev->mtu > ETH_DATA_LEN)
		dev_set_mtu(vif->dev, ETH_DATA_LEN);
	netdev_update_features(vif->dev);
	netif_carrier_on(vif->dev);
	rtnl_unlock();

	return 0;
err_unmap:
	xen_netbk_unmap_frontend_rings(vif);
err:
	return err;
}

void xenvif_disconnect(struct xenvif *vif)
{
	struct net_device *dev = vif->dev;
	if (netif_carrier_ok(dev)) {
		rtnl_lock();
		netif_carrier_off(dev); /* discard queued packets */
		if (netif_running(dev))
			xenvif_down(vif);
		rtnl_unlock();
		xenvif_put(vif);
	}

	atomic_dec(&vif->refcnt);
	wait_event(vif->waiting_to_free, atomic_read(&vif->refcnt) == 0);

	del_timer_sync(&vif->credit_timeout);

	if (vif->irq)
		unbind_from_irqhandler(vif->irq, vif);

	unregister_netdev(vif->dev);

	xen_netbk_unmap_frontend_rings(vif);

	free_netdev(vif->dev);
}
