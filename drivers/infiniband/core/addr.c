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
#include <rdma/ib_cache.h>
#include <rdma/ib_sa.h>
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
	bool resolve_by_gid_attr;	/* Consider gid attr in resolve phase */
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

int rdma_addr_size(const struct sockaddr *addr)
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

/**
 * rdma_copy_src_l2_addr - Copy netdevice source addresses
 * @dev_addr:	Destination address pointer where to copy the addresses
 * @dev:	Netdevice whose source addresses to copy
 *
 * rdma_copy_src_l2_addr() copies source addresses from the specified netdevice.
 * This includes unicast address, broadcast address, device type and
 * interface index.
 */
void rdma_copy_src_l2_addr(struct rdma_dev_addr *dev_addr,
			   const struct net_device *dev)
{
	dev_addr->dev_type = dev->type;
	memcpy(dev_addr->src_dev_addr, dev->dev_addr, MAX_ADDR_LEN);
	memcpy(dev_addr->broadcast, dev->broadcast, MAX_ADDR_LEN);
	dev_addr->bound_dev_if = dev->ifindex;
}
EXPORT_SYMBOL(rdma_copy_src_l2_addr);

static struct net_device *
rdma_find_ndev_for_src_ip_rcu(struct net *net, const struct sockaddr *src_in)
{
	struct net_device *dev = NULL;
	int ret = -EADDRNOTAVAIL;

	switch (src_in->sa_family) {
	case AF_INET:
		dev = __ip_dev_find(net,
				    ((const struct sockaddr_in *)src_in)->sin_addr.s_addr,
				    false);
		if (dev)
			ret = 0;
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		for_each_netdev_rcu(net, dev) {
			if (ipv6_chk_addr(net,
					  &((const struct sockaddr_in6 *)src_in)->sin6_addr,
					  dev, 1)) {
				ret = 0;
				break;
			}
		}
		break;
#endif
	}
	return ret ? ERR_PTR(ret) : dev;
}

int rdma_translate_ip(const struct sockaddr *addr,
		      struct rdma_dev_addr *dev_addr)
{
	struct net_device *dev;

	if (dev_addr->bound_dev_if) {
		dev = dev_get_by_index(dev_addr->net, dev_addr->bound_dev_if);
		if (!dev)
			return -ENODEV;
		rdma_copy_src_l2_addr(dev_addr, dev);
		dev_put(dev);
		return 0;
	}

	rcu_read_lock();
	dev = rdma_find_ndev_for_src_ip_rcu(dev_addr->net, addr);
	if (!IS_ERR(dev))
		rdma_copy_src_l2_addr(dev_addr, dev);
	rcu_read_unlock();
	return PTR_ERR_OR_ZERO(dev);
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

static int ib_nl_fetch_ha(struct rdma_dev_addr *dev_addr,
			  const void *daddr, u32 seq, u16 family)
{
	if (!rdma_nl_chk_listeners(RDMA_NL_GROUP_LS))
		return -EADDRNOTAVAIL;

	return ib_nl_ip_send_msg(dev_addr, daddr, seq, family);
}

static int dst_fetch_ha(const struct dst_entry *dst,
			struct rdma_dev_addr *dev_addr,
			const void *daddr)
{
	struct neighbour *n;
	int ret = 0;

	n = dst_neigh_lookup(dst, daddr);
	if (!n)
		return -ENODATA;

	if (!(n->nud_state & NUD_VALID)) {
		neigh_event_send(n, NULL);
		ret = -ENODATA;
	} else {
		memcpy(dev_addr->dst_dev_addr, n->ha, MAX_ADDR_LEN);
	}

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

	/* If we have a gateway in IB mode then it must be an IB network */
	if (has_gateway(dst, family) && dev_addr->network == RDMA_NETWORK_IB)
		return ib_nl_fetch_ha(dev_addr, daddr, seq, family);
	else
		return dst_fetch_ha(dst, dev_addr, daddr);
}

static int addr4_resolve(struct sockaddr *src_sock,
			 const struct sockaddr *dst_sock,
			 struct rdma_dev_addr *addr,
			 struct rtable **prt)
{
	struct sockaddr_in *src_in = (struct sockaddr_in *)src_sock;
	const struct sockaddr_in *dst_in =
			(const struct sockaddr_in *)dst_sock;

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

	src_in->sin_addr.s_addr = fl4.saddr;

	addr->hoplimit = ip4_dst_hoplimit(&rt->dst);

