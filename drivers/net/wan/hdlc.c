/*
 * Generic HDLC support routines for Linux
 *
 * Copyright (C) 1999 - 2006 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * Currently supported:
 *	* raw IP-in-HDLC
 *	* Cisco HDLC
 *	* Frame Relay with ANSI or CCITT LMI (both user and network side)
 *	* PPP
 *	* X.25
 *
 * Use sethdlc utility to set line parameters, protocol and PVCs
 *
 * How does it work:
 * - proto->open(), close(), start(), stop() calls are serialized.
 *   The order is: open, [ start, stop ... ] close ...
 * - proto->start() and stop() are called with spin_lock_irq held.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/pkt_sched.h>
#include <linux/inetdevice.h>
#include <linux/lapb.h>
#include <linux/rtnetlink.h>
#include <linux/notifier.h>
#include <linux/hdlc.h>


static const char* version = "HDLC support module revision 1.21";

#undef DEBUG_LINK

static struct hdlc_proto *first_proto = NULL;


static int hdlc_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > HDLC_MAX_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}



static struct net_device_stats *hdlc_get_stats(struct net_device *dev)
{
	return hdlc_stats(dev);
}



static int hdlc_rcv(struct sk_buff *skb, struct net_device *dev,
		    struct packet_type *p, struct net_device *orig_dev)
{
	struct hdlc_device_desc *desc = dev_to_desc(dev);
	if (desc->netif_rx)
		return desc->netif_rx(skb);

	desc->stats.rx_dropped++; /* Shouldn't happen */
	dev_kfree_skb(skb);
	return NET_RX_DROP;
}



static inline void hdlc_proto_start(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	if (hdlc->proto->start)
		return hdlc->proto->start(dev);
}



static inline void hdlc_proto_stop(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	if (hdlc->proto->stop)
		return hdlc->proto->stop(dev);
}



static int hdlc_device_event(struct notifier_block *this, unsigned long event,
			     void *ptr)
{
	struct net_device *dev = ptr;
	hdlc_device *hdlc;
	unsigned long flags;
	int on;
 
	if (dev->get_stats != hdlc_get_stats)
		return NOTIFY_DONE; /* not an HDLC device */
 
	if (event != NETDEV_CHANGE)
		return NOTIFY_DONE; /* Only interrested in carrier changes */

	on = netif_carrier_ok(dev);

#ifdef DEBUG_LINK
	printk(KERN_DEBUG "%s: hdlc_device_event NETDEV_CHANGE, carrier %i\n",
	       dev->name, on);
#endif

	hdlc = dev_to_hdlc(dev);
	spin_lock_irqsave(&hdlc->state_lock, flags);

	if (hdlc->carrier == on)
		goto carrier_exit; /* no change in DCD line level */

	hdlc->carrier = on;

	if (!hdlc->open)
		goto carrier_exit;

	if (hdlc->carrier) {
		printk(KERN_INFO "%s: Carrier detected\n", dev->name);
		hdlc_proto_start(dev);
	} else {
		printk(KERN_INFO "%s: Carrier lost\n", dev->name);
		hdlc_proto_stop(dev);
	}

carrier_exit:
	spin_unlock_irqrestore(&hdlc->state_lock, flags);
	return NOTIFY_DONE;
}



/* Must be called by hardware driver when HDLC device is being opened */
int hdlc_open(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
#ifdef DEBUG_LINK
	printk(KERN_DEBUG "%s: hdlc_open() carrier %i open %i\n", dev->name,
	       hdlc->carrier, hdlc->open);
#endif

	if (hdlc->proto == NULL)
		return -ENOSYS;	/* no protocol attached */

	if (hdlc->proto->open) {
		int result = hdlc->proto->open(dev);
		if (result)
			return result;
	}

	spin_lock_irq(&hdlc->state_lock);

	if (hdlc->carrier) {
		printk(KERN_INFO "%s: Carrier detected\n", dev->name);
		hdlc_proto_start(dev);
	} else
		printk(KERN_INFO "%s: No carrier\n", dev->name);

	hdlc->open = 1;

	spin_unlock_irq(&hdlc->state_lock);
	return 0;
}



/* Must be called by hardware driver when HDLC device is being closed */
void hdlc_close(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
#ifdef DEBUG_LINK
	printk(KERN_DEBUG "%s: hdlc_close() carrier %i open %i\n", dev->name,
	       hdlc->carrier, hdlc->open);
#endif

	spin_lock_irq(&hdlc->state_lock);

	hdlc->open = 0;
	if (hdlc->carrier)
		hdlc_proto_stop(dev);

	spin_unlock_irq(&hdlc->state_lock);

	if (hdlc->proto->close)
		hdlc->proto->close(dev);
}



