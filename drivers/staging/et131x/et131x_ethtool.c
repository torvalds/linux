/*
 *  Copyright (C) 2011 Mark Einon
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Mark Einon <mark.einon@gmail.com>
 */
#include "et131x_version.h"
#include "et131x_defs.h"

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/pci.h>

#include "et131x_adapter.h"
#include "et131x.h"

static int et131x_get_settings(struct net_device *netdev,
			       struct ethtool_cmd *cmd)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	return phy_ethtool_gset(adapter->phydev, cmd);
}

static int et131x_set_settings(struct net_device *netdev,
			       struct ethtool_cmd *cmd)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	return phy_ethtool_sset(adapter->phydev, cmd);
}

#define ET131X_DRVINFO_LEN 32 /* value from ethtool.h */
static void et131x_get_drvinfo(struct net_device *netdev,
			       struct ethtool_drvinfo *info)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	strncpy(info->driver, DRIVER_NAME, ET131X_DRVINFO_LEN);
	strncpy(info->version, DRIVER_VERSION_STRING, ET131X_DRVINFO_LEN);
	strncpy(info->bus_info, pci_name(adapter->pdev), ET131X_DRVINFO_LEN);
}

static struct ethtool_ops et131x_ethtool_ops = {
        .get_settings = et131x_get_settings,
        .set_settings = et131x_set_settings,
        .get_drvinfo = et131x_get_drvinfo,
        .get_link = ethtool_op_get_link,
};

void et131x_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &et131x_ethtool_ops);
}

