// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2021 Taehee Yoo <ap420073@gmail.com> */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/jhash.h>
#include <linux/if_tunnel.h>
#include <linux/net.h>
#include <linux/igmp.h>
#include <linux/workqueue.h>
#include <net/net_namespace.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/icmp.h>
#include <net/mld.h>
#include <net/amt.h>
#include <uapi/linux/amt.h>
#include <linux/security.h>
#include <net/gro_cells.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/if_inet6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/inet_common.h>

static struct workqueue_struct *amt_wq;

static struct socket *amt_create_sock(struct net *net, __be16 port)
{
	struct udp_port_cfg udp_conf;
	struct socket *sock;
	int err;

	memset(&udp_conf, 0, sizeof(udp_conf));
	udp_conf.family = AF_INET;
	udp_conf.local_ip.s_addr = htonl(INADDR_ANY);

	udp_conf.local_udp_port = port;

	err = udp_sock_create(net, &udp_conf, &sock);
	if (err < 0)
		return ERR_PTR(err);

	return sock;
}

static int amt_socket_create(struct amt_dev *amt)
{
	struct udp_tunnel_sock_cfg tunnel_cfg;
	struct socket *sock;

	sock = amt_create_sock(amt->net, amt->relay_port);
	if (IS_ERR(sock))
		return PTR_ERR(sock);

	/* Mark socket as an encapsulation socket */
	memset(&tunnel_cfg, 0, sizeof(tunnel_cfg));
	tunnel_cfg.sk_user_data = amt;
	tunnel_cfg.encap_type = 1;
	tunnel_cfg.encap_destroy = NULL;
	setup_udp_tunnel_sock(amt->net, sock, &tunnel_cfg);

	rcu_assign_pointer(amt->sock, sock);
	return 0;
}

static int amt_dev_open(struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);
	int err;

	amt->ready4 = false;
	amt->ready6 = false;

	err = amt_socket_create(amt);
	if (err)
		return err;

	amt->req_cnt = 0;
	amt->remote_ip = 0;
	get_random_bytes(&amt->key, sizeof(siphash_key_t));

	amt->status = AMT_STATUS_INIT;
	return err;
}

static int amt_dev_stop(struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);
	struct socket *sock;

	/* shutdown */
	sock = rtnl_dereference(amt->sock);
	RCU_INIT_POINTER(amt->sock, NULL);
	synchronize_net();
	if (sock)
		udp_tunnel_sock_release(sock);

	amt->ready4 = false;
	amt->ready6 = false;
	amt->req_cnt = 0;
	amt->remote_ip = 0;

	return 0;
}

static const struct device_type amt_type = {
	.name = "amt",
};

static int amt_dev_init(struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);
	int err;

	amt->dev = dev;
	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;

	err = gro_cells_init(&amt->gro_cells, dev);
	if (err) {
		free_percpu(dev->tstats);
		return err;
	}

	return 0;
}

static void amt_dev_uninit(struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);

	gro_cells_destroy(&amt->gro_cells);
	free_percpu(dev->tstats);
}

static const struct net_device_ops amt_netdev_ops = {
	.ndo_init               = amt_dev_init,
	.ndo_uninit             = amt_dev_uninit,
	.ndo_open		= amt_dev_open,
	.ndo_stop		= amt_dev_stop,
	.ndo_get_stats64        = dev_get_tstats64,
};

static void amt_link_setup(struct net_device *dev)
{
	dev->netdev_ops         = &amt_netdev_ops;
	dev->needs_free_netdev  = true;
	SET_NETDEV_DEVTYPE(dev, &amt_type);
	dev->min_mtu		= ETH_MIN_MTU;
	dev->max_mtu		= ETH_MAX_MTU;
	dev->type		= ARPHRD_NONE;
	dev->flags		= IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	dev->hard_header_len	= 0;
	dev->addr_len		= 0;
	dev->priv_flags		|= IFF_NO_QUEUE;
	dev->features		|= NETIF_F_LLTX;
	dev->features		|= NETIF_F_GSO_SOFTWARE;
	dev->features		|= NETIF_F_NETNS_LOCAL;
	dev->hw_features	|= NETIF_F_SG | NETIF_F_HW_CSUM;
	dev->hw_features	|= NETIF_F_FRAGLIST | NETIF_F_RXCSUM;
	dev->hw_features	|= NETIF_F_GSO_SOFTWARE;
	eth_hw_addr_random(dev);
	eth_zero_addr(dev->broadcast);
	ether_setup(dev);
}

