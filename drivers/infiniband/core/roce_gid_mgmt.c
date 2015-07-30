/*
 * Copyright (c) 2015, Mellanox Technologies inc.  All rights reserved.
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

#include "core_priv.h"

#include <linux/in.h>
#include <linux/in6.h>

/* For in6_dev_get/in6_dev_put */
#include <net/addrconf.h>

#include <rdma/ib_cache.h>
#include <rdma/ib_addr.h>

enum gid_op_type {
	GID_DEL = 0,
	GID_ADD
};

struct update_gid_event_work {
	struct work_struct work;
	union ib_gid       gid;
	struct ib_gid_attr gid_attr;
	enum gid_op_type gid_op;
};

#define ROCE_NETDEV_CALLBACK_SZ		2
struct netdev_event_work_cmd {
	roce_netdev_callback	cb;
	roce_netdev_filter	filter;
};

struct netdev_event_work {
	struct work_struct		work;
	struct netdev_event_work_cmd	cmds[ROCE_NETDEV_CALLBACK_SZ];
	struct net_device		*ndev;
};

static void update_gid(enum gid_op_type gid_op, struct ib_device *ib_dev,
		       u8 port, union ib_gid *gid,
		       struct ib_gid_attr *gid_attr)
{
	switch (gid_op) {
	case GID_ADD:
		ib_cache_gid_add(ib_dev, port, gid, gid_attr);
		break;
	case GID_DEL:
		ib_cache_gid_del(ib_dev, port, gid, gid_attr);
		break;
	}
}

static int is_eth_port_of_netdev(struct ib_device *ib_dev, u8 port,
				 struct net_device *rdma_ndev, void *cookie)
{
	struct net_device *real_dev;
	struct net_device *master_dev;
	struct net_device *event_ndev = (struct net_device *)cookie;
	int res;

	if (!rdma_ndev)
		return 0;

	rcu_read_lock();
	master_dev = netdev_master_upper_dev_get_rcu(rdma_ndev);
	real_dev = rdma_vlan_dev_real_dev(event_ndev);
	res = (real_dev ? real_dev : event_ndev) ==
		(master_dev ? master_dev : rdma_ndev);
	rcu_read_unlock();

	return res;
}

static int pass_all_filter(struct ib_device *ib_dev, u8 port,
			   struct net_device *rdma_ndev, void *cookie)
{
	return 1;
}

static void update_gid_ip(enum gid_op_type gid_op,
			  struct ib_device *ib_dev,
			  u8 port, struct net_device *ndev,
			  struct sockaddr *addr)
{
	union ib_gid gid;
	struct ib_gid_attr gid_attr;

	rdma_ip2gid(addr, &gid);
	memset(&gid_attr, 0, sizeof(gid_attr));
	gid_attr.ndev = ndev;

	update_gid(gid_op, ib_dev, port, &gid, &gid_attr);
}

static void enum_netdev_default_gids(struct ib_device *ib_dev,
				     u8 port, struct net_device *event_ndev,
				     struct net_device *rdma_ndev)
{
	if (rdma_ndev != event_ndev)
		return;

	ib_cache_gid_set_default_gid(ib_dev, port, rdma_ndev,
				     IB_CACHE_GID_DEFAULT_MODE_SET);
}

static void enum_netdev_ipv4_ips(struct ib_device *ib_dev,
				 u8 port, struct net_device *ndev)
{
	struct in_device *in_dev;

	if (ndev->reg_state >= NETREG_UNREGISTERING)
		return;

	in_dev = in_dev_get(ndev);
	if (!in_dev)
		return;

	for_ifa(in_dev) {
		struct sockaddr_in ip;

		ip.sin_family = AF_INET;
		ip.sin_addr.s_addr = ifa->ifa_address;
		update_gid_ip(GID_ADD, ib_dev, port, ndev,
			      (struct sockaddr *)&ip);
	}
	endfor_ifa(in_dev);

	in_dev_put(in_dev);
}

