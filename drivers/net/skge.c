/*
 * New driver for Marvell Yukon chipset and SysKonnect Gigabit
 * Ethernet adapters. Based on earlier sk98lin, e100 and
 * FreeBSD if_sk drivers.
 *
 * This driver intentionally does not support all the features
 * of the original driver such as link fail-over and link management because
 * those should be done at higher levels.
 *
 * Copyright (C) 2004, Stephen Hemminger <shemminger@osdl.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/dma-mapping.h>
#include <asm/irq.h>

#include "skge.h"

#define DRV_NAME		"skge"
#define DRV_VERSION		"0.6"
#define PFX			DRV_NAME " "

#define DEFAULT_TX_RING_SIZE	128
#define DEFAULT_RX_RING_SIZE	512
#define MAX_TX_RING_SIZE	1024
#define MAX_RX_RING_SIZE	4096
#define PHY_RETRIES	        1000
#define ETH_JUMBO_MTU		9000
#define TX_WATCHDOG		(5 * HZ)
#define NAPI_WEIGHT		64
#define BLINK_HZ		(HZ/4)
#define LINK_POLL_HZ		(HZ/10)

MODULE_DESCRIPTION("SysKonnect Gigabit Ethernet driver");
MODULE_AUTHOR("Stephen Hemminger <shemminger@osdl.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static const u32 default_msg
	= NETIF_MSG_DRV| NETIF_MSG_PROBE| NETIF_MSG_LINK
	  | NETIF_MSG_IFUP| NETIF_MSG_IFDOWN;

static int debug = -1;	/* defaults above */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

static const struct pci_device_id skge_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_3COM, PCI_DEVICE_ID_3COM_3C940) },
	{ PCI_DEVICE(PCI_VENDOR_ID_3COM, PCI_DEVICE_ID_3COM_3C940B) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, PCI_DEVICE_ID_SYSKONNECT_GE) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, PCI_DEVICE_ID_SYSKONNECT_YU) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, 0x9E00) }, /* SK-9Exx  */
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK, PCI_DEVICE_ID_DLINK_DGE510T), },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4320) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x5005) }, /* Belkin */
	{ PCI_DEVICE(PCI_VENDOR_ID_CNET, PCI_DEVICE_ID_CNET_GIGACARD) },
	{ PCI_DEVICE(PCI_VENDOR_ID_LINKSYS, PCI_DEVICE_ID_LINKSYS_EG1032) },
	{ PCI_DEVICE(PCI_VENDOR_ID_LINKSYS, PCI_DEVICE_ID_LINKSYS_EG1064) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, skge_id_table);

static int skge_up(struct net_device *dev);
static int skge_down(struct net_device *dev);
static void skge_tx_clean(struct skge_port *skge);
static void skge_xm_phy_write(struct skge_hw *hw, int port, u16 reg, u16 val);
static void skge_gm_phy_write(struct skge_hw *hw, int port, u16 reg, u16 val);
static void genesis_get_stats(struct skge_port *skge, u64 *data);
static void yukon_get_stats(struct skge_port *skge, u64 *data);
static void yukon_init(struct skge_hw *hw, int port);
static void yukon_reset(struct skge_hw *hw, int port);
static void genesis_mac_init(struct skge_hw *hw, int port);
static void genesis_reset(struct skge_hw *hw, int port);

static const int txqaddr[] = { Q_XA1, Q_XA2 };
static const int rxqaddr[] = { Q_R1, Q_R2 };
static const u32 rxirqmask[] = { IS_R1_F, IS_R2_F };
static const u32 txirqmask[] = { IS_XA1_F, IS_XA2_F };

/* Don't need to look at whole 16K.
 * last interesting register is descriptor poll timer.
 */
#define SKGE_REGS_LEN	(29*128)

static int skge_get_regs_len(struct net_device *dev)
{
	return SKGE_REGS_LEN;
}

/*
 * Returns copy of control register region
 * I/O region is divided into banks and certain regions are unreadable
 */
static void skge_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			  void *p)
{
	const struct skge_port *skge = netdev_priv(dev);
	unsigned long offs;
	const void __iomem *io = skge->hw->regs;
	static const unsigned long bankmap
		= (1<<0) | (1<<2) | (1<<8) | (1<<9)
		  | (1<<12) | (1<<13) | (1<<14) | (1<<15) | (1<<16)
		  | (1<<17) | (1<<20) | (1<<21) | (1<<22) | (1<<23)
		  | (1<<24)  | (1<<25) | (1<<26) | (1<<27) | (1<<28);

	regs->version = 1;
	for (offs = 0; offs < regs->len; offs += 128) {
		u32 len = min_t(u32, 128, regs->len - offs);

		if (bankmap & (1<<(offs/128)))
			memcpy_fromio(p + offs, io + offs, len);
		else
			memset(p + offs, 0, len);
	}
}

/* Wake on Lan only supported on Yukon chps with rev 1 or above */
static int wol_supported(const struct skge_hw *hw)
{
	return !((hw->chip_id == CHIP_ID_GENESIS ||
		  (hw->chip_id == CHIP_ID_YUKON && chip_rev(hw) == 0)));
}

static void skge_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct skge_port *skge = netdev_priv(dev);

	wol->supported = wol_supported(skge->hw) ? WAKE_MAGIC : 0;
	wol->wolopts = skge->wol ? WAKE_MAGIC : 0;
}

static int skge_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;

	if (wol->wolopts != WAKE_MAGIC && wol->wolopts != 0)
		return -EOPNOTSUPP;

	if (wol->wolopts == WAKE_MAGIC && !wol_supported(hw))
		return -EOPNOTSUPP;

	skge->wol = wol->wolopts == WAKE_MAGIC;

	if (skge->wol) {
		memcpy_toio(hw->regs + WOL_MAC_ADDR, dev->dev_addr, ETH_ALEN);

		skge_write16(hw, WOL_CTRL_STAT,
			     WOL_CTL_ENA_PME_ON_MAGIC_PKT |
			     WOL_CTL_ENA_MAGIC_PKT_UNIT);
	} else
		skge_write16(hw, WOL_CTRL_STAT, WOL_CTL_DEFAULT);

	return 0;
}


static int skge_get_settings(struct net_device *dev,
			     struct ethtool_cmd *ecmd)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;

	ecmd->transceiver = XCVR_INTERNAL;

	if (iscopper(hw)) {
		if (hw->chip_id == CHIP_ID_GENESIS)
			ecmd->supported = SUPPORTED_1000baseT_Full
				| SUPPORTED_1000baseT_Half
				| SUPPORTED_Autoneg | SUPPORTED_TP;
		else {
			ecmd->supported = SUPPORTED_10baseT_Half
				| SUPPORTED_10baseT_Full
				| SUPPORTED_100baseT_Half
				| SUPPORTED_100baseT_Full
				| SUPPORTED_1000baseT_Half
				| SUPPORTED_1000baseT_Full
				| SUPPORTED_Autoneg| SUPPORTED_TP;

			if (hw->chip_id == CHIP_ID_YUKON)
				ecmd->supported &= ~SUPPORTED_1000baseT_Half;

			else if (hw->chip_id == CHIP_ID_YUKON_FE)
				ecmd->supported &= ~(SUPPORTED_1000baseT_Half
						     | SUPPORTED_1000baseT_Full);
		}

		ecmd->port = PORT_TP;
		ecmd->phy_address = hw->phy_addr;
	} else {
		ecmd->supported = SUPPORTED_1000baseT_Full
			| SUPPORTED_FIBRE
			| SUPPORTED_Autoneg;

		ecmd->port = PORT_FIBRE;
	}

	ecmd->advertising = skge->advertising;
	ecmd->autoneg = skge->autoneg;
	ecmd->speed = skge->speed;
	ecmd->duplex = skge->duplex;
	return 0;
}

static u32 skge_modes(const struct skge_hw *hw)
{
	u32 modes = ADVERTISED_Autoneg
		| ADVERTISED_1000baseT_Full | ADVERTISED_1000baseT_Half
		| ADVERTISED_100baseT_Full | ADVERTISED_100baseT_Half
		| ADVERTISED_10baseT_Full | ADVERTISED_10baseT_Half;

	if (iscopper(hw)) {
		modes |= ADVERTISED_TP;
		switch (hw->chip_id) {
		case CHIP_ID_GENESIS:
			modes &= ~(ADVERTISED_100baseT_Full
				   | ADVERTISED_100baseT_Half
				   | ADVERTISED_10baseT_Full
				   | ADVERTISED_10baseT_Half);
			break;

		case CHIP_ID_YUKON:
			modes &= ~ADVERTISED_1000baseT_Half;
			break;

		case CHIP_ID_YUKON_FE:
			modes &= ~(ADVERTISED_1000baseT_Half|ADVERTISED_1000baseT_Full);
			break;
		}
	} else {
		modes |= ADVERTISED_FIBRE;
		modes &= ~ADVERTISED_1000baseT_Half;
	}
	return modes;
}

static int skge_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct skge_port *skge = netdev_priv(dev);
	const struct skge_hw *hw = skge->hw;

	if (ecmd->autoneg == AUTONEG_ENABLE) {
		if (ecmd->advertising & skge_modes(hw))
			return -EINVAL;
	} else {
		switch (ecmd->speed) {
		case SPEED_1000:
			if (hw->chip_id == CHIP_ID_YUKON_FE)
				return -EINVAL;
			break;
		case SPEED_100:
		case SPEED_10:
			if (iscopper(hw) || hw->chip_id == CHIP_ID_GENESIS)
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
	}

	skge->autoneg = ecmd->autoneg;
	skge->speed = ecmd->speed;
	skge->duplex = ecmd->duplex;
	skge->advertising = ecmd->advertising;

	if (netif_running(dev)) {
		skge_down(dev);
		skge_up(dev);
	}
	return (0);
}

static void skge_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	struct skge_port *skge = netdev_priv(dev);

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->fw_version, "N/A");
	strcpy(info->bus_info, pci_name(skge->hw->pdev));
}

static const struct skge_stat {
	char 	   name[ETH_GSTRING_LEN];
	u16	   xmac_offset;
	u16	   gma_offset;
} skge_stats[] = {
	{ "tx_bytes",		XM_TXO_OK_HI,  GM_TXO_OK_HI },
	{ "rx_bytes",		XM_RXO_OK_HI,  GM_RXO_OK_HI },

	{ "tx_broadcast",	XM_TXF_BC_OK,  GM_TXF_BC_OK },
	{ "rx_broadcast",	XM_RXF_BC_OK,  GM_RXF_BC_OK },
	{ "tx_multicast",	XM_TXF_MC_OK,  GM_TXF_MC_OK },
	{ "rx_multicast",	XM_RXF_MC_OK,  GM_RXF_MC_OK },
	{ "tx_unicast",		XM_TXF_UC_OK,  GM_TXF_UC_OK },
	{ "rx_unicast",		XM_RXF_UC_OK,  GM_RXF_UC_OK },
	{ "tx_mac_pause",	XM_TXF_MPAUSE, GM_TXF_MPAUSE },
	{ "rx_mac_pause",	XM_RXF_MPAUSE, GM_RXF_MPAUSE },

	{ "collisions",		XM_TXF_SNG_COL, GM_TXF_SNG_COL },
	{ "multi_collisions",	XM_TXF_MUL_COL, GM_TXF_MUL_COL },
	{ "aborted",		XM_TXF_ABO_COL, GM_TXF_ABO_COL },
	{ "late_collision",	XM_TXF_LAT_COL, GM_TXF_LAT_COL },
	{ "fifo_underrun",	XM_TXE_FIFO_UR, GM_TXE_FIFO_UR },
	{ "fifo_overflow",	XM_RXE_FIFO_OV, GM_RXE_FIFO_OV },

	{ "rx_toolong",		XM_RXF_LNG_ERR, GM_RXF_LNG_ERR },
	{ "rx_jabber",		XM_RXF_JAB_PKT, GM_RXF_JAB_PKT },
	{ "rx_runt",		XM_RXE_RUNT, 	GM_RXE_FRAG },
	{ "rx_too_long",	XM_RXF_LNG_ERR, GM_RXF_LNG_ERR },
	{ "rx_fcs_error",	XM_RXF_FCS_ERR, GM_RXF_FCS_ERR },
};

static int skge_get_stats_count(struct net_device *dev)
{
	return ARRAY_SIZE(skge_stats);
}

static void skge_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct skge_port *skge = netdev_priv(dev);

	if (skge->hw->chip_id == CHIP_ID_GENESIS)
		genesis_get_stats(skge, data);
	else
		yukon_get_stats(skge, data);
}

/* Use hardware MIB variables for critical path statistics and
 * transmit feedback not reported at interrupt.
 * Other errors are accounted for in interrupt handler.
 */
static struct net_device_stats *skge_get_stats(struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);
	u64 data[ARRAY_SIZE(skge_stats)];

	if (skge->hw->chip_id == CHIP_ID_GENESIS)
		genesis_get_stats(skge, data);
	else
		yukon_get_stats(skge, data);

	skge->net_stats.tx_bytes = data[0];
	skge->net_stats.rx_bytes = data[1];
	skge->net_stats.tx_packets = data[2] + data[4] + data[6];
	skge->net_stats.rx_packets = data[3] + data[5] + data[7];
	skge->net_stats.multicast = data[5] + data[7];
	skge->net_stats.collisions = data[10];
	skge->net_stats.tx_aborted_errors = data[12];

	return &skge->net_stats;
}

static void skge_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(skge_stats); i++)
			memcpy(data + i * ETH_GSTRING_LEN,
			       skge_stats[i].name, ETH_GSTRING_LEN);
		break;
	}
}

static void skge_get_ring_param(struct net_device *dev,
				struct ethtool_ringparam *p)
{
	struct skge_port *skge = netdev_priv(dev);

	p->rx_max_pending = MAX_RX_RING_SIZE;
	p->tx_max_pending = MAX_TX_RING_SIZE;
	p->rx_mini_max_pending = 0;
	p->rx_jumbo_max_pending = 0;

	p->rx_pending = skge->rx_ring.count;
	p->tx_pending = skge->tx_ring.count;
	p->rx_mini_pending = 0;
	p->rx_jumbo_pending = 0;
}

static int skge_set_ring_param(struct net_device *dev,
			       struct ethtool_ringparam *p)
{
	struct skge_port *skge = netdev_priv(dev);

	if (p->rx_pending == 0 || p->rx_pending > MAX_RX_RING_SIZE ||
	    p->tx_pending == 0 || p->tx_pending > MAX_TX_RING_SIZE)
		return -EINVAL;

	skge->rx_ring.count = p->rx_pending;
	skge->tx_ring.count = p->tx_pending;

	if (netif_running(dev)) {
		skge_down(dev);
		skge_up(dev);
	}

	return 0;
}

static u32 skge_get_msglevel(struct net_device *netdev)
{
	struct skge_port *skge = netdev_priv(netdev);
	return skge->msg_enable;
}

static void skge_set_msglevel(struct net_device *netdev, u32 value)
{
	struct skge_port *skge = netdev_priv(netdev);
	skge->msg_enable = value;
}

static int skge_nway_reset(struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;
	int port = skge->port;

	if (skge->autoneg != AUTONEG_ENABLE || !netif_running(dev))
		return -EINVAL;

	spin_lock_bh(&hw->phy_lock);
	if (hw->chip_id == CHIP_ID_GENESIS) {
		genesis_reset(hw, port);
		genesis_mac_init(hw, port);
	} else {
		yukon_reset(hw, port);
		yukon_init(hw, port);
	}
	spin_unlock_bh(&hw->phy_lock);
	return 0;
}

static int skge_set_sg(struct net_device *dev, u32 data)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;

	if (hw->chip_id == CHIP_ID_GENESIS && data)
		return -EOPNOTSUPP;
	return ethtool_op_set_sg(dev, data);
}

