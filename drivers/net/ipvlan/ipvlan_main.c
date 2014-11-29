/* Copyright (c) 2014 Mahesh Bandewar <maheshb@google.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include "ipvlan.h"

void ipvlan_adjust_mtu(struct ipvl_dev *ipvlan, struct net_device *dev)
{
	ipvlan->dev->mtu = dev->mtu - ipvlan->mtu_adj;
}

void ipvlan_set_port_mode(struct ipvl_port *port, u32 nval)
{
	struct ipvl_dev *ipvlan;

	if (port->mode != nval) {
		list_for_each_entry(ipvlan, &port->ipvlans, pnode) {
			if (nval == IPVLAN_MODE_L3)
				ipvlan->dev->flags |= IFF_NOARP;
			else
				ipvlan->dev->flags &= ~IFF_NOARP;
		}
		port->mode = nval;
	}
}

static int ipvlan_port_create(struct net_device *dev)
{
	struct ipvl_port *port;
	int err, idx;

	if (dev->type != ARPHRD_ETHER || dev->flags & IFF_LOOPBACK) {
		netdev_err(dev, "Master is either lo or non-ether device\n");
		return -EINVAL;
	}
	port = kzalloc(sizeof(struct ipvl_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->dev = dev;
	port->mode = IPVLAN_MODE_L3;
	INIT_LIST_HEAD(&port->ipvlans);
	for (idx = 0; idx < IPVLAN_HASH_SIZE; idx++)
		INIT_HLIST_HEAD(&port->hlhead[idx]);

	err = netdev_rx_handler_register(dev, ipvlan_handle_frame, port);
	if (err)
		goto err;

	dev->priv_flags |= IFF_IPVLAN_MASTER;
	return 0;

err:
	kfree_rcu(port, rcu);
	return err;
}

static void ipvlan_port_destroy(struct net_device *dev)
{
	struct ipvl_port *port = ipvlan_port_get_rtnl(dev);

	dev->priv_flags &= ~IFF_IPVLAN_MASTER;
	netdev_rx_handler_unregister(dev);
	kfree_rcu(port, rcu);
}

/* ipvlan network devices have devices nesting below it and are a special
 * "super class" of normal network devices; split their locks off into a
 * separate class since they always nest.
 */
static struct lock_class_key ipvlan_netdev_xmit_lock_key;
static struct lock_class_key ipvlan_netdev_addr_lock_key;

#define IPVLAN_FEATURES \
	(NETIF_F_SG | NETIF_F_ALL_CSUM | NETIF_F_HIGHDMA | NETIF_F_FRAGLIST | \
	 NETIF_F_GSO | NETIF_F_TSO | NETIF_F_UFO | NETIF_F_GSO_ROBUST | \
	 NETIF_F_TSO_ECN | NETIF_F_TSO6 | NETIF_F_GRO | NETIF_F_RXCSUM | \
	 NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_STAG_FILTER)

#define IPVLAN_STATE_MASK \
	((1<<__LINK_STATE_NOCARRIER) | (1<<__LINK_STATE_DORMANT))

static void ipvlan_set_lockdep_class_one(struct net_device *dev,
					 struct netdev_queue *txq,
					 void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock, &ipvlan_netdev_xmit_lock_key);
}

static void ipvlan_set_lockdep_class(struct net_device *dev)
{
	lockdep_set_class(&dev->addr_list_lock, &ipvlan_netdev_addr_lock_key);
	netdev_for_each_tx_queue(dev, ipvlan_set_lockdep_class_one, NULL);
}

static int ipvlan_init(struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	const struct net_device *phy_dev = ipvlan->phy_dev;

	dev->state = (dev->state & ~IPVLAN_STATE_MASK) |
		     (phy_dev->state & IPVLAN_STATE_MASK);
	dev->features = phy_dev->features & IPVLAN_FEATURES;
	dev->features |= NETIF_F_LLTX;
	dev->gso_max_size = phy_dev->gso_max_size;
	dev->iflink = phy_dev->ifindex;
	dev->hard_header_len = phy_dev->hard_header_len;

	ipvlan_set_lockdep_class(dev);

	ipvlan->pcpu_stats = alloc_percpu(struct ipvl_pcpu_stats);
	if (!ipvlan->pcpu_stats)
		return -ENOMEM;

	return 0;
}

static void ipvlan_uninit(struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ipvl_port *port = ipvlan->port;

	free_percpu(ipvlan->pcpu_stats);

	port->count -= 1;
	if (!port->count)
		ipvlan_port_destroy(port->dev);
}

