/*
 * Copyright (C) 2009 - QLogic Corporation.
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
 * in the file called "COPYING".
 *
 */

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>

#include "qlcnic.h"

struct qlcnic_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define QLC_SIZEOF(m) FIELD_SIZEOF(struct qlcnic_adapter, m)
#define QLC_OFF(m) offsetof(struct qlcnic_adapter, m)

static const struct qlcnic_stats qlcnic_gstrings_stats[] = {
	{"xmit_called",
		QLC_SIZEOF(stats.xmitcalled), QLC_OFF(stats.xmitcalled)},
	{"xmit_finished",
		QLC_SIZEOF(stats.xmitfinished), QLC_OFF(stats.xmitfinished)},
	{"rx_dropped",
		QLC_SIZEOF(stats.rxdropped), QLC_OFF(stats.rxdropped)},
	{"tx_dropped",
		QLC_SIZEOF(stats.txdropped), QLC_OFF(stats.txdropped)},
	{"csummed",
		QLC_SIZEOF(stats.csummed), QLC_OFF(stats.csummed)},
	{"rx_pkts",
		QLC_SIZEOF(stats.rx_pkts), QLC_OFF(stats.rx_pkts)},
	{"lro_pkts",
		QLC_SIZEOF(stats.lro_pkts), QLC_OFF(stats.lro_pkts)},
	{"rx_bytes",
		QLC_SIZEOF(stats.rxbytes), QLC_OFF(stats.rxbytes)},
	{"tx_bytes",
		QLC_SIZEOF(stats.txbytes), QLC_OFF(stats.txbytes)},
	{"lrobytes",
		QLC_SIZEOF(stats.lrobytes), QLC_OFF(stats.lrobytes)},
	{"lso_frames",
		QLC_SIZEOF(stats.lso_frames), QLC_OFF(stats.lso_frames)},
	{"xmit_on",
		QLC_SIZEOF(stats.xmit_on), QLC_OFF(stats.xmit_on)},
	{"xmit_off",
		QLC_SIZEOF(stats.xmit_off), QLC_OFF(stats.xmit_off)},
	{"skb_alloc_failure", QLC_SIZEOF(stats.skb_alloc_failure),
		QLC_OFF(stats.skb_alloc_failure)},
	{"null rxbuf",
		QLC_SIZEOF(stats.null_rxbuf), QLC_OFF(stats.null_rxbuf)},
	{"rx dma map error", QLC_SIZEOF(stats.rx_dma_map_error),
					 QLC_OFF(stats.rx_dma_map_error)},
	{"tx dma map error", QLC_SIZEOF(stats.tx_dma_map_error),
					 QLC_OFF(stats.tx_dma_map_error)},

};

#define QLCNIC_STATS_LEN	ARRAY_SIZE(qlcnic_gstrings_stats)

static const char qlcnic_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register_Test_on_offline",
	"Link_Test_on_offline",
	"Interrupt_Test_offline",
	"Loopback_Test_offline"
};

#define QLCNIC_TEST_LEN	ARRAY_SIZE(qlcnic_gstrings_test)

#define QLCNIC_RING_REGS_COUNT	20
#define QLCNIC_RING_REGS_LEN	(QLCNIC_RING_REGS_COUNT * sizeof(u32))
#define QLCNIC_MAX_EEPROM_LEN   1024

static const u32 diag_registers[] = {
	CRB_CMDPEG_STATE,
	CRB_RCVPEG_STATE,
	CRB_XG_STATE_P3,
	CRB_FW_CAPABILITIES_1,
	ISR_INT_STATE_REG,
	QLCNIC_CRB_DRV_ACTIVE,
	QLCNIC_CRB_DEV_STATE,
	QLCNIC_CRB_DRV_STATE,
	QLCNIC_CRB_DRV_SCRATCH,
	QLCNIC_CRB_DEV_PARTITION_INFO,
	QLCNIC_CRB_DRV_IDC_VER,
	QLCNIC_PEG_ALIVE_COUNTER,
	QLCNIC_PEG_HALT_STATUS1,
	QLCNIC_PEG_HALT_STATUS2,
	QLCNIC_CRB_PEG_NET_0+0x3c,
	QLCNIC_CRB_PEG_NET_1+0x3c,
	QLCNIC_CRB_PEG_NET_2+0x3c,
	QLCNIC_CRB_PEG_NET_4+0x3c,
	-1
};

