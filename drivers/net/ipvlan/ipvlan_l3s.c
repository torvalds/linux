// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2014 Mahesh Bandewar <maheshb@google.com>
 */

#include "ipvlan.h"

static unsigned int ipvlan_netid __read_mostly;

struct ipvlan_netns {
	unsigned int ipvl_nf_hook_refcnt;
};

static struct ipvl_addr *ipvlan_skb_to_addr(struct sk_buff *skb,
					    struct net_device *dev)
{
	struct ipvl_addr *addr = NULL;
	struct ipvl_port *port;
	int addr_type;
	void *lyr3h;

	if (!dev || !netif_is_ipvlan_port(dev))
		goto out;

	port = ipvlan_port_get_rcu(dev);
	if (!port || port->mode != IPVLAN_MODE_L3S)
		goto out;

	lyr3h = ipvlan_get_L3_hdr(port, skb, &addr_type);
	if (!lyr3h)
		goto out;

	addr = ipvlan_addr_lookup(port, lyr3h, addr_type, true);
out:
	return addr;
}

static struct sk_buff *ipvlan_l3_rcv(struct net_device *dev,
				     struct sk_buff *skb, u16 proto)
{
	struct ipvl_addr *addr;
	struct net_device *sdev;

	addr = ipvlan_skb_to_addr(skb, dev);
	if (!addr)
		goto out;

	sdev = addr->master->dev;
	switch (proto) {
	case AF_INET:
	{
		struct iphdr *ip4h = ip_hdr(skb);
		int err;

		err = ip_route_input_noref(skb, ip4h->daddr, ip4h->saddr,
					   ip4h->tos, sdev);
		if (unlikely(err))
			goto out;
		break;
	}
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
	{
		struct dst_entry *dst;
		struct ipv6hdr *ip6h = ipv6_hdr(skb);
		int flags = RT6_LOOKUP_F_HAS_SADDR;
		struct flowi6 fl6 = {
			.flowi6_iif   = sdev->ifindex,
			.daddr        = ip6h->daddr,
			.saddr        = ip6h->saddr,
			.flowlabel    = ip6_flowinfo(ip6h),
			.flowi6_mark  = skb->mark,
			.flowi6_proto = ip6h->nexthdr,
		};

		skb_dst_drop(skb);
		dst = ip6_route_input_lookup(dev_net(sdev), sdev, &fl6,
					     skb, flags);
		skb_dst_set(skb, dst);
		break;
	}
#endif
	default:
		break;
	}
out:
	return skb;
}

static const struct l3mdev_ops ipvl_l3mdev_ops = {
	.l3mdev_l3_rcv = ipvlan_l3_rcv,
};

static unsigned int ipvlan_nf_input(void *priv, struct sk_buff *skb,
				    const struct nf_hook_state *state)
{
	struct ipvl_addr *addr;
	unsigned int len;

	addr = ipvlan_skb_to_addr(skb, skb->dev);
	if (!addr)
		goto out;

	skb->dev = addr->master->dev;
	skb->skb_iif = skb->dev->ifindex;
	len = skb->len + ETH_HLEN;
	ipvlan_count_rx(addr->master, len, true, false);
out:
	return NF_ACCEPT;
}

static const struct nf_hook_ops ipvl_nfops[] = {
	{
		.hook     = ipvlan_nf_input,
		.pf       = NFPROTO_IPV4,
		.hooknum  = NF_INET_LOCAL_IN,
		.priority = INT_MAX,
	},
#if IS_ENABLED(CONFIG_IPV6)
	{
		.hook     = ipvlan_nf_input,
		.pf       = NFPROTO_IPV6,
		.hooknum  = NF_INET_LOCAL_IN,
		.priority = INT_MAX,
	},
#endif
};

static int ipvlan_register_nf_hook(struct net *net)
{
	struct ipvlan_netns *vnet = net_generic(net, ipvlan_netid);
	int err = 0;

	if (!vnet->ipvl_nf_hook_refcnt) {
		err = nf_register_net_hooks(net, ipvl_nfops,
					    ARRAY_SIZE(ipvl_nfops));
		if (!err)
			vnet->ipvl_nf_hook_refcnt = 1;
	} else {
		vnet->ipvl_nf_hook_refcnt++;
	}

	return err;
}

static void ipvlan_unregister_nf_hook(struct net *net)
{
	struct ipvlan_netns *vnet = net_generic(net, ipvlan_netid);

	if (WARN_ON(!vnet->ipvl_nf_hook_refcnt))
		return;

	vnet->ipvl_nf_hook_refcnt--;
	if (!vnet->ipvl_nf_hook_refcnt)
		nf_unregister_net_hooks(net, ipvl_nfops,
					ARRAY_SIZE(ipvl_nfops));
}

void ipvlan_migrate_l3s_hook(struct net *oldnet, struct net *newnet)
{
	struct ipvlan_netns *old_vnet;

	ASSERT_RTNL();

	old_vnet = net_generic(oldnet, ipvlan_netid);
	if (!old_vnet->ipvl_nf_hook_refcnt)
		return;

	ipvlan_register_nf_hook(newnet);
	ipvlan_unregister_nf_hook(oldnet);
}

static void ipvlan_ns_exit(struct net *net)
{
	struct ipvlan_netns *vnet = net_generic(net, ipvlan_netid);

	if (WARN_ON_ONCE(vnet->ipvl_nf_hook_refcnt)) {
		vnet->ipvl_nf_hook_refcnt = 0;
		nf_unregister_net_hooks(net, ipvl_nfops,
					ARRAY_SIZE(ipvl_nfops));
	}
}

static struct pernet_operations ipvlan_net_ops = {
	.id   = &ipvlan_netid,
	.size = sizeof(struct ipvlan_netns),
	.exit = ipvlan_ns_exit,
};

int ipvlan_l3s_init(void)
{
	return register_pernet_subsys(&ipvlan_net_ops);
}

void ipvlan_l3s_cleanup(void)
{
	unregister_pernet_subsys(&ipvlan_net_ops);
}

int ipvlan_l3s_register(struct ipvl_port *port)
{
	struct net_device *dev = port->dev;
	int ret;

	ASSERT_RTNL();

	ret = ipvlan_register_nf_hook(read_pnet(&port->pnet));
	if (!ret) {
		dev->l3mdev_ops = &ipvl_l3mdev_ops;
		dev->priv_flags |= IFF_L3MDEV_RX_HANDLER;
	}

	return ret;
}

void ipvlan_l3s_unregister(struct ipvl_port *port)
{
	struct net_device *dev = port->dev;

	ASSERT_RTNL();

	dev->priv_flags &= ~IFF_L3MDEV_RX_HANDLER;
	ipvlan_unregister_nf_hook(read_pnet(&port->pnet));
	dev->l3mdev_ops = NULL;
}
