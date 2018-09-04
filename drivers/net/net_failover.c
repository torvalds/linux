// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

/* This provides a net_failover interface for paravirtual drivers to
 * provide an alternate datapath by exporting APIs to create and
 * destroy a upper 'net_failover' netdev. The upper dev manages the
 * original paravirtual interface as a 'standby' netdev and uses the
 * generic failover infrastructure to register and manage a direct
 * attached VF as a 'primary' netdev. This enables live migration of
 * a VM with direct attached VF by failing over to the paravirtual
 * datapath when the VF is unplugged.
 *
 * Some of the netdev management routines are based on bond/team driver as
 * this driver provides active-backup functionality similar to those drivers.
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/netpoll.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <linux/pci.h>
#include <net/sch_generic.h>
#include <uapi/linux/if_arp.h>
#include <net/net_failover.h>

static bool net_failover_xmit_ready(struct net_device *dev)
{
	return netif_running(dev) && netif_carrier_ok(dev);
}

static int net_failover_open(struct net_device *dev)
{
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *primary_dev, *standby_dev;
	int err;

	primary_dev = rtnl_dereference(nfo_info->primary_dev);
	if (primary_dev) {
		err = dev_open(primary_dev);
		if (err)
			goto err_primary_open;
	}

	standby_dev = rtnl_dereference(nfo_info->standby_dev);
	if (standby_dev) {
		err = dev_open(standby_dev);
		if (err)
			goto err_standby_open;
	}

	if ((primary_dev && net_failover_xmit_ready(primary_dev)) ||
	    (standby_dev && net_failover_xmit_ready(standby_dev))) {
		netif_carrier_on(dev);
		netif_tx_wake_all_queues(dev);
	}

	return 0;

err_standby_open:
	dev_close(primary_dev);
err_primary_open:
	netif_tx_disable(dev);
	return err;
}

static int net_failover_close(struct net_device *dev)
{
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *slave_dev;

	netif_tx_disable(dev);

	slave_dev = rtnl_dereference(nfo_info->primary_dev);
	if (slave_dev)
		dev_close(slave_dev);

	slave_dev = rtnl_dereference(nfo_info->standby_dev);
	if (slave_dev)
		dev_close(slave_dev);

	return 0;
}

static netdev_tx_t net_failover_drop_xmit(struct sk_buff *skb,
					  struct net_device *dev)
{
	atomic_long_inc(&dev->tx_dropped);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static netdev_tx_t net_failover_start_xmit(struct sk_buff *skb,
					   struct net_device *dev)
{
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *xmit_dev;

	/* Try xmit via primary netdev followed by standby netdev */
	xmit_dev = rcu_dereference_bh(nfo_info->primary_dev);
	if (!xmit_dev || !net_failover_xmit_ready(xmit_dev)) {
		xmit_dev = rcu_dereference_bh(nfo_info->standby_dev);
		if (!xmit_dev || !net_failover_xmit_ready(xmit_dev))
			return net_failover_drop_xmit(skb, dev);
	}

	skb->dev = xmit_dev;
	skb->queue_mapping = qdisc_skb_cb(skb)->slave_dev_queue_mapping;

	return dev_queue_xmit(skb);
}

static u16 net_failover_select_queue(struct net_device *dev,
				     struct sk_buff *skb,
				     struct net_device *sb_dev,
				     select_queue_fallback_t fallback)
{
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *primary_dev;
	u16 txq;

	primary_dev = rcu_dereference(nfo_info->primary_dev);
	if (primary_dev) {
		const struct net_device_ops *ops = primary_dev->netdev_ops;

		if (ops->ndo_select_queue)
			txq = ops->ndo_select_queue(primary_dev, skb,
						    sb_dev, fallback);
		else
			txq = fallback(primary_dev, skb, NULL);

		qdisc_skb_cb(skb)->slave_dev_queue_mapping = skb->queue_mapping;

		return txq;
	}

	txq = skb_rx_queue_recorded(skb) ? skb_get_rx_queue(skb) : 0;

	/* Save the original txq to restore before passing to the driver */
	qdisc_skb_cb(skb)->slave_dev_queue_mapping = skb->queue_mapping;

	if (unlikely(txq >= dev->real_num_tx_queues)) {
		do {
			txq -= dev->real_num_tx_queues;
		} while (txq >= dev->real_num_tx_queues);
	}

	return txq;
}

