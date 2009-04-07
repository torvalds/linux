/*
 * Copyright (C) 2003 - 2009 NetXen, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 *
 * Contact Information:
 *    info@netxen.com
 * NetXen Inc,
 * 18922 Forge Drive
 * Cupertino, CA 95014-0701
 *
 */

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>

#include "netxen_nic.h"
#include "netxen_nic_hw.h"
#include "netxen_nic_phan_reg.h"

struct netxen_nic_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define NETXEN_NIC_STAT(m) sizeof(((struct netxen_adapter *)0)->m), \
			offsetof(struct netxen_adapter, m)

#define NETXEN_NIC_PORT_WINDOW 0x10000
#define NETXEN_NIC_INVALID_DATA 0xDEADBEEF

static const struct netxen_nic_stats netxen_nic_gstrings_stats[] = {
	{"xmit_called", NETXEN_NIC_STAT(stats.xmitcalled)},
	{"xmit_finished", NETXEN_NIC_STAT(stats.xmitfinished)},
	{"rx_dropped", NETXEN_NIC_STAT(stats.rxdropped)},
	{"tx_dropped", NETXEN_NIC_STAT(stats.txdropped)},
	{"csummed", NETXEN_NIC_STAT(stats.csummed)},
	{"no_rcv", NETXEN_NIC_STAT(stats.no_rcv)},
	{"rx_bytes", NETXEN_NIC_STAT(stats.rxbytes)},
	{"tx_bytes", NETXEN_NIC_STAT(stats.txbytes)},
};

#define NETXEN_NIC_STATS_LEN	ARRAY_SIZE(netxen_nic_gstrings_stats)

static const char netxen_nic_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register_Test_on_offline",
	"Link_Test_on_offline"
};

#define NETXEN_NIC_TEST_LEN	ARRAY_SIZE(netxen_nic_gstrings_test)

#define NETXEN_NIC_REGS_COUNT 42
#define NETXEN_NIC_REGS_LEN (NETXEN_NIC_REGS_COUNT * sizeof(__le32))
#define NETXEN_MAX_EEPROM_LEN   1024

static int netxen_nic_get_eeprom_len(struct net_device *dev)
{
	return NETXEN_FLASH_TOTAL_SIZE;
}

static void
netxen_nic_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *drvinfo)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	unsigned long flags;
	u32 fw_major = 0;
	u32 fw_minor = 0;
	u32 fw_build = 0;

	strncpy(drvinfo->driver, netxen_nic_driver_name, 32);
	strncpy(drvinfo->version, NETXEN_NIC_LINUX_VERSIONID, 32);
	write_lock_irqsave(&adapter->adapter_lock, flags);
	fw_major = adapter->pci_read_normalize(adapter,
					NETXEN_FW_VERSION_MAJOR);
	fw_minor = adapter->pci_read_normalize(adapter,
					NETXEN_FW_VERSION_MINOR);
	fw_build = adapter->pci_read_normalize(adapter,
					NETXEN_FW_VERSION_SUB);
	write_unlock_irqrestore(&adapter->adapter_lock, flags);
	sprintf(drvinfo->fw_version, "%d.%d.%d", fw_major, fw_minor, fw_build);

	strncpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
	drvinfo->regdump_len = NETXEN_NIC_REGS_LEN;
	drvinfo->eedump_len = netxen_nic_get_eeprom_len(dev);
}