static int skge_set_tx_csum(struct net_device *dev, u32 data)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;

	if (hw->chip_id == CHIP_ID_GENESIS && data)
		return -EOPNOTSUPP;

	return ethtool_op_set_tx_csum(dev, data);
}

static u32 skge_get_rx_csum(struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);

	return skge->rx_csum;
}

/* Only Yukon supports checksum offload. */
static int skge_set_rx_csum(struct net_device *dev, u32 data)
{
	struct skge_port *skge = netdev_priv(dev);

	if (skge->hw->chip_id == CHIP_ID_GENESIS && data)
		return -EOPNOTSUPP;

	skge->rx_csum = data;
	return 0;
}

/* Only Yukon II supports TSO (not implemented yet) */
static int skge_set_tso(struct net_device *dev, u32 data)
{
	if (data)
		return -EOPNOTSUPP;
	return 0;
}

static void skge_get_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *ecmd)
{
	struct skge_port *skge = netdev_priv(dev);

	ecmd->tx_pause = (skge->flow_control == FLOW_MODE_LOC_SEND)
		|| (skge->flow_control == FLOW_MODE_SYMMETRIC);
	ecmd->rx_pause = (skge->flow_control == FLOW_MODE_REM_SEND)
		|| (skge->flow_control == FLOW_MODE_SYMMETRIC);

	ecmd->autoneg = skge->autoneg;
}

static int skge_set_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *ecmd)
{
	struct skge_port *skge = netdev_priv(dev);

	skge->autoneg = ecmd->autoneg;
	if (ecmd->rx_pause && ecmd->tx_pause)
		skge->flow_control = FLOW_MODE_SYMMETRIC;
	else if (ecmd->rx_pause && !ecmd->tx_pause)
		skge->flow_control = FLOW_MODE_REM_SEND;
	else if (!ecmd->rx_pause && ecmd->tx_pause)
		skge->flow_control = FLOW_MODE_LOC_SEND;
	else
		skge->flow_control = FLOW_MODE_NONE;

	if (netif_running(dev)) {
		skge_down(dev);
		skge_up(dev);
	}
	return 0;
}

/* Chip internal frequency for clock calculations */
static inline u32 hwkhz(const struct skge_hw *hw)
{
	if (hw->chip_id == CHIP_ID_GENESIS)
		return 53215; /* or:  53.125 MHz */
	else if (hw->chip_id == CHIP_ID_YUKON_EC)
		return 125000; /* or: 125.000 MHz */
	else
		return 78215; /* or:  78.125 MHz */
}

/* Chip hz to microseconds */
static inline u32 skge_clk2usec(const struct skge_hw *hw, u32 ticks)
{
	return (ticks * 1000) / hwkhz(hw);
}

/* Microseconds to chip hz */
static inline u32 skge_usecs2clk(const struct skge_hw *hw, u32 usec)
{
	return hwkhz(hw) * usec / 1000;
}

static int skge_get_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *ecmd)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;
	int port = skge->port;

	ecmd->rx_coalesce_usecs = 0;
	ecmd->tx_coalesce_usecs = 0;

	if (skge_read32(hw, B2_IRQM_CTRL) & TIM_START) {
		u32 delay = skge_clk2usec(hw, skge_read32(hw, B2_IRQM_INI));
		u32 msk = skge_read32(hw, B2_IRQM_MSK);

		if (msk & rxirqmask[port])
			ecmd->rx_coalesce_usecs = delay;
		if (msk & txirqmask[port])
			ecmd->tx_coalesce_usecs = delay;
	}

	return 0;
}

/* Note: interrupt timer is per board, but can turn on/off per port */
static int skge_set_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *ecmd)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;
	int port = skge->port;
	u32 msk = skge_read32(hw, B2_IRQM_MSK);
	u32 delay = 25;

	if (ecmd->rx_coalesce_usecs == 0)
		msk &= ~rxirqmask[port];
	else if (ecmd->rx_coalesce_usecs < 25 ||
		 ecmd->rx_coalesce_usecs > 33333)
		return -EINVAL;
	else {
		msk |= rxirqmask[port];
		delay = ecmd->rx_coalesce_usecs;
	}

	if (ecmd->tx_coalesce_usecs == 0)
		msk &= ~txirqmask[port];
	else if (ecmd->tx_coalesce_usecs < 25 ||
		 ecmd->tx_coalesce_usecs > 33333)
		return -EINVAL;
	else {
		msk |= txirqmask[port];
		delay = min(delay, ecmd->rx_coalesce_usecs);
	}

	skge_write32(hw, B2_IRQM_MSK, msk);
	if (msk == 0)
		skge_write32(hw, B2_IRQM_CTRL, TIM_STOP);
	else {
		skge_write32(hw, B2_IRQM_INI, skge_usecs2clk(hw, delay));
		skge_write32(hw, B2_IRQM_CTRL, TIM_START);
	}
	return 0;
}

static void skge_led_on(struct skge_hw *hw, int port)
{
	if (hw->chip_id == CHIP_ID_GENESIS) {
		skge_write8(hw, SKGEMAC_REG(port, LNK_LED_REG), LINKLED_ON);
		skge_write8(hw, B0_LED, LED_STAT_ON);

		skge_write8(hw, SKGEMAC_REG(port, RX_LED_TST), LED_T_ON);
		skge_write32(hw, SKGEMAC_REG(port, RX_LED_VAL), 100);
		skge_write8(hw, SKGEMAC_REG(port, RX_LED_CTRL), LED_START);

		switch (hw->phy_type) {
		case SK_PHY_BCOM:
			skge_xm_phy_write(hw, port, PHY_BCOM_P_EXT_CTRL,
					  PHY_B_PEC_LED_ON);
			break;
		case SK_PHY_LONE:
			skge_xm_phy_write(hw, port, PHY_LONE_LED_CFG,
					  0x0800);
			break;
		default:
			skge_write8(hw, SKGEMAC_REG(port, TX_LED_TST), LED_T_ON);
			skge_write32(hw, SKGEMAC_REG(port, TX_LED_VAL), 100);
			skge_write8(hw, SKGEMAC_REG(port, TX_LED_CTRL), LED_START);
		}
	} else {
		skge_gm_phy_write(hw, port, PHY_MARV_LED_CTRL, 0);
		skge_gm_phy_write(hw, port, PHY_MARV_LED_OVER,
				  PHY_M_LED_MO_DUP(MO_LED_ON)  |
				  PHY_M_LED_MO_10(MO_LED_ON)   |
				  PHY_M_LED_MO_100(MO_LED_ON)  |
				  PHY_M_LED_MO_1000(MO_LED_ON) |
				  PHY_M_LED_MO_RX(MO_LED_ON));
	}
}

static void skge_led_off(struct skge_hw *hw, int port)
{
	if (hw->chip_id == CHIP_ID_GENESIS) {
		skge_write8(hw, SKGEMAC_REG(port, LNK_LED_REG), LINKLED_OFF);
		skge_write8(hw, B0_LED, LED_STAT_OFF);

		skge_write32(hw, SKGEMAC_REG(port, RX_LED_VAL), 0);
		skge_write8(hw, SKGEMAC_REG(port, RX_LED_CTRL), LED_T_OFF);

		switch (hw->phy_type) {
		case SK_PHY_BCOM:
			skge_xm_phy_write(hw, port, PHY_BCOM_P_EXT_CTRL,
					  PHY_B_PEC_LED_OFF);
			break;
		case SK_PHY_LONE:
			skge_xm_phy_write(hw, port, PHY_LONE_LED_CFG,
					  PHY_L_LC_LEDT);
			break;
		default:
			skge_write32(hw, SKGEMAC_REG(port, TX_LED_VAL), 0);
			skge_write8(hw, SKGEMAC_REG(port, TX_LED_CTRL), LED_T_OFF);
		}
	} else {
		skge_gm_phy_write(hw, port, PHY_MARV_LED_CTRL, 0);
		skge_gm_phy_write(hw, port, PHY_MARV_LED_OVER,
				  PHY_M_LED_MO_DUP(MO_LED_OFF)  |
				  PHY_M_LED_MO_10(MO_LED_OFF)   |
				  PHY_M_LED_MO_100(MO_LED_OFF)  |
				  PHY_M_LED_MO_1000(MO_LED_OFF) |
				  PHY_M_LED_MO_RX(MO_LED_OFF));
	}
}

static void skge_blink_timer(unsigned long data)
{
	struct skge_port *skge = (struct skge_port *) data;
	struct skge_hw *hw = skge->hw;
	unsigned long flags;

	spin_lock_irqsave(&hw->phy_lock, flags);
	if (skge->blink_on)
		skge_led_on(hw, skge->port);
	else
		skge_led_off(hw, skge->port);
	spin_unlock_irqrestore(&hw->phy_lock, flags);

	skge->blink_on = !skge->blink_on;
	mod_timer(&skge->led_blink, jiffies + BLINK_HZ);
}

/* blink LED's for finding board */
static int skge_phys_id(struct net_device *dev, u32 data)
{
	struct skge_port *skge = netdev_priv(dev);

	if (!data || data > (u32)(MAX_SCHEDULE_TIMEOUT / HZ))
		data = (u32)(MAX_SCHEDULE_TIMEOUT / HZ);

	/* start blinking */
	skge->blink_on = 1;
	mod_timer(&skge->led_blink, jiffies+1);

	msleep_interruptible(data * 1000);
	del_timer_sync(&skge->led_blink);

	skge_led_off(skge->hw, skge->port);

	return 0;
}

static struct ethtool_ops skge_ethtool_ops = {
	.get_settings	= skge_get_settings,
	.set_settings	= skge_set_settings,
	.get_drvinfo	= skge_get_drvinfo,
	.get_regs_len	= skge_get_regs_len,
	.get_regs	= skge_get_regs,
	.get_wol	= skge_get_wol,
	.set_wol	= skge_set_wol,
	.get_msglevel	= skge_get_msglevel,
	.set_msglevel	= skge_set_msglevel,
	.nway_reset	= skge_nway_reset,
	.get_link	= ethtool_op_get_link,
	.get_ringparam	= skge_get_ring_param,
	.set_ringparam	= skge_set_ring_param,
	.get_pauseparam = skge_get_pauseparam,
	.set_pauseparam = skge_set_pauseparam,
	.get_coalesce	= skge_get_coalesce,
	.set_coalesce	= skge_set_coalesce,
	.get_tso	= ethtool_op_get_tso,
	.set_tso	= skge_set_tso,
	.get_sg		= ethtool_op_get_sg,
	.set_sg		= skge_set_sg,
	.get_tx_csum	= ethtool_op_get_tx_csum,
	.set_tx_csum	= skge_set_tx_csum,
	.get_rx_csum	= skge_get_rx_csum,
	.set_rx_csum	= skge_set_rx_csum,
	.get_strings	= skge_get_strings,
	.phys_id	= skge_phys_id,
	.get_stats_count = skge_get_stats_count,
	.get_ethtool_stats = skge_get_ethtool_stats,
};

/*
 * Allocate ring elements and chain them together
 * One-to-one association of board descriptors with ring elements
 */
static int skge_ring_alloc(struct skge_ring *ring, void *vaddr, u64 base)
{
	struct skge_tx_desc *d;
	struct skge_element *e;
	int i;

	ring->start = kmalloc(sizeof(*e)*ring->count, GFP_KERNEL);
	if (!ring->start)
		return -ENOMEM;

	for (i = 0, e = ring->start, d = vaddr; i < ring->count; i++, e++, d++) {
		e->desc = d;
		if (i == ring->count - 1) {
			e->next = ring->start;
			d->next_offset = base;
		} else {
			e->next = e + 1;
			d->next_offset = base + (i+1) * sizeof(*d);
		}
	}
	ring->to_use = ring->to_clean = ring->start;

	return 0;
}

/* Setup buffer for receiving */
static inline int skge_rx_alloc(struct skge_port *skge,
				struct skge_element *e)
{
	unsigned long bufsize = skge->netdev->mtu + ETH_HLEN; /* VLAN? */
	struct skge_rx_desc *rd = e->desc;
	struct sk_buff *skb;
	u64 map;

	skb = dev_alloc_skb(bufsize + NET_IP_ALIGN);
	if (unlikely(!skb)) {
		printk(KERN_DEBUG PFX "%s: out of memory for receive\n",
		       skge->netdev->name);
		return -ENOMEM;
	}

	skb->dev = skge->netdev;
	skb_reserve(skb, NET_IP_ALIGN);

	map = pci_map_single(skge->hw->pdev, skb->data, bufsize,
			     PCI_DMA_FROMDEVICE);

	rd->dma_lo = map;
	rd->dma_hi = map >> 32;
	e->skb = skb;
	rd->csum1_start = ETH_HLEN;
	rd->csum2_start = ETH_HLEN;
	rd->csum1 = 0;
	rd->csum2 = 0;

	wmb();

	rd->control = BMU_OWN | BMU_STF | BMU_IRQ_EOF | BMU_TCP_CHECK | bufsize;
	pci_unmap_addr_set(e, mapaddr, map);
	pci_unmap_len_set(e, maplen, bufsize);
	return 0;
}

/* Free all unused buffers in receive ring, assumes receiver stopped */
static void skge_rx_clean(struct skge_port *skge)
{
	struct skge_hw *hw = skge->hw;
	struct skge_ring *ring = &skge->rx_ring;
	struct skge_element *e;

	for (e = ring->to_clean; e != ring->to_use; e = e->next) {
		struct skge_rx_desc *rd = e->desc;
		rd->control = 0;

		pci_unmap_single(hw->pdev,
				 pci_unmap_addr(e, mapaddr),
				 pci_unmap_len(e, maplen),
				 PCI_DMA_FROMDEVICE);
		dev_kfree_skb(e->skb);
		e->skb = NULL;
	}
	ring->to_clean = e;
}

/* Allocate buffers for receive ring
 * For receive: to_use   is refill location
 *              to_clean is next received frame.
 *
 * if (to_use == to_clean)
 *	 then ring all frames in ring need buffers
 * if (to_use->next == to_clean)
 *	 then ring all frames in ring have buffers
 */
static int skge_rx_fill(struct skge_port *skge)
{
	struct skge_ring *ring = &skge->rx_ring;
	struct skge_element *e;
	int ret = 0;

	for (e = ring->to_use; e->next != ring->to_clean; e = e->next) {
		if (skge_rx_alloc(skge, e)) {
			ret = 1;
			break;
		}

	}
	ring->to_use = e;

	return ret;
}

static void skge_link_up(struct skge_port *skge)
{
	netif_carrier_on(skge->netdev);
	if (skge->tx_avail > MAX_SKB_FRAGS + 1)
		netif_wake_queue(skge->netdev);

	if (netif_msg_link(skge))
		printk(KERN_INFO PFX
		       "%s: Link is up at %d Mbps, %s duplex, flow control %s\n",
		       skge->netdev->name, skge->speed,
		       skge->duplex == DUPLEX_FULL ? "full" : "half",
		       (skge->flow_control == FLOW_MODE_NONE) ? "none" :
		       (skge->flow_control == FLOW_MODE_LOC_SEND) ? "tx only" :
		       (skge->flow_control == FLOW_MODE_REM_SEND) ? "rx only" :
		       (skge->flow_control == FLOW_MODE_SYMMETRIC) ? "tx and rx" :
		       "unknown");
}

static void skge_link_down(struct skge_port *skge)
{
	netif_carrier_off(skge->netdev);
	netif_stop_queue(skge->netdev);

	if (netif_msg_link(skge))
		printk(KERN_INFO PFX "%s: Link is down.\n", skge->netdev->name);
}

static u16 skge_xm_phy_read(struct skge_hw *hw, int port,  u16 reg)
{
	int i;
	u16 v;

	skge_xm_write16(hw, port, XM_PHY_ADDR, reg | hw->phy_addr);
	v = skge_xm_read16(hw, port, XM_PHY_DATA);
	if (hw->phy_type != SK_PHY_XMAC) {
		for (i = 0; i < PHY_RETRIES; i++) {
			udelay(1);
			if (skge_xm_read16(hw, port, XM_MMU_CMD)
			    & XM_MMU_PHY_RDY)
				goto ready;
		}

		printk(KERN_WARNING PFX "%s: phy read timed out\n",
		       hw->dev[port]->name);
		return 0;
	ready:
		v = skge_xm_read16(hw, port, XM_PHY_DATA);
	}

	return v;
}

