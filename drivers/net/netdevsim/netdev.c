/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree.
 *
 * THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS"
 * WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE
 * OF THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME
 * THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.
 */

#include <linux/debugfs.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <net/netlink.h>
#include <net/pkt_cls.h>
#include <net/rtnetlink.h>

#include "netdevsim.h"

static int nsim_get_port_parent_id(struct net_device *dev,
				   struct netdev_phys_item_id *ppid)
{
	struct netdevsim *ns = netdev_priv(dev);

	memcpy(ppid, &ns->nsim_dev->switch_id, sizeof(*ppid));
	return 0;
}

static int nsim_init(struct net_device *dev)
{
	struct netdevsim *ns = netdev_priv(dev);
	char dev_link_name[32];
	int err;

	ns->ddir = debugfs_create_dir("0", ns->nsim_dev->ports_ddir);
	if (IS_ERR_OR_NULL(ns->ddir))
		return -ENOMEM;

	sprintf(dev_link_name, "../../../" DRV_NAME "%u",
		ns->nsim_dev->nsim_bus_dev->dev.id);
	debugfs_create_symlink("dev", ns->ddir, dev_link_name);

	err = nsim_bpf_init(ns);
	if (err)
		goto err_debugfs_destroy;

	nsim_ipsec_init(ns);

	return 0;

err_debugfs_destroy:
	debugfs_remove_recursive(ns->ddir);
	return err;
}

static void nsim_uninit(struct net_device *dev)
{
	struct netdevsim *ns = netdev_priv(dev);

	nsim_ipsec_teardown(ns);
	debugfs_remove_recursive(ns->ddir);
	nsim_bpf_uninit(ns);
}

static void nsim_free(struct net_device *dev)
{
	struct netdevsim *ns = netdev_priv(dev);

	nsim_dev_destroy(ns->nsim_dev);
	nsim_bus_dev_del(ns->nsim_bus_dev);
	/* netdev and vf state will be freed out of device_release() */
}

static netdev_tx_t nsim_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct netdevsim *ns = netdev_priv(dev);

	if (!nsim_ipsec_tx(ns, skb))
		goto out;

	u64_stats_update_begin(&ns->syncp);
	ns->tx_packets++;
	ns->tx_bytes += skb->len;
	u64_stats_update_end(&ns->syncp);

out:
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static void nsim_set_rx_mode(struct net_device *dev)
{
}

static int nsim_change_mtu(struct net_device *dev, int new_mtu)
{
	struct netdevsim *ns = netdev_priv(dev);

	if (ns->xdp.prog && new_mtu > NSIM_XDP_MAX_MTU)
		return -EBUSY;

	dev->mtu = new_mtu;

	return 0;
}

static void
nsim_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct netdevsim *ns = netdev_priv(dev);
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(&ns->syncp);
		stats->tx_bytes = ns->tx_bytes;
		stats->tx_packets = ns->tx_packets;
	} while (u64_stats_fetch_retry(&ns->syncp, start));
}

static int
nsim_setup_tc_block_cb(enum tc_setup_type type, void *type_data, void *cb_priv)
{
	return nsim_bpf_setup_tc_block_cb(type, type_data, cb_priv);
}

