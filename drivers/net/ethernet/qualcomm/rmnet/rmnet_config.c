/* Copyright (c) 2013-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * RMNET configuration engine
 *
 */

#include <net/sock.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netdevice.h>
#include "rmnet_config.h"
#include "rmnet_handlers.h"
#include "rmnet_vnd.h"
#include "rmnet_private.h"

/* Locking scheme -
 * The shared resource which needs to be protected is realdev->rx_handler_data.
 * For the writer path, this is using rtnl_lock(). The writer paths are
 * rmnet_newlink(), rmnet_dellink() and rmnet_force_unassociate_device(). These
 * paths are already called with rtnl_lock() acquired in. There is also an
 * ASSERT_RTNL() to ensure that we are calling with rtnl acquired. For
 * dereference here, we will need to use rtnl_dereference(). Dev list writing
 * needs to happen with rtnl_lock() acquired for netdev_master_upper_dev_link().
 * For the reader path, the real_dev->rx_handler_data is called in the TX / RX
 * path. We only need rcu_read_lock() for these scenarios. In these cases,
 * the rcu_read_lock() is held in __dev_queue_xmit() and
 * netif_receive_skb_internal(), so readers need to use rcu_dereference_rtnl()
 * to get the relevant information. For dev list reading, we again acquire
 * rcu_read_lock() in rmnet_dellink() for netdev_master_upper_dev_get_rcu().
 * We also use unregister_netdevice_many() to free all rmnet devices in
 * rmnet_force_unassociate_device() so we dont lose the rtnl_lock() and free in
 * same context.
 */

/* Local Definitions and Declarations */

struct rmnet_walk_data {
	struct net_device *real_dev;
	struct list_head *head;
	struct rmnet_port *port;
};

static int rmnet_is_real_dev_registered(const struct net_device *real_dev)
{
	return rcu_access_pointer(real_dev->rx_handler) == rmnet_rx_handler;
}

/* Needs rtnl lock */
static struct rmnet_port*
rmnet_get_port_rtnl(const struct net_device *real_dev)
{
	return rtnl_dereference(real_dev->rx_handler_data);
}

static int rmnet_unregister_real_device(struct net_device *real_dev,
					struct rmnet_port *port)
{
	if (port->nr_rmnet_devs)
		return -EINVAL;

	kfree(port);

	netdev_rx_handler_unregister(real_dev);

	/* release reference on real_dev */
	dev_put(real_dev);

	netdev_dbg(real_dev, "Removed from rmnet\n");
	return 0;
}

static int rmnet_register_real_device(struct net_device *real_dev)
{
	struct rmnet_port *port;
	int rc, entry;

	ASSERT_RTNL();

	if (rmnet_is_real_dev_registered(real_dev))
		return 0;

	port = kzalloc(sizeof(*port), GFP_ATOMIC);
	if (!port)
		return -ENOMEM;

	port->dev = real_dev;
	rc = netdev_rx_handler_register(real_dev, rmnet_rx_handler, port);
	if (rc) {
		kfree(port);
		return -EBUSY;
	}

	/* hold on to real dev for MAP data */
	dev_hold(real_dev);

	for (entry = 0; entry < RMNET_MAX_LOGICAL_EP; entry++)
		INIT_HLIST_HEAD(&port->muxed_ep[entry]);

	netdev_dbg(real_dev, "registered with rmnet\n");
	return 0;
}

static void rmnet_unregister_bridge(struct net_device *dev,
				    struct rmnet_port *port)
{
	struct net_device *rmnet_dev, *bridge_dev;
	struct rmnet_port *bridge_port;

	if (port->rmnet_mode != RMNET_EPMODE_BRIDGE)
		return;

	/* bridge slave handling */
	if (!port->nr_rmnet_devs) {
		rmnet_dev = netdev_master_upper_dev_get_rcu(dev);
		netdev_upper_dev_unlink(dev, rmnet_dev);

		bridge_dev = port->bridge_ep;

		bridge_port = rmnet_get_port_rtnl(bridge_dev);
		bridge_port->bridge_ep = NULL;
		bridge_port->rmnet_mode = RMNET_EPMODE_VND;
	} else {
		bridge_dev = port->bridge_ep;

		bridge_port = rmnet_get_port_rtnl(bridge_dev);
		rmnet_dev = netdev_master_upper_dev_get_rcu(bridge_dev);
		netdev_upper_dev_unlink(bridge_dev, rmnet_dev);

		rmnet_unregister_real_device(bridge_dev, bridge_port);
	}
}

