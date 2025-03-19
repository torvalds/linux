// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/debugfs.h>
#include <linux/random.h>
#include <net/netdev_queues.h>

#include "netdevsim.h"

static void
nsim_get_pause_stats(struct net_device *dev,
		     struct ethtool_pause_stats *pause_stats)
{
	struct netdevsim *ns = netdev_priv(dev);

	if (ns->ethtool.pauseparam.report_stats_rx)
		pause_stats->rx_pause_frames = 1;
	if (ns->ethtool.pauseparam.report_stats_tx)
		pause_stats->tx_pause_frames = 2;
}

static void
nsim_get_pauseparam(struct net_device *dev, struct ethtool_pauseparam *pause)
{
	struct netdevsim *ns = netdev_priv(dev);

	pause->autoneg = 0; /* We don't support ksettings, so can't pretend */
	pause->rx_pause = ns->ethtool.pauseparam.rx;
	pause->tx_pause = ns->ethtool.pauseparam.tx;
}

static int
nsim_set_pauseparam(struct net_device *dev, struct ethtool_pauseparam *pause)
{
	struct netdevsim *ns = netdev_priv(dev);

	if (pause->autoneg)
		return -EINVAL;

	ns->ethtool.pauseparam.rx = pause->rx_pause;
	ns->ethtool.pauseparam.tx = pause->tx_pause;
	return 0;
}

static int nsim_get_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *coal,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct netdevsim *ns = netdev_priv(dev);

	memcpy(coal, &ns->ethtool.coalesce, sizeof(ns->ethtool.coalesce));
	return 0;
}

static int nsim_set_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *coal,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct netdevsim *ns = netdev_priv(dev);

	memcpy(&ns->ethtool.coalesce, coal, sizeof(ns->ethtool.coalesce));
	return 0;
}

static void nsim_get_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct netdevsim *ns = netdev_priv(dev);

	memcpy(ring, &ns->ethtool.ring, sizeof(ns->ethtool.ring));
	kernel_ring->hds_thresh_max = NSIM_HDS_THRESHOLD_MAX;

	if (dev->cfg->hds_config == ETHTOOL_TCP_DATA_SPLIT_UNKNOWN)
		kernel_ring->tcp_data_split = ETHTOOL_TCP_DATA_SPLIT_ENABLED;
}

static int nsim_set_ringparam(struct net_device *dev,
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kernel_ring,
			      struct netlink_ext_ack *extack)
{
	struct netdevsim *ns = netdev_priv(dev);

	ns->ethtool.ring.rx_pending = ring->rx_pending;
	ns->ethtool.ring.rx_jumbo_pending = ring->rx_jumbo_pending;
	ns->ethtool.ring.rx_mini_pending = ring->rx_mini_pending;
	ns->ethtool.ring.tx_pending = ring->tx_pending;
	return 0;
}

static void
nsim_get_channels(struct net_device *dev, struct ethtool_channels *ch)
{
	struct netdevsim *ns = netdev_priv(dev);

	ch->max_combined = ns->nsim_bus_dev->num_queues;
	ch->combined_count = ns->ethtool.channels;
}

static int
nsim_set_channels(struct net_device *dev, struct ethtool_channels *ch)
{
	struct netdevsim *ns = netdev_priv(dev);
	int err;

	netdev_lock(dev);
	err = netif_set_real_num_queues(dev, ch->combined_count,
					ch->combined_count);
	netdev_unlock(dev);
	if (err)
		return err;

	ns->ethtool.channels = ch->combined_count;
	return 0;
}

static int
nsim_get_fecparam(struct net_device *dev, struct ethtool_fecparam *fecparam)
{
	struct netdevsim *ns = netdev_priv(dev);

	if (ns->ethtool.get_err)
		return -ns->ethtool.get_err;
	memcpy(fecparam, &ns->ethtool.fec, sizeof(ns->ethtool.fec));
	return 0;
}

