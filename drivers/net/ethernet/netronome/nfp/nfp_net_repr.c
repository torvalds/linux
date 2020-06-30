// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/etherdevice.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/lockdep.h>
#include <net/dst_metadata.h>

#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nsp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net.h"
#include "nfp_net_ctrl.h"
#include "nfp_net_repr.h"
#include "nfp_net_sriov.h"
#include "nfp_port.h"

struct net_device *
nfp_repr_get_locked(struct nfp_app *app, struct nfp_reprs *set, unsigned int id)
{
	return rcu_dereference_protected(set->reprs[id],
					 lockdep_is_held(&app->pf->lock));
}

static void
nfp_repr_inc_tx_stats(struct net_device *netdev, unsigned int len,
		      int tx_status)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	struct nfp_repr_pcpu_stats *stats;

	if (unlikely(tx_status != NET_XMIT_SUCCESS &&
		     tx_status != NET_XMIT_CN)) {
		this_cpu_inc(repr->stats->tx_drops);
		return;
	}

	stats = this_cpu_ptr(repr->stats);
	u64_stats_update_begin(&stats->syncp);
	stats->tx_packets++;
	stats->tx_bytes += len;
	u64_stats_update_end(&stats->syncp);
}

void nfp_repr_inc_rx_stats(struct net_device *netdev, unsigned int len)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	struct nfp_repr_pcpu_stats *stats;

	stats = this_cpu_ptr(repr->stats);
	u64_stats_update_begin(&stats->syncp);
	stats->rx_packets++;
	stats->rx_bytes += len;
	u64_stats_update_end(&stats->syncp);
}

static void
nfp_repr_phy_port_get_stats64(struct nfp_port *port,
			      struct rtnl_link_stats64 *stats)
{
	u8 __iomem *mem = port->eth_stats;

	stats->tx_packets = readq(mem + NFP_MAC_STATS_TX_FRAMES_TRANSMITTED_OK);
	stats->tx_bytes = readq(mem + NFP_MAC_STATS_TX_OUT_OCTETS);
	stats->tx_dropped = readq(mem + NFP_MAC_STATS_TX_OUT_ERRORS);

	stats->rx_packets = readq(mem + NFP_MAC_STATS_RX_FRAMES_RECEIVED_OK);
	stats->rx_bytes = readq(mem + NFP_MAC_STATS_RX_IN_OCTETS);
	stats->rx_dropped = readq(mem + NFP_MAC_STATS_RX_IN_ERRORS);
}

static void
nfp_repr_vnic_get_stats64(struct nfp_port *port,
			  struct rtnl_link_stats64 *stats)
{
	/* TX and RX stats are flipped as we are returning the stats as seen
	 * at the switch port corresponding to the VF.
	 */
	stats->tx_packets = readq(port->vnic + NFP_NET_CFG_STATS_RX_FRAMES);
	stats->tx_bytes = readq(port->vnic + NFP_NET_CFG_STATS_RX_OCTETS);
	stats->tx_dropped = readq(port->vnic + NFP_NET_CFG_STATS_RX_DISCARDS);

	stats->rx_packets = readq(port->vnic + NFP_NET_CFG_STATS_TX_FRAMES);
	stats->rx_bytes = readq(port->vnic + NFP_NET_CFG_STATS_TX_OCTETS);
	stats->rx_dropped = readq(port->vnic + NFP_NET_CFG_STATS_TX_DISCARDS);
}

static void
nfp_repr_get_stats64(struct net_device *netdev, struct rtnl_link_stats64 *stats)
{
	struct nfp_repr *repr = netdev_priv(netdev);

	if (WARN_ON(!repr->port))
		return;

	switch (repr->port->type) {
	case NFP_PORT_PHYS_PORT:
		if (!__nfp_port_get_eth_port(repr->port))
			break;
		nfp_repr_phy_port_get_stats64(repr->port, stats);
		break;
	case NFP_PORT_PF_PORT:
	case NFP_PORT_VF_PORT:
		nfp_repr_vnic_get_stats64(repr->port, stats);
	default:
		break;
	}
}

static bool
nfp_repr_has_offload_stats(const struct net_device *dev, int attr_id)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		return true;
	}

	return false;
}

