// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/debugfs.h>
#include <linux/ethtool.h>
#include <linux/random.h>

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
			     struct ethtool_coalesce *coal)
{
	struct netdevsim *ns = netdev_priv(dev);

	memcpy(coal, &ns->ethtool.coalesce, sizeof(ns->ethtool.coalesce));
	return 0;
}

static int nsim_set_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *coal)
{
	struct netdevsim *ns = netdev_priv(dev);

	memcpy(&ns->ethtool.coalesce, coal, sizeof(ns->ethtool.coalesce));
	return 0;
}

static void nsim_get_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *ring)
{
	struct netdevsim *ns = netdev_priv(dev);

	memcpy(ring, &ns->ethtool.ring, sizeof(ns->ethtool.ring));
}

static int nsim_set_ringparam(struct net_device *dev,
			      struct ethtool_ringparam *ring)
{
	struct netdevsim *ns = netdev_priv(dev);

	memcpy(&ns->ethtool.ring, ring, sizeof(ns->ethtool.ring));
	return 0;
}

static const struct ethtool_ops nsim_ethtool_ops = {
	.supported_coalesce_params	= ETHTOOL_COALESCE_ALL_PARAMS,
	.get_pause_stats	        = nsim_get_pause_stats,
	.get_pauseparam		        = nsim_get_pauseparam,
	.set_pauseparam		        = nsim_set_pauseparam,
	.set_coalesce			= nsim_set_coalesce,
	.get_coalesce			= nsim_get_coalesce,
	.get_ringparam			= nsim_get_ringparam,
	.set_ringparam			= nsim_set_ringparam,
};

static void nsim_ethtool_ring_init(struct netdevsim *ns)
{
	ns->ethtool.ring.rx_max_pending = 4096;
	ns->ethtool.ring.rx_jumbo_max_pending = 4096;
	ns->ethtool.ring.rx_mini_max_pending = 4096;
	ns->ethtool.ring.tx_max_pending = 4096;
}

void nsim_ethtool_init(struct netdevsim *ns)
{
	struct dentry *ethtool, *dir;

	ns->netdev->ethtool_ops = &nsim_ethtool_ops;

	nsim_ethtool_ring_init(ns);

	ethtool = debugfs_create_dir("ethtool", ns->nsim_dev_port->ddir);

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
