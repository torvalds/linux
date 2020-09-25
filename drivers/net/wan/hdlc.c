// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic HDLC support routines for Linux
 *
 * Copyright (C) 1999 - 2008 Krzysztof Halasa <khc@pm.waw.pl>
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/hdlc.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/pkt_sched.h>
#include <linux/poll.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/net_namespace.h>


static const char* version = "HDLC support module revision 1.22";

#undef DEBUG_LINK

static struct hdlc_proto *first_proto;

static int hdlc_rcv(struct sk_buff *skb, struct net_device *dev,
		    struct packet_type *p, struct net_device *orig_dev)
{
	struct hdlc_device *hdlc = dev_to_hdlc(dev);

	if (!net_eq(dev_net(dev), &init_net)) {
		kfree_skb(skb);
		return 0;
	}

	BUG_ON(!hdlc->proto->netif_rx);
	return hdlc->proto->netif_rx(skb);
}

netdev_tx_t hdlc_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);

	if (hdlc->proto->xmit)
		return hdlc->proto->xmit(skb, dev);

	return hdlc->xmit(skb, dev); /* call hardware driver directly */
}

static inline void hdlc_proto_start(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	if (hdlc->proto->start)
		hdlc->proto->start(dev);
}



static inline void hdlc_proto_stop(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	if (hdlc->proto->stop)
		hdlc->proto->stop(dev);
}



static int hdlc_device_event(struct notifier_block *this, unsigned long event,
			     void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	hdlc_device *hdlc;
	unsigned long flags;
	int on;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	if (!(dev->priv_flags & IFF_WAN_HDLC))
		return NOTIFY_DONE; /* not an HDLC device */

	if (event != NETDEV_CHANGE)
		return NOTIFY_DONE; /* Only interested in carrier changes */

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
		netdev_info(dev, "Carrier detected\n");
		hdlc_proto_start(dev);
	} else {
		netdev_info(dev, "Carrier lost\n");
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
		netdev_info(dev, "Carrier detected\n");
		hdlc_proto_start(dev);
	} else
		netdev_info(dev, "No carrier\n");

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

static const struct header_ops hdlc_null_ops;

static void hdlc_setup_dev(struct net_device *dev)
{
	/* Re-init all variables changed by HDLC protocol drivers,
	 * including ether_setup() called from hdlc_raw_eth.c.
	 */
	dev->flags		 = IFF_POINTOPOINT | IFF_NOARP;
	dev->priv_flags		 = IFF_WAN_HDLC;
	dev->mtu		 = HDLC_MAX_MTU;
	dev->min_mtu		 = 68;
	dev->max_mtu		 = HDLC_MAX_MTU;
	dev->type		 = ARPHRD_RAWHDLC;
	dev->hard_header_len	 = 0;
	dev->needed_headroom	 = 0;
	dev->addr_len		 = 0;
	dev->header_ops		 = &hdlc_null_ops;
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
	dev = alloc_netdev(sizeof(struct hdlc_device), "hdlc%d",
			   NET_NAME_UNKNOWN, hdlc_setup);
	if (dev)
		dev_to_hdlc(dev)->priv = priv;
	return dev;
}

void unregister_hdlc_device(struct net_device *dev)
{
	rtnl_lock();
	detach_hdlc_protocol(dev);
	unregister_netdevice(dev);
	rtnl_unlock();
}



int attach_hdlc_protocol(struct net_device *dev, struct hdlc_proto *proto,
			 size_t size)
{
	int err;

	err = detach_hdlc_protocol(dev);
	if (err)
		return err;

	if (!try_module_get(proto->module))
		return -ENOSYS;

	if (size) {
		dev_to_hdlc(dev)->state = kmalloc(size, GFP_KERNEL);
		if (dev_to_hdlc(dev)->state == NULL) {
			module_put(proto->module);
			return -ENOBUFS;
		}
	}
	dev_to_hdlc(dev)->proto = proto;

	return 0;
}


int detach_hdlc_protocol(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	int err;

	if (hdlc->proto) {
		err = call_netdevice_notifiers(NETDEV_PRE_TYPE_CHANGE, dev);
		err = notifier_to_errno(err);
		if (err) {
			netdev_err(dev, "Refused to change device type\n");
			return err;
		}

		if (hdlc->proto->detach)
			hdlc->proto->detach(dev);
		module_put(hdlc->proto->module);
		hdlc->proto = NULL;
	}
	kfree(hdlc->state);
	hdlc->state = NULL;
	hdlc_setup_dev(dev);

	return 0;
}


void register_hdlc_protocol(struct hdlc_proto *proto)
{
	rtnl_lock();
	proto->next = first_proto;
	first_proto = proto;
	rtnl_unlock();
}


void unregister_hdlc_protocol(struct hdlc_proto *proto)
{
	struct hdlc_proto **p;

	rtnl_lock();
	p = &first_proto;
	while (*p != proto) {
		BUG_ON(!*p);
		p = &((*p)->next);
	}
	*p = proto->next;
	rtnl_unlock();
}



MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("HDLC support module");
MODULE_LICENSE("GPL v2");

EXPORT_SYMBOL(hdlc_start_xmit);
EXPORT_SYMBOL(hdlc_open);
EXPORT_SYMBOL(hdlc_close);
EXPORT_SYMBOL(hdlc_ioctl);
EXPORT_SYMBOL(alloc_hdlcdev);
EXPORT_SYMBOL(unregister_hdlc_device);
EXPORT_SYMBOL(register_hdlc_protocol);
EXPORT_SYMBOL(unregister_hdlc_protocol);
EXPORT_SYMBOL(attach_hdlc_protocol);
EXPORT_SYMBOL(detach_hdlc_protocol);

static struct packet_type hdlc_packet_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_HDLC),
	.func = hdlc_rcv,
};


static struct notifier_block hdlc_notifier = {
	.notifier_call = hdlc_device_event,
};


static int __init hdlc_module_init(void)
{
	int result;

	pr_info("%s\n", version);
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