static void skge_xm_phy_write(struct skge_hw *hw, int port, u16 reg, u16 val)
{
	int i;

	skge_xm_write16(hw, port, XM_PHY_ADDR, reg | hw->phy_addr);
	for (i = 0; i < PHY_RETRIES; i++) {
		if (!(skge_xm_read16(hw, port, XM_MMU_CMD) & XM_MMU_PHY_BUSY))
			goto ready;
		cpu_relax();
	}
	printk(KERN_WARNING PFX "%s: phy write failed to come ready\n",
	       hw->dev[port]->name);


 ready:
	skge_xm_write16(hw, port, XM_PHY_DATA, val);
	for (i = 0; i < PHY_RETRIES; i++) {
		udelay(1);
		if (!(skge_xm_read16(hw, port, XM_MMU_CMD) & XM_MMU_PHY_BUSY))
			return;
	}
	printk(KERN_WARNING PFX "%s: phy write timed out\n",
		       hw->dev[port]->name);
}

static void genesis_init(struct skge_hw *hw)
{
	/* set blink source counter */
	skge_write32(hw, B2_BSC_INI, (SK_BLK_DUR * SK_FACT_53) / 100);
	skge_write8(hw, B2_BSC_CTRL, BSC_START);

	/* configure mac arbiter */
	skge_write16(hw, B3_MA_TO_CTRL, MA_RST_CLR);

	/* configure mac arbiter timeout values */
	skge_write8(hw, B3_MA_TOINI_RX1, SK_MAC_TO_53);
	skge_write8(hw, B3_MA_TOINI_RX2, SK_MAC_TO_53);
	skge_write8(hw, B3_MA_TOINI_TX1, SK_MAC_TO_53);
	skge_write8(hw, B3_MA_TOINI_TX2, SK_MAC_TO_53);

	skge_write8(hw, B3_MA_RCINI_RX1, 0);
	skge_write8(hw, B3_MA_RCINI_RX2, 0);
	skge_write8(hw, B3_MA_RCINI_TX1, 0);
	skge_write8(hw, B3_MA_RCINI_TX2, 0);

	/* configure packet arbiter timeout */
	skge_write16(hw, B3_PA_CTRL, PA_RST_CLR);
	skge_write16(hw, B3_PA_TOINI_RX1, SK_PKT_TO_MAX);
	skge_write16(hw, B3_PA_TOINI_TX1, SK_PKT_TO_MAX);
	skge_write16(hw, B3_PA_TOINI_RX2, SK_PKT_TO_MAX);
	skge_write16(hw, B3_PA_TOINI_TX2, SK_PKT_TO_MAX);
}

static void genesis_reset(struct skge_hw *hw, int port)
{
	int i;
	u64 zero = 0;

	/* reset the statistics module */
	skge_xm_write32(hw, port, XM_GP_PORT, XM_GP_RES_STAT);
	skge_xm_write16(hw, port, XM_IMSK, 0xffff);	/* disable XMAC IRQs */
	skge_xm_write32(hw, port, XM_MODE, 0);		/* clear Mode Reg */
	skge_xm_write16(hw, port, XM_TX_CMD, 0);	/* reset TX CMD Reg */
	skge_xm_write16(hw, port, XM_RX_CMD, 0);	/* reset RX CMD Reg */

	/* disable all PHY IRQs */
	if  (hw->phy_type == SK_PHY_BCOM)
		skge_xm_write16(hw, port, PHY_BCOM_INT_MASK, 0xffff);

	skge_xm_outhash(hw, port, XM_HSM, (u8 *) &zero);
	for (i = 0; i < 15; i++)
		skge_xm_outaddr(hw, port, XM_EXM(i), (u8 *) &zero);
	skge_xm_outhash(hw, port, XM_SRC_CHK, (u8 *) &zero);
}


static void genesis_mac_init(struct skge_hw *hw, int port)
{
	struct skge_port *skge = netdev_priv(hw->dev[port]);
	int i;
	u32 r;
	u16 id1;
	u16 ctrl1, ctrl2, ctrl3, ctrl4, ctrl5;

	/* magic workaround patterns for Broadcom */
	static const struct {
		u16 reg;
		u16 val;
	} A1hack[] = {
		{ 0x18, 0x0c20 }, { 0x17, 0x0012 }, { 0x15, 0x1104 },
		{ 0x17, 0x0013 }, { 0x15, 0x0404 }, { 0x17, 0x8006 },
		{ 0x15, 0x0132 }, { 0x17, 0x8006 }, { 0x15, 0x0232 },
		{ 0x17, 0x800D }, { 0x15, 0x000F }, { 0x18, 0x0420 },
	}, C0hack[] = {
		{ 0x18, 0x0c20 }, { 0x17, 0x0012 }, { 0x15, 0x1204 },
		{ 0x17, 0x0013 }, { 0x15, 0x0A04 }, { 0x18, 0x0420 },
	};


	/* initialize Rx, Tx and Link LED */
	skge_write8(hw, SKGEMAC_REG(port, LNK_LED_REG), LINKLED_ON);
	skge_write8(hw, SKGEMAC_REG(port, LNK_LED_REG), LINKLED_LINKSYNC_ON);

	skge_write8(hw, SKGEMAC_REG(port, RX_LED_CTRL), LED_START);
	skge_write8(hw, SKGEMAC_REG(port, TX_LED_CTRL), LED_START);

	/* Unreset the XMAC. */
	skge_write16(hw, SKGEMAC_REG(port, TX_MFF_CTRL1), MFF_CLR_MAC_RST);

	/*
	 * Perform additional initialization for external PHYs,
	 * namely for the 1000baseTX cards that use the XMAC's
	 * GMII mode.
	 */
	spin_lock_bh(&hw->phy_lock);
	if (hw->phy_type != SK_PHY_XMAC) {
		/* Take PHY out of reset. */
		r = skge_read32(hw, B2_GP_IO);
		if (port == 0)
			r |= GP_DIR_0|GP_IO_0;
		else
			r |= GP_DIR_2|GP_IO_2;

		skge_write32(hw, B2_GP_IO, r);
		skge_read32(hw, B2_GP_IO);

		/* Enable GMII mode on the XMAC. */
		skge_xm_write16(hw, port, XM_HW_CFG, XM_HW_GMII_MD);

		id1 = skge_xm_phy_read(hw, port, PHY_XMAC_ID1);

		/* Optimize MDIO transfer by suppressing preamble. */
		skge_xm_write16(hw, port, XM_MMU_CMD,
				skge_xm_read16(hw, port, XM_MMU_CMD)
				| XM_MMU_NO_PRE);

		if (id1 == PHY_BCOM_ID1_C0) {
			/*
			 * Workaround BCOM Errata for the C0 type.
			 * Write magic patterns to reserved registers.
			 */
			for (i = 0; i < ARRAY_SIZE(C0hack); i++)
				skge_xm_phy_write(hw, port,
					  C0hack[i].reg, C0hack[i].val);

		} else if (id1 == PHY_BCOM_ID1_A1) {
			/*
			 * Workaround BCOM Errata for the A1 type.
			 * Write magic patterns to reserved registers.
			 */
			for (i = 0; i < ARRAY_SIZE(A1hack); i++)
				skge_xm_phy_write(hw, port,
					  A1hack[i].reg, A1hack[i].val);
		}

		/*
		 * Workaround BCOM Errata (#10523) for all BCom PHYs.
		 * Disable Power Management after reset.
		 */
		r = skge_xm_phy_read(hw, port, PHY_BCOM_AUX_CTRL);
		skge_xm_phy_write(hw, port, PHY_BCOM_AUX_CTRL, r | PHY_B_AC_DIS_PM);
	}

	/* Dummy read */
	skge_xm_read16(hw, port, XM_ISRC);

	r = skge_xm_read32(hw, port, XM_MODE);
	skge_xm_write32(hw, port, XM_MODE, r|XM_MD_CSA);

	/* We don't need the FCS appended to the packet. */
	r = skge_xm_read16(hw, port, XM_RX_CMD);
	skge_xm_write16(hw, port, XM_RX_CMD, r | XM_RX_STRIP_FCS);

	/* We want short frames padded to 60 bytes. */
	r = skge_xm_read16(hw, port, XM_TX_CMD);
	skge_xm_write16(hw, port, XM_TX_CMD, r | XM_TX_AUTO_PAD);

	/*
	 * Enable the reception of all error frames. This is is
	 * a necessary evil due to the design of the XMAC. The
	 * XMAC's receive FIFO is only 8K in size, however jumbo
	 * frames can be up to 9000 bytes in length. When bad
	 * frame filtering is enabled, the XMAC's RX FIFO operates
	 * in 'store and forward' mode. For this to work, the
	 * entire frame has to fit into the FIFO, but that means
	 * that jumbo frames larger than 8192 bytes will be
	 * truncated. Disabling all bad frame filtering causes
	 * the RX FIFO to operate in streaming mode, in which
	 * case the XMAC will start transfering frames out of the
	 * RX FIFO as soon as the FIFO threshold is reached.
	 */
	r = skge_xm_read32(hw, port, XM_MODE);
	skge_xm_write32(hw, port, XM_MODE,
		     XM_MD_RX_CRCE|XM_MD_RX_LONG|XM_MD_RX_RUNT|
		     XM_MD_RX_ERR|XM_MD_RX_IRLE);

	skge_xm_outaddr(hw, port, XM_SA, hw->dev[port]->dev_addr);
	skge_xm_outaddr(hw, port, XM_EXM(0), hw->dev[port]->dev_addr);

	/*
	 * Bump up the transmit threshold. This helps hold off transmit
	 * underruns when we're blasting traffic from both ports at once.
	 */
	skge_xm_write16(hw, port, XM_TX_THR, 512);

	/* Configure MAC arbiter */
	skge_write16(hw, B3_MA_TO_CTRL, MA_RST_CLR);

	/* configure timeout values */
	skge_write8(hw, B3_MA_TOINI_RX1, 72);
	skge_write8(hw, B3_MA_TOINI_RX2, 72);
	skge_write8(hw, B3_MA_TOINI_TX1, 72);
	skge_write8(hw, B3_MA_TOINI_TX2, 72);

	skge_write8(hw, B3_MA_RCINI_RX1, 0);
	skge_write8(hw, B3_MA_RCINI_RX2, 0);
	skge_write8(hw, B3_MA_RCINI_TX1, 0);
	skge_write8(hw, B3_MA_RCINI_TX2, 0);

	/* Configure Rx MAC FIFO */
	skge_write8(hw, SKGEMAC_REG(port, RX_MFF_CTRL2), MFF_RST_CLR);
	skge_write16(hw, SKGEMAC_REG(port, RX_MFF_CTRL1), MFF_ENA_TIM_PAT);
	skge_write8(hw, SKGEMAC_REG(port, RX_MFF_CTRL2), MFF_ENA_OP_MD);

	/* Configure Tx MAC FIFO */
	skge_write8(hw, SKGEMAC_REG(port, TX_MFF_CTRL2), MFF_RST_CLR);
	skge_write16(hw, SKGEMAC_REG(port, TX_MFF_CTRL1), MFF_TX_CTRL_DEF);
	skge_write8(hw, SKGEMAC_REG(port, TX_MFF_CTRL2), MFF_ENA_OP_MD);

	if (hw->dev[port]->mtu > ETH_DATA_LEN) {
		/* Enable frame flushing if jumbo frames used */
		skge_write16(hw, SKGEMAC_REG(port,RX_MFF_CTRL1), MFF_ENA_FLUSH);
	} else {
		/* enable timeout timers if normal frames */
		skge_write16(hw, B3_PA_CTRL,
			     port == 0 ? PA_ENA_TO_TX1 : PA_ENA_TO_TX2);
	}


	r = skge_xm_read16(hw, port, XM_RX_CMD);
	if (hw->dev[port]->mtu > ETH_DATA_LEN)
		skge_xm_write16(hw, port, XM_RX_CMD, r | XM_RX_BIG_PK_OK);
	else
		skge_xm_write16(hw, port, XM_RX_CMD, r & ~(XM_RX_BIG_PK_OK));

	switch (hw->phy_type) {
	case SK_PHY_XMAC:
		if (skge->autoneg == AUTONEG_ENABLE) {
			ctrl1 = PHY_X_AN_FD | PHY_X_AN_HD;

			switch (skge->flow_control) {
			case FLOW_MODE_NONE:
				ctrl1 |= PHY_X_P_NO_PAUSE;
				break;
			case FLOW_MODE_LOC_SEND:
				ctrl1 |= PHY_X_P_ASYM_MD;
				break;
			case FLOW_MODE_SYMMETRIC:
				ctrl1 |= PHY_X_P_SYM_MD;
				break;
			case FLOW_MODE_REM_SEND:
				ctrl1 |= PHY_X_P_BOTH_MD;
				break;
			}

			skge_xm_phy_write(hw, port, PHY_XMAC_AUNE_ADV, ctrl1);
			ctrl2 = PHY_CT_ANE | PHY_CT_RE_CFG;
		} else {
			ctrl2 = 0;
			if (skge->duplex == DUPLEX_FULL)
				ctrl2 |= PHY_CT_DUP_MD;
		}

		skge_xm_phy_write(hw, port, PHY_XMAC_CTRL, ctrl2);
		break;

	case SK_PHY_BCOM:
		ctrl1 = PHY_CT_SP1000;
		ctrl2 = 0;
		ctrl3 = PHY_SEL_TYPE;
		ctrl4 = PHY_B_PEC_EN_LTR;
		ctrl5 = PHY_B_AC_TX_TST;

		if (skge->autoneg == AUTONEG_ENABLE) {
			/*
			 * Workaround BCOM Errata #1 for the C5 type.
			 * 1000Base-T Link Acquisition Failure in Slave Mode
			 * Set Repeater/DTE bit 10 of the 1000Base-T Control Register
			 */
			ctrl2 |= PHY_B_1000C_RD;
			if (skge->advertising & ADVERTISED_1000baseT_Half)
				ctrl2 |= PHY_B_1000C_AHD;
			if (skge->advertising & ADVERTISED_1000baseT_Full)
				ctrl2 |= PHY_B_1000C_AFD;

			/* Set Flow-control capabilities */
			switch (skge->flow_control) {
			case FLOW_MODE_NONE:
				ctrl3 |= PHY_B_P_NO_PAUSE;
				break;
			case FLOW_MODE_LOC_SEND:
				ctrl3 |= PHY_B_P_ASYM_MD;
				break;
			case FLOW_MODE_SYMMETRIC:
				ctrl3 |= PHY_B_P_SYM_MD;
				break;
			case FLOW_MODE_REM_SEND:
				ctrl3 |= PHY_B_P_BOTH_MD;
				break;
			}

			/* Restart Auto-negotiation */
			ctrl1 |= PHY_CT_ANE | PHY_CT_RE_CFG;
		} else {
			if (skge->duplex == DUPLEX_FULL)
				ctrl1 |= PHY_CT_DUP_MD;

			ctrl2 |= PHY_B_1000C_MSE;	/* set it to Slave */
		}

		skge_xm_phy_write(hw, port, PHY_BCOM_1000T_CTRL, ctrl2);
		skge_xm_phy_write(hw, port, PHY_BCOM_AUNE_ADV, ctrl3);

		if (skge->netdev->mtu > ETH_DATA_LEN) {
			ctrl4 |= PHY_B_PEC_HIGH_LA;
			ctrl5 |= PHY_B_AC_LONG_PACK;

			skge_xm_phy_write(hw, port,PHY_BCOM_AUX_CTRL, ctrl5);
		}

		skge_xm_phy_write(hw, port, PHY_BCOM_P_EXT_CTRL, ctrl4);
		skge_xm_phy_write(hw, port, PHY_BCOM_CTRL, ctrl1);
		break;
	}
	spin_unlock_bh(&hw->phy_lock);

	/* Clear MIB counters */
	skge_xm_write16(hw, port, XM_STAT_CMD,
			XM_SC_CLR_RXC | XM_SC_CLR_TXC);
	/* Clear two times according to Errata #3 */
	skge_xm_write16(hw, port, XM_STAT_CMD,
			XM_SC_CLR_RXC | XM_SC_CLR_TXC);

	/* Start polling for link status */
	mod_timer(&skge->link_check, jiffies + LINK_POLL_HZ);
}

