// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2014 Mahesh Bandewar <maheshb@google.com>
 */

#include <linux/ethtool.h>

#include "ipvlan.h"

static int ipvlan_set_port_mode(struct ipvl_port *port, u16 nval,
				struct netlink_ext_ack *extack)
{
	struct ipvl_dev *ipvlan;
	unsigned int flags;
	int err;

	ASSERT_RTNL();
	if (port->mode != nval) {
		list_for_each_entry(ipvlan, &port->ipvlans, pnode) {
			flags = ipvlan->dev->flags;
			if (nval == IPVLAN_MODE_L3 || nval == IPVLAN_MODE_L3S) {
				err = dev_change_flags(ipvlan->dev,
						       flags | IFF_NOARP,
						       extack);
			} else {
				err = dev_change_flags(ipvlan->dev,
						       flags & ~IFF_NOARP,
						       extack);
			}
			if (unlikely(err))
				goto fail;
		}
		if (nval == IPVLAN_MODE_L3S) {
			/* New mode is L3S */
			err = ipvlan_l3s_register(port);
			if (err)
				goto fail;
		} else if (port->mode == IPVLAN_MODE_L3S) {
			/* Old mode was L3S */
			ipvlan_l3s_unregister(port);
		}
		port->mode = nval;
	}
	return 0;

fail:
	/* Undo the flags changes that have been done so far. */
	list_for_each_entry_continue_reverse(ipvlan, &port->ipvlans, pnode) {
		flags = ipvlan->dev->flags;
		if (port->mode == IPVLAN_MODE_L3 ||
		    port->mode == IPVLAN_MODE_L3S)
			dev_change_flags(ipvlan->dev, flags | IFF_NOARP,
					 NULL);
		else
			dev_change_flags(ipvlan->dev, flags & ~IFF_NOARP,
					 NULL);
	}

	return err;
}