#define QLCNIC_MGMT_API_VERSION	2
#define QLCNIC_DEV_INFO_SIZE	1
#define QLCNIC_ETHTOOL_REGS_VER	2
static int qlcnic_get_regs_len(struct net_device *dev)
{
	return sizeof(diag_registers) + QLCNIC_RING_REGS_LEN +
				QLCNIC_DEV_INFO_SIZE + 1;
}

static int qlcnic_get_eeprom_len(struct net_device *dev)
{
	return QLCNIC_FLASH_TOTAL_SIZE;
}

static void
qlcnic_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *drvinfo)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	u32 fw_major, fw_minor, fw_build;

	fw_major = QLCRD32(adapter, QLCNIC_FW_VERSION_MAJOR);
	fw_minor = QLCRD32(adapter, QLCNIC_FW_VERSION_MINOR);
	fw_build = QLCRD32(adapter, QLCNIC_FW_VERSION_SUB);
	sprintf(drvinfo->fw_version, "%d.%d.%d", fw_major, fw_minor, fw_build);

	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
	strlcpy(drvinfo->driver, qlcnic_driver_name, 32);
	strlcpy(drvinfo->version, QLCNIC_LINUX_VERSIONID, 32);
}

static int
qlcnic_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	int check_sfp_module = 0;
	u16 pcifn = adapter->ahw.pci_func;

	/* read which mode */
	if (adapter->ahw.port_type == QLCNIC_GBE) {
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

		ecmd->speed = adapter->link_speed;
		ecmd->duplex = adapter->link_duplex;
		ecmd->autoneg = adapter->link_autoneg;

	} else if (adapter->ahw.port_type == QLCNIC_XGBE) {
		u32 val;

		val = QLCRD32(adapter, QLCNIC_PORT_MODE_ADDR);
		if (val == QLCNIC_PORT_MODE_802_3_AP) {
			ecmd->supported = SUPPORTED_1000baseT_Full;
			ecmd->advertising = ADVERTISED_1000baseT_Full;
		} else {
			ecmd->supported = SUPPORTED_10000baseT_Full;
			ecmd->advertising = ADVERTISED_10000baseT_Full;
		}

		if (netif_running(dev) && adapter->has_link_events) {
			ecmd->speed = adapter->link_speed;
			ecmd->autoneg = adapter->link_autoneg;
			ecmd->duplex = adapter->link_duplex;
			goto skip;
		}

		val = QLCRD32(adapter, P3_LINK_SPEED_REG(pcifn));
		ecmd->speed = P3_LINK_SPEED_MHZ *
			P3_LINK_SPEED_VAL(pcifn, val);
		ecmd->duplex = DUPLEX_FULL;
		ecmd->autoneg = AUTONEG_DISABLE;
	} else
		return -EIO;

skip:
	ecmd->phy_address = adapter->physical_port;
	ecmd->transceiver = XCVR_EXTERNAL;

	switch (adapter->ahw.board_type) {
	case QLCNIC_BRDTYPE_P3_REF_QG:
	case QLCNIC_BRDTYPE_P3_4_GB:
	case QLCNIC_BRDTYPE_P3_4_GB_MM:

		ecmd->supported |= SUPPORTED_Autoneg;
		ecmd->advertising |= ADVERTISED_Autoneg;
	case QLCNIC_BRDTYPE_P3_10G_CX4:
	case QLCNIC_BRDTYPE_P3_10G_CX4_LP:
	case QLCNIC_BRDTYPE_P3_10000_BASE_T:
		ecmd->supported |= SUPPORTED_TP;
		ecmd->advertising |= ADVERTISED_TP;
		ecmd->port = PORT_TP;
		ecmd->autoneg =  adapter->link_autoneg;
		break;
	case QLCNIC_BRDTYPE_P3_IMEZ:
	case QLCNIC_BRDTYPE_P3_XG_LOM:
	case QLCNIC_BRDTYPE_P3_HMEZ:
		ecmd->supported |= SUPPORTED_MII;
		ecmd->advertising |= ADVERTISED_MII;
		ecmd->port = PORT_MII;
		ecmd->autoneg = AUTONEG_DISABLE;
		break;
	case QLCNIC_BRDTYPE_P3_10G_SFP_PLUS:
	case QLCNIC_BRDTYPE_P3_10G_SFP_CT:
	case QLCNIC_BRDTYPE_P3_10G_SFP_QT:
		ecmd->advertising |= ADVERTISED_TP;
		ecmd->supported |= SUPPORTED_TP;
		check_sfp_module = netif_running(dev) &&
			adapter->has_link_events;
	case QLCNIC_BRDTYPE_P3_10G_XFP:
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_FIBRE;
		ecmd->autoneg = AUTONEG_DISABLE;
		break;
	case QLCNIC_BRDTYPE_P3_10G_TP:
		if (adapter->ahw.port_type == QLCNIC_XGBE) {
			ecmd->autoneg = AUTONEG_DISABLE;
			ecmd->supported |= (SUPPORTED_FIBRE | SUPPORTED_TP);
			ecmd->advertising |=
				(ADVERTISED_FIBRE | ADVERTISED_TP);
			ecmd->port = PORT_FIBRE;
			check_sfp_module = netif_running(dev) &&
				adapter->has_link_events;
		} else {
			ecmd->autoneg = AUTONEG_ENABLE;
			ecmd->supported |= (SUPPORTED_TP | SUPPORTED_Autoneg);
			ecmd->advertising |=
				(ADVERTISED_TP | ADVERTISED_Autoneg);
			ecmd->port = PORT_TP;
		}
		break;
	default:
		dev_err(&adapter->pdev->dev, "Unsupported board model %d\n",
			adapter->ahw.board_type);
		return -EIO;
	}

	if (check_sfp_module) {
		switch (adapter->module_type) {
		case LINKEVENT_MODULE_OPTICAL_UNKNOWN:
		case LINKEVENT_MODULE_OPTICAL_SRLR:
		case LINKEVENT_MODULE_OPTICAL_LRM:
		case LINKEVENT_MODULE_OPTICAL_SFP_1G:
			ecmd->port = PORT_FIBRE;
			break;
		case LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLE:
		case LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLELEN:
		case LINKEVENT_MODULE_TWINAX:
			ecmd->port = PORT_TP;
			break;
		default:
			ecmd->port = PORT_OTHER;
		}
	}

	return 0;
}