static int
nfp_repr_get_host_stats64(const struct net_device *netdev,
			  struct rtnl_link_stats64 *stats)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	int i;

	for_each_possible_cpu(i) {
		u64 tbytes, tpkts, tdrops, rbytes, rpkts;
		struct nfp_repr_pcpu_stats *repr_stats;
		unsigned int start;

		repr_stats = per_cpu_ptr(repr->stats, i);
		do {
			start = u64_stats_fetch_begin_irq(&repr_stats->syncp);
			tbytes = repr_stats->tx_bytes;
			tpkts = repr_stats->tx_packets;
			tdrops = repr_stats->tx_drops;
			rbytes = repr_stats->rx_bytes;
			rpkts = repr_stats->rx_packets;
		} while (u64_stats_fetch_retry_irq(&repr_stats->syncp, start));

		stats->tx_bytes += tbytes;
		stats->tx_packets += tpkts;
		stats->tx_dropped += tdrops;
		stats->rx_bytes += rbytes;
		stats->rx_packets += rpkts;
	}

	return 0;
}

static int
nfp_repr_get_offload_stats(int attr_id, const struct net_device *dev,
			   void *stats)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		return nfp_repr_get_host_stats64(dev, stats);
	}

	return -EINVAL;
}

static int nfp_repr_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	int err;

	err = nfp_app_check_mtu(repr->app, netdev, new_mtu);
	if (err)
		return err;

	err = nfp_app_repr_change_mtu(repr->app, netdev, new_mtu);
	if (err)
		return err;

	netdev->mtu = new_mtu;

	return 0;
}

static netdev_tx_t nfp_repr_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	unsigned int len = skb->len;
	int ret;

	skb_dst_drop(skb);
	dst_hold((struct dst_entry *)repr->dst);
	skb_dst_set(skb, (struct dst_entry *)repr->dst);
	skb->dev = repr->dst->u.port_info.lower_dev;

	ret = dev_queue_xmit(skb);
	nfp_repr_inc_tx_stats(netdev, len, ret);

	return NETDEV_TX_OK;
}

static int nfp_repr_stop(struct net_device *netdev)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	int err;

	err = nfp_app_repr_stop(repr->app, repr);
	if (err)
		return err;

	nfp_port_configure(netdev, false);
	return 0;
}

static int nfp_repr_open(struct net_device *netdev)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	int err;

	err = nfp_port_configure(netdev, true);
	if (err)
		return err;

	err = nfp_app_repr_open(repr->app, repr);
	if (err)
		goto err_port_disable;

	return 0;

err_port_disable:
	nfp_port_configure(netdev, false);
	return err;
}

static netdev_features_t
nfp_repr_fix_features(struct net_device *netdev, netdev_features_t features)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	netdev_features_t old_features = features;
	netdev_features_t lower_features;
	struct net_device *lower_dev;

	lower_dev = repr->dst->u.port_info.lower_dev;

	lower_features = lower_dev->features;
	if (lower_features & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM))
		lower_features |= NETIF_F_HW_CSUM;

	features = netdev_intersect_features(features, lower_features);
	features |= old_features & (NETIF_F_SOFT_FEATURES | NETIF_F_HW_TC);
	features |= NETIF_F_LLTX;

	return features;
}

const struct net_device_ops nfp_repr_netdev_ops = {
	.ndo_init		= nfp_app_ndo_init,
	.ndo_uninit		= nfp_app_ndo_uninit,
	.ndo_open		= nfp_repr_open,
	.ndo_stop		= nfp_repr_stop,
	.ndo_start_xmit		= nfp_repr_xmit,
	.ndo_change_mtu		= nfp_repr_change_mtu,
	.ndo_get_stats64	= nfp_repr_get_stats64,
	.ndo_has_offload_stats	= nfp_repr_has_offload_stats,
	.ndo_get_offload_stats	= nfp_repr_get_offload_stats,
	.ndo_get_phys_port_name	= nfp_port_get_phys_port_name,
	.ndo_setup_tc		= nfp_port_setup_tc,
	.ndo_set_vf_mac		= nfp_app_set_vf_mac,
	.ndo_set_vf_vlan	= nfp_app_set_vf_vlan,
	.ndo_set_vf_spoofchk	= nfp_app_set_vf_spoofchk,
	.ndo_set_vf_trust	= nfp_app_set_vf_trust,
	.ndo_get_vf_config	= nfp_app_get_vf_config,
	.ndo_set_vf_link_state	= nfp_app_set_vf_link_state,
	.ndo_fix_features	= nfp_repr_fix_features,
	.ndo_set_features	= nfp_port_set_features,
	.ndo_set_mac_address    = eth_mac_addr,
	.ndo_get_port_parent_id	= nfp_port_get_port_parent_id,
	.ndo_get_devlink_port	= nfp_devlink_get_devlink_port,
};