/* fold stats, assuming all rtnl_link_stats64 fields are u64, but
 * that some drivers can provide 32bit values only.
 */
static void net_failover_fold_stats(struct rtnl_link_stats64 *_res,
				    const struct rtnl_link_stats64 *_new,
				    const struct rtnl_link_stats64 *_old)
{
	const u64 *new = (const u64 *)_new;
	const u64 *old = (const u64 *)_old;
	u64 *res = (u64 *)_res;
	int i;

	for (i = 0; i < sizeof(*_res) / sizeof(u64); i++) {
		u64 nv = new[i];
		u64 ov = old[i];
		s64 delta = nv - ov;

		/* detects if this particular field is 32bit only */
		if (((nv | ov) >> 32) == 0)
			delta = (s64)(s32)((u32)nv - (u32)ov);

		/* filter anomalies, some drivers reset their stats
		 * at down/up events.
		 */
		if (delta > 0)
			res[i] += delta;
	}
}

static void net_failover_get_stats(struct net_device *dev,
				   struct rtnl_link_stats64 *stats)
{
	struct net_failover_info *nfo_info = netdev_priv(dev);
	const struct rtnl_link_stats64 *new;
	struct rtnl_link_stats64 temp;
	struct net_device *slave_dev;

	spin_lock(&nfo_info->stats_lock);
	memcpy(stats, &nfo_info->failover_stats, sizeof(*stats));

	rcu_read_lock();

	slave_dev = rcu_dereference(nfo_info->primary_dev);
	if (slave_dev) {
		new = dev_get_stats(slave_dev, &temp);
		net_failover_fold_stats(stats, new, &nfo_info->primary_stats);
		memcpy(&nfo_info->primary_stats, new, sizeof(*new));
	}

	slave_dev = rcu_dereference(nfo_info->standby_dev);
	if (slave_dev) {
		new = dev_get_stats(slave_dev, &temp);
		net_failover_fold_stats(stats, new, &nfo_info->standby_stats);
		memcpy(&nfo_info->standby_stats, new, sizeof(*new));
	}

	rcu_read_unlock();

	memcpy(&nfo_info->failover_stats, stats, sizeof(*stats));
	spin_unlock(&nfo_info->stats_lock);
}

static int net_failover_change_mtu(struct net_device *dev, int new_mtu)
{
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *primary_dev, *standby_dev;
	int ret = 0;

	primary_dev = rtnl_dereference(nfo_info->primary_dev);
	if (primary_dev) {
		ret = dev_set_mtu(primary_dev, new_mtu);
		if (ret)
			return ret;
	}

	standby_dev = rtnl_dereference(nfo_info->standby_dev);
	if (standby_dev) {
		ret = dev_set_mtu(standby_dev, new_mtu);
		if (ret) {
			if (primary_dev)
				dev_set_mtu(primary_dev, dev->mtu);
			return ret;
		}
	}

	dev->mtu = new_mtu;

	return 0;
}

static void net_failover_set_rx_mode(struct net_device *dev)
{
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *slave_dev;

	rcu_read_lock();

	slave_dev = rcu_dereference(nfo_info->primary_dev);
	if (slave_dev) {
		dev_uc_sync_multiple(slave_dev, dev);
		dev_mc_sync_multiple(slave_dev, dev);
	}

	slave_dev = rcu_dereference(nfo_info->standby_dev);
	if (slave_dev) {
		dev_uc_sync_multiple(slave_dev, dev);
		dev_mc_sync_multiple(slave_dev, dev);
	}

	rcu_read_unlock();
}