static int ipvlan_port_create(struct net_device *dev)
{
	struct ipvl_port *port;
	int err, idx;

	port = kzalloc(sizeof(struct ipvl_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	write_pnet(&port->pnet, dev_net(dev));
	port->dev = dev;
	port->mode = IPVLAN_MODE_L3;
	INIT_LIST_HEAD(&port->ipvlans);
	for (idx = 0; idx < IPVLAN_HASH_SIZE; idx++)
		INIT_HLIST_HEAD(&port->hlhead[idx]);

	skb_queue_head_init(&port->backlog);
	INIT_WORK(&port->wq, ipvlan_process_multicast);
	ida_init(&port->ida);
	port->dev_id_start = 1;

	err = netdev_rx_handler_register(dev, ipvlan_handle_frame, port);
	if (err)
		goto err;

	netdev_hold(dev, &port->dev_tracker, GFP_KERNEL);
	return 0;

err:
	kfree(port);
	return err;
}

static void ipvlan_port_destroy(struct net_device *dev)
{
	struct ipvl_port *port = ipvlan_port_get_rtnl(dev);
	struct sk_buff *skb;

	netdev_put(dev, &port->dev_tracker);
	if (port->mode == IPVLAN_MODE_L3S)
		ipvlan_l3s_unregister(port);
	netdev_rx_handler_unregister(dev);
	cancel_work_sync(&port->wq);
	while ((skb = __skb_dequeue(&port->backlog)) != NULL) {
		dev_put(skb->dev);
		kfree_skb(skb);
	}
	ida_destroy(&port->ida);
	kfree(port);
}

#define IPVLAN_ALWAYS_ON_OFLOADS \
	(NETIF_F_SG | NETIF_F_HW_CSUM | \
	 NETIF_F_GSO_ROBUST | NETIF_F_GSO_SOFTWARE | NETIF_F_GSO_ENCAP_ALL)

#define IPVLAN_ALWAYS_ON \
	(IPVLAN_ALWAYS_ON_OFLOADS | NETIF_F_LLTX | NETIF_F_VLAN_CHALLENGED)

#define IPVLAN_FEATURES \
	(NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_HIGHDMA | NETIF_F_FRAGLIST | \
	 NETIF_F_GSO | NETIF_F_ALL_TSO | NETIF_F_GSO_ROBUST | \
	 NETIF_F_GRO | NETIF_F_RXCSUM | \
	 NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_STAG_FILTER)

	/* NETIF_F_GSO_ENCAP_ALL NETIF_F_GSO_SOFTWARE Newly added */

#define IPVLAN_STATE_MASK \
	((1<<__LINK_STATE_NOCARRIER) | (1<<__LINK_STATE_DORMANT))

static int ipvlan_init(struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct net_device *phy_dev = ipvlan->phy_dev;
	struct ipvl_port *port;
	int err;

	dev->state = (dev->state & ~IPVLAN_STATE_MASK) |
		     (phy_dev->state & IPVLAN_STATE_MASK);
	dev->features = phy_dev->features & IPVLAN_FEATURES;
	dev->features |= IPVLAN_ALWAYS_ON;
	dev->vlan_features = phy_dev->vlan_features & IPVLAN_FEATURES;
	dev->vlan_features |= IPVLAN_ALWAYS_ON_OFLOADS;
	dev->hw_enc_features |= dev->features;
	netif_inherit_tso_max(dev, phy_dev);
	dev->hard_header_len = phy_dev->hard_header_len;

	netdev_lockdep_set_classes(dev);

	ipvlan->pcpu_stats = netdev_alloc_pcpu_stats(struct ipvl_pcpu_stats);
	if (!ipvlan->pcpu_stats)
		return -ENOMEM;

	if (!netif_is_ipvlan_port(phy_dev)) {
		err = ipvlan_port_create(phy_dev);
		if (err < 0) {
			free_percpu(ipvlan->pcpu_stats);
			return err;
		}
	}
	port = ipvlan_port_get_rtnl(phy_dev);
	port->count += 1;
	return 0;
}

static void ipvlan_uninit(struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct net_device *phy_dev = ipvlan->phy_dev;
	struct ipvl_port *port;

	free_percpu(ipvlan->pcpu_stats);

	port = ipvlan_port_get_rtnl(phy_dev);
	port->count -= 1;
	if (!port->count)
		ipvlan_port_destroy(port->dev);
}

static int ipvlan_open(struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ipvl_addr *addr;

	if (ipvlan->port->mode == IPVLAN_MODE_L3 ||
	    ipvlan->port->mode == IPVLAN_MODE_L3S)
		dev->flags |= IFF_NOARP;
	else
		dev->flags &= ~IFF_NOARP;

	rcu_read_lock();
	list_for_each_entry_rcu(addr, &ipvlan->addrs, anode)
		ipvlan_ht_addr_add(ipvlan, addr);
	rcu_read_unlock();

	return 0;
}

static int ipvlan_stop(struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct net_device *phy_dev = ipvlan->phy_dev;
	struct ipvl_addr *addr;

	dev_uc_unsync(phy_dev, dev);
	dev_mc_unsync(phy_dev, dev);

	rcu_read_lock();
	list_for_each_entry_rcu(addr, &ipvlan->addrs, anode)
		ipvlan_ht_addr_del(addr);
	rcu_read_unlock();

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
		u64_stats_inc(&pcptr->tx_pkts);
		u64_stats_add(&pcptr->tx_bytes, skblen);
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

	features |= NETIF_F_ALL_FOR_ALL;
	features &= (ipvlan->sfeatures | ~IPVLAN_FEATURES);
	features = netdev_increment_features(ipvlan->phy_dev->features,
					     features, features);
	features |= IPVLAN_ALWAYS_ON;
	features &= (IPVLAN_FEATURES | IPVLAN_ALWAYS_ON);

	return features;
}

static void ipvlan_change_rx_flags(struct net_device *dev, int change)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct net_device *phy_dev = ipvlan->phy_dev;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(phy_dev, dev->flags & IFF_ALLMULTI? 1 : -1);
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

		/* Turn-on broadcast bit irrespective of address family,
		 * since broadcast is deferred to a work-queue, hence no
		 * impact on fast-path processing.
		 */
		__set_bit(ipvlan_mac_hash(dev->broadcast), mc_filters);

		bitmap_copy(ipvlan->mac_filters, mc_filters,
			    IPVLAN_MAC_FILTER_SIZE);
	}
	dev_uc_sync(ipvlan->phy_dev, dev);
	dev_mc_sync(ipvlan->phy_dev, dev);
}

