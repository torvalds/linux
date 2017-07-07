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
	u32 addr;
	u32 mask;
};

#define XGENE_STAT(m) { #m, offsetof(struct rtnl_link_stats64, m) }
#define XGENE_EXTD_STAT(s, a, m)		\
		{			\
		.name = #s,		\
		.addr = a ## _ADDR,	\
		.mask = m		\
		}

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

static const struct xgene_gstrings_stats gstrings_extd_stats[] = {
	XGENE_EXTD_STAT(tx_rx_64b_frame_cntr, TR64, 31),
	XGENE_EXTD_STAT(tx_rx_127b_frame_cntr, TR127, 31),
	XGENE_EXTD_STAT(tx_rx_255b_frame_cntr, TR255, 31),
	XGENE_EXTD_STAT(tx_rx_511b_frame_cntr, TR511, 31),
	XGENE_EXTD_STAT(tx_rx_1023b_frame_cntr, TR1K, 31),
	XGENE_EXTD_STAT(tx_rx_1518b_frame_cntr, TRMAX, 31),
	XGENE_EXTD_STAT(tx_rx_1522b_frame_cntr, TRMGV, 31),
	XGENE_EXTD_STAT(rx_fcs_error_cntr, RFCS, 16),
	XGENE_EXTD_STAT(rx_multicast_pkt_cntr, RMCA, 31),
	XGENE_EXTD_STAT(rx_broadcast_pkt_cntr, RBCA, 31),
	XGENE_EXTD_STAT(rx_ctrl_frame_pkt_cntr, RXCF, 16),
	XGENE_EXTD_STAT(rx_pause_frame_pkt_cntr, RXPF, 16),
	XGENE_EXTD_STAT(rx_unk_opcode_cntr, RXUO, 16),
	XGENE_EXTD_STAT(rx_align_err_cntr, RALN, 16),
	XGENE_EXTD_STAT(rx_frame_len_err_cntr, RFLR, 16),
	XGENE_EXTD_STAT(rx_frame_len_err_recov_cntr, DUMP, 0),
	XGENE_EXTD_STAT(rx_code_err_cntr, RCDE, 16),
	XGENE_EXTD_STAT(rx_carrier_sense_err_cntr, RCSE, 16),
	XGENE_EXTD_STAT(rx_undersize_pkt_cntr, RUND, 16),
	XGENE_EXTD_STAT(rx_oversize_pkt_cntr, ROVR, 16),
	XGENE_EXTD_STAT(rx_fragments_cntr, RFRG, 16),
	XGENE_EXTD_STAT(rx_jabber_cntr, RJBR, 16),
	XGENE_EXTD_STAT(rx_jabber_recov_cntr, DUMP, 0),
	XGENE_EXTD_STAT(rx_dropped_pkt_cntr, RDRP, 16),
	XGENE_EXTD_STAT(rx_overrun_cntr, DUMP, 0),
	XGENE_EXTD_STAT(tx_multicast_pkt_cntr, TMCA, 31),
	XGENE_EXTD_STAT(tx_broadcast_pkt_cntr, TBCA, 31),
	XGENE_EXTD_STAT(tx_pause_ctrl_frame_cntr, TXPF, 16),
	XGENE_EXTD_STAT(tx_defer_pkt_cntr, TDFR, 31),
	XGENE_EXTD_STAT(tx_excv_defer_pkt_cntr, TEDF, 31),
	XGENE_EXTD_STAT(tx_single_col_pkt_cntr, TSCL, 31),
	XGENE_EXTD_STAT(tx_multi_col_pkt_cntr, TMCL, 31),
	XGENE_EXTD_STAT(tx_late_col_pkt_cntr, TLCL, 31),
	XGENE_EXTD_STAT(tx_excv_col_pkt_cntr, TXCL, 31),
	XGENE_EXTD_STAT(tx_total_col_cntr, TNCL, 31),
	XGENE_EXTD_STAT(tx_pause_frames_hnrd_cntr, TPFH, 16),
	XGENE_EXTD_STAT(tx_drop_frame_cntr, TDRP, 16),
	XGENE_EXTD_STAT(tx_jabber_frame_cntr, TJBR, 12),
	XGENE_EXTD_STAT(tx_fcs_error_cntr, TFCS, 12),
	XGENE_EXTD_STAT(tx_ctrl_frame_cntr, TXCF, 12),
	XGENE_EXTD_STAT(tx_oversize_frame_cntr, TOVR, 12),
	XGENE_EXTD_STAT(tx_undersize_frame_cntr, TUND, 12),
	XGENE_EXTD_STAT(tx_fragments_cntr, TFRG, 12),
	XGENE_EXTD_STAT(tx_underrun_cntr, DUMP, 0)
};