static int net_failover_vlan_rx_add_vid(struct net_device *dev, __be16 proto,
					u16 vid)
{
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *primary_dev, *standby_dev;
	int ret = 0;

	primary_dev = rcu_dereference(nfo_info->primary_dev);
	if (primary_dev) {
		ret = vlan_vid_add(primary_dev, proto, vid);
		if (ret)
			return ret;
	}

	standby_dev = rcu_dereference(nfo_info->standby_dev);
	if (standby_dev) {
		ret = vlan_vid_add(standby_dev, proto, vid);
		if (ret)
			if (primary_dev)
				vlan_vid_del(primary_dev, proto, vid);
	}

	return ret;
}

static int net_failover_vlan_rx_kill_vid(struct net_device *dev, __be16 proto,
					 u16 vid)
{
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *slave_dev;

	slave_dev = rcu_dereference(nfo_info->primary_dev);
	if (slave_dev)
		vlan_vid_del(slave_dev, proto, vid);

	slave_dev = rcu_dereference(nfo_info->standby_dev);
	if (slave_dev)
		vlan_vid_del(slave_dev, proto, vid);

	return 0;
}

static const struct net_device_ops failover_dev_ops = {
	.ndo_open		= net_failover_open,
	.ndo_stop		= net_failover_close,
	.ndo_start_xmit		= net_failover_start_xmit,
	.ndo_select_queue	= net_failover_select_queue,
	.ndo_get_stats64	= net_failover_get_stats,
	.ndo_change_mtu		= net_failover_change_mtu,
	.ndo_set_rx_mode	= net_failover_set_rx_mode,
	.ndo_vlan_rx_add_vid	= net_failover_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= net_failover_vlan_rx_kill_vid,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_features_check	= passthru_features_check,
};

#define FAILOVER_NAME "net_failover"
#define FAILOVER_VERSION "0.1"

static void nfo_ethtool_get_drvinfo(struct net_device *dev,
				    struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, FAILOVER_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, FAILOVER_VERSION, sizeof(drvinfo->version));
}

static int nfo_ethtool_get_link_ksettings(struct net_device *dev,
					  struct ethtool_link_ksettings *cmd)
{
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *slave_dev;

	slave_dev = rtnl_dereference(nfo_info->primary_dev);
	if (!slave_dev || !net_failover_xmit_ready(slave_dev)) {
		slave_dev = rtnl_dereference(nfo_info->standby_dev);
		if (!slave_dev || !net_failover_xmit_ready(slave_dev)) {
			cmd->base.duplex = DUPLEX_UNKNOWN;
			cmd->base.port = PORT_OTHER;
			cmd->base.speed = SPEED_UNKNOWN;

			return 0;
		}
	}

	return __ethtool_get_link_ksettings(slave_dev, cmd);
}

static const struct ethtool_ops failover_ethtool_ops = {
	.get_drvinfo            = nfo_ethtool_get_drvinfo,
	.get_link               = ethtool_op_get_link,
	.get_link_ksettings     = nfo_ethtool_get_link_ksettings,
};

/* Called when slave dev is injecting data into network stack.
 * Change the associated network device from lower dev to failover dev.
 * note: already called with rcu_read_lock
 */
static rx_handler_result_t net_failover_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct net_device *dev = rcu_dereference(skb->dev->rx_handler_data);
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *primary_dev, *standby_dev;

	primary_dev = rcu_dereference(nfo_info->primary_dev);
	standby_dev = rcu_dereference(nfo_info->standby_dev);

	if (primary_dev && skb->dev == standby_dev)
		return RX_HANDLER_EXACT;

	skb->dev = dev;

	return RX_HANDLER_ANOTHER;
}

