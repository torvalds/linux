/*
 * Generic HDLC support routines for Linux
 * Cisco HDLC support
 *
 * Copyright (C) 2000 - 2003 Krzysztof Halasa <khc@pm.waw.pl>
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

#undef DEBUG_HARD_HEADER

#define CISCO_MULTICAST		0x8F	/* Cisco multicast address */
#define CISCO_UNICAST		0x0F	/* Cisco unicast address */
#define CISCO_KEEPALIVE		0x8035	/* Cisco keepalive protocol */
#define CISCO_SYS_INFO		0x2000	/* Cisco interface/system info */
#define CISCO_ADDR_REQ		0	/* Cisco address request */
#define CISCO_ADDR_REPLY	1	/* Cisco address reply */
#define CISCO_KEEPALIVE_REQ	2	/* Cisco keepalive request */


static int cisco_hard_header(struct sk_buff *skb, struct net_device *dev,
			     u16 type, void *daddr, void *saddr,
			     unsigned int len)
{
	hdlc_header *data;
#ifdef DEBUG_HARD_HEADER
	printk(KERN_DEBUG "%s: cisco_hard_header called\n", dev->name);
#endif

	skb_push(skb, sizeof(hdlc_header));
	data = (hdlc_header*)skb->data;
	if (type == CISCO_KEEPALIVE)
		data->address = CISCO_MULTICAST;
	else
		data->address = CISCO_UNICAST;
	data->control = 0;
	data->protocol = htons(type);

	return sizeof(hdlc_header);
}



static void cisco_keepalive_send(struct net_device *dev, u32 type,
				 u32 par1, u32 par2)
{
	struct sk_buff *skb;
	cisco_packet *data;

	skb = dev_alloc_skb(sizeof(hdlc_header) + sizeof(cisco_packet));
	if (!skb) {
		printk(KERN_WARNING
		       "%s: Memory squeeze on cisco_keepalive_send()\n",
		       dev->name);
		return;
	}
	skb_reserve(skb, 4);
	cisco_hard_header(skb, dev, CISCO_KEEPALIVE, NULL, NULL, 0);
	data = (cisco_packet*)(skb->data + 4);

	data->type = htonl(type);
	data->par1 = htonl(par1);
	data->par2 = htonl(par2);
	data->rel = 0xFFFF;
	/* we will need do_div here if 1000 % HZ != 0 */
	data->time = htonl((jiffies - INITIAL_JIFFIES) * (1000 / HZ));

	skb_put(skb, sizeof(cisco_packet));
	skb->priority = TC_PRIO_CONTROL;
	skb->dev = dev;
	skb->nh.raw = skb->data;

	dev_queue_xmit(skb);
}



static __be16 cisco_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	hdlc_header *data = (hdlc_header*)skb->data;

	if (skb->len < sizeof(hdlc_header))
		return __constant_htons(ETH_P_HDLC);

	if (data->address != CISCO_MULTICAST &&
	    data->address != CISCO_UNICAST)
		return __constant_htons(ETH_P_HDLC);

	switch(data->protocol) {
	case __constant_htons(ETH_P_IP):
	case __constant_htons(ETH_P_IPX):
	case __constant_htons(ETH_P_IPV6):
		skb_pull(skb, sizeof(hdlc_header));
		return data->protocol;
	default:
		return __constant_htons(ETH_P_HDLC);
	}
}


static int cisco_rx(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	hdlc_header *data = (hdlc_header*)skb->data;
	cisco_packet *cisco_data;
	struct in_device *in_dev;
	u32 addr, mask;

	if (skb->len < sizeof(hdlc_header))
		goto rx_error;

	if (data->address != CISCO_MULTICAST &&
	    data->address != CISCO_UNICAST)
		goto rx_error;

	switch(ntohs(data->protocol)) {
	case CISCO_SYS_INFO:
		/* Packet is not needed, drop it. */
		dev_kfree_skb_any(skb);
		return NET_RX_SUCCESS;

	case CISCO_KEEPALIVE:
		if (skb->len != sizeof(hdlc_header) + CISCO_PACKET_LEN &&
		    skb->len != sizeof(hdlc_header) + CISCO_BIG_PACKET_LEN) {
			printk(KERN_INFO "%s: Invalid length of Cisco "
			       "control packet (%d bytes)\n",
			       dev->name, skb->len);
			goto rx_error;
		}

		cisco_data = (cisco_packet*)(skb->data + sizeof(hdlc_header));

		switch(ntohl (cisco_data->type)) {
		case CISCO_ADDR_REQ: /* Stolen from syncppp.c :-) */
			in_dev = dev->ip_ptr;
			addr = 0;
			mask = ~0; /* is the mask correct? */

			if (in_dev != NULL) {
				struct in_ifaddr **ifap = &in_dev->ifa_list;

				while (*ifap != NULL) {
					if (strcmp(dev->name,
						   (*ifap)->ifa_label) == 0) {
						addr = (*ifap)->ifa_local;
						mask = (*ifap)->ifa_mask;
						break;
					}
					ifap = &(*ifap)->ifa_next;
				}

				cisco_keepalive_send(dev, CISCO_ADDR_REPLY,
						     addr, mask);
			}
			dev_kfree_skb_any(skb);
			return NET_RX_SUCCESS;

		case CISCO_ADDR_REPLY:
			printk(KERN_INFO "%s: Unexpected Cisco IP address "
			       "reply\n", dev->name);
			goto rx_error;

		case CISCO_KEEPALIVE_REQ:
			hdlc->state.cisco.rxseq = ntohl(cisco_data->par1);
			if (hdlc->state.cisco.request_sent &&
			    ntohl(cisco_data->par2)==hdlc->state.cisco.txseq) {
				hdlc->state.cisco.last_poll = jiffies;
				if (!hdlc->state.cisco.up) {
					u32 sec, min, hrs, days;
					sec = ntohl(cisco_data->time) / 1000;
					min = sec / 60; sec -= min * 60;
					hrs = min / 60; min -= hrs * 60;
					days = hrs / 24; hrs -= days * 24;
					printk(KERN_INFO "%s: Link up (peer "
					       "uptime %ud%uh%um%us)\n",
					       dev->name, days, hrs,
					       min, sec);
#if 0
					netif_carrier_on(dev);
#endif
					hdlc->state.cisco.up = 1;
				}
			}

			dev_kfree_skb_any(skb);
			return NET_RX_SUCCESS;
		} /* switch(keepalive type) */
	} /* switch(protocol) */

	printk(KERN_INFO "%s: Unsupported protocol %x\n", dev->name,
	       data->protocol);
	dev_kfree_skb_any(skb);
	return NET_RX_DROP;

 rx_error:
	hdlc->stats.rx_errors++; /* Mark error */
	dev_kfree_skb_any(skb);
	return NET_RX_DROP;
}