static int
netxen_nic_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct netxen_adapter *adapter = netdev_priv(dev);

	/* read which mode */
	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		ecmd->supported = (SUPPORTED_10baseT_Half |
				   SUPPORTED_10baseT_Full |
				   SUPPORTED_100baseT_Half |
				   SUPPORTED_100baseT_Full |
				   SUPPORTED_1000baseT_Half |
				   SUPPORTED_1000baseT_Full);

		ecmd->advertising = (ADVERTISED_100baseT_Half |
				     ADVERTISED_100baseT_Full |
				     ADVERTISED_1000baseT_Half |
				     ADVERTISED_1000baseT_Full);

		ecmd->port = PORT_TP;

		ecmd->speed = adapter->link_speed;
		ecmd->duplex = adapter->link_duplex;
		ecmd->autoneg = adapter->link_autoneg;

	} else if (adapter->ahw.port_type == NETXEN_NIC_XGBE) {
		u32 val;

		adapter->hw_read_wx(adapter, NETXEN_PORT_MODE_ADDR, &val, 4);
		if (val == NETXEN_PORT_MODE_802_3_AP) {
			ecmd->supported = SUPPORTED_1000baseT_Full;
			ecmd->advertising = ADVERTISED_1000baseT_Full;
		} else {
			ecmd->supported = SUPPORTED_10000baseT_Full;
			ecmd->advertising = ADVERTISED_10000baseT_Full;
		}

		ecmd->port = PORT_TP;

		if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
			u16 pcifn = adapter->ahw.pci_func;

			adapter->hw_read_wx(adapter,
				P3_LINK_SPEED_REG(pcifn), &val, 4);
			ecmd->speed = P3_LINK_SPEED_MHZ *
					P3_LINK_SPEED_VAL(pcifn, val);
		} else
			ecmd->speed = SPEED_10000;

		ecmd->duplex = DUPLEX_FULL;
		ecmd->autoneg = AUTONEG_DISABLE;
	} else
		return -EIO;

	ecmd->phy_address = adapter->physical_port;
	ecmd->transceiver = XCVR_EXTERNAL;

	switch (adapter->ahw.board_type) {
	case NETXEN_BRDTYPE_P2_SB35_4G:
	case NETXEN_BRDTYPE_P2_SB31_2G:
	case NETXEN_BRDTYPE_P3_REF_QG:
	case NETXEN_BRDTYPE_P3_4_GB:
	case NETXEN_BRDTYPE_P3_4_GB_MM:

		ecmd->supported |= SUPPORTED_Autoneg;
		ecmd->advertising |= ADVERTISED_Autoneg;
	case NETXEN_BRDTYPE_P2_SB31_10G_CX4:
	case NETXEN_BRDTYPE_P3_10G_CX4:
	case NETXEN_BRDTYPE_P3_10G_CX4_LP:
	case NETXEN_BRDTYPE_P3_10000_BASE_T:
		ecmd->supported |= SUPPORTED_TP;
		ecmd->advertising |= ADVERTISED_TP;
		ecmd->port = PORT_TP;
		ecmd->autoneg = (adapter->ahw.board_type ==
				 NETXEN_BRDTYPE_P2_SB31_10G_CX4) ?
		    (AUTONEG_DISABLE) : (adapter->link_autoneg);
		break;
	case NETXEN_BRDTYPE_P2_SB31_10G_HMEZ:
	case NETXEN_BRDTYPE_P2_SB31_10G_IMEZ:
	case NETXEN_BRDTYPE_P3_IMEZ:
	case NETXEN_BRDTYPE_P3_XG_LOM:
	case NETXEN_BRDTYPE_P3_HMEZ:
		ecmd->supported |= SUPPORTED_MII;
		ecmd->advertising |= ADVERTISED_MII;
		ecmd->port = PORT_FIBRE;
		ecmd->autoneg = AUTONEG_DISABLE;
		break;
	case NETXEN_BRDTYPE_P3_10G_SFP_PLUS:
	case NETXEN_BRDTYPE_P3_10G_SFP_CT:
	case NETXEN_BRDTYPE_P3_10G_SFP_QT:
		ecmd->advertising |= ADVERTISED_TP;
		ecmd->supported |= SUPPORTED_TP;
	case NETXEN_BRDTYPE_P2_SB31_10G:
	case NETXEN_BRDTYPE_P3_10G_XFP:
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_FIBRE;
		ecmd->autoneg = AUTONEG_DISABLE;
		break;
	case NETXEN_BRDTYPE_P3_10G_TP:
		if (adapter->ahw.port_type == NETXEN_NIC_XGBE) {
			ecmd->autoneg = AUTONEG_DISABLE;
			ecmd->supported |= (SUPPORTED_FIBRE | SUPPORTED_TP);
			ecmd->advertising |=
				(ADVERTISED_FIBRE | ADVERTISED_TP);
			ecmd->port = PORT_FIBRE;
		} else {
			ecmd->autoneg = AUTONEG_ENABLE;
			ecmd->supported |= (SUPPORTED_TP |SUPPORTED_Autoneg);
			ecmd->advertising |=
				(ADVERTISED_TP | ADVERTISED_Autoneg);
			ecmd->port = PORT_TP;
		}
		break;
	default:
		printk(KERN_ERR "netxen-nic: Unsupported board model %d\n",
				adapter->ahw.board_type);
		return -EIO;
	}

	return 0;
}