static void net_failover_compute_features(struct net_device *dev)
{
	netdev_features_t vlan_features = FAILOVER_VLAN_FEATURES &
					  NETIF_F_ALL_FOR_ALL;
	netdev_features_t enc_features  = FAILOVER_ENC_FEATURES;
	unsigned short max_hard_header_len = ETH_HLEN;
	unsigned int dst_release_flag = IFF_XMIT_DST_RELEASE |
					IFF_XMIT_DST_RELEASE_PERM;
	struct net_failover_info *nfo_info = netdev_priv(dev);
	struct net_device *primary_dev, *standby_dev;

	primary_dev = rcu_dereference(nfo_info->primary_dev);
	if (primary_dev) {
		vlan_features =
			netdev_increment_features(vlan_features,
						  primary_dev->vlan_features,
						  FAILOVER_VLAN_FEATURES);
		enc_features =
			netdev_increment_features(enc_features,
						  primary_dev->hw_enc_features,
						  FAILOVER_ENC_FEATURES);

		dst_release_flag &= primary_dev->priv_flags;
		if (primary_dev->hard_header_len > max_hard_header_len)
			max_hard_header_len = primary_dev->hard_header_len;
	}

	standby_dev = rcu_dereference(nfo_info->standby_dev);
	if (standby_dev) {
		vlan_features =
			netdev_increment_features(vlan_features,
						  standby_dev->vlan_features,
						  FAILOVER_VLAN_FEATURES);
		enc_features =
			netdev_increment_features(enc_features,
						  standby_dev->hw_enc_features,
						  FAILOVER_ENC_FEATURES);

		dst_release_flag &= standby_dev->priv_flags;
		if (standby_dev->hard_header_len > max_hard_header_len)
			max_hard_header_len = standby_dev->hard_header_len;
	}

	dev->vlan_features = vlan_features;
	dev->hw_enc_features = enc_features | NETIF_F_GSO_ENCAP_ALL;
	dev->hard_header_len = max_hard_header_len;

	dev->priv_flags &= ~IFF_XMIT_DST_RELEASE;
	if (dst_release_flag == (IFF_XMIT_DST_RELEASE |
				 IFF_XMIT_DST_RELEASE_PERM))
		dev->priv_flags |= IFF_XMIT_DST_RELEASE;

	netdev_change_features(dev);
}

static void net_failover_lower_state_changed(struct net_device *slave_dev,
					     struct net_device *primary_dev,
					     struct net_device *standby_dev)
{
	struct netdev_lag_lower_state_info info;

	if (netif_carrier_ok(slave_dev))
		info.link_up = true;
	else
		info.link_up = false;

	if (slave_dev == primary_dev) {
		if (netif_running(primary_dev))
			info.tx_enabled = true;
		else
			info.tx_enabled = false;
	} else {
		if ((primary_dev && netif_running(primary_dev)) ||
		    (!netif_running(standby_dev)))
			info.tx_enabled = false;
		else
			info.tx_enabled = true;
	}

	netdev_lower_state_changed(slave_dev, &info);
}

static int net_failover_slave_pre_register(struct net_device *slave_dev,
					   struct net_device *failover_dev)
{
	struct net_device *standby_dev, *primary_dev;
	struct net_failover_info *nfo_info;
	bool slave_is_standby;

	nfo_info = netdev_priv(failover_dev);
	standby_dev = rtnl_dereference(nfo_info->standby_dev);
	primary_dev = rtnl_dereference(nfo_info->primary_dev);
	slave_is_standby = slave_dev->dev.parent == failover_dev->dev.parent;
	if (slave_is_standby ? standby_dev : primary_dev) {
		netdev_err(failover_dev, "%s attempting to register as slave dev when %s already present\n",
			   slave_dev->name,
			   slave_is_standby ? "standby" : "primary");
		return -EINVAL;
	}

	/* We want to allow only a direct attached VF device as a primary
	 * netdev. As there is no easy way to check for a VF device, restrict
	 * this to a pci device.
	 */
	if (!slave_is_standby && (!slave_dev->dev.parent ||
				  !dev_is_pci(slave_dev->dev.parent)))
		return -EINVAL;

	if (failover_dev->features & NETIF_F_VLAN_CHALLENGED &&
	    vlan_uses_dev(failover_dev)) {
		netdev_err(failover_dev, "Device %s is VLAN challenged and failover device has VLAN set up\n",
			   failover_dev->name);
		return -EINVAL;
	}