#define XGENE_STATS_LEN		ARRAY_SIZE(gstrings_stats)
#define XGENE_EXTD_STATS_LEN	ARRAY_SIZE(gstrings_extd_stats)
#define RFCS_IDX		7
#define RALN_IDX		13
#define RFLR_IDX		14
#define FALSE_RFLR_IDX		15
#define RUND_IDX		18
#define FALSE_RJBR_IDX		22
#define RX_OVERRUN_IDX		24
#define TFCS_IDX		38
#define TFRG_IDX		42
#define TX_UNDERRUN_IDX		43

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

static int xgene_get_link_ksettings(struct net_device *ndev,
				    struct ethtool_link_ksettings *cmd)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	u32 supported;

	if (phy_interface_mode_is_rgmii(pdata->phy_mode)) {
		if (phydev == NULL)
			return -ENODEV;

		phy_ethtool_ksettings_get(phydev, cmd);

		return 0;
	} else if (pdata->phy_mode == PHY_INTERFACE_MODE_SGMII) {
		if (pdata->mdio_driver) {
			if (!phydev)
				return -ENODEV;

			phy_ethtool_ksettings_get(phydev, cmd);

			return 0;
		}

		supported = SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg |
			SUPPORTED_MII;
		ethtool_convert_legacy_u32_to_link_mode(
			cmd->link_modes.supported,
			supported);
		ethtool_convert_legacy_u32_to_link_mode(
			cmd->link_modes.advertising,
			supported);

		cmd->base.speed = SPEED_1000;
		cmd->base.duplex = DUPLEX_FULL;
		cmd->base.port = PORT_MII;
		cmd->base.autoneg = AUTONEG_ENABLE;
	} else {
		supported = SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE;
		ethtool_convert_legacy_u32_to_link_mode(
			cmd->link_modes.supported,
			supported);
		ethtool_convert_legacy_u32_to_link_mode(
			cmd->link_modes.advertising,
			supported);

		cmd->base.speed = SPEED_10000;
		cmd->base.duplex = DUPLEX_FULL;
		cmd->base.port = PORT_FIBRE;
		cmd->base.autoneg = AUTONEG_DISABLE;
	}

	return 0;
}

static int xgene_set_link_ksettings(struct net_device *ndev,
				    const struct ethtool_link_ksettings *cmd)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;

	if (phy_interface_mode_is_rgmii(pdata->phy_mode)) {
		if (!phydev)
			return -ENODEV;

		return phy_ethtool_ksettings_set(phydev, cmd);
	}

	if (pdata->phy_mode == PHY_INTERFACE_MODE_SGMII) {
		if (pdata->mdio_driver) {
			if (!phydev)
				return -ENODEV;

			return phy_ethtool_ksettings_set(phydev, cmd);
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

	for (i = 0; i < XGENE_EXTD_STATS_LEN; i++) {
		memcpy(p, gstrings_extd_stats[i].name, ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
	}
}

static int xgene_get_sset_count(struct net_device *ndev, int sset)
{
	if (sset != ETH_SS_STATS)
		return -EINVAL;

	return XGENE_STATS_LEN + XGENE_EXTD_STATS_LEN;
}

static void xgene_get_extd_stats(struct xgene_enet_pdata *pdata)
{
	u32 rx_drop, tx_drop;
	u32 mask, tmp;
	int i;

	for (i = 0; i < XGENE_EXTD_STATS_LEN; i++) {
		tmp = xgene_enet_rd_stat(pdata, gstrings_extd_stats[i].addr);
		if (gstrings_extd_stats[i].mask) {
			mask = GENMASK(gstrings_extd_stats[i].mask - 1, 0);
			pdata->extd_stats[i] += (tmp & mask);
		}
	}

	if (pdata->phy_mode == PHY_INTERFACE_MODE_XGMII) {
		/* Errata 10GE_10 - SW should intepret RALN as 0 */
		pdata->extd_stats[RALN_IDX] = 0;
	} else {
		/* Errata ENET_15 - Fixes RFCS, RFLR, TFCS counter */
		pdata->extd_stats[RFCS_IDX] -= pdata->extd_stats[RALN_IDX];
		pdata->extd_stats[RFLR_IDX] -= pdata->extd_stats[RUND_IDX];
		pdata->extd_stats[TFCS_IDX] -= pdata->extd_stats[TFRG_IDX];
	}

	pdata->mac_ops->get_drop_cnt(pdata, &rx_drop, &tx_drop);
	pdata->extd_stats[RX_OVERRUN_IDX] += rx_drop;
	pdata->extd_stats[TX_UNDERRUN_IDX] += tx_drop;

	/* Errata 10GE_8 -  Update Frame recovered from Errata 10GE_8/ENET_11 */
	pdata->extd_stats[FALSE_RFLR_IDX] = pdata->false_rflr;
	/* Errata ENET_15 - Jabber Frame recov'ed from Errata 10GE_10/ENET_15 */
	pdata->extd_stats[FALSE_RJBR_IDX] = pdata->vlan_rjbr;
}

int xgene_extd_stats_init(struct xgene_enet_pdata *pdata)
{
	pdata->extd_stats = devm_kmalloc_array(&pdata->pdev->dev,
			XGENE_EXTD_STATS_LEN, sizeof(u64), GFP_KERNEL);
	if (!pdata->extd_stats)
		return -ENOMEM;

	xgene_get_extd_stats(pdata);
	memset(pdata->extd_stats, 0, XGENE_EXTD_STATS_LEN * sizeof(u64));

	return 0;
}

static void xgene_get_ethtool_stats(struct net_device *ndev,
				    struct ethtool_stats *dummy,
				    u64 *data)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct rtnl_link_stats64 stats;
	int i;

	dev_get_stats(ndev, &stats);
	for (i = 0; i < XGENE_STATS_LEN; i++)
		data[i] = *(u64 *)((char *)&stats + gstrings_stats[i].offset);

	xgene_get_extd_stats(pdata);
	for (i = 0; i < XGENE_EXTD_STATS_LEN; i++)
		data[i + XGENE_STATS_LEN] = pdata->extd_stats[i];
}

static void xgene_get_pauseparam(struct net_device *ndev,
				 struct ethtool_pauseparam *pp)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);

	pp->autoneg = pdata->pause_autoneg;
	pp->tx_pause = pdata->tx_pause;
	pp->rx_pause = pdata->rx_pause;
}

