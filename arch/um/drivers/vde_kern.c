// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Luca Bigliardi (shammash@artha.org).
 *
 * Transport usage:
 *  ethN=vde,<vde_switch>,<mac addr>,<port>,<group>,<mode>,<description>
 *
 */

#include <linux/init.h>
#include <linux/netdevice.h>
#include <net_kern.h>
#include <net_user.h>
#include "vde.h"

static void vde_init(struct net_device *dev, void *data)
{
	struct vde_init *init = data;
	struct uml_net_private *pri;
	struct vde_data *vpri;

	pri = netdev_priv(dev);
	vpri = (struct vde_data *) pri->user;

	vpri->vde_switch = init->vde_switch;
	vpri->descr = init->descr ? init->descr : "UML vde_transport";
	vpri->args = NULL;
	vpri->conn = NULL;
	vpri->dev = dev;

	printk("vde backend - %s, ", vpri->vde_switch ?
	       vpri->vde_switch : "(default socket)");

	vde_init_libstuff(vpri, init);

	printk("\n");
}

static int vde_read(int fd, struct sk_buff *skb, struct uml_net_private *lp)
{
	struct vde_data *pri = (struct vde_data *) &lp->user;

	if (pri->conn != NULL)
		return vde_user_read(pri->conn, skb_mac_header(skb),
				     skb->dev->mtu + ETH_HEADER_OTHER);

	printk(KERN_ERR "vde_read - we have no VDECONN to read from");
	return -EBADF;
}

static int vde_write(int fd, struct sk_buff *skb, struct uml_net_private *lp)
{
	struct vde_data *pri = (struct vde_data *) &lp->user;

	if (pri->conn != NULL)
		return vde_user_write((void *)pri->conn, skb->data,
				      skb->len);

	printk(KERN_ERR "vde_write - we have no VDECONN to write to");
	return -EBADF;
}

static const struct net_kern_info vde_kern_info = {
	.init			= vde_init,
	.protocol		= eth_protocol,
	.read			= vde_read,
	.write			= vde_write,
};

static int vde_setup(char *str, char **mac_out, void *data)
{
	struct vde_init *init = data;
	char *remain, *port_str = NULL, *mode_str = NULL, *last;

	*init = ((struct vde_init)
		{ .vde_switch		= NULL,
		  .descr		= NULL,
		  .port			= 0,
		  .group		= NULL,
		  .mode			= 0 });

	remain = split_if_spec(str, &init->vde_switch, mac_out, &port_str,
				&init->group, &mode_str, &init->descr, NULL);

	if (remain != NULL)
		printk(KERN_WARNING "vde_setup - Ignoring extra data :"
		       "'%s'\n", remain);

	if (port_str != NULL) {
		init->port = simple_strtoul(port_str, &last, 10);
		if ((*last != '\0') || (last == port_str)) {
			printk(KERN_ERR "vde_setup - Bad port : '%s'\n",
						port_str);
			return 0;
		}
	}

	if (mode_str != NULL) {
		init->mode = simple_strtoul(mode_str, &last, 8);
		if ((*last != '\0') || (last == mode_str)) {
			printk(KERN_ERR "vde_setup - Bad mode : '%s'\n",
						mode_str);
			return 0;
		}
	}

	printk(KERN_INFO "Configured vde device: %s\n", init->vde_switch ?
	       init->vde_switch : "(default socket)");

	return 1;
}

static struct transport vde_transport = {
	.list 		= LIST_HEAD_INIT(vde_transport.list),
	.name 		= "vde",
	.setup  	= vde_setup,
	.user 		= &vde_user_info,
	.kern 		= &vde_kern_info,
	.private_size 	= sizeof(struct vde_data),
	.setup_size 	= sizeof(struct vde_init),
};

static int register_vde(void)
{
	register_transport(&vde_transport);
	return 0;
}

late_initcall(register_vde);
