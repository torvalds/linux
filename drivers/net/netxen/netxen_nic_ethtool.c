/*
 * Copyright (C) 2003 - 2006 NetXen, Inc.
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
 * NetXen,
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 *
 *
 * ethtool support for netxen nic
 *
 */

#include <linux/types.h>
#include <asm/uaccess.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/version.h>

#include "netxen_nic_hw.h"
#include "netxen_nic.h"
#include "netxen_nic_phan_reg.h"
#include "netxen_nic_ioctl.h"

struct netxen_nic_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define NETXEN_NIC_STAT(m) sizeof(((struct netxen_port *)0)->m), \
			offsetof(struct netxen_port, m)

#define NETXEN_NIC_PORT_WINDOW 0x10000
#define NETXEN_NIC_INVALID_DATA 0xDEADBEEF

static const struct netxen_nic_stats netxen_nic_gstrings_stats[] = {
	{"rcvd_bad_skb", NETXEN_NIC_STAT(stats.rcvdbadskb)},
	{"xmit_called", NETXEN_NIC_STAT(stats.xmitcalled)},
	{"xmited_frames", NETXEN_NIC_STAT(stats.xmitedframes)},
	{"xmit_finished", NETXEN_NIC_STAT(stats.xmitfinished)},
	{"bad_skb_len", NETXEN_NIC_STAT(stats.badskblen)},
	{"no_cmd_desc", NETXEN_NIC_STAT(stats.nocmddescriptor)},
	{"polled", NETXEN_NIC_STAT(stats.polled)},
	{"uphappy", NETXEN_NIC_STAT(stats.uphappy)},
	{"updropped", NETXEN_NIC_STAT(stats.updropped)},
	{"uplcong", NETXEN_NIC_STAT(stats.uplcong)},
	{"uphcong", NETXEN_NIC_STAT(stats.uphcong)},
	{"upmcong", NETXEN_NIC_STAT(stats.upmcong)},
	{"updunno", NETXEN_NIC_STAT(stats.updunno)},
	{"skb_freed", NETXEN_NIC_STAT(stats.skbfreed)},
	{"tx_dropped", NETXEN_NIC_STAT(stats.txdropped)},
	{"tx_null_skb", NETXEN_NIC_STAT(stats.txnullskb)},
	{"csummed", NETXEN_NIC_STAT(stats.csummed)},
	{"no_rcv", NETXEN_NIC_STAT(stats.no_rcv)},
	{"rx_bytes", NETXEN_NIC_STAT(stats.rxbytes)},
	{"tx_bytes", NETXEN_NIC_STAT(stats.txbytes)},
};

#define NETXEN_NIC_STATS_LEN	\
	sizeof(netxen_nic_gstrings_stats) / sizeof(struct netxen_nic_stats)

static const char netxen_nic_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register_Test_offline", "EEPROM_Test_offline",
	"Interrupt_Test_offline", "Loopback_Test_offline",
	"Link_Test_on_offline"
};

#define NETXEN_NIC_TEST_LEN sizeof(netxen_nic_gstrings_test) / ETH_GSTRING_LEN

#define NETXEN_NIC_REGS_COUNT 42
#define NETXEN_NIC_REGS_LEN (NETXEN_NIC_REGS_COUNT * sizeof(__le32))
#define NETXEN_MAX_EEPROM_LEN   1024

static int netxen_nic_get_eeprom_len(struct net_device *dev)
{
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	int n;

	if ((netxen_rom_fast_read(adapter, 0, &n) == 0)
	    && (n & NETXEN_ROM_ROUNDUP)) {
		n &= ~NETXEN_ROM_ROUNDUP;
		if (n < NETXEN_MAX_EEPROM_LEN)
			return n;
	}
	return 0;
}