static void cisco_timer(unsigned long arg)
{
	struct net_device *dev = (struct net_device *)arg;
	hdlc_device *hdlc = dev_to_hdlc(dev);

	if (hdlc->state.cisco.up &&
	    time_after(jiffies, hdlc->state.cisco.last_poll +
		       hdlc->state.cisco.settings.timeout * HZ)) {
		hdlc->state.cisco.up = 0;
		printk(KERN_INFO "%s: Link down\n", dev->name);
#if 0
		netif_carrier_off(dev);
#endif
	}

	cisco_keepalive_send(dev, CISCO_KEEPALIVE_REQ,
			     ++hdlc->state.cisco.txseq,
			     hdlc->state.cisco.rxseq);
	hdlc->state.cisco.request_sent = 1;
	hdlc->state.cisco.timer.expires = jiffies +
		hdlc->state.cisco.settings.interval * HZ;
	hdlc->state.cisco.timer.function = cisco_timer;
	hdlc->state.cisco.timer.data = arg;
	add_timer(&hdlc->state.cisco.timer);
}



static void cisco_start(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	hdlc->state.cisco.up = 0;
	hdlc->state.cisco.request_sent = 0;
	hdlc->state.cisco.txseq = hdlc->state.cisco.rxseq = 0;

	init_timer(&hdlc->state.cisco.timer);
	hdlc->state.cisco.timer.expires = jiffies + HZ; /*First poll after 1s*/
	hdlc->state.cisco.timer.function = cisco_timer;
	hdlc->state.cisco.timer.data = (unsigned long)dev;
	add_timer(&hdlc->state.cisco.timer);
}



static void cisco_stop(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	del_timer_sync(&hdlc->state.cisco.timer);
#if 0
	if (netif_carrier_ok(dev))
		netif_carrier_off(dev);
#endif
	hdlc->state.cisco.up = 0;
	hdlc->state.cisco.request_sent = 0;
}



int hdlc_cisco_ioctl(struct net_device *dev, struct ifreq *ifr)
{
	cisco_proto __user *cisco_s = ifr->ifr_settings.ifs_ifsu.cisco;
	const size_t size = sizeof(cisco_proto);
	cisco_proto new_settings;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	int result;

	switch (ifr->ifr_settings.type) {
	case IF_GET_PROTO:
		ifr->ifr_settings.type = IF_PROTO_CISCO;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		if (copy_to_user(cisco_s, &hdlc->state.cisco.settings, size))
			return -EFAULT;
		return 0;

	case IF_PROTO_CISCO:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if(dev->flags & IFF_UP)
			return -EBUSY;

		if (copy_from_user(&new_settings, cisco_s, size))
			return -EFAULT;

		if (new_settings.interval < 1 ||
		    new_settings.timeout < 2)
			return -EINVAL;

		result=hdlc->attach(dev, ENCODING_NRZ,PARITY_CRC16_PR1_CCITT);

		if (result)
			return result;

		hdlc_proto_detach(hdlc);
		memcpy(&hdlc->state.cisco.settings, &new_settings, size);
		memset(&hdlc->proto, 0, sizeof(hdlc->proto));

		hdlc->proto.start = cisco_start;
		hdlc->proto.stop = cisco_stop;
		hdlc->proto.netif_rx = cisco_rx;
		hdlc->proto.type_trans = cisco_type_trans;
		hdlc->proto.id = IF_PROTO_CISCO;
		dev->hard_start_xmit = hdlc->xmit;
		dev->hard_header = cisco_hard_header;
		dev->hard_header_cache = NULL;
		dev->type = ARPHRD_CISCO;
		dev->flags = IFF_POINTOPOINT | IFF_NOARP;
		dev->addr_len = 0;
		return 0;
	}

	return -EINVAL;
}
