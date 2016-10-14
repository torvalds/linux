/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Authors: Iyappan Subramanian <isubramanian@apm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/ethtool.h>
#include "xgene_enet_main.h"

struct xgene_gstrings_stats {
	char name[ETH_GSTRING_LEN];
	int offset;
};

#define XGENE_STAT(m) { #m, offsetof(struct xgene_enet_pdata, stats.m) }

static const struct xgene_gstrings_stats gstrings_stats[] = {
	XGENE_STAT(rx_packets),
	XGENE_STAT(tx_packets),
	XGENE_STAT(rx_bytes),
	XGENE_STAT(tx_bytes),
	XGENE_STAT(rx_errors),
	XGENE_STAT(tx_errors),
	XGENE_STAT(rx_length_errors),
	XGENE_STAT(rx_crc_errors),
	XGENE_STAT(rx_frame_errors),
	XGENE_STAT(rx_fifo_errors)
};

#define XGENE_STATS_LEN		ARRAY_SIZE(gstrings_stats)

static void xgene_get_drvinfo(struct net_device *ndev,
			      struct ethtool_drvinfo *info)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct platform_device *pdev = pdata->pdev;

	strcpy(info->driver, "xgene_enet");
	strcpy(info->version, XGENE_DRV_VERSION);
	snprintf(info->fw_version, ETHTOOL_FWVERS_LEN, "N/A");
	sprintf(info->bus_info, "%s", pdev->name);
}

static int xgene_get_settings(struct net_device *ndev, struct ethtool_cmd *cmd)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct phy_device *phydev = pdata->phy_dev;

	if (pdata->phy_mode == PHY_INTERFACE_MODE_RGMII) {
		if (phydev == NULL)
			return -ENODEV;

		return phy_ethtool_gset(phydev, cmd);
	} else if (pdata->phy_mode == PHY_INTERFACE_MODE_SGMII) {
		if (pdata->mdio_driver) {
			if (!phydev)
				return -ENODEV;

			return phy_ethtool_gset(phydev, cmd);
		}

		cmd->supported = SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg |
				 SUPPORTED_MII;
		cmd->advertising = cmd->supported;
		ethtool_cmd_speed_set(cmd, SPEED_1000);
		cmd->duplex = DUPLEX_FULL;
		cmd->port = PORT_MII;
		cmd->transceiver = XCVR_INTERNAL;
		cmd->autoneg = AUTONEG_ENABLE;
	} else {
		cmd->supported = SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE;
		cmd->advertising = cmd->supported;
		ethtool_cmd_speed_set(cmd, SPEED_10000);
		cmd->duplex = DUPLEX_FULL;
		cmd->port = PORT_FIBRE;
		cmd->transceiver = XCVR_INTERNAL;
		cmd->autoneg = AUTONEG_DISABLE;
	}

	return 0;
}

static int xgene_set_settings(struct net_device *ndev, struct ethtool_cmd *cmd)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct phy_device *phydev = pdata->phy_dev;

	if (pdata->phy_mode == PHY_INTERFACE_MODE_RGMII) {
		if (!phydev)
			return -ENODEV;

		return phy_ethtool_sset(phydev, cmd);
	}

	if (pdata->phy_mode == PHY_INTERFACE_MODE_SGMII) {
		if (pdata->mdio_driver) {
			if (!phydev)
				return -ENODEV;

			return phy_ethtool_sset(phydev, cmd);
		}
	}

	return -EINVAL;
}

static void xgene_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	int i;
	u8 *p = data;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < XGENE_STATS_LEN; i++) {
		memcpy(p, gstrings_stats[i].name, ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
	}
}

static int xgene_get_sset_count(struct net_device *ndev, int sset)
{
	if (sset != ETH_SS_STATS)
		return -EINVAL;

	return XGENE_STATS_LEN;
}

static void xgene_get_ethtool_stats(struct net_device *ndev,
				    struct ethtool_stats *dummy,
				    u64 *data)
{
	void *pdata = netdev_priv(ndev);
	int i;

	for (i = 0; i < XGENE_STATS_LEN; i++)
		*data++ = *(u64 *)(pdata + gstrings_stats[i].offset);
}

static const struct ethtool_ops xgene_ethtool_ops = {
	.get_drvinfo = xgene_get_drvinfo,
	.get_settings = xgene_get_settings,
	.set_settings = xgene_set_settings,
	.get_link = ethtool_op_get_link,
	.get_strings = xgene_get_strings,
	.get_sset_count = xgene_get_sset_count,
	.get_ethtool_stats = xgene_get_ethtool_stats
};

void xgene_enet_set_ethtool_ops(struct net_device *ndev)
{
	ndev->ethtool_ops = &xgene_ethtool_ops;
}