static int ipvlan_open(struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct net_device *phy_dev = ipvlan->phy_dev;
	struct ipvl_addr *addr;

	if (ipvlan->port->mode == IPVLAN_MODE_L3)
		dev->flags |= IFF_NOARP;
	else
		dev->flags &= ~IFF_NOARP;

	if (ipvlan->ipv6cnt > 0 || ipvlan->ipv4cnt > 0) {
		list_for_each_entry(addr, &ipvlan->addrs, anode)
			ipvlan_ht_addr_add(ipvlan, addr);
	}
	return dev_uc_add(phy_dev, phy_dev->dev_addr);
}

static int ipvlan_stop(struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct net_device *phy_dev = ipvlan->phy_dev;
	struct ipvl_addr *addr;

	dev_uc_unsync(phy_dev, dev);
	dev_mc_unsync(phy_dev, dev);

	dev_uc_del(phy_dev, phy_dev->dev_addr);

	if (ipvlan->ipv6cnt > 0 || ipvlan->ipv4cnt > 0) {
		list_for_each_entry(addr, &ipvlan->addrs, anode)
			ipvlan_ht_addr_del(addr, !dev->dismantle);
	}
	return 0;
}

static netdev_tx_t ipvlan_start_xmit(struct sk_buff *skb,
				     struct net_device *dev)
{
	const struct ipvl_dev *ipvlan = netdev_priv(dev);
	int skblen = skb->len;
	int ret;

	ret = ipvlan_queue_xmit(skb, dev);
	if (likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN)) {
		struct ipvl_pcpu_stats *pcptr;

		pcptr = this_cpu_ptr(ipvlan->pcpu_stats);

		u64_stats_update_begin(&pcptr->syncp);
		pcptr->tx_pkts++;
		pcptr->tx_bytes += skblen;
		u64_stats_update_end(&pcptr->syncp);
	} else {
		this_cpu_inc(ipvlan->pcpu_stats->tx_drps);
	}
	return ret;
}

static netdev_features_t ipvlan_fix_features(struct net_device *dev,
					     netdev_features_t features)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	return features & (ipvlan->sfeatures | ~IPVLAN_FEATURES);
}

static void ipvlan_change_rx_flags(struct net_device *dev, int change)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct net_device *phy_dev = ipvlan->phy_dev;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(phy_dev, dev->flags & IFF_ALLMULTI? 1 : -1);
}

static void ipvlan_set_broadcast_mac_filter(struct ipvl_dev *ipvlan, bool set)
{
	struct net_device *dev = ipvlan->dev;
	unsigned int hashbit = ipvlan_mac_hash(dev->broadcast);

	if (set && !test_bit(hashbit, ipvlan->mac_filters))
		__set_bit(hashbit, ipvlan->mac_filters);
	else if (!set && test_bit(hashbit, ipvlan->mac_filters))
		__clear_bit(hashbit, ipvlan->mac_filters);
}

static void ipvlan_set_multicast_mac_filter(struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	if (dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		bitmap_fill(ipvlan->mac_filters, IPVLAN_MAC_FILTER_SIZE);
	} else {
		struct netdev_hw_addr *ha;
		DECLARE_BITMAP(mc_filters, IPVLAN_MAC_FILTER_SIZE);

		bitmap_zero(mc_filters, IPVLAN_MAC_FILTER_SIZE);
		netdev_for_each_mc_addr(ha, dev)
			__set_bit(ipvlan_mac_hash(ha->addr), mc_filters);

		bitmap_copy(ipvlan->mac_filters, mc_filters,
			    IPVLAN_MAC_FILTER_SIZE);
	}
	dev_uc_sync(ipvlan->phy_dev, dev);
	dev_mc_sync(ipvlan->phy_dev, dev);
}