static void ipvlan_get_stats64(struct net_device *dev,
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
				rx_pkts = u64_stats_read(&pcptr->rx_pkts);
				rx_bytes = u64_stats_read(&pcptr->rx_bytes);
				rx_mcast = u64_stats_read(&pcptr->rx_mcast);
				tx_pkts = u64_stats_read(&pcptr->tx_pkts);
				tx_bytes = u64_stats_read(&pcptr->tx_bytes);
			} while (u64_stats_fetch_retry_irq(&pcptr->syncp,
							   strt));

			s->rx_packets += rx_pkts;
			s->rx_bytes += rx_bytes;
			s->multicast += rx_mcast;
			s->tx_packets += tx_pkts;
			s->tx_bytes += tx_bytes;

			/* u32 values are updated without syncp protection. */
			rx_errs += READ_ONCE(pcptr->rx_errs);
			tx_drps += READ_ONCE(pcptr->tx_drps);
		}
		s->rx_errors = rx_errs;
		s->rx_dropped = rx_errs;
		s->tx_dropped = tx_drps;
	}
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

static int ipvlan_get_iflink(const struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	return ipvlan->phy_dev->ifindex;
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
	.ndo_get_iflink		= ipvlan_get_iflink,
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
			       saddr ? : phy_dev->dev_addr, len);
}

static const struct header_ops ipvlan_header_ops = {
	.create  	= ipvlan_hard_header,
	.parse		= eth_header_parse,
	.cache		= eth_header_cache,
	.cache_update	= eth_header_cache_update,
};

static void ipvlan_adjust_mtu(struct ipvl_dev *ipvlan, struct net_device *dev)
{
	ipvlan->dev->mtu = dev->mtu;
}

static bool netif_is_ipvlan(const struct net_device *dev)
{
	/* both ipvlan and ipvtap devices use the same netdev_ops */
	return dev->netdev_ops == &ipvlan_netdev_ops;
}

static int ipvlan_ethtool_get_link_ksettings(struct net_device *dev,
					     struct ethtool_link_ksettings *cmd)
{
	const struct ipvl_dev *ipvlan = netdev_priv(dev);

	return __ethtool_get_link_ksettings(ipvlan->phy_dev, cmd);
}

static void ipvlan_ethtool_get_drvinfo(struct net_device *dev,
				       struct ethtool_drvinfo *drvinfo)
{
	strscpy(drvinfo->driver, IPVLAN_DRV, sizeof(drvinfo->driver));
	strscpy(drvinfo->version, IPV_DRV_VER, sizeof(drvinfo->version));
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
	.get_link_ksettings	= ipvlan_ethtool_get_link_ksettings,
	.get_drvinfo	= ipvlan_ethtool_get_drvinfo,
	.get_msglevel	= ipvlan_ethtool_get_msglevel,
	.set_msglevel	= ipvlan_ethtool_set_msglevel,
};