	*prt = rt;
	return 0;
}

#if IS_ENABLED(CONFIG_IPV6)
static int addr6_resolve(struct sockaddr *src_sock,
			 const struct sockaddr *dst_sock,
			 struct rdma_dev_addr *addr,
			 struct dst_entry **pdst)
{
	struct sockaddr_in6 *src_in = (struct sockaddr_in6 *)src_sock;
	const struct sockaddr_in6 *dst_in =
				(const struct sockaddr_in6 *)dst_sock;
	struct flowi6 fl6;
	struct dst_entry *dst;
	int ret;

	memset(&fl6, 0, sizeof fl6);
	fl6.daddr = dst_in->sin6_addr;
	fl6.saddr = src_in->sin6_addr;
	fl6.flowi6_oif = addr->bound_dev_if;

	ret = ipv6_stub->ipv6_dst_lookup(addr->net, NULL, &dst, &fl6);
	if (ret < 0)
		return ret;

	if (ipv6_addr_any(&src_in->sin6_addr))
		src_in->sin6_addr = fl6.saddr;

	addr->hoplimit = ip6_dst_hoplimit(dst);

	*pdst = dst;
	return 0;
}
#else
static int addr6_resolve(struct sockaddr *src_sock,
			 const struct sockaddr *dst_sock,
			 struct rdma_dev_addr *addr,
			 struct dst_entry **pdst)
{
	return -EADDRNOTAVAIL;
}
#endif

static int addr_resolve_neigh(const struct dst_entry *dst,
			      const struct sockaddr *dst_in,
			      struct rdma_dev_addr *addr,
			      unsigned int ndev_flags,
			      u32 seq)
{
	int ret = 0;

	if (ndev_flags & IFF_LOOPBACK) {
		memcpy(addr->dst_dev_addr, addr->src_dev_addr, MAX_ADDR_LEN);
	} else {
		if (!(ndev_flags & IFF_NOARP)) {
			/* If the device doesn't do ARP internally */
			ret = fetch_ha(dst, addr, dst_in, seq);
		}
	}
	return ret;
}

static int copy_src_l2_addr(struct rdma_dev_addr *dev_addr,
			    const struct sockaddr *dst_in,
			    const struct dst_entry *dst,
			    const struct net_device *ndev)
{
	int ret = 0;

	if (dst->dev->flags & IFF_LOOPBACK)
		ret = rdma_translate_ip(dst_in, dev_addr);
	else
		rdma_copy_src_l2_addr(dev_addr, dst->dev);

	/*
	 * If there's a gateway and type of device not ARPHRD_INFINIBAND,
	 * we're definitely in RoCE v2 (as RoCE v1 isn't routable) set the
	 * network type accordingly.
	 */
	if (has_gateway(dst, dst_in->sa_family) &&
	    ndev->type != ARPHRD_INFINIBAND)
		dev_addr->network = dst_in->sa_family == AF_INET ?
						RDMA_NETWORK_IPV4 :
						RDMA_NETWORK_IPV6;
	else
		dev_addr->network = RDMA_NETWORK_IB;

	return ret;
}

static int rdma_set_src_addr_rcu(struct rdma_dev_addr *dev_addr,
				 unsigned int *ndev_flags,
				 const struct sockaddr *dst_in,
				 const struct dst_entry *dst)
{
	struct net_device *ndev = READ_ONCE(dst->dev);

	*ndev_flags = ndev->flags;
	/* A physical device must be the RDMA device to use */
	if (ndev->flags & IFF_LOOPBACK) {
		/*
		 * RDMA (IB/RoCE, iWarp) doesn't run on lo interface or
		 * loopback IP address. So if route is resolved to loopback
		 * interface, translate that to a real ndev based on non
		 * loopback IP address.
		 */
		ndev = rdma_find_ndev_for_src_ip_rcu(dev_net(ndev), dst_in);
		if (IS_ERR(ndev))
			return -ENODEV;
	}

	return copy_src_l2_addr(dev_addr, dst_in, dst, ndev);
}

static int set_addr_netns_by_gid_rcu(struct rdma_dev_addr *addr)
{
	struct net_device *ndev;

	ndev = rdma_read_gid_attr_ndev_rcu(addr->sgid_attr);
	if (IS_ERR(ndev))
		return PTR_ERR(ndev);

	/*
	 * Since we are holding the rcu, reading net and ifindex
	 * are safe without any additional reference; because
	 * change_net_namespace() in net/core/dev.c does rcu sync
	 * after it changes the state to IFF_DOWN and before
	 * updating netdev fields {net, ifindex}.
	 */
	addr->net = dev_net(ndev);
	addr->bound_dev_if = ndev->ifindex;
	return 0;
}

static void rdma_addr_set_net_defaults(struct rdma_dev_addr *addr)
{
	addr->net = &init_net;
	addr->bound_dev_if = 0;
}

static int addr_resolve(struct sockaddr *src_in,
			const struct sockaddr *dst_in,
			struct rdma_dev_addr *addr,
			bool resolve_neigh,
			bool resolve_by_gid_attr,
			u32 seq)
{
	struct dst_entry *dst = NULL;
	unsigned int ndev_flags = 0;
	struct rtable *rt = NULL;
	int ret;

	if (!addr->net) {
		pr_warn_ratelimited("%s: missing namespace\n", __func__);
		return -EINVAL;
	}

	rcu_read_lock();
	if (resolve_by_gid_attr) {
		if (!addr->sgid_attr) {
			rcu_read_unlock();
			pr_warn_ratelimited("%s: missing gid_attr\n", __func__);
			return -EINVAL;
		}
		/*
		 * If the request is for a specific gid attribute of the
		 * rdma_dev_addr, derive net from the netdevice of the
		 * GID attribute.
		 */
		ret = set_addr_netns_by_gid_rcu(addr);
		if (ret) {
			rcu_read_unlock();
			return ret;
		}
	}
	if (src_in->sa_family == AF_INET) {
		ret = addr4_resolve(src_in, dst_in, addr, &rt);
		dst = &rt->dst;
	} else {
		ret = addr6_resolve(src_in, dst_in, addr, &dst);
	}
	if (ret) {
		rcu_read_unlock();
		goto done;
	}
	ret = rdma_set_src_addr_rcu(addr, &ndev_flags, dst_in, dst);
	rcu_read_unlock();

	/*
	 * Resolve neighbor destination address if requested and
	 * only if src addr translation didn't fail.
	 */
	if (!ret && resolve_neigh)
		ret = addr_resolve_neigh(dst, dst_in, addr, ndev_flags, seq);

	if (src_in->sa_family == AF_INET)
		ip_rt_put(rt);
	else
		dst_release(dst);
done:
	/*
	 * Clear the addr net to go back to its original state, only if it was
	 * derived from GID attribute in this context.
	 */
	if (resolve_by_gid_attr)
		rdma_addr_set_net_defaults(addr);
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
					   true, req->resolve_by_gid_attr,
					   req->seq);
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

int rdma_resolve_ip(struct sockaddr *src_addr, const struct sockaddr *dst_addr,
		    struct rdma_dev_addr *addr, unsigned long timeout_ms,
		    void (*callback)(int status, struct sockaddr *src_addr,
				     struct rdma_dev_addr *addr, void *context),
		    bool resolve_by_gid_attr, void *context)
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
	req->resolve_by_gid_attr = resolve_by_gid_attr;
	INIT_DELAYED_WORK(&req->work, process_one_req);
	req->seq = (u32)atomic_inc_return(&ib_nl_addr_request_seq);