static struct rtnl_link_stats64 *ipvlan_get_stats64(struct net_device *dev,
						    struct rtnl_link_stats64 *s)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	if (ipvlan->pcpu_stats) {
		struct ipvl_pcpu_stats *pcptr;
		u64 rx_pkts, rx_bytes, rx_mcast, tx_pkts, tx_bytes;
		u32 rx_errs = 0, tx_drps = 0;
		u32 strt;
		int idx;

		for_each_possible_cpu(idx) {
			pcptr = per_cpu_ptr(ipvlan->pcpu_stats, idx);
			do {
				strt= u64_stats_fetch_begin_irq(&pcptr->syncp);
				rx_pkts = pcptr->rx_pkts;
				rx_bytes = pcptr->rx_bytes;
				rx_mcast = pcptr->rx_mcast;
				tx_pkts = pcptr->tx_pkts;
				tx_bytes = pcptr->tx_bytes;
			} while (u64_stats_fetch_retry_irq(&pcptr->syncp,
							   strt));

			s->rx_packets += rx_pkts;
			s->rx_bytes += rx_bytes;
			s->multicast += rx_mcast;
			s->tx_packets += tx_pkts;
			s->tx_bytes += tx_bytes;

			/* u32 values are updated without syncp protection. */
			rx_errs += pcptr->rx_errs;
			tx_drps += pcptr->tx_drps;
		}
		s->rx_errors = rx_errs;
		s->rx_dropped = rx_errs;
		s->tx_dropped = tx_drps;
	}
	return s;
}

static int ipvlan_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct net_device *phy_dev = ipvlan->phy_dev;

	return vlan_vid_add(phy_dev, proto, vid);
}

static int ipvlan_vlan_rx_kill_vid(struct net_device *dev, __be16 proto,
				   u16 vid)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct net_device *phy_dev = ipvlan->phy_dev;

	vlan_vid_del(phy_dev, proto, vid);
	return 0;
}

static const struct net_device_ops ipvlan_netdev_ops = {
	.ndo_init		= ipvlan_init,
	.ndo_uninit		= ipvlan_uninit,
	.ndo_open		= ipvlan_open,
	.ndo_stop		= ipvlan_stop,
	.ndo_start_xmit		= ipvlan_start_xmit,
	.ndo_fix_features	= ipvlan_fix_features,
	.ndo_change_rx_flags	= ipvlan_change_rx_flags,
	.ndo_set_rx_mode	= ipvlan_set_multicast_mac_filter,
	.ndo_get_stats64	= ipvlan_get_stats64,
	.ndo_vlan_rx_add_vid	= ipvlan_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= ipvlan_vlan_rx_kill_vid,
};

static int ipvlan_hard_header(struct sk_buff *skb, struct net_device *dev,
			      unsigned short type, const void *daddr,
			      const void *saddr, unsigned len)
{
	const struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct net_device *phy_dev = ipvlan->phy_dev;

	/* TODO Probably use a different field than dev_addr so that the
	 * mac-address on the virtual device is portable and can be carried
	 * while the packets use the mac-addr on the physical device.
	 */
	return dev_hard_header(skb, phy_dev, type, daddr,
			       saddr ? : dev->dev_addr, len);
}

static const struct header_ops ipvlan_header_ops = {
	.create  	= ipvlan_hard_header,
	.rebuild	= eth_rebuild_header,
	.parse		= eth_header_parse,
	.cache		= eth_header_cache,
	.cache_update	= eth_header_cache_update,
};

static int ipvlan_ethtool_get_settings(struct net_device *dev,
				       struct ethtool_cmd *cmd)
{
	const struct ipvl_dev *ipvlan = netdev_priv(dev);

	return __ethtool_get_settings(ipvlan->phy_dev, cmd);
}

static void ipvlan_ethtool_get_drvinfo(struct net_device *dev,
				       struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, IPVLAN_DRV, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, IPV_DRV_VER, sizeof(drvinfo->version));
}

static u32 ipvlan_ethtool_get_msglevel(struct net_device *dev)
{
	const struct ipvl_dev *ipvlan = netdev_priv(dev);

	return ipvlan->msg_enable;
}

static void ipvlan_ethtool_set_msglevel(struct net_device *dev, u32 value)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	ipvlan->msg_enable = value;
}

static const struct ethtool_ops ipvlan_ethtool_ops = {
	.get_link	= ethtool_op_get_link,
	.get_settings	= ipvlan_ethtool_get_settings,
	.get_drvinfo	= ipvlan_ethtool_get_drvinfo,
	.get_msglevel	= ipvlan_ethtool_get_msglevel,
	.set_msglevel	= ipvlan_ethtool_set_msglevel,
};

static int ipvlan_nl_changelink(struct net_device *dev,
				struct nlattr *tb[], struct nlattr *data[])
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ipvl_port *port = ipvlan_port_get_rtnl(ipvlan->phy_dev);

	if (data && data[IFLA_IPVLAN_MODE]) {
		u16 nmode = nla_get_u16(data[IFLA_IPVLAN_MODE]);

		ipvlan_set_port_mode(port, nmode);
	}
	return 0;
}

