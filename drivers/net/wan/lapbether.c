/*
 *	"LAPB via ethernet" driver release 001
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	This is a "pseudo" network driver to allow LAPB over Ethernet.
 *
 *	This driver can use any ethernet destination address, and can be 
 *	limited to accept frames from one dedicated ethernet card only.
 *
 *	History
 *	LAPBETH 001	Jonathan Naylor		Cloned from bpqether.c
 *	2000-10-29	Henner Eisen	lapb_data_indication() return status.
 *	2000-11-14	Henner Eisen	dev_hold/put, NETDEV_GOING_DOWN support
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/lapb.h>
#include <linux/init.h>

#include <net/x25device.h>

static const u8 bcast_addr[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* If this number is made larger, check that the temporary string buffer
 * in lapbeth_new_device is large enough to store the probe device name.*/
#define MAXLAPBDEV 100

struct lapbethdev {
	struct list_head	node;
	struct net_device	*ethdev;	/* link to ethernet device */
	struct net_device	*axdev;		/* lapbeth device (lapb#) */
};

static LIST_HEAD(lapbeth_devices);

/* ------------------------------------------------------------------------ */

/*
 *	Get the LAPB device for the ethernet device
 */
static struct lapbethdev *lapbeth_get_x25_dev(struct net_device *dev)
{
	struct lapbethdev *lapbeth;

	list_for_each_entry_rcu(lapbeth, &lapbeth_devices, node) {
		if (lapbeth->ethdev == dev) 
			return lapbeth;
	}
	return NULL;
}

static __inline__ int dev_is_ethdev(struct net_device *dev)
{
	return dev->type == ARPHRD_ETHER && strncmp(dev->name, "dummy", 5);
}

/* ------------------------------------------------------------------------ */

/*
 *	Receive a LAPB frame via an ethernet interface.
 */
static int lapbeth_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *ptype, struct net_device *orig_dev)
{
	int len, err;
	struct lapbethdev *lapbeth;

	if (dev_net(dev) != &init_net)
		goto drop;

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)
		return NET_RX_DROP;

	if (!pskb_may_pull(skb, 2))
		goto drop;

	rcu_read_lock();
	lapbeth = lapbeth_get_x25_dev(dev);
	if (!lapbeth)
		goto drop_unlock;
	if (!netif_running(lapbeth->axdev))
		goto drop_unlock;

	len = skb->data[0] + skb->data[1] * 256;
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;

	skb_pull(skb, 2);	/* Remove the length bytes */
	skb_trim(skb, len);	/* Set the length of the data */

	if ((err = lapb_data_received(lapbeth->axdev, skb)) != LAPB_OK) {
		printk(KERN_DEBUG "lapbether: lapb_data_received err - %d\n", err);
		goto drop_unlock;
	}
out:
	rcu_read_unlock();
	return 0;
drop_unlock:
	kfree_skb(skb);
	goto out;
drop:
	kfree_skb(skb);
	return 0;
}

static int lapbeth_data_indication(struct net_device *dev, struct sk_buff *skb)
{
	unsigned char *ptr;

	skb_push(skb, 1);

	if (skb_cow(skb, 1))
		return NET_RX_DROP;

	ptr  = skb->data;
	*ptr = X25_IFACE_DATA;

	skb->protocol = x25_type_trans(skb, dev);
	return netif_rx(skb);
}

/*
 *	Send a LAPB frame via an ethernet interface
 */
static netdev_tx_t lapbeth_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	int err;

	/*
	 * Just to be *really* sure not to send anything if the interface
	 * is down, the ethernet device may have gone.
	 */
	if (!netif_running(dev))
		goto drop;

	switch (skb->data[0]) {
	case X25_IFACE_DATA:
		break;
	case X25_IFACE_CONNECT:
		if ((err = lapb_connect_request(dev)) != LAPB_OK)
			pr_err("lapb_connect_request error: %d\n", err);
		goto drop;
	case X25_IFACE_DISCONNECT:
		if ((err = lapb_disconnect_request(dev)) != LAPB_OK)
			pr_err("lapb_disconnect_request err: %d\n", err);
		/* Fall thru */
	default:
		goto drop;
	}

	skb_pull(skb, 1);

	if ((err = lapb_data_request(dev, skb)) != LAPB_OK) {
		pr_err("lapb_data_request error - %d\n", err);
		goto drop;
	}
out:
	return NETDEV_TX_OK;
drop:
	kfree_skb(skb);
	goto out;
}

static void lapbeth_data_transmit(struct net_device *ndev, struct sk_buff *skb)
{
	struct lapbethdev *lapbeth = netdev_priv(ndev);
	unsigned char *ptr;
	struct net_device *dev;
	int size = skb->len;

	skb->protocol = htons(ETH_P_X25);

	ptr = skb_push(skb, 2);

	*ptr++ = size % 256;
	*ptr++ = size / 256;

	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += size;

	skb->dev = dev = lapbeth->ethdev;

	dev_hard_header(skb, dev, ETH_P_DEC, bcast_addr, NULL, 0);

	dev_queue_xmit(skb);
}

static void lapbeth_connected(struct net_device *dev, int reason)
{
	unsigned char *ptr;
	struct sk_buff *skb = dev_alloc_skb(1);

	if (!skb) {
		pr_err("out of memory\n");
		return;
	}

	ptr  = skb_put(skb, 1);
	*ptr = X25_IFACE_CONNECT;

	skb->protocol = x25_type_trans(skb, dev);
	netif_rx(skb);
}