static int
netxen_nic_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	__u32 status;

	/* read which mode */
	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		/* autonegotiation */
		if (adapter->phy_write
		    && adapter->phy_write(adapter,
					  NETXEN_NIU_GB_MII_MGMT_ADDR_AUTONEG,
					  ecmd->autoneg) != 0)
			return -EIO;
		else
			adapter->link_autoneg = ecmd->autoneg;

		if (adapter->phy_read
		    && adapter->phy_read(adapter,
					 NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
					 &status) != 0)
			return -EIO;

		/* speed */
		switch (ecmd->speed) {
		case SPEED_10:
			netxen_set_phy_speed(status, 0);
			break;
		case SPEED_100:
			netxen_set_phy_speed(status, 1);
			break;
		case SPEED_1000:
			netxen_set_phy_speed(status, 2);
			break;
		}
		/* set duplex mode */
		if (ecmd->duplex == DUPLEX_HALF)
			netxen_clear_phy_duplex(status);
		if (ecmd->duplex == DUPLEX_FULL)
			netxen_set_phy_duplex(status);
		if (adapter->phy_write
		    && adapter->phy_write(adapter,
					  NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
					  *((int *)&status)) != 0)
			return -EIO;
		else {
			adapter->link_speed = ecmd->speed;
			adapter->link_duplex = ecmd->duplex;
		}
	} else
		return -EOPNOTSUPP;

	if (!netif_running(dev))
		return 0;

	dev->netdev_ops->ndo_stop(dev);
	return dev->netdev_ops->ndo_open(dev);
}

static int netxen_nic_get_regs_len(struct net_device *dev)
{
	return NETXEN_NIC_REGS_LEN;
}

struct netxen_niu_regs {
	__u32 reg[NETXEN_NIC_REGS_COUNT];
};