static void genesis_stop(struct skge_port *skge)
{
	struct skge_hw *hw = skge->hw;
	int port = skge->port;

	/* Clear Tx packet arbiter timeout IRQ */
	skge_write16(hw, B3_PA_CTRL,
		     port == 0 ? PA_CLR_TO_TX1 : PA_CLR_TO_TX2);

	/*
	 * If the transfer stucks at the MAC the STOP command will not
	 * terminate if we don't flush the XMAC's transmit FIFO !
	 */
	skge_xm_write32(hw, port, XM_MODE,
			skge_xm_read32(hw, port, XM_MODE)|XM_MD_FTF);


	/* Reset the MAC */
	skge_write16(hw, SKGEMAC_REG(port, TX_MFF_CTRL1), MFF_SET_MAC_RST);

	/* For external PHYs there must be special handling */
	if (hw->phy_type != SK_PHY_XMAC) {
		u32 reg = skge_read32(hw, B2_GP_IO);

		if (port == 0) {
			reg |= GP_DIR_0;
			reg &= ~GP_IO_0;
		} else {
			reg |= GP_DIR_2;
			reg &= ~GP_IO_2;
		}
		skge_write32(hw, B2_GP_IO, reg);
		skge_read32(hw, B2_GP_IO);
	}

	skge_xm_write16(hw, port, XM_MMU_CMD,
			skge_xm_read16(hw, port, XM_MMU_CMD)
			& ~(XM_MMU_ENA_RX | XM_MMU_ENA_TX));

	skge_xm_read16(hw, port, XM_MMU_CMD);
}


static void genesis_get_stats(struct skge_port *skge, u64 *data)
{
	struct skge_hw *hw = skge->hw;
	int port = skge->port;
	int i;
	unsigned long timeout = jiffies + HZ;

	skge_xm_write16(hw, port,
			XM_STAT_CMD, XM_SC_SNP_TXC | XM_SC_SNP_RXC);

	/* wait for update to complete */
	while (skge_xm_read16(hw, port, XM_STAT_CMD)
	       & (XM_SC_SNP_TXC | XM_SC_SNP_RXC)) {
		if (time_after(jiffies, timeout))
			break;
		udelay(10);
	}

	/* special case for 64 bit octet counter */
	data[0] = (u64) skge_xm_read32(hw, port, XM_TXO_OK_HI) << 32
		| skge_xm_read32(hw, port, XM_TXO_OK_LO);
	data[1] = (u64) skge_xm_read32(hw, port, XM_RXO_OK_HI) << 32
		| skge_xm_read32(hw, port, XM_RXO_OK_LO);

	for (i = 2; i < ARRAY_SIZE(skge_stats); i++)
		data[i] = skge_xm_read32(hw, port, skge_stats[i].xmac_offset);
}

static void genesis_mac_intr(struct skge_hw *hw, int port)
{
	struct skge_port *skge = netdev_priv(hw->dev[port]);
	u16 status = skge_xm_read16(hw, port, XM_ISRC);

	pr_debug("genesis_intr status %x\n", status);
	if (hw->phy_type == SK_PHY_XMAC) {
		/* LInk down, start polling for state change */
		if (status & XM_IS_INP_ASS) {
			skge_xm_write16(hw, port, XM_IMSK,
					skge_xm_read16(hw, port, XM_IMSK) | XM_IS_INP_ASS);
			mod_timer(&skge->link_check, jiffies + LINK_POLL_HZ);
		}
		else if (status & XM_IS_AND)
			mod_timer(&skge->link_check, jiffies + LINK_POLL_HZ);
	}

	if (status & XM_IS_TXF_UR) {
		skge_xm_write32(hw, port, XM_MODE, XM_MD_FTF);
		++skge->net_stats.tx_fifo_errors;
	}
	if (status & XM_IS_RXF_OV) {
		skge_xm_write32(hw, port, XM_MODE, XM_MD_FRF);
		++skge->net_stats.rx_fifo_errors;
	}
}

static void skge_gm_phy_write(struct skge_hw *hw, int port, u16 reg, u16 val)
{
	int i;

	skge_gma_write16(hw, port, GM_SMI_DATA, val);
	skge_gma_write16(hw, port, GM_SMI_CTRL,
			 GM_SMI_CT_PHY_AD(hw->phy_addr) | GM_SMI_CT_REG_AD(reg));
	for (i = 0; i < PHY_RETRIES; i++) {
		udelay(1);

		if (!(skge_gma_read16(hw, port, GM_SMI_CTRL) & GM_SMI_CT_BUSY))
			break;
	}
}

static u16 skge_gm_phy_read(struct skge_hw *hw, int port, u16 reg)
{
	int i;

	skge_gma_write16(hw, port, GM_SMI_CTRL,
			 GM_SMI_CT_PHY_AD(hw->phy_addr)
			 | GM_SMI_CT_REG_AD(reg) | GM_SMI_CT_OP_RD);

	for (i = 0; i < PHY_RETRIES; i++) {
		udelay(1);
		if (skge_gma_read16(hw, port, GM_SMI_CTRL) & GM_SMI_CT_RD_VAL)
			goto ready;
	}

	printk(KERN_WARNING PFX "%s: phy read timeout\n",
	       hw->dev[port]->name);
	return 0;
 ready:
	return skge_gma_read16(hw, port, GM_SMI_DATA);
}

static void genesis_link_down(struct skge_port *skge)
{
	struct skge_hw *hw = skge->hw;
	int port = skge->port;

	pr_debug("genesis_link_down\n");

	skge_xm_write16(hw, port, XM_MMU_CMD,
			skge_xm_read16(hw, port, XM_MMU_CMD)
			& ~(XM_MMU_ENA_RX | XM_MMU_ENA_TX));

	/* dummy read to ensure writing */
	(void) skge_xm_read16(hw, port, XM_MMU_CMD);

	skge_link_down(skge);
}

static void genesis_link_up(struct skge_port *skge)
{
	struct skge_hw *hw = skge->hw;
	int port = skge->port;
	u16 cmd;
	u32 mode, msk;

	pr_debug("genesis_link_up\n");
	cmd = skge_xm_read16(hw, port, XM_MMU_CMD);

	/*
	 * enabling pause frame reception is required for 1000BT
	 * because the XMAC is not reset if the link is going down
	 */
	if (skge->flow_control == FLOW_MODE_NONE ||
	    skge->flow_control == FLOW_MODE_LOC_SEND)
		cmd |= XM_MMU_IGN_PF;
	else
		/* Enable Pause Frame Reception */
		cmd &= ~XM_MMU_IGN_PF;

	skge_xm_write16(hw, port, XM_MMU_CMD, cmd);

	mode = skge_xm_read32(hw, port, XM_MODE);
	if (skge->flow_control == FLOW_MODE_SYMMETRIC ||
	    skge->flow_control == FLOW_MODE_LOC_SEND) {
		/*
		 * Configure Pause Frame Generation
		 * Use internal and external Pause Frame Generation.
		 * Sending pause frames is edge triggered.
		 * Send a Pause frame with the maximum pause time if
		 * internal oder external FIFO full condition occurs.
		 * Send a zero pause time frame to re-start transmission.
		 */
		/* XM_PAUSE_DA = '010000C28001' (default) */
		/* XM_MAC_PTIME = 0xffff (maximum) */
		/* remember this value is defined in big endian (!) */
		skge_xm_write16(hw, port, XM_MAC_PTIME, 0xffff);

		mode |= XM_PAUSE_MODE;
		skge_write16(hw, SKGEMAC_REG(port, RX_MFF_CTRL1), MFF_ENA_PAUSE);
	} else {
		/*
		 * disable pause frame generation is required for 1000BT
		 * because the XMAC is not reset if the link is going down
		 */
		/* Disable Pause Mode in Mode Register */
		mode &= ~XM_PAUSE_MODE;

		skge_write16(hw, SKGEMAC_REG(port, RX_MFF_CTRL1), MFF_DIS_PAUSE);
	}

	skge_xm_write32(hw, port, XM_MODE, mode);

	msk = XM_DEF_MSK;
	if (hw->phy_type != SK_PHY_XMAC)
		msk |= XM_IS_INP_ASS;	/* disable GP0 interrupt bit */

	skge_xm_write16(hw, port, XM_IMSK, msk);
	skge_xm_read16(hw, port, XM_ISRC);

	/* get MMU Command Reg. */
	cmd = skge_xm_read16(hw, port, XM_MMU_CMD);
	if (hw->phy_type != SK_PHY_XMAC && skge->duplex == DUPLEX_FULL)
		cmd |= XM_MMU_GMII_FD;

	if (hw->phy_type == SK_PHY_BCOM) {
		/*
		 * Workaround BCOM Errata (#10523) for all BCom Phys
		 * Enable Power Management after link up
		 */
		skge_xm_phy_write(hw, port, PHY_BCOM_AUX_CTRL,
				  skge_xm_phy_read(hw, port, PHY_BCOM_AUX_CTRL)
				  & ~PHY_B_AC_DIS_PM);
		skge_xm_phy_write(hw, port, PHY_BCOM_INT_MASK,
				  PHY_B_DEF_MSK);
	}

	/* enable Rx/Tx */
	skge_xm_write16(hw, port, XM_MMU_CMD,
			cmd | XM_MMU_ENA_RX | XM_MMU_ENA_TX);
	skge_link_up(skge);
}


static void genesis_bcom_intr(struct skge_port *skge)
{
	struct skge_hw *hw = skge->hw;
	int port = skge->port;
	u16 stat = skge_xm_phy_read(hw, port, PHY_BCOM_INT_STAT);

	pr_debug("genesis_bcom intr stat=%x\n", stat);

	/* Workaround BCom Errata:
	 *	enable and disable loopback mode if "NO HCD" occurs.
	 */
	if (stat & PHY_B_IS_NO_HDCL) {
		u16 ctrl = skge_xm_phy_read(hw, port, PHY_BCOM_CTRL);
		skge_xm_phy_write(hw, port, PHY_BCOM_CTRL,
				  ctrl | PHY_CT_LOOP);
		skge_xm_phy_write(hw, port, PHY_BCOM_CTRL,
				  ctrl & ~PHY_CT_LOOP);
	}

	stat = skge_xm_phy_read(hw, port, PHY_BCOM_STAT);
	if (stat & (PHY_B_IS_AN_PR | PHY_B_IS_LST_CHANGE)) {
		u16 aux = skge_xm_phy_read(hw, port, PHY_BCOM_AUX_STAT);
		if ( !(aux & PHY_B_AS_LS) && netif_carrier_ok(skge->netdev))
			genesis_link_down(skge);

		else if (stat & PHY_B_IS_LST_CHANGE) {
			if (aux & PHY_B_AS_AN_C) {
				switch (aux & PHY_B_AS_AN_RES_MSK) {
				case PHY_B_RES_1000FD:
					skge->duplex = DUPLEX_FULL;
					break;
				case PHY_B_RES_1000HD:
					skge->duplex = DUPLEX_HALF;
					break;
				}

				switch (aux & PHY_B_AS_PAUSE_MSK) {
				case PHY_B_AS_PAUSE_MSK:
					skge->flow_control = FLOW_MODE_SYMMETRIC;
					break;
				case PHY_B_AS_PRR:
					skge->flow_control = FLOW_MODE_REM_SEND;
					break;
				case PHY_B_AS_PRT:
					skge->flow_control = FLOW_MODE_LOC_SEND;
					break;
				default:
					skge->flow_control = FLOW_MODE_NONE;
				}
				skge->speed = SPEED_1000;
			}
			genesis_link_up(skge);
		}
		else
			mod_timer(&skge->link_check, jiffies + LINK_POLL_HZ);
	}
}

/* Perodic poll of phy status to check for link transistion  */
static void skge_link_timer(unsigned long __arg)
{
	struct skge_port *skge = (struct skge_port *) __arg;
	struct skge_hw *hw = skge->hw;
	int port = skge->port;

	if (hw->chip_id != CHIP_ID_GENESIS || !netif_running(skge->netdev))
		return;

	spin_lock_bh(&hw->phy_lock);
	if (hw->phy_type == SK_PHY_BCOM)
		genesis_bcom_intr(skge);
	else {
		int i;
		for (i = 0; i < 3; i++)
			if (skge_xm_read16(hw, port, XM_ISRC) & XM_IS_INP_ASS)
				break;

		if (i == 3)
			mod_timer(&skge->link_check, jiffies + LINK_POLL_HZ);
		else
			genesis_link_up(skge);
	}
	spin_unlock_bh(&hw->phy_lock);
}

