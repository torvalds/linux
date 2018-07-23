/*
 * Copyright (c) 2005 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2002-2005, Network Appliance, Inc. All rights reserved.
 * Copyright (c) 1999-2005, Mellanox Technologies, Inc. All rights reserved.
 * Copyright (c) 2005 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/mutex.h>
#include <linux/inetdevice.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <net/arp.h>
#include <net/neighbour.h>
#include <net/route.h>
#include <net/netevent.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <rdma/ib_addr.h>
#include <rdma/ib.h>
#include <rdma/rdma_netlink.h>
#include <net/netlink.h>

#include "core_priv.h"

struct addr_req {
	struct list_head list;
	struct sockaddr_storage src_addr;
	struct sockaddr_storage dst_addr;
	struct rdma_dev_addr *addr;
	void *context;
	void (*callback)(int status, struct sockaddr *src_addr,
			 struct rdma_dev_addr *addr, void *context);
	unsigned long timeout;
	struct delayed_work work;
	int status;
	u32 seq;
};

static atomic_t ib_nl_addr_request_seq = ATOMIC_INIT(0);

static DEFINE_SPINLOCK(lock);
static LIST_HEAD(req_list);
static struct workqueue_struct *addr_wq;

static const struct nla_policy ib_nl_addr_policy[LS_NLA_TYPE_MAX] = {
	[LS_NLA_TYPE_DGID] = {.type = NLA_BINARY,
		.len = sizeof(struct rdma_nla_ls_gid)},
};

static inline bool ib_nl_is_good_ip_resp(const struct nlmsghdr *nlh)
{
	struct nlattr *tb[LS_NLA_TYPE_MAX] = {};
	int ret;

	if (nlh->nlmsg_flags & RDMA_NL_LS_F_ERR)
		return false;

	ret = nla_parse(tb, LS_NLA_TYPE_MAX - 1, nlmsg_data(nlh),
			nlmsg_len(nlh), ib_nl_addr_policy, NULL);
	if (ret)
		return false;

	return true;
}

static void ib_nl_process_good_ip_rsep(const struct nlmsghdr *nlh)
{
	const struct nlattr *head, *curr;
	union ib_gid gid;
	struct addr_req *req;
	int len, rem;
	int found = 0;

	head = (const struct nlattr *)nlmsg_data(nlh);
	len = nlmsg_len(nlh);

	nla_for_each_attr(curr, head, len, rem) {
		if (curr->nla_type == LS_NLA_TYPE_DGID)
			memcpy(&gid, nla_data(curr), nla_len(curr));
	}

	spin_lock_bh(&lock);
	list_for_each_entry(req, &req_list, list) {
		if (nlh->nlmsg_seq != req->seq)
			continue;
		/* We set the DGID part, the rest was set earlier */
		rdma_addr_set_dgid(req->addr, &gid);
		req->status = 0;
		found = 1;
		break;
	}
	spin_unlock_bh(&lock);

	if (!found)
		pr_info("Couldn't find request waiting for DGID: %pI6\n",
			&gid);
}

int ib_nl_handle_ip_res_resp(struct sk_buff *skb,
			     struct nlmsghdr *nlh,
			     struct netlink_ext_ack *extack)
{
	if ((nlh->nlmsg_flags & NLM_F_REQUEST) ||
	    !(NETLINK_CB(skb).sk))
		return -EPERM;

	if (ib_nl_is_good_ip_resp(nlh))
		ib_nl_process_good_ip_rsep(nlh);

	return skb->len;
}

