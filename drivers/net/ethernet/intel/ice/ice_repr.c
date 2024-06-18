// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice.h"
#include "ice_eswitch.h"
#include "devlink/devlink.h"
#include "devlink/devlink_port.h"
#include "ice_sriov.h"
#include "ice_tc_lib.h"
#include "ice_dcb_lib.h"

/**
 * ice_repr_inc_tx_stats - increment Tx statistic by one packet
 * @repr: repr to increment stats on
 * @len: length of the packet
 * @xmit_status: value returned by xmit function
 */
void ice_repr_inc_tx_stats(struct ice_repr *repr, unsigned int len,
			   int xmit_status)
{
	struct ice_repr_pcpu_stats *stats;

	if (unlikely(xmit_status != NET_XMIT_SUCCESS &&
		     xmit_status != NET_XMIT_CN)) {
		this_cpu_inc(repr->stats->tx_drops);
		return;
	}

	stats = this_cpu_ptr(repr->stats);
	u64_stats_update_begin(&stats->syncp);
	stats->tx_packets++;
	stats->tx_bytes += len;
	u64_stats_update_end(&stats->syncp);
}

/**
 * ice_repr_inc_rx_stats - increment Rx statistic by one packet
 * @netdev: repr netdev to increment stats on
 * @len: length of the packet
 */
void ice_repr_inc_rx_stats(struct net_device *netdev, unsigned int len)
{
	struct ice_repr *repr = ice_netdev_to_repr(netdev);
	struct ice_repr_pcpu_stats *stats;

	stats = this_cpu_ptr(repr->stats);
	u64_stats_update_begin(&stats->syncp);
	stats->rx_packets++;
	stats->rx_bytes += len;
	u64_stats_update_end(&stats->syncp);
}

/**
 * ice_repr_get_stats64 - get VF stats for VFPR use
 * @netdev: pointer to port representor netdev
 * @stats: pointer to struct where stats can be stored
 */
static void
ice_repr_get_stats64(struct net_device *netdev, struct rtnl_link_stats64 *stats)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_eth_stats *eth_stats;
	struct ice_vsi *vsi;

	if (ice_is_vf_disabled(np->repr->vf))
		return;
	vsi = np->repr->src_vsi;

	ice_update_vsi_stats(vsi);
	eth_stats = &vsi->eth_stats;

	stats->tx_packets = eth_stats->tx_unicast + eth_stats->tx_broadcast +
			    eth_stats->tx_multicast;
	stats->rx_packets = eth_stats->rx_unicast + eth_stats->rx_broadcast +
			    eth_stats->rx_multicast;
	stats->tx_bytes = eth_stats->tx_bytes;
	stats->rx_bytes = eth_stats->rx_bytes;
	stats->multicast = eth_stats->rx_multicast;
	stats->tx_errors = eth_stats->tx_errors;
	stats->tx_dropped = eth_stats->tx_discards;
	stats->rx_dropped = eth_stats->rx_discards;
}

/**
 * ice_netdev_to_repr - Get port representor for given netdevice
 * @netdev: pointer to port representor netdev
 */
struct ice_repr *ice_netdev_to_repr(const struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);

	return np->repr;
}

/**
 * ice_repr_open - Enable port representor's network interface
 * @netdev: network interface device structure
 *
 * The open entry point is called when a port representor's network
 * interface is made active by the system (IFF_UP). Corresponding
 * VF is notified about link status change.
 *
 * Returns 0 on success
 */
static int ice_repr_open(struct net_device *netdev)
{
	struct ice_repr *repr = ice_netdev_to_repr(netdev);
	struct ice_vf *vf;

	vf = repr->vf;
	vf->link_forced = true;
	vf->link_up = true;
	ice_vc_notify_vf_link_state(vf);

	netif_carrier_on(netdev);
	netif_tx_start_all_queues(netdev);

	return 0;
}

/**
 * ice_repr_stop - Disable port representor's network interface
 * @netdev: network interface device structure
 *
 * The stop entry point is called when a port representor's network
 * interface is de-activated by the system. Corresponding
 * VF is notified about link status change.
 *
 * Returns 0 on success
 */
static int ice_repr_stop(struct net_device *netdev)
{
	struct ice_repr *repr = ice_netdev_to_repr(netdev);
	struct ice_vf *vf;

	vf = repr->vf;
	vf->link_forced = true;
	vf->link_up = false;
	ice_vc_notify_vf_link_state(vf);

	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	return 0;
}