/* Marvell Phy Initailization */
static void yukon_init(struct skge_hw *hw, int port)
{
	struct skge_port *skge = netdev_priv(hw->dev[port]);
	u16 ctrl, ct1000, adv;
	u16 ledctrl, ledover;

	pr_debug("yukon_init\n");
	if (skge->autoneg == AUTONEG_ENABLE) {
		u16 ectrl = skge_gm_phy_read(hw, port, PHY_MARV_EXT_CTRL);

		ectrl &= ~(PHY_M_EC_M_DSC_MSK | PHY_M_EC_S_DSC_MSK |
			  PHY_M_EC_MAC_S_MSK);
		ectrl |= PHY_M_EC_MAC_S(MAC_TX_CLK_25_MHZ);

		/* on PHY 88E1111 there is a change for downshift control */
		if (hw->chip_id == CHIP_ID_YUKON_EC)
			ectrl |= PHY_M_EC_M_DSC_2(0) | PHY_M_EC_DOWN_S_ENA;
		else
			ectrl |= PHY_M_EC_M_DSC(0) | PHY_M_EC_S_DSC(1);

		skge_gm_phy_write(hw, port, PHY_MARV_EXT_CTRL, ectrl);
	}

	ctrl = skge_gm_phy_read(hw, port, PHY_MARV_CTRL);
	if (skge->autoneg == AUTONEG_DISABLE)
		ctrl &= ~PHY_CT_ANE;

	ctrl |= PHY_CT_RESET;
	skge_gm_phy_write(hw, port, PHY_MARV_CTRL, ctrl);

	ctrl = 0;
	ct1000 = 0;
	adv = PHY_SEL_TYPE;

	if (skge->autoneg == AUTONEG_ENABLE) {
		if (iscopper(hw)) {
			if (skge->advertising & ADVERTISED_1000baseT_Full)
				ct1000 |= PHY_M_1000C_AFD;
			if (skge->advertising & ADVERTISED_1000baseT_Half)
				ct1000 |= PHY_M_1000C_AHD;
			if (skge->advertising & ADVERTISED_100baseT_Full)
				adv |= PHY_M_AN_100_FD;
			if (skge->advertising & ADVERTISED_100baseT_Half)
				adv |= PHY_M_AN_100_HD;
			if (skge->advertising & ADVERTISED_10baseT_Full)
				adv |= PHY_M_AN_10_FD;
			if (skge->advertising & ADVERTISED_10baseT_Half)
				adv |= PHY_M_AN_10_HD;

			/* Set Flow-control capabilities */
			switch (skge->flow_control) {
			case FLOW_MODE_NONE:
				adv |= PHY_B_P_NO_PAUSE;
				break;
			case FLOW_MODE_LOC_SEND:
				adv |= PHY_B_P_ASYM_MD;
				break;
			case FLOW_MODE_SYMMETRIC:
				adv |= PHY_B_P_SYM_MD;
				break;
			case FLOW_MODE_REM_SEND:
				adv |= PHY_B_P_BOTH_MD;
				break;
			}
		} else {	/* special defines for FIBER (88E1011S only) */
			adv |= PHY_M_AN_1000X_AHD | PHY_M_AN_1000X_AFD;

			/* Set Flow-control capabilities */
			switch (skge->flow_control) {
			case FLOW_MODE_NONE:
				adv |= PHY_M_P_NO_PAUSE_X;
				break;
			case FLOW_MODE_LOC_SEND:
				adv |= PHY_M_P_ASYM_MD_X;
				break;
			case FLOW_MODE_SYMMETRIC:
				adv |= PHY_M_P_SYM_MD_X;
				break;
			case FLOW_MODE_REM_SEND:
				adv |= PHY_M_P_BOTH_MD_X;
				break;
			}
		}
		/* Restart Auto-negotiation */
		ctrl |= PHY_CT_ANE | PHY_CT_RE_CFG;
	} else {
		/* forced speed/duplex settings */
		ct1000 = PHY_M_1000C_MSE;

		if (skge->duplex == DUPLEX_FULL)
			ctrl |= PHY_CT_DUP_MD;

		switch (skge->speed) {
		case SPEED_1000:
			ctrl |= PHY_CT_SP1000;
			break;
		case SPEED_100:
			ctrl |= PHY_CT_SP100;
			break;
		}

		ctrl |= PHY_CT_RESET;
	}

	if (hw->chip_id != CHIP_ID_YUKON_FE)
		skge_gm_phy_write(hw, port, PHY_MARV_1000T_CTRL, ct1000);

	skge_gm_phy_write(hw, port, PHY_MARV_AUNE_ADV, adv);
	skge_gm_phy_write(hw, port, PHY_MARV_CTRL, ctrl);

	/* Setup Phy LED's */
	ledctrl = PHY_M_LED_PULS_DUR(PULS_170MS);
	ledover = 0;

	if (hw->chip_id == CHIP_ID_YUKON_FE) {
		/* on 88E3082 these bits are at 11..9 (shifted left) */
		ledctrl |= PHY_M_LED_BLINK_RT(BLINK_84MS) << 1;

		skge_gm_phy_write(hw, port, PHY_MARV_FE_LED_PAR,
				  ((skge_gm_phy_read(hw, port, PHY_MARV_FE_LED_PAR)

				    & ~PHY_M_FELP_LED1_MSK)
				   | PHY_M_FELP_LED1_CTRL(LED_PAR_CTRL_ACT_BL)));
	} else {
		/* set Tx LED (LED_TX) to blink mode on Rx OR Tx activity */
		ledctrl |= PHY_M_LED_BLINK_RT(BLINK_84MS) | PHY_M_LEDC_TX_CTRL;

		/* turn off the Rx LED (LED_RX) */
		ledover |= PHY_M_LED_MO_RX(MO_LED_OFF);
	}

	/* disable blink mode (LED_DUPLEX) on collisions */
	ctrl |= PHY_M_LEDC_DP_CTRL;
	skge_gm_phy_write(hw, port, PHY_MARV_LED_CTRL, ledctrl);

	if (skge->autoneg == AUTONEG_DISABLE || skge->speed == SPEED_100) {
		/* turn on 100 Mbps LED (LED_LINK100) */
		ledover |= PHY_M_LED_MO_100(MO_LED_ON);
	}

	if (ledover)
		skge_gm_phy_write(hw, port, PHY_MARV_LED_OVER, ledover);

	/* Enable phy interrupt on autonegotiation complete (or link up) */
	if (skge->autoneg == AUTONEG_ENABLE)
		skge_gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_IS_AN_COMPL);
	else
		skge_gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_DEF_MSK);
}

static void yukon_reset(struct skge_hw *hw, int port)
{
	skge_gm_phy_write(hw, port, PHY_MARV_INT_MASK, 0);/* disable PHY IRQs */
	skge_gma_write16(hw, port, GM_MC_ADDR_H1, 0);	/* clear MC hash */
	skge_gma_write16(hw, port, GM_MC_ADDR_H2, 0);
	skge_gma_write16(hw, port, GM_MC_ADDR_H3, 0);
	skge_gma_write16(hw, port, GM_MC_ADDR_H4, 0);

	skge_gma_write16(hw, port, GM_RX_CTRL,
			 skge_gma_read16(hw, port, GM_RX_CTRL)
			 | GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA);
}

static void yukon_mac_init(struct skge_hw *hw, int port)
{
	struct skge_port *skge = netdev_priv(hw->dev[port]);
	int i;
	u32 reg;
	const u8 *addr = hw->dev[port]->dev_addr;

	/* WA code for COMA mode -- set PHY reset */
	if (hw->chip_id == CHIP_ID_YUKON_LITE &&
	    chip_rev(hw) == CHIP_REV_YU_LITE_A3)
		skge_write32(hw, B2_GP_IO,
			     (skge_read32(hw, B2_GP_IO) | GP_DIR_9 | GP_IO_9));

	/* hard reset */
	skge_write32(hw, SKGEMAC_REG(port, GPHY_CTRL), GPC_RST_SET);
	skge_write32(hw, SKGEMAC_REG(port, GMAC_CTRL), GMC_RST_SET);

	/* WA code for COMA mode -- clear PHY reset */
	if (hw->chip_id == CHIP_ID_YUKON_LITE &&
	    chip_rev(hw) == CHIP_REV_YU_LITE_A3)
		skge_write32(hw, B2_GP_IO,
			     (skge_read32(hw, B2_GP_IO) | GP_DIR_9)
			     & ~GP_IO_9);

	/* Set hardware config mode */
	reg = GPC_INT_POL_HI | GPC_DIS_FC | GPC_DIS_SLEEP |
		GPC_ENA_XC | GPC_ANEG_ADV_ALL_M | GPC_ENA_PAUSE;
	reg |= iscopper(hw) ? GPC_HWCFG_GMII_COP : GPC_HWCFG_GMII_FIB;

	/* Clear GMC reset */
	skge_write32(hw, SKGEMAC_REG(port, GPHY_CTRL), reg | GPC_RST_SET);
	skge_write32(hw, SKGEMAC_REG(port, GPHY_CTRL), reg | GPC_RST_CLR);
	skge_write32(hw, SKGEMAC_REG(port, GMAC_CTRL), GMC_PAUSE_ON | GMC_RST_CLR);
	if (skge->autoneg == AUTONEG_DISABLE) {
		reg = GM_GPCR_AU_ALL_DIS;
		skge_gma_write16(hw, port, GM_GP_CTRL,
				 skge_gma_read16(hw, port, GM_GP_CTRL) | reg);

		switch (skge->speed) {
		case SPEED_1000:
			reg |= GM_GPCR_SPEED_1000;
			/* fallthru */
		case SPEED_100:
			reg |= GM_GPCR_SPEED_100;
		}

		if (skge->duplex == DUPLEX_FULL)
			reg |= GM_GPCR_DUP_FULL;
	} else
		reg = GM_GPCR_SPEED_1000 | GM_GPCR_SPEED_100 | GM_GPCR_DUP_FULL;
	switch (skge->flow_control) {
	case FLOW_MODE_NONE:
		skge_write32(hw, SKGEMAC_REG(port, GMAC_CTRL), GMC_PAUSE_OFF);
		reg |= GM_GPCR_FC_TX_DIS | GM_GPCR_FC_RX_DIS | GM_GPCR_AU_FCT_DIS;
		break;
	case FLOW_MODE_LOC_SEND:
		/* disable Rx flow-control */
		reg |= GM_GPCR_FC_RX_DIS | GM_GPCR_AU_FCT_DIS;
	}

	skge_gma_write16(hw, port, GM_GP_CTRL, reg);
	skge_read16(hw, GMAC_IRQ_SRC);

	spin_lock_bh(&hw->phy_lock);
	yukon_init(hw, port);
	spin_unlock_bh(&hw->phy_lock);

	/* MIB clear */
	reg = skge_gma_read16(hw, port, GM_PHY_ADDR);
	skge_gma_write16(hw, port, GM_PHY_ADDR, reg | GM_PAR_MIB_CLR);

	for (i = 0; i < GM_MIB_CNT_SIZE; i++)
		skge_gma_read16(hw, port, GM_MIB_CNT_BASE + 8*i);
	skge_gma_write16(hw, port, GM_PHY_ADDR, reg);

	/* transmit control */
	skge_gma_write16(hw, port, GM_TX_CTRL, TX_COL_THR(TX_COL_DEF));

	/* receive control reg: unicast + multicast + no FCS  */
	skge_gma_write16(hw, port, GM_RX_CTRL,
			 GM_RXCR_UCF_ENA | GM_RXCR_CRC_DIS | GM_RXCR_MCF_ENA);

	/* transmit flow control */
	skge_gma_write16(hw, port, GM_TX_FLOW_CTRL, 0xffff);

	/* transmit parameter */
	skge_gma_write16(hw, port, GM_TX_PARAM,
			 TX_JAM_LEN_VAL(TX_JAM_LEN_DEF) |
			 TX_JAM_IPG_VAL(TX_JAM_IPG_DEF) |
			 TX_IPG_JAM_DATA(TX_IPG_JAM_DEF));

	/* serial mode register */
	reg = GM_SMOD_VLAN_ENA | IPG_DATA_VAL(IPG_DATA_DEF);
	if (hw->dev[port]->mtu > 1500)
		reg |= GM_SMOD_JUMBO_ENA;

	skge_gma_write16(hw, port, GM_SERIAL_MODE, reg);

	/* physical address: used for pause frames */
	skge_gm_set_addr(hw, port, GM_SRC_ADDR_1L, addr);
	/* virtual address for data */
	skge_gm_set_addr(hw, port, GM_SRC_ADDR_2L, addr);

	/* enable interrupt mask for counter overflows */
	skge_gma_write16(hw, port, GM_TX_IRQ_MSK, 0);
	skge_gma_write16(hw, port, GM_RX_IRQ_MSK, 0);
	skge_gma_write16(hw, port, GM_TR_IRQ_MSK, 0);

	/* Initialize Mac Fifo */

	/* Configure Rx MAC FIFO */
	skge_write16(hw, SKGEMAC_REG(port, RX_GMF_FL_MSK), RX_FF_FL_DEF_MSK);
	reg = GMF_OPER_ON | GMF_RX_F_FL_ON;
	if (hw->chip_id == CHIP_ID_YUKON_LITE &&
	    chip_rev(hw) == CHIP_REV_YU_LITE_A3)
		reg &= ~GMF_RX_F_FL_ON;
	skge_write8(hw, SKGEMAC_REG(port, RX_GMF_CTRL_T), GMF_RST_CLR);
	skge_write16(hw, SKGEMAC_REG(port, RX_GMF_CTRL_T), reg);
	skge_write16(hw, SKGEMAC_REG(port, RX_GMF_FL_THR), RX_GMF_FL_THR_DEF);

	/* Configure Tx MAC FIFO */
	skge_write8(hw, SKGEMAC_REG(port, TX_GMF_CTRL_T), GMF_RST_CLR);
	skge_write16(hw, SKGEMAC_REG(port, TX_GMF_CTRL_T), GMF_OPER_ON);
}

static void yukon_stop(struct skge_port *skge)
{
	struct skge_hw *hw = skge->hw;
	int port = skge->port;

	if (hw->chip_id == CHIP_ID_YUKON_LITE &&
	    chip_rev(hw) == CHIP_REV_YU_LITE_A3) {
		skge_write32(hw, B2_GP_IO,
			     skge_read32(hw, B2_GP_IO) | GP_DIR_9 | GP_IO_9);
	}

	skge_gma_write16(hw, port, GM_GP_CTRL,
			 skge_gma_read16(hw, port, GM_GP_CTRL)
			 & ~(GM_GPCR_RX_ENA|GM_GPCR_RX_ENA));
	skge_gma_read16(hw, port, GM_GP_CTRL);

	/* set GPHY Control reset */
	skge_gma_write32(hw, port, GPHY_CTRL, GPC_RST_SET);
	skge_gma_write32(hw, port, GMAC_CTRL, GMC_RST_SET);
}

static void yukon_get_stats(struct skge_port *skge, u64 *data)
{
	struct skge_hw *hw = skge->hw;
	int port = skge->port;
	int i;

	data[0] = (u64) skge_gma_read32(hw, port, GM_TXO_OK_HI) << 32
		| skge_gma_read32(hw, port, GM_TXO_OK_LO);
	data[1] = (u64) skge_gma_read32(hw, port, GM_RXO_OK_HI) << 32
		| skge_gma_read32(hw, port, GM_RXO_OK_LO);

	for (i = 2; i < ARRAY_SIZE(skge_stats); i++)
		data[i] = skge_gma_read32(hw, port,
					  skge_stats[i].gma_offset);
}

static void yukon_mac_intr(struct skge_hw *hw, int port)
{
	struct skge_port *skge = netdev_priv(hw->dev[port]);
	u8 status = skge_read8(hw, SKGEMAC_REG(port, GMAC_IRQ_SRC));

	pr_debug("yukon_intr status %x\n", status);
	if (status & GM_IS_RX_FF_OR) {
		++skge->net_stats.rx_fifo_errors;
		skge_gma_write8(hw, port, RX_GMF_CTRL_T, GMF_CLI_RX_FO);
	}
	if (status & GM_IS_TX_FF_UR) {
		++skge->net_stats.tx_fifo_errors;
		skge_gma_write8(hw, port, TX_GMF_CTRL_T, GMF_CLI_TX_FU);
	}

}

static u16 yukon_speed(const struct skge_hw *hw, u16 aux)
{
	if (hw->chip_id == CHIP_ID_YUKON_FE)
		return (aux & PHY_M_PS_SPEED_100) ? SPEED_100 : SPEED_10;

	switch (aux & PHY_M_PS_SPEED_MSK) {
	case PHY_M_PS_SPEED_1000:
		return SPEED_1000;
	case PHY_M_PS_SPEED_100:
		return SPEED_100;
	default:
		return SPEED_10;
	}
}

static void yukon_link_up(struct skge_port *skge)
{
	struct skge_hw *hw = skge->hw;
	int port = skge->port;
	u16 reg;

	pr_debug("yukon_link_up\n");

	/* Enable Transmit FIFO Underrun */
	skge_write8(hw, GMAC_IRQ_MSK, GMAC_DEF_MSK);

	reg = skge_gma_read16(hw, port, GM_GP_CTRL);
	if (skge->duplex == DUPLEX_FULL || skge->autoneg == AUTONEG_ENABLE)
		reg |= GM_GPCR_DUP_FULL;

	/* enable Rx/Tx */
	reg |= GM_GPCR_RX_ENA | GM_GPCR_TX_ENA;
	skge_gma_write16(hw, port, GM_GP_CTRL, reg);

	skge_gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_DEF_MSK);
	skge_link_up(skge);
}

static void yukon_link_down(struct skge_port *skge)
{
	struct skge_hw *hw = skge->hw;
	int port = skge->port;

	pr_debug("yukon_link_down\n");
	skge_gm_phy_write(hw, port, PHY_MARV_INT_MASK, 0);
	skge_gm_phy_write(hw, port, GM_GP_CTRL,
			  skge_gm_phy_read(hw, port, GM_GP_CTRL)
			  & ~(GM_GPCR_RX_ENA | GM_GPCR_TX_ENA));

	if (hw->chip_id != CHIP_ID_YUKON_FE &&
	    skge->flow_control == FLOW_MODE_REM_SEND) {
		/* restore Asymmetric Pause bit */
		skge_gm_phy_write(hw, port, PHY_MARV_AUNE_ADV,
				  skge_gm_phy_read(hw, port,
						   PHY_MARV_AUNE_ADV)
				  | PHY_M_AN_ASP);

	}

	yukon_reset(hw, port);
	skge_link_down(skge);

	yukon_init(hw, port);
}