static int
qlcnic_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	__u32 status;

	/* read which mode */
	if (adapter->ahw.port_type == QLCNIC_GBE) {
		/* autonegotiation */
		if (qlcnic_fw_cmd_set_phy(adapter,
			       QLCNIC_NIU_GB_MII_MGMT_ADDR_AUTONEG,
			       ecmd->autoneg) != 0)
			return -EIO;
		else
			adapter->link_autoneg = ecmd->autoneg;

		if (qlcnic_fw_cmd_query_phy(adapter,
			      QLCNIC_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
			      &status) != 0)
			return -EIO;

		switch (ecmd->speed) {
		case SPEED_10:
			qlcnic_set_phy_speed(status, 0);
			break;
		case SPEED_100:
			qlcnic_set_phy_speed(status, 1);
			break;
		case SPEED_1000:
			qlcnic_set_phy_speed(status, 2);
			break;
		}

		if (ecmd->duplex == DUPLEX_HALF)
			qlcnic_clear_phy_duplex(status);
		if (ecmd->duplex == DUPLEX_FULL)
			qlcnic_set_phy_duplex(status);
		if (qlcnic_fw_cmd_set_phy(adapter,
			       QLCNIC_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
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

static void
qlcnic_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *p)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;
	struct qlcnic_host_sds_ring *sds_ring;
	u32 *regs_buff = p;
	int ring, i = 0;

	memset(p, 0, qlcnic_get_regs_len(dev));
	regs->version = (QLCNIC_ETHTOOL_REGS_VER << 24) |
		(adapter->ahw.revision_id << 16) | (adapter->pdev)->device;

	regs_buff[0] = (0xcafe0000 | (QLCNIC_DEV_INFO_SIZE & 0xffff));
	regs_buff[1] = QLCNIC_MGMT_API_VERSION;

	for (i = QLCNIC_DEV_INFO_SIZE + 1; diag_registers[i] != -1; i++)
		regs_buff[i] = QLCRD32(adapter, diag_registers[i]);

	if (!test_bit(__QLCNIC_DEV_UP, &adapter->state))
		return;

	regs_buff[i++] = 0xFFEFCDAB; /* Marker btw regs and ring count*/

	regs_buff[i++] = 1; /* No. of tx ring */
	regs_buff[i++] = le32_to_cpu(*(adapter->tx_ring->hw_consumer));
	regs_buff[i++] = readl(adapter->tx_ring->crb_cmd_producer);

	regs_buff[i++] = 2; /* No. of rx ring */
	regs_buff[i++] = readl(recv_ctx->rds_rings[0].crb_rcv_producer);
	regs_buff[i++] = readl(recv_ctx->rds_rings[1].crb_rcv_producer);

	regs_buff[i++] = adapter->max_sds_rings;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &(recv_ctx->sds_rings[ring]);
		regs_buff[i++] = readl(sds_ring->crb_sts_consumer);
	}
}

