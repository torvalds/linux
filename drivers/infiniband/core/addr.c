/*
 * Copyright (c) 2005 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2002-2005, Network Appliance, Inc. All rights reserved.
 * Copyright (c) 1999-2005, Mellanox Technologies, Inc. All rights reserved.
 * Copyright (c) 2005 Intel Corporation.  All rights reserved.
 *
 * This Software is licensed under one of the following licenses:
 *
 * 1) under the terms of the "Common Public License 1.0" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/cpl.php.
 *
 * 2) under the terms of the "The BSD License" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/bsd-license.php.
 *
 * 3) under the terms of the "GNU General Public License (GPL) Version 2" a
 *    copy of which is available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/gpl-license.php.
 *
 * Licensee has the right to choose one of the above licenses.
 *
 * Redistributions of source code must retain the above copyright
 * notice and one of the license notices.
 *
 * Redistributions in binary form must reproduce both the above copyright
 * notice, one of the license notices in the documentation
 * and/or other materials provided with the distribution.
 */

#include <linux/mutex.h>
#include <linux/inetdevice.h>
#include <linux/workqueue.h>
#include <linux/if_arp.h>
#include <net/arp.h>
#include <net/neighbour.h>
#include <net/route.h>
#include <net/netevent.h>
#include <rdma/ib_addr.h>

MODULE_AUTHOR("Sean Hefty");
MODULE_DESCRIPTION("IB Address Translation");
MODULE_LICENSE("Dual BSD/GPL");

struct addr_req {
	struct list_head list;
	struct sockaddr src_addr;
	struct sockaddr dst_addr;
	struct rdma_dev_addr *addr;
	struct rdma_addr_client *client;
	void *context;
	void (*callback)(int status, struct sockaddr *src_addr,
			 struct rdma_dev_addr *addr, void *context);
	unsigned long timeout;
	int status;
};

static void process_req(void *data);

static DEFINE_MUTEX(lock);
static LIST_HEAD(req_list);
static DECLARE_WORK(work, process_req, NULL);
static struct workqueue_struct *addr_wq;

void rdma_addr_register_client(struct rdma_addr_client *client)
{
	atomic_set(&client->refcount, 1);
	init_completion(&client->comp);
}
EXPORT_SYMBOL(rdma_addr_register_client);

static inline void put_client(struct rdma_addr_client *client)
{
	if (atomic_dec_and_test(&client->refcount))
		complete(&client->comp);
}

void rdma_addr_unregister_client(struct rdma_addr_client *client)
{
	put_client(client);
	wait_for_completion(&client->comp);
}
EXPORT_SYMBOL(rdma_addr_unregister_client);

int rdma_copy_addr(struct rdma_dev_addr *dev_addr, struct net_device *dev,
		     const unsigned char *dst_dev_addr)
{
	switch (dev->type) {
	case ARPHRD_INFINIBAND:
		dev_addr->dev_type = RDMA_NODE_IB_CA;
		break;
	case ARPHRD_ETHER:
		dev_addr->dev_type = RDMA_NODE_RNIC;
		break;
	default:
		return -EADDRNOTAVAIL;
	}

	memcpy(dev_addr->src_dev_addr, dev->dev_addr, MAX_ADDR_LEN);
	memcpy(dev_addr->broadcast, dev->broadcast, MAX_ADDR_LEN);
	if (dst_dev_addr)
		memcpy(dev_addr->dst_dev_addr, dst_dev_addr, MAX_ADDR_LEN);
	return 0;
}
EXPORT_SYMBOL(rdma_copy_addr);

int rdma_translate_ip(struct sockaddr *addr, struct rdma_dev_addr *dev_addr)
{
	struct net_device *dev;
	__be32 ip = ((struct sockaddr_in *) addr)->sin_addr.s_addr;
	int ret;

	dev = ip_dev_find(ip);
	if (!dev)
		return -EADDRNOTAVAIL;

	ret = rdma_copy_addr(dev_addr, dev, NULL);
	dev_put(dev);
	return ret;
}
EXPORT_SYMBOL(rdma_translate_ip);