static void enum_netdev_ipv6_ips(struct ib_device *ib_dev,
				 u8 port, struct net_device *ndev)
{
	struct inet6_ifaddr *ifp;
	struct inet6_dev *in6_dev;
	struct sin6_list {
		struct list_head	list;
		struct sockaddr_in6	sin6;
	};
	struct sin6_list *sin6_iter;
	struct sin6_list *sin6_temp;
	struct ib_gid_attr gid_attr = {.ndev = ndev};
	LIST_HEAD(sin6_list);

	if (ndev->reg_state >= NETREG_UNREGISTERING)
		return;

	in6_dev = in6_dev_get(ndev);
	if (!in6_dev)
		return;

	read_lock_bh(&in6_dev->lock);
	list_for_each_entry(ifp, &in6_dev->addr_list, if_list) {
		struct sin6_list *entry = kzalloc(sizeof(*entry), GFP_ATOMIC);

		if (!entry) {
			pr_warn("roce_gid_mgmt: couldn't allocate entry for IPv6 update\n");
			continue;
		}

		entry->sin6.sin6_family = AF_INET6;
		entry->sin6.sin6_addr = ifp->addr;
		list_add_tail(&entry->list, &sin6_list);
	}
	read_unlock_bh(&in6_dev->lock);

	in6_dev_put(in6_dev);

	list_for_each_entry_safe(sin6_iter, sin6_temp, &sin6_list, list) {
		union ib_gid	gid;

		rdma_ip2gid((struct sockaddr *)&sin6_iter->sin6, &gid);
		update_gid(GID_ADD, ib_dev, port, &gid, &gid_attr);
		list_del(&sin6_iter->list);
		kfree(sin6_iter);
	}
}

static void add_netdev_ips(struct ib_device *ib_dev, u8 port,
			   struct net_device *rdma_ndev, void *cookie)
{
	struct net_device *event_ndev = (struct net_device *)cookie;

	enum_netdev_default_gids(ib_dev, port, event_ndev, rdma_ndev);
	enum_netdev_ipv4_ips(ib_dev, port, event_ndev);
	if (IS_ENABLED(CONFIG_IPV6))
		enum_netdev_ipv6_ips(ib_dev, port, event_ndev);
}

static void del_netdev_ips(struct ib_device *ib_dev, u8 port,
			   struct net_device *rdma_ndev, void *cookie)
{
	struct net_device *event_ndev = (struct net_device *)cookie;

	ib_cache_gid_del_all_netdev_gids(ib_dev, port, event_ndev);
}

static void enum_all_gids_of_dev_cb(struct ib_device *ib_dev,
				    u8 port,
				    struct net_device *rdma_ndev,
				    void *cookie)
{
	struct net *net;
	struct net_device *ndev;

	/* Lock the rtnl to make sure the netdevs does not move under
	 * our feet
	 */
	rtnl_lock();
	for_each_net(net)
		for_each_netdev(net, ndev)
			if (is_eth_port_of_netdev(ib_dev, port, rdma_ndev, ndev))
				add_netdev_ips(ib_dev, port, rdma_ndev, ndev);
	rtnl_unlock();
}

/* This function will rescan all of the network devices in the system
 * and add their gids, as needed, to the relevant RoCE devices. */
int roce_rescan_device(struct ib_device *ib_dev)
{
	ib_enum_roce_netdev(ib_dev, pass_all_filter, NULL,
			    enum_all_gids_of_dev_cb, NULL);

	return 0;
}

static void callback_for_addr_gid_device_scan(struct ib_device *device,
					      u8 port,
					      struct net_device *rdma_ndev,
					      void *cookie)
{
	struct update_gid_event_work *parsed = cookie;

	return update_gid(parsed->gid_op, device,
			  port, &parsed->gid,
			  &parsed->gid_attr);
}

/* The following functions operate on all IB devices. netdevice_event and
 * addr_event execute ib_enum_all_roce_netdevs through a work.
 * ib_enum_all_roce_netdevs iterates through all IB devices.
 */

static void netdevice_event_work_handler(struct work_struct *_work)
{
	struct netdev_event_work *work =
		container_of(_work, struct netdev_event_work, work);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(work->cmds) && work->cmds[i].cb; i++)
		ib_enum_all_roce_netdevs(work->cmds[i].filter, work->ndev,
					 work->cmds[i].cb, work->ndev);

	dev_put(work->ndev);
	kfree(work);
}

static int netdevice_queue_work(struct netdev_event_work_cmd *cmds,
				struct net_device *ndev)
{
	struct netdev_event_work *ndev_work =
		kmalloc(sizeof(*ndev_work), GFP_KERNEL);

	if (!ndev_work) {
		pr_warn("roce_gid_mgmt: can't allocate work for netdevice_event\n");
		return NOTIFY_DONE;
	}

	memcpy(ndev_work->cmds, cmds, sizeof(ndev_work->cmds));
	ndev_work->ndev = ndev;
	dev_hold(ndev);
	INIT_WORK(&ndev_work->work, netdevice_event_work_handler);

	queue_work(ib_wq, &ndev_work->work);

	return NOTIFY_DONE;
}