static int ib_nl_ip_send_msg(struct rdma_dev_addr *dev_addr,
			     const void *daddr,
			     u32 seq, u16 family)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	struct rdma_ls_ip_resolve_header *header;
	void *data;
	size_t size;
	int attrtype;
	int len;

	if (family == AF_INET) {
		size = sizeof(struct in_addr);
		attrtype = RDMA_NLA_F_MANDATORY | LS_NLA_TYPE_IPV4;
	} else {
		size = sizeof(struct in6_addr);
		attrtype = RDMA_NLA_F_MANDATORY | LS_NLA_TYPE_IPV6;
	}

	len = nla_total_size(sizeof(size));
	len += NLMSG_ALIGN(sizeof(*header));

	skb = nlmsg_new(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	data = ibnl_put_msg(skb, &nlh, seq, 0, RDMA_NL_LS,
			    RDMA_NL_LS_OP_IP_RESOLVE, NLM_F_REQUEST);
	if (!data) {
		nlmsg_free(skb);
		return -ENODATA;
	}

	/* Construct the family header first */
	header = skb_put(skb, NLMSG_ALIGN(sizeof(*header)));
	header->ifindex = dev_addr->bound_dev_if;
	nla_put(skb, attrtype, size, daddr);

	/* Repair the nlmsg header length */
	nlmsg_end(skb, nlh);
	rdma_nl_multicast(skb, RDMA_NL_GROUP_LS, GFP_KERNEL);

	/* Make the request retry, so when we get the response from userspace
	 * we will have something.
	 */
	return -ENODATA;
}

int rdma_addr_size(struct sockaddr *addr)
{
	switch (addr->sa_family) {
	case AF_INET:
		return sizeof(struct sockaddr_in);
	case AF_INET6:
		return sizeof(struct sockaddr_in6);
	case AF_IB:
		return sizeof(struct sockaddr_ib);
	default:
		return 0;
	}
}
EXPORT_SYMBOL(rdma_addr_size);

int rdma_addr_size_in6(struct sockaddr_in6 *addr)
{
	int ret = rdma_addr_size((struct sockaddr *) addr);

	return ret <= sizeof(*addr) ? ret : 0;
}
EXPORT_SYMBOL(rdma_addr_size_in6);

int rdma_addr_size_kss(struct __kernel_sockaddr_storage *addr)
{
	int ret = rdma_addr_size((struct sockaddr *) addr);

	return ret <= sizeof(*addr) ? ret : 0;
}
EXPORT_SYMBOL(rdma_addr_size_kss);

void rdma_copy_addr(struct rdma_dev_addr *dev_addr,
		    const struct net_device *dev,
		    const unsigned char *dst_dev_addr)
{
	dev_addr->dev_type = dev->type;
	memcpy(dev_addr->src_dev_addr, dev->dev_addr, MAX_ADDR_LEN);
	memcpy(dev_addr->broadcast, dev->broadcast, MAX_ADDR_LEN);
	if (dst_dev_addr)
		memcpy(dev_addr->dst_dev_addr, dst_dev_addr, MAX_ADDR_LEN);
	dev_addr->bound_dev_if = dev->ifindex;
}
EXPORT_SYMBOL(rdma_copy_addr);

int rdma_translate_ip(const struct sockaddr *addr,
		      struct rdma_dev_addr *dev_addr)
{
	struct net_device *dev;

	if (dev_addr->bound_dev_if) {
		dev = dev_get_by_index(dev_addr->net, dev_addr->bound_dev_if);
		if (!dev)
			return -ENODEV;
		rdma_copy_addr(dev_addr, dev, NULL);
		dev_put(dev);
		return 0;
	}

	switch (addr->sa_family) {
	case AF_INET:
		dev = ip_dev_find(dev_addr->net,
			((const struct sockaddr_in *)addr)->sin_addr.s_addr);

		if (!dev)
			return -EADDRNOTAVAIL;

		rdma_copy_addr(dev_addr, dev, NULL);
		dev_put(dev);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		rcu_read_lock();
		for_each_netdev_rcu(dev_addr->net, dev) {
			if (ipv6_chk_addr(dev_addr->net,
					  &((const struct sockaddr_in6 *)addr)->sin6_addr,
					  dev, 1)) {
				rdma_copy_addr(dev_addr, dev, NULL);
				break;
			}
		}
		rcu_read_unlock();
		break;
#endif
	}
	return 0;
}
EXPORT_SYMBOL(rdma_translate_ip);