static u32 qlcnic_test_link(struct net_device *dev)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	u32 val;

	val = QLCRD32(adapter, CRB_XG_STATE_P3);
	val = XG_LINK_STATE_P3(adapter->ahw.pci_func, val);
	return (val == XG_LINK_UP_P3) ? 0 : 1;
}

static int
qlcnic_get_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
		      u8 *bytes)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	int offset;
	int ret;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = (adapter->pdev)->vendor |
			((adapter->pdev)->device << 16);
	offset = eeprom->offset;

	ret = qlcnic_rom_fast_read_words(adapter, offset, bytes,
						eeprom->len);
	if (ret < 0)
		return ret;

	return 0;
}

static void
qlcnic_get_ringparam(struct net_device *dev,
		struct ethtool_ringparam *ring)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);

	ring->rx_pending = adapter->num_rxd;
	ring->rx_jumbo_pending = adapter->num_jumbo_rxd;
	ring->tx_pending = adapter->num_txd;

	if (adapter->ahw.port_type == QLCNIC_GBE) {
		ring->rx_max_pending = MAX_RCV_DESCRIPTORS_1G;
		ring->rx_jumbo_max_pending = MAX_JUMBO_RCV_DESCRIPTORS_1G;
	} else {
		ring->rx_max_pending = MAX_RCV_DESCRIPTORS_10G;
		ring->rx_jumbo_max_pending = MAX_JUMBO_RCV_DESCRIPTORS_10G;
	}

	ring->tx_max_pending = MAX_CMD_DESCRIPTORS;

	ring->rx_mini_max_pending = 0;
	ring->rx_mini_pending = 0;
}

static u32
qlcnic_validate_ringparam(u32 val, u32 min, u32 max, char *r_name)
{
	u32 num_desc;
	num_desc = max(val, min);
	num_desc = min(num_desc, max);
	num_desc = roundup_pow_of_two(num_desc);

	if (val != num_desc) {
		printk(KERN_INFO "%s: setting %s ring size %d instead of %d\n",
		       qlcnic_driver_name, r_name, num_desc, val);
	}

	return num_desc;
}

static int
qlcnic_set_ringparam(struct net_device *dev,
		struct ethtool_ringparam *ring)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	u16 max_rcv_desc = MAX_RCV_DESCRIPTORS_10G;
	u16 max_jumbo_desc = MAX_JUMBO_RCV_DESCRIPTORS_10G;
	u16 num_rxd, num_jumbo_rxd, num_txd;


	if (ring->rx_mini_pending)
		return -EOPNOTSUPP;

	if (adapter->ahw.port_type == QLCNIC_GBE) {
		max_rcv_desc = MAX_RCV_DESCRIPTORS_1G;
		max_jumbo_desc = MAX_JUMBO_RCV_DESCRIPTORS_10G;
	}

	num_rxd = qlcnic_validate_ringparam(ring->rx_pending,
			MIN_RCV_DESCRIPTORS, max_rcv_desc, "rx");

	num_jumbo_rxd = qlcnic_validate_ringparam(ring->rx_jumbo_pending,
			MIN_JUMBO_DESCRIPTORS, max_jumbo_desc, "rx jumbo");

	num_txd = qlcnic_validate_ringparam(ring->tx_pending,
			MIN_CMD_DESCRIPTORS, MAX_CMD_DESCRIPTORS, "tx");

	if (num_rxd == adapter->num_rxd && num_txd == adapter->num_txd &&
			num_jumbo_rxd == adapter->num_jumbo_rxd)
		return 0;

	adapter->num_rxd = num_rxd;
	adapter->num_jumbo_rxd = num_jumbo_rxd;
	adapter->num_txd = num_txd;

	return qlcnic_reset_context(adapter);
}