static size_t ipvlan_nl_getsize(const struct net_device *dev)
{
	return (0
		+ nla_total_size(2) /* IFLA_IPVLAN_MODE */
		);
}

static int ipvlan_nl_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (data && data[IFLA_IPVLAN_MODE]) {
		u16 mode = nla_get_u16(data[IFLA_IPVLAN_MODE]);

		if (mode < IPVLAN_MODE_L2 || mode >= IPVLAN_MODE_MAX)
			return -EINVAL;
	}
	return 0;
}

static int ipvlan_nl_fillinfo(struct sk_buff *skb,
			      const struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ipvl_port *port = ipvlan_port_get_rtnl(ipvlan->phy_dev);
	int ret = -EINVAL;

	if (!port)
		goto err;

	ret = -EMSGSIZE;
	if (nla_put_u16(skb, IFLA_IPVLAN_MODE, port->mode))
		goto err;

	return 0;

err:
	return ret;
}

static int ipvlan_link_new(struct net *src_net, struct net_device *dev,
			   struct nlattr *tb[], struct nlattr *data[])
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ipvl_port *port;
	struct net_device *phy_dev;
	int err;

	if (!tb[IFLA_LINK])
		return -EINVAL;

	phy_dev = __dev_get_by_index(src_net, nla_get_u32(tb[IFLA_LINK]));
	if (!phy_dev)
		return -ENODEV;

	if (ipvlan_dev_slave(phy_dev)) {
		struct ipvl_dev *tmp = netdev_priv(phy_dev);

		phy_dev = tmp->phy_dev;
	} else if (!ipvlan_dev_master(phy_dev)) {
		err = ipvlan_port_create(phy_dev);
		if (err < 0)
			return err;
	}

	port = ipvlan_port_get_rtnl(phy_dev);
	if (data && data[IFLA_IPVLAN_MODE])
		port->mode = nla_get_u16(data[IFLA_IPVLAN_MODE]);

	ipvlan->phy_dev = phy_dev;
	ipvlan->dev = dev;
	ipvlan->port = port;
	ipvlan->sfeatures = IPVLAN_FEATURES;
	INIT_LIST_HEAD(&ipvlan->addrs);
	ipvlan->ipv4cnt = 0;
	ipvlan->ipv6cnt = 0;

	/* TODO Probably put random address here to be presented to the
	 * world but keep using the physical-dev address for the outgoing
	 * packets.
	 */
	memcpy(dev->dev_addr, phy_dev->dev_addr, ETH_ALEN);

	dev->priv_flags |= IFF_IPVLAN_SLAVE;

	port->count += 1;
	err = register_netdevice(dev);
	if (err < 0)
		goto ipvlan_destroy_port;

	err = netdev_upper_dev_link(phy_dev, dev);
	if (err)
		goto ipvlan_destroy_port;

	list_add_tail_rcu(&ipvlan->pnode, &port->ipvlans);
	netif_stacked_transfer_operstate(phy_dev, dev);
	return 0;

ipvlan_destroy_port:
	port->count -= 1;
	if (!port->count)
		ipvlan_port_destroy(phy_dev);

	return err;
}

static void ipvlan_link_delete(struct net_device *dev, struct list_head *head)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ipvl_addr *addr, *next;

	if (ipvlan->ipv6cnt > 0 || ipvlan->ipv4cnt > 0) {
		list_for_each_entry_safe(addr, next, &ipvlan->addrs, anode) {
			ipvlan_ht_addr_del(addr, !dev->dismantle);
			list_del_rcu(&addr->anode);
		}
	}
	list_del_rcu(&ipvlan->pnode);
	unregister_netdevice_queue(dev, head);
	netdev_upper_dev_unlink(ipvlan->phy_dev, dev);
}

static void ipvlan_link_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->priv_flags &= ~(IFF_XMIT_DST_RELEASE | IFF_TX_SKB_SHARING);
	dev->priv_flags |= IFF_UNICAST_FLT;
	dev->netdev_ops = &ipvlan_netdev_ops;
	dev->destructor = free_netdev;
	dev->header_ops = &ipvlan_header_ops;
	dev->ethtool_ops = &ipvlan_ethtool_ops;
	dev->tx_queue_len = 0;
}