static void set_timeout(struct addr_req *req, unsigned long time)
{
	unsigned long delay;

	delay = time - jiffies;
	if ((long)delay < 0)
		delay = 0;

	mod_delayed_work(addr_wq, &req->work, delay);
}

static void queue_req(struct addr_req *req)
{
	spin_lock_bh(&lock);
	list_add_tail(&req->list, &req_list);
	set_timeout(req, req->timeout);
	spin_unlock_bh(&lock);
}

static int ib_nl_fetch_ha(const struct dst_entry *dst,
			  struct rdma_dev_addr *dev_addr,
			  const void *daddr, u32 seq, u16 family)
{
	if (rdma_nl_chk_listeners(RDMA_NL_GROUP_LS))
		return -EADDRNOTAVAIL;

	/* We fill in what we can, the response will fill the rest */
	rdma_copy_addr(dev_addr, dst->dev, NULL);
	return ib_nl_ip_send_msg(dev_addr, daddr, seq, family);
}

static int dst_fetch_ha(const struct dst_entry *dst,
			struct rdma_dev_addr *dev_addr,
			const void *daddr)
{
	struct neighbour *n;
	int ret = 0;

	n = dst_neigh_lookup(dst, daddr);

	rcu_read_lock();
	if (!n || !(n->nud_state & NUD_VALID)) {
		if (n)
			neigh_event_send(n, NULL);
		ret = -ENODATA;
	} else {
		rdma_copy_addr(dev_addr, dst->dev, n->ha);
	}
	rcu_read_unlock();

	if (n)
		neigh_release(n);

	return ret;
}

static bool has_gateway(const struct dst_entry *dst, sa_family_t family)
{
	struct rtable *rt;
	struct rt6_info *rt6;

	if (family == AF_INET) {
		rt = container_of(dst, struct rtable, dst);
		return rt->rt_uses_gateway;
	}

	rt6 = container_of(dst, struct rt6_info, dst);
	return rt6->rt6i_flags & RTF_GATEWAY;
}

static int fetch_ha(const struct dst_entry *dst, struct rdma_dev_addr *dev_addr,
		    const struct sockaddr *dst_in, u32 seq)
{
	const struct sockaddr_in *dst_in4 =
		(const struct sockaddr_in *)dst_in;
	const struct sockaddr_in6 *dst_in6 =
		(const struct sockaddr_in6 *)dst_in;
	const void *daddr = (dst_in->sa_family == AF_INET) ?
		(const void *)&dst_in4->sin_addr.s_addr :
		(const void *)&dst_in6->sin6_addr;
	sa_family_t family = dst_in->sa_family;

	/* Gateway + ARPHRD_INFINIBAND -> IB router */
	if (has_gateway(dst, family) && dst->dev->type == ARPHRD_INFINIBAND)
		return ib_nl_fetch_ha(dst, dev_addr, daddr, seq, family);
	else
		return dst_fetch_ha(dst, dev_addr, daddr);
}

static int addr4_resolve(struct sockaddr_in *src_in,
			 const struct sockaddr_in *dst_in,
			 struct rdma_dev_addr *addr,
			 struct rtable **prt)
{
	__be32 src_ip = src_in->sin_addr.s_addr;
	__be32 dst_ip = dst_in->sin_addr.s_addr;
	struct rtable *rt;
	struct flowi4 fl4;
	int ret;

	memset(&fl4, 0, sizeof(fl4));
	fl4.daddr = dst_ip;
	fl4.saddr = src_ip;
	fl4.flowi4_oif = addr->bound_dev_if;
	rt = ip_route_output_key(addr->net, &fl4);
	ret = PTR_ERR_OR_ZERO(rt);
	if (ret)
		return ret;

	src_in->sin_family = AF_INET;
	src_in->sin_addr.s_addr = fl4.saddr;

	/* If there's a gateway and type of device not ARPHRD_INFINIBAND, we're
	 * definitely in RoCE v2 (as RoCE v1 isn't routable) set the network
	 * type accordingly.
	 */
	if (rt->rt_uses_gateway && rt->dst.dev->type != ARPHRD_INFINIBAND)
		addr->network = RDMA_NETWORK_IPV4;

