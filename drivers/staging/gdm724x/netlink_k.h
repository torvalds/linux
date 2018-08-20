/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved. */

#ifndef _NETLINK_K_H
#define _NETLINK_K_H

#include <linux/netdevice.h>
#include <net/sock.h>

struct sock *netlink_init(int unit,
			  void (*cb)(struct net_device *dev,
				     u16 type, void *msg, int len));
int netlink_send(struct sock *sock, int group, u16 type, void *msg, int len);

#endif /* _NETLINK_K_H_ */
