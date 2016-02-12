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
#include <net/bonding.h>

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

#define ROCE_NETDEV_CALLBACK_SZ		3
struct netdev_event_work_cmd {
	roce_netdev_callback	cb;
	roce_netdev_filter	filter;
	struct net_device	*ndev;
	struct net_device	*filter_ndev;
};

struct netdev_event_work {
	struct work_struct		work;
	struct netdev_event_work_cmd	cmds[ROCE_NETDEV_CALLBACK_SZ];
};

static const struct {
	bool (*is_supported)(const struct ib_device *device, u8 port_num);
	enum ib_gid_type gid_type;
} PORT_CAP_TO_GID_TYPE[] = {
	{rdma_protocol_roce_eth_encap, IB_GID_TYPE_ROCE},
	{rdma_protocol_roce_udp_encap, IB_GID_TYPE_ROCE_UDP_ENCAP},
};

#define CAP_TO_GID_TABLE_SIZE	ARRAY_SIZE(PORT_CAP_TO_GID_TYPE)

unsigned long roce_gid_type_mask_support(struct ib_device *ib_dev, u8 port)
{
	int i;
	unsigned int ret_flags = 0;

	if (!rdma_protocol_roce(ib_dev, port))
		return 1UL << IB_GID_TYPE_IB;

	for (i = 0; i < CAP_TO_GID_TABLE_SIZE; i++)
		if (PORT_CAP_TO_GID_TYPE[i].is_supported(ib_dev, port))
			ret_flags |= 1UL << PORT_CAP_TO_GID_TYPE[i].gid_type;

	return ret_flags;
}
EXPORT_SYMBOL(roce_gid_type_mask_support);

static void update_gid(enum gid_op_type gid_op, struct ib_device *ib_dev,
		       u8 port, union ib_gid *gid,
		       struct ib_gid_attr *gid_attr)
{
	int i;
	unsigned long gid_type_mask = roce_gid_type_mask_support(ib_dev, port);

	for (i = 0; i < IB_GID_TYPE_SIZE; i++) {
		if ((1UL << i) & gid_type_mask) {
			gid_attr->gid_type = i;
			switch (gid_op) {
			case GID_ADD:
				ib_cache_gid_add(ib_dev, port,
						 gid, gid_attr);
				break;
			case GID_DEL:
				ib_cache_gid_del(ib_dev, port,
						 gid, gid_attr);
				break;
			}
		}
	}
}

enum bonding_slave_state {
	BONDING_SLAVE_STATE_ACTIVE	= 1UL << 0,
	BONDING_SLAVE_STATE_INACTIVE	= 1UL << 1,
	/* No primary slave or the device isn't a slave in bonding */
	BONDING_SLAVE_STATE_NA		= 1UL << 2,
};

static enum bonding_slave_state is_eth_active_slave_of_bonding_rcu(struct net_device *dev,
								   struct net_device *upper)
{
	if (upper && netif_is_bond_master(upper)) {
		struct net_device *pdev =
			bond_option_active_slave_get_rcu(netdev_priv(upper));

		if (pdev)
			return dev == pdev ? BONDING_SLAVE_STATE_ACTIVE :
				BONDING_SLAVE_STATE_INACTIVE;
	}

	return BONDING_SLAVE_STATE_NA;
}

#define REQUIRED_BOND_STATES		(BONDING_SLAVE_STATE_ACTIVE |	\
					 BONDING_SLAVE_STATE_NA)
static int is_eth_port_of_netdev(struct ib_device *ib_dev, u8 port,
				 struct net_device *rdma_ndev, void *cookie)
{
	struct net_device *event_ndev = (struct net_device *)cookie;
	struct net_device *real_dev;
	int res;

	if (!rdma_ndev)
		return 0;

	rcu_read_lock();
	real_dev = rdma_vlan_dev_real_dev(event_ndev);
	if (!real_dev)
		real_dev = event_ndev;

	res = ((rdma_is_upper_dev_rcu(rdma_ndev, event_ndev) &&
	       (is_eth_active_slave_of_bonding_rcu(rdma_ndev, real_dev) &
		REQUIRED_BOND_STATES)) ||
	       real_dev == rdma_ndev);