static void yukon_phy_intr(struct skge_port *skge)
{
	struct skge_hw *hw = skge->hw;
	int port = skge->port;
	const char *reason = NULL;
	u16 istatus, phystat;

	istatus = skge_gm_phy_read(hw, port, PHY_MARV_INT_STAT);
	phystat = skge_gm_phy_read(hw, port, PHY_MARV_PHY_STAT);
	pr_debug("yukon phy intr istat=%x phy_stat=%x\n", istatus, phystat);

	if (istatus & PHY_M_IS_AN_COMPL) {
		if (skge_gm_phy_read(hw, port, PHY_MARV_AUNE_LP)
		    & PHY_M_AN_RF) {
			reason = "remote fault";
			goto failed;
		}

		if (!(hw->chip_id == CHIP_ID_YUKON_FE || hw->chip_id == CHIP_ID_YUKON_EC)
		    && (skge_gm_phy_read(hw, port, PHY_MARV_1000T_STAT)
			& PHY_B_1000S_MSF)) {
			reason = "master/slave fault";
			goto failed;
		}

		if (!(phystat & PHY_M_PS_SPDUP_RES)) {
			reason = "speed/duplex";
			goto failed;
		}

		skge->duplex = (phystat & PHY_M_PS_FULL_DUP)
			? DUPLEX_FULL : DUPLEX_HALF;
		skge->speed = yukon_speed(hw, phystat);

		/* Tx & Rx Pause Enabled bits are at 9..8 */
		if (hw->chip_id == CHIP_ID_YUKON_XL)
			phystat >>= 6;

		/* We are using IEEE 802.3z/D5.0 Table 37-4 */
		switch (phystat & PHY_M_PS_PAUSE_MSK) {
		case PHY_M_PS_PAUSE_MSK:
			skge->flow_control = FLOW_MODE_SYMMETRIC;
			break;
		case PHY_M_PS_RX_P_EN:
			skge->flow_control = FLOW_MODE_REM_SEND;
			break;
		case PHY_M_PS_TX_P_EN:
			skge->flow_control = FLOW_MODE_LOC_SEND;
			break;
		default:
			skge->flow_control = FLOW_MODE_NONE;
		}

		if (skge->flow_control == FLOW_MODE_NONE ||
		    (skge->speed < SPEED_1000 && skge->duplex == DUPLEX_HALF))
			skge_write8(hw, SKGEMAC_REG(port, GMAC_CTRL), GMC_PAUSE_OFF);
		else
			skge_write8(hw, SKGEMAC_REG(port, GMAC_CTRL), GMC_PAUSE_ON);
		yukon_link_up(skge);
		return;
	}

	if (istatus & PHY_M_IS_LSP_CHANGE)
		skge->speed = yukon_speed(hw, phystat);

	if (istatus & PHY_M_IS_DUP_CHANGE)
		skge->duplex = (phystat & PHY_M_PS_FULL_DUP) ? DUPLEX_FULL : DUPLEX_HALF;
	if (istatus & PHY_M_IS_LST_CHANGE) {
		if (phystat & PHY_M_PS_LINK_UP)
			yukon_link_up(skge);
		else
			yukon_link_down(skge);
	}
	return;
 failed:
	printk(KERN_ERR PFX "%s: autonegotiation failed (%s)\n",
	       skge->netdev->name, reason);

	/* XXX restart autonegotiation? */
}

static void skge_ramset(struct skge_hw *hw, u16 q, u32 start, size_t len)
{
	u32 end;

	start /= 8;
	len /= 8;
	end = start + len - 1;

	skge_write8(hw, RB_ADDR(q, RB_CTRL), RB_RST_CLR);
	skge_write32(hw, RB_ADDR(q, RB_START), start);
	skge_write32(hw, RB_ADDR(q, RB_WP), start);
	skge_write32(hw, RB_ADDR(q, RB_RP), start);
	skge_write32(hw, RB_ADDR(q, RB_END), end);

	if (q == Q_R1 || q == Q_R2) {
		/* Set thresholds on receive queue's */
		skge_write32(hw, RB_ADDR(q, RB_RX_UTPP),
			     start + (2*len)/3);
		skge_write32(hw, RB_ADDR(q, RB_RX_LTPP),
			     start + (len/3));
	} else {
		/* Enable store & forward on Tx queue's because
		 * Tx FIFO is only 4K on Genesis and 1K on Yukon
		 */
		skge_write8(hw, RB_ADDR(q, RB_CTRL), RB_ENA_STFWD);
	}

	skge_write8(hw, RB_ADDR(q, RB_CTRL), RB_ENA_OP_MD);
}

/* Setup Bus Memory Interface */
static void skge_qset(struct skge_port *skge, u16 q,
		      const struct skge_element *e)
{
	struct skge_hw *hw = skge->hw;
	u32 watermark = 0x600;
	u64 base = skge->dma + (e->desc - skge->mem);

	/* optimization to reduce window on 32bit/33mhz */
	if ((skge_read16(hw, B0_CTST) & (CS_BUS_CLOCK | CS_BUS_SLOT_SZ)) == 0)
		watermark /= 2;

	skge_write32(hw, Q_ADDR(q, Q_CSR), CSR_CLR_RESET);
	skge_write32(hw, Q_ADDR(q, Q_F), watermark);
	skge_write32(hw, Q_ADDR(q, Q_DA_H), (u32)(base >> 32));
	skge_write32(hw, Q_ADDR(q, Q_DA_L), (u32)base);
}

static int skge_up(struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;
	int port = skge->port;
	u32 chunk, ram_addr;
	size_t rx_size, tx_size;
	int err;

	if (netif_msg_ifup(skge))
		printk(KERN_INFO PFX "%s: enabling interface\n", dev->name);

	rx_size = skge->rx_ring.count * sizeof(struct skge_rx_desc);
	tx_size = skge->tx_ring.count * sizeof(struct skge_tx_desc);
	skge->mem_size = tx_size + rx_size;
	skge->mem = pci_alloc_consistent(hw->pdev, skge->mem_size, &skge->dma);
	if (!skge->mem)
		return -ENOMEM;

	memset(skge->mem, 0, skge->mem_size);

	if ((err = skge_ring_alloc(&skge->rx_ring, skge->mem, skge->dma)))
		goto free_pci_mem;

	if (skge_rx_fill(skge))
		goto free_rx_ring;

	if ((err = skge_ring_alloc(&skge->tx_ring, skge->mem + rx_size,
				   skge->dma + rx_size)))
		goto free_rx_ring;

	skge->tx_avail = skge->tx_ring.count - 1;

	/* Initialze MAC */
	if (hw->chip_id == CHIP_ID_GENESIS)
		genesis_mac_init(hw, port);
	else
		yukon_mac_init(hw, port);

	/* Configure RAMbuffers */
	chunk = hw->ram_size / (isdualport(hw) ? 4 : 2);
	ram_addr = hw->ram_offset + 2 * chunk * port;

	skge_ramset(hw, rxqaddr[port], ram_addr, chunk);
	skge_qset(skge, rxqaddr[port], skge->rx_ring.to_clean);

	BUG_ON(skge->tx_ring.to_use != skge->tx_ring.to_clean);
	skge_ramset(hw, txqaddr[port], ram_addr+chunk, chunk);
	skge_qset(skge, txqaddr[port], skge->tx_ring.to_use);

	/* Start receiver BMU */
	wmb();
	skge_write8(hw, Q_ADDR(rxqaddr[port], Q_CSR), CSR_START | CSR_IRQ_CL_F);

	pr_debug("skge_up completed\n");
	return 0;

 free_rx_ring:
	skge_rx_clean(skge);
	kfree(skge->rx_ring.start);
 free_pci_mem:
	pci_free_consistent(hw->pdev, skge->mem_size, skge->mem, skge->dma);

	return err;
}

static int skge_down(struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;
	int port = skge->port;

	if (netif_msg_ifdown(skge))
		printk(KERN_INFO PFX "%s: disabling interface\n", dev->name);

	netif_stop_queue(dev);

	del_timer_sync(&skge->led_blink);
	del_timer_sync(&skge->link_check);

	/* Stop transmitter */
	skge_write8(hw, Q_ADDR(txqaddr[port], Q_CSR), CSR_STOP);
	skge_write32(hw, RB_ADDR(txqaddr[port], RB_CTRL),
		     RB_RST_SET|RB_DIS_OP_MD);

	if (hw->chip_id == CHIP_ID_GENESIS)
		genesis_stop(skge);
	else
		yukon_stop(skge);

	/* Disable Force Sync bit and Enable Alloc bit */
	skge_write8(hw, SKGEMAC_REG(port, TXA_CTRL),
		    TXA_DIS_FSYNC | TXA_DIS_ALLOC | TXA_STOP_RC);

	/* Stop Interval Timer and Limit Counter of Tx Arbiter */
	skge_write32(hw, SKGEMAC_REG(port, TXA_ITI_INI), 0L);
	skge_write32(hw, SKGEMAC_REG(port, TXA_LIM_INI), 0L);

	/* Reset PCI FIFO */
	skge_write32(hw, Q_ADDR(txqaddr[port], Q_CSR), CSR_SET_RESET);
	skge_write32(hw, RB_ADDR(txqaddr[port], RB_CTRL), RB_RST_SET);

	/* Reset the RAM Buffer async Tx queue */
	skge_write8(hw, RB_ADDR(port == 0 ? Q_XA1 : Q_XA2, RB_CTRL), RB_RST_SET);
	/* stop receiver */
	skge_write8(hw, Q_ADDR(rxqaddr[port], Q_CSR), CSR_STOP);
	skge_write32(hw, RB_ADDR(port ? Q_R2 : Q_R1, RB_CTRL),
		     RB_RST_SET|RB_DIS_OP_MD);
	skge_write32(hw, Q_ADDR(rxqaddr[port], Q_CSR), CSR_SET_RESET);

	if (hw->chip_id == CHIP_ID_GENESIS) {
		skge_write8(hw, SKGEMAC_REG(port, TX_MFF_CTRL2), MFF_RST_SET);
		skge_write8(hw, SKGEMAC_REG(port, RX_MFF_CTRL2), MFF_RST_SET);
		skge_write8(hw, SKGEMAC_REG(port, TX_LED_CTRL), LED_STOP);
		skge_write8(hw, SKGEMAC_REG(port, RX_LED_CTRL), LED_STOP);
	} else {
		skge_write8(hw, SKGEMAC_REG(port, RX_GMF_CTRL_T), GMF_RST_SET);
		skge_write8(hw, SKGEMAC_REG(port, TX_GMF_CTRL_T), GMF_RST_SET);
	}

	/* turn off led's */
	skge_write16(hw, B0_LED, LED_STAT_OFF);

	skge_tx_clean(skge);
	skge_rx_clean(skge);

	kfree(skge->rx_ring.start);
	kfree(skge->tx_ring.start);
	pci_free_consistent(hw->pdev, skge->mem_size, skge->mem, skge->dma);
	return 0;
}

static int skge_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;
	struct skge_ring *ring = &skge->tx_ring;
	struct skge_element *e;
	struct skge_tx_desc *td;
	int i;
	u32 control, len;
	u64 map;
	unsigned long flags;

	skb = skb_padto(skb, ETH_ZLEN);
	if (!skb)
		return NETDEV_TX_OK;

	local_irq_save(flags);
	if (!spin_trylock(&skge->tx_lock)) {
 		/* Collision - tell upper layer to requeue */
 		local_irq_restore(flags);
 		return NETDEV_TX_LOCKED;
 	}

	if (unlikely(skge->tx_avail < skb_shinfo(skb)->nr_frags +1)) {
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&skge->tx_lock, flags);

		printk(KERN_WARNING PFX "%s: ring full when queue awake!\n",
		       dev->name);
		return NETDEV_TX_BUSY;
	}

	e = ring->to_use;
	td = e->desc;
	e->skb = skb;
	len = skb_headlen(skb);
	map = pci_map_single(hw->pdev, skb->data, len, PCI_DMA_TODEVICE);
	pci_unmap_addr_set(e, mapaddr, map);
	pci_unmap_len_set(e, maplen, len);

	td->dma_lo = map;
	td->dma_hi = map >> 32;

	if (skb->ip_summed == CHECKSUM_HW) {
		const struct iphdr *ip
			= (const struct iphdr *) (skb->data + ETH_HLEN);
		int offset = skb->h.raw - skb->data;

		/* This seems backwards, but it is what the sk98lin
		 * does.  Looks like hardware is wrong?
		 */
		if (ip->protocol == IPPROTO_UDP
	            && chip_rev(hw) == 0 && hw->chip_id == CHIP_ID_YUKON)
			control = BMU_TCP_CHECK;
		else
			control = BMU_UDP_CHECK;

		td->csum_offs = 0;
		td->csum_start = offset;
		td->csum_write = offset + skb->csum;
	} else
		control = BMU_CHECK;

	if (!skb_shinfo(skb)->nr_frags) /* single buffer i.e. no fragments */
		control |= BMU_EOF| BMU_IRQ_EOF;
	else {
		struct skge_tx_desc *tf = td;

		control |= BMU_STFWD;
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			map = pci_map_page(hw->pdev, frag->page, frag->page_offset,
					   frag->size, PCI_DMA_TODEVICE);

			e = e->next;
			e->skb = NULL;
			tf = e->desc;
			tf->dma_lo = map;
			tf->dma_hi = (u64) map >> 32;
			pci_unmap_addr_set(e, mapaddr, map);
			pci_unmap_len_set(e, maplen, frag->size);

			tf->control = BMU_OWN | BMU_SW | control | frag->size;
		}
		tf->control |= BMU_EOF | BMU_IRQ_EOF;
	}
	/* Make sure all the descriptors written */
	wmb();
	td->control = BMU_OWN | BMU_SW | BMU_STF | control | len;
	wmb();

	skge_write8(hw, Q_ADDR(txqaddr[skge->port], Q_CSR), CSR_START);

	if (netif_msg_tx_queued(skge))
		printk(KERN_DEBUG "%s: tx queued, slot %td, len %d\n",
		       dev->name, e - ring->start, skb->len);

	ring->to_use = e->next;
	skge->tx_avail -= skb_shinfo(skb)->nr_frags + 1;
	if (skge->tx_avail <= MAX_SKB_FRAGS + 1) {
		pr_debug("%s: transmit queue full\n", dev->name);
		netif_stop_queue(dev);
	}

	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&skge->tx_lock, flags);

	return NETDEV_TX_OK;
}

static inline void skge_tx_free(struct skge_hw *hw, struct skge_element *e)
{
	if (e->skb) {
		pci_unmap_single(hw->pdev,
			       pci_unmap_addr(e, mapaddr),
			       pci_unmap_len(e, maplen),
			       PCI_DMA_TODEVICE);
		dev_kfree_skb_any(e->skb);
		e->skb = NULL;
	} else {
		pci_unmap_page(hw->pdev,
			       pci_unmap_addr(e, mapaddr),
			       pci_unmap_len(e, maplen),
			       PCI_DMA_TODEVICE);
	}
}

static void skge_tx_clean(struct skge_port *skge)
{
	struct skge_ring *ring = &skge->tx_ring;
	struct skge_element *e;
	unsigned long flags;

	spin_lock_irqsave(&skge->tx_lock, flags);
	for (e = ring->to_clean; e != ring->to_use; e = e->next) {
		++skge->tx_avail;
		skge_tx_free(skge->hw, e);
	}
	ring->to_clean = e;
	spin_unlock_irqrestore(&skge->tx_lock, flags);
}

static void skge_tx_timeout(struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);

	if (netif_msg_timer(skge))
		printk(KERN_DEBUG PFX "%s: tx timeout\n", dev->name);

	skge_write8(skge->hw, Q_ADDR(txqaddr[skge->port], Q_CSR), CSR_STOP);
	skge_tx_clean(skge);
}