	return 0;
}

static int net_failover_slave_register(struct net_device *slave_dev,
				       struct net_device *failover_dev)
{
	struct net_device *standby_dev, *primary_dev;
	struct net_failover_info *nfo_info;
	bool slave_is_standby;
	u32 orig_mtu;
	int err;

	/* Align MTU of slave with failover dev */
	orig_mtu = slave_dev->mtu;
	err = dev_set_mtu(slave_dev, failover_dev->mtu);
	if (err) {
		netdev_err(failover_dev, "unable to change mtu of %s to %u register failed\n",
			   slave_dev->name, failover_dev->mtu);
		goto done;
	}

	dev_hold(slave_dev);

	if (netif_running(failover_dev)) {
		err = dev_open(slave_dev);
		if (err && (err != -EBUSY)) {
			netdev_err(failover_dev, "Opening slave %s failed err:%d\n",
				   slave_dev->name, err);
			goto err_dev_open;
		}
	}

	netif_addr_lock_bh(failover_dev);
	dev_uc_sync_multiple(slave_dev, failover_dev);
	dev_mc_sync_multiple(slave_dev, failover_dev);
	netif_addr_unlock_bh(failover_dev);

	err = vlan_vids_add_by_dev(slave_dev, failover_dev);
	if (err) {
		netdev_err(failover_dev, "Failed to add vlan ids to device %s err:%d\n",
			   slave_dev->name, err);
		goto err_vlan_add;
	}

	nfo_info = netdev_priv(failover_dev);
	standby_dev = rtnl_dereference(nfo_info->standby_dev);
	primary_dev = rtnl_dereference(nfo_info->primary_dev);
	slave_is_standby = slave_dev->dev.parent == failover_dev->dev.parent;

	if (slave_is_standby) {
		rcu_assign_pointer(nfo_info->standby_dev, slave_dev);
		standby_dev = slave_dev;
		dev_get_stats(standby_dev, &nfo_info->standby_stats);
	} else {
		rcu_assign_pointer(nfo_info->primary_dev, slave_dev);
		primary_dev = slave_dev;
		dev_get_stats(primary_dev, &nfo_info->primary_stats);
		failover_dev->min_mtu = slave_dev->min_mtu;
		failover_dev->max_mtu = slave_dev->max_mtu;
	}

	net_failover_lower_state_changed(slave_dev, primary_dev, standby_dev);
	net_failover_compute_features(failover_dev);

	call_netdevice_notifiers(NETDEV_JOIN, slave_dev);

	netdev_info(failover_dev, "failover %s slave:%s registered\n",
		    slave_is_standby ? "standby" : "primary", slave_dev->name);

	return 0;

err_vlan_add:
	dev_uc_unsync(slave_dev, failover_dev);
	dev_mc_unsync(slave_dev, failover_dev);
	dev_close(slave_dev);
err_dev_open:
	dev_put(slave_dev);
	dev_set_mtu(slave_dev, orig_mtu);
done:
	return err;
}

static int net_failover_slave_pre_unregister(struct net_device *slave_dev,
					     struct net_device *failover_dev)
{
	struct net_device *standby_dev, *primary_dev;
	struct net_failover_info *nfo_info;

	nfo_info = netdev_priv(failover_dev);
	primary_dev = rtnl_dereference(nfo_info->primary_dev);
	standby_dev = rtnl_dereference(nfo_info->standby_dev);

	if (slave_dev != primary_dev && slave_dev != standby_dev)
		return -ENODEV;

	return 0;
}

static int net_failover_slave_unregister(struct net_device *slave_dev,
					 struct net_device *failover_dev)
{
	struct net_device *standby_dev, *primary_dev;
	struct net_failover_info *nfo_info;
	bool slave_is_standby;

	nfo_info = netdev_priv(failover_dev);
	primary_dev = rtnl_dereference(nfo_info->primary_dev);
	standby_dev = rtnl_dereference(nfo_info->standby_dev);