static int netdevice_event(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	static const struct netdev_event_work_cmd add_cmd = {
		.cb = add_netdev_ips, .filter = is_eth_port_of_netdev};
	static const struct netdev_event_work_cmd del_cmd = {
		.cb = del_netdev_ips, .filter = pass_all_filter};
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct netdev_event_work_cmd cmds[ROCE_NETDEV_CALLBACK_SZ] = { {NULL} };

	if (ndev->type != ARPHRD_ETHER)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
	case NETDEV_UP:
		cmds[0] = add_cmd;
		break;

	case NETDEV_UNREGISTER:
		if (ndev->reg_state < NETREG_UNREGISTERED)
			cmds[0] = del_cmd;
		else
			return NOTIFY_DONE;
		break;

	case NETDEV_CHANGEADDR:
		cmds[0] = del_cmd;
		cmds[1] = add_cmd;
		break;
	default:
		return NOTIFY_DONE;
	}

	return netdevice_queue_work(cmds, ndev);
}

static void update_gid_event_work_handler(struct work_struct *_work)
{
	struct update_gid_event_work *work =
		container_of(_work, struct update_gid_event_work, work);

	ib_enum_all_roce_netdevs(is_eth_port_of_netdev, work->gid_attr.ndev,
				 callback_for_addr_gid_device_scan, work);

	dev_put(work->gid_attr.ndev);
	kfree(work);
}

static int addr_event(struct notifier_block *this, unsigned long event,
		      struct sockaddr *sa, struct net_device *ndev)
{
	struct update_gid_event_work *work;
	enum gid_op_type gid_op;

	if (ndev->type != ARPHRD_ETHER)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		gid_op = GID_ADD;
		break;

	case NETDEV_DOWN:
		gid_op = GID_DEL;
		break;

	default:
		return NOTIFY_DONE;
	}

	work = kmalloc(sizeof(*work), GFP_ATOMIC);
	if (!work) {
		pr_warn("roce_gid_mgmt: Couldn't allocate work for addr_event\n");
		return NOTIFY_DONE;
	}

	INIT_WORK(&work->work, update_gid_event_work_handler);

	rdma_ip2gid(sa, &work->gid);
	work->gid_op = gid_op;

	memset(&work->gid_attr, 0, sizeof(work->gid_attr));
	dev_hold(ndev);
	work->gid_attr.ndev   = ndev;

	queue_work(ib_wq, &work->work);

	return NOTIFY_DONE;
}

static int inetaddr_event(struct notifier_block *this, unsigned long event,
			  void *ptr)
{
	struct sockaddr_in	in;
	struct net_device	*ndev;
	struct in_ifaddr	*ifa = ptr;

	in.sin_family = AF_INET;
	in.sin_addr.s_addr = ifa->ifa_address;
	ndev = ifa->ifa_dev->dev;

	return addr_event(this, event, (struct sockaddr *)&in, ndev);
}

static int inet6addr_event(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct sockaddr_in6	in6;
	struct net_device	*ndev;
	struct inet6_ifaddr	*ifa6 = ptr;

	in6.sin6_family = AF_INET6;
	in6.sin6_addr = ifa6->addr;
	ndev = ifa6->idev->dev;

	return addr_event(this, event, (struct sockaddr *)&in6, ndev);
}

static struct notifier_block nb_netdevice = {
	.notifier_call = netdevice_event
};

static struct notifier_block nb_inetaddr = {
	.notifier_call = inetaddr_event
};

static struct notifier_block nb_inet6addr = {
	.notifier_call = inet6addr_event
};

int __init roce_gid_mgmt_init(void)
{
	register_inetaddr_notifier(&nb_inetaddr);
	if (IS_ENABLED(CONFIG_IPV6))
		register_inet6addr_notifier(&nb_inet6addr);
	/* We relay on the netdevice notifier to enumerate all
	 * existing devices in the system. Register to this notifier
	 * last to make sure we will not miss any IP add/del
	 * callbacks.
	 */
	register_netdevice_notifier(&nb_netdevice);

	return 0;
}

void __exit roce_gid_mgmt_cleanup(void)
{
	if (IS_ENABLED(CONFIG_IPV6))
		unregister_inet6addr_notifier(&nb_inet6addr);
	unregister_inetaddr_notifier(&nb_inetaddr);
	unregister_netdevice_notifier(&nb_netdevice);
	/* Ensure all gid deletion tasks complete before we go down,
	 * to avoid any reference to free'd memory. By the time
	 * ib-core is removed, all physical devices have been removed,
	 * so no issue with remaining hardware contexts.
	 */
}