static struct netxen_niu_regs niu_registers[] = {
	{
	 /* GB Mode */
	 {
	  NETXEN_NIU_GB_SERDES_RESET,
	  NETXEN_NIU_GB0_MII_MODE,
	  NETXEN_NIU_GB1_MII_MODE,
	  NETXEN_NIU_GB2_MII_MODE,
	  NETXEN_NIU_GB3_MII_MODE,
	  NETXEN_NIU_GB0_GMII_MODE,
	  NETXEN_NIU_GB1_GMII_MODE,
	  NETXEN_NIU_GB2_GMII_MODE,
	  NETXEN_NIU_GB3_GMII_MODE,
	  NETXEN_NIU_REMOTE_LOOPBACK,
	  NETXEN_NIU_GB0_HALF_DUPLEX,
	  NETXEN_NIU_GB1_HALF_DUPLEX,
	  NETXEN_NIU_RESET_SYS_FIFOS,
	  NETXEN_NIU_GB_CRC_DROP,
	  NETXEN_NIU_GB_DROP_WRONGADDR,
	  NETXEN_NIU_TEST_MUX_CTL,

	  NETXEN_NIU_GB_MAC_CONFIG_0(0),
	  NETXEN_NIU_GB_MAC_CONFIG_1(0),
	  NETXEN_NIU_GB_HALF_DUPLEX_CTRL(0),
	  NETXEN_NIU_GB_MAX_FRAME_SIZE(0),
	  NETXEN_NIU_GB_TEST_REG(0),
	  NETXEN_NIU_GB_MII_MGMT_CONFIG(0),
	  NETXEN_NIU_GB_MII_MGMT_COMMAND(0),
	  NETXEN_NIU_GB_MII_MGMT_ADDR(0),
	  NETXEN_NIU_GB_MII_MGMT_CTRL(0),
	  NETXEN_NIU_GB_MII_MGMT_STATUS(0),
	  NETXEN_NIU_GB_MII_MGMT_INDICATE(0),
	  NETXEN_NIU_GB_INTERFACE_CTRL(0),
	  NETXEN_NIU_GB_INTERFACE_STATUS(0),
	  NETXEN_NIU_GB_STATION_ADDR_0(0),
	  NETXEN_NIU_GB_STATION_ADDR_1(0),
	  -1,
	  }
	 },
	{
	 /* XG Mode */
	 {
	  NETXEN_NIU_XG_SINGLE_TERM,
	  NETXEN_NIU_XG_DRIVE_HI,
	  NETXEN_NIU_XG_DRIVE_LO,
	  NETXEN_NIU_XG_DTX,
	  NETXEN_NIU_XG_DEQ,
	  NETXEN_NIU_XG_WORD_ALIGN,
	  NETXEN_NIU_XG_RESET,
	  NETXEN_NIU_XG_POWER_DOWN,
	  NETXEN_NIU_XG_RESET_PLL,
	  NETXEN_NIU_XG_SERDES_LOOPBACK,
	  NETXEN_NIU_XG_DO_BYTE_ALIGN,
	  NETXEN_NIU_XG_TX_ENABLE,
	  NETXEN_NIU_XG_RX_ENABLE,
	  NETXEN_NIU_XG_STATUS,
	  NETXEN_NIU_XG_PAUSE_THRESHOLD,
	  NETXEN_NIU_XGE_CONFIG_0,
	  NETXEN_NIU_XGE_CONFIG_1,
	  NETXEN_NIU_XGE_IPG,
	  NETXEN_NIU_XGE_STATION_ADDR_0_HI,
	  NETXEN_NIU_XGE_STATION_ADDR_0_1,
	  NETXEN_NIU_XGE_STATION_ADDR_1_LO,
	  NETXEN_NIU_XGE_STATUS,
	  NETXEN_NIU_XGE_MAX_FRAME_SIZE,
	  NETXEN_NIU_XGE_PAUSE_FRAME_VALUE,
	  NETXEN_NIU_XGE_TX_BYTE_CNT,
	  NETXEN_NIU_XGE_TX_FRAME_CNT,
	  NETXEN_NIU_XGE_RX_BYTE_CNT,
	  NETXEN_NIU_XGE_RX_FRAME_CNT,
	  NETXEN_NIU_XGE_AGGR_ERROR_CNT,
	  NETXEN_NIU_XGE_MULTICAST_FRAME_CNT,
	  NETXEN_NIU_XGE_UNICAST_FRAME_CNT,
	  NETXEN_NIU_XGE_CRC_ERROR_CNT,
	  NETXEN_NIU_XGE_OVERSIZE_FRAME_ERR,
	  NETXEN_NIU_XGE_UNDERSIZE_FRAME_ERR,
	  NETXEN_NIU_XGE_LOCAL_ERROR_CNT,
	  NETXEN_NIU_XGE_REMOTE_ERROR_CNT,
	  NETXEN_NIU_XGE_CONTROL_CHAR_CNT,
	  NETXEN_NIU_XGE_PAUSE_FRAME_CNT,
	  -1,
	  }
	 }
};

static void
netxen_nic_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *p)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	__u32 mode, *regs_buff = p;
	int i, window;

	memset(p, 0, NETXEN_NIC_REGS_LEN);
	regs->version = (1 << 24) | (adapter->ahw.revision_id << 16) |
	    (adapter->pdev)->device;
	/* which mode */
	adapter->hw_read_wx(adapter, NETXEN_NIU_MODE, &regs_buff[0], 4);
	mode = regs_buff[0];

	/* Common registers to all the modes */
	adapter->hw_read_wx(adapter,
			NETXEN_NIU_STRAP_VALUE_SAVE_HIGHER, &regs_buff[2], 4);
	/* GB/XGB Mode */
	mode = (mode / 2) - 1;
	window = 0;
	if (mode <= 1) {
		for (i = 3; niu_registers[mode].reg[i - 3] != -1; i++) {
			/* GB: port specific registers */
			if (mode == 0 && i >= 19)
				window = adapter->physical_port *
					NETXEN_NIC_PORT_WINDOW;

			adapter->hw_read_wx(adapter,
				niu_registers[mode].reg[i - 3] + window,
				&regs_buff[i], 4);
		}

	}
}