	if (WARN_ON_ONCE(slave_dev != primary_dev && slave_dev != standby_dev))
		return -ENODEV;

	vlan_vids_del_by_dev(slave_dev, failover_dev);
	dev_uc_unsync(slave_dev, failover_dev);
	dev_mc_unsync(slave_dev, failover_dev);
	dev_close(slave_dev);

	nfo_info = netdev_priv(failover_dev);
	dev_get_stats(failover_dev, &nfo_info->failover_stats);

	slave_is_standby = slave_dev->dev.parent == failover_dev->dev.parent;
	if (slave_is_standby) {
		RCU_INIT_POINTER(nfo_info->standby_dev, NULL);
	} else {
		RCU_INIT_POINTER(nfo_info->primary_dev, NULL);
		if (standby_dev) {
			failover_dev->min_mtu = standby_dev->min_mtu;
			failover_dev->max_mtu = standby_dev->max_mtu;
		}
	}

	dev_put(slave_dev);

	net_failover_compute_features(failover_dev);

	netdev_info(failover_dev, "failover %s slave:%s unregistered\n",
		    slave_is_standby ? "standby" : "primary", slave_dev->name);

	return 0;
}

static int net_failover_slave_link_change(struct net_device *slave_dev,
					  struct net_device *failover_dev)
{
	struct net_device *primary_dev, *standby_dev;
	struct net_failover_info *nfo_info;

	nfo_info = netdev_priv(failover_dev);

	primary_dev = rtnl_dereference(nfo_info->primary_dev);
	standby_dev = rtnl_dereference(nfo_info->standby_dev);

	if (slave_dev != primary_dev && slave_dev != standby_dev)
		return -ENODEV;

	if ((primary_dev && net_failover_xmit_ready(primary_dev)) ||
	    (standby_dev && net_failover_xmit_ready(standby_dev))) {
		netif_carrier_on(failover_dev);
		netif_tx_wake_all_queues(failover_dev);
	} else {
		dev_get_stats(failover_dev, &nfo_info->failover_stats);
		netif_carrier_off(failover_dev);
		netif_tx_stop_all_queues(failover_dev);
	}

	net_failover_lower_state_changed(slave_dev, primary_dev, standby_dev);

	return 0;
}

static int net_failover_slave_name_change(struct net_device *slave_dev,
					  struct net_device *failover_dev)
{
	struct net_device *primary_dev, *standby_dev;
	struct net_failover_info *nfo_info;

	nfo_info = netdev_priv(failover_dev);

	primary_dev = rtnl_dereference(nfo_info->primary_dev);
	standby_dev = rtnl_dereference(nfo_info->standby_dev);

	if (slave_dev != primary_dev && slave_dev != standby_dev)
		return -ENODEV;

	/* We need to bring up the slave after the rename by udev in case
	 * open failed with EBUSY when it was registered.
	 */
	dev_open(slave_dev);

	return 0;
}

static struct failover_ops net_failover_ops = {
	.slave_pre_register	= net_failover_slave_pre_register,
	.slave_register		= net_failover_slave_register,
	.slave_pre_unregister	= net_failover_slave_pre_unregister,
	.slave_unregister	= net_failover_slave_unregister,
	.slave_link_change	= net_failover_slave_link_change,
	.slave_name_change	= net_failover_slave_name_change,
	.slave_handle_frame	= net_failover_handle_frame,
};

/**
 * net_failover_create - Create and register a failover instance
 *
 * @dev: standby netdev
 *
 * Creates a failover netdev and registers a failover instance for a standby
 * netdev. Used by paravirtual drivers that use 3-netdev model.
 * The failover netdev acts as a master device and controls 2 slave devices -
 * the original standby netdev and a VF netdev with the same MAC gets
 * registered as primary netdev.
 *
 * Return: pointer to failover instance
 */
