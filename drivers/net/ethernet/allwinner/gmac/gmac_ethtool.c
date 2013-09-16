/*******************************************************************************
  Copyright (C) 2012 Shuge

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".
*******************************************************************************/

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <asm/io.h>

#include "sunxi_gmac.h"
#include "gmac_ethtool.h"

#define REG_SPACE_SIZE		0x1054
#define GMAC_ETHTOOL_NAME	"sunxi_gmac"

struct gmac_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define GMAC_STAT(m)	\
	{ #m, FIELD_SIZEOF(struct gmac_extra_stats, m),	\
	offsetof(struct gmac_priv, xstats.m)}

static const struct gmac_stats gmac_gstrings_stats[] = {
	/* Transmit errors */
	GMAC_STAT(tx_underflow),
	GMAC_STAT(tx_carrier),
	GMAC_STAT(tx_losscarrier),
	GMAC_STAT(vlan_tag),
	GMAC_STAT(tx_deferred),
	GMAC_STAT(tx_vlan),
	GMAC_STAT(tx_jabber),
	GMAC_STAT(tx_frame_flushed),
	GMAC_STAT(tx_payload_error),
	GMAC_STAT(tx_ip_header_error),
	/* Receive errors */
	GMAC_STAT(rx_desc),
	GMAC_STAT(sa_filter_fail),
	GMAC_STAT(overflow_error),
	GMAC_STAT(ipc_csum_error),
	GMAC_STAT(rx_collision),
	GMAC_STAT(rx_crc),
	GMAC_STAT(dribbling_bit),
	GMAC_STAT(rx_length),
	GMAC_STAT(rx_mii),
	GMAC_STAT(rx_multicast),
	GMAC_STAT(rx_gmac_overflow),
	GMAC_STAT(rx_watchdog),
	GMAC_STAT(da_rx_filter_fail),
	GMAC_STAT(sa_rx_filter_fail),
	GMAC_STAT(rx_missed_cntr),
	GMAC_STAT(rx_overflow_cntr),
	GMAC_STAT(rx_vlan),
	/* Tx/Rx IRQ errors */
	GMAC_STAT(tx_undeflow_irq),
	GMAC_STAT(tx_process_stopped_irq),
	GMAC_STAT(tx_jabber_irq),
	GMAC_STAT(rx_overflow_irq),
	GMAC_STAT(rx_buf_unav_irq),
	GMAC_STAT(rx_process_stopped_irq),
	GMAC_STAT(rx_watchdog_irq),
	GMAC_STAT(tx_early_irq),
	GMAC_STAT(fatal_bus_error_irq),
	/* Extra info */
	GMAC_STAT(threshold),
	GMAC_STAT(tx_pkt_n),
	GMAC_STAT(rx_pkt_n),
	GMAC_STAT(poll_n),
	GMAC_STAT(sched_timer_n),
	GMAC_STAT(normal_irq_n),
};
#define GMAC_STATS_LEN ARRAY_SIZE(gmac_gstrings_stats)

static void gmac_ethtool_getdrvinfo(struct net_device *ndev,
				      struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, GMAC_ETHTOOL_NAME, sizeof(info->driver));

#define DRV_MODULE_VERSION "SUNXI GMAC driver V0.1"

	strcpy(info->version, DRV_MODULE_VERSION);
	info->fw_version[0] = '\0';
}

static int gmac_ethtool_getsettings(struct net_device *ndev,
				      struct ethtool_cmd *cmd)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	struct phy_device *phy = ndev->phydev;
	int rc;
	if (phy == NULL) {
		pr_err("%s: %s: PHY is not registered\n",
		       __func__, ndev->name);
		return -ENODEV;
	}
	if (!netif_running(ndev)) {
		pr_err("%s: interface is disabled: we cannot track "
		"link speed / duplex setting\n", ndev->name);
		return -EBUSY;
	}
	cmd->transceiver = XCVR_INTERNAL;
	spin_lock_irq(&priv->lock);
	rc = phy_ethtool_gset(phy, cmd);
	spin_unlock_irq(&priv->lock);
	return rc;
}

static int gmac_ethtool_setsettings(struct net_device *ndev,
				      struct ethtool_cmd *cmd)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	struct phy_device *phy = ndev->phydev;
	int rc;

	spin_lock(&priv->lock);
	rc = phy_ethtool_sset(phy, cmd);
	spin_unlock(&priv->lock);

	return rc;
}

static u32 gmac_ethtool_getmsglevel(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	return priv->msg_enable;
}

