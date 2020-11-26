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

	if (ns->ethtool.report_stats_rx)
		pause_stats->rx_pause_frames = 1;
	if (ns->ethtool.report_stats_tx)
		pause_stats->tx_pause_frames = 2;
}

static void
nsim_get_pauseparam(struct net_device *dev, struct ethtool_pauseparam *pause)
{
	struct netdevsim *ns = netdev_priv(dev);

	pause->autoneg = 0; /* We don't support ksettings, so can't pretend */
	pause->rx_pause = ns->ethtool.rx;
	pause->tx_pause = ns->ethtool.tx;
}

static int
nsim_set_pauseparam(struct net_device *dev, struct ethtool_pauseparam *pause)
{
	struct netdevsim *ns = netdev_priv(dev);

	if (pause->autoneg)
		return -EINVAL;

	ns->ethtool.rx = pause->rx_pause;
	ns->ethtool.tx = pause->tx_pause;
	return 0;
}

static const struct ethtool_ops nsim_ethtool_ops = {
	.get_pause_stats	= nsim_get_pause_stats,
	.get_pauseparam		= nsim_get_pauseparam,
	.set_pauseparam		= nsim_set_pauseparam,
};

void nsim_ethtool_init(struct netdevsim *ns)
{
	struct dentry *ethtool, *dir;

	ns->netdev->ethtool_ops = &nsim_ethtool_ops;

	ethtool = debugfs_create_dir("ethtool", ns->nsim_dev_port->ddir);

	dir = debugfs_create_dir("pause", ethtool);
	debugfs_create_bool("report_stats_rx", 0600, dir,
			    &ns->ethtool.report_stats_rx);
	debugfs_create_bool("report_stats_tx", 0600, dir,
			    &ns->ethtool.report_stats_tx);
}