	addr->hoplimit = ip4_dst_hoplimit(&rt->dst);

	*prt = rt;
	return 0;
}

#if IS_ENABLED(CONFIG_IPV6)
static int addr6_resolve(struct sockaddr_in6 *src_in,
			 const struct sockaddr_in6 *dst_in,
			 struct rdma_dev_addr *addr,
			 struct dst_entry **pdst)
{
	struct flowi6 fl6;
	struct dst_entry *dst;
	struct rt6_info *rt;
	int ret;

	memset(&fl6, 0, sizeof fl6);
	fl6.daddr = dst_in->sin6_addr;
	fl6.saddr = src_in->sin6_addr;
	fl6.flowi6_oif = addr->bound_dev_if;

	ret = ipv6_stub->ipv6_dst_lookup(addr->net, NULL, &dst, &fl6);
	if (ret < 0)
		return ret;

	rt = (struct rt6_info *)dst;
	if (ipv6_addr_any(&src_in->sin6_addr)) {
		src_in->sin6_family = AF_INET6;
		src_in->sin6_addr = fl6.saddr;
	}

	/* If there's a gateway and type of device not ARPHRD_INFINIBAND, we're
	 * definitely in RoCE v2 (as RoCE v1 isn't routable) set the network
	 * type accordingly.
	 */
	if (rt->rt6i_flags & RTF_GATEWAY &&
	    ip6_dst_idev(dst)->dev->type != ARPHRD_INFINIBAND)
		addr->network = RDMA_NETWORK_IPV6;

	addr->hoplimit = ip6_dst_hoplimit(dst);

	*pdst = dst;
	return 0;
}
#else
static int addr6_resolve(struct sockaddr_in6 *src_in,
			 const struct sockaddr_in6 *dst_in,
			 struct rdma_dev_addr *addr,
			 struct dst_entry **pdst)
{
	return -EADDRNOTAVAIL;
}
#endif

static int addr_resolve_neigh(const struct dst_entry *dst,
			      const struct sockaddr *dst_in,
			      struct rdma_dev_addr *addr,
			      u32 seq)
{
	if (dst->dev->flags & IFF_LOOPBACK) {
		int ret;

		ret = rdma_translate_ip(dst_in, addr);
		if (!ret)
			memcpy(addr->dst_dev_addr, addr->src_dev_addr,
			       MAX_ADDR_LEN);

		return ret;
	}

	/* If the device doesn't do ARP internally */
	if (!(dst->dev->flags & IFF_NOARP))
		return fetch_ha(dst, addr, dst_in, seq);

	rdma_copy_addr(addr, dst->dev, NULL);

	return 0;
}

static int addr_resolve(struct sockaddr *src_in,
			const struct sockaddr *dst_in,
			struct rdma_dev_addr *addr,
			bool resolve_neigh,
			u32 seq)
{
	struct net_device *ndev;
	struct dst_entry *dst;
	int ret;

	if (!addr->net) {
		pr_warn_ratelimited("%s: missing namespace\n", __func__);
		return -EINVAL;
	}

	if (src_in->sa_family == AF_INET) {
		struct rtable *rt = NULL;
		const struct sockaddr_in *dst_in4 =
			(const struct sockaddr_in *)dst_in;

		ret = addr4_resolve((struct sockaddr_in *)src_in,
				    dst_in4, addr, &rt);
		if (ret)
			return ret;

		if (resolve_neigh)
			ret = addr_resolve_neigh(&rt->dst, dst_in, addr, seq);

		if (addr->bound_dev_if) {
			ndev = dev_get_by_index(addr->net, addr->bound_dev_if);
		} else {
			ndev = rt->dst.dev;
			dev_hold(ndev);
		}

		ip_rt_put(rt);
	} else {
		const struct sockaddr_in6 *dst_in6 =
			(const struct sockaddr_in6 *)dst_in;

		ret = addr6_resolve((struct sockaddr_in6 *)src_in,
				    dst_in6, addr,
				    &dst);
		if (ret)
			return ret;

		if (resolve_neigh)
			ret = addr_resolve_neigh(dst, dst_in, addr, seq);

		if (addr->bound_dev_if) {
			ndev = dev_get_by_index(addr->net, addr->bound_dev_if);
		} else {
			ndev = dst->dev;
			dev_hold(ndev);
		}

		dst_release(dst);
	}

	if (ndev) {
		if (ndev->flags & IFF_LOOPBACK)
			ret = rdma_translate_ip(dst_in, addr);
		else
			addr->bound_dev_if = ndev->ifindex;
		dev_put(ndev);
	}

	return ret;
}