struct failover *net_failover_create(struct net_device *standby_dev)
{
	struct device *dev = standby_dev->dev.parent;
	struct net_device *failover_dev;
	struct failover *failover;
	int err;

	/* Alloc at least 2 queues, for now we are going with 16 assuming
	 * that VF devices being enslaved won't have too many queues.
	 */
	failover_dev = alloc_etherdev_mq(sizeof(struct net_failover_info), 16);
	if (!failover_dev) {
		dev_err(dev, "Unable to allocate failover_netdev!\n");
		return ERR_PTR(-ENOMEM);
	}

	dev_net_set(failover_dev, dev_net(standby_dev));
	SET_NETDEV_DEV(failover_dev, dev);

	failover_dev->netdev_ops = &failover_dev_ops;
	failover_dev->ethtool_ops = &failover_ethtool_ops;

	/* Initialize the device options */
	failover_dev->priv_flags |= IFF_UNICAST_FLT | IFF_NO_QUEUE;
	failover_dev->priv_flags &= ~(IFF_XMIT_DST_RELEASE |
				       IFF_TX_SKB_SHARING);

	/* don't acquire failover netdev's netif_tx_lock when transmitting */
	failover_dev->features |= NETIF_F_LLTX;

	/* Don't allow failover devices to change network namespaces. */
	failover_dev->features |= NETIF_F_NETNS_LOCAL;

	failover_dev->hw_features = FAILOVER_VLAN_FEATURES |
				    NETIF_F_HW_VLAN_CTAG_TX |
				    NETIF_F_HW_VLAN_CTAG_RX |
				    NETIF_F_HW_VLAN_CTAG_FILTER;

	failover_dev->hw_features |= NETIF_F_GSO_ENCAP_ALL;
	failover_dev->features |= failover_dev->hw_features;

	memcpy(failover_dev->dev_addr, standby_dev->dev_addr,
	       failover_dev->addr_len);

	failover_dev->min_mtu = standby_dev->min_mtu;
	failover_dev->max_mtu = standby_dev->max_mtu;

	err = register_netdev(failover_dev);
	if (err) {
		dev_err(dev, "Unable to register failover_dev!\n");
		goto err_register_netdev;
	}

	netif_carrier_off(failover_dev);

	failover = failover_register(failover_dev, &net_failover_ops);
	if (IS_ERR(failover))
		goto err_failover_register;

	return failover;

err_failover_register:
	unregister_netdev(failover_dev);
err_register_netdev:
	free_netdev(failover_dev);

	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(net_failover_create);

/**
 * net_failover_destroy - Destroy a failover instance
 *
 * @failover: pointer to failover instance
 *
 * Unregisters any slave netdevs associated with the failover instance by
 * calling failover_slave_unregister().
 * unregisters the failover instance itself and finally frees the failover
 * netdev. Used by paravirtual drivers that use 3-netdev model.
 *
 */
void net_failover_destroy(struct failover *failover)
{
	struct net_failover_info *nfo_info;
	struct net_device *failover_dev;
	struct net_device *slave_dev;

	if (!failover)
		return;

	failover_dev = rcu_dereference(failover->failover_dev);
	nfo_info = netdev_priv(failover_dev);

	netif_device_detach(failover_dev);

	rtnl_lock();

	slave_dev = rtnl_dereference(nfo_info->primary_dev);
	if (slave_dev)
		failover_slave_unregister(slave_dev);

	slave_dev = rtnl_dereference(nfo_info->standby_dev);
	if (slave_dev)
		failover_slave_unregister(slave_dev);

	failover_unregister(failover);

	unregister_netdevice(failover_dev);

	rtnl_unlock();

	free_netdev(failover_dev);
}
EXPORT_SYMBOL_GPL(net_failover_destroy);

static __init int
net_failover_init(void)
{
	return 0;
}
module_init(net_failover_init);

static __exit
void net_failover_exit(void)
{
}
module_exit(net_failover_exit);

MODULE_DESCRIPTION("Failover driver for Paravirtual drivers");
MODULE_LICENSE("GPL v2");