static int
nsim_set_fecparam(struct net_device *dev, struct ethtool_fecparam *fecparam)
{
	struct netdevsim *ns = netdev_priv(dev);
	u32 fec;

	if (ns->ethtool.set_err)
		return -ns->ethtool.set_err;
	memcpy(&ns->ethtool.fec, fecparam, sizeof(ns->ethtool.fec));
	fec = fecparam->fec;
	if (fec == ETHTOOL_FEC_AUTO)
		fec |= ETHTOOL_FEC_OFF;
	fec |= ETHTOOL_FEC_NONE;
	ns->ethtool.fec.active_fec = 1 << (fls(fec) - 1);
	return 0;
}

static void
nsim_get_fec_stats(struct net_device *dev, struct ethtool_fec_stats *fec_stats)
{
	fec_stats->corrected_blocks.total = 123;
	fec_stats->uncorrectable_blocks.total = 4;
}

static int nsim_get_ts_info(struct net_device *dev,
			    struct kernel_ethtool_ts_info *info)
{
	struct netdevsim *ns = netdev_priv(dev);

	info->phc_index = mock_phc_index(ns->phc);

	return 0;
}

static const struct ethtool_ops nsim_ethtool_ops = {
	.supported_coalesce_params	= ETHTOOL_COALESCE_ALL_PARAMS,
	.supported_ring_params		= ETHTOOL_RING_USE_TCP_DATA_SPLIT |
					  ETHTOOL_RING_USE_HDS_THRS,
	.get_pause_stats	        = nsim_get_pause_stats,
	.get_pauseparam		        = nsim_get_pauseparam,
	.set_pauseparam		        = nsim_set_pauseparam,
	.set_coalesce			= nsim_set_coalesce,
	.get_coalesce			= nsim_get_coalesce,
	.get_ringparam			= nsim_get_ringparam,
	.set_ringparam			= nsim_set_ringparam,
	.get_channels			= nsim_get_channels,
	.set_channels			= nsim_set_channels,
	.get_fecparam			= nsim_get_fecparam,
	.set_fecparam			= nsim_set_fecparam,
	.get_fec_stats			= nsim_get_fec_stats,
	.get_ts_info			= nsim_get_ts_info,
};

static void nsim_ethtool_ring_init(struct netdevsim *ns)
{
	ns->ethtool.ring.rx_pending = 512;
	ns->ethtool.ring.rx_max_pending = 4096;
	ns->ethtool.ring.rx_jumbo_max_pending = 4096;
	ns->ethtool.ring.rx_mini_max_pending = 4096;
	ns->ethtool.ring.tx_pending = 512;
	ns->ethtool.ring.tx_max_pending = 4096;
}

void nsim_ethtool_init(struct netdevsim *ns)
{
	struct dentry *ethtool, *dir;

	ns->netdev->ethtool_ops = &nsim_ethtool_ops;

	nsim_ethtool_ring_init(ns);

	ns->ethtool.pauseparam.report_stats_rx = true;
	ns->ethtool.pauseparam.report_stats_tx = true;

	ns->ethtool.fec.fec = ETHTOOL_FEC_NONE;
	ns->ethtool.fec.active_fec = ETHTOOL_FEC_NONE;

	ns->ethtool.channels = ns->nsim_bus_dev->num_queues;

	ethtool = debugfs_create_dir("ethtool", ns->nsim_dev_port->ddir);

	debugfs_create_u32("get_err", 0600, ethtool, &ns->ethtool.get_err);
	debugfs_create_u32("set_err", 0600, ethtool, &ns->ethtool.set_err);

	dir = debugfs_create_dir("pause", ethtool);
	debugfs_create_bool("report_stats_rx", 0600, dir,
			    &ns->ethtool.pauseparam.report_stats_rx);
	debugfs_create_bool("report_stats_tx", 0600, dir,
			    &ns->ethtool.pauseparam.report_stats_tx);

	dir = debugfs_create_dir("ring", ethtool);
	debugfs_create_u32("rx_max_pending", 0600, dir,
			   &ns->ethtool.ring.rx_max_pending);
	debugfs_create_u32("rx_jumbo_max_pending", 0600, dir,
			   &ns->ethtool.ring.rx_jumbo_max_pending);
	debugfs_create_u32("rx_mini_max_pending", 0600, dir,
			   &ns->ethtool.ring.rx_mini_max_pending);
	debugfs_create_u32("tx_max_pending", 0600, dir,
			   &ns->ethtool.ring.tx_max_pending);
}