static void
qlcnic_get_pauseparam(struct net_device *netdev,
			  struct ethtool_pauseparam *pause)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	int port = adapter->physical_port;
	__u32 val;

	if (adapter->ahw.port_type == QLCNIC_GBE) {
		if ((port < 0) || (port > QLCNIC_NIU_MAX_GBE_PORTS))
			return;
		/* get flow control settings */
		val = QLCRD32(adapter, QLCNIC_NIU_GB_MAC_CONFIG_0(port));
		pause->rx_pause = qlcnic_gb_get_rx_flowctl(val);
		val = QLCRD32(adapter, QLCNIC_NIU_GB_PAUSE_CTL);
		switch (port) {
		case 0:
			pause->tx_pause = !(qlcnic_gb_get_gb0_mask(val));
			break;
		case 1:
			pause->tx_pause = !(qlcnic_gb_get_gb1_mask(val));
			break;
		case 2:
			pause->tx_pause = !(qlcnic_gb_get_gb2_mask(val));
			break;
		case 3:
		default:
			pause->tx_pause = !(qlcnic_gb_get_gb3_mask(val));
			break;
		}
	} else if (adapter->ahw.port_type == QLCNIC_XGBE) {
		if ((port < 0) || (port > QLCNIC_NIU_MAX_XG_PORTS))
			return;
		pause->rx_pause = 1;
		val = QLCRD32(adapter, QLCNIC_NIU_XG_PAUSE_CTL);
		if (port == 0)
			pause->tx_pause = !(qlcnic_xg_get_xg0_mask(val));
		else
			pause->tx_pause = !(qlcnic_xg_get_xg1_mask(val));
	} else {
		dev_err(&netdev->dev, "Unknown board type: %x\n",
					adapter->ahw.port_type);
	}
}

static int
qlcnic_set_pauseparam(struct net_device *netdev,
			  struct ethtool_pauseparam *pause)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	int port = adapter->physical_port;
	__u32 val;

	/* read mode */
	if (adapter->ahw.port_type == QLCNIC_GBE) {
		if ((port < 0) || (port > QLCNIC_NIU_MAX_GBE_PORTS))
			return -EIO;
		/* set flow control */
		val = QLCRD32(adapter, QLCNIC_NIU_GB_MAC_CONFIG_0(port));

		if (pause->rx_pause)
			qlcnic_gb_rx_flowctl(val);
		else
			qlcnic_gb_unset_rx_flowctl(val);

		QLCWR32(adapter, QLCNIC_NIU_GB_MAC_CONFIG_0(port),
				val);
		/* set autoneg */
		val = QLCRD32(adapter, QLCNIC_NIU_GB_PAUSE_CTL);
		switch (port) {
		case 0:
			if (pause->tx_pause)
				qlcnic_gb_unset_gb0_mask(val);
			else
				qlcnic_gb_set_gb0_mask(val);
			break;
		case 1:
			if (pause->tx_pause)
				qlcnic_gb_unset_gb1_mask(val);
			else
				qlcnic_gb_set_gb1_mask(val);
			break;
		case 2:
			if (pause->tx_pause)
				qlcnic_gb_unset_gb2_mask(val);
			else
				qlcnic_gb_set_gb2_mask(val);
			break;
		case 3:
		default:
			if (pause->tx_pause)
				qlcnic_gb_unset_gb3_mask(val);
			else
				qlcnic_gb_set_gb3_mask(val);
			break;
		}
		QLCWR32(adapter, QLCNIC_NIU_GB_PAUSE_CTL, val);
	} else if (adapter->ahw.port_type == QLCNIC_XGBE) {
		if (!pause->rx_pause || pause->autoneg)
			return -EOPNOTSUPP;

		if ((port < 0) || (port > QLCNIC_NIU_MAX_XG_PORTS))
			return -EIO;

		val = QLCRD32(adapter, QLCNIC_NIU_XG_PAUSE_CTL);
		if (port == 0) {
			if (pause->tx_pause)
				qlcnic_xg_unset_xg0_mask(val);
			else
				qlcnic_xg_set_xg0_mask(val);
		} else {
			if (pause->tx_pause)
				qlcnic_xg_unset_xg1_mask(val);
			else
				qlcnic_xg_set_xg1_mask(val);
		}
		QLCWR32(adapter, QLCNIC_NIU_XG_PAUSE_CTL, val);
	} else {
		dev_err(&netdev->dev, "Unknown board type: %x\n",
				adapter->ahw.port_type);
	}
	return 0;
}

static int qlcnic_reg_test(struct net_device *dev)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	u32 data_read;

	data_read = QLCRD32(adapter, QLCNIC_PCIX_PH_REG(0));
	if ((data_read & 0xffff) != adapter->pdev->vendor)
		return 1;

	return 0;
}

static int qlcnic_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return QLCNIC_TEST_LEN;
	case ETH_SS_STATS:
		return QLCNIC_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

#define QLC_ILB_PKT_SIZE 64
#define QLC_NUM_ILB_PKT	16
#define QLC_ILB_MAX_RCV_LOOP 10

