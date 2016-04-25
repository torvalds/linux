/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 *****************************************************************************
 */
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/delay.h>

#include "rtl_core.h"

static void _rtl92e_ethtool_get_drvinfo(struct net_device *dev,
					struct ethtool_drvinfo *info)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(priv->pdev), sizeof(info->bus_info));
}

static u32 _rtl92e_ethtool_get_link(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	return ((priv->rtllib->state == RTLLIB_LINKED) ||
		(priv->rtllib->state == RTLLIB_LINKED_SCANNING));
}

const struct ethtool_ops rtl819x_ethtool_ops = {
	.get_drvinfo = _rtl92e_ethtool_get_drvinfo,
	.get_link = _rtl92e_ethtool_get_link,
};