static int ipvlan_nl_changelink(struct net_device *dev,
				struct nlattr *tb[], struct nlattr *data[],
				struct netlink_ext_ack *extack)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ipvl_port *port = ipvlan_port_get_rtnl(ipvlan->phy_dev);
	int err = 0;

	if (!data)
		return 0;
	if (!ns_capable(dev_net(ipvlan->phy_dev)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	if (data[IFLA_IPVLAN_MODE]) {
		u16 nmode = nla_get_u16(data[IFLA_IPVLAN_MODE]);

		err = ipvlan_set_port_mode(port, nmode, extack);
	}

	if (!err && data[IFLA_IPVLAN_FLAGS]) {
		u16 flags = nla_get_u16(data[IFLA_IPVLAN_FLAGS]);

		if (flags & IPVLAN_F_PRIVATE)
			ipvlan_mark_private(port);
		else
			ipvlan_clear_private(port);

		if (flags & IPVLAN_F_VEPA)
			ipvlan_mark_vepa(port);
		else
			ipvlan_clear_vepa(port);
	}

	return err;
}

static size_t ipvlan_nl_getsize(const struct net_device *dev)
{
	return (0
		+ nla_total_size(2) /* IFLA_IPVLAN_MODE */
		+ nla_total_size(2) /* IFLA_IPVLAN_FLAGS */
		);
}

static int ipvlan_nl_validate(struct nlattr *tb[], struct nlattr *data[],
			      struct netlink_ext_ack *extack)
{
	if (!data)
		return 0;

	if (data[IFLA_IPVLAN_MODE]) {
		u16 mode = nla_get_u16(data[IFLA_IPVLAN_MODE]);

		if (mode >= IPVLAN_MODE_MAX)
			return -EINVAL;
	}
	if (data[IFLA_IPVLAN_FLAGS]) {
		u16 flags = nla_get_u16(data[IFLA_IPVLAN_FLAGS]);

		/* Only two bits are used at this moment. */
		if (flags & ~(IPVLAN_F_PRIVATE | IPVLAN_F_VEPA))
			return -EINVAL;
		/* Also both flags can't be active at the same time. */
		if ((flags & (IPVLAN_F_PRIVATE | IPVLAN_F_VEPA)) ==
		    (IPVLAN_F_PRIVATE | IPVLAN_F_VEPA))
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
	if (nla_put_u16(skb, IFLA_IPVLAN_FLAGS, port->flags))
		goto err;

	return 0;

err:
	return ret;
}

int ipvlan_link_new(struct net *src_net, struct net_device *dev,
		    struct nlattr *tb[], struct nlattr *data[],
		    struct netlink_ext_ack *extack)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ipvl_port *port;
	struct net_device *phy_dev;
	int err;
	u16 mode = IPVLAN_MODE_L3;

	if (!tb[IFLA_LINK])
		return -EINVAL;

	phy_dev = __dev_get_by_index(src_net, nla_get_u32(tb[IFLA_LINK]));
	if (!phy_dev)
		return -ENODEV;

	if (netif_is_ipvlan(phy_dev)) {
		struct ipvl_dev *tmp = netdev_priv(phy_dev);

		phy_dev = tmp->phy_dev;
		if (!ns_capable(dev_net(phy_dev)->user_ns, CAP_NET_ADMIN))
			return -EPERM;
	} else if (!netif_is_ipvlan_port(phy_dev)) {
		/* Exit early if the underlying link is invalid or busy */
		if (phy_dev->type != ARPHRD_ETHER ||
		    phy_dev->flags & IFF_LOOPBACK) {
			netdev_err(phy_dev,
				   "Master is either lo or non-ether device\n");
			return -EINVAL;
		}

		if (netdev_is_rx_handler_busy(phy_dev)) {
			netdev_err(phy_dev, "Device is already in use.\n");
			return -EBUSY;
		}
	}

	ipvlan->phy_dev = phy_dev;
	ipvlan->dev = dev;
	ipvlan->sfeatures = IPVLAN_FEATURES;
	if (!tb[IFLA_MTU])
		ipvlan_adjust_mtu(ipvlan, phy_dev);
	INIT_LIST_HEAD(&ipvlan->addrs);
	spin_lock_init(&ipvlan->addrs_lock);

	/* TODO Probably put random address here to be presented to the
	 * world but keep using the physical-dev address for the outgoing
	 * packets.
	 */
	eth_hw_addr_set(dev, phy_dev->dev_addr);

	dev->priv_flags |= IFF_NO_RX_HANDLER;

	err = register_netdevice(dev);
	if (err < 0)
		return err;

	/* ipvlan_init() would have created the port, if required */
	port = ipvlan_port_get_rtnl(phy_dev);
	ipvlan->port = port;

	/* If the port-id base is at the MAX value, then wrap it around and
	 * begin from 0x1 again. This may be due to a busy system where lots
	 * of slaves are getting created and deleted.
	 */
	if (port->dev_id_start == 0xFFFE)
		port->dev_id_start = 0x1;

	/* Since L2 address is shared among all IPvlan slaves including
	 * master, use unique 16 bit dev-ids to diffentiate among them.
	 * Assign IDs between 0x1 and 0xFFFE (used by the master) to each
	 * slave link [see addrconf_ifid_eui48()].
	 */
	err = ida_simple_get(&port->ida, port->dev_id_start, 0xFFFE,
			     GFP_KERNEL);
	if (err < 0)
		err = ida_simple_get(&port->ida, 0x1, port->dev_id_start,
				     GFP_KERNEL);
	if (err < 0)
		goto unregister_netdev;
	dev->dev_id = err;

	/* Increment id-base to the next slot for the future assignment */
	port->dev_id_start = err + 1;

	err = netdev_upper_dev_link(phy_dev, dev, extack);
	if (err)
		goto remove_ida;

	/* Flags are per port and latest update overrides. User has
	 * to be consistent in setting it just like the mode attribute.
	 */
	if (data && data[IFLA_IPVLAN_FLAGS])
		port->flags = nla_get_u16(data[IFLA_IPVLAN_FLAGS]);

	if (data && data[IFLA_IPVLAN_MODE])
		mode = nla_get_u16(data[IFLA_IPVLAN_MODE]);

	err = ipvlan_set_port_mode(port, mode, extack);
	if (err)
		goto unlink_netdev;

	list_add_tail_rcu(&ipvlan->pnode, &port->ipvlans);
	netif_stacked_transfer_operstate(phy_dev, dev);
	return 0;

unlink_netdev:
	netdev_upper_dev_unlink(phy_dev, dev);
remove_ida:
	ida_simple_remove(&port->ida, dev->dev_id);
unregister_netdev:
	unregister_netdevice(dev);
	return err;
}
EXPORT_SYMBOL_GPL(ipvlan_link_new);

void ipvlan_link_delete(struct net_device *dev, struct list_head *head)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ipvl_addr *addr, *next;

	spin_lock_bh(&ipvlan->addrs_lock);
	list_for_each_entry_safe(addr, next, &ipvlan->addrs, anode) {
		ipvlan_ht_addr_del(addr);
		list_del_rcu(&addr->anode);
		kfree_rcu(addr, rcu);
	}
	spin_unlock_bh(&ipvlan->addrs_lock);

	ida_simple_remove(&ipvlan->port->ida, dev->dev_id);
	list_del_rcu(&ipvlan->pnode);
	unregister_netdevice_queue(dev, head);
	netdev_upper_dev_unlink(ipvlan->phy_dev, dev);
}
EXPORT_SYMBOL_GPL(ipvlan_link_delete);