int hdlc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct hdlc_proto *proto = first_proto;
	int result;

	if (cmd != SIOCWANDEV)
		return -EINVAL;

	if (dev_to_hdlc(dev)->proto) {
		result = dev_to_hdlc(dev)->proto->ioctl(dev, ifr);
		if (result != -EINVAL)
			return result;
	}

	/* Not handled by currently attached protocol (if any) */

	while (proto) {
		if ((result = proto->ioctl(dev, ifr)) != -EINVAL)
			return result;
		proto = proto->next;
	}
	return -EINVAL;
}

static void hdlc_setup_dev(struct net_device *dev)
{
	/* Re-init all variables changed by HDLC protocol drivers,
	 * including ether_setup() called from hdlc_raw_eth.c.
	 */
	dev->get_stats		 = hdlc_get_stats;
	dev->flags		 = IFF_POINTOPOINT | IFF_NOARP;
	dev->mtu		 = HDLC_MAX_MTU;
	dev->type		 = ARPHRD_RAWHDLC;
	dev->hard_header_len	 = 16;
	dev->addr_len		 = 0;
	dev->hard_header	 = NULL;
	dev->rebuild_header	 = NULL;
	dev->set_mac_address	 = NULL;
	dev->hard_header_cache	 = NULL;
	dev->header_cache_update = NULL;
	dev->change_mtu		 = hdlc_change_mtu;
	dev->hard_header_parse	 = NULL;
}

static void hdlc_setup(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);

	hdlc_setup_dev(dev);
	hdlc->carrier = 1;
	hdlc->open = 0;
	spin_lock_init(&hdlc->state_lock);
}

struct net_device *alloc_hdlcdev(void *priv)
{
	struct net_device *dev;
	dev = alloc_netdev(sizeof(struct hdlc_device_desc) +
			   sizeof(hdlc_device), "hdlc%d", hdlc_setup);
	if (dev)
		dev_to_hdlc(dev)->priv = priv;
	return dev;
}

void unregister_hdlc_device(struct net_device *dev)
{
	rtnl_lock();
	unregister_netdevice(dev);
	detach_hdlc_protocol(dev);
	rtnl_unlock();
}



int attach_hdlc_protocol(struct net_device *dev, struct hdlc_proto *proto,
			 int (*rx)(struct sk_buff *skb), size_t size)
{
	detach_hdlc_protocol(dev);

	if (!try_module_get(proto->module))
		return -ENOSYS;

	if (size)
		if ((dev_to_hdlc(dev)->state = kmalloc(size,
						       GFP_KERNEL)) == NULL) {
			printk(KERN_WARNING "Memory squeeze on"
			       " hdlc_proto_attach()\n");
			module_put(proto->module);
			return -ENOBUFS;
		}
	dev_to_hdlc(dev)->proto = proto;
	dev_to_desc(dev)->netif_rx = rx;
	return 0;
}


void detach_hdlc_protocol(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);

	if (hdlc->proto) {
		if (hdlc->proto->detach)
			hdlc->proto->detach(dev);
		module_put(hdlc->proto->module);
		hdlc->proto = NULL;
	}
	kfree(hdlc->state);
	hdlc->state = NULL;
	hdlc_setup_dev(dev);
}


void register_hdlc_protocol(struct hdlc_proto *proto)
{
	proto->next = first_proto;
	first_proto = proto;
}


void unregister_hdlc_protocol(struct hdlc_proto *proto)
{
	struct hdlc_proto **p = &first_proto;
	while (*p) {
		if (*p == proto) {
			*p = proto->next;
			return;
		}
		p = &((*p)->next);
	}
}



MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("HDLC support module");
MODULE_LICENSE("GPL v2");

EXPORT_SYMBOL(hdlc_open);
EXPORT_SYMBOL(hdlc_close);
EXPORT_SYMBOL(hdlc_ioctl);
EXPORT_SYMBOL(alloc_hdlcdev);
EXPORT_SYMBOL(unregister_hdlc_device);
EXPORT_SYMBOL(register_hdlc_protocol);
EXPORT_SYMBOL(unregister_hdlc_protocol);
EXPORT_SYMBOL(attach_hdlc_protocol);
EXPORT_SYMBOL(detach_hdlc_protocol);

static struct packet_type hdlc_packet_type = {
	.type = __constant_htons(ETH_P_HDLC),
	.func = hdlc_rcv,
};


static struct notifier_block hdlc_notifier = {
        .notifier_call = hdlc_device_event,
};


static int __init hdlc_module_init(void)
{
	int result;

	printk(KERN_INFO "%s\n", version);
	if ((result = register_netdevice_notifier(&hdlc_notifier)) != 0)
                return result;
        dev_add_pack(&hdlc_packet_type);
	return 0;
}



static void __exit hdlc_module_exit(void)
{
	dev_remove_pack(&hdlc_packet_type);
	unregister_netdevice_notifier(&hdlc_notifier);
}


module_init(hdlc_module_init);
module_exit(hdlc_module_exit);
