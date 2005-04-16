/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/netdevice.h"
#include "linux/etherdevice.h"
#include "linux/skbuff.h"
#include "linux/init.h"
#include "asm/errno.h"
#include "net_kern.h"
#include "net_user.h"
#include "tuntap.h"

struct tuntap_init {
	char *dev_name;
	char *gate_addr;
};

static void tuntap_init(struct net_device *dev, void *data)
{
	struct uml_net_private *pri;
	struct tuntap_data *tpri;
	struct tuntap_init *init = data;

	pri = dev->priv;
	tpri = (struct tuntap_data *) pri->user;
	tpri->dev_name = init->dev_name;
	tpri->fixed_config = (init->dev_name != NULL);
	tpri->gate_addr = init->gate_addr;
	tpri->fd = -1;
	tpri->dev = dev;

	printk("TUN/TAP backend - ");
	if (tpri->gate_addr != NULL)
		printk("IP = %s", tpri->gate_addr);
	printk("\n");
}

static int tuntap_read(int fd, struct sk_buff **skb, 
		       struct uml_net_private *lp)
{
	*skb = ether_adjust_skb(*skb, ETH_HEADER_OTHER);
	if(*skb == NULL) return(-ENOMEM);
	return(net_read(fd, (*skb)->mac.raw, 
			(*skb)->dev->mtu + ETH_HEADER_OTHER));
}

static int tuntap_write(int fd, struct sk_buff **skb, 
			struct uml_net_private *lp)
{
	return(net_write(fd, (*skb)->data, (*skb)->len));
}

struct net_kern_info tuntap_kern_info = {
	.init			= tuntap_init,
	.protocol		= eth_protocol,
	.read			= tuntap_read,
	.write 			= tuntap_write,
};

int tuntap_setup(char *str, char **mac_out, void *data)
{
	struct tuntap_init *init = data;

	*init = ((struct tuntap_init)
		{ .dev_name 	= NULL,
		  .gate_addr 	= NULL });
	if(tap_setup_common(str, "tuntap", &init->dev_name, mac_out,
			    &init->gate_addr))
		return(0);

	return(1);
}

static struct transport tuntap_transport = {
	.list 		= LIST_HEAD_INIT(tuntap_transport.list),
	.name 		= "tuntap",
	.setup  	= tuntap_setup,
	.user 		= &tuntap_user_info,
	.kern 		= &tuntap_kern_info,
	.private_size 	= sizeof(struct tuntap_data),
	.setup_size 	= sizeof(struct tuntap_init),
};

static int register_tuntap(void)
{
	register_transport(&tuntap_transport);
	return(1);
}

__initcall(register_tuntap);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