static u32 netxen_nic_test_link(struct net_device *dev)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	__u32 status;
	int val;

	/* read which mode */
	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		if (adapter->phy_read
		    && adapter->phy_read(adapter,
					 NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
					 &status) != 0)
			return -EIO;
		else {
			val = netxen_get_phy_link(status);
			return !val;
		}
	} else if (adapter->ahw.port_type == NETXEN_NIC_XGBE) {
		val = adapter->pci_read_normalize(adapter, CRB_XG_STATE);
		return (val == XG_LINK_UP) ? 0 : 1;
	}
	return -EIO;
}

static int
netxen_nic_get_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
		      u8 * bytes)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	int offset;
	int ret;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = (adapter->pdev)->vendor |
			((adapter->pdev)->device << 16);
	offset = eeprom->offset;

	ret = netxen_rom_fast_read_words(adapter, offset, bytes,
						eeprom->len);
	if (ret < 0)
		return ret;

	return 0;
}

static void
netxen_nic_get_ringparam(struct net_device *dev, struct ethtool_ringparam *ring)
{
	struct netxen_adapter *adapter = netdev_priv(dev);

	ring->rx_pending = 0;
	ring->rx_jumbo_pending = 0;
	ring->rx_pending += adapter->recv_ctx.
		rds_rings[RCV_RING_NORMAL].num_desc;
	ring->rx_jumbo_pending += adapter->recv_ctx.
		rds_rings[RCV_RING_JUMBO].num_desc;
	ring->tx_pending = adapter->num_txd;

	if (adapter->ahw.port_type == NETXEN_NIC_GBE)
		ring->rx_max_pending = MAX_RCV_DESCRIPTORS_1G;
	else
		ring->rx_max_pending = MAX_RCV_DESCRIPTORS_10G;
	ring->tx_max_pending = MAX_CMD_DESCRIPTORS_HOST;
	ring->rx_jumbo_max_pending = MAX_JUMBO_RCV_DESCRIPTORS;
	ring->rx_mini_max_pending = 0;
	ring->rx_mini_pending = 0;
}

static void
netxen_nic_get_pauseparam(struct net_device *dev,
			  struct ethtool_pauseparam *pause)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	__u32 val;
	int port = adapter->physical_port;

	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		if ((port < 0) || (port > NETXEN_NIU_MAX_GBE_PORTS))
			return;
		/* get flow control settings */
		netxen_nic_read_w0(adapter,NETXEN_NIU_GB_MAC_CONFIG_0(port),
				&val);
		pause->rx_pause = netxen_gb_get_rx_flowctl(val);
		netxen_nic_read_w0(adapter, NETXEN_NIU_GB_PAUSE_CTL, &val);
		switch (port) {
			case 0:
				pause->tx_pause = !(netxen_gb_get_gb0_mask(val));
				break;
			case 1:
				pause->tx_pause = !(netxen_gb_get_gb1_mask(val));
				break;
			case 2:
				pause->tx_pause = !(netxen_gb_get_gb2_mask(val));
				break;
			case 3:
			default:
				pause->tx_pause = !(netxen_gb_get_gb3_mask(val));
				break;
		}
	} else if (adapter->ahw.port_type == NETXEN_NIC_XGBE) {
		if ((port < 0) || (port > NETXEN_NIU_MAX_XG_PORTS))
			return;
		pause->rx_pause = 1;
		netxen_nic_read_w0(adapter, NETXEN_NIU_XG_PAUSE_CTL, &val);
		if (port == 0)
			pause->tx_pause = !(netxen_xg_get_xg0_mask(val));
		else
			pause->tx_pause = !(netxen_xg_get_xg1_mask(val));
	} else {
		printk(KERN_ERR"%s: Unknown board type: %x\n",
				netxen_nic_driver_name, adapter->ahw.port_type);
	}
}