static int skge_change_mtu(struct net_device *dev, int new_mtu)
{
	int err = 0;

	if (new_mtu < ETH_ZLEN || new_mtu > ETH_JUMBO_MTU)
		return -EINVAL;

	dev->mtu = new_mtu;

	if (netif_running(dev)) {
		skge_down(dev);
		skge_up(dev);
	}

	return err;
}

static void genesis_set_multicast(struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;
	int port = skge->port;
	int i, count = dev->mc_count;
	struct dev_mc_list *list = dev->mc_list;
	u32 mode;
	u8 filter[8];

	mode = skge_xm_read32(hw, port, XM_MODE);
	mode |= XM_MD_ENA_HASH;
	if (dev->flags & IFF_PROMISC)
		mode |= XM_MD_ENA_PROM;
	else
		mode &= ~XM_MD_ENA_PROM;

	if (dev->flags & IFF_ALLMULTI)
		memset(filter, 0xff, sizeof(filter));
	else {
		memset(filter, 0, sizeof(filter));
		for (i = 0; list && i < count; i++, list = list->next) {
			u32 crc = crc32_le(~0, list->dmi_addr, ETH_ALEN);
			u8 bit = 63 - (crc & 63);

			filter[bit/8] |= 1 << (bit%8);
		}
	}

	skge_xm_outhash(hw, port, XM_HSM, filter);

	skge_xm_write32(hw, port, XM_MODE, mode);
}

static void yukon_set_multicast(struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;
	int port = skge->port;
	struct dev_mc_list *list = dev->mc_list;
	u16 reg;
	u8 filter[8];

	memset(filter, 0, sizeof(filter));

	reg = skge_gma_read16(hw, port, GM_RX_CTRL);
	reg |= GM_RXCR_UCF_ENA;

	if (dev->flags & IFF_PROMISC) 		/* promiscious */
		reg &= ~(GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA);
	else if (dev->flags & IFF_ALLMULTI)	/* all multicast */
		memset(filter, 0xff, sizeof(filter));
	else if (dev->mc_count == 0)		/* no multicast */
		reg &= ~GM_RXCR_MCF_ENA;
	else {
		int i;
		reg |= GM_RXCR_MCF_ENA;

		for (i = 0; list && i < dev->mc_count; i++, list = list->next) {
			u32 bit = ether_crc(ETH_ALEN, list->dmi_addr) & 0x3f;
			filter[bit/8] |= 1 << (bit%8);
		}
	}


	skge_gma_write16(hw, port, GM_MC_ADDR_H1,
			 (u16)filter[0] | ((u16)filter[1] << 8));
	skge_gma_write16(hw, port, GM_MC_ADDR_H2,
			 (u16)filter[2] | ((u16)filter[3] << 8));
	skge_gma_write16(hw, port, GM_MC_ADDR_H3,
			 (u16)filter[4] | ((u16)filter[5] << 8));
	skge_gma_write16(hw, port, GM_MC_ADDR_H4,
			 (u16)filter[6] | ((u16)filter[7] << 8));

	skge_gma_write16(hw, port, GM_RX_CTRL, reg);
}

static inline int bad_phy_status(const struct skge_hw *hw, u32 status)
{
	if (hw->chip_id == CHIP_ID_GENESIS)
		return (status & (XMR_FS_ERR | XMR_FS_2L_VLAN)) != 0;
	else
		return (status & GMR_FS_ANY_ERR) ||
			(status & GMR_FS_RX_OK) == 0;
}

static void skge_rx_error(struct skge_port *skge, int slot,
			  u32 control, u32 status)
{
	if (netif_msg_rx_err(skge))
		printk(KERN_DEBUG PFX "%s: rx err, slot %d control 0x%x status 0x%x\n",
		       skge->netdev->name, slot, control, status);

	if ((control & (BMU_EOF|BMU_STF)) != (BMU_STF|BMU_EOF)
	    || (control & BMU_BBC) > skge->netdev->mtu + VLAN_ETH_HLEN)
		skge->net_stats.rx_length_errors++;
	else {
		if (skge->hw->chip_id == CHIP_ID_GENESIS) {
			if (status & (XMR_FS_RUNT|XMR_FS_LNG_ERR))
				skge->net_stats.rx_length_errors++;
			if (status & XMR_FS_FRA_ERR)
				skge->net_stats.rx_frame_errors++;
			if (status & XMR_FS_FCS_ERR)
				skge->net_stats.rx_crc_errors++;
		} else {
			if (status & (GMR_FS_LONG_ERR|GMR_FS_UN_SIZE))
				skge->net_stats.rx_length_errors++;
			if (status & GMR_FS_FRAGMENT)
				skge->net_stats.rx_frame_errors++;
			if (status & GMR_FS_CRC_ERR)
				skge->net_stats.rx_crc_errors++;
		}
	}
}

static int skge_poll(struct net_device *dev, int *budget)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;
	struct skge_ring *ring = &skge->rx_ring;
	struct skge_element *e;
	unsigned int to_do = min(dev->quota, *budget);
	unsigned int work_done = 0;
	int done;
	static const u32 irqmask[] = { IS_PORT_1, IS_PORT_2 };

	for (e = ring->to_clean; e != ring->to_use && work_done < to_do;
	     e = e->next) {
		struct skge_rx_desc *rd = e->desc;
		struct sk_buff *skb = e->skb;
		u32 control, len, status;

		rmb();
		control = rd->control;
		if (control & BMU_OWN)
			break;

		len = control & BMU_BBC;
		e->skb = NULL;

		pci_unmap_single(hw->pdev,
				 pci_unmap_addr(e, mapaddr),
				 pci_unmap_len(e, maplen),
				 PCI_DMA_FROMDEVICE);

		status = rd->status;
		if ((control & (BMU_EOF|BMU_STF)) != (BMU_STF|BMU_EOF)
		     || len > dev->mtu + VLAN_ETH_HLEN
		     || bad_phy_status(hw, status)) {
			skge_rx_error(skge, e - ring->start, control, status);
			dev_kfree_skb(skb);
			continue;
		}

		if (netif_msg_rx_status(skge))
		    printk(KERN_DEBUG PFX "%s: rx slot %td status 0x%x len %d\n",
			   dev->name, e - ring->start, rd->status, len);

		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, dev);

		if (skge->rx_csum) {
			skb->csum = le16_to_cpu(rd->csum2);
			skb->ip_summed = CHECKSUM_HW;
		}

		dev->last_rx = jiffies;
		netif_receive_skb(skb);

		++work_done;
	}
	ring->to_clean = e;

	*budget -= work_done;
	dev->quota -= work_done;
	done = work_done < to_do;

	if (skge_rx_fill(skge))
		done = 0;

	/* restart receiver */
	wmb();
	skge_write8(hw, Q_ADDR(rxqaddr[skge->port], Q_CSR),
		    CSR_START | CSR_IRQ_CL_F);

	if (done) {
		local_irq_disable();
		hw->intr_mask |= irqmask[skge->port];
		/* Order is important since data can get interrupted */
		skge_write32(hw, B0_IMSK, hw->intr_mask);
		__netif_rx_complete(dev);
		local_irq_enable();
	}

	return !done;
}

static inline void skge_tx_intr(struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);
	struct skge_hw *hw = skge->hw;
	struct skge_ring *ring = &skge->tx_ring;
	struct skge_element *e;

	spin_lock(&skge->tx_lock);
	for (e = ring->to_clean; e != ring->to_use; e = e->next) {
		struct skge_tx_desc *td = e->desc;
		u32 control;

		rmb();
		control = td->control;
		if (control & BMU_OWN)
			break;

		if (unlikely(netif_msg_tx_done(skge)))
			printk(KERN_DEBUG PFX "%s: tx done slot %td status 0x%x\n",
			       dev->name, e - ring->start, td->status);

		skge_tx_free(hw, e);
		e->skb = NULL;
		++skge->tx_avail;
	}
	ring->to_clean = e;
	skge_write8(hw, Q_ADDR(txqaddr[skge->port], Q_CSR), CSR_IRQ_CL_F);

	if (skge->tx_avail > MAX_SKB_FRAGS + 1)
		netif_wake_queue(dev);

	spin_unlock(&skge->tx_lock);
}

static void skge_mac_parity(struct skge_hw *hw, int port)
{
	printk(KERN_ERR PFX "%s: mac data parity error\n",
	       hw->dev[port] ? hw->dev[port]->name
	       : (port == 0 ? "(port A)": "(port B"));

	if (hw->chip_id == CHIP_ID_GENESIS)
		skge_write16(hw, SKGEMAC_REG(port, TX_MFF_CTRL1),
			     MFF_CLR_PERR);
	else
		/* HW-Bug #8: cleared by GMF_CLI_TX_FC instead of GMF_CLI_TX_PE */
		skge_write8(hw, SKGEMAC_REG(port, TX_GMF_CTRL_T),
			    (hw->chip_id == CHIP_ID_YUKON && chip_rev(hw) == 0)
			    ? GMF_CLI_TX_FC : GMF_CLI_TX_PE);
}

static void skge_pci_clear(struct skge_hw *hw)
{
	u16 status;

	status = skge_read16(hw, SKGEPCI_REG(PCI_STATUS));
	skge_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
	skge_write16(hw, SKGEPCI_REG(PCI_STATUS),
		     status | PCI_STATUS_ERROR_BITS);
	skge_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
}

static void skge_mac_intr(struct skge_hw *hw, int port)
{
	if (hw->chip_id == CHIP_ID_GENESIS)
		genesis_mac_intr(hw, port);
	else
		yukon_mac_intr(hw, port);
}

/* Handle device specific framing and timeout interrupts */
static void skge_error_irq(struct skge_hw *hw)
{
	u32 hwstatus = skge_read32(hw, B0_HWE_ISRC);

	if (hw->chip_id == CHIP_ID_GENESIS) {
		/* clear xmac errors */
		if (hwstatus & (IS_NO_STAT_M1|IS_NO_TIST_M1))
			skge_write16(hw, SKGEMAC_REG(0, RX_MFF_CTRL1), MFF_CLR_INSTAT);
		if (hwstatus & (IS_NO_STAT_M2|IS_NO_TIST_M2))
			skge_write16(hw, SKGEMAC_REG(0, RX_MFF_CTRL2), MFF_CLR_INSTAT);
	} else {
		/* Timestamp (unused) overflow */
		if (hwstatus & IS_IRQ_TIST_OV)
			skge_write8(hw, GMAC_TI_ST_CTRL, GMT_ST_CLR_IRQ);

		if (hwstatus & IS_IRQ_SENSOR) {
			/* no sensors on 32-bit Yukon */
			if (!(skge_read16(hw, B0_CTST) & CS_BUS_SLOT_SZ)) {
				printk(KERN_ERR PFX "ignoring bogus sensor interrups\n");
				skge_write32(hw, B0_HWE_IMSK,
					     IS_ERR_MSK & ~IS_IRQ_SENSOR);
			} else
				printk(KERN_WARNING PFX "sensor interrupt\n");
		}


	}

	if (hwstatus & IS_RAM_RD_PAR) {
		printk(KERN_ERR PFX "Ram read data parity error\n");
		skge_write16(hw, B3_RI_CTRL, RI_CLR_RD_PERR);
	}

	if (hwstatus & IS_RAM_WR_PAR) {
		printk(KERN_ERR PFX "Ram write data parity error\n");
		skge_write16(hw, B3_RI_CTRL, RI_CLR_WR_PERR);
	}

	if (hwstatus & IS_M1_PAR_ERR)
		skge_mac_parity(hw, 0);

	if (hwstatus & IS_M2_PAR_ERR)
		skge_mac_parity(hw, 1);

	if (hwstatus & IS_R1_PAR_ERR)
		skge_write32(hw, B0_R1_CSR, CSR_IRQ_CL_P);

	if (hwstatus & IS_R2_PAR_ERR)
		skge_write32(hw, B0_R2_CSR, CSR_IRQ_CL_P);

	if (hwstatus & (IS_IRQ_MST_ERR|IS_IRQ_STAT)) {
		printk(KERN_ERR PFX "hardware error detected (status 0x%x)\n",
		       hwstatus);

		skge_pci_clear(hw);

		hwstatus = skge_read32(hw, B0_HWE_ISRC);
		if (hwstatus & IS_IRQ_STAT) {
			printk(KERN_WARNING PFX "IRQ status %x: still set ignoring hardware errors\n",
			       hwstatus);
			hw->intr_mask &= ~IS_HW_ERR;
		}
	}
}

/*
 * Interrrupt from PHY are handled in tasklet (soft irq)
 * because accessing phy registers requires spin wait which might
 * cause excess interrupt latency.
 */
static void skge_extirq(unsigned long data)
{
	struct skge_hw *hw = (struct skge_hw *) data;
	int port;

	spin_lock(&hw->phy_lock);
	for (port = 0; port < 2; port++) {
		struct net_device *dev = hw->dev[port];

		if (dev && netif_running(dev)) {
			struct skge_port *skge = netdev_priv(dev);

			if (hw->chip_id != CHIP_ID_GENESIS)
				yukon_phy_intr(skge);
			else if (hw->phy_type == SK_PHY_BCOM)
				genesis_bcom_intr(skge);
		}
	}
	spin_unlock(&hw->phy_lock);

	local_irq_disable();
	hw->intr_mask |= IS_EXT_REG;
	skge_write32(hw, B0_IMSK, hw->intr_mask);
	local_irq_enable();
}

static irqreturn_t skge_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct skge_hw *hw = dev_id;
	u32 status = skge_read32(hw, B0_SP_ISRC);

	if (status == 0 || status == ~0) /* hotplug or shared irq */
		return IRQ_NONE;

	status &= hw->intr_mask;

	if ((status & IS_R1_F) && netif_rx_schedule_prep(hw->dev[0])) {
		status &= ~IS_R1_F;
		hw->intr_mask &= ~IS_R1_F;
		skge_write32(hw, B0_IMSK, hw->intr_mask);
		__netif_rx_schedule(hw->dev[0]);
	}

	if ((status & IS_R2_F) && netif_rx_schedule_prep(hw->dev[1])) {
		status &= ~IS_R2_F;
		hw->intr_mask &= ~IS_R2_F;
		skge_write32(hw, B0_IMSK, hw->intr_mask);
		__netif_rx_schedule(hw->dev[1]);
	}

	if (status & IS_XA1_F)
		skge_tx_intr(hw->dev[0]);

	if (status & IS_XA2_F)
		skge_tx_intr(hw->dev[1]);

	if (status & IS_MAC1)
		skge_mac_intr(hw, 0);

	if (status & IS_MAC2)
		skge_mac_intr(hw, 1);

	if (status & IS_HW_ERR)
		skge_error_irq(hw);

	if (status & IS_EXT_REG) {
		hw->intr_mask &= ~IS_EXT_REG;
		tasklet_schedule(&hw->ext_tasklet);
	}

	if (status)
		skge_write32(hw, B0_IMSK, hw->intr_mask);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void skge_netpoll(struct net_device *dev)
{
	struct skge_port *skge = netdev_priv(dev);

	disable_irq(dev->irq);
	skge_intr(dev->irq, skge->hw, NULL);
	enable_irq(dev->irq);
}
#endif

static int skge_set_mac_address(struct net_device *dev, void *p)
{
	struct skge_port *skge = netdev_priv(dev);
	struct sockaddr *addr = p;
	int err = 0;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	skge_down(dev);
	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	memcpy_toio(skge->hw->regs + B2_MAC_1 + skge->port*8,
		    dev->dev_addr, ETH_ALEN);
	memcpy_toio(skge->hw->regs + B2_MAC_2 + skge->port*8,
		    dev->dev_addr, ETH_ALEN);
	if (dev->flags & IFF_UP)
		err = skge_up(dev);
	return err;
}

static const struct {
	u8 id;
	const char *name;
} skge_chips[] = {
	{ CHIP_ID_GENESIS,	"Genesis" },
	{ CHIP_ID_YUKON,	 "Yukon" },
	{ CHIP_ID_YUKON_LITE,	 "Yukon-Lite"},
	{ CHIP_ID_YUKON_LP,	 "Yukon-LP"},
	{ CHIP_ID_YUKON_XL,	 "Yukon-2 XL"},
	{ CHIP_ID_YUKON_EC,	 "YUKON-2 EC"},
	{ CHIP_ID_YUKON_FE,	 "YUKON-2 FE"},
};