static const struct nla_policy amt_policy[IFLA_AMT_MAX + 1] = {
	[IFLA_AMT_MODE]		= { .type = NLA_U32 },
	[IFLA_AMT_RELAY_PORT]	= { .type = NLA_U16 },
	[IFLA_AMT_GATEWAY_PORT]	= { .type = NLA_U16 },
	[IFLA_AMT_LINK]		= { .type = NLA_U32 },
	[IFLA_AMT_LOCAL_IP]	= { .len = sizeof_field(struct iphdr, daddr) },
	[IFLA_AMT_REMOTE_IP]	= { .len = sizeof_field(struct iphdr, daddr) },
	[IFLA_AMT_DISCOVERY_IP]	= { .len = sizeof_field(struct iphdr, daddr) },
	[IFLA_AMT_MAX_TUNNELS]	= { .type = NLA_U32 },
};

static int amt_validate(struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	if (!data)
		return -EINVAL;

	if (!data[IFLA_AMT_LINK]) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_AMT_LINK],
				    "Link attribute is required");
		return -EINVAL;
	}

	if (!data[IFLA_AMT_MODE]) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_AMT_MODE],
				    "Mode attribute is required");
		return -EINVAL;
	}

	if (nla_get_u32(data[IFLA_AMT_MODE]) > AMT_MODE_MAX) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_AMT_MODE],
				    "Mode attribute is not valid");
		return -EINVAL;
	}

	if (!data[IFLA_AMT_LOCAL_IP]) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_AMT_DISCOVERY_IP],
				    "Local attribute is required");
		return -EINVAL;
	}

	if (!data[IFLA_AMT_DISCOVERY_IP] &&
	    nla_get_u32(data[IFLA_AMT_MODE]) == AMT_MODE_GATEWAY) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_AMT_LOCAL_IP],
				    "Discovery attribute is required");
		return -EINVAL;
	}

	return 0;
}

static int amt_newlink(struct net *net, struct net_device *dev,
		       struct nlattr *tb[], struct nlattr *data[],
		       struct netlink_ext_ack *extack)
{
	struct amt_dev *amt = netdev_priv(dev);
	int err = -EINVAL;

	amt->net = net;
	amt->mode = nla_get_u32(data[IFLA_AMT_MODE]);

	if (data[IFLA_AMT_MAX_TUNNELS])
		amt->max_tunnels = nla_get_u32(data[IFLA_AMT_MAX_TUNNELS]);
	else
		amt->max_tunnels = AMT_MAX_TUNNELS;