static void
netxen_nic_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *drvinfo)
{
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	u32 fw_major = 0;
	u32 fw_minor = 0;
	u32 fw_build = 0;

	strncpy(drvinfo->driver, "netxen_nic", 32);
	strncpy(drvinfo->version, NETXEN_NIC_LINUX_VERSIONID, 32);
	fw_major = readl(NETXEN_CRB_NORMALIZE(adapter,
					      NETXEN_FW_VERSION_MAJOR));
	fw_minor = readl(NETXEN_CRB_NORMALIZE(adapter,
					      NETXEN_FW_VERSION_MINOR));
	fw_build = readl(NETXEN_CRB_NORMALIZE(adapter, NETXEN_FW_VERSION_SUB));
	sprintf(drvinfo->fw_version, "%d.%d.%d", fw_major, fw_minor, fw_build);

	strncpy(drvinfo->bus_info, pci_name(port->pdev), 32);
	drvinfo->n_stats = NETXEN_NIC_STATS_LEN;
	drvinfo->testinfo_len = NETXEN_NIC_TEST_LEN;
	drvinfo->regdump_len = NETXEN_NIC_REGS_LEN;
	drvinfo->eedump_len = netxen_nic_get_eeprom_len(dev);
}

static int
netxen_nic_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	struct netxen_board_info *boardinfo = &adapter->ahw.boardcfg;

	/* read which mode */
	if (adapter->ahw.board_type == NETXEN_NIC_GBE) {
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

		if (netif_running(dev)) {
			ecmd->speed = port->link_speed;
			ecmd->duplex = port->link_duplex;
		} else
			return -EIO;	/* link absent */
	} else if (adapter->ahw.board_type == NETXEN_NIC_XGBE) {
		ecmd->supported = (SUPPORTED_TP |
				   SUPPORTED_1000baseT_Full |
				   SUPPORTED_10000baseT_Full);
		ecmd->advertising = (ADVERTISED_TP |
				     ADVERTISED_1000baseT_Full |
				     ADVERTISED_10000baseT_Full);
		ecmd->port = PORT_TP;

		ecmd->speed = SPEED_10000;
		ecmd->duplex = DUPLEX_FULL;
		ecmd->autoneg = AUTONEG_DISABLE;
	} else
		return -EIO;

	ecmd->phy_address = port->portnum;
	ecmd->transceiver = XCVR_EXTERNAL;

	switch ((netxen_brdtype_t) boardinfo->board_type) {
	case NETXEN_BRDTYPE_P2_SB35_4G:
	case NETXEN_BRDTYPE_P2_SB31_2G:
		ecmd->supported |= SUPPORTED_Autoneg;
		ecmd->advertising |= ADVERTISED_Autoneg;
	case NETXEN_BRDTYPE_P2_SB31_10G_CX4:
		ecmd->supported |= SUPPORTED_TP;
		ecmd->advertising |= ADVERTISED_TP;
		ecmd->port = PORT_TP;
		ecmd->autoneg = (boardinfo->board_type ==
				 NETXEN_BRDTYPE_P2_SB31_10G_CX4) ?
		    (AUTONEG_DISABLE) : (port->link_autoneg);
		break;
	case NETXEN_BRDTYPE_P2_SB31_10G_HMEZ:
	case NETXEN_BRDTYPE_P2_SB31_10G_IMEZ:
		ecmd->supported |= SUPPORTED_MII;
		ecmd->advertising |= ADVERTISED_MII;
		ecmd->port = PORT_FIBRE;
		ecmd->autoneg = AUTONEG_DISABLE;
		break;
	case NETXEN_BRDTYPE_P2_SB31_10G:
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_FIBRE;
		ecmd->autoneg = AUTONEG_DISABLE;
		break;
	default:
		printk(KERN_ERR "netxen-nic: Unsupported board model %d\n",
		       (netxen_brdtype_t) boardinfo->board_type);
		return -EIO;

	}

	return 0;
}

