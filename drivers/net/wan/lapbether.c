// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	"LAPB via ethernet" driver release 001
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
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
 * in lapbeth_new_device is large enough to store the probe device name.
 */
#define MAXLAPBDEV 100

struct lapbethdev {
	struct list_head	node;
	struct net_device	*ethdev;	/* link to ethernet device */
	struct net_device	*axdev;		/* lapbeth device (lapb#) */
	bool			up;
	spinlock_t		up_lock;	/* Protects "up" */
	struct sk_buff_head	rx_queue;
	struct napi_struct	napi;
};

static LIST_HEAD(lapbeth_devices);

static void lapbeth_connected(struct net_device *dev, int reason);
static void lapbeth_disconnected(struct net_device *dev, int reason);

/* ------------------------------------------------------------------------ */

/*	Get the LAPB device for the ethernet device
 */
static struct lapbethdev *lapbeth_get_x25_dev(struct net_device *dev)
{
	struct lapbethdev *lapbeth;

	list_for_each_entry_rcu(lapbeth, &lapbeth_devices, node, lockdep_rtnl_is_held()) {
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

static int lapbeth_napi_poll(struct napi_struct *napi, int budget)
{
	struct lapbethdev *lapbeth = container_of(napi, struct lapbethdev,
						  napi);
	struct sk_buff *skb;
	int processed = 0;

	for (; processed < budget; ++processed) {
		skb = skb_dequeue(&lapbeth->rx_queue);
		if (!skb)
			break;
		netif_receive_skb_core(skb);
	}

	if (processed < budget)
		napi_complete(napi);

	return processed;
}

/*	Receive a LAPB frame via an ethernet interface.
 */
static int lapbeth_rcv(struct sk_buff *skb, struct net_device *dev,
		       struct packet_type *ptype, struct net_device *orig_dev)
{
	int len, err;
	struct lapbethdev *lapbeth;

	if (dev_net(dev) != &init_net)
		goto drop;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		return NET_RX_DROP;

	if (!pskb_may_pull(skb, 2))
		goto drop;

	rcu_read_lock();
	lapbeth = lapbeth_get_x25_dev(dev);
	if (!lapbeth)
		goto drop_unlock_rcu;
	spin_lock_bh(&lapbeth->up_lock);
	if (!lapbeth->up)
		goto drop_unlock;

	len = skb->data[0] + skb->data[1] * 256;
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;

	skb_pull(skb, 2);	/* Remove the length bytes */
	skb_trim(skb, len);	/* Set the length of the data */

	err = lapb_data_received(lapbeth->axdev, skb);
	if (err != LAPB_OK) {
		printk(KERN_DEBUG "lapbether: lapb_data_received err - %d\n", err);
		goto drop_unlock;
	}
out:
	spin_unlock_bh(&lapbeth->up_lock);
	rcu_read_unlock();
	return 0;
drop_unlock:
	kfree_skb(skb);
	goto out;
drop_unlock_rcu:
	rcu_read_unlock();
drop:
	kfree_skb(skb);
	return 0;
}

static int lapbeth_data_indication(struct net_device *dev, struct sk_buff *skb)
{
	struct lapbethdev *lapbeth = netdev_priv(dev);
	unsigned char *ptr;

	if (skb_cow(skb, 1)) {
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	skb_push(skb, 1);

	ptr  = skb->data;
	*ptr = X25_IFACE_DATA;

	skb->protocol = x25_type_trans(skb, dev);

	skb_queue_tail(&lapbeth->rx_queue, skb);
	napi_schedule(&lapbeth->napi);
	return NET_RX_SUCCESS;
}

/*	Send a LAPB frame via an ethernet interface
 */
static netdev_tx_t lapbeth_xmit(struct sk_buff *skb,
				struct net_device *dev)
{
	struct lapbethdev *lapbeth = netdev_priv(dev);
	int err;

	spin_lock_bh(&lapbeth->up_lock);
	if (!lapbeth->up)
		goto drop;

	/* There should be a pseudo header of 1 byte added by upper layers.
	 * Check to make sure it is there before reading it.
	 */
	if (skb->len < 1)
		goto drop;

	switch (skb->data[0]) {
	case X25_IFACE_DATA:
		break;
	case X25_IFACE_CONNECT:
		err = lapb_connect_request(dev);
		if (err == LAPB_CONNECTED)
			lapbeth_connected(dev, LAPB_OK);
		else if (err != LAPB_OK)
			pr_err("lapb_connect_request error: %d\n", err);
		goto drop;
	case X25_IFACE_DISCONNECT:
		err = lapb_disconnect_request(dev);
		if (err == LAPB_NOTCONNECTED)
			lapbeth_disconnected(dev, LAPB_OK);
		else if (err != LAPB_OK)
			pr_err("lapb_disconnect_request err: %d\n", err);
		fallthrough;
	default:
		goto drop;
	}

	skb_pull(skb, 1);

	err = lapb_data_request(dev, skb);
	if (err != LAPB_OK) {
		pr_err("lapb_data_request error - %d\n", err);
		goto drop;
	}
out:
	spin_unlock_bh(&lapbeth->up_lock);
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

	ptr = skb_push(skb, 2);

	*ptr++ = size % 256;
	*ptr++ = size / 256;

	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += size;

	skb->dev = dev = lapbeth->ethdev;

	skb->protocol = htons(ETH_P_DEC);

	skb_reset_network_header(skb);

	dev_hard_header(skb, dev, ETH_P_DEC, bcast_addr, NULL, 0);

	dev_queue_xmit(skb);
}

static void lapbeth_connected(struct net_device *dev, int reason)
{
	struct lapbethdev *lapbeth = netdev_priv(dev);
	unsigned char *ptr;
	struct sk_buff *skb = __dev_alloc_skb(1, GFP_ATOMIC | __GFP_NOMEMALLOC);

	if (!skb)
		return;

	ptr  = skb_put(skb, 1);
	*ptr = X25_IFACE_CONNECT;

	skb->protocol = x25_type_trans(skb, dev);

	skb_queue_tail(&lapbeth->rx_queue, skb);
	napi_schedule(&lapbeth->napi);
}

static void lapbeth_disconnected(struct net_device *dev, int reason)
{
	struct lapbethdev *lapbeth = netdev_priv(dev);
	unsigned char *ptr;
	struct sk_buff *skb = __dev_alloc_skb(1, GFP_ATOMIC | __GFP_NOMEMALLOC);

	if (!skb)
		return;

	ptr  = skb_put(skb, 1);
	*ptr = X25_IFACE_DISCONNECT;

	skb->protocol = x25_type_trans(skb, dev);

	skb_queue_tail(&lapbeth->rx_queue, skb);
	napi_schedule(&lapbeth->napi);
}

/*	Set AX.25 callsign
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

/* open/close a device
 */
static int lapbeth_open(struct net_device *dev)
{
	struct lapbethdev *lapbeth = netdev_priv(dev);
	int err;

	napi_enable(&lapbeth->napi);

	err = lapb_register(dev, &lapbeth_callbacks);
	if (err != LAPB_OK) {
		pr_err("lapb_register error: %d\n", err);
		return -ENODEV;
	}

	spin_lock_bh(&lapbeth->up_lock);
	lapbeth->up = true;
	spin_unlock_bh(&lapbeth->up_lock);

	return 0;
}

static int lapbeth_close(struct net_device *dev)
{
	struct lapbethdev *lapbeth = netdev_priv(dev);
	int err;

	spin_lock_bh(&lapbeth->up_lock);
	lapbeth->up = false;
	spin_unlock_bh(&lapbeth->up_lock);

	err = lapb_unregister(dev);
	if (err != LAPB_OK)
		pr_err("lapb_unregister error: %d\n", err);

	napi_disable(&lapbeth->napi);

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
	dev->needs_free_netdev = true;
	dev->type            = ARPHRD_X25;
	dev->hard_header_len = 0;
	dev->mtu             = 1000;
	dev->addr_len        = 0;
}

/*	Setup a new device.
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

	/* When transmitting data:
	 * first this driver removes a pseudo header of 1 byte,
	 * then the lapb module prepends an LAPB header of at most 3 bytes,
	 * then this driver prepends a length field of 2 bytes,
	 * then the underlying Ethernet device prepends its own header.
	 */
	ndev->needed_headroom = -1 + 3 + 2 + dev->hard_header_len
					   + dev->needed_headroom;
	ndev->needed_tailroom = dev->needed_tailroom;

	lapbeth = netdev_priv(ndev);
	lapbeth->axdev = ndev;

	dev_hold(dev);
	lapbeth->ethdev = dev;

	lapbeth->up = false;
	spin_lock_init(&lapbeth->up_lock);

	skb_queue_head_init(&lapbeth->rx_queue);
	netif_napi_add(ndev, &lapbeth->napi, lapbeth_napi_poll, 16);

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
	goto out;
}

/*	Free a lapb network device.
 */
static void lapbeth_free_device(struct lapbethdev *lapbeth)
{
	dev_put(lapbeth->ethdev);
	list_del_rcu(&lapbeth->node);
	unregister_netdevice(lapbeth->axdev);
}

/*	Handle device status changes.
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
		if (!lapbeth_get_x25_dev(dev))
			lapbeth_new_device(dev);
		break;
	case NETDEV_GOING_DOWN:
		/* ethernet device closes -> close LAPB interface */
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
