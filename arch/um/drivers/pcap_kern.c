/*
 * Copyright (C) 2002 Jeff Dike <jdike@karaya.com>
 * Licensed under the GPL.
 */

#include "linux/init.h"
#include "linux/netdevice.h"
#include "linux/etherdevice.h"
#include "net_kern.h"
#include "net_user.h"
#include "pcap_user.h"

struct pcap_init {
	char *host_if;
	int promisc;
	int optimize;
	char *filter;
};

void pcap_init(struct net_device *dev, void *data)
{
	struct uml_net_private *pri;
	struct pcap_data *ppri;
	struct pcap_init *init = data;

	pri = dev->priv;
	ppri = (struct pcap_data *) pri->user;
	ppri->host_if = init->host_if;
	ppri->promisc = init->promisc;
	ppri->optimize = init->optimize;
	ppri->filter = init->filter;
}

static int pcap_read(int fd, struct sk_buff **skb, 
		       struct uml_net_private *lp)
{
	*skb = ether_adjust_skb(*skb, ETH_HEADER_OTHER);
	if(*skb == NULL) return(-ENOMEM);
	return(pcap_user_read(fd, (*skb)->mac.raw, 
			      (*skb)->dev->mtu + ETH_HEADER_OTHER,
			      (struct pcap_data *) &lp->user));
}

static int pcap_write(int fd, struct sk_buff **skb, struct uml_net_private *lp)
{
	return(-EPERM);
}

static struct net_kern_info pcap_kern_info = {
	.init			= pcap_init,
	.protocol		= eth_protocol,
	.read			= pcap_read,
	.write			= pcap_write,
};

int pcap_setup(char *str, char **mac_out, void *data)
{
	struct pcap_init *init = data;
	char *remain, *host_if = NULL, *options[2] = { NULL, NULL };
	int i;

	*init = ((struct pcap_init)
		{ .host_if 	= "eth0",
		  .promisc 	= 1,
		  .optimize 	= 0,
		  .filter 	= NULL });

	remain = split_if_spec(str, &host_if, &init->filter, 
			       &options[0], &options[1], NULL);
	if(remain != NULL){
		printk(KERN_ERR "pcap_setup - Extra garbage on "
		       "specification : '%s'\n", remain);
		return(0);
	}

	if(host_if != NULL)
		init->host_if = host_if;

	for(i = 0; i < sizeof(options)/sizeof(options[0]); i++){
		if(options[i] == NULL)
			continue;
		if(!strcmp(options[i], "promisc"))
			init->promisc = 1;
		else if(!strcmp(options[i], "nopromisc"))
			init->promisc = 0;
		else if(!strcmp(options[i], "optimize"))
			init->optimize = 1;
		else if(!strcmp(options[i], "nooptimize"))
			init->optimize = 0;
		else printk("pcap_setup : bad option - '%s'\n", options[i]);
	}

	return(1);
}

static struct transport pcap_transport = {
	.list 		= LIST_HEAD_INIT(pcap_transport.list),
	.name 		= "pcap",
	.setup  	= pcap_setup,
	.user 		= &pcap_user_info,
	.kern 		= &pcap_kern_info,
	.private_size 	= sizeof(struct pcap_data),
	.setup_size 	= sizeof(struct pcap_init),
};

static int register_pcap(void)
{
	register_transport(&pcap_transport);
	return 0;
}

__initcall(register_pcap);