void ipvlan_link_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->max_mtu = ETH_MAX_MTU;
	dev->priv_flags &= ~(IFF_XMIT_DST_RELEASE | IFF_TX_SKB_SHARING);
	dev->priv_flags |= IFF_UNICAST_FLT | IFF_NO_QUEUE;
	dev->netdev_ops = &ipvlan_netdev_ops;
	dev->needs_free_netdev = true;
	dev->header_ops = &ipvlan_header_ops;
	dev->ethtool_ops = &ipvlan_ethtool_ops;
}
EXPORT_SYMBOL_GPL(ipvlan_link_setup);

static const struct nla_policy ipvlan_nl_policy[IFLA_IPVLAN_MAX + 1] =
{
	[IFLA_IPVLAN_MODE] = { .type = NLA_U16 },
	[IFLA_IPVLAN_FLAGS] = { .type = NLA_U16 },
};

static struct net *ipvlan_get_link_net(const struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	return dev_net(ipvlan->phy_dev);
}

static struct rtnl_link_ops ipvlan_link_ops = {
	.kind		= "ipvlan",
	.priv_size	= sizeof(struct ipvl_dev),

	.setup		= ipvlan_link_setup,
	.newlink	= ipvlan_link_new,
	.dellink	= ipvlan_link_delete,
	.get_link_net   = ipvlan_get_link_net,
};