static const struct nla_policy ipvlan_nl_policy[IFLA_IPVLAN_MAX + 1] =
{
	[IFLA_IPVLAN_MODE] = { .type = NLA_U16 },
};

static struct rtnl_link_ops ipvlan_link_ops = {
	.kind		= "ipvlan",
	.priv_size	= sizeof(struct ipvl_dev),

	.get_size	= ipvlan_nl_getsize,
	.policy		= ipvlan_nl_policy,
	.validate	= ipvlan_nl_validate,
	.fill_info	= ipvlan_nl_fillinfo,
	.changelink	= ipvlan_nl_changelink,
	.maxtype	= IFLA_IPVLAN_MAX,

	.setup		= ipvlan_link_setup,
	.newlink	= ipvlan_link_new,
	.dellink	= ipvlan_link_delete,
};

static int ipvlan_link_register(struct rtnl_link_ops *ops)
{
	return rtnl_link_register(ops);
}

static int ipvlan_device_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct ipvl_dev *ipvlan, *next;
	struct ipvl_port *port;
	LIST_HEAD(lst_kill);

	if (!ipvlan_dev_master(dev))
		return NOTIFY_DONE;

	port = ipvlan_port_get_rtnl(dev);

	switch (event) {
	case NETDEV_CHANGE:
		list_for_each_entry(ipvlan, &port->ipvlans, pnode)
			netif_stacked_transfer_operstate(ipvlan->phy_dev,
							 ipvlan->dev);
		break;

	case NETDEV_UNREGISTER:
		if (dev->reg_state != NETREG_UNREGISTERING)
			break;

		list_for_each_entry_safe(ipvlan, next, &port->ipvlans,
					 pnode)
			ipvlan->dev->rtnl_link_ops->dellink(ipvlan->dev,
							    &lst_kill);
		unregister_netdevice_many(&lst_kill);
		break;

	case NETDEV_FEAT_CHANGE:
		list_for_each_entry(ipvlan, &port->ipvlans, pnode) {
			ipvlan->dev->features = dev->features & IPVLAN_FEATURES;
			ipvlan->dev->gso_max_size = dev->gso_max_size;
			netdev_features_change(ipvlan->dev);
		}
		break;

	case NETDEV_CHANGEMTU:
		list_for_each_entry(ipvlan, &port->ipvlans, pnode)
			ipvlan_adjust_mtu(ipvlan, dev);
		break;

	case NETDEV_PRE_TYPE_CHANGE:
		/* Forbid underlying device to change its type. */
		return NOTIFY_BAD;
	}
	return NOTIFY_DONE;
}

static int ipvlan_add_addr6(struct ipvl_dev *ipvlan, struct in6_addr *ip6_addr)
{
	struct ipvl_addr *addr;

	if (ipvlan_addr_busy(ipvlan, ip6_addr, true)) {
		netif_err(ipvlan, ifup, ipvlan->dev,
			  "Failed to add IPv6=%pI6c addr for %s intf\n",
			  ip6_addr, ipvlan->dev->name);
		return -EINVAL;
	}
	addr = kzalloc(sizeof(struct ipvl_addr), GFP_ATOMIC);
	if (!addr)
		return -ENOMEM;

	addr->master = ipvlan;
	memcpy(&addr->ip6addr, ip6_addr, sizeof(struct in6_addr));
	addr->atype = IPVL_IPV6;
	list_add_tail_rcu(&addr->anode, &ipvlan->addrs);
	ipvlan->ipv6cnt++;
	ipvlan_ht_addr_add(ipvlan, addr);

	return 0;
}

static void ipvlan_del_addr6(struct ipvl_dev *ipvlan, struct in6_addr *ip6_addr)
{
	struct ipvl_addr *addr;

	addr = ipvlan_ht_addr_lookup(ipvlan->port, ip6_addr, true);
	if (!addr)
		return;

	ipvlan_ht_addr_del(addr, true);
	list_del_rcu(&addr->anode);
	ipvlan->ipv6cnt--;
	WARN_ON(ipvlan->ipv6cnt < 0);
	kfree_rcu(addr, rcu);

	return;
}

static int ipvlan_addr6_event(struct notifier_block *unused,
			      unsigned long event, void *ptr)
{
	struct inet6_ifaddr *if6 = (struct inet6_ifaddr *)ptr;
	struct net_device *dev = (struct net_device *)if6->idev->dev;
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	if (!ipvlan_dev_slave(dev))
		return NOTIFY_DONE;

	if (!ipvlan || !ipvlan->port)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		if (ipvlan_add_addr6(ipvlan, &if6->addr))
			return NOTIFY_BAD;
		break;

	case NETDEV_DOWN:
		ipvlan_del_addr6(ipvlan, &if6->addr);
		break;
	}

	return NOTIFY_OK;
}

