/*
 * Generic HDLC support routines for Linux
 * Point-to-point protocol support
 *
 * Copyright (C) 1999 - 2006 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
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
#include <linux/hdlc.h>
#include <net/syncppp.h>

struct ppp_state {
	struct ppp_device pppdev;
	struct ppp_device *syncppp_ptr;
	int (*old_change_mtu)(struct net_device *dev, int new_mtu);
};

static int ppp_ioctl(struct net_device *dev, struct ifreq *ifr);


static inline struct ppp_state* state(hdlc_device *hdlc)
{
	return(struct ppp_state *)(hdlc->state);
}


static int ppp_open(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	void *old_ioctl;
	int result;

	dev->priv = &state(hdlc)->syncppp_ptr;
	state(hdlc)->syncppp_ptr = &state(hdlc)->pppdev;
	state(hdlc)->pppdev.dev = dev;

	old_ioctl = dev->do_ioctl;
	state(hdlc)->old_change_mtu = dev->change_mtu;
	sppp_attach(&state(hdlc)->pppdev);
	/* sppp_attach nukes them. We don't need syncppp's ioctl */
	dev->do_ioctl = old_ioctl;
	state(hdlc)->pppdev.sppp.pp_flags &= ~PP_CISCO;
	dev->type = ARPHRD_PPP;
	result = sppp_open(dev);
	if (result) {
		sppp_detach(dev);
		return result;
	}

	return 0;
}



static void ppp_close(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);

	sppp_close(dev);
	sppp_detach(dev);
	dev->rebuild_header = NULL;
	dev->change_mtu = state(hdlc)->old_change_mtu;
	dev->mtu = HDLC_MAX_MTU;
	dev->hard_header_len = 16;
}



static __be16 ppp_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	return __constant_htons(ETH_P_WAN_PPP);
}



static struct hdlc_proto proto = {
	.open		= ppp_open,
	.close		= ppp_close,
	.type_trans	= ppp_type_trans,
	.ioctl		= ppp_ioctl,
	.module		= THIS_MODULE,
};


static int ppp_ioctl(struct net_device *dev, struct ifreq *ifr)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	int result;

	switch (ifr->ifr_settings.type) {
	case IF_GET_PROTO:
		if (dev_to_hdlc(dev)->proto != &proto)
			return -EINVAL;
		ifr->ifr_settings.type = IF_PROTO_PPP;
		return 0; /* return protocol only, no settable parameters */

	case IF_PROTO_PPP:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if(dev->flags & IFF_UP)
			return -EBUSY;

		/* no settable parameters */

		result=hdlc->attach(dev, ENCODING_NRZ,PARITY_CRC16_PR1_CCITT);
		if (result)
			return result;

		result = attach_hdlc_protocol(dev, &proto, NULL,
					      sizeof(struct ppp_state));
		if (result)
			return result;
		dev->hard_start_xmit = hdlc->xmit;
		dev->type = ARPHRD_PPP;
		netif_dormant_off(dev);
		return 0;
	}

	return -EINVAL;
}


static int __init mod_init(void)
{
	register_hdlc_protocol(&proto);
	return 0;
}



static void __exit mod_exit(void)
{
	unregister_hdlc_protocol(&proto);
}


module_init(mod_init);
module_exit(mod_exit);

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("PPP protocol support for generic HDLC");
MODULE_LICENSE("GPL v2");
