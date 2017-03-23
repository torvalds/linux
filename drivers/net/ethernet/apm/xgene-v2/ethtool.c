/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 *	      Keyur Chudgar <kchudgar@apm.com>
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

#include "main.h"

struct xge_gstrings_stats {
	char name[ETH_GSTRING_LEN];
	int offset;
};

#define XGE_STAT(m)		{ #m, offsetof(struct xge_pdata, stats.m) }

static const struct xge_gstrings_stats gstrings_stats[] = {
	XGE_STAT(rx_packets),
	XGE_STAT(tx_packets),
	XGE_STAT(rx_bytes),
	XGE_STAT(tx_bytes),
	XGE_STAT(rx_errors)
};

#define XGE_STATS_LEN		ARRAY_SIZE(gstrings_stats)

static void xge_get_drvinfo(struct net_device *ndev,
			    struct ethtool_drvinfo *info)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct platform_device *pdev = pdata->pdev;

	strcpy(info->driver, "xgene-enet-v2");
	strcpy(info->version, XGENE_ENET_V2_VERSION);
	snprintf(info->fw_version, ETHTOOL_FWVERS_LEN, "N/A");
	sprintf(info->bus_info, "%s", pdev->name);
}

static void xge_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	u8 *p = data;
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < XGE_STATS_LEN; i++) {
		memcpy(p, gstrings_stats[i].name, ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
	}
}

static int xge_get_sset_count(struct net_device *ndev, int sset)
{
	if (sset != ETH_SS_STATS)
		return -EINVAL;

	return XGE_STATS_LEN;
}

static void xge_get_ethtool_stats(struct net_device *ndev,
				  struct ethtool_stats *dummy,
				  u64 *data)
{
	void *pdata = netdev_priv(ndev);
	int i;

	for (i = 0; i < XGE_STATS_LEN; i++)
		*data++ = *(u64 *)(pdata + gstrings_stats[i].offset);
}

static int xge_get_link_ksettings(struct net_device *ndev,
				  struct ethtool_link_ksettings *cmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_ksettings_get(phydev, cmd);
}

static int xge_set_link_ksettings(struct net_device *ndev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_ksettings_set(phydev, cmd);
}

static const struct ethtool_ops xge_ethtool_ops = {
	.get_drvinfo = xge_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_strings = xge_get_strings,
	.get_sset_count = xge_get_sset_count,
	.get_ethtool_stats = xge_get_ethtool_stats,
	.get_link_ksettings = xge_get_link_ksettings,
	.set_link_ksettings = xge_set_link_ksettings,
};

void xge_set_ethtool_ops(struct net_device *ndev)
{
	ndev->ethtool_ops = &xge_ethtool_ops;
}