void
nfp_repr_transfer_features(struct net_device *netdev, struct net_device *lower)
{
	struct nfp_repr *repr = netdev_priv(netdev);

	if (repr->dst->u.port_info.lower_dev != lower)
		return;

	netdev->gso_max_size = lower->gso_max_size;
	netdev->gso_max_segs = lower->gso_max_segs;

	netdev_update_features(netdev);
}

static void nfp_repr_clean(struct nfp_repr *repr)
{
	unregister_netdev(repr->netdev);
	nfp_app_repr_clean(repr->app, repr->netdev);
	dst_release((struct dst_entry *)repr->dst);
	nfp_port_free(repr->port);
}

static struct lock_class_key nfp_repr_netdev_xmit_lock_key;

static void nfp_repr_set_lockdep_class_one(struct net_device *dev,
					   struct netdev_queue *txq,
					   void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock, &nfp_repr_netdev_xmit_lock_key);
}

static void nfp_repr_set_lockdep_class(struct net_device *dev)
{
	netdev_for_each_tx_queue(dev, nfp_repr_set_lockdep_class_one, NULL);
}

int nfp_repr_init(struct nfp_app *app, struct net_device *netdev,
		  u32 cmsg_port_id, struct nfp_port *port,
		  struct net_device *pf_netdev)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	struct nfp_net *nn = netdev_priv(pf_netdev);
	u32 repr_cap = nn->tlv_caps.repr_cap;
	int err;

	nfp_repr_set_lockdep_class(netdev);

	repr->port = port;
	repr->dst = metadata_dst_alloc(0, METADATA_HW_PORT_MUX, GFP_KERNEL);
	if (!repr->dst)
		return -ENOMEM;
	repr->dst->u.port_info.port_id = cmsg_port_id;
	repr->dst->u.port_info.lower_dev = pf_netdev;

	netdev->netdev_ops = &nfp_repr_netdev_ops;
	netdev->ethtool_ops = &nfp_port_ethtool_ops;

	netdev->max_mtu = pf_netdev->max_mtu;

	/* Set features the lower device can support with representors */
	if (repr_cap & NFP_NET_CFG_CTRL_LIVE_ADDR)
		netdev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

	netdev->hw_features = NETIF_F_HIGHDMA;
	if (repr_cap & NFP_NET_CFG_CTRL_RXCSUM_ANY)
		netdev->hw_features |= NETIF_F_RXCSUM;
	if (repr_cap & NFP_NET_CFG_CTRL_TXCSUM)
		netdev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	if (repr_cap & NFP_NET_CFG_CTRL_GATHER)
		netdev->hw_features |= NETIF_F_SG;
	if ((repr_cap & NFP_NET_CFG_CTRL_LSO && nn->fw_ver.major > 2) ||
	    repr_cap & NFP_NET_CFG_CTRL_LSO2)
		netdev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
	if (repr_cap & NFP_NET_CFG_CTRL_RSS_ANY)
		netdev->hw_features |= NETIF_F_RXHASH;
	if (repr_cap & NFP_NET_CFG_CTRL_VXLAN) {
		if (repr_cap & NFP_NET_CFG_CTRL_LSO)
			netdev->hw_features |= NETIF_F_GSO_UDP_TUNNEL;
	}
	if (repr_cap & NFP_NET_CFG_CTRL_NVGRE) {
		if (repr_cap & NFP_NET_CFG_CTRL_LSO)
			netdev->hw_features |= NETIF_F_GSO_GRE;
	}
	if (repr_cap & (NFP_NET_CFG_CTRL_VXLAN | NFP_NET_CFG_CTRL_NVGRE))
		netdev->hw_enc_features = netdev->hw_features;

	netdev->vlan_features = netdev->hw_features;

	if (repr_cap & NFP_NET_CFG_CTRL_RXVLAN)
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_RX;
	if (repr_cap & NFP_NET_CFG_CTRL_TXVLAN) {
		if (repr_cap & NFP_NET_CFG_CTRL_LSO2)
			netdev_warn(netdev, "Device advertises both TSO2 and TXVLAN. Refusing to enable TXVLAN.\n");
		else
			netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_TX;
	}
	if (repr_cap & NFP_NET_CFG_CTRL_CTAG_FILTER)
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	netdev->features = netdev->hw_features;

	/* Advertise but disable TSO by default. */
	netdev->features &= ~(NETIF_F_TSO | NETIF_F_TSO6);
	netdev->gso_max_segs = NFP_NET_LSO_MAX_SEGS;

	netdev->priv_flags |= IFF_NO_QUEUE | IFF_DISABLE_NETPOLL;
	netdev->features |= NETIF_F_LLTX;

	if (nfp_app_has_tc(app)) {
		netdev->features |= NETIF_F_HW_TC;
		netdev->hw_features |= NETIF_F_HW_TC;
	}

	err = nfp_app_repr_init(app, netdev);
	if (err)
		goto err_clean;

	err = register_netdev(netdev);
	if (err)
		goto err_repr_clean;

	return 0;

