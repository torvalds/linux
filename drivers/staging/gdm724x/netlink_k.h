/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _NETLINK_K_H
#define _NETLINK_K_H

#include <linux/netdevice.h>
#include <net/sock.h>

struct sock *netlink_init(int unit,
	void (*cb)(struct net_device *dev, u16 type, void *msg, int len));
int netlink_send(struct sock *sock, int group, u16 type, void *msg, int len);

#endif /* _NETLINK_K_H_ */