int ipvlan_link_register(struct rtnl_link_ops *ops)
{
	ops->get_size	= ipvlan_nl_getsize;
	ops->policy	= ipvlan_nl_policy;
	ops->validate	= ipvlan_nl_validate;
	ops->fill_info	= ipvlan_nl_fillinfo;
	ops->changelink = ipvlan_nl_changelink;
	ops->maxtype	= IFLA_IPVLAN_MAX;
	return rtnl_link_register(ops);
}
EXPORT_SYMBOL_GPL(ipvlan_link_register);

static int ipvlan_device_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct netlink_ext_ack *extack = netdev_notifier_info_to_extack(ptr);
	struct netdev_notifier_pre_changeaddr_info *prechaddr_info;
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct ipvl_dev *ipvlan, *next;
	struct ipvl_port *port;
	LIST_HEAD(lst_kill);
	int err;

	if (!netif_is_ipvlan_port(dev))
		return NOTIFY_DONE;

	port = ipvlan_port_get_rtnl(dev);

	switch (event) {
	case NETDEV_UP:
	case NETDEV_CHANGE:
		list_for_each_entry(ipvlan, &port->ipvlans, pnode)
			netif_stacked_transfer_operstate(ipvlan->phy_dev,
							 ipvlan->dev);
		break;

	case NETDEV_REGISTER: {
		struct net *oldnet, *newnet = dev_net(dev);

		oldnet = read_pnet(&port->pnet);
		if (net_eq(newnet, oldnet))
			break;

		write_pnet(&port->pnet, newnet);

		if (port->mode == IPVLAN_MODE_L3S)
			ipvlan_migrate_l3s_hook(oldnet, newnet);
		break;
	}
	case NETDEV_UNREGISTER:
		if (dev->reg_state != NETREG_UNREGISTERING)
			break;

		list_for_each_entry_safe(ipvlan, next, &port->ipvlans, pnode)
			ipvlan->dev->rtnl_link_ops->dellink(ipvlan->dev,
							    &lst_kill);
		unregister_netdevice_many(&lst_kill);
		break;

	case NETDEV_FEAT_CHANGE:
		list_for_each_entry(ipvlan, &port->ipvlans, pnode) {
			netif_inherit_tso_max(ipvlan->dev, dev);
			netdev_update_features(ipvlan->dev);
		}
		break;

	case NETDEV_CHANGEMTU:
		list_for_each_entry(ipvlan, &port->ipvlans, pnode)
			ipvlan_adjust_mtu(ipvlan, dev);
		break;

	case NETDEV_PRE_CHANGEADDR:
		prechaddr_info = ptr;
		list_for_each_entry(ipvlan, &port->ipvlans, pnode) {
			err = dev_pre_changeaddr_notify(ipvlan->dev,
						    prechaddr_info->dev_addr,
						    extack);
			if (err)
				return notifier_from_errno(err);
		}
		break;

	case NETDEV_CHANGEADDR:
		list_for_each_entry(ipvlan, &port->ipvlans, pnode) {
			eth_hw_addr_set(ipvlan->dev, dev->dev_addr);
			call_netdevice_notifiers(NETDEV_CHANGEADDR, ipvlan->dev);
		}
		break;

	case NETDEV_PRE_TYPE_CHANGE:
		/* Forbid underlying device to change its type. */
		return NOTIFY_BAD;
	}
	return NOTIFY_DONE;
}