static int rmnet_newlink(struct net *src_net, struct net_device *dev,
			 struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	int ingress_format = RMNET_INGRESS_FORMAT_DEMUXING |
			     RMNET_INGRESS_FORMAT_DEAGGREGATION |
			     RMNET_INGRESS_FORMAT_MAP;
	int egress_format = RMNET_EGRESS_FORMAT_MUXING |
			    RMNET_EGRESS_FORMAT_MAP;
	struct net_device *real_dev;
	int mode = RMNET_EPMODE_VND;
	struct rmnet_endpoint *ep;
	struct rmnet_port *port;
	int err = 0;
	u16 mux_id;

	real_dev = __dev_get_by_index(src_net, nla_get_u32(tb[IFLA_LINK]));
	if (!real_dev || !dev)
		return -ENODEV;

	if (!data[IFLA_VLAN_ID])
		return -EINVAL;

	ep = kzalloc(sizeof(*ep), GFP_ATOMIC);
	if (!ep)
		return -ENOMEM;

	mux_id = nla_get_u16(data[IFLA_VLAN_ID]);

	err = rmnet_register_real_device(real_dev);
	if (err)
		goto err0;

	port = rmnet_get_port_rtnl(real_dev);
	err = rmnet_vnd_newlink(mux_id, dev, port, real_dev, ep);
	if (err)
		goto err1;

	err = netdev_master_upper_dev_link(dev, real_dev, NULL, NULL, extack);
	if (err)
		goto err2;

	netdev_dbg(dev, "data format [ingress 0x%08X] [egress 0x%08X]\n",
		   ingress_format, egress_format);
	port->egress_data_format = egress_format;
	port->ingress_data_format = ingress_format;
	port->rmnet_mode = mode;

	hlist_add_head_rcu(&ep->hlnode, &port->muxed_ep[mux_id]);
	return 0;

err2:
	rmnet_vnd_dellink(mux_id, port, ep);
err1:
	rmnet_unregister_real_device(real_dev, port);
err0:
	return err;
}

static void rmnet_dellink(struct net_device *dev, struct list_head *head)
{
	struct net_device *real_dev;
	struct rmnet_endpoint *ep;
	struct rmnet_port *port;
	u8 mux_id;

	rcu_read_lock();
	real_dev = netdev_master_upper_dev_get_rcu(dev);
	rcu_read_unlock();

	if (!real_dev || !rmnet_is_real_dev_registered(real_dev))
		return;

	port = rmnet_get_port_rtnl(real_dev);

	mux_id = rmnet_vnd_get_mux(dev);
	netdev_upper_dev_unlink(dev, real_dev);

	ep = rmnet_get_endpoint(port, mux_id);
	if (ep) {
		hlist_del_init_rcu(&ep->hlnode);
		rmnet_unregister_bridge(dev, port);
		rmnet_vnd_dellink(mux_id, port, ep);
		kfree(ep);
	}
	rmnet_unregister_real_device(real_dev, port);

	unregister_netdevice_queue(dev, head);
}

static int rmnet_dev_walk_unreg(struct net_device *rmnet_dev, void *data)
{
	struct rmnet_walk_data *d = data;
	struct rmnet_endpoint *ep;
	u8 mux_id;

	mux_id = rmnet_vnd_get_mux(rmnet_dev);
	ep = rmnet_get_endpoint(d->port, mux_id);
	if (ep) {
		hlist_del_init_rcu(&ep->hlnode);
		rmnet_vnd_dellink(mux_id, d->port, ep);
		kfree(ep);
	}
	netdev_upper_dev_unlink(rmnet_dev, d->real_dev);
	unregister_netdevice_queue(rmnet_dev, d->head);

	return 0;
}

static void rmnet_force_unassociate_device(struct net_device *dev)
{
	struct net_device *real_dev = dev;
	struct rmnet_walk_data d;
	struct rmnet_port *port;
	LIST_HEAD(list);

	if (!rmnet_is_real_dev_registered(real_dev))
		return;

	ASSERT_RTNL();

	d.real_dev = real_dev;
	d.head = &list;

	port = rmnet_get_port_rtnl(dev);
	d.port = port;

	rcu_read_lock();
	rmnet_unregister_bridge(dev, port);

	netdev_walk_all_lower_dev_rcu(real_dev, rmnet_dev_walk_unreg, &d);
	rcu_read_unlock();
	unregister_netdevice_many(&list);

	rmnet_unregister_real_device(real_dev, port);
}

