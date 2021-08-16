// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic HDLC support routines for Linux
 * X.25 support
 *
 * Copyright (C) 1999 - 2006 Krzysztof Halasa <khc@pm.waw.pl>
 */

#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/hdlc.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/lapb.h>
#include <linux/module.h>
#include <linux/pkt_sched.h>
#include <linux/poll.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <net/x25device.h>

struct x25_state {
	x25_hdlc_proto settings;
	bool up;
	spinlock_t up_lock; /* Protects "up" */
	struct sk_buff_head rx_queue;
	struct tasklet_struct rx_tasklet;
};

static int x25_ioctl(struct net_device *dev, struct ifreq *ifr);

static struct x25_state *state(hdlc_device *hdlc)
{
	return hdlc->state;
}

static void x25_rx_queue_kick(struct tasklet_struct *t)
{
	struct x25_state *x25st = from_tasklet(x25st, t, rx_tasklet);
	struct sk_buff *skb = skb_dequeue(&x25st->rx_queue);

	while (skb) {
		netif_receive_skb_core(skb);
		skb = skb_dequeue(&x25st->rx_queue);
	}
}

/* These functions are callbacks called by LAPB layer */

static void x25_connect_disconnect(struct net_device *dev, int reason, int code)
{
	struct x25_state *x25st = state(dev_to_hdlc(dev));
	struct sk_buff *skb;
	unsigned char *ptr;

	skb = __dev_alloc_skb(1, GFP_ATOMIC | __GFP_NOMEMALLOC);
	if (!skb)
		return;

	ptr = skb_put(skb, 1);
	*ptr = code;

	skb->protocol = x25_type_trans(skb, dev);

	skb_queue_tail(&x25st->rx_queue, skb);
	tasklet_schedule(&x25st->rx_tasklet);
}

static void x25_connected(struct net_device *dev, int reason)
{
	x25_connect_disconnect(dev, reason, X25_IFACE_CONNECT);
}

static void x25_disconnected(struct net_device *dev, int reason)
{
	x25_connect_disconnect(dev, reason, X25_IFACE_DISCONNECT);
}

static int x25_data_indication(struct net_device *dev, struct sk_buff *skb)
{
	struct x25_state *x25st = state(dev_to_hdlc(dev));
	unsigned char *ptr;

	if (skb_cow(skb, 1)) {
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	skb_push(skb, 1);

	ptr  = skb->data;
	*ptr = X25_IFACE_DATA;

	skb->protocol = x25_type_trans(skb, dev);

	skb_queue_tail(&x25st->rx_queue, skb);
	tasklet_schedule(&x25st->rx_tasklet);
	return NET_RX_SUCCESS;
}

static void x25_data_transmit(struct net_device *dev, struct sk_buff *skb)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);

	skb_reset_network_header(skb);
	skb->protocol = hdlc_type_trans(skb, dev);

	if (dev_nit_active(dev))
		dev_queue_xmit_nit(skb, dev);

	hdlc->xmit(skb, dev); /* Ignore return value :-( */
}