err_repr_clean:
	nfp_app_repr_clean(app, netdev);
err_clean:
	dst_release((struct dst_entry *)repr->dst);
	return err;
}

static void __nfp_repr_free(struct nfp_repr *repr)
{
	free_percpu(repr->stats);
	free_netdev(repr->netdev);
}

void nfp_repr_free(struct net_device *netdev)
{
	__nfp_repr_free(netdev_priv(netdev));
}

struct net_device *
nfp_repr_alloc_mqs(struct nfp_app *app, unsigned int txqs, unsigned int rxqs)
{
	struct net_device *netdev;
	struct nfp_repr *repr;

	netdev = alloc_etherdev_mqs(sizeof(*repr), txqs, rxqs);
	if (!netdev)
		return NULL;

	netif_carrier_off(netdev);

	repr = netdev_priv(netdev);
	repr->netdev = netdev;
	repr->app = app;

	repr->stats = netdev_alloc_pcpu_stats(struct nfp_repr_pcpu_stats);
	if (!repr->stats)
		goto err_free_netdev;

	return netdev;

err_free_netdev:
	free_netdev(netdev);
	return NULL;
}

void nfp_repr_clean_and_free(struct nfp_repr *repr)
{
	nfp_info(repr->app->cpp, "Destroying Representor(%s)\n",
		 repr->netdev->name);
	nfp_repr_clean(repr);
	__nfp_repr_free(repr);
}

void nfp_reprs_clean_and_free(struct nfp_app *app, struct nfp_reprs *reprs)
{
	struct net_device *netdev;
	unsigned int i;

	for (i = 0; i < reprs->num_reprs; i++) {
		netdev = nfp_repr_get_locked(app, reprs, i);
		if (netdev)
			nfp_repr_clean_and_free(netdev_priv(netdev));
	}

	kfree(reprs);
}

void
nfp_reprs_clean_and_free_by_type(struct nfp_app *app, enum nfp_repr_type type)
{
	struct net_device *netdev;
	struct nfp_reprs *reprs;
	int i;

	reprs = rcu_dereference_protected(app->reprs[type],
					  lockdep_is_held(&app->pf->lock));
	if (!reprs)
		return;

	/* Preclean must happen before we remove the reprs reference from the
	 * app below.
	 */
	for (i = 0; i < reprs->num_reprs; i++) {
		netdev = nfp_repr_get_locked(app, reprs, i);
		if (netdev)
			nfp_app_repr_preclean(app, netdev);
	}

	reprs = nfp_app_reprs_set(app, type, NULL);

	synchronize_rcu();
	nfp_reprs_clean_and_free(app, reprs);
}

struct nfp_reprs *nfp_reprs_alloc(unsigned int num_reprs)
{
	struct nfp_reprs *reprs;

	reprs = kzalloc(sizeof(*reprs) +
			num_reprs * sizeof(struct net_device *), GFP_KERNEL);
	if (!reprs)
		return NULL;
	reprs->num_reprs = num_reprs;

	return reprs;
}

int nfp_reprs_resync_phys_ports(struct nfp_app *app)
{
	struct net_device *netdev;
	struct nfp_reprs *reprs;
	struct nfp_repr *repr;
	int i;

	reprs = nfp_reprs_get_locked(app, NFP_REPR_TYPE_PHYS_PORT);
	if (!reprs)
		return 0;

	for (i = 0; i < reprs->num_reprs; i++) {
		netdev = nfp_repr_get_locked(app, reprs, i);
		if (!netdev)
			continue;

		repr = netdev_priv(netdev);
		if (repr->port->type != NFP_PORT_INVALID)
			continue;

		nfp_app_repr_preclean(app, netdev);
		rtnl_lock();
		rcu_assign_pointer(reprs->reprs[i], NULL);
		rtnl_unlock();
		synchronize_rcu();
		nfp_repr_clean(repr);
	}

	return 0;
}