/**
 * ice_repr_sp_stats64 - get slow path stats for port representor
 * @dev: network interface device structure
 * @stats: netlink stats structure
 */
static int
ice_repr_sp_stats64(const struct net_device *dev,
		    struct rtnl_link_stats64 *stats)
{
	struct ice_repr *repr = ice_netdev_to_repr(dev);
	int i;

	for_each_possible_cpu(i) {
		u64 tbytes, tpkts, tdrops, rbytes, rpkts;
		struct ice_repr_pcpu_stats *repr_stats;
		unsigned int start;

		repr_stats = per_cpu_ptr(repr->stats, i);
		do {
			start = u64_stats_fetch_begin(&repr_stats->syncp);
			tbytes = repr_stats->tx_bytes;
			tpkts = repr_stats->tx_packets;
			tdrops = repr_stats->tx_drops;
			rbytes = repr_stats->rx_bytes;
			rpkts = repr_stats->rx_packets;
		} while (u64_stats_fetch_retry(&repr_stats->syncp, start));

		stats->tx_bytes += tbytes;
		stats->tx_packets += tpkts;
		stats->tx_dropped += tdrops;
		stats->rx_bytes += rbytes;
		stats->rx_packets += rpkts;
	}
	return 0;
}

static bool
ice_repr_ndo_has_offload_stats(const struct net_device *dev, int attr_id)
{
	return attr_id == IFLA_OFFLOAD_XSTATS_CPU_HIT;
}

static int
ice_repr_ndo_get_offload_stats(int attr_id, const struct net_device *dev,
			       void *sp)
{
	if (attr_id == IFLA_OFFLOAD_XSTATS_CPU_HIT)
		return ice_repr_sp_stats64(dev, (struct rtnl_link_stats64 *)sp);

	return -EINVAL;
}

static int
ice_repr_setup_tc_cls_flower(struct ice_repr *repr,
			     struct flow_cls_offload *flower)
{
	switch (flower->command) {
	case FLOW_CLS_REPLACE:
		return ice_add_cls_flower(repr->netdev, repr->src_vsi, flower);
	case FLOW_CLS_DESTROY:
		return ice_del_cls_flower(repr->src_vsi, flower);
	default:
		return -EINVAL;
	}
}