static netdev_tx_t x25_xmit(struct sk_buff *skb, struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct x25_state *x25st = state(hdlc);
	int result;

	/* There should be a pseudo header of 1 byte added by upper layers.
	 * Check to make sure it is there before reading it.
	 */
	if (skb->len < 1) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	spin_lock_bh(&x25st->up_lock);
	if (!x25st->up) {
		spin_unlock_bh(&x25st->up_lock);
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	switch (skb->data[0]) {
	case X25_IFACE_DATA:	/* Data to be transmitted */
		skb_pull(skb, 1);
		result = lapb_data_request(dev, skb);
		if (result != LAPB_OK)
			dev_kfree_skb(skb);
		spin_unlock_bh(&x25st->up_lock);
		return NETDEV_TX_OK;

	case X25_IFACE_CONNECT:
		result = lapb_connect_request(dev);
		if (result != LAPB_OK) {
			if (result == LAPB_CONNECTED)
				/* Send connect confirm. msg to level 3 */
				x25_connected(dev, 0);
			else
				netdev_err(dev, "LAPB connect request failed, error code = %i\n",
					   result);
		}
		break;

	case X25_IFACE_DISCONNECT:
		result = lapb_disconnect_request(dev);
		if (result != LAPB_OK) {
			if (result == LAPB_NOTCONNECTED)
				/* Send disconnect confirm. msg to level 3 */
				x25_disconnected(dev, 0);
			else
				netdev_err(dev, "LAPB disconnect request failed, error code = %i\n",
					   result);
		}
		break;

	default:		/* to be defined */
		break;
	}

	spin_unlock_bh(&x25st->up_lock);
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int x25_open(struct net_device *dev)
{
	static const struct lapb_register_struct cb = {
		.connect_confirmation = x25_connected,
		.connect_indication = x25_connected,
		.disconnect_confirmation = x25_disconnected,
		.disconnect_indication = x25_disconnected,
		.data_indication = x25_data_indication,
		.data_transmit = x25_data_transmit,
	};
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct x25_state *x25st = state(hdlc);
	struct lapb_parms_struct params;
	int result;

	result = lapb_register(dev, &cb);
	if (result != LAPB_OK)
		return -ENOMEM;

	result = lapb_getparms(dev, &params);
	if (result != LAPB_OK)
		return -EINVAL;

	if (state(hdlc)->settings.dce)
		params.mode = params.mode | LAPB_DCE;

	if (state(hdlc)->settings.modulo == 128)
		params.mode = params.mode | LAPB_EXTENDED;

	params.window = state(hdlc)->settings.window;
	params.t1 = state(hdlc)->settings.t1;
	params.t2 = state(hdlc)->settings.t2;
	params.n2 = state(hdlc)->settings.n2;

	result = lapb_setparms(dev, &params);
	if (result != LAPB_OK)
		return -EINVAL;

	spin_lock_bh(&x25st->up_lock);
	x25st->up = true;
	spin_unlock_bh(&x25st->up_lock);

	return 0;
}

static void x25_close(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct x25_state *x25st = state(hdlc);

	spin_lock_bh(&x25st->up_lock);
	x25st->up = false;
	spin_unlock_bh(&x25st->up_lock);

	lapb_unregister(dev);
	tasklet_kill(&x25st->rx_tasklet);
}

static int x25_rx(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct x25_state *x25st = state(hdlc);

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb) {
		dev->stats.rx_dropped++;
		return NET_RX_DROP;
	}

	spin_lock_bh(&x25st->up_lock);
	if (!x25st->up) {
		spin_unlock_bh(&x25st->up_lock);
		kfree_skb(skb);
		dev->stats.rx_dropped++;
		return NET_RX_DROP;
	}

	if (lapb_data_received(dev, skb) == LAPB_OK) {
		spin_unlock_bh(&x25st->up_lock);
		return NET_RX_SUCCESS;
	}

	spin_unlock_bh(&x25st->up_lock);
	dev->stats.rx_errors++;
	dev_kfree_skb_any(skb);
	return NET_RX_DROP;
}

static struct hdlc_proto proto = {
	.open		= x25_open,
	.close		= x25_close,
	.ioctl		= x25_ioctl,
	.netif_rx	= x25_rx,
	.xmit		= x25_xmit,
	.module		= THIS_MODULE,
};

static int x25_ioctl(struct net_device *dev, struct ifreq *ifr)
{
	x25_hdlc_proto __user *x25_s = ifr->ifr_settings.ifs_ifsu.x25;
	const size_t size = sizeof(x25_hdlc_proto);
	hdlc_device *hdlc = dev_to_hdlc(dev);
	x25_hdlc_proto new_settings;
	int result;

	switch (ifr->ifr_settings.type) {
	case IF_GET_PROTO:
		if (dev_to_hdlc(dev)->proto != &proto)
			return -EINVAL;
		ifr->ifr_settings.type = IF_PROTO_X25;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		if (copy_to_user(x25_s, &state(hdlc)->settings, size))
			return -EFAULT;
		return 0;

	case IF_PROTO_X25:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (dev->flags & IFF_UP)
			return -EBUSY;

		/* backward compatibility */
		if (ifr->ifr_settings.size == 0) {
			new_settings.dce = 0;
			new_settings.modulo = 8;
			new_settings.window = 7;
			new_settings.t1 = 3;
			new_settings.t2 = 1;
			new_settings.n2 = 10;
		} else {
			if (copy_from_user(&new_settings, x25_s, size))
				return -EFAULT;

			if ((new_settings.dce != 0 &&
			     new_settings.dce != 1) ||
			    (new_settings.modulo != 8 &&
			     new_settings.modulo != 128) ||
			    new_settings.window < 1 ||
			    (new_settings.modulo == 8 &&
			     new_settings.window > 7) ||
			    (new_settings.modulo == 128 &&
			     new_settings.window > 127) ||
			    new_settings.t1 < 1 ||
			    new_settings.t1 > 255 ||
			    new_settings.t2 < 1 ||
			    new_settings.t2 > 255 ||
			    new_settings.n2 < 1 ||
			    new_settings.n2 > 255)
				return -EINVAL;
		}

		result = hdlc->attach(dev, ENCODING_NRZ,
				      PARITY_CRC16_PR1_CCITT);
		if (result)
			return result;

		result = attach_hdlc_protocol(dev, &proto,
					      sizeof(struct x25_state));
		if (result)
			return result;

		memcpy(&state(hdlc)->settings, &new_settings, size);
		state(hdlc)->up = false;
		spin_lock_init(&state(hdlc)->up_lock);
		skb_queue_head_init(&state(hdlc)->rx_queue);
		tasklet_setup(&state(hdlc)->rx_tasklet, x25_rx_queue_kick);

		/* There's no header_ops so hard_header_len should be 0. */
		dev->hard_header_len = 0;
		/* When transmitting data:
		 * first we'll remove a pseudo header of 1 byte,
		 * then we'll prepend an LAPB header of at most 3 bytes.
		 */
		dev->needed_headroom = 3 - 1;

		dev->type = ARPHRD_X25;
		call_netdevice_notifiers(NETDEV_POST_TYPE_CHANGE, dev);
		netif_dormant_off(dev);
		return 0;
	}

	return -EINVAL;
}

static int __init hdlc_x25_init(void)
{
	register_hdlc_protocol(&proto);
	return 0;
}

static void __exit hdlc_x25_exit(void)
{
	unregister_hdlc_protocol(&proto);
}

module_init(hdlc_x25_init);
module_exit(hdlc_x25_exit);

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("X.25 protocol support for generic HDLC");
MODULE_LICENSE("GPL v2");