	spin_lock_init(&amt->lock);
	amt->max_groups = AMT_MAX_GROUP;
	amt->max_sources = AMT_MAX_SOURCE;
	amt->hash_buckets = AMT_HSIZE;
	amt->nr_tunnels = 0;
	get_random_bytes(&amt->hash_seed, sizeof(amt->hash_seed));
	amt->stream_dev = dev_get_by_index(net,
					   nla_get_u32(data[IFLA_AMT_LINK]));
	if (!amt->stream_dev) {
		NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_LINK],
				    "Can't find stream device");
		return -ENODEV;
	}

	if (amt->stream_dev->type != ARPHRD_ETHER) {
		NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_LINK],
				    "Invalid stream device type");
		goto err;
	}

	amt->local_ip = nla_get_in_addr(data[IFLA_AMT_LOCAL_IP]);
	if (ipv4_is_loopback(amt->local_ip) ||
	    ipv4_is_zeronet(amt->local_ip) ||
	    ipv4_is_multicast(amt->local_ip)) {
		NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_LOCAL_IP],
				    "Invalid Local address");
		goto err;
	}

	if (data[IFLA_AMT_RELAY_PORT])
		amt->relay_port = nla_get_be16(data[IFLA_AMT_RELAY_PORT]);
	else
		amt->relay_port = htons(IANA_AMT_UDP_PORT);

	if (data[IFLA_AMT_GATEWAY_PORT])
		amt->gw_port = nla_get_be16(data[IFLA_AMT_GATEWAY_PORT]);
	else
		amt->gw_port = htons(IANA_AMT_UDP_PORT);

	if (!amt->relay_port) {
		NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_DISCOVERY_IP],
				    "relay port must not be 0");
		goto err;
	}
	if (amt->mode == AMT_MODE_RELAY) {
		amt->qrv = amt->net->ipv4.sysctl_igmp_qrv;
		amt->qri = 10;
		dev->needed_headroom = amt->stream_dev->needed_headroom +
				       AMT_RELAY_HLEN;
		dev->mtu = amt->stream_dev->mtu - AMT_RELAY_HLEN;
		dev->max_mtu = dev->mtu;
		dev->min_mtu = ETH_MIN_MTU + AMT_RELAY_HLEN;
	} else {
		if (!data[IFLA_AMT_DISCOVERY_IP]) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_DISCOVERY_IP],
					    "discovery must be set in gateway mode");
			goto err;
		}
		if (!amt->gw_port) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_DISCOVERY_IP],
					    "gateway port must not be 0");
			goto err;
		}
		amt->remote_ip = 0;
		amt->discovery_ip = nla_get_in_addr(data[IFLA_AMT_DISCOVERY_IP]);
		if (ipv4_is_loopback(amt->discovery_ip) ||
		    ipv4_is_zeronet(amt->discovery_ip) ||
		    ipv4_is_multicast(amt->discovery_ip)) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_AMT_DISCOVERY_IP],
					    "discovery must be unicast");
			goto err;
		}

		dev->needed_headroom = amt->stream_dev->needed_headroom +
				       AMT_GW_HLEN;
		dev->mtu = amt->stream_dev->mtu - AMT_GW_HLEN;
		dev->max_mtu = dev->mtu;
		dev->min_mtu = ETH_MIN_MTU + AMT_GW_HLEN;
	}
	amt->qi = AMT_INIT_QUERY_INTERVAL;

	err = register_netdevice(dev);
	if (err < 0) {
		netdev_dbg(dev, "failed to register new netdev %d\n", err);
		goto err;
	}

	err = netdev_upper_dev_link(amt->stream_dev, dev, extack);
	if (err < 0) {
		unregister_netdevice(dev);
		goto err;
	}

	return 0;
err:
	dev_put(amt->stream_dev);
	return err;
}

static void amt_dellink(struct net_device *dev, struct list_head *head)
{
	struct amt_dev *amt = netdev_priv(dev);

	unregister_netdevice_queue(dev, head);
	netdev_upper_dev_unlink(amt->stream_dev, dev);
	dev_put(amt->stream_dev);
}

static size_t amt_get_size(const struct net_device *dev)
{
	return nla_total_size(sizeof(__u32)) + /* IFLA_AMT_MODE */
	       nla_total_size(sizeof(__u16)) + /* IFLA_AMT_RELAY_PORT */
	       nla_total_size(sizeof(__u16)) + /* IFLA_AMT_GATEWAY_PORT */
	       nla_total_size(sizeof(__u32)) + /* IFLA_AMT_LINK */
	       nla_total_size(sizeof(__u32)) + /* IFLA_MAX_TUNNELS */
	       nla_total_size(sizeof(struct iphdr)) + /* IFLA_AMT_DISCOVERY_IP */
	       nla_total_size(sizeof(struct iphdr)) + /* IFLA_AMT_REMOTE_IP */
	       nla_total_size(sizeof(struct iphdr)); /* IFLA_AMT_LOCAL_IP */
}

