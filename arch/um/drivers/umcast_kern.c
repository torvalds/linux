// SPDX-License-Identifier: GPL-2.0
/*
 * user-mode-linux networking multicast transport
 * Copyright (C) 2001 by Harald Welte <laforge@gnumonks.org>
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 *
 * based on the existing uml-networking code, which is
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org) and
 * James Leu (jleu@mindspring.net).
 * Copyright (C) 2001 by various other people who didn't put their name here.
 *
 */

#include <linux/init.h>
#include <linux/netdevice.h>
#include "umcast.h"
#include <net_kern.h>

struct umcast_init {
	char *addr;
	int lport;
	int rport;
	int ttl;
	bool unicast;
};

static void umcast_init(struct net_device *dev, void *data)
{
	struct uml_net_private *pri;
	struct umcast_data *dpri;
	struct umcast_init *init = data;

	pri = netdev_priv(dev);
	dpri = (struct umcast_data *) pri->user;
	dpri->addr = init->addr;
	dpri->lport = init->lport;
	dpri->rport = init->rport;
	dpri->unicast = init->unicast;
	dpri->ttl = init->ttl;
	dpri->dev = dev;

	if (dpri->unicast) {
		printk(KERN_INFO "ucast backend address: %s:%u listen port: "
		       "%u\n", dpri->addr, dpri->rport, dpri->lport);
	} else {
		printk(KERN_INFO "mcast backend multicast address: %s:%u, "
		       "TTL:%u\n", dpri->addr, dpri->lport, dpri->ttl);
	}
}

static int umcast_read(int fd, struct sk_buff *skb, struct uml_net_private *lp)
{
	return net_recvfrom(fd, skb_mac_header(skb),
			    skb->dev->mtu + ETH_HEADER_OTHER);
}

static int umcast_write(int fd, struct sk_buff *skb, struct uml_net_private *lp)
{
	return umcast_user_write(fd, skb->data, skb->len,
				(struct umcast_data *) &lp->user);
}

static const struct net_kern_info umcast_kern_info = {
	.init			= umcast_init,
	.protocol		= eth_protocol,
	.read			= umcast_read,
	.write			= umcast_write,
};

static int mcast_setup(char *str, char **mac_out, void *data)
{
	struct umcast_init *init = data;
	char *port_str = NULL, *ttl_str = NULL, *remain;
	char *last;

	*init = ((struct umcast_init)
		{ .addr	= "239.192.168.1",
		  .lport	= 1102,
		  .ttl	= 1 });

	remain = split_if_spec(str, mac_out, &init->addr, &port_str, &ttl_str,
			       NULL);
	if (remain != NULL) {
		printk(KERN_ERR "mcast_setup - Extra garbage on "
		       "specification : '%s'\n", remain);
		return 0;
	}

	if (port_str != NULL) {
		init->lport = simple_strtoul(port_str, &last, 10);
		if ((*last != '\0') || (last == port_str)) {
			printk(KERN_ERR "mcast_setup - Bad port : '%s'\n",
			       port_str);
			return 0;
		}
	}

	if (ttl_str != NULL) {
		init->ttl = simple_strtoul(ttl_str, &last, 10);
		if ((*last != '\0') || (last == ttl_str)) {
			printk(KERN_ERR "mcast_setup - Bad ttl : '%s'\n",
			       ttl_str);
			return 0;
		}
	}

	init->unicast = false;
	init->rport = init->lport;

	printk(KERN_INFO "Configured mcast device: %s:%u-%u\n", init->addr,
	       init->lport, init->ttl);

	return 1;
}

static int ucast_setup(char *str, char **mac_out, void *data)
{
	struct umcast_init *init = data;
	char *lport_str = NULL, *rport_str = NULL, *remain;
	char *last;

	*init = ((struct umcast_init)
		{ .addr		= "",
		  .lport	= 1102,
		  .rport	= 1102 });

	remain = split_if_spec(str, mac_out, &init->addr,
			       &lport_str, &rport_str, NULL);
	if (remain != NULL) {
		printk(KERN_ERR "ucast_setup - Extra garbage on "
		       "specification : '%s'\n", remain);
		return 0;
	}

	if (lport_str != NULL) {
		init->lport = simple_strtoul(lport_str, &last, 10);
		if ((*last != '\0') || (last == lport_str)) {
			printk(KERN_ERR "ucast_setup - Bad listen port : "
			       "'%s'\n", lport_str);
			return 0;
		}
	}

	if (rport_str != NULL) {
		init->rport = simple_strtoul(rport_str, &last, 10);
		if ((*last != '\0') || (last == rport_str)) {
			printk(KERN_ERR "ucast_setup - Bad remote port : "
			       "'%s'\n", rport_str);
			return 0;
		}
	}

	init->unicast = true;

	printk(KERN_INFO "Configured ucast device: :%u -> %s:%u\n",
	       init->lport, init->addr, init->rport);

	return 1;
}

static struct transport mcast_transport = {
	.list	= LIST_HEAD_INIT(mcast_transport.list),
	.name	= "mcast",
	.setup	= mcast_setup,
	.user	= &umcast_user_info,
	.kern	= &umcast_kern_info,
	.private_size	= sizeof(struct umcast_data),
	.setup_size	= sizeof(struct umcast_init),
};

static struct transport ucast_transport = {
	.list	= LIST_HEAD_INIT(ucast_transport.list),
	.name	= "ucast",
	.setup	= ucast_setup,
	.user	= &umcast_user_info,
	.kern	= &umcast_kern_info,
	.private_size	= sizeof(struct umcast_data),
	.setup_size	= sizeof(struct umcast_init),
};

static int register_umcast(void)
{
	register_transport(&mcast_transport);
	register_transport(&ucast_transport);
	return 0;
}

late_initcall(register_umcast);