static int
nsim_setup_tc_block(struct net_device *dev, struct tc_block_offload *f)
{
	struct netdevsim *ns = netdev_priv(dev);

	if (f->binder_type != TCF_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	switch (f->command) {
	case TC_BLOCK_BIND:
		return tcf_block_cb_register(f->block, nsim_setup_tc_block_cb,
					     ns, ns, f->extack);
	case TC_BLOCK_UNBIND:
		tcf_block_cb_unregister(f->block, nsim_setup_tc_block_cb, ns);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int nsim_set_vf_mac(struct net_device *dev, int vf, u8 *mac)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_bus_dev *nsim_bus_dev = ns->nsim_bus_dev;

	/* Only refuse multicast addresses, zero address can mean unset/any. */
	if (vf >= nsim_bus_dev->num_vfs || is_multicast_ether_addr(mac))
		return -EINVAL;
	memcpy(nsim_bus_dev->vfconfigs[vf].vf_mac, mac, ETH_ALEN);

	return 0;
}

static int nsim_set_vf_vlan(struct net_device *dev, int vf,
			    u16 vlan, u8 qos, __be16 vlan_proto)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_bus_dev *nsim_bus_dev = ns->nsim_bus_dev;

	if (vf >= nsim_bus_dev->num_vfs || vlan > 4095 || qos > 7)
		return -EINVAL;

	nsim_bus_dev->vfconfigs[vf].vlan = vlan;
	nsim_bus_dev->vfconfigs[vf].qos = qos;
	nsim_bus_dev->vfconfigs[vf].vlan_proto = vlan_proto;

	return 0;
}

static int nsim_set_vf_rate(struct net_device *dev, int vf, int min, int max)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_bus_dev *nsim_bus_dev = ns->nsim_bus_dev;

	if (vf >= nsim_bus_dev->num_vfs)
		return -EINVAL;

	nsim_bus_dev->vfconfigs[vf].min_tx_rate = min;
	nsim_bus_dev->vfconfigs[vf].max_tx_rate = max;

	return 0;
}

static int nsim_set_vf_spoofchk(struct net_device *dev, int vf, bool val)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_bus_dev *nsim_bus_dev = ns->nsim_bus_dev;

	if (vf >= nsim_bus_dev->num_vfs)
		return -EINVAL;
	nsim_bus_dev->vfconfigs[vf].spoofchk_enabled = val;

	return 0;
}

static int nsim_set_vf_rss_query_en(struct net_device *dev, int vf, bool val)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_bus_dev *nsim_bus_dev = ns->nsim_bus_dev;

	if (vf >= nsim_bus_dev->num_vfs)
		return -EINVAL;
	nsim_bus_dev->vfconfigs[vf].rss_query_enabled = val;

	return 0;
}

static int nsim_set_vf_trust(struct net_device *dev, int vf, bool val)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_bus_dev *nsim_bus_dev = ns->nsim_bus_dev;

	if (vf >= nsim_bus_dev->num_vfs)
		return -EINVAL;
	nsim_bus_dev->vfconfigs[vf].trusted = val;

	return 0;
}

static int
nsim_get_vf_config(struct net_device *dev, int vf, struct ifla_vf_info *ivi)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_bus_dev *nsim_bus_dev = ns->nsim_bus_dev;

	if (vf >= nsim_bus_dev->num_vfs)
		return -EINVAL;

	ivi->vf = vf;
	ivi->linkstate = nsim_bus_dev->vfconfigs[vf].link_state;
	ivi->min_tx_rate = nsim_bus_dev->vfconfigs[vf].min_tx_rate;
	ivi->max_tx_rate = nsim_bus_dev->vfconfigs[vf].max_tx_rate;
	ivi->vlan = nsim_bus_dev->vfconfigs[vf].vlan;
	ivi->vlan_proto = nsim_bus_dev->vfconfigs[vf].vlan_proto;
	ivi->qos = nsim_bus_dev->vfconfigs[vf].qos;
	memcpy(&ivi->mac, nsim_bus_dev->vfconfigs[vf].vf_mac, ETH_ALEN);
	ivi->spoofchk = nsim_bus_dev->vfconfigs[vf].spoofchk_enabled;
	ivi->trusted = nsim_bus_dev->vfconfigs[vf].trusted;
	ivi->rss_query_en = nsim_bus_dev->vfconfigs[vf].rss_query_enabled;

	return 0;
}

static int nsim_set_vf_link_state(struct net_device *dev, int vf, int state)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_bus_dev *nsim_bus_dev = ns->nsim_bus_dev;

	if (vf >= nsim_bus_dev->num_vfs)
		return -EINVAL;

	switch (state) {
	case IFLA_VF_LINK_STATE_AUTO:
	case IFLA_VF_LINK_STATE_ENABLE:
	case IFLA_VF_LINK_STATE_DISABLE:
		break;
	default:
		return -EINVAL;
	}

	nsim_bus_dev->vfconfigs[vf].link_state = state;

	return 0;
}

