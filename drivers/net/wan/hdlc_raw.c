/*
 * Generic HDLC support routines for Linux
 * HDLC support
 *
 * Copyright (C) 1999 - 2003 Krzysztof Halasa <khc@pm.waw.pl>
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


static __be16 raw_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	return __constant_htons(ETH_P_IP);
}



int hdlc_raw_ioctl(struct net_device *dev, struct ifreq *ifr)
{
	raw_hdlc_proto __user *raw_s = ifr->ifr_settings.ifs_ifsu.raw_hdlc;
	const size_t size = sizeof(raw_hdlc_proto);
	raw_hdlc_proto new_settings;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	int result;

	switch (ifr->ifr_settings.type) {
	case IF_GET_PROTO:
		ifr->ifr_settings.type = IF_PROTO_HDLC;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		if (copy_to_user(raw_s, &hdlc->state.raw_hdlc.settings, size))
			return -EFAULT;
		return 0;

	case IF_PROTO_HDLC:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (dev->flags & IFF_UP)
			return -EBUSY;

		if (copy_from_user(&new_settings, raw_s, size))
			return -EFAULT;

		if (new_settings.encoding == ENCODING_DEFAULT)
			new_settings.encoding = ENCODING_NRZ;

		if (new_settings.parity == PARITY_DEFAULT)
			new_settings.parity = PARITY_CRC16_PR1_CCITT;

		result = hdlc->attach(dev, new_settings.encoding,
				      new_settings.parity);
		if (result)
			return result;

		hdlc_proto_detach(hdlc);
		memcpy(&hdlc->state.raw_hdlc.settings, &new_settings, size);
		memset(&hdlc->proto, 0, sizeof(hdlc->proto));

		hdlc->proto.type_trans = raw_type_trans;
		hdlc->proto.id = IF_PROTO_HDLC;
		dev->hard_start_xmit = hdlc->xmit;
		dev->hard_header = NULL;
		dev->type = ARPHRD_RAWHDLC;
		dev->flags = IFF_POINTOPOINT | IFF_NOARP;
		dev->addr_len = 0;
		return 0;
	}

	return -EINVAL;
}
