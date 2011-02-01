/*
 * QLogic qlcnic NIC Driver
 * Copyright (c)  2009-2010 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
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

static const char qlcnic_device_gstrings_stats[][ETH_GSTRING_LEN] = {
	"rx unicast frames",
	"rx multicast frames",
	"rx broadcast frames",
	"rx dropped frames",
	"rx errors",
	"rx local frames",
	"rx numbytes",
	"tx unicast frames",
	"tx multicast frames",
	"tx broadcast frames",
	"tx dropped frames",
	"tx errors",
	"tx local frames",
	"tx numbytes",
};

#define QLCNIC_STATS_LEN	ARRAY_SIZE(qlcnic_gstrings_stats)
#define QLCNIC_DEVICE_STATS_LEN	ARRAY_SIZE(qlcnic_device_gstrings_stats)

static const char qlcnic_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register_Test_on_offline",
	"Link_Test_on_offline",
	"Interrupt_Test_offline"
};

#define QLCNIC_TEST_LEN	ARRAY_SIZE(qlcnic_gstrings_test)

#define QLCNIC_RING_REGS_COUNT	20
#define QLCNIC_RING_REGS_LEN	(QLCNIC_RING_REGS_COUNT * sizeof(u32))
#define QLCNIC_MAX_EEPROM_LEN   1024

static const u32 diag_registers[] = {
	CRB_CMDPEG_STATE,
	CRB_RCVPEG_STATE,
	CRB_XG_STATE_P3P,
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

		val = QLCRD32(adapter, P3P_LINK_SPEED_REG(pcifn));
		ecmd->speed = P3P_LINK_SPEED_MHZ *
			P3P_LINK_SPEED_VAL(pcifn, val);
		ecmd->duplex = DUPLEX_FULL;
		ecmd->autoneg = AUTONEG_DISABLE;
	} else
		return -EIO;

skip:
	ecmd->phy_address = adapter->physical_port;
	ecmd->transceiver = XCVR_EXTERNAL;

	switch (adapter->ahw.board_type) {
	case QLCNIC_BRDTYPE_P3P_REF_QG:
	case QLCNIC_BRDTYPE_P3P_4_GB:
	case QLCNIC_BRDTYPE_P3P_4_GB_MM:

		ecmd->supported |= SUPPORTED_Autoneg;
		ecmd->advertising |= ADVERTISED_Autoneg;
	case QLCNIC_BRDTYPE_P3P_10G_CX4:
	case QLCNIC_BRDTYPE_P3P_10G_CX4_LP:
	case QLCNIC_BRDTYPE_P3P_10000_BASE_T:
		ecmd->supported |= SUPPORTED_TP;
		ecmd->advertising |= ADVERTISED_TP;
		ecmd->port = PORT_TP;
		ecmd->autoneg =  adapter->link_autoneg;
		break;
	case QLCNIC_BRDTYPE_P3P_IMEZ:
	case QLCNIC_BRDTYPE_P3P_XG_LOM:
	case QLCNIC_BRDTYPE_P3P_HMEZ:
		ecmd->supported |= SUPPORTED_MII;
		ecmd->advertising |= ADVERTISED_MII;
		ecmd->port = PORT_MII;
		ecmd->autoneg = AUTONEG_DISABLE;
		break;
	case QLCNIC_BRDTYPE_P3P_10G_SFP_PLUS:
	case QLCNIC_BRDTYPE_P3P_10G_SFP_CT:
	case QLCNIC_BRDTYPE_P3P_10G_SFP_QT:
		ecmd->advertising |= ADVERTISED_TP;
		ecmd->supported |= SUPPORTED_TP;
		check_sfp_module = netif_running(dev) &&
			adapter->has_link_events;
	case QLCNIC_BRDTYPE_P3P_10G_XFP:
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_FIBRE;
		ecmd->autoneg = AUTONEG_DISABLE;
		break;
	case QLCNIC_BRDTYPE_P3P_10G_TP:
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
	int ring, i = 0, j = 0;

	memset(p, 0, qlcnic_get_regs_len(dev));
	regs->version = (QLCNIC_ETHTOOL_REGS_VER << 24) |
		(adapter->ahw.revision_id << 16) | (adapter->pdev)->device;

	regs_buff[0] = (0xcafe0000 | (QLCNIC_DEV_INFO_SIZE & 0xffff));
	regs_buff[1] = QLCNIC_MGMT_API_VERSION;

	for (i = QLCNIC_DEV_INFO_SIZE + 1; diag_registers[j] != -1; j++, i++)
		regs_buff[i] = QLCRD32(adapter, diag_registers[j]);

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

	val = QLCRD32(adapter, CRB_XG_STATE_P3P);
	val = XG_LINK_STATE_P3P(adapter->ahw.pci_func, val);
	return (val == XG_LINK_UP_P3P) ? 0 : 1;
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

	ring->rx_max_pending = adapter->max_rxd;
	ring->rx_jumbo_max_pending = adapter->max_jumbo_rxd;
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
	u16 num_rxd, num_jumbo_rxd, num_txd;

	if (ring->rx_mini_pending)
		return -EOPNOTSUPP;

	num_rxd = qlcnic_validate_ringparam(ring->rx_pending,
			MIN_RCV_DESCRIPTORS, adapter->max_rxd, "rx");

	num_jumbo_rxd = qlcnic_validate_ringparam(ring->rx_jumbo_pending,
			MIN_JUMBO_DESCRIPTORS, adapter->max_jumbo_rxd,
						"rx jumbo");

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
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	switch (sset) {
	case ETH_SS_TEST:
		return QLCNIC_TEST_LEN;
	case ETH_SS_STATS:
		if (adapter->flags & QLCNIC_ESWITCH_ENABLED)
			return QLCNIC_STATS_LEN + QLCNIC_DEVICE_STATS_LEN;
		return QLCNIC_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
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

	if (eth_test->flags & ETH_TEST_FL_OFFLINE) {
		data[2] = qlcnic_irq_test(dev);
		if (data[2])
			eth_test->flags |= ETH_TEST_FL_FAILED;


	}
}

static void
qlcnic_get_strings(struct net_device *dev, u32 stringset, u8 * data)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	int index, i;

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
		if (!(adapter->flags & QLCNIC_ESWITCH_ENABLED))
			return;
		for (i = 0; i < QLCNIC_DEVICE_STATS_LEN; index++, i++) {
			memcpy(data + index * ETH_GSTRING_LEN,
			       qlcnic_device_gstrings_stats[i],
			       ETH_GSTRING_LEN);
		}
	}
}

#define QLCNIC_FILL_ESWITCH_STATS(VAL1) \
	(((VAL1) == QLCNIC_ESW_STATS_NOT_AVAIL) ? 0 : VAL1)

static void
qlcnic_fill_device_stats(int *index, u64 *data,
		struct __qlcnic_esw_statistics *stats)
{
	int ind = *index;

	data[ind++] = QLCNIC_FILL_ESWITCH_STATS(stats->unicast_frames);
	data[ind++] = QLCNIC_FILL_ESWITCH_STATS(stats->multicast_frames);
	data[ind++] = QLCNIC_FILL_ESWITCH_STATS(stats->broadcast_frames);
	data[ind++] = QLCNIC_FILL_ESWITCH_STATS(stats->dropped_frames);
	data[ind++] = QLCNIC_FILL_ESWITCH_STATS(stats->errors);
	data[ind++] = QLCNIC_FILL_ESWITCH_STATS(stats->local_frames);
	data[ind++] = QLCNIC_FILL_ESWITCH_STATS(stats->numbytes);

	*index = ind;
}

static void
qlcnic_get_ethtool_stats(struct net_device *dev,
			     struct ethtool_stats *stats, u64 * data)
{
	struct qlcnic_adapter *adapter = netdev_priv(dev);
	struct qlcnic_esw_statistics port_stats;
	int index, ret;

	for (index = 0; index < QLCNIC_STATS_LEN; index++) {
		char *p =
		    (char *)adapter +
		    qlcnic_gstrings_stats[index].stat_offset;
		data[index] =
		    (qlcnic_gstrings_stats[index].sizeof_stat ==
		     sizeof(u64)) ? *(u64 *)p:(*(u32 *)p);
	}

	if (!(adapter->flags & QLCNIC_ESWITCH_ENABLED))
		return;

	memset(&port_stats, 0, sizeof(struct qlcnic_esw_statistics));
	ret = qlcnic_get_port_stats(adapter, adapter->ahw.pci_func,
			QLCNIC_QUERY_RX_COUNTER, &port_stats.rx);
	if (ret)
		return;

	qlcnic_fill_device_stats(&index, data, &port_stats.rx);

	ret = qlcnic_get_port_stats(adapter, adapter->ahw.pci_func,
			QLCNIC_QUERY_TX_COUNTER, &port_stats.tx);
	if (ret)
		return;

	qlcnic_fill_device_stats(&index, data, &port_stats.tx);
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

	if (dev->features & NETIF_F_LRO) {
		if (qlcnic_config_hw_lro(adapter, QLCNIC_LRO_DISABLED))
			return -EIO;

		dev->features &= ~NETIF_F_LRO;
		qlcnic_send_lro_cleanup(adapter);
		dev_info(&adapter->pdev->dev,
					"disabling LRO as rx_csum is off\n");
	}
	adapter->rx_csum = !!data;
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
	int max_sds_rings = adapter->max_sds_rings;
	int dev_down = 0;
	int ret;

	if (!test_bit(__QLCNIC_DEV_UP, &adapter->state)) {
		dev_down = 1;
		if (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
			return -EIO;

		ret = qlcnic_diag_alloc_res(dev, QLCNIC_LED_TEST);
		if (ret) {
			clear_bit(__QLCNIC_RESETTING, &adapter->state);
			return ret;
		}
	}

	ret = adapter->nic_ops->config_led(adapter, 1, 0xf);
	if (ret) {
		dev_err(&adapter->pdev->dev,
			"Failed to set LED blink state.\n");
		goto done;
	}

	msleep_interruptible(val * 1000);

	ret = adapter->nic_ops->config_led(adapter, 0, 0xf);
	if (ret) {
		dev_err(&adapter->pdev->dev,
			"Failed to reset LED blink state.\n");
		goto done;
	}

done:
	if (dev_down) {
		qlcnic_diag_free_res(dev, max_sds_rings);
		clear_bit(__QLCNIC_RESETTING, &adapter->state);
	}
	return ret;

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

	if ((data & ETH_FLAG_LRO) && (netdev->features & NETIF_F_LRO))
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