static int amt_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct amt_dev *amt = netdev_priv(dev);

	if (nla_put_u32(skb, IFLA_AMT_MODE, amt->mode))
		goto nla_put_failure;
	if (nla_put_be16(skb, IFLA_AMT_RELAY_PORT, amt->relay_port))
		goto nla_put_failure;
	if (nla_put_be16(skb, IFLA_AMT_GATEWAY_PORT, amt->gw_port))
		goto nla_put_failure;
	if (nla_put_u32(skb, IFLA_AMT_LINK, amt->stream_dev->ifindex))
		goto nla_put_failure;
	if (nla_put_in_addr(skb, IFLA_AMT_LOCAL_IP, amt->local_ip))
		goto nla_put_failure;
	if (nla_put_in_addr(skb, IFLA_AMT_DISCOVERY_IP, amt->discovery_ip))
		goto nla_put_failure;
	if (amt->remote_ip)
		if (nla_put_in_addr(skb, IFLA_AMT_REMOTE_IP, amt->remote_ip))
			goto nla_put_failure;
	if (nla_put_u32(skb, IFLA_AMT_MAX_TUNNELS, amt->max_tunnels))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct rtnl_link_ops amt_link_ops __read_mostly = {
	.kind		= "amt",
	.maxtype	= IFLA_AMT_MAX,
	.policy		= amt_policy,
	.priv_size	= sizeof(struct amt_dev),
	.setup		= amt_link_setup,
	.validate	= amt_validate,
	.newlink	= amt_newlink,
	.dellink	= amt_dellink,
	.get_size       = amt_get_size,
	.fill_info      = amt_fill_info,
};

static struct net_device *amt_lookup_upper_dev(struct net_device *dev)
{
	struct net_device *upper_dev;
	struct amt_dev *amt;

	for_each_netdev(dev_net(dev), upper_dev) {
		if (netif_is_amt(upper_dev)) {
			amt = netdev_priv(upper_dev);
			if (amt->stream_dev == dev)
				return upper_dev;
		}
	}

	return NULL;
}

static int amt_device_event(struct notifier_block *unused,
			    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net_device *upper_dev;
	struct amt_dev *amt;
	LIST_HEAD(list);
	int new_mtu;

	upper_dev = amt_lookup_upper_dev(dev);
	if (!upper_dev)
		return NOTIFY_DONE;
	amt = netdev_priv(upper_dev);

	switch (event) {
	case NETDEV_UNREGISTER:
		amt_dellink(amt->dev, &list);
		unregister_netdevice_many(&list);
		break;
	case NETDEV_CHANGEMTU:
		if (amt->mode == AMT_MODE_RELAY)
			new_mtu = dev->mtu - AMT_RELAY_HLEN;
		else
			new_mtu = dev->mtu - AMT_GW_HLEN;

		dev_set_mtu(amt->dev, new_mtu);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block amt_notifier_block __read_mostly = {
	.notifier_call = amt_device_event,
};

static int __init amt_init(void)
{
	int err;

	err = register_netdevice_notifier(&amt_notifier_block);
	if (err < 0)
		goto err;

	err = rtnl_link_register(&amt_link_ops);
	if (err < 0)
		goto unregister_notifier;

	amt_wq = alloc_workqueue("amt", WQ_UNBOUND, 1);
	if (!amt_wq)
		goto rtnl_unregister;

	return 0;

rtnl_unregister:
	rtnl_link_unregister(&amt_link_ops);
unregister_notifier:
	unregister_netdevice_notifier(&amt_notifier_block);
err:
	pr_err("error loading AMT module loaded\n");
	return err;
}
late_initcall(amt_init);

static void __exit amt_fini(void)
{
	rtnl_link_unregister(&amt_link_ops);
	unregister_netdevice_notifier(&amt_notifier_block);
	destroy_workqueue(amt_wq);
}
module_exit(amt_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Taehee Yoo <ap420073@gmail.com>");
MODULE_ALIAS_RTNL_LINK("amt");