static int ipvlan_add_addr4(struct ipvl_dev *ipvlan, struct in_addr *ip4_addr)
{
	struct ipvl_addr *addr;

	if (ipvlan_addr_busy(ipvlan, ip4_addr, false)) {
		netif_err(ipvlan, ifup, ipvlan->dev,
			  "Failed to add IPv4=%pI4 on %s intf.\n",
			  ip4_addr, ipvlan->dev->name);
		return -EINVAL;
	}
	addr = kzalloc(sizeof(struct ipvl_addr), GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	addr->master = ipvlan;
	memcpy(&addr->ip4addr, ip4_addr, sizeof(struct in_addr));
	addr->atype = IPVL_IPV4;
	list_add_tail_rcu(&addr->anode, &ipvlan->addrs);
	ipvlan->ipv4cnt++;
	ipvlan_ht_addr_add(ipvlan, addr);
	ipvlan_set_broadcast_mac_filter(ipvlan, true);

	return 0;
}

static void ipvlan_del_addr4(struct ipvl_dev *ipvlan, struct in_addr *ip4_addr)
{
	struct ipvl_addr *addr;

	addr = ipvlan_ht_addr_lookup(ipvlan->port, ip4_addr, false);
	if (!addr)
		return;

	ipvlan_ht_addr_del(addr, true);
	list_del_rcu(&addr->anode);
	ipvlan->ipv4cnt--;
	WARN_ON(ipvlan->ipv4cnt < 0);
	if (!ipvlan->ipv4cnt)
	    ipvlan_set_broadcast_mac_filter(ipvlan, false);
	kfree_rcu(addr, rcu);

	return;
}

static int ipvlan_addr4_event(struct notifier_block *unused,
			      unsigned long event, void *ptr)
{
	struct in_ifaddr *if4 = (struct in_ifaddr *)ptr;
	struct net_device *dev = (struct net_device *)if4->ifa_dev->dev;
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct in_addr ip4_addr;

	if (!ipvlan_dev_slave(dev))
		return NOTIFY_DONE;

	if (!ipvlan || !ipvlan->port)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		ip4_addr.s_addr = if4->ifa_address;
		if (ipvlan_add_addr4(ipvlan, &ip4_addr))
			return NOTIFY_BAD;
		break;

	case NETDEV_DOWN:
		ip4_addr.s_addr = if4->ifa_address;
		ipvlan_del_addr4(ipvlan, &ip4_addr);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block ipvlan_addr4_notifier_block __read_mostly = {
	.notifier_call = ipvlan_addr4_event,
};

static struct notifier_block ipvlan_notifier_block __read_mostly = {
	.notifier_call = ipvlan_device_event,
};

static struct notifier_block ipvlan_addr6_notifier_block __read_mostly = {
	.notifier_call = ipvlan_addr6_event,
};

static int __init ipvlan_init_module(void)
{
	int err;

	ipvlan_init_secret();
	register_netdevice_notifier(&ipvlan_notifier_block);
	register_inet6addr_notifier(&ipvlan_addr6_notifier_block);
	register_inetaddr_notifier(&ipvlan_addr4_notifier_block);

	err = ipvlan_link_register(&ipvlan_link_ops);
	if (err < 0)
		goto error;

	return 0;
error:
	unregister_inetaddr_notifier(&ipvlan_addr4_notifier_block);
	unregister_inet6addr_notifier(&ipvlan_addr6_notifier_block);
	unregister_netdevice_notifier(&ipvlan_notifier_block);
	return err;
}

static void __exit ipvlan_cleanup_module(void)
{
	rtnl_link_unregister(&ipvlan_link_ops);
	unregister_netdevice_notifier(&ipvlan_notifier_block);
	unregister_inetaddr_notifier(&ipvlan_addr4_notifier_block);
	unregister_inet6addr_notifier(&ipvlan_addr6_notifier_block);
}

module_init(ipvlan_init_module);
module_exit(ipvlan_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mahesh Bandewar <maheshb@google.com>");
MODULE_DESCRIPTION("Driver for L3 (IPv6/IPv4) based VLANs");
MODULE_ALIAS_RTNL_LINK("ipvlan");