/* the caller must held the addrs lock */
static int ipvlan_add_addr(struct ipvl_dev *ipvlan, void *iaddr, bool is_v6)
{
	struct ipvl_addr *addr;

	addr = kzalloc(sizeof(struct ipvl_addr), GFP_ATOMIC);
	if (!addr)
		return -ENOMEM;

	addr->master = ipvlan;
	if (!is_v6) {
		memcpy(&addr->ip4addr, iaddr, sizeof(struct in_addr));
		addr->atype = IPVL_IPV4;
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		memcpy(&addr->ip6addr, iaddr, sizeof(struct in6_addr));
		addr->atype = IPVL_IPV6;
#endif
	}

	list_add_tail_rcu(&addr->anode, &ipvlan->addrs);

	/* If the interface is not up, the address will be added to the hash
	 * list by ipvlan_open.
	 */
	if (netif_running(ipvlan->dev))
		ipvlan_ht_addr_add(ipvlan, addr);

	return 0;
}

static void ipvlan_del_addr(struct ipvl_dev *ipvlan, void *iaddr, bool is_v6)
{
	struct ipvl_addr *addr;

	spin_lock_bh(&ipvlan->addrs_lock);
	addr = ipvlan_find_addr(ipvlan, iaddr, is_v6);
	if (!addr) {
		spin_unlock_bh(&ipvlan->addrs_lock);
		return;
	}

	ipvlan_ht_addr_del(addr);
	list_del_rcu(&addr->anode);
	spin_unlock_bh(&ipvlan->addrs_lock);
	kfree_rcu(addr, rcu);
}

static bool ipvlan_is_valid_dev(const struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	if (!netif_is_ipvlan(dev))
		return false;

	if (!ipvlan || !ipvlan->port)
		return false;

	return true;
}

#if IS_ENABLED(CONFIG_IPV6)
static int ipvlan_add_addr6(struct ipvl_dev *ipvlan, struct in6_addr *ip6_addr)
{
	int ret = -EINVAL;

	spin_lock_bh(&ipvlan->addrs_lock);
	if (ipvlan_addr_busy(ipvlan->port, ip6_addr, true))
		netif_err(ipvlan, ifup, ipvlan->dev,
			  "Failed to add IPv6=%pI6c addr for %s intf\n",
			  ip6_addr, ipvlan->dev->name);
	else
		ret = ipvlan_add_addr(ipvlan, ip6_addr, true);
	spin_unlock_bh(&ipvlan->addrs_lock);
	return ret;
}

static void ipvlan_del_addr6(struct ipvl_dev *ipvlan, struct in6_addr *ip6_addr)
{
	return ipvlan_del_addr(ipvlan, ip6_addr, true);
}