static int
nsim_setup_tc(struct net_device *dev, enum tc_setup_type type, void *type_data)
{
	switch (type) {
	case TC_SETUP_BLOCK:
		return nsim_setup_tc_block(dev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static int
nsim_set_features(struct net_device *dev, netdev_features_t features)
{
	struct netdevsim *ns = netdev_priv(dev);

	if ((dev->features & NETIF_F_HW_TC) > (features & NETIF_F_HW_TC))
		return nsim_bpf_disable_tc(ns);

	return 0;
}

static const struct net_device_ops nsim_netdev_ops = {
	.ndo_init		= nsim_init,
	.ndo_uninit		= nsim_uninit,
	.ndo_start_xmit		= nsim_start_xmit,
	.ndo_set_rx_mode	= nsim_set_rx_mode,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= nsim_change_mtu,
	.ndo_get_stats64	= nsim_get_stats64,
	.ndo_set_vf_mac		= nsim_set_vf_mac,
	.ndo_set_vf_vlan	= nsim_set_vf_vlan,
	.ndo_set_vf_rate	= nsim_set_vf_rate,
	.ndo_set_vf_spoofchk	= nsim_set_vf_spoofchk,
	.ndo_set_vf_trust	= nsim_set_vf_trust,
	.ndo_get_vf_config	= nsim_get_vf_config,
	.ndo_set_vf_link_state	= nsim_set_vf_link_state,
	.ndo_set_vf_rss_query_en = nsim_set_vf_rss_query_en,
	.ndo_setup_tc		= nsim_setup_tc,
	.ndo_set_features	= nsim_set_features,
	.ndo_bpf		= nsim_bpf,
	.ndo_get_port_parent_id	= nsim_get_port_parent_id,
};

static void nsim_setup(struct net_device *dev)
{
	ether_setup(dev);
	eth_hw_addr_random(dev);

	dev->netdev_ops = &nsim_netdev_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = nsim_free;

	dev->tx_queue_len = 0;
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE |
			   IFF_NO_QUEUE;
	dev->features |= NETIF_F_HIGHDMA |
			 NETIF_F_SG |
			 NETIF_F_FRAGLIST |
			 NETIF_F_HW_CSUM |
			 NETIF_F_TSO;
	dev->hw_features |= NETIF_F_HW_TC;
	dev->max_mtu = ETH_MAX_MTU;
}

static int nsim_validate(struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}
	return 0;
}

static int nsim_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	struct netdevsim *ns = netdev_priv(dev);
	int err;

	ns->nsim_bus_dev = nsim_bus_dev_new(~0, 0);
	if (IS_ERR(ns->nsim_bus_dev))
		return PTR_ERR(ns->nsim_bus_dev);

	SET_NETDEV_DEV(dev, &ns->nsim_bus_dev->dev);
	ns->netdev = dev;

	ns->nsim_dev = nsim_dev_create_with_ns(ns->nsim_bus_dev, ns);
	if (IS_ERR(ns->nsim_dev)) {
		err = PTR_ERR(ns->nsim_dev);
		goto err_dev_del;
	}

	err = register_netdevice(dev);
	if (err)
		goto err_dev_destroy;
	return 0;

err_dev_destroy:
	nsim_dev_destroy(ns->nsim_dev);
err_dev_del:
	nsim_bus_dev_del(ns->nsim_bus_dev);
	return err;
}

static struct rtnl_link_ops nsim_link_ops __read_mostly = {
	.kind		= DRV_NAME,
	.priv_size	= sizeof(struct netdevsim),
	.setup		= nsim_setup,
	.validate	= nsim_validate,
	.newlink	= nsim_newlink,
};

static int __init nsim_module_init(void)
{
	int err;

	err = nsim_dev_init();
	if (err)
		return err;

	err = nsim_bus_init();
	if (err)
		goto err_dev_exit;

	err = rtnl_link_register(&nsim_link_ops);
	if (err)
		goto err_bus_exit;

	return 0;

err_bus_exit:
	nsim_bus_exit();
err_dev_exit:
	nsim_dev_exit();
	return err;
}

static void __exit nsim_module_exit(void)
{
	rtnl_link_unregister(&nsim_link_ops);
	nsim_bus_exit();
	nsim_dev_exit();
}

module_init(nsim_module_init);
module_exit(nsim_module_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
