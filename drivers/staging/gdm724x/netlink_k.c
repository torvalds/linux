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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/etherdevice.h>
#include <linux/netlink.h>
#include <asm/byteorder.h>
#include <net/sock.h>

#include "netlink_k.h"

#if defined(DEFINE_MUTEX)
static DEFINE_MUTEX(netlink_mutex);
#else
static struct semaphore netlink_mutex;
#define mutex_lock(x)		down(x)
#define mutex_unlock(x)		up(x)
#endif

#define ND_MAX_GROUP		30
#define ND_IFINDEX_LEN		sizeof(int)
#define ND_NLMSG_SPACE(len)	(NLMSG_SPACE(len) + ND_IFINDEX_LEN)
#define ND_NLMSG_DATA(nlh)	((void *)((char *)NLMSG_DATA(nlh) + \
						  ND_IFINDEX_LEN))
#define ND_NLMSG_S_LEN(len)	(len+ND_IFINDEX_LEN)
#define ND_NLMSG_R_LEN(nlh)	(nlh->nlmsg_len-ND_IFINDEX_LEN)
#define ND_NLMSG_IFIDX(nlh)	NLMSG_DATA(nlh)
#define ND_MAX_MSG_LEN		(1024 * 32)

static void (*rcv_cb)(struct net_device *dev, u16 type, void *msg, int len);

static void netlink_rcv_cb(struct sk_buff *skb)
{
	struct nlmsghdr	*nlh;
	struct net_device *dev;
	u32 mlen;
	void *msg;
	int ifindex;

	if (!rcv_cb) {
		pr_err("nl cb - unregistered\n");
		return;
	}

	if (skb->len < NLMSG_HDRLEN) {
		pr_err("nl cb - invalid skb length\n");
		return;
	}

	nlh = (struct nlmsghdr *)skb->data;

	if (skb->len < nlh->nlmsg_len || nlh->nlmsg_len > ND_MAX_MSG_LEN) {
		pr_err("nl cb - invalid length (%d,%d)\n",
		       skb->len, nlh->nlmsg_len);
		return;
	}

	memcpy(&ifindex, ND_NLMSG_IFIDX(nlh), ND_IFINDEX_LEN);
	msg = ND_NLMSG_DATA(nlh);
	mlen = ND_NLMSG_R_LEN(nlh);

	dev = dev_get_by_index(&init_net, ifindex);
	if (dev) {
		rcv_cb(dev, nlh->nlmsg_type, msg, mlen);
		dev_put(dev);
	} else {
		pr_err("nl cb - dev (%d) not found\n", ifindex);
	}
}

static void netlink_rcv(struct sk_buff *skb)
{
	mutex_lock(&netlink_mutex);
	netlink_rcv_cb(skb);
	mutex_unlock(&netlink_mutex);
}

struct sock *netlink_init(int unit,
	void (*cb)(struct net_device *dev, u16 type, void *msg, int len))
{
	struct sock *sock;
	struct netlink_kernel_cfg cfg = {
		.input  = netlink_rcv,
	};

#if !defined(DEFINE_MUTEX)
	init_MUTEX(&netlink_mutex);
#endif

	sock = netlink_kernel_create(&init_net, unit, &cfg);

	if (sock)
		rcv_cb = cb;

	return sock;
}

void netlink_exit(struct sock *sock)
{
	sock_release(sock->sk_socket);
}

int netlink_send(struct sock *sock, int group, u16 type, void *msg, int len)
{
	static u32 seq;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	int ret = 0;

	if (group > ND_MAX_GROUP)
		return -EINVAL;

	if (!netlink_has_listeners(sock, group+1))
		return -ESRCH;

	skb = alloc_skb(NLMSG_SPACE(len), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	seq++;

	nlh = nlmsg_put(skb, 0, seq, type, len, 0);
	memcpy(NLMSG_DATA(nlh), msg, len);
	NETLINK_CB(skb).portid = 0;
	NETLINK_CB(skb).dst_group = 0;

	ret = netlink_broadcast(sock, skb, 0, group+1, GFP_ATOMIC);
	if (!ret)
		return len;

	if (ret != -ESRCH)
		pr_err("nl broadcast g=%d, t=%d, l=%d, r=%d\n",
		       group, type, len, ret);
	else if (netlink_has_listeners(sock, group+1))
		return -EAGAIN;

	return ret;
}