static void qlcnic_create_loopback_buff(unsigned char *data)
{
	unsigned char random_data[] = {0xa8, 0x06, 0x45, 0x00};
	memset(data, 0x4e, QLC_ILB_PKT_SIZE);
	memset(data, 0xff, 12);
	memcpy(data + 12, random_data, sizeof(random_data));
}

int qlcnic_check_loopback_buff(unsigned char *data)
{
	unsigned char buff[QLC_ILB_PKT_SIZE];
	qlcnic_create_loopback_buff(buff);
	return memcmp(data, buff, QLC_ILB_PKT_SIZE);
}

static int qlcnic_do_ilb_test(struct qlcnic_adapter *adapter)
{
	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;
	struct qlcnic_host_sds_ring *sds_ring = &recv_ctx->sds_rings[0];
	struct sk_buff *skb;
	int i, loop, cnt = 0;

	for (i = 0; i < QLC_NUM_ILB_PKT; i++) {
		skb = dev_alloc_skb(QLC_ILB_PKT_SIZE);
		qlcnic_create_loopback_buff(skb->data);
		skb_put(skb, QLC_ILB_PKT_SIZE);

		adapter->diag_cnt = 0;
		qlcnic_xmit_frame(skb, adapter->netdev);

		loop = 0;
		do {
			msleep(1);
			qlcnic_process_rcv_ring_diag(sds_ring);
		} while (loop++ < QLC_ILB_MAX_RCV_LOOP &&
			 !adapter->diag_cnt);

		dev_kfree_skb_any(skb);

		if (!adapter->diag_cnt)
			dev_warn(&adapter->pdev->dev, "ILB Test: %dth packet"
				" not recevied\n", i + 1);
		else
			cnt++;
	}
	if (cnt != i) {
		dev_warn(&adapter->pdev->dev, "ILB Test failed\n");
		return -1;
	}
	return 0;
}

static int qlcnic_loopback_test(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	int max_sds_rings = adapter->max_sds_rings;
	int ret;

	if (adapter->op_mode == QLCNIC_NON_PRIV_FUNC) {
		dev_warn(&adapter->pdev->dev, "Loopback test not supported"
				"for non privilege function\n");
		return 0;
	}

	if (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		return -EIO;

	ret = qlcnic_diag_alloc_res(netdev, QLCNIC_LOOPBACK_TEST);
	if (ret)
		goto clear_it;

	ret = qlcnic_set_ilb_mode(adapter);
	if (ret)
		goto done;

	ret = qlcnic_do_ilb_test(adapter);

	qlcnic_clear_ilb_mode(adapter);

done:
	qlcnic_diag_free_res(netdev, max_sds_rings);

clear_it:
	adapter->max_sds_rings = max_sds_rings;
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	return ret;
}

static int qlcnic_irq_test(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	int max_sds_rings = adapter->max_sds_rings;
	int ret;

	if (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		return -EIO;

	ret = qlcnic_diag_alloc_res(netdev, QLCNIC_INTERRUPT_TEST);
	if (ret)
		goto clear_it;

	adapter->diag_cnt = 0;
	ret = qlcnic_issue_cmd(adapter, adapter->ahw.pci_func,
			adapter->fw_hal_version, adapter->portnum,
			0, 0, 0x00000011);
	if (ret)
		goto done;

	msleep(10);

	ret = !adapter->diag_cnt;

done:
	qlcnic_diag_free_res(netdev, max_sds_rings);

clear_it:
	adapter->max_sds_rings = max_sds_rings;
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	return ret;
}

static void
qlcnic_diag_test(struct net_device *dev, struct ethtool_test *eth_test,
		     u64 *data)
{
	memset(data, 0, sizeof(u64) * QLCNIC_TEST_LEN);

	data[0] = qlcnic_reg_test(dev);
	if (data[0])
		eth_test->flags |= ETH_TEST_FL_FAILED;

	data[1] = (u64) qlcnic_test_link(dev);
	if (data[1])
		eth_test->flags |= ETH_TEST_FL_FAILED;

	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		data[2] = qlcnic_irq_test(dev);
		if (data[2])
			eth_test->flags |= ETH_TEST_FL_FAILED;

		data[3] = qlcnic_loopback_test(dev);
		if (data[3])
			eth_test->flags |= ETH_TEST_FL_FAILED;

	}
}

static void
qlcnic_get_strings(struct net_device *dev, u32 stringset, u8 * data)
{
	int index;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *qlcnic_gstrings_test,
		       QLCNIC_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		for (index = 0; index < QLCNIC_STATS_LEN; index++) {
			memcpy(data + index * ETH_GSTRING_LEN,
			       qlcnic_gstrings_stats[index].stat_string,
			       ETH_GSTRING_LEN);
		}
		break;
	}
}

