/* Copyright (c) 2013-2018, The Linux Foundation. All rights reserved.
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

/* Local Definitions and Declarations */

static const struct nla_policy rmnet_policy[IFLA_RMNET_MAX + 1] = {
	[IFLA_RMNET_MUX_ID]	= { .type = NLA_U16 },
	[IFLA_RMNET_FLAGS]	= { .len = sizeof(struct ifla_rmnet_flags) },
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

static int rmnet_unregister_real_device(struct net_device *real_dev)
{
	struct rmnet_port *port = rmnet_get_port_rtnl(real_dev);

	if (port->nr_rmnet_devs)
		return -EINVAL;

	netdev_rx_handler_unregister(real_dev);

	kfree(port);

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

	for (entry = 0; entry < RMNET_MAX_LOGICAL_EP; entry++)
		INIT_HLIST_HEAD(&port->muxed_ep[entry]);

	netdev_dbg(real_dev, "registered with rmnet\n");
	return 0;
}

static void rmnet_unregister_bridge(struct rmnet_port *port)
{
	struct net_device *bridge_dev, *real_dev, *rmnet_dev;
	struct rmnet_port *real_port;

	if (port->rmnet_mode != RMNET_EPMODE_BRIDGE)
		return;

	rmnet_dev = port->rmnet_dev;
	if (!port->nr_rmnet_devs) {
		/* bridge device */
		real_dev = port->bridge_ep;
		bridge_dev = port->dev;

		real_port = rmnet_get_port_rtnl(real_dev);
		real_port->bridge_ep = NULL;
		real_port->rmnet_mode = RMNET_EPMODE_VND;
	} else {
		/* real device */
		bridge_dev = port->bridge_ep;

		port->bridge_ep = NULL;
		port->rmnet_mode = RMNET_EPMODE_VND;
	}

	netdev_upper_dev_unlink(bridge_dev, rmnet_dev);
	rmnet_unregister_real_device(bridge_dev);
}

static int rmnet_newlink(struct net *src_net, struct net_device *dev,
			 struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	u32 data_format = RMNET_FLAGS_INGRESS_DEAGGREGATION;
	struct net_device *real_dev;
	int mode = RMNET_EPMODE_VND;
	struct rmnet_endpoint *ep;
	struct rmnet_port *port;
	int err = 0;
	u16 mux_id;

	if (!tb[IFLA_LINK]) {
		NL_SET_ERR_MSG_MOD(extack, "link not specified");
		return -EINVAL;
	}

	real_dev = __dev_get_by_index(src_net, nla_get_u32(tb[IFLA_LINK]));
	if (!real_dev || !dev)
		return -ENODEV;

	if (!data[IFLA_RMNET_MUX_ID])
		return -EINVAL;

	ep = kzalloc(sizeof(*ep), GFP_ATOMIC);
	if (!ep)
		return -ENOMEM;

	mux_id = nla_get_u16(data[IFLA_RMNET_MUX_ID]);

	err = rmnet_register_real_device(real_dev);
	if (err)
		goto err0;

	port = rmnet_get_port_rtnl(real_dev);
	err = rmnet_vnd_newlink(mux_id, dev, port, real_dev, ep);
	if (err)
		goto err1;

	err = netdev_upper_dev_link(real_dev, dev, extack);
	if (err < 0)
		goto err2;

	port->rmnet_mode = mode;
	port->rmnet_dev = dev;

	hlist_add_head_rcu(&ep->hlnode, &port->muxed_ep[mux_id]);

	if (data[IFLA_RMNET_FLAGS]) {
		struct ifla_rmnet_flags *flags;

		flags = nla_data(data[IFLA_RMNET_FLAGS]);
		data_format = flags->flags & flags->mask;
	}

	netdev_dbg(dev, "data format [0x%08X]\n", data_format);
	port->data_format = data_format;

	return 0;

err2:
	unregister_netdevice(dev);
	rmnet_vnd_dellink(mux_id, port, ep);
err1:
	rmnet_unregister_real_device(real_dev);
err0:
	kfree(ep);
	return err;
}

static void rmnet_dellink(struct net_device *dev, struct list_head *head)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct net_device *real_dev, *bridge_dev;
	struct rmnet_port *real_port, *bridge_port;
	struct rmnet_endpoint *ep;
	u8 mux_id = priv->mux_id;

	real_dev = priv->real_dev;

	if (!rmnet_is_real_dev_registered(real_dev))
		return;

	real_port = rmnet_get_port_rtnl(real_dev);
	bridge_dev = real_port->bridge_ep;
	if (bridge_dev) {
		bridge_port = rmnet_get_port_rtnl(bridge_dev);
		rmnet_unregister_bridge(bridge_port);
	}

	ep = rmnet_get_endpoint(real_port, mux_id);
	if (ep) {
		hlist_del_init_rcu(&ep->hlnode);
		rmnet_vnd_dellink(mux_id, real_port, ep);
		kfree(ep);
	}

	netdev_upper_dev_unlink(real_dev, dev);
	rmnet_unregister_real_device(real_dev);
	unregister_netdevice_queue(dev, head);
}

