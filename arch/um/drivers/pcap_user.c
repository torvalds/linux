/*
 * Copyright (C) 2002 Jeff Dike <jdike@karaya.com>
 * Licensed under the GPL.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pcap.h>
#include <asm/types.h>
#include "net_user.h"
#include "pcap_user.h"
#include "user.h"

#define MAX_PACKET (ETH_MAX_PACKET + ETH_HEADER_OTHER)

#define PCAP_FD(p) (*(int *)(p))

static void pcap_user_init(void *data, void *dev)
{
	struct pcap_data *pri = data;
	pcap_t *p;
	char errors[PCAP_ERRBUF_SIZE];

	p = pcap_open_live(pri->host_if, MAX_PACKET, pri->promisc, 0, errors);
	if(p == NULL){
		printk("pcap_user_init : pcap_open_live failed - '%s'\n", 
		       errors);
		return;
	}

	pri->dev = dev;
	pri->pcap = p;
}

static int pcap_open(void *data)
{
	struct pcap_data *pri = data;
	__u32 netmask;
	int err;

	if(pri->pcap == NULL)
		return(-ENODEV);

	if(pri->filter != NULL){
		err = dev_netmask(pri->dev, &netmask);
		if(err < 0){
			printk("pcap_open : dev_netmask failed\n");
			return(-EIO);
		}

		pri->compiled = um_kmalloc(sizeof(struct bpf_program));
		if(pri->compiled == NULL){
			printk("pcap_open : kmalloc failed\n");
			return(-ENOMEM);
		}
		
		err = pcap_compile(pri->pcap, 
				   (struct bpf_program *) pri->compiled, 
				   pri->filter, pri->optimize, netmask);
		if(err < 0){
			printk("pcap_open : pcap_compile failed - '%s'\n", 
			       pcap_geterr(pri->pcap));
			return(-EIO);
		}

		err = pcap_setfilter(pri->pcap, pri->compiled);
		if(err < 0){
			printk("pcap_open : pcap_setfilter failed - '%s'\n", 
			       pcap_geterr(pri->pcap));
			return(-EIO);
		}
	}
	
	return(PCAP_FD(pri->pcap));
}

static void pcap_remove(void *data)
{
	struct pcap_data *pri = data;

	if(pri->compiled != NULL)
		pcap_freecode(pri->compiled);

	pcap_close(pri->pcap);
}

struct pcap_handler_data {
	char *buffer;
	int len;
};

static void handler(u_char *data, const struct pcap_pkthdr *header, 
		    const u_char *packet)
{
	int len;

	struct pcap_handler_data *hdata = (struct pcap_handler_data *) data;

	len = hdata->len < header->caplen ? hdata->len : header->caplen;
	memcpy(hdata->buffer, packet, len);
	hdata->len = len;
}

int pcap_user_read(int fd, void *buffer, int len, struct pcap_data *pri)
{
	struct pcap_handler_data hdata = ((struct pcap_handler_data)
		                          { .buffer  	= buffer,
					    .len 	= len });
	int n;

	n = pcap_dispatch(pri->pcap, 1, handler, (u_char *) &hdata);
	if(n < 0){
		printk("pcap_dispatch failed - %s\n", pcap_geterr(pri->pcap));
		return(-EIO);
	}
	else if(n == 0) 
		return(0);
	return(hdata.len);
}

struct net_user_info pcap_user_info = {
	.init		= pcap_user_init,
	.open		= pcap_open,
	.close	 	= NULL,
	.remove	 	= pcap_remove,
	.set_mtu	= NULL,
	.add_address	= NULL,
	.delete_address = NULL,
	.max_packet	= MAX_PACKET - ETH_HEADER_OTHER
};

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