static int
netxen_nic_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	__le32 status;

	/* read which mode */
	if (adapter->ahw.board_type == NETXEN_NIC_GBE) {
		/* autonegotiation */
		if (adapter->ops->phy_write
		    && adapter->ops->phy_write(adapter, port->portnum,
					       NETXEN_NIU_GB_MII_MGMT_ADDR_AUTONEG,
					       (__le32) ecmd->autoneg) != 0)
			return -EIO;
		else
			port->link_autoneg = ecmd->autoneg;

		if (adapter->ops->phy_read
		    && adapter->ops->phy_read(adapter, port->portnum,
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
		if (adapter->ops->phy_write
		    && adapter->ops->phy_write(adapter, port->portnum,
					       NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
					       *((int *)&status)) != 0)
			return -EIO;
		else {
			port->link_speed = ecmd->speed;
			port->link_duplex = ecmd->duplex;
		}
	} else
		return -EOPNOTSUPP;

	if (netif_running(dev)) {
		dev->stop(dev);
		dev->open(dev);
	}
	return 0;
}

static int netxen_nic_get_regs_len(struct net_device *dev)
{
	return NETXEN_NIC_REGS_LEN;
}

struct netxen_niu_regs {
	__le32 reg[NETXEN_NIC_REGS_COUNT];
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
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	__le32 mode, *regs_buff = p;
	void __iomem *addr;
	int i, window;

	memset(p, 0, NETXEN_NIC_REGS_LEN);
	regs->version = (1 << 24) | (adapter->ahw.revision_id << 16) |
	    (port->pdev)->device;
	/* which mode */
	NETXEN_NIC_LOCKED_READ_REG(NETXEN_NIU_MODE, &regs_buff[0]);
	mode = regs_buff[0];

	/* Common registers to all the modes */
	NETXEN_NIC_LOCKED_READ_REG(NETXEN_NIU_STRAP_VALUE_SAVE_HIGHER,
				   &regs_buff[2]);
	/* GB/XGB Mode */
	mode = (mode / 2) - 1;
	window = 0;
	if (mode <= 1) {
		for (i = 3; niu_registers[mode].reg[i - 3] != -1; i++) {
			/* GB: port specific registers */
			if (mode == 0 && i >= 19)
				window = port->portnum * NETXEN_NIC_PORT_WINDOW;

			NETXEN_NIC_LOCKED_READ_REG(niu_registers[mode].
						   reg[i - 3] + window,
						   &regs_buff[i]);
		}

	}
}

static void
netxen_nic_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	wol->supported = WAKE_UCAST | WAKE_MCAST | WAKE_BCAST | WAKE_MAGIC;
	/* options can be added depending upon the mode */
	wol->wolopts = 0;
}

static u32 netxen_nic_get_link(struct net_device *dev)
{
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	__le32 status;

	/* read which mode */
	if (adapter->ahw.board_type == NETXEN_NIC_GBE) {
		if (adapter->ops->phy_read
		    && adapter->ops->phy_read(adapter, port->portnum,
					      NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
					      &status) != 0)
			return -EIO;
		else
			return (netxen_get_phy_link(status));
	} else if (adapter->ahw.board_type == NETXEN_NIC_XGBE) {
		int val = readl(NETXEN_CRB_NORMALIZE(adapter, CRB_XG_STATE));
		return val == XG_LINK_UP;
	}
	return -EIO;
}

static int
netxen_nic_get_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
		      u8 * bytes)
{
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	int offset;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = (port->pdev)->vendor | ((port->pdev)->device << 16);
	for (offset = 0; offset < eeprom->len; offset++)
		if (netxen_rom_fast_read
		    (adapter, (8 * offset) + 8, (int *)eeprom->data) == -1)
			return -EIO;
	return 0;
}