	req->status = addr_resolve(src_in, dst_in, addr, true,
				   req->resolve_by_gid_attr, req->seq);
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

int roce_resolve_route_from_path(struct sa_path_rec *rec,
				 const struct ib_gid_attr *attr)
{
	union {
		struct sockaddr     _sockaddr;
		struct sockaddr_in  _sockaddr_in;
		struct sockaddr_in6 _sockaddr_in6;
	} sgid, dgid;
	struct rdma_dev_addr dev_addr = {};
	int ret;

	if (rec->roce.route_resolved)
		return 0;

	rdma_gid2ip(&sgid._sockaddr, &rec->sgid);
	rdma_gid2ip(&dgid._sockaddr, &rec->dgid);

	if (sgid._sockaddr.sa_family != dgid._sockaddr.sa_family)
		return -EINVAL;

	if (!attr || !attr->ndev)
		return -EINVAL;

	dev_addr.net = &init_net;
	dev_addr.sgid_attr = attr;

	ret = addr_resolve(&sgid._sockaddr, &dgid._sockaddr,
			   &dev_addr, false, true, 0);
	if (ret)
		return ret;

	if ((dev_addr.network == RDMA_NETWORK_IPV4 ||
	     dev_addr.network == RDMA_NETWORK_IPV6) &&
	    rec->rec_type != SA_PATH_REC_TYPE_ROCE_V2)
		return -EINVAL;

	rec->roce.route_resolved = true;
	return 0;
}

/**
 * rdma_addr_cancel - Cancel resolve ip request
 * @addr:	Pointer to address structure given previously
 *		during rdma_resolve_ip().
 * rdma_addr_cancel() is synchronous function which cancels any pending
 * request if there is any.
 */
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
				 u8 *dmac, const struct ib_gid_attr *sgid_attr,
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
	dev_addr.net = &init_net;
	dev_addr.sgid_attr = sgid_attr;

	init_completion(&ctx.comp);
	ret = rdma_resolve_ip(&sgid_addr._sockaddr, &dgid_addr._sockaddr,
			      &dev_addr, 1000, resolve_cb, true, &ctx);
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