	rcu_read_unlock();
	return res;
}

static int is_eth_port_inactive_slave(struct ib_device *ib_dev, u8 port,
				      struct net_device *rdma_ndev, void *cookie)
{
	struct net_device *master_dev;
	int res;

	if (!rdma_ndev)
		return 0;

	rcu_read_lock();
	master_dev = netdev_master_upper_dev_get_rcu(rdma_ndev);
	res = is_eth_active_slave_of_bonding_rcu(rdma_ndev, master_dev) ==
		BONDING_SLAVE_STATE_INACTIVE;
	rcu_read_unlock();

	return res;
}

static int pass_all_filter(struct ib_device *ib_dev, u8 port,
			   struct net_device *rdma_ndev, void *cookie)
{
	return 1;
}

static int upper_device_filter(struct ib_device *ib_dev, u8 port,
			       struct net_device *rdma_ndev, void *cookie)
{
	struct net_device *event_ndev = (struct net_device *)cookie;
	int res;

	if (!rdma_ndev)
		return 0;

	if (rdma_ndev == event_ndev)
		return 1;

	rcu_read_lock();
	res = rdma_is_upper_dev_rcu(rdma_ndev, event_ndev);
	rcu_read_unlock();

	return res;
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
	unsigned long gid_type_mask;

	rcu_read_lock();
	if (!rdma_ndev ||
	    ((rdma_ndev != event_ndev &&
	      !rdma_is_upper_dev_rcu(rdma_ndev, event_ndev)) ||
	     is_eth_active_slave_of_bonding_rcu(rdma_ndev,
						netdev_master_upper_dev_get_rcu(rdma_ndev)) ==
	     BONDING_SLAVE_STATE_INACTIVE)) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();

	gid_type_mask = roce_gid_type_mask_support(ib_dev, port);

	ib_cache_gid_set_default_gid(ib_dev, port, rdma_ndev, gid_type_mask,
				     IB_CACHE_GID_DEFAULT_MODE_SET);
}

static void bond_delete_netdev_default_gids(struct ib_device *ib_dev,
					    u8 port,
					    struct net_device *event_ndev,
					    struct net_device *rdma_ndev)
{
	struct net_device *real_dev = rdma_vlan_dev_real_dev(event_ndev);

	if (!rdma_ndev)
		return;

	if (!real_dev)
		real_dev = event_ndev;

	rcu_read_lock();

	if (rdma_is_upper_dev_rcu(rdma_ndev, event_ndev) &&
	    is_eth_active_slave_of_bonding_rcu(rdma_ndev, real_dev) ==
	    BONDING_SLAVE_STATE_INACTIVE) {
		unsigned long gid_type_mask;

		rcu_read_unlock();

		gid_type_mask = roce_gid_type_mask_support(ib_dev, port);

		ib_cache_gid_set_default_gid(ib_dev, port, rdma_ndev,
					     gid_type_mask,
					     IB_CACHE_GID_DEFAULT_MODE_DELETE);
	} else {
		rcu_read_unlock();
	}
}