static void
qlcnic_get_ethtool_stats(struct net_device *dev,
			     struct ethtool_stats *stats, u64 * data)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	int index;

	for (index = 0; index < QLCNIC_STATS_LEN; index++) {
		char *p =
		    (char *)adapter +
		    qlcnic_gstrings_stats[index].stat_offset;
		data[index] =
		    (qlcnic_gstrings_stats[index].sizeof_stat ==
		     sizeof(u64)) ? *(u64 *)p:(*(u32 *)p);
	}
}

static int qlcnic_set_tx_csum(struct net_device *dev, u32 data)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);

	if ((adapter->flags & QLCNIC_ESWITCH_ENABLED))
		return -EOPNOTSUPP;
	if (data)
		dev->features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
	else
		dev->features &= ~(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);

	return 0;

}
static u32 qlcnic_get_tx_csum(struct net_device *dev)
{
	return dev->features & NETIF_F_IP_CSUM;
}

static u32 qlcnic_get_rx_csum(struct net_device *dev)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	return adapter->rx_csum;
}

static int qlcnic_set_rx_csum(struct net_device *dev, u32 data)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);

	if ((adapter->flags & QLCNIC_ESWITCH_ENABLED))
		return -EOPNOTSUPP;
	if (!!data) {
		adapter->rx_csum = !!data;
		return 0;
	}

	if (adapter->flags & QLCNIC_LRO_ENABLED) {
		if (qlcnic_config_hw_lro(adapter, QLCNIC_LRO_DISABLED))
			return -EIO;

		dev->features &= ~NETIF_F_LRO;
		qlcnic_send_lro_cleanup(adapter);
	}
	adapter->rx_csum = !!data;
	dev_info(&adapter->pdev->dev, "disabling LRO as rx_csum is off\n");
	return 0;
}

static u32 qlcnic_get_tso(struct net_device *dev)
{
	return (dev->features & (NETIF_F_TSO | NETIF_F_TSO6)) != 0;
}

static int qlcnic_set_tso(struct net_device *dev, u32 data)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	if (!(adapter->capabilities & QLCNIC_FW_CAPABILITY_TSO))
		return -EOPNOTSUPP;
	if (data)
		dev->features |= (NETIF_F_TSO | NETIF_F_TSO6);
	else
		dev->features &= ~(NETIF_F_TSO | NETIF_F_TSO6);

	return 0;
}

static int qlcnic_blink_led(struct net_device *dev, u32 val)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	int ret;

	if (!test_bit(__QLCNIC_DEV_UP, &adapter->state))
		return -EIO;

	ret = adapter->nic_ops->config_led(adapter, 1, 0xf);
	if (ret) {
		dev_err(&adapter->pdev->dev,
			"Failed to set LED blink state.\n");
		return ret;
	}

	msleep_interruptible(val * 1000);

	ret = adapter->nic_ops->config_led(adapter, 0, 0xf);
	if (ret) {
		dev_err(&adapter->pdev->dev,
			"Failed to reset LED blink state.\n");
		return ret;
	}

	return 0;
}

static void
qlcnic_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	u32 wol_cfg;

	wol->supported = 0;
	wol->wolopts = 0;

	wol_cfg = QLCRD32(adapter, QLCNIC_WOL_CONFIG_NV);
	if (wol_cfg & (1UL << adapter->portnum))
		wol->supported |= WAKE_MAGIC;

	wol_cfg = QLCRD32(adapter, QLCNIC_WOL_CONFIG);
	if (wol_cfg & (1UL << adapter->portnum))
		wol->wolopts |= WAKE_MAGIC;
}

static int
qlcnic_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	u32 wol_cfg;

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EOPNOTSUPP;

	wol_cfg = QLCRD32(adapter, QLCNIC_WOL_CONFIG_NV);
	if (!(wol_cfg & (1 << adapter->portnum)))
		return -EOPNOTSUPP;

	wol_cfg = QLCRD32(adapter, QLCNIC_WOL_CONFIG);
	if (wol->wolopts & WAKE_MAGIC)
		wol_cfg |= 1UL << adapter->portnum;
	else
		wol_cfg &= ~(1UL << adapter->portnum);

	QLCWR32(adapter, QLCNIC_WOL_CONFIG, wol_cfg);

	return 0;
}