static int xgene_set_pauseparam(struct net_device *ndev,
				struct ethtool_pauseparam *pp)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	u32 oldadv, newadv;

	if (phy_interface_mode_is_rgmii(pdata->phy_mode) ||
	    pdata->phy_mode == PHY_INTERFACE_MODE_SGMII) {
		if (!phydev)
			return -EINVAL;

		if (!(phydev->supported & SUPPORTED_Pause) ||
		    (!(phydev->supported & SUPPORTED_Asym_Pause) &&
		     pp->rx_pause != pp->tx_pause))
			return -EINVAL;

		pdata->pause_autoneg = pp->autoneg;
		pdata->tx_pause = pp->tx_pause;
		pdata->rx_pause = pp->rx_pause;

		oldadv = phydev->advertising;
		newadv = oldadv & ~(ADVERTISED_Pause | ADVERTISED_Asym_Pause);

		if (pp->rx_pause)
			newadv |= ADVERTISED_Pause | ADVERTISED_Asym_Pause;

		if (pp->tx_pause)
			newadv ^= ADVERTISED_Asym_Pause;

		if (oldadv ^ newadv) {
			phydev->advertising = newadv;

			if (phydev->autoneg)
				return phy_start_aneg(phydev);

			if (!pp->autoneg) {
				pdata->mac_ops->flowctl_tx(pdata,
							   pdata->tx_pause);
				pdata->mac_ops->flowctl_rx(pdata,
							   pdata->rx_pause);
			}
		}

	} else {
		if (pp->autoneg)
			return -EINVAL;

		pdata->tx_pause = pp->tx_pause;
		pdata->rx_pause = pp->rx_pause;

		pdata->mac_ops->flowctl_tx(pdata, pdata->tx_pause);
		pdata->mac_ops->flowctl_rx(pdata, pdata->rx_pause);
	}

	return 0;
}

static const struct ethtool_ops xgene_ethtool_ops = {
	.get_drvinfo = xgene_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_strings = xgene_get_strings,
	.get_sset_count = xgene_get_sset_count,
	.get_ethtool_stats = xgene_get_ethtool_stats,
	.get_link_ksettings = xgene_get_link_ksettings,
	.set_link_ksettings = xgene_set_link_ksettings,
	.get_pauseparam = xgene_get_pauseparam,
	.set_pauseparam = xgene_set_pauseparam
};

void xgene_enet_set_ethtool_ops(struct net_device *ndev)
{
	ndev->ethtool_ops = &xgene_ethtool_ops;
}