static void set_timeout(unsigned long time)
{
	unsigned long delay;

	cancel_delayed_work(&work);

	delay = time - jiffies;
	if ((long)delay <= 0)
		delay = 1;

	queue_delayed_work(addr_wq, &work, delay);
}

static void queue_req(struct addr_req *req)
{
	struct addr_req *temp_req;

	mutex_lock(&lock);
	list_for_each_entry_reverse(temp_req, &req_list, list) {
		if (time_after_eq(req->timeout, temp_req->timeout))
			break;
	}

	list_add(&req->list, &temp_req->list);

	if (req_list.next == &req->list)
		set_timeout(req->timeout);
	mutex_unlock(&lock);
}

static void addr_send_arp(struct sockaddr_in *dst_in)
{
	struct rtable *rt;
	struct flowi fl;
	u32 dst_ip = dst_in->sin_addr.s_addr;

	memset(&fl, 0, sizeof fl);
	fl.nl_u.ip4_u.daddr = dst_ip;
	if (ip_route_output_key(&rt, &fl))
		return;

	arp_send(ARPOP_REQUEST, ETH_P_ARP, rt->rt_gateway, rt->idev->dev,
		 rt->rt_src, NULL, rt->idev->dev->dev_addr, NULL);
	ip_rt_put(rt);
}

static int addr_resolve_remote(struct sockaddr_in *src_in,
			       struct sockaddr_in *dst_in,
			       struct rdma_dev_addr *addr)
{
	u32 src_ip = src_in->sin_addr.s_addr;
	u32 dst_ip = dst_in->sin_addr.s_addr;
	struct flowi fl;
	struct rtable *rt;
	struct neighbour *neigh;
	int ret;

	memset(&fl, 0, sizeof fl);
	fl.nl_u.ip4_u.daddr = dst_ip;
	fl.nl_u.ip4_u.saddr = src_ip;
	ret = ip_route_output_key(&rt, &fl);
	if (ret)
		goto out;

	/* If the device does ARP internally, return 'done' */
	if (rt->idev->dev->flags & IFF_NOARP) {
		rdma_copy_addr(addr, rt->idev->dev, NULL);
		goto put;
	}

	neigh = neigh_lookup(&arp_tbl, &rt->rt_gateway, rt->idev->dev);
	if (!neigh) {
		ret = -ENODATA;
		goto put;
	}

	if (!(neigh->nud_state & NUD_VALID)) {
		ret = -ENODATA;
		goto release;
	}

	if (!src_ip) {
		src_in->sin_family = dst_in->sin_family;
		src_in->sin_addr.s_addr = rt->rt_src;
	}

	ret = rdma_copy_addr(addr, neigh->dev, neigh->ha);
release:
	neigh_release(neigh);
put:
	ip_rt_put(rt);
out:
	return ret;
}

static void process_req(void *data)
{
	struct addr_req *req, *temp_req;
	struct sockaddr_in *src_in, *dst_in;
	struct list_head done_list;

	INIT_LIST_HEAD(&done_list);

	mutex_lock(&lock);
	list_for_each_entry_safe(req, temp_req, &req_list, list) {
		if (req->status) {
			src_in = (struct sockaddr_in *) &req->src_addr;
			dst_in = (struct sockaddr_in *) &req->dst_addr;
			req->status = addr_resolve_remote(src_in, dst_in,
							  req->addr);
		}
		if (req->status && time_after(jiffies, req->timeout))
			req->status = -ETIMEDOUT;
		else if (req->status == -ENODATA)
			continue;

		list_del(&req->list);
		list_add_tail(&req->list, &done_list);
	}

	if (!list_empty(&req_list)) {
		req = list_entry(req_list.next, struct addr_req, list);
		set_timeout(req->timeout);
	}
	mutex_unlock(&lock);

	list_for_each_entry_safe(req, temp_req, &done_list, list) {
		list_del(&req->list);
		req->callback(req->status, &req->src_addr, req->addr,
			      req->context);
		put_client(req->client);
		kfree(req);
	}
}

static int addr_resolve_local(struct sockaddr_in *src_in,
			      struct sockaddr_in *dst_in,
			      struct rdma_dev_addr *addr)
{
	struct net_device *dev;
	u32 src_ip = src_in->sin_addr.s_addr;
	__be32 dst_ip = dst_in->sin_addr.s_addr;
	int ret;