static void rmnet_force_unassociate_device(struct net_device *real_dev)
{
	struct hlist_node *tmp_ep;
	struct rmnet_endpoint *ep;
	struct rmnet_port *port;
	unsigned long bkt_ep;
	LIST_HEAD(list);

	port = rmnet_get_port_rtnl(real_dev);

	if (port->nr_rmnet_devs) {
		/* real device */
		rmnet_unregister_bridge(port);
		hash_for_each_safe(port->muxed_ep, bkt_ep, tmp_ep, ep, hlnode) {
			unregister_netdevice_queue(ep->egress_dev, &list);
			netdev_upper_dev_unlink(real_dev, ep->egress_dev);
			rmnet_vnd_dellink(ep->mux_id, port, ep);
			hlist_del_init_rcu(&ep->hlnode);
			kfree(ep);
		}
		rmnet_unregister_real_device(real_dev);
		unregister_netdevice_many(&list);
	} else {
		rmnet_unregister_bridge(port);
	}
}

static int rmnet_config_notify_cb(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct net_device *real_dev = netdev_notifier_info_to_dev(data);

	if (!rmnet_is_real_dev_registered(real_dev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UNREGISTER:
		netdev_dbg(real_dev, "Kernel unregister\n");
		rmnet_force_unassociate_device(real_dev);
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

	if (!data || !data[IFLA_RMNET_MUX_ID])
		return -EINVAL;

	mux_id = nla_get_u16(data[IFLA_RMNET_MUX_ID]);
	if (mux_id > (RMNET_MAX_LOGICAL_EP - 1))
		return -ERANGE;

	return 0;
}

static int rmnet_changelink(struct net_device *dev, struct nlattr *tb[],
			    struct nlattr *data[],
			    struct netlink_ext_ack *extack)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct net_device *real_dev;
	struct rmnet_endpoint *ep;
	struct rmnet_port *port;
	u16 mux_id;

	if (!dev)
		return -ENODEV;

	real_dev = priv->real_dev;
	if (!rmnet_is_real_dev_registered(real_dev))
		return -ENODEV;

	port = rmnet_get_port_rtnl(real_dev);

	if (data[IFLA_RMNET_MUX_ID]) {
		mux_id = nla_get_u16(data[IFLA_RMNET_MUX_ID]);
		if (rmnet_get_endpoint(port, mux_id)) {
			NL_SET_ERR_MSG_MOD(extack, "MUX ID already exists");
			return -EINVAL;
		}
		ep = rmnet_get_endpoint(port, priv->mux_id);
		if (!ep)
			return -ENODEV;

		hlist_del_init_rcu(&ep->hlnode);
		hlist_add_head_rcu(&ep->hlnode, &port->muxed_ep[mux_id]);

		ep->mux_id = mux_id;
		priv->mux_id = mux_id;
	}

	if (data[IFLA_RMNET_FLAGS]) {
		struct ifla_rmnet_flags *flags;

		flags = nla_data(data[IFLA_RMNET_FLAGS]);
		port->data_format = flags->flags & flags->mask;
	}

	return 0;
}

static size_t rmnet_get_size(const struct net_device *dev)
{
	return
		/* IFLA_RMNET_MUX_ID */
		nla_total_size(2) +
		/* IFLA_RMNET_FLAGS */
		nla_total_size(sizeof(struct ifla_rmnet_flags));
}

static int rmnet_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct net_device *real_dev;
	struct ifla_rmnet_flags f;
	struct rmnet_port *port;

	real_dev = priv->real_dev;

	if (nla_put_u16(skb, IFLA_RMNET_MUX_ID, priv->mux_id))
		goto nla_put_failure;

	if (rmnet_is_real_dev_registered(real_dev)) {
		port = rmnet_get_port_rtnl(real_dev);
		f.flags = port->data_format;
	} else {
		f.flags = 0;
	}

	f.mask  = ~0;

	if (nla_put(skb, IFLA_RMNET_FLAGS, sizeof(f), &f))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

struct rtnl_link_ops rmnet_link_ops __read_mostly = {
	.kind		= "rmnet",
	.maxtype	= __IFLA_RMNET_MAX,
	.priv_size	= sizeof(struct rmnet_priv),
	.setup		= rmnet_vnd_setup,
	.validate	= rmnet_rtnl_validate,
	.newlink	= rmnet_newlink,
	.dellink	= rmnet_dellink,
	.get_size	= rmnet_get_size,
	.changelink     = rmnet_changelink,
	.policy		= rmnet_policy,
	.fill_info	= rmnet_fill_info,
};

struct rmnet_port *rmnet_get_port_rcu(struct net_device *real_dev)
{
	if (rmnet_is_real_dev_registered(real_dev))
		return rcu_dereference_bh(real_dev->rx_handler_data);
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

	port = rmnet_get_port_rtnl(real_dev);

	/* If there is more than one rmnet dev attached, its probably being
	 * used for muxing. Skip the briding in that case
	 */
	if (port->nr_rmnet_devs > 1)
		return -EINVAL;

	if (port->rmnet_mode != RMNET_EPMODE_VND)
		return -EINVAL;

	if (rmnet_is_real_dev_registered(slave_dev))
		return -EBUSY;

	err = rmnet_register_real_device(slave_dev);
	if (err)
		return -EBUSY;

	err = netdev_master_upper_dev_link(slave_dev, rmnet_dev, NULL, NULL,
					   extack);
	if (err) {
		rmnet_unregister_real_device(slave_dev);
		return err;
	}

	slave_port = rmnet_get_port_rtnl(slave_dev);
	slave_port->rmnet_mode = RMNET_EPMODE_BRIDGE;
	slave_port->bridge_ep = real_dev;
	slave_port->rmnet_dev = rmnet_dev;

	port->rmnet_mode = RMNET_EPMODE_BRIDGE;
	port->bridge_ep = slave_dev;

	netdev_dbg(slave_dev, "registered with rmnet as slave\n");
	return 0;
}

int rmnet_del_bridge(struct net_device *rmnet_dev,
		     struct net_device *slave_dev)
{
	struct rmnet_port *port = rmnet_get_port_rtnl(slave_dev);

	rmnet_unregister_bridge(port);

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
	rtnl_link_unregister(&rmnet_link_ops);
	unregister_netdevice_notifier(&rmnet_dev_notifier);
}

module_init(rmnet_init)
module_exit(rmnet_exit)
MODULE_LICENSE("GPL v2");