static int
netxen_nic_set_pauseparam(struct net_device *dev,
			  struct ethtool_pauseparam *pause)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	__u32 val;
	int port = adapter->physical_port;
	/* read mode */
	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		if ((port < 0) || (port > NETXEN_NIU_MAX_GBE_PORTS))
			return -EIO;
		/* set flow control */
		netxen_nic_read_w0(adapter,
					NETXEN_NIU_GB_MAC_CONFIG_0(port), &val);

		if (pause->rx_pause)
			netxen_gb_rx_flowctl(val);
		else
			netxen_gb_unset_rx_flowctl(val);

		netxen_nic_write_w0(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				val);
		/* set autoneg */
		netxen_nic_read_w0(adapter, NETXEN_NIU_GB_PAUSE_CTL, &val);
		switch (port) {
			case 0:
				if (pause->tx_pause)
					netxen_gb_unset_gb0_mask(val);
				else
					netxen_gb_set_gb0_mask(val);
				break;
			case 1:
				if (pause->tx_pause)
					netxen_gb_unset_gb1_mask(val);
				else
					netxen_gb_set_gb1_mask(val);
				break;
			case 2:
				if (pause->tx_pause)
					netxen_gb_unset_gb2_mask(val);
				else
					netxen_gb_set_gb2_mask(val);
				break;
			case 3:
			default:
				if (pause->tx_pause)
					netxen_gb_unset_gb3_mask(val);
				else
					netxen_gb_set_gb3_mask(val);
				break;
		}
		netxen_nic_write_w0(adapter, NETXEN_NIU_GB_PAUSE_CTL, val);
	} else if (adapter->ahw.port_type == NETXEN_NIC_XGBE) {
		if ((port < 0) || (port > NETXEN_NIU_MAX_XG_PORTS))
			return -EIO;
		netxen_nic_read_w0(adapter, NETXEN_NIU_XG_PAUSE_CTL, &val);
		if (port == 0) {
			if (pause->tx_pause)
				netxen_xg_unset_xg0_mask(val);
			else
				netxen_xg_set_xg0_mask(val);
		} else {
			if (pause->tx_pause)
				netxen_xg_unset_xg1_mask(val);
			else
				netxen_xg_set_xg1_mask(val);
		}
		netxen_nic_write_w0(adapter, NETXEN_NIU_XG_PAUSE_CTL, val);
	} else {
		printk(KERN_ERR "%s: Unknown board type: %x\n",
				netxen_nic_driver_name,
				adapter->ahw.port_type);
	}
	return 0;
}

static int netxen_nic_reg_test(struct net_device *dev)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	u32 data_read, data_written;

	netxen_nic_read_w0(adapter, NETXEN_PCIX_PH_REG(0), &data_read);
	if ((data_read & 0xffff) != PHAN_VENDOR_ID)
	return 1;

	data_written = (u32)0xa5a5a5a5;

	netxen_nic_reg_write(adapter, CRB_SCRATCHPAD_TEST, data_written);
	data_read = adapter->pci_read_normalize(adapter, CRB_SCRATCHPAD_TEST);
	if (data_written != data_read)
		return 1;

	return 0;
}

static int netxen_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return NETXEN_NIC_TEST_LEN;
	case ETH_SS_STATS:
		return NETXEN_NIC_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void
netxen_nic_diag_test(struct net_device *dev, struct ethtool_test *eth_test,
		     u64 * data)
{
	memset(data, 0, sizeof(uint64_t) * NETXEN_NIC_TEST_LEN);
	if ((data[0] = netxen_nic_reg_test(dev)))
		eth_test->flags |= ETH_TEST_FL_FAILED;
	/* link test */
	if ((data[1] = (u64) netxen_nic_test_link(dev)))
		eth_test->flags |= ETH_TEST_FL_FAILED;
}

