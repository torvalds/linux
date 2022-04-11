// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org) and
 * James Leu (jleu@mindspring.net).
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright (C) 2001 by various other people who didn't put their name here.
 */

#include <linux/init.h>
#include <linux/netdevice.h>
#include "etap.h"
#include <net_kern.h>

struct ethertap_init {
	char *dev_name;
	char *gate_addr;
};

static void etap_init(struct net_device *dev, void *data)
{
	struct uml_net_private *pri;
	struct ethertap_data *epri;
	struct ethertap_init *init = data;

	pri = netdev_priv(dev);
	epri = (struct ethertap_data *) pri->user;
	epri->dev_name = init->dev_name;
	epri->gate_addr = init->gate_addr;
	epri->data_fd = -1;
	epri->control_fd = -1;
	epri->dev = dev;

	printk(KERN_INFO "ethertap backend - %s", epri->dev_name);
	if (epri->gate_addr != NULL)
		printk(KERN_CONT ", IP = %s", epri->gate_addr);
	printk(KERN_CONT "\n");
}

static int etap_read(int fd, struct sk_buff *skb, struct uml_net_private *lp)
{
	int len;

	len = net_recvfrom(fd, skb_mac_header(skb),
			   skb->dev->mtu + 2 + ETH_HEADER_ETHERTAP);
	if (len <= 0)
		return(len);

	skb_pull(skb, 2);
	len -= 2;
	return len;
}

static int etap_write(int fd, struct sk_buff *skb, struct uml_net_private *lp)
{
	skb_push(skb, 2);
	return net_send(fd, skb->data, skb->len);
}

const struct net_kern_info ethertap_kern_info = {
	.init			= etap_init,
	.protocol		= eth_protocol,
	.read			= etap_read,
	.write 			= etap_write,
};

int ethertap_setup(char *str, char **mac_out, void *data)
{
	struct ethertap_init *init = data;

	*init = ((struct ethertap_init)
		{ .dev_name 	= NULL,
		  .gate_addr 	= NULL });
	if (tap_setup_common(str, "ethertap", &init->dev_name, mac_out,
			    &init->gate_addr))
		return 0;
	if (init->dev_name == NULL) {
		printk(KERN_ERR "ethertap_setup : Missing tap device name\n");
		return 0;
	}

	return 1;
}

static struct transport ethertap_transport = {
	.list 		= LIST_HEAD_INIT(ethertap_transport.list),
	.name 		= "ethertap",
	.setup  	= ethertap_setup,
	.user 		= &ethertap_user_info,
	.kern 		= &ethertap_kern_info,
	.private_size 	= sizeof(struct ethertap_data),
	.setup_size 	= sizeof(struct ethertap_init),
};

static int register_ethertap(void)
{
	register_transport(&ethertap_transport);
	return 0;
}

late_initcall(register_ethertap);
