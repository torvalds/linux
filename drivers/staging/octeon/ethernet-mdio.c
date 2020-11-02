// SPDX-License-Identifier: GPL-2.0
/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2007 Cavium Networks
 */

#include <linux/kernel.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/ratelimit.h>
#include <linux/of_mdio.h>
#include <generated/utsrelease.h>
#include <net/dst.h>

#include "octeon-ethernet.h"
#include "ethernet-defines.h"
#include "ethernet-mdio.h"
#include "ethernet-util.h"

static void cvm_oct_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->version, UTS_RELEASE, sizeof(info->version));
	strlcpy(info->bus_info, "Builtin", sizeof(info->bus_info));
}

static int cvm_oct_nway_reset(struct net_device *dev)
{
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (dev->phydev)
		return phy_start_aneg(dev->phydev);

	return -EINVAL;
}

const struct ethtool_ops cvm_oct_ethtool_ops = {
	.get_drvinfo = cvm_oct_get_drvinfo,
	.nway_reset = cvm_oct_nway_reset,
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

/**
 * cvm_oct_ioctl - IOCTL support for PHY control
 * @dev:    Device to change
 * @rq:     the request
 * @cmd:    the command
 *
 * Returns Zero on success
 */
int cvm_oct_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	if (!netif_running(dev))
		return -EINVAL;

	if (!dev->phydev)
		return -EINVAL;

	return phy_mii_ioctl(dev->phydev, rq, cmd);
}

void cvm_oct_note_carrier(struct octeon_ethernet *priv,
			  union cvmx_helper_link_info li)
{
	if (li.s.link_up) {
		pr_notice_ratelimited("%s: %u Mbps %s duplex, port %d, queue %d\n",
				      netdev_name(priv->netdev), li.s.speed,
				      (li.s.full_duplex) ? "Full" : "Half",
				      priv->port, priv->queue);
	} else {
		pr_notice_ratelimited("%s: Link down\n",
				      netdev_name(priv->netdev));
	}
}

void cvm_oct_adjust_link(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	union cvmx_helper_link_info link_info;

	link_info.u64		= 0;
	link_info.s.link_up	= dev->phydev->link ? 1 : 0;
	link_info.s.full_duplex = dev->phydev->duplex ? 1 : 0;
	link_info.s.speed	= dev->phydev->speed;
	priv->link_info		= link_info.u64;

	/*
	 * The polling task need to know about link status changes.
	 */
	if (priv->poll)
		priv->poll(dev);

	if (priv->last_link != dev->phydev->link) {
		priv->last_link = dev->phydev->link;
		cvmx_helper_link_set(priv->port, link_info);
		cvm_oct_note_carrier(priv, link_info);
	}
}

int cvm_oct_common_stop(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	int interface = INTERFACE(priv->port);
	union cvmx_helper_link_info link_info;
	union cvmx_gmxx_prtx_cfg gmx_cfg;
	int index = INDEX(priv->port);

	gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
	gmx_cfg.s.en = 0;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmx_cfg.u64);

	priv->poll = NULL;

	if (dev->phydev)
		phy_disconnect(dev->phydev);

	if (priv->last_link) {
		link_info.u64 = 0;
		priv->last_link = 0;

		cvmx_helper_link_set(priv->port, link_info);
		cvm_oct_note_carrier(priv, link_info);
	}
	return 0;
}

/**
 * cvm_oct_phy_setup_device - setup the PHY
 *
 * @dev:    Device to setup
 *
 * Returns Zero on success, negative on failure
 */
int cvm_oct_phy_setup_device(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	struct device_node *phy_node;
	struct phy_device *phydev = NULL;

	if (!priv->of_node)
		goto no_phy;

	phy_node = of_parse_phandle(priv->of_node, "phy-handle", 0);
	if (!phy_node && of_phy_is_fixed_link(priv->of_node)) {
		phy_node = of_node_get(priv->of_node);
	}
	if (!phy_node)
		goto no_phy;

	phydev = of_phy_connect(dev, phy_node, cvm_oct_adjust_link, 0,
				priv->phy_mode);
	of_node_put(phy_node);

	if (!phydev)
		return -EPROBE_DEFER;

	priv->last_link = 0;
	phy_start(phydev);

	return 0;
no_phy:
	/* If there is no phy, assume a direct MAC connection and that
	 * the link is up.
	 */
	netif_carrier_on(dev);
	return 0;
}