static void
netxen_nic_get_strings(struct net_device *dev, u32 stringset, u8 * data)
{
	int index;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *netxen_nic_gstrings_test,
		       NETXEN_NIC_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		for (index = 0; index < NETXEN_NIC_STATS_LEN; index++) {
			memcpy(data + index * ETH_GSTRING_LEN,
			       netxen_nic_gstrings_stats[index].stat_string,
			       ETH_GSTRING_LEN);
		}
		break;
	}
}

static void
netxen_nic_get_ethtool_stats(struct net_device *dev,
			     struct ethtool_stats *stats, u64 * data)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	int index;

	for (index = 0; index < NETXEN_NIC_STATS_LEN; index++) {
		char *p =
		    (char *)adapter +
		    netxen_nic_gstrings_stats[index].stat_offset;
		data[index] =
		    (netxen_nic_gstrings_stats[index].sizeof_stat ==
		     sizeof(u64)) ? *(u64 *) p : *(u32 *) p;
	}
}

static u32 netxen_nic_get_rx_csum(struct net_device *dev)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	return adapter->rx_csum;
}

static int netxen_nic_set_rx_csum(struct net_device *dev, u32 data)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	adapter->rx_csum = !!data;
	return 0;
}

static u32 netxen_nic_get_tso(struct net_device *dev)
{
	struct netxen_adapter *adapter = netdev_priv(dev);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return (dev->features & (NETIF_F_TSO | NETIF_F_TSO6)) != 0;

	return (dev->features & NETIF_F_TSO) != 0;
}

static int netxen_nic_set_tso(struct net_device *dev, u32 data)
{
	if (data) {
		struct netxen_adapter *adapter = netdev_priv(dev);

		dev->features |= NETIF_F_TSO;
		if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
			dev->features |= NETIF_F_TSO6;
	} else
		dev->features &= ~(NETIF_F_TSO | NETIF_F_TSO6);

	return 0;
}

static void
netxen_nic_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	u32 wol_cfg = 0;

	wol->supported = 0;
	wol->wolopts = 0;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return;

	wol_cfg = netxen_nic_reg_read(adapter, NETXEN_WOL_CONFIG_NV);
	if (wol_cfg & (1UL << adapter->portnum))
		wol->supported |= WAKE_MAGIC;

	wol_cfg = netxen_nic_reg_read(adapter, NETXEN_WOL_CONFIG);
	if (wol_cfg & (1UL << adapter->portnum))
		wol->wolopts |= WAKE_MAGIC;
}

static int
netxen_nic_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	u32 wol_cfg = 0;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return -EOPNOTSUPP;

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EOPNOTSUPP;

	wol_cfg = netxen_nic_reg_read(adapter, NETXEN_WOL_CONFIG_NV);
	if (!(wol_cfg & (1 << adapter->portnum)))
		return -EOPNOTSUPP;

	wol_cfg = netxen_nic_reg_read(adapter, NETXEN_WOL_CONFIG);
	if (wol->wolopts & WAKE_MAGIC)
		wol_cfg |= 1UL << adapter->portnum;
	else
		wol_cfg &= ~(1UL << adapter->portnum);
	netxen_nic_reg_write(adapter, NETXEN_WOL_CONFIG, wol_cfg);

	return 0;
}

/*
 * Set the coalescing parameters. Currently only normal is supported.
 * If rx_coalesce_usecs == 0 or rx_max_coalesced_frames == 0 then set the
 * firmware coalescing to default.
 */