/*
 * Set the coalescing parameters. Currently only normal is supported.
 * If rx_coalesce_usecs == 0 or rx_max_coalesced_frames == 0 then set the
 * firmware coalescing to default.
 */
static int qlcnic_set_intr_coalesce(struct net_device *netdev,
			struct ethtool_coalesce *ethcoal)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	if (!test_bit(__QLCNIC_DEV_UP, &adapter->state))
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
		adapter->coal.flags = QLCNIC_INTR_DEFAULT;
		adapter->coal.normal.data.rx_time_us =
			QLCNIC_DEFAULT_INTR_COALESCE_RX_TIME_US;
		adapter->coal.normal.data.rx_packets =
			QLCNIC_DEFAULT_INTR_COALESCE_RX_PACKETS;
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

	qlcnic_config_intr_coalesce(adapter);

	return 0;
}

static int qlcnic_get_intr_coalesce(struct net_device *netdev,
			struct ethtool_coalesce *ethcoal)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return -EINVAL;

	ethcoal->rx_coalesce_usecs = adapter->coal.normal.data.rx_time_us;
	ethcoal->tx_coalesce_usecs = adapter->coal.normal.data.tx_time_us;
	ethcoal->rx_max_coalesced_frames =
		adapter->coal.normal.data.rx_packets;
	ethcoal->tx_max_coalesced_frames =
		adapter->coal.normal.data.tx_packets;

	return 0;
}

static int qlcnic_set_flags(struct net_device *netdev, u32 data)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	int hw_lro;

	if (data & ~ETH_FLAG_LRO)
		return -EINVAL;

	if (!(adapter->capabilities & QLCNIC_FW_CAPABILITY_HW_LRO))
		return -EINVAL;

	if (!adapter->rx_csum) {
		dev_info(&adapter->pdev->dev, "rx csum is off, "
			"cannot toggle lro\n");
		return -EINVAL;
	}

	if ((data & ETH_FLAG_LRO) && (adapter->flags & QLCNIC_LRO_ENABLED))
		return 0;

	if (data & ETH_FLAG_LRO) {
		hw_lro = QLCNIC_LRO_ENABLED;
		netdev->features |= NETIF_F_LRO;
	} else {
		hw_lro = 0;
		netdev->features &= ~NETIF_F_LRO;
	}

	if (qlcnic_config_hw_lro(adapter, hw_lro))
		return -EIO;

	if ((hw_lro == 0) && qlcnic_send_lro_cleanup(adapter))
		return -EIO;


	return 0;
}

static u32 qlcnic_get_msglevel(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void qlcnic_set_msglevel(struct net_device *netdev, u32 msglvl)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = msglvl;
}

const struct ethtool_ops qlcnic_ethtool_ops = {
	.get_settings = qlcnic_get_settings,
	.set_settings = qlcnic_set_settings,
	.get_drvinfo = qlcnic_get_drvinfo,
	.get_regs_len = qlcnic_get_regs_len,
	.get_regs = qlcnic_get_regs,
	.get_link = ethtool_op_get_link,
	.get_eeprom_len = qlcnic_get_eeprom_len,
	.get_eeprom = qlcnic_get_eeprom,
	.get_ringparam = qlcnic_get_ringparam,
	.set_ringparam = qlcnic_set_ringparam,
	.get_pauseparam = qlcnic_get_pauseparam,
	.set_pauseparam = qlcnic_set_pauseparam,
	.get_tx_csum = qlcnic_get_tx_csum,
	.set_tx_csum = qlcnic_set_tx_csum,
	.set_sg = ethtool_op_set_sg,
	.get_tso = qlcnic_get_tso,
	.set_tso = qlcnic_set_tso,
	.get_wol = qlcnic_get_wol,
	.set_wol = qlcnic_set_wol,
	.self_test = qlcnic_diag_test,
	.get_strings = qlcnic_get_strings,
	.get_ethtool_stats = qlcnic_get_ethtool_stats,
	.get_sset_count = qlcnic_get_sset_count,
	.get_rx_csum = qlcnic_get_rx_csum,
	.set_rx_csum = qlcnic_set_rx_csum,
	.get_coalesce = qlcnic_get_intr_coalesce,
	.set_coalesce = qlcnic_set_intr_coalesce,
	.get_flags = ethtool_op_get_flags,
	.set_flags = qlcnic_set_flags,
	.phys_id = qlcnic_blink_led,
	.set_msglevel = qlcnic_set_msglevel,
	.get_msglevel = qlcnic_get_msglevel,
};