static const char *skge_board_name(const struct skge_hw *hw)
{
	int i;
	static char buf[16];

	for (i = 0; i < ARRAY_SIZE(skge_chips); i++)
		if (skge_chips[i].id == hw->chip_id)
			return skge_chips[i].name;

	snprintf(buf, sizeof buf, "chipid 0x%x", hw->chip_id);
	return buf;
}


/*
 * Setup the board data structure, but don't bring up
 * the port(s)
 */
static int skge_reset(struct skge_hw *hw)
{
	u16 ctst;
	u8 t8;
	int i, ports;

	ctst = skge_read16(hw, B0_CTST);

	/* do a SW reset */
	skge_write8(hw, B0_CTST, CS_RST_SET);
	skge_write8(hw, B0_CTST, CS_RST_CLR);

	/* clear PCI errors, if any */
	skge_pci_clear(hw);

	skge_write8(hw, B0_CTST, CS_MRST_CLR);

	/* restore CLK_RUN bits (for Yukon-Lite) */
	skge_write16(hw, B0_CTST,
		     ctst & (CS_CLK_RUN_HOT|CS_CLK_RUN_RST|CS_CLK_RUN_ENA));

	hw->chip_id = skge_read8(hw, B2_CHIP_ID);
	hw->phy_type = skge_read8(hw, B2_E_1) & 0xf;
	hw->pmd_type = skge_read8(hw, B2_PMD_TYP);

	switch (hw->chip_id) {
	case CHIP_ID_GENESIS:
		switch (hw->phy_type) {
		case SK_PHY_XMAC:
			hw->phy_addr = PHY_ADDR_XMAC;
			break;
		case SK_PHY_BCOM:
			hw->phy_addr = PHY_ADDR_BCOM;
			break;
		default:
			printk(KERN_ERR PFX "%s: unsupported phy type 0x%x\n",
			       pci_name(hw->pdev), hw->phy_type);
			return -EOPNOTSUPP;
		}
		break;

	case CHIP_ID_YUKON:
	case CHIP_ID_YUKON_LITE:
	case CHIP_ID_YUKON_LP:
		if (hw->phy_type < SK_PHY_MARV_COPPER && hw->pmd_type != 'S')
			hw->phy_type = SK_PHY_MARV_COPPER;

		hw->phy_addr = PHY_ADDR_MARV;
		if (!iscopper(hw))
			hw->phy_type = SK_PHY_MARV_FIBER;

		break;

	default:
		printk(KERN_ERR PFX "%s: unsupported chip type 0x%x\n",
		       pci_name(hw->pdev), hw->chip_id);
		return -EOPNOTSUPP;
	}

	hw->mac_cfg = skge_read8(hw, B2_MAC_CFG);
	ports = isdualport(hw) ? 2 : 1;

	/* read the adapters RAM size */
	t8 = skge_read8(hw, B2_E_0);
	if (hw->chip_id == CHIP_ID_GENESIS) {
		if (t8 == 3) {
			/* special case: 4 x 64k x 36, offset = 0x80000 */
			hw->ram_size = 0x100000;
			hw->ram_offset = 0x80000;
		} else
			hw->ram_size = t8 * 512;
	}
	else if (t8 == 0)
		hw->ram_size = 0x20000;
	else
		hw->ram_size = t8 * 4096;

	if (hw->chip_id == CHIP_ID_GENESIS)
		genesis_init(hw);
	else {
		/* switch power to VCC (WA for VAUX problem) */
		skge_write8(hw, B0_POWER_CTRL,
			    PC_VAUX_ENA | PC_VCC_ENA | PC_VAUX_OFF | PC_VCC_ON);
		for (i = 0; i < ports; i++) {
			skge_write16(hw, SKGEMAC_REG(i, GMAC_LINK_CTRL), GMLC_RST_SET);
			skge_write16(hw, SKGEMAC_REG(i, GMAC_LINK_CTRL), GMLC_RST_CLR);
		}
	}

	/* turn off hardware timer (unused) */
	skge_write8(hw, B2_TI_CTRL, TIM_STOP);
	skge_write8(hw, B2_TI_CTRL, TIM_CLR_IRQ);
	skge_write8(hw, B0_LED, LED_STAT_ON);

	/* enable the Tx Arbiters */
	for (i = 0; i < ports; i++)
		skge_write8(hw, SKGEMAC_REG(i, TXA_CTRL), TXA_ENA_ARB);

	/* Initialize ram interface */
	skge_write16(hw, B3_RI_CTRL, RI_RST_CLR);

	skge_write8(hw, B3_RI_WTO_R1, SK_RI_TO_53);
	skge_write8(hw, B3_RI_WTO_XA1, SK_RI_TO_53);
	skge_write8(hw, B3_RI_WTO_XS1, SK_RI_TO_53);
	skge_write8(hw, B3_RI_RTO_R1, SK_RI_TO_53);
	skge_write8(hw, B3_RI_RTO_XA1, SK_RI_TO_53);
	skge_write8(hw, B3_RI_RTO_XS1, SK_RI_TO_53);
	skge_write8(hw, B3_RI_WTO_R2, SK_RI_TO_53);
	skge_write8(hw, B3_RI_WTO_XA2, SK_RI_TO_53);
	skge_write8(hw, B3_RI_WTO_XS2, SK_RI_TO_53);
	skge_write8(hw, B3_RI_RTO_R2, SK_RI_TO_53);
	skge_write8(hw, B3_RI_RTO_XA2, SK_RI_TO_53);
	skge_write8(hw, B3_RI_RTO_XS2, SK_RI_TO_53);

	skge_write32(hw, B0_HWE_IMSK, IS_ERR_MSK);

	/* Set interrupt moderation for Transmit only
	 * Receive interrupts avoided by NAPI
	 */
	skge_write32(hw, B2_IRQM_MSK, IS_XA1_F|IS_XA2_F);
	skge_write32(hw, B2_IRQM_INI, skge_usecs2clk(hw, 100));
	skge_write32(hw, B2_IRQM_CTRL, TIM_START);

	hw->intr_mask = IS_HW_ERR | IS_EXT_REG | IS_PORT_1;
	if (isdualport(hw))
		hw->intr_mask |= IS_PORT_2;
	skge_write32(hw, B0_IMSK, hw->intr_mask);

	if (hw->chip_id != CHIP_ID_GENESIS)
		skge_write8(hw, GMAC_IRQ_MSK, 0);

	spin_lock_bh(&hw->phy_lock);
	for (i = 0; i < ports; i++) {
		if (hw->chip_id == CHIP_ID_GENESIS)
			genesis_reset(hw, i);
		else
			yukon_reset(hw, i);
	}
	spin_unlock_bh(&hw->phy_lock);

	return 0;
}

/* Initialize network device */
static struct net_device *skge_devinit(struct skge_hw *hw, int port)
{
	struct skge_port *skge;
	struct net_device *dev = alloc_etherdev(sizeof(*skge));

	if (!dev) {
		printk(KERN_ERR "skge etherdev alloc failed");
		return NULL;
	}

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &hw->pdev->dev);
	dev->open = skge_up;
	dev->stop = skge_down;
	dev->hard_start_xmit = skge_xmit_frame;
	dev->get_stats = skge_get_stats;
	if (hw->chip_id == CHIP_ID_GENESIS)
		dev->set_multicast_list = genesis_set_multicast;
	else
		dev->set_multicast_list = yukon_set_multicast;

	dev->set_mac_address = skge_set_mac_address;
	dev->change_mtu = skge_change_mtu;
	SET_ETHTOOL_OPS(dev, &skge_ethtool_ops);
	dev->tx_timeout = skge_tx_timeout;
	dev->watchdog_timeo = TX_WATCHDOG;
	dev->poll = skge_poll;
	dev->weight = NAPI_WEIGHT;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = skge_netpoll;
#endif
	dev->irq = hw->pdev->irq;
	dev->features = NETIF_F_LLTX;

	skge = netdev_priv(dev);
	skge->netdev = dev;
	skge->hw = hw;
	skge->msg_enable = netif_msg_init(debug, default_msg);
	skge->tx_ring.count = DEFAULT_TX_RING_SIZE;
	skge->rx_ring.count = DEFAULT_RX_RING_SIZE;

	/* Auto speed and flow control */
	skge->autoneg = AUTONEG_ENABLE;
	skge->flow_control = FLOW_MODE_SYMMETRIC;
	skge->duplex = -1;
	skge->speed = -1;
	skge->advertising = skge_modes(hw);

	hw->dev[port] = dev;

	skge->port = port;

	spin_lock_init(&skge->tx_lock);

	init_timer(&skge->link_check);
	skge->link_check.function = skge_link_timer;
	skge->link_check.data = (unsigned long) skge;

	init_timer(&skge->led_blink);
	skge->led_blink.function = skge_blink_timer;
	skge->led_blink.data = (unsigned long) skge;

	if (hw->chip_id != CHIP_ID_GENESIS) {
		dev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
		skge->rx_csum = 1;
	}

	/* read the mac address */
	memcpy_fromio(dev->dev_addr, hw->regs + B2_MAC_1 + port*8, ETH_ALEN);

	/* device is off until link detection */
	netif_carrier_off(dev);
	netif_stop_queue(dev);

	return dev;
}

static void __devinit skge_show_addr(struct net_device *dev)
{
	const struct skge_port *skge = netdev_priv(dev);

	if (netif_msg_probe(skge))
		printk(KERN_INFO PFX "%s: addr %02x:%02x:%02x:%02x:%02x:%02x\n",
		       dev->name,
		       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		       dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
}

static int __devinit skge_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct net_device *dev, *dev1;
	struct skge_hw *hw;
	int err, using_dac = 0;

	if ((err = pci_enable_device(pdev))) {
		printk(KERN_ERR PFX "%s cannot enable PCI device\n",
		       pci_name(pdev));
		goto err_out;
	}

	if ((err = pci_request_regions(pdev, DRV_NAME))) {
		printk(KERN_ERR PFX "%s cannot obtain PCI resources\n",
		       pci_name(pdev));
		goto err_out_disable_pdev;
	}

	pci_set_master(pdev);

	if (!(err = pci_set_dma_mask(pdev, DMA_64BIT_MASK)))
		using_dac = 1;
	else if (!(err = pci_set_dma_mask(pdev, DMA_32BIT_MASK))) {
		printk(KERN_ERR PFX "%s no usable DMA configuration\n",
		       pci_name(pdev));
		goto err_out_free_regions;
	}

#ifdef __BIG_ENDIAN
	/* byte swap decriptors in hardware */
	{
		u32 reg;

		pci_read_config_dword(pdev, PCI_DEV_REG2, &reg);
		reg |= PCI_REV_DESC;
		pci_write_config_dword(pdev, PCI_DEV_REG2, reg);
	}
#endif

	err = -ENOMEM;
	hw = kmalloc(sizeof(*hw), GFP_KERNEL);
	if (!hw) {
		printk(KERN_ERR PFX "%s: cannot allocate hardware struct\n",
		       pci_name(pdev));
		goto err_out_free_regions;
	}

	memset(hw, 0, sizeof(*hw));
	hw->pdev = pdev;
	spin_lock_init(&hw->phy_lock);
	tasklet_init(&hw->ext_tasklet, skge_extirq, (unsigned long) hw);

	hw->regs = ioremap_nocache(pci_resource_start(pdev, 0), 0x4000);
	if (!hw->regs) {
		printk(KERN_ERR PFX "%s: cannot map device registers\n",
		       pci_name(pdev));
		goto err_out_free_hw;
	}

	if ((err = request_irq(pdev->irq, skge_intr, SA_SHIRQ, DRV_NAME, hw))) {
		printk(KERN_ERR PFX "%s: cannot assign irq %d\n",
		       pci_name(pdev), pdev->irq);
		goto err_out_iounmap;
	}
	pci_set_drvdata(pdev, hw);

	err = skge_reset(hw);
	if (err)
		goto err_out_free_irq;

	printk(KERN_INFO PFX "addr 0x%lx irq %d chip %s rev %d\n",
	       pci_resource_start(pdev, 0), pdev->irq,
	       skge_board_name(hw), chip_rev(hw));

	if ((dev = skge_devinit(hw, 0)) == NULL)
		goto err_out_led_off;

	if (using_dac)
		dev->features |= NETIF_F_HIGHDMA;

	if ((err = register_netdev(dev))) {
		printk(KERN_ERR PFX "%s: cannot register net device\n",
		       pci_name(pdev));
		goto err_out_free_netdev;
	}

	skge_show_addr(dev);

	if (isdualport(hw) && (dev1 = skge_devinit(hw, 1))) {
		if (using_dac)
			dev1->features |= NETIF_F_HIGHDMA;

		if (register_netdev(dev1) == 0)
			skge_show_addr(dev1);
		else {
			/* Failure to register second port need not be fatal */
			printk(KERN_WARNING PFX "register of second port failed\n");
			hw->dev[1] = NULL;
			free_netdev(dev1);
		}
	}

	return 0;

err_out_free_netdev:
	free_netdev(dev);
err_out_led_off:
	skge_write16(hw, B0_LED, LED_STAT_OFF);
err_out_free_irq:
	free_irq(pdev->irq, hw);
err_out_iounmap:
	iounmap(hw->regs);
err_out_free_hw:
	kfree(hw);
err_out_free_regions:
	pci_release_regions(pdev);
err_out_disable_pdev:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
err_out:
	return err;
}

static void __devexit skge_remove(struct pci_dev *pdev)
{
	struct skge_hw *hw  = pci_get_drvdata(pdev);
	struct net_device *dev0, *dev1;

	if (!hw)
		return;

	if ((dev1 = hw->dev[1]))
		unregister_netdev(dev1);
	dev0 = hw->dev[0];
	unregister_netdev(dev0);

	tasklet_kill(&hw->ext_tasklet);

	free_irq(pdev->irq, hw);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	if (dev1)
		free_netdev(dev1);
	free_netdev(dev0);
	skge_write16(hw, B0_LED, LED_STAT_OFF);
	iounmap(hw->regs);
	kfree(hw);
	pci_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM
static int skge_suspend(struct pci_dev *pdev, u32 state)
{
	struct skge_hw *hw  = pci_get_drvdata(pdev);
	int i, wol = 0;

	for (i = 0; i < 2; i++) {
		struct net_device *dev = hw->dev[i];

		if (dev) {
			struct skge_port *skge = netdev_priv(dev);
			if (netif_running(dev)) {
				netif_carrier_off(dev);
				skge_down(dev);
			}
			netif_device_detach(dev);
			wol |= skge->wol;
		}
	}

	pci_save_state(pdev);
	pci_enable_wake(pdev, state, wol);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int skge_resume(struct pci_dev *pdev)
{
	struct skge_hw *hw  = pci_get_drvdata(pdev);
	int i;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_enable_wake(pdev, PCI_D0, 0);

	skge_reset(hw);

	for (i = 0; i < 2; i++) {
		struct net_device *dev = hw->dev[i];
		if (dev) {
			netif_device_attach(dev);
			if (netif_running(dev))
				skge_up(dev);
		}
	}
	return 0;
}
#endif

static struct pci_driver skge_driver = {
	.name =         DRV_NAME,
	.id_table =     skge_id_table,
	.probe =        skge_probe,
	.remove =       __devexit_p(skge_remove),
#ifdef CONFIG_PM
	.suspend = 	skge_suspend,
	.resume = 	skge_resume,
#endif
};

static int __init skge_init_module(void)
{
	return pci_module_init(&skge_driver);
}

static void __exit skge_cleanup_module(void)
{
	pci_unregister_driver(&skge_driver);
}

module_init(skge_init_module);
module_exit(skge_cleanup_module);