static int netxen_set_intr_coalesce(struct net_device *netdev,
			struct ethtool_coalesce *ethcoal)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);

	if (!NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return -EINVAL;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		return -EINVAL;

	/*
	* Return Error if unsupported values or
	* unsupported parameters are set.
	*/
	if (ethcoal->rx_coalesce_usecs > 0xffff ||
		ethcoal->rx_max_coalesced_frames > 0xffff ||
		ethcoal->tx_coalesce_usecs > 0xffff ||
		ethcoal->tx_max_coalesced_frames > 0xffff ||
		ethcoal->rx_coalesce_usecs_irq ||
		ethcoal->rx_max_coalesced_frames_irq ||
		ethcoal->tx_coalesce_usecs_irq ||
		ethcoal->tx_max_coalesced_frames_irq ||
		ethcoal->stats_block_coalesce_usecs ||
		ethcoal->use_adaptive_rx_coalesce ||
		ethcoal->use_adaptive_tx_coalesce ||
		ethcoal->pkt_rate_low ||
		ethcoal->rx_coalesce_usecs_low ||
		ethcoal->rx_max_coalesced_frames_low ||
		ethcoal->tx_coalesce_usecs_low ||
		ethcoal->tx_max_coalesced_frames_low ||
		ethcoal->pkt_rate_high ||
		ethcoal->rx_coalesce_usecs_high ||
		ethcoal->rx_max_coalesced_frames_high ||
		ethcoal->tx_coalesce_usecs_high ||
		ethcoal->tx_max_coalesced_frames_high)
		return -EINVAL;

	if (!ethcoal->rx_coalesce_usecs ||
		!ethcoal->rx_max_coalesced_frames) {
		adapter->coal.flags = NETXEN_NIC_INTR_DEFAULT;
		adapter->coal.normal.data.rx_time_us =
			NETXEN_DEFAULT_INTR_COALESCE_RX_TIME_US;
		adapter->coal.normal.data.rx_packets =
			NETXEN_DEFAULT_INTR_COALESCE_RX_PACKETS;
	} else {
		adapter->coal.flags = 0;
		adapter->coal.normal.data.rx_time_us =
		ethcoal->rx_coalesce_usecs;
		adapter->coal.normal.data.rx_packets =
		ethcoal->rx_max_coalesced_frames;
	}
	adapter->coal.normal.data.tx_time_us = ethcoal->tx_coalesce_usecs;
	adapter->coal.normal.data.tx_packets =
	ethcoal->tx_max_coalesced_frames;

	netxen_config_intr_coalesce(adapter);

	return 0;
}

static int netxen_get_intr_coalesce(struct net_device *netdev,
			struct ethtool_coalesce *ethcoal)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);

	if (!NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return -EINVAL;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		return -EINVAL;

	ethcoal->rx_coalesce_usecs = adapter->coal.normal.data.rx_time_us;
	ethcoal->tx_coalesce_usecs = adapter->coal.normal.data.tx_time_us;
	ethcoal->rx_max_coalesced_frames =
		adapter->coal.normal.data.rx_packets;
	ethcoal->tx_max_coalesced_frames =
		adapter->coal.normal.data.tx_packets;

	return 0;
}

struct ethtool_ops netxen_nic_ethtool_ops = {
	.get_settings = netxen_nic_get_settings,
	.set_settings = netxen_nic_set_settings,
	.get_drvinfo = netxen_nic_get_drvinfo,
	.get_regs_len = netxen_nic_get_regs_len,
	.get_regs = netxen_nic_get_regs,
	.get_link = ethtool_op_get_link,
	.get_eeprom_len = netxen_nic_get_eeprom_len,
	.get_eeprom = netxen_nic_get_eeprom,
	.get_ringparam = netxen_nic_get_ringparam,
	.get_pauseparam = netxen_nic_get_pauseparam,
	.set_pauseparam = netxen_nic_set_pauseparam,
	.set_tx_csum = ethtool_op_set_tx_csum,
	.set_sg = ethtool_op_set_sg,
	.get_tso = netxen_nic_get_tso,
	.set_tso = netxen_nic_set_tso,
	.get_wol = netxen_nic_get_wol,
	.set_wol = netxen_nic_set_wol,
	.self_test = netxen_nic_diag_test,
	.get_strings = netxen_nic_get_strings,
	.get_ethtool_stats = netxen_nic_get_ethtool_stats,
	.get_sset_count = netxen_get_sset_count,
	.get_rx_csum = netxen_nic_get_rx_csum,
	.set_rx_csum = netxen_nic_set_rx_csum,
	.get_coalesce = netxen_get_intr_coalesce,
	.set_coalesce = netxen_set_intr_coalesce,
};