static int
ice_repr_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
			   void *cb_priv)
{
	struct flow_cls_offload *flower = (struct flow_cls_offload *)type_data;
	struct ice_netdev_priv *np = (struct ice_netdev_priv *)cb_priv;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return ice_repr_setup_tc_cls_flower(np->repr, flower);
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(ice_repr_block_cb_list);

static int
ice_repr_setup_tc(struct net_device *netdev, enum tc_setup_type type,
		  void *type_data)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple((struct flow_block_offload *)
						  type_data,
						  &ice_repr_block_cb_list,
						  ice_repr_setup_tc_block_cb,
						  np, np, true);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops ice_repr_netdev_ops = {
	.ndo_get_stats64 = ice_repr_get_stats64,
	.ndo_open = ice_repr_open,
	.ndo_stop = ice_repr_stop,
	.ndo_start_xmit = ice_eswitch_port_start_xmit,
	.ndo_setup_tc = ice_repr_setup_tc,
	.ndo_has_offload_stats = ice_repr_ndo_has_offload_stats,
	.ndo_get_offload_stats = ice_repr_ndo_get_offload_stats,
};

/**
 * ice_is_port_repr_netdev - Check if a given netdevice is a port representor netdev
 * @netdev: pointer to netdev
 */
bool ice_is_port_repr_netdev(const struct net_device *netdev)
{
	return netdev && (netdev->netdev_ops == &ice_repr_netdev_ops);
}

/**
 * ice_repr_reg_netdev - register port representor netdev
 * @netdev: pointer to port representor netdev
 */
static int
ice_repr_reg_netdev(struct net_device *netdev)
{
	eth_hw_addr_random(netdev);
	netdev->netdev_ops = &ice_repr_netdev_ops;
	ice_set_ethtool_repr_ops(netdev);

	netdev->hw_features |= NETIF_F_HW_TC;

	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	return register_netdev(netdev);
}

static void ice_repr_remove_node(struct devlink_port *devlink_port)
{
	devl_lock(devlink_port->devlink);
	devl_rate_leaf_destroy(devlink_port);
	devl_unlock(devlink_port->devlink);
}

/**
 * ice_repr_rem - remove representor from VF
 * @repr: pointer to representor structure
 */
static void ice_repr_rem(struct ice_repr *repr)
{
	free_percpu(repr->stats);
	free_netdev(repr->netdev);
	kfree(repr);
}

/**
 * ice_repr_rem_vf - remove representor from VF
 * @repr: pointer to representor structure
 */
void ice_repr_rem_vf(struct ice_repr *repr)
{
	ice_repr_remove_node(&repr->vf->devlink_port);
	unregister_netdev(repr->netdev);
	ice_devlink_destroy_vf_port(repr->vf);
	ice_virtchnl_set_dflt_ops(repr->vf);
	ice_repr_rem(repr);
}

static void ice_repr_set_tx_topology(struct ice_pf *pf)
{
	struct devlink *devlink;

	/* only export if ADQ and DCB disabled and eswitch enabled*/
	if (ice_is_adq_active(pf) || ice_is_dcb_active(pf) ||
	    !ice_is_switchdev_running(pf))
		return;

	devlink = priv_to_devlink(pf);
	ice_devlink_rate_init_tx_topology(devlink, ice_get_main_vsi(pf));
}

/**
 * ice_repr_add - add representor for generic VSI
 * @pf: pointer to PF structure
 * @src_vsi: pointer to VSI structure of device to represent
 * @parent_mac: device MAC address
 */
static struct ice_repr *
ice_repr_add(struct ice_pf *pf, struct ice_vsi *src_vsi, const u8 *parent_mac)
{
	struct ice_netdev_priv *np;
	struct ice_repr *repr;
	int err;

	repr = kzalloc(sizeof(*repr), GFP_KERNEL);
	if (!repr)
		return ERR_PTR(-ENOMEM);

	repr->netdev = alloc_etherdev(sizeof(struct ice_netdev_priv));
	if (!repr->netdev) {
		err =  -ENOMEM;
		goto err_alloc;
	}

	repr->stats = netdev_alloc_pcpu_stats(struct ice_repr_pcpu_stats);
	if (!repr->stats) {
		err = -ENOMEM;
		goto err_stats;
	}

	repr->src_vsi = src_vsi;
	repr->id = src_vsi->vsi_num;
	np = netdev_priv(repr->netdev);
	np->repr = repr;

	ether_addr_copy(repr->parent_mac, parent_mac);

	return repr;

err_stats:
	free_netdev(repr->netdev);
err_alloc:
	kfree(repr);
	return ERR_PTR(err);
}

struct ice_repr *ice_repr_add_vf(struct ice_vf *vf)
{
	struct ice_repr *repr;
	struct ice_vsi *vsi;
	int err;

	vsi = ice_get_vf_vsi(vf);
	if (!vsi)
		return ERR_PTR(-ENOENT);

	err = ice_devlink_create_vf_port(vf);
	if (err)
		return ERR_PTR(err);

	repr = ice_repr_add(vf->pf, vsi, vf->hw_lan_addr);
	if (IS_ERR(repr)) {
		err = PTR_ERR(repr);
		goto err_repr_add;
	}

	repr->vf = vf;

	repr->netdev->min_mtu = ETH_MIN_MTU;
	repr->netdev->max_mtu = ICE_MAX_MTU;

	SET_NETDEV_DEV(repr->netdev, ice_pf_to_dev(vf->pf));
	SET_NETDEV_DEVLINK_PORT(repr->netdev, &vf->devlink_port);
	err = ice_repr_reg_netdev(repr->netdev);
	if (err)
		goto err_netdev;

	ice_virtchnl_set_repr_ops(vf);
	ice_repr_set_tx_topology(vf->pf);

	return repr;

err_netdev:
	ice_repr_rem(repr);
err_repr_add:
	ice_devlink_destroy_vf_port(vf);
	return ERR_PTR(err);
}

struct ice_repr *ice_repr_get_by_vsi(struct ice_vsi *vsi)
{
	if (!vsi->vf)
		return NULL;

	return xa_load(&vsi->back->eswitch.reprs, vsi->vf->repr_id);
}

/**
 * ice_repr_start_tx_queues - start Tx queues of port representor
 * @repr: pointer to repr structure
 */
void ice_repr_start_tx_queues(struct ice_repr *repr)
{
	netif_carrier_on(repr->netdev);
	netif_tx_start_all_queues(repr->netdev);
}

/**
 * ice_repr_stop_tx_queues - stop Tx queues of port representor
 * @repr: pointer to repr structure
 */
void ice_repr_stop_tx_queues(struct ice_repr *repr)
{
	netif_carrier_off(repr->netdev);
	netif_tx_stop_all_queues(repr->netdev);
}
