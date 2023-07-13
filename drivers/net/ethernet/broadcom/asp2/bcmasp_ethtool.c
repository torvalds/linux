// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt)				"bcmasp_ethtool: " fmt

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>

#include "bcmasp.h"
#include "bcmasp_intf_defs.h"

static void bcmasp_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strscpy(info->driver, "bcmasp", sizeof(info->driver));
	strscpy(info->bus_info, dev_name(dev->dev.parent),
		sizeof(info->bus_info));
}

static u32 bcmasp_get_msglevel(struct net_device *dev)
{
	struct bcmasp_intf *intf = netdev_priv(dev);

	return intf->msg_enable;
}

static void bcmasp_set_msglevel(struct net_device *dev, u32 level)
{
	struct bcmasp_intf *intf = netdev_priv(dev);

	intf->msg_enable = level;
}

#define BCMASP_SUPPORTED_WAKE   (WAKE_MAGIC | WAKE_MAGICSECURE)
static void bcmasp_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bcmasp_intf *intf = netdev_priv(dev);

	wol->supported = BCMASP_SUPPORTED_WAKE;
	wol->wolopts = intf->wolopts;
	memset(wol->sopass, 0, sizeof(wol->sopass));

	if (wol->wolopts & WAKE_MAGICSECURE)
		memcpy(wol->sopass, intf->sopass, sizeof(intf->sopass));
}

static int bcmasp_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bcmasp_intf *intf = netdev_priv(dev);
	struct bcmasp_priv *priv = intf->parent;
	struct device *kdev = &priv->pdev->dev;

	if (!device_can_wakeup(kdev))
		return -EOPNOTSUPP;

	/* Interface Specific */
	intf->wolopts = wol->wolopts;
	if (intf->wolopts & WAKE_MAGICSECURE)
		memcpy(intf->sopass, wol->sopass, sizeof(wol->sopass));

	mutex_lock(&priv->wol_lock);
	priv->enable_wol(intf, !!intf->wolopts);
	mutex_unlock(&priv->wol_lock);

	return 0;
}

const struct ethtool_ops bcmasp_ethtool_ops = {
	.get_drvinfo		= bcmasp_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.get_msglevel		= bcmasp_get_msglevel,
	.set_msglevel		= bcmasp_set_msglevel,
	.get_wol		= bcmasp_get_wol,
	.set_wol		= bcmasp_set_wol,
};