static int ipvlan_addr6_event(struct notifier_block *unused,
			      unsigned long event, void *ptr)
{
	struct inet6_ifaddr *if6 = (struct inet6_ifaddr *)ptr;
	struct net_device *dev = (struct net_device *)if6->idev->dev;
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	if (!ipvlan_is_valid_dev(dev))
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

static int ipvlan_addr6_validator_event(struct notifier_block *unused,
					unsigned long event, void *ptr)
{
	struct in6_validator_info *i6vi = (struct in6_validator_info *)ptr;
	struct net_device *dev = (struct net_device *)i6vi->i6vi_dev->dev;
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	if (!ipvlan_is_valid_dev(dev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		if (ipvlan_addr_busy(ipvlan->port, &i6vi->i6vi_addr, true)) {
			NL_SET_ERR_MSG(i6vi->extack,
				       "Address already assigned to an ipvlan device");
			return notifier_from_errno(-EADDRINUSE);
		}
		break;
	}

	return NOTIFY_OK;
}
#endif

static int ipvlan_add_addr4(struct ipvl_dev *ipvlan, struct in_addr *ip4_addr)
{
	int ret = -EINVAL;

	spin_lock_bh(&ipvlan->addrs_lock);
	if (ipvlan_addr_busy(ipvlan->port, ip4_addr, false))
		netif_err(ipvlan, ifup, ipvlan->dev,
			  "Failed to add IPv4=%pI4 on %s intf.\n",
			  ip4_addr, ipvlan->dev->name);
	else
		ret = ipvlan_add_addr(ipvlan, ip4_addr, false);
	spin_unlock_bh(&ipvlan->addrs_lock);
	return ret;
}

static void ipvlan_del_addr4(struct ipvl_dev *ipvlan, struct in_addr *ip4_addr)
{
	return ipvlan_del_addr(ipvlan, ip4_addr, false);
}

static int ipvlan_addr4_event(struct notifier_block *unused,
			      unsigned long event, void *ptr)
{
	struct in_ifaddr *if4 = (struct in_ifaddr *)ptr;
	struct net_device *dev = (struct net_device *)if4->ifa_dev->dev;
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct in_addr ip4_addr;

	if (!ipvlan_is_valid_dev(dev))
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

static int ipvlan_addr4_validator_event(struct notifier_block *unused,
					unsigned long event, void *ptr)
{
	struct in_validator_info *ivi = (struct in_validator_info *)ptr;
	struct net_device *dev = (struct net_device *)ivi->ivi_dev->dev;
	struct ipvl_dev *ipvlan = netdev_priv(dev);

	if (!ipvlan_is_valid_dev(dev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		if (ipvlan_addr_busy(ipvlan->port, &ivi->ivi_addr, false)) {
			NL_SET_ERR_MSG(ivi->extack,
				       "Address already assigned to an ipvlan device");
			return notifier_from_errno(-EADDRINUSE);
		}
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block ipvlan_addr4_notifier_block __read_mostly = {
	.notifier_call = ipvlan_addr4_event,
};

static struct notifier_block ipvlan_addr4_vtor_notifier_block __read_mostly = {
	.notifier_call = ipvlan_addr4_validator_event,
};

static struct notifier_block ipvlan_notifier_block __read_mostly = {
	.notifier_call = ipvlan_device_event,
};

#if IS_ENABLED(CONFIG_IPV6)
static struct notifier_block ipvlan_addr6_notifier_block __read_mostly = {
	.notifier_call = ipvlan_addr6_event,
};

static struct notifier_block ipvlan_addr6_vtor_notifier_block __read_mostly = {
	.notifier_call = ipvlan_addr6_validator_event,
};
#endif

static int __init ipvlan_init_module(void)
{
	int err;

	ipvlan_init_secret();
	register_netdevice_notifier(&ipvlan_notifier_block);
#if IS_ENABLED(CONFIG_IPV6)
	register_inet6addr_notifier(&ipvlan_addr6_notifier_block);
	register_inet6addr_validator_notifier(
	    &ipvlan_addr6_vtor_notifier_block);
#endif
	register_inetaddr_notifier(&ipvlan_addr4_notifier_block);
	register_inetaddr_validator_notifier(&ipvlan_addr4_vtor_notifier_block);

	err = ipvlan_l3s_init();
	if (err < 0)
		goto error;

	err = ipvlan_link_register(&ipvlan_link_ops);
	if (err < 0) {
		ipvlan_l3s_cleanup();
		goto error;
	}

	return 0;
error:
	unregister_inetaddr_notifier(&ipvlan_addr4_notifier_block);
	unregister_inetaddr_validator_notifier(
	    &ipvlan_addr4_vtor_notifier_block);
#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&ipvlan_addr6_notifier_block);
	unregister_inet6addr_validator_notifier(
	    &ipvlan_addr6_vtor_notifier_block);
#endif
	unregister_netdevice_notifier(&ipvlan_notifier_block);
	return err;
}

static void __exit ipvlan_cleanup_module(void)
{
	rtnl_link_unregister(&ipvlan_link_ops);
	ipvlan_l3s_cleanup();
	unregister_netdevice_notifier(&ipvlan_notifier_block);
	unregister_inetaddr_notifier(&ipvlan_addr4_notifier_block);
	unregister_inetaddr_validator_notifier(
	    &ipvlan_addr4_vtor_notifier_block);
#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&ipvlan_addr6_notifier_block);
	unregister_inet6addr_validator_notifier(
	    &ipvlan_addr6_vtor_notifier_block);
#endif
}

module_init(ipvlan_init_module);
module_exit(ipvlan_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mahesh Bandewar <maheshb@google.com>");
MODULE_DESCRIPTION("Driver for L3 (IPv6/IPv4) based VLANs");
MODULE_ALIAS_RTNL_LINK("ipvlan");
