/*
 * w1_netlink.c
 *
 * Copyright (c) 2003 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/skbuff.h>
#include <linux/netlink.h>

#include "w1.h"
#include "w1_log.h"
#include "w1_netlink.h"

#ifndef NETLINK_DISABLED
void w1_netlink_send(struct w1_master *dev, struct w1_netlink_msg *msg)
{
	unsigned int size;
	struct sk_buff *skb;
	struct w1_netlink_msg *data;
	struct nlmsghdr *nlh;

	if (!dev->nls)
		return;

	size = NLMSG_SPACE(sizeof(struct w1_netlink_msg));

	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb) {
		dev_err(&dev->dev, "skb_alloc() failed.\n");
		return;
	}

	nlh = NLMSG_PUT(skb, 0, dev->seq++, NLMSG_DONE, size - sizeof(*nlh));

	data = (struct w1_netlink_msg *)NLMSG_DATA(nlh);

	memcpy(data, msg, sizeof(struct w1_netlink_msg));

	NETLINK_CB(skb).dst_groups = dev->groups;
	netlink_broadcast(dev->nls, skb, 0, dev->groups, GFP_ATOMIC);

nlmsg_failure:
	return;
}
#else
#warning Netlink support is disabled. Please compile with NET support enabled.

void w1_netlink_send(struct w1_master *dev, struct w1_netlink_msg *msg)
{
}
#endif