	dev = ip_dev_find(dst_ip);
	if (!dev)
		return -EADDRNOTAVAIL;

	if (ZERONET(src_ip)) {
		src_in->sin_family = dst_in->sin_family;
		src_in->sin_addr.s_addr = dst_ip;
		ret = rdma_copy_addr(addr, dev, dev->dev_addr);
	} else if (LOOPBACK(src_ip)) {
		ret = rdma_translate_ip((struct sockaddr *)dst_in, addr);
		if (!ret)
			memcpy(addr->dst_dev_addr, dev->dev_addr, MAX_ADDR_LEN);
	} else {
		ret = rdma_translate_ip((struct sockaddr *)src_in, addr);
		if (!ret)
			memcpy(addr->dst_dev_addr, dev->dev_addr, MAX_ADDR_LEN);
	}

	dev_put(dev);
	return ret;
}

int rdma_resolve_ip(struct rdma_addr_client *client,
		    struct sockaddr *src_addr, struct sockaddr *dst_addr,
		    struct rdma_dev_addr *addr, int timeout_ms,
		    void (*callback)(int status, struct sockaddr *src_addr,
				     struct rdma_dev_addr *addr, void *context),
		    void *context)
{
	struct sockaddr_in *src_in, *dst_in;
	struct addr_req *req;
	int ret = 0;

	req = kmalloc(sizeof *req, GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	memset(req, 0, sizeof *req);

	if (src_addr)
		memcpy(&req->src_addr, src_addr, ip_addr_size(src_addr));
	memcpy(&req->dst_addr, dst_addr, ip_addr_size(dst_addr));
	req->addr = addr;
	req->callback = callback;
	req->context = context;
	req->client = client;
	atomic_inc(&client->refcount);

	src_in = (struct sockaddr_in *) &req->src_addr;
	dst_in = (struct sockaddr_in *) &req->dst_addr;

	req->status = addr_resolve_local(src_in, dst_in, addr);
	if (req->status == -EADDRNOTAVAIL)
		req->status = addr_resolve_remote(src_in, dst_in, addr);

	switch (req->status) {
	case 0:
		req->timeout = jiffies;
		queue_req(req);
		break;
	case -ENODATA:
		req->timeout = msecs_to_jiffies(timeout_ms) + jiffies;
		queue_req(req);
		addr_send_arp(dst_in);
		break;
	default:
		ret = req->status;
		atomic_dec(&client->refcount);
		kfree(req);
		break;
	}
	return ret;
}
EXPORT_SYMBOL(rdma_resolve_ip);

void rdma_addr_cancel(struct rdma_dev_addr *addr)
{
	struct addr_req *req, *temp_req;

	mutex_lock(&lock);
	list_for_each_entry_safe(req, temp_req, &req_list, list) {
		if (req->addr == addr) {
			req->status = -ECANCELED;
			req->timeout = jiffies;
			list_del(&req->list);
			list_add(&req->list, &req_list);
			set_timeout(req->timeout);
			break;
		}
	}
	mutex_unlock(&lock);
}
EXPORT_SYMBOL(rdma_addr_cancel);

static int netevent_callback(struct notifier_block *self, unsigned long event,
	void *ctx)
{
	if (event == NETEVENT_NEIGH_UPDATE) {
		struct neighbour *neigh = ctx;

		if (neigh->dev->type == ARPHRD_INFINIBAND &&
		    (neigh->nud_state & NUD_VALID)) {
			set_timeout(jiffies);
		}
	}
	return 0;
}

static struct notifier_block nb = {
	.notifier_call = netevent_callback
};

static int addr_init(void)
{
	addr_wq = create_singlethread_workqueue("ib_addr_wq");
	if (!addr_wq)
		return -ENOMEM;

	register_netevent_notifier(&nb);
	return 0;
}

static void addr_cleanup(void)
{
	unregister_netevent_notifier(&nb);
	destroy_workqueue(addr_wq);
}

module_init(addr_init);
module_exit(addr_cleanup);