static void enum_netdev_ipv4_ips(struct ib_device *ib_dev,
				 u8 port, struct net_device *ndev)
{
	struct in_device *in_dev;
	struct sin_list {
		struct list_head	list;
		struct sockaddr_in	ip;
	};
	struct sin_list *sin_iter;
	struct sin_list *sin_temp;

	LIST_HEAD(sin_list);
	if (ndev->reg_state >= NETREG_UNREGISTERING)
		return;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(ndev);
	if (!in_dev) {
		rcu_read_unlock();
		return;
	}

	for_ifa(in_dev) {
		struct sin_list *entry = kzalloc(sizeof(*entry), GFP_ATOMIC);

		if (!entry) {
			pr_warn("roce_gid_mgmt: couldn't allocate entry for IPv4 update\n");
			continue;
		}
		entry->ip.sin_family = AF_INET;
		entry->ip.sin_addr.s_addr = ifa->ifa_address;
		list_add_tail(&entry->list, &sin_list);
	}
	endfor_ifa(in_dev);
	rcu_read_unlock();

	list_for_each_entry_safe(sin_iter, sin_temp, &sin_list, list) {
		update_gid_ip(GID_ADD, ib_dev, port, ndev,
			      (struct sockaddr *)&sin_iter->ip);
		list_del(&sin_iter->list);
		kfree(sin_iter);
	}
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

static void _add_netdev_ips(struct ib_device *ib_dev, u8 port,
			    struct net_device *ndev)
{
	enum_netdev_ipv4_ips(ib_dev, port, ndev);
	if (IS_ENABLED(CONFIG_IPV6))
		enum_netdev_ipv6_ips(ib_dev, port, ndev);
}

static void add_netdev_ips(struct ib_device *ib_dev, u8 port,
			   struct net_device *rdma_ndev, void *cookie)
{
	struct net_device *event_ndev = (struct net_device *)cookie;

	enum_netdev_default_gids(ib_dev, port, event_ndev, rdma_ndev);
	_add_netdev_ips(ib_dev, port, event_ndev);
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

static void handle_netdev_upper(struct ib_device *ib_dev, u8 port,
				void *cookie,
				void (*handle_netdev)(struct ib_device *ib_dev,
						      u8 port,
						      struct net_device *ndev))
{
	struct net_device *ndev = (struct net_device *)cookie;
	struct upper_list {
		struct list_head list;
		struct net_device *upper;
	};
	struct net_device *upper;
	struct list_head *iter;
	struct upper_list *upper_iter;
	struct upper_list *upper_temp;
	LIST_HEAD(upper_list);

	rcu_read_lock();
	netdev_for_each_all_upper_dev_rcu(ndev, upper, iter) {
		struct upper_list *entry = kmalloc(sizeof(*entry),
						   GFP_ATOMIC);

		if (!entry) {
			pr_info("roce_gid_mgmt: couldn't allocate entry to delete ndev\n");
			continue;
		}

		list_add_tail(&entry->list, &upper_list);
		dev_hold(upper);
		entry->upper = upper;
	}
	rcu_read_unlock();

	handle_netdev(ib_dev, port, ndev);
	list_for_each_entry_safe(upper_iter, upper_temp, &upper_list,
				 list) {
		handle_netdev(ib_dev, port, upper_iter->upper);
		dev_put(upper_iter->upper);
		list_del(&upper_iter->list);
		kfree(upper_iter);
	}
}

static void _roce_del_all_netdev_gids(struct ib_device *ib_dev, u8 port,
				      struct net_device *event_ndev)
{
	ib_cache_gid_del_all_netdev_gids(ib_dev, port, event_ndev);
}

static void del_netdev_upper_ips(struct ib_device *ib_dev, u8 port,
				 struct net_device *rdma_ndev, void *cookie)
{
	handle_netdev_upper(ib_dev, port, cookie, _roce_del_all_netdev_gids);
}

static void add_netdev_upper_ips(struct ib_device *ib_dev, u8 port,
				 struct net_device *rdma_ndev, void *cookie)
{
	handle_netdev_upper(ib_dev, port, cookie, _add_netdev_ips);
}

static void del_netdev_default_ips_join(struct ib_device *ib_dev, u8 port,
					struct net_device *rdma_ndev,
					void *cookie)
{
	struct net_device *master_ndev;

	rcu_read_lock();
	master_ndev = netdev_master_upper_dev_get_rcu(rdma_ndev);
	if (master_ndev)
		dev_hold(master_ndev);
	rcu_read_unlock();

	if (master_ndev) {
		bond_delete_netdev_default_gids(ib_dev, port, master_ndev,
						rdma_ndev);
		dev_put(master_ndev);
	}
}

static void del_netdev_default_ips(struct ib_device *ib_dev, u8 port,
				   struct net_device *rdma_ndev, void *cookie)
{
	struct net_device *event_ndev = (struct net_device *)cookie;

	bond_delete_netdev_default_gids(ib_dev, port, event_ndev, rdma_ndev);
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

	for (i = 0; i < ARRAY_SIZE(work->cmds) && work->cmds[i].cb; i++) {
		ib_enum_all_roce_netdevs(work->cmds[i].filter,
					 work->cmds[i].filter_ndev,
					 work->cmds[i].cb,
					 work->cmds[i].ndev);
		dev_put(work->cmds[i].ndev);
		dev_put(work->cmds[i].filter_ndev);
	}

	kfree(work);
}

static int netdevice_queue_work(struct netdev_event_work_cmd *cmds,
				struct net_device *ndev)
{
	unsigned int i;
	struct netdev_event_work *ndev_work =
		kmalloc(sizeof(*ndev_work), GFP_KERNEL);

	if (!ndev_work) {
		pr_warn("roce_gid_mgmt: can't allocate work for netdevice_event\n");
		return NOTIFY_DONE;
	}

	memcpy(ndev_work->cmds, cmds, sizeof(ndev_work->cmds));
	for (i = 0; i < ARRAY_SIZE(ndev_work->cmds) && ndev_work->cmds[i].cb; i++) {
		if (!ndev_work->cmds[i].ndev)
			ndev_work->cmds[i].ndev = ndev;
		if (!ndev_work->cmds[i].filter_ndev)
			ndev_work->cmds[i].filter_ndev = ndev;
		dev_hold(ndev_work->cmds[i].ndev);
		dev_hold(ndev_work->cmds[i].filter_ndev);
	}
	INIT_WORK(&ndev_work->work, netdevice_event_work_handler);

	queue_work(ib_wq, &ndev_work->work);

	return NOTIFY_DONE;
}

static const struct netdev_event_work_cmd add_cmd = {
	.cb = add_netdev_ips, .filter = is_eth_port_of_netdev};
static const struct netdev_event_work_cmd add_cmd_upper_ips = {
	.cb = add_netdev_upper_ips, .filter = is_eth_port_of_netdev};

static void netdevice_event_changeupper(struct netdev_notifier_changeupper_info *changeupper_info,
					struct netdev_event_work_cmd *cmds)
{
	static const struct netdev_event_work_cmd upper_ips_del_cmd = {
		.cb = del_netdev_upper_ips, .filter = upper_device_filter};
	static const struct netdev_event_work_cmd bonding_default_del_cmd = {
		.cb = del_netdev_default_ips, .filter = is_eth_port_inactive_slave};

	if (changeupper_info->linking == false) {
		cmds[0] = upper_ips_del_cmd;
		cmds[0].ndev = changeupper_info->upper_dev;
		cmds[1] = add_cmd;
	} else {
		cmds[0] = bonding_default_del_cmd;
		cmds[0].ndev = changeupper_info->upper_dev;
		cmds[1] = add_cmd_upper_ips;
		cmds[1].ndev = changeupper_info->upper_dev;
		cmds[1].filter_ndev = changeupper_info->upper_dev;
	}
}

static int netdevice_event(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	static const struct netdev_event_work_cmd del_cmd = {
		.cb = del_netdev_ips, .filter = pass_all_filter};
	static const struct netdev_event_work_cmd bonding_default_del_cmd_join = {
		.cb = del_netdev_default_ips_join, .filter = is_eth_port_inactive_slave};
	static const struct netdev_event_work_cmd default_del_cmd = {
		.cb = del_netdev_default_ips, .filter = pass_all_filter};
	static const struct netdev_event_work_cmd bonding_event_ips_del_cmd = {
		.cb = del_netdev_upper_ips, .filter = upper_device_filter};
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct netdev_event_work_cmd cmds[ROCE_NETDEV_CALLBACK_SZ] = { {NULL} };

	if (ndev->type != ARPHRD_ETHER)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
	case NETDEV_UP:
		cmds[0] = bonding_default_del_cmd_join;
		cmds[1] = add_cmd;
		break;

	case NETDEV_UNREGISTER:
		if (ndev->reg_state < NETREG_UNREGISTERED)
			cmds[0] = del_cmd;
		else
			return NOTIFY_DONE;
		break;

	case NETDEV_CHANGEADDR:
		cmds[0] = default_del_cmd;
		cmds[1] = add_cmd;
		break;

	case NETDEV_CHANGEUPPER:
		netdevice_event_changeupper(
			container_of(ptr, struct netdev_notifier_changeupper_info, info),
			cmds);
		break;

	case NETDEV_BONDING_FAILOVER:
		cmds[0] = bonding_event_ips_del_cmd;
		cmds[1] = bonding_default_del_cmd_join;
		cmds[2] = add_cmd_upper_ips;
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