static void
netxen_nic_get_ringparam(struct net_device *dev, struct ethtool_ringparam *ring)
{
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	int i, j;

	ring->rx_pending = 0;
	for (i = 0; i < MAX_RCV_CTX; ++i) {
		for (j = 0; j < NUM_RCV_DESC_RINGS; j++)
			ring->rx_pending +=
			    adapter->recv_ctx[i].rcv_desc[j].rcv_pending;
	}

	ring->rx_max_pending = adapter->max_rx_desc_count;
	ring->tx_max_pending = adapter->max_tx_desc_count;
	ring->rx_mini_max_pending = 0;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static void
netxen_nic_get_pauseparam(struct net_device *dev,
			  struct ethtool_pauseparam *pause)
{
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	__le32 val;

	if (adapter->ahw.board_type == NETXEN_NIC_GBE) {
		/* get flow control settings */
		netxen_nic_read_w0(adapter,
				   NETXEN_NIU_GB_MAC_CONFIG_0(port->portnum),
				   (u32 *) & val);
		pause->rx_pause = netxen_gb_get_rx_flowctl(val);
		pause->tx_pause = netxen_gb_get_tx_flowctl(val);
		/* get autoneg settings */
		pause->autoneg = port->link_autoneg;
	}
}

static int
netxen_nic_set_pauseparam(struct net_device *dev,
			  struct ethtool_pauseparam *pause)
{
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	__le32 val;
	unsigned int autoneg;

	/* read mode */
	if (adapter->ahw.board_type == NETXEN_NIC_GBE) {
		/* set flow control */
		netxen_nic_read_w0(adapter,
				   NETXEN_NIU_GB_MAC_CONFIG_0(port->portnum),
				   (u32 *) & val);
		if (pause->tx_pause)
			netxen_gb_tx_flowctl(val);
		else
			netxen_gb_unset_tx_flowctl(val);
		if (pause->rx_pause)
			netxen_gb_rx_flowctl(val);
		else
			netxen_gb_unset_rx_flowctl(val);

		netxen_nic_write_w0(adapter,
				    NETXEN_NIU_GB_MAC_CONFIG_0(port->portnum),
				    *(u32 *) (&val));
		/* set autoneg */
		autoneg = pause->autoneg;
		if (adapter->ops->phy_write
		    && adapter->ops->phy_write(adapter, port->portnum,
					       NETXEN_NIU_GB_MII_MGMT_ADDR_AUTONEG,
					       (__le32) autoneg) != 0)
			return -EIO;
		else {
			port->link_autoneg = pause->autoneg;
			return 0;
		}
	} else
		return -EOPNOTSUPP;
}

static int netxen_nic_reg_test(struct net_device *dev)
{
	struct netxen_port *port = netdev_priv(dev);
	struct netxen_adapter *adapter = port->adapter;
	u32 data_read, data_written, save;
	__le32 mode;

	/* 
	 * first test the "Read Only" registers by writing which mode
	 */
	netxen_nic_read_w0(adapter, NETXEN_NIU_MODE, &mode);
	if (netxen_get_niu_enable_ge(mode)) {	/* GB Mode */
		netxen_nic_read_w0(adapter,
				   NETXEN_NIU_GB_MII_MGMT_STATUS(port->portnum),
				   &data_read);

		save = data_read;
		if (data_read)
			data_written = data_read & NETXEN_NIC_INVALID_DATA;
		else
			data_written = NETXEN_NIC_INVALID_DATA;
		netxen_nic_write_w0(adapter,
				    NETXEN_NIU_GB_MII_MGMT_STATUS(port->
								  portnum),
				    data_written);
		netxen_nic_read_w0(adapter,
				   NETXEN_NIU_GB_MII_MGMT_STATUS(port->portnum),
				   &data_read);

		if (data_written == data_read) {
			netxen_nic_write_w0(adapter,
					    NETXEN_NIU_GB_MII_MGMT_STATUS(port->
									  portnum),
					    save);

			return 0;
		}

		/* netxen_niu_gb_mii_mgmt_indicators is read only */
		netxen_nic_read_w0(adapter,
				   NETXEN_NIU_GB_MII_MGMT_INDICATE(port->
								   portnum),
				   &data_read);

		save = data_read;
		if (data_read)
			data_written = data_read & NETXEN_NIC_INVALID_DATA;
		else
			data_written = NETXEN_NIC_INVALID_DATA;
		netxen_nic_write_w0(adapter,
				    NETXEN_NIU_GB_MII_MGMT_INDICATE(port->
								    portnum),
				    data_written);

		netxen_nic_read_w0(adapter,
				   NETXEN_NIU_GB_MII_MGMT_INDICATE(port->
								   portnum),
				   &data_read);

		if (data_written == data_read) {
			netxen_nic_write_w0(adapter,
					    NETXEN_NIU_GB_MII_MGMT_INDICATE
					    (port->portnum), save);
			return 0;
		}

		/* netxen_niu_gb_interface_status is read only */
		netxen_nic_read_w0(adapter,
				   NETXEN_NIU_GB_INTERFACE_STATUS(port->
								  portnum),
				   &data_read);

		save = data_read;
		if (data_read)
			data_written = data_read & NETXEN_NIC_INVALID_DATA;
		else
			data_written = NETXEN_NIC_INVALID_DATA;
		netxen_nic_write_w0(adapter,
				    NETXEN_NIU_GB_INTERFACE_STATUS(port->
								   portnum),
				    data_written);

		netxen_nic_read_w0(adapter,
				   NETXEN_NIU_GB_INTERFACE_STATUS(port->
								  portnum),
				   &data_read);

		if (data_written == data_read) {
			netxen_nic_write_w0(adapter,
					    NETXEN_NIU_GB_INTERFACE_STATUS
					    (port->portnum), save);

			return 0;
		}
	}			/* GB Mode */
	return 1;
}

static int netxen_nic_diag_test_count(struct net_device *dev)
{
	return NETXEN_NIC_TEST_LEN;
}

static void
netxen_nic_diag_test(struct net_device *dev, struct ethtool_test *eth_test,
		     u64 * data)
{
	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {	/* offline tests */
		/* link test */
		if (!(data[4] = (u64) netxen_nic_get_link(dev)))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (netif_running(dev))
			dev->stop(dev);

		/* register tests */
		if (!(data[0] = netxen_nic_reg_test(dev)))
			eth_test->flags |= ETH_TEST_FL_FAILED;
		/* other tests pass as of now */
		data[1] = data[2] = data[3] = 1;
		if (netif_running(dev))
			dev->open(dev);
	} else {		/* online tests */
		/* link test */
		if (!(data[4] = (u64) netxen_nic_get_link(dev)))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* other tests pass by default */
		data[0] = data[1] = data[2] = data[3] = 1;
	}
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

static int netxen_nic_get_stats_count(struct net_device *dev)
{
	return NETXEN_NIC_STATS_LEN;
}

static void
netxen_nic_get_ethtool_stats(struct net_device *dev,
			     struct ethtool_stats *stats, u64 * data)
{
	struct netxen_port *port = netdev_priv(dev);
	int index;

	for (index = 0; index < NETXEN_NIC_STATS_LEN; index++) {
		char *p =
		    (char *)port + netxen_nic_gstrings_stats[index].stat_offset;
		data[index] =
		    (netxen_nic_gstrings_stats[index].sizeof_stat ==
		     sizeof(u64)) ? *(u64 *) p : *(u32 *) p;
	}

}

struct ethtool_ops netxen_nic_ethtool_ops = {
	.get_settings = netxen_nic_get_settings,
	.set_settings = netxen_nic_set_settings,
	.get_drvinfo = netxen_nic_get_drvinfo,
	.get_regs_len = netxen_nic_get_regs_len,
	.get_regs = netxen_nic_get_regs,
	.get_wol = netxen_nic_get_wol,
	.get_link = netxen_nic_get_link,
	.get_eeprom_len = netxen_nic_get_eeprom_len,
	.get_eeprom = netxen_nic_get_eeprom,
	.get_ringparam = netxen_nic_get_ringparam,
	.get_pauseparam = netxen_nic_get_pauseparam,
	.set_pauseparam = netxen_nic_set_pauseparam,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = ethtool_op_set_tx_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_tso = ethtool_op_get_tso,
	.set_tso = ethtool_op_set_tso,
	.self_test_count = netxen_nic_diag_test_count,
	.self_test = netxen_nic_diag_test,
	.get_strings = netxen_nic_get_strings,
	.get_stats_count = netxen_nic_get_stats_count,
	.get_ethtool_stats = netxen_nic_get_ethtool_stats,
	.get_perm_addr = ethtool_op_get_perm_addr,
};