static void gmac_ethtool_setmsglevel(struct net_device *ndev, u32 level)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	priv->msg_enable = level;

}

static int gmac_check_if_running(struct net_device *ndev)
{
	if (!netif_running(ndev))
		return -EBUSY;
	return 0;
}

static int gmac_ethtool_get_regs_len(struct net_device *ndev)
{
	return REG_SPACE_SIZE;
}

static void gmac_ethtool_gregs(struct net_device *ndev,
			  struct ethtool_regs *regs, void *space)
{
	int i;
	u32 *reg_space = (u32 *) space;

	struct gmac_priv *priv = netdev_priv(ndev);

	memset(reg_space, 0x0, REG_SPACE_SIZE);

	/* MAC registers */
	for (i = 0; i < 55; i++)
		reg_space[i] = readl(priv->ioaddr + (i * 4));
	/* DMA registers */
	for (i = 0; i < 22; i++)
		reg_space[i + 55] =
			readl(priv->ioaddr + (GDMA_BUS_MODE + (i * 4)));
}

static void
gmac_get_pauseparam(struct net_device *netdev,
		      struct ethtool_pauseparam *pause)
{
	struct gmac_priv *priv = netdev_priv(netdev);

	spin_lock(&priv->lock);

	pause->rx_pause = 0;
	pause->tx_pause = 0;
	pause->autoneg = netdev->phydev->autoneg;

	if (priv->flow_ctrl & FLOW_RX)
		pause->rx_pause = 1;
	if (priv->flow_ctrl & FLOW_TX)
		pause->tx_pause = 1;

	spin_unlock(&priv->lock);
}

static int
gmac_set_pauseparam(struct net_device *netdev,
		      struct ethtool_pauseparam *pause)
{
	struct gmac_priv *priv = netdev_priv(netdev);
	struct phy_device *phy = netdev->phydev;
	int new_pause = FLOW_OFF;
	int ret = 0;

	spin_lock(&priv->lock);

	if (pause->rx_pause)
		new_pause |= FLOW_RX;
	if (pause->tx_pause)
		new_pause |= FLOW_TX;

	priv->flow_ctrl = new_pause;
	phy->autoneg = pause->autoneg;

	if (phy->autoneg) {
		if (netif_running(netdev))
			ret = phy_start_aneg(phy);
	} else
		core_flow_ctrl(priv->ioaddr, phy->duplex,
					 priv->flow_ctrl, priv->pause);

	spin_unlock(&priv->lock);

	return ret;
}

static void gmac_get_ethtool_stats(struct net_device *ndev,
				 struct ethtool_stats *dummy, u64 *data)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	int i, j = 0;

	for (i = 0; i < GMAC_STATS_LEN; i++) {
		char *p = (char *)priv + gmac_gstrings_stats[i].stat_offset;
		data[j++] = (gmac_gstrings_stats[i].sizeof_stat ==
			     sizeof(u64)) ? (*(u64 *)p) : (*(u32 *)p);
	}
}

static int gmac_get_sset_count(struct net_device *netdev, int sset)
{
	int len;

	switch (sset) {
	case ETH_SS_STATS:
		len = GMAC_STATS_LEN;
		return len;
	default:
		return -EOPNOTSUPP;
	}
}

static void gmac_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	int i;
	u8 *p = data;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < GMAC_STATS_LEN; i++) {
			memcpy(p, gmac_gstrings_stats[i].stat_string,
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static const struct ethtool_ops gmac_ethtool_ops = {
	.begin = gmac_check_if_running,
	.get_drvinfo = gmac_ethtool_getdrvinfo,
	.get_settings = gmac_ethtool_getsettings,
	.set_settings = gmac_ethtool_setsettings,
	.get_msglevel = gmac_ethtool_getmsglevel,
	.set_msglevel = gmac_ethtool_setmsglevel,
	.get_regs = gmac_ethtool_gregs,
	.get_regs_len = gmac_ethtool_get_regs_len,
	.get_link = ethtool_op_get_link,
	.get_pauseparam = gmac_get_pauseparam,
	.set_pauseparam = gmac_set_pauseparam,
	.get_ethtool_stats = gmac_get_ethtool_stats,
	.get_strings = gmac_get_strings,
	//.get_wol = gmac_get_wol,
	//.set_wol = gmac_set_wol,
	.get_sset_count	= gmac_get_sset_count,
};

void gmac_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &gmac_ethtool_ops;
}
