// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PFCP according to 3GPP TS 29.244
 *
 * Copyright (C) 2022, Intel Corporation.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/skbuff.h>
#include <linux/types.h>

#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/pfcp.h>

struct pfcp_dev {
	struct list_head	list;

	struct socket		*sock;
	struct net_device	*dev;
	struct net		*net;
};

static unsigned int pfcp_net_id __read_mostly;

struct pfcp_net {
	struct list_head	pfcp_dev_list;
};

static void pfcp_del_sock(struct pfcp_dev *pfcp)
{
	udp_tunnel_sock_release(pfcp->sock);
	pfcp->sock = NULL;
}

static void pfcp_dev_uninit(struct net_device *dev)
{
	struct pfcp_dev *pfcp = netdev_priv(dev);

	pfcp_del_sock(pfcp);
}

static int pfcp_dev_init(struct net_device *dev)
{
	struct pfcp_dev *pfcp = netdev_priv(dev);

	pfcp->dev = dev;

	return 0;
}

static const struct net_device_ops pfcp_netdev_ops = {
	.ndo_init		= pfcp_dev_init,
	.ndo_uninit		= pfcp_dev_uninit,
	.ndo_get_stats64	= dev_get_tstats64,
};

static const struct device_type pfcp_type = {
	.name = "pfcp",
};

static void pfcp_link_setup(struct net_device *dev)
{
	dev->netdev_ops = &pfcp_netdev_ops;
	dev->needs_free_netdev = true;
	SET_NETDEV_DEVTYPE(dev, &pfcp_type);

	dev->hard_header_len = 0;
	dev->addr_len = 0;

	dev->type = ARPHRD_NONE;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	dev->priv_flags |= IFF_NO_QUEUE;

	netif_keep_dst(dev);
}

static struct socket *pfcp_create_sock(struct pfcp_dev *pfcp)
{
	struct udp_tunnel_sock_cfg tuncfg = {};
	struct udp_port_cfg udp_conf = {
		.local_ip.s_addr	= htonl(INADDR_ANY),
		.family			= AF_INET,
	};
	struct net *net = pfcp->net;
	struct socket *sock;
	int err;

	udp_conf.local_udp_port = htons(PFCP_PORT);

	err = udp_sock_create(net, &udp_conf, &sock);
	if (err)
		return ERR_PTR(err);

	setup_udp_tunnel_sock(net, sock, &tuncfg);

	return sock;
}

static int pfcp_add_sock(struct pfcp_dev *pfcp)
{
	pfcp->sock = pfcp_create_sock(pfcp);

	return PTR_ERR_OR_ZERO(pfcp->sock);
}

static int pfcp_newlink(struct net *net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	struct pfcp_dev *pfcp = netdev_priv(dev);
	struct pfcp_net *pn;
	int err;

	pfcp->net = net;

	err = pfcp_add_sock(pfcp);
	if (err) {
		netdev_dbg(dev, "failed to add pfcp socket %d\n", err);
		goto exit_err;
	}

	err = register_netdevice(dev);
	if (err) {
		netdev_dbg(dev, "failed to register pfcp netdev %d\n", err);
		goto exit_del_pfcp_sock;
	}

	pn = net_generic(dev_net(dev), pfcp_net_id);
	list_add_rcu(&pfcp->list, &pn->pfcp_dev_list);

	netdev_dbg(dev, "registered new PFCP interface\n");

	return 0;

exit_del_pfcp_sock:
	pfcp_del_sock(pfcp);
exit_err:
	pfcp->net = NULL;
	return err;
}

static void pfcp_dellink(struct net_device *dev, struct list_head *head)
{
	struct pfcp_dev *pfcp = netdev_priv(dev);

	list_del_rcu(&pfcp->list);
	unregister_netdevice_queue(dev, head);
}

static struct rtnl_link_ops pfcp_link_ops __read_mostly = {
	.kind		= "pfcp",
	.priv_size	= sizeof(struct pfcp_dev),
	.setup		= pfcp_link_setup,
	.newlink	= pfcp_newlink,
	.dellink	= pfcp_dellink,
};

static int __net_init pfcp_net_init(struct net *net)
{
	struct pfcp_net *pn = net_generic(net, pfcp_net_id);

	INIT_LIST_HEAD(&pn->pfcp_dev_list);
	return 0;
}

static void __net_exit pfcp_net_exit(struct net *net)
{
	struct pfcp_net *pn = net_generic(net, pfcp_net_id);
	struct pfcp_dev *pfcp;
	LIST_HEAD(list);

	rtnl_lock();
	list_for_each_entry(pfcp, &pn->pfcp_dev_list, list)
		pfcp_dellink(pfcp->dev, &list);

	unregister_netdevice_many(&list);
	rtnl_unlock();
}

static struct pernet_operations pfcp_net_ops = {
	.init	= pfcp_net_init,
	.exit	= pfcp_net_exit,
	.id	= &pfcp_net_id,
	.size	= sizeof(struct pfcp_net),
};

static int __init pfcp_init(void)
{
	int err;

	err = register_pernet_subsys(&pfcp_net_ops);
	if (err)
		goto exit_err;

	err = rtnl_link_register(&pfcp_link_ops);
	if (err)
		goto exit_unregister_subsys;
	return 0;

exit_unregister_subsys:
	unregister_pernet_subsys(&pfcp_net_ops);
exit_err:
	pr_err("loading PFCP module failed: err %d\n", err);
	return err;
}
late_initcall(pfcp_init);

static void __exit pfcp_exit(void)
{
	rtnl_link_unregister(&pfcp_link_ops);
	unregister_pernet_subsys(&pfcp_net_ops);

	pr_info("PFCP module unloaded\n");
}
module_exit(pfcp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wojciech Drewek <wojciech.drewek@intel.com>");
MODULE_DESCRIPTION("Interface driver for PFCP encapsulated traffic");
MODULE_ALIAS_RTNL_LINK("pfcp");
