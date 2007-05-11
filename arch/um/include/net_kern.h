/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_NET_KERN_H
#define __UM_NET_KERN_H

#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/list.h>
#include <linux/workqueue.h>

struct uml_net {
	struct list_head list;
	struct net_device *dev;
	struct platform_device pdev;
	int index;
	unsigned char mac[ETH_ALEN];
};

struct uml_net_private {
	struct list_head list;
	spinlock_t lock;
	struct net_device *dev;
	struct timer_list tl;
	struct net_device_stats stats;
	struct work_struct work;
	int fd;
	unsigned char mac[ETH_ALEN];
	unsigned short (*protocol)(struct sk_buff *);
	int (*open)(void *);
	void (*close)(int, void *);
	void (*remove)(void *);
	int (*read)(int, struct sk_buff **skb, struct uml_net_private *);
	int (*write)(int, struct sk_buff **skb, struct uml_net_private *);

	void (*add_address)(unsigned char *, unsigned char *, void *);
	void (*delete_address)(unsigned char *, unsigned char *, void *);
	int (*set_mtu)(int mtu, void *);
	char user[0];
};

struct net_kern_info {
	void (*init)(struct net_device *, void *);
	unsigned short (*protocol)(struct sk_buff *);
	int (*read)(int, struct sk_buff **skb, struct uml_net_private *);
	int (*write)(int, struct sk_buff **skb, struct uml_net_private *);
};

struct transport {
	struct list_head list;
	const char *name;
	int (* const setup)(char *, char **, void *);
	const struct net_user_info *user;
	const struct net_kern_info *kern;
	const int private_size;
	const int setup_size;
};

extern struct net_device *ether_init(int);
extern unsigned short ether_protocol(struct sk_buff *);
extern struct sk_buff *ether_adjust_skb(struct sk_buff *skb, int extra);
extern int tap_setup_common(char *str, char *type, char **dev_name,
			    char **mac_out, char **gate_addr);
extern void register_transport(struct transport *new);
extern unsigned short eth_protocol(struct sk_buff *skb);

#endif
