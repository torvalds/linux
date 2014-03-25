/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/phy.h>

#include "sxgbe_common.h"

struct sxgbe_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define SXGBE_STAT(m)						\
{								\
	#m,							\
	FIELD_SIZEOF(struct sxgbe_extra_stats, m),		\
	offsetof(struct sxgbe_priv_data, xstats.m)		\
}

static const struct sxgbe_stats sxgbe_gstrings_stats[] = {
	SXGBE_STAT(tx_lpi_entry_n),
	SXGBE_STAT(tx_lpi_exit_n),
	SXGBE_STAT(rx_lpi_entry_n),
	SXGBE_STAT(rx_lpi_exit_n),
	SXGBE_STAT(eee_wakeup_error_n),
};
#define SXGBE_STATS_LEN ARRAY_SIZE(sxgbe_gstrings_stats)

static int sxgbe_get_eee(struct net_device *dev,
			 struct ethtool_eee *edata)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);

	if (!priv->hw_cap.eee)
		return -EOPNOTSUPP;

	edata->eee_enabled = priv->eee_enabled;
	edata->eee_active = priv->eee_active;
	edata->tx_lpi_timer = priv->tx_lpi_timer;

	return phy_ethtool_get_eee(priv->phydev, edata);
}

static int sxgbe_set_eee(struct net_device *dev,
			 struct ethtool_eee *edata)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);

	priv->eee_enabled = edata->eee_enabled;

	if (!priv->eee_enabled) {
		sxgbe_disable_eee_mode(priv);
	} else {
		/* We are asking for enabling the EEE but it is safe
		 * to verify all by invoking the eee_init function.
		 * In case of failure it will return an error.
		 */
		priv->eee_enabled = sxgbe_eee_init(priv);
		if (!priv->eee_enabled)
			return -EOPNOTSUPP;

		/* Do not change tx_lpi_timer in case of failure */
		priv->tx_lpi_timer = edata->tx_lpi_timer;
	}

	return phy_ethtool_set_eee(priv->phydev, edata);
}

static const struct ethtool_ops sxgbe_ethtool_ops = {
	.get_eee = sxgbe_get_eee,
	.set_eee = sxgbe_set_eee,
};

void sxgbe_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &sxgbe_ethtool_ops);
}