static void lapbeth_disconnected(struct net_device *dev, int reason)
{
	unsigned char *ptr;
	struct sk_buff *skb = dev_alloc_skb(1);

	if (!skb) {
		pr_err("out of memory\n");
		return;
	}

	ptr  = skb_put(skb, 1);
	*ptr = X25_IFACE_DISCONNECT;

	skb->protocol = x25_type_trans(skb, dev);
	netif_rx(skb);
}

/*
 *	Set AX.25 callsign
 */
static int lapbeth_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = addr;
	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);
	return 0;
}


static const struct lapb_register_struct lapbeth_callbacks = {
	.connect_confirmation    = lapbeth_connected,
	.connect_indication      = lapbeth_connected,
	.disconnect_confirmation = lapbeth_disconnected,
	.disconnect_indication   = lapbeth_disconnected,
	.data_indication         = lapbeth_data_indication,
	.data_transmit           = lapbeth_data_transmit,
};

/*
 * open/close a device
 */
static int lapbeth_open(struct net_device *dev)
{
	int err;

	if ((err = lapb_register(dev, &lapbeth_callbacks)) != LAPB_OK) {
		pr_err("lapb_register error: %d\n", err);
		return -ENODEV;
	}

	netif_start_queue(dev);
	return 0;
}

static int lapbeth_close(struct net_device *dev)
{
	int err;

	netif_stop_queue(dev);

	if ((err = lapb_unregister(dev)) != LAPB_OK)
		pr_err("lapb_unregister error: %d\n", err);

	return 0;
}

/* ------------------------------------------------------------------------ */

static const struct net_device_ops lapbeth_netdev_ops = {
	.ndo_open	     = lapbeth_open,
	.ndo_stop	     = lapbeth_close,
	.ndo_start_xmit	     = lapbeth_xmit,
	.ndo_set_mac_address = lapbeth_set_mac_address,
};

static void lapbeth_setup(struct net_device *dev)
{
	dev->netdev_ops	     = &lapbeth_netdev_ops;
	dev->destructor	     = free_netdev;
	dev->type            = ARPHRD_X25;
	dev->hard_header_len = 3;
	dev->mtu             = 1000;
	dev->addr_len        = 0;
}

/*
 *	Setup a new device.
 */
static int lapbeth_new_device(struct net_device *dev)
{
	struct net_device *ndev;
	struct lapbethdev *lapbeth;
	int rc = -ENOMEM;

	ASSERT_RTNL();

	ndev = alloc_netdev(sizeof(*lapbeth), "lapb%d", NET_NAME_UNKNOWN,
			    lapbeth_setup);
	if (!ndev)
		goto out;

	lapbeth = netdev_priv(ndev);
	lapbeth->axdev = ndev;

	dev_hold(dev);
	lapbeth->ethdev = dev;

	rc = -EIO;
	if (register_netdevice(ndev))
		goto fail;

	list_add_rcu(&lapbeth->node, &lapbeth_devices);
	rc = 0;
out:
	return rc;
fail:
	dev_put(dev);
	free_netdev(ndev);
	kfree(lapbeth);
	goto out;
}

/*
 *	Free a lapb network device.
 */
static void lapbeth_free_device(struct lapbethdev *lapbeth)
{
	dev_put(lapbeth->ethdev);
	list_del_rcu(&lapbeth->node);
	unregister_netdevice(lapbeth->axdev);
}

/*
 *	Handle device status changes.
 *
 * Called from notifier with RTNL held.
 */
static int lapbeth_device_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct lapbethdev *lapbeth;
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (dev_net(dev) != &init_net)
		return NOTIFY_DONE;

	if (!dev_is_ethdev(dev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		/* New ethernet device -> new LAPB interface	 */
		if (lapbeth_get_x25_dev(dev) == NULL)
			lapbeth_new_device(dev);
		break;
	case NETDEV_DOWN:	
		/* ethernet device closed -> close LAPB interface */
		lapbeth = lapbeth_get_x25_dev(dev);
		if (lapbeth) 
			dev_close(lapbeth->axdev);
		break;
	case NETDEV_UNREGISTER:
		/* ethernet device disappears -> remove LAPB interface */
		lapbeth = lapbeth_get_x25_dev(dev);
		if (lapbeth)
			lapbeth_free_device(lapbeth);
		break;
	}

	return NOTIFY_DONE;
}

/* ------------------------------------------------------------------------ */

static struct packet_type lapbeth_packet_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_DEC),
	.func = lapbeth_rcv,
};

static struct notifier_block lapbeth_dev_notifier = {
	.notifier_call = lapbeth_device_event,
};

static const char banner[] __initconst =
	KERN_INFO "LAPB Ethernet driver version 0.02\n";

static int __init lapbeth_init_driver(void)
{
	dev_add_pack(&lapbeth_packet_type);

	register_netdevice_notifier(&lapbeth_dev_notifier);

	printk(banner);

	return 0;
}
module_init(lapbeth_init_driver);

static void __exit lapbeth_cleanup_driver(void)
{
	struct lapbethdev *lapbeth;
	struct list_head *entry, *tmp;

	dev_remove_pack(&lapbeth_packet_type);
	unregister_netdevice_notifier(&lapbeth_dev_notifier);

	rtnl_lock();
	list_for_each_safe(entry, tmp, &lapbeth_devices) {
		lapbeth = list_entry(entry, struct lapbethdev, node);

		dev_put(lapbeth->ethdev);
		unregister_netdevice(lapbeth->axdev);
	}
	rtnl_unlock();
}
module_exit(lapbeth_cleanup_driver);

MODULE_AUTHOR("Jonathan Naylor <g4klx@g4klx.demon.co.uk>");
MODULE_DESCRIPTION("The unofficial LAPB over Ethernet driver");
MODULE_LICENSE("GPL");