static void process_one_req(struct work_struct *_work)
{
	struct addr_req *req;
	struct sockaddr *src_in, *dst_in;

	req = container_of(_work, struct addr_req, work.work);

	if (req->status == -ENODATA) {
		src_in = (struct sockaddr *)&req->src_addr;
		dst_in = (struct sockaddr *)&req->dst_addr;
		req->status = addr_resolve(src_in, dst_in, req->addr,
					   true, req->seq);
		if (req->status && time_after_eq(jiffies, req->timeout)) {
			req->status = -ETIMEDOUT;
		} else if (req->status == -ENODATA) {
			/* requeue the work for retrying again */
			spin_lock_bh(&lock);
			if (!list_empty(&req->list))
				set_timeout(req, req->timeout);
			spin_unlock_bh(&lock);
			return;
		}
	}

	req->callback(req->status, (struct sockaddr *)&req->src_addr,
		req->addr, req->context);
	req->callback = NULL;

	spin_lock_bh(&lock);
	if (!list_empty(&req->list)) {
		/*
		 * Although the work will normally have been canceled by the
		 * workqueue, it can still be requeued as long as it is on the
		 * req_list.
		 */
		cancel_delayed_work(&req->work);
		list_del_init(&req->list);
		kfree(req);
	}
	spin_unlock_bh(&lock);
}