static int rmnet_config_notify_cb(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct net_device *dev = netdev_notifier_info_to_dev(data);

	if (!dev)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UNREGISTER:
		netdev_dbg(dev, "Kernel unregister\n");
		rmnet_force_unassociate_device(dev);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block rmnet_dev_notifier __read_mostly = {
	.notifier_call = rmnet_config_notify_cb,
};

static int rmnet_rtnl_validate(struct nlattr *tb[], struct nlattr *data[],
			       struct netlink_ext_ack *extack)
{
	u16 mux_id;

	if (!data || !data[IFLA_VLAN_ID])
		return -EINVAL;

	mux_id = nla_get_u16(data[IFLA_VLAN_ID]);
	if (mux_id > (RMNET_MAX_LOGICAL_EP - 1))
		return -ERANGE;

	return 0;
}

static size_t rmnet_get_size(const struct net_device *dev)
{
	return nla_total_size(2); /* IFLA_VLAN_ID */
}

struct rtnl_link_ops rmnet_link_ops __read_mostly = {
	.kind		= "rmnet",
	.maxtype	= __IFLA_VLAN_MAX,
	.priv_size	= sizeof(struct rmnet_priv),
	.setup		= rmnet_vnd_setup,
	.validate	= rmnet_rtnl_validate,
	.newlink	= rmnet_newlink,
	.dellink	= rmnet_dellink,
	.get_size	= rmnet_get_size,
};

/* Needs either rcu_read_lock() or rtnl lock */
struct rmnet_port *rmnet_get_port(struct net_device *real_dev)
{
	if (rmnet_is_real_dev_registered(real_dev))
		return rcu_dereference_rtnl(real_dev->rx_handler_data);
	else
		return NULL;
}

struct rmnet_endpoint *rmnet_get_endpoint(struct rmnet_port *port, u8 mux_id)
{
	struct rmnet_endpoint *ep;

	hlist_for_each_entry_rcu(ep, &port->muxed_ep[mux_id], hlnode) {
		if (ep->mux_id == mux_id)
			return ep;
	}

	return NULL;
}

int rmnet_add_bridge(struct net_device *rmnet_dev,
		     struct net_device *slave_dev,
		     struct netlink_ext_ack *extack)
{
	struct rmnet_priv *priv = netdev_priv(rmnet_dev);
	struct net_device *real_dev = priv->real_dev;
	struct rmnet_port *port, *slave_port;
	int err;

	port = rmnet_get_port(real_dev);

	/* If there is more than one rmnet dev attached, its probably being
	 * used for muxing. Skip the briding in that case
	 */
	if (port->nr_rmnet_devs > 1)
		return -EINVAL;

	if (rmnet_is_real_dev_registered(slave_dev))
		return -EBUSY;

	err = rmnet_register_real_device(slave_dev);
	if (err)
		return -EBUSY;

	err = netdev_master_upper_dev_link(slave_dev, rmnet_dev, NULL, NULL,
					   extack);
	if (err)
		return -EINVAL;

	slave_port = rmnet_get_port(slave_dev);
	slave_port->rmnet_mode = RMNET_EPMODE_BRIDGE;
	slave_port->bridge_ep = real_dev;

	port->rmnet_mode = RMNET_EPMODE_BRIDGE;
	port->bridge_ep = slave_dev;

	netdev_dbg(slave_dev, "registered with rmnet as slave\n");
	return 0;
}

int rmnet_del_bridge(struct net_device *rmnet_dev,
		     struct net_device *slave_dev)
{
	struct rmnet_priv *priv = netdev_priv(rmnet_dev);
	struct net_device *real_dev = priv->real_dev;
	struct rmnet_port *port, *slave_port;

	port = rmnet_get_port(real_dev);
	port->rmnet_mode = RMNET_EPMODE_VND;
	port->bridge_ep = NULL;

	netdev_upper_dev_unlink(slave_dev, rmnet_dev);
	slave_port = rmnet_get_port(slave_dev);
	rmnet_unregister_real_device(slave_dev, slave_port);

	netdev_dbg(slave_dev, "removed from rmnet as slave\n");
	return 0;
}

/* Startup/Shutdown */

static int __init rmnet_init(void)
{
	int rc;

	rc = register_netdevice_notifier(&rmnet_dev_notifier);
	if (rc != 0)
		return rc;

	rc = rtnl_link_register(&rmnet_link_ops);
	if (rc != 0) {
		unregister_netdevice_notifier(&rmnet_dev_notifier);
		return rc;
	}
	return rc;
}

static void __exit rmnet_exit(void)
{
	unregister_netdevice_notifier(&rmnet_dev_notifier);
	rtnl_link_unregister(&rmnet_link_ops);
}

module_init(rmnet_init)
module_exit(rmnet_exit)
MODULE_LICENSE("GPL v2");