int rdma_resolve_ip(struct sockaddr *src_addr, struct sockaddr *dst_addr,
		    struct rdma_dev_addr *addr, int timeout_ms,
		    void (*callback)(int status, struct sockaddr *src_addr,
				     struct rdma_dev_addr *addr, void *context),
		    void *context)
{
	struct sockaddr *src_in, *dst_in;
	struct addr_req *req;
	int ret = 0;

	req = kzalloc(sizeof *req, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	src_in = (struct sockaddr *) &req->src_addr;
	dst_in = (struct sockaddr *) &req->dst_addr;

	if (src_addr) {
		if (src_addr->sa_family != dst_addr->sa_family) {
			ret = -EINVAL;
			goto err;
		}

		memcpy(src_in, src_addr, rdma_addr_size(src_addr));
	} else {
		src_in->sa_family = dst_addr->sa_family;
	}

	memcpy(dst_in, dst_addr, rdma_addr_size(dst_addr));
	req->addr = addr;
	req->callback = callback;
	req->context = context;
	INIT_DELAYED_WORK(&req->work, process_one_req);
	req->seq = (u32)atomic_inc_return(&ib_nl_addr_request_seq);

	req->status = addr_resolve(src_in, dst_in, addr, true, req->seq);
	switch (req->status) {
	case 0:
		req->timeout = jiffies;
		queue_req(req);
		break;
	case -ENODATA:
		req->timeout = msecs_to_jiffies(timeout_ms) + jiffies;
		queue_req(req);
		break;
	default:
		ret = req->status;
		goto err;
	}
	return ret;
err:
	kfree(req);
	return ret;
}
EXPORT_SYMBOL(rdma_resolve_ip);

int rdma_resolve_ip_route(struct sockaddr *src_addr,
			  const struct sockaddr *dst_addr,
			  struct rdma_dev_addr *addr)
{
	struct sockaddr_storage ssrc_addr = {};
	struct sockaddr *src_in = (struct sockaddr *)&ssrc_addr;

	if (src_addr) {
		if (src_addr->sa_family != dst_addr->sa_family)
			return -EINVAL;

		memcpy(src_in, src_addr, rdma_addr_size(src_addr));
	} else {
		src_in->sa_family = dst_addr->sa_family;
	}

	return addr_resolve(src_in, dst_addr, addr, false, 0);
}

void rdma_addr_cancel(struct rdma_dev_addr *addr)
{
	struct addr_req *req, *temp_req;
	struct addr_req *found = NULL;

	spin_lock_bh(&lock);
	list_for_each_entry_safe(req, temp_req, &req_list, list) {
		if (req->addr == addr) {
			/*
			 * Removing from the list means we take ownership of
			 * the req
			 */
			list_del_init(&req->list);
			found = req;
			break;
		}
	}
	spin_unlock_bh(&lock);

	if (!found)
		return;

	/*
	 * sync canceling the work after removing it from the req_list
	 * guarentees no work is running and none will be started.
	 */
	cancel_delayed_work_sync(&found->work);

	if (found->callback)
		found->callback(-ECANCELED, (struct sockaddr *)&found->src_addr,
			      found->addr, found->context);

	kfree(found);
}
EXPORT_SYMBOL(rdma_addr_cancel);

struct resolve_cb_context {
	struct completion comp;
	int status;
};

static void resolve_cb(int status, struct sockaddr *src_addr,
	     struct rdma_dev_addr *addr, void *context)
{
	((struct resolve_cb_context *)context)->status = status;
	complete(&((struct resolve_cb_context *)context)->comp);
}

int rdma_addr_find_l2_eth_by_grh(const union ib_gid *sgid,
				 const union ib_gid *dgid,
				 u8 *dmac, const struct net_device *ndev,
				 int *hoplimit)
{
	struct rdma_dev_addr dev_addr;
	struct resolve_cb_context ctx;
	union {
		struct sockaddr     _sockaddr;
		struct sockaddr_in  _sockaddr_in;
		struct sockaddr_in6 _sockaddr_in6;
	} sgid_addr, dgid_addr;
	int ret;

	rdma_gid2ip(&sgid_addr._sockaddr, sgid);
	rdma_gid2ip(&dgid_addr._sockaddr, dgid);

	memset(&dev_addr, 0, sizeof(dev_addr));
	dev_addr.bound_dev_if = ndev->ifindex;
	dev_addr.net = &init_net;

	init_completion(&ctx.comp);
	ret = rdma_resolve_ip(&sgid_addr._sockaddr, &dgid_addr._sockaddr,
			      &dev_addr, 1000, resolve_cb, &ctx);
	if (ret)
		return ret;

	wait_for_completion(&ctx.comp);

	ret = ctx.status;
	if (ret)
		return ret;

	memcpy(dmac, dev_addr.dst_dev_addr, ETH_ALEN);
	*hoplimit = dev_addr.hoplimit;
	return 0;
}

static int netevent_callback(struct notifier_block *self, unsigned long event,
	void *ctx)
{
	struct addr_req *req;

	if (event == NETEVENT_NEIGH_UPDATE) {
		struct neighbour *neigh = ctx;

		if (neigh->nud_state & NUD_VALID) {
			spin_lock_bh(&lock);
			list_for_each_entry(req, &req_list, list)
				set_timeout(req, jiffies);
			spin_unlock_bh(&lock);
		}
	}
	return 0;
}

static struct notifier_block nb = {
	.notifier_call = netevent_callback
};

int addr_init(void)
{
	addr_wq = alloc_ordered_workqueue("ib_addr", 0);
	if (!addr_wq)
		return -ENOMEM;

	register_netevent_notifier(&nb);

	return 0;
}

void addr_cleanup(void)
{
	unregister_netevent_notifier(&nb);
	destroy_workqueue(addr_wq);
	WARN_ON(!list_empty(&req_list));
}
