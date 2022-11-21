// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2003 - 2009 NetXen, Inc.
 * Copyright (C) 2009 - QLogic Corporation.
 * All rights reserved.
 */

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>

#include "netxen_nic.h"
#include "netxen_nic_hw.h"

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
	{"rx_pkts", NETXEN_NIC_STAT(stats.rx_pkts)},
	{"lro_pkts", NETXEN_NIC_STAT(stats.lro_pkts)},
	{"rx_bytes", NETXEN_NIC_STAT(stats.rxbytes)},
	{"tx_bytes", NETXEN_NIC_STAT(stats.txbytes)},
};

#define NETXEN_NIC_STATS_LEN	ARRAY_SIZE(netxen_nic_gstrings_stats)

static const char netxen_nic_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register_Test_on_offline",
	"Link_Test_on_offline"
};

#define NETXEN_NIC_TEST_LEN	ARRAY_SIZE(netxen_nic_gstrings_test)

#define NETXEN_NIC_REGS_COUNT 30
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
	u32 fw_major = 0;
	u32 fw_minor = 0;
	u32 fw_build = 0;

	strlcpy(drvinfo->driver, netxen_nic_driver_name,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, NETXEN_NIC_LINUX_VERSIONID,
		sizeof(drvinfo->version));
	fw_major = NXRD32(adapter, NETXEN_FW_VERSION_MAJOR);
	fw_minor = NXRD32(adapter, NETXEN_FW_VERSION_MINOR);
	fw_build = NXRD32(adapter, NETXEN_FW_VERSION_SUB);
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		"%d.%d.%d", fw_major, fw_minor, fw_build);

	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
}

static int
netxen_nic_get_link_ksettings(struct net_device *dev,
			      struct ethtool_link_ksettings *cmd)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	int check_sfp_module = 0;
	u32 supported, advertising;

	/* read which mode */
	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		supported = (SUPPORTED_10baseT_Half |
				   SUPPORTED_10baseT_Full |
				   SUPPORTED_100baseT_Half |
				   SUPPORTED_100baseT_Full |
				   SUPPORTED_1000baseT_Half |
				   SUPPORTED_1000baseT_Full);

		advertising = (ADVERTISED_100baseT_Half |
				     ADVERTISED_100baseT_Full |
				     ADVERTISED_1000baseT_Half |
				     ADVERTISED_1000baseT_Full);

		cmd->base.port = PORT_TP;

		cmd->base.speed = adapter->link_speed;
		cmd->base.duplex = adapter->link_duplex;
		cmd->base.autoneg = adapter->link_autoneg;

	} else if (adapter->ahw.port_type == NETXEN_NIC_XGBE) {
		u32 val;

		val = NXRD32(adapter, NETXEN_PORT_MODE_ADDR);
		if (val == NETXEN_PORT_MODE_802_3_AP) {
			supported = SUPPORTED_1000baseT_Full;
			advertising = ADVERTISED_1000baseT_Full;
		} else {
			supported = SUPPORTED_10000baseT_Full;
			advertising = ADVERTISED_10000baseT_Full;
		}

		if (netif_running(dev) && adapter->has_link_events) {
			cmd->base.speed = adapter->link_speed;
			cmd->base.autoneg = adapter->link_autoneg;
			cmd->base.duplex = adapter->link_duplex;
			goto skip;
		}

		cmd->base.port = PORT_TP;

		if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
			u16 pcifn = adapter->ahw.pci_func;

			val = NXRD32(adapter, P3_LINK_SPEED_REG(pcifn));
			cmd->base.speed = P3_LINK_SPEED_MHZ *
				P3_LINK_SPEED_VAL(pcifn, val);
		} else
			cmd->base.speed = SPEED_10000;

		cmd->base.duplex = DUPLEX_FULL;
		cmd->base.autoneg = AUTONEG_DISABLE;
	} else
		return -EIO;

skip:
	cmd->base.phy_address = adapter->physical_port;

	switch (adapter->ahw.board_type) {
	case NETXEN_BRDTYPE_P2_SB35_4G:
	case NETXEN_BRDTYPE_P2_SB31_2G:
	case NETXEN_BRDTYPE_P3_REF_QG:
	case NETXEN_BRDTYPE_P3_4_GB:
	case NETXEN_BRDTYPE_P3_4_GB_MM:
		supported |= SUPPORTED_Autoneg;
		advertising |= ADVERTISED_Autoneg;
		fallthrough;
	case NETXEN_BRDTYPE_P2_SB31_10G_CX4:
	case NETXEN_BRDTYPE_P3_10G_CX4:
	case NETXEN_BRDTYPE_P3_10G_CX4_LP:
	case NETXEN_BRDTYPE_P3_10000_BASE_T:
		supported |= SUPPORTED_TP;
		advertising |= ADVERTISED_TP;
		cmd->base.port = PORT_TP;
		cmd->base.autoneg = (adapter->ahw.board_type ==
				 NETXEN_BRDTYPE_P2_SB31_10G_CX4) ?
		    (AUTONEG_DISABLE) : (adapter->link_autoneg);
		break;
	case NETXEN_BRDTYPE_P2_SB31_10G_HMEZ:
	case NETXEN_BRDTYPE_P2_SB31_10G_IMEZ:
	case NETXEN_BRDTYPE_P3_IMEZ:
	case NETXEN_BRDTYPE_P3_XG_LOM:
	case NETXEN_BRDTYPE_P3_HMEZ:
		supported |= SUPPORTED_MII;
		advertising |= ADVERTISED_MII;
		cmd->base.port = PORT_MII;
		cmd->base.autoneg = AUTONEG_DISABLE;
		break;
	case NETXEN_BRDTYPE_P3_10G_SFP_PLUS:
	case NETXEN_BRDTYPE_P3_10G_SFP_CT:
	case NETXEN_BRDTYPE_P3_10G_SFP_QT:
		advertising |= ADVERTISED_TP;
		supported |= SUPPORTED_TP;
		check_sfp_module = netif_running(dev) &&
			adapter->has_link_events;
		fallthrough;
	case NETXEN_BRDTYPE_P2_SB31_10G:
	case NETXEN_BRDTYPE_P3_10G_XFP:
		supported |= SUPPORTED_FIBRE;
		advertising |= ADVERTISED_FIBRE;
		cmd->base.port = PORT_FIBRE;
		cmd->base.autoneg = AUTONEG_DISABLE;
		break;
	case NETXEN_BRDTYPE_P3_10G_TP:
		if (adapter->ahw.port_type == NETXEN_NIC_XGBE) {
			cmd->base.autoneg = AUTONEG_DISABLE;
			supported |= (SUPPORTED_FIBRE | SUPPORTED_TP);
			advertising |=
				(ADVERTISED_FIBRE | ADVERTISED_TP);
			cmd->base.port = PORT_FIBRE;
			check_sfp_module = netif_running(dev) &&
				adapter->has_link_events;
		} else {
			supported |= (SUPPORTED_TP | SUPPORTED_Autoneg);
			advertising |=
				(ADVERTISED_TP | ADVERTISED_Autoneg);
			cmd->base.port = PORT_TP;
		}
		break;
	default:
		printk(KERN_ERR "netxen-nic: Unsupported board model %d\n",
				adapter->ahw.board_type);
		return -EIO;
	}

	if (check_sfp_module) {
		switch (adapter->module_type) {
		case LINKEVENT_MODULE_OPTICAL_UNKNOWN:
		case LINKEVENT_MODULE_OPTICAL_SRLR:
		case LINKEVENT_MODULE_OPTICAL_LRM:
		case LINKEVENT_MODULE_OPTICAL_SFP_1G:
			cmd->base.port = PORT_FIBRE;
			break;
		case LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLE:
		case LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLELEN:
		case LINKEVENT_MODULE_TWINAX:
			cmd->base.port = PORT_TP;
			break;
		default:
			cmd->base.port = -1;
		}
	}

	if (!netif_running(dev) || !adapter->ahw.linkup) {
		cmd->base.duplex = DUPLEX_UNKNOWN;
		cmd->base.speed = SPEED_UNKNOWN;
	}

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	return 0;
}

static int
netxen_nic_set_link_ksettings(struct net_device *dev,
			      const struct ethtool_link_ksettings *cmd)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	u32 speed = cmd->base.speed;
	int ret;

	if (adapter->ahw.port_type != NETXEN_NIC_GBE)
		return -EOPNOTSUPP;

	if (!(adapter->capabilities & NX_FW_CAPABILITY_GBE_LINK_CFG))
		return -EOPNOTSUPP;

	ret = nx_fw_cmd_set_gbe_port(adapter, speed, cmd->base.duplex,
				     cmd->base.autoneg);
	if (ret == NX_RCODE_NOT_SUPPORTED)
		return -EOPNOTSUPP;
	else if (ret)
		return -EIO;

	adapter->link_speed = speed;
	adapter->link_duplex = cmd->base.duplex;
	adapter->link_autoneg = cmd->base.autoneg;

	if (!netif_running(dev))
		return 0;

	dev->netdev_ops->ndo_stop(dev);
	return dev->netdev_ops->ndo_open(dev);
}

static int netxen_nic_get_regs_len(struct net_device *dev)
{
	return NETXEN_NIC_REGS_LEN;
}

static void
netxen_nic_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *p)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;
	struct nx_host_sds_ring *sds_ring;
	u32 *regs_buff = p;
	int ring, i = 0;
	int port = adapter->physical_port;

	memset(p, 0, NETXEN_NIC_REGS_LEN);

	regs->version = (1 << 24) | (adapter->ahw.revision_id << 16) |
	    (adapter->pdev)->device;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		return;

	regs_buff[i++] = NXRD32(adapter, CRB_CMDPEG_STATE);
	regs_buff[i++] = NXRD32(adapter, CRB_RCVPEG_STATE);
	regs_buff[i++] = NXRD32(adapter, CRB_FW_CAPABILITIES_1);
	regs_buff[i++] = NXRDIO(adapter, adapter->crb_int_state_reg);
	regs_buff[i++] = NXRD32(adapter, NX_CRB_DEV_REF_COUNT);
	regs_buff[i++] = NXRD32(adapter, NX_CRB_DEV_STATE);
	regs_buff[i++] = NXRD32(adapter, NETXEN_PEG_ALIVE_COUNTER);
	regs_buff[i++] = NXRD32(adapter, NETXEN_PEG_HALT_STATUS1);
	regs_buff[i++] = NXRD32(adapter, NETXEN_PEG_HALT_STATUS2);

	regs_buff[i++] = NXRD32(adapter, NETXEN_CRB_PEG_NET_0+0x3c);
	regs_buff[i++] = NXRD32(adapter, NETXEN_CRB_PEG_NET_1+0x3c);
	regs_buff[i++] = NXRD32(adapter, NETXEN_CRB_PEG_NET_2+0x3c);
	regs_buff[i++] = NXRD32(adapter, NETXEN_CRB_PEG_NET_3+0x3c);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {

		regs_buff[i++] = NXRD32(adapter, NETXEN_CRB_PEG_NET_4+0x3c);
		i += 2;

		regs_buff[i++] = NXRD32(adapter, CRB_XG_STATE_P3);
		regs_buff[i++] = le32_to_cpu(*(adapter->tx_ring->hw_consumer));

	} else {
		i++;

		regs_buff[i++] = NXRD32(adapter,
					NETXEN_NIU_XGE_CONFIG_0+(0x10000*port));
		regs_buff[i++] = NXRD32(adapter,
					NETXEN_NIU_XGE_CONFIG_1+(0x10000*port));

		regs_buff[i++] = NXRD32(adapter, CRB_XG_STATE);
		regs_buff[i++] = NXRDIO(adapter,
				 adapter->tx_ring->crb_cmd_consumer);
	}

	regs_buff[i++] = NXRDIO(adapter, adapter->tx_ring->crb_cmd_producer);

	regs_buff[i++] = NXRDIO(adapter,
			 recv_ctx->rds_rings[0].crb_rcv_producer);
	regs_buff[i++] = NXRDIO(adapter,
			 recv_ctx->rds_rings[1].crb_rcv_producer);

	regs_buff[i++] = adapter->max_sds_rings;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &(recv_ctx->sds_rings[ring]);
		regs_buff[i++] = NXRDIO(adapter,
					sds_ring->crb_sts_consumer);
	}
}

static u32 netxen_nic_test_link(struct net_device *dev)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	u32 val, port;

	port = adapter->physical_port;
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		val = NXRD32(adapter, CRB_XG_STATE_P3);
		val = XG_LINK_STATE_P3(adapter->ahw.pci_func, val);
		return (val == XG_LINK_UP_P3) ? 0 : 1;
	} else {
		val = NXRD32(adapter, CRB_XG_STATE);
		val = (val >> port*8) & 0xff;
		return (val == XG_LINK_UP) ? 0 : 1;
	}
}

static int
netxen_nic_get_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
		      u8 *bytes)
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
netxen_nic_get_ringparam(struct net_device *dev,
			 struct ethtool_ringparam *ring,
			 struct kernel_ethtool_ringparam *kernel_ring,
			 struct netlink_ext_ack *extack)
{
	struct netxen_adapter *adapter = netdev_priv(dev);

	ring->rx_pending = adapter->num_rxd;
	ring->rx_jumbo_pending = adapter->num_jumbo_rxd;
	ring->rx_jumbo_pending += adapter->num_lro_rxd;
	ring->tx_pending = adapter->num_txd;

	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		ring->rx_max_pending = MAX_RCV_DESCRIPTORS_1G;
		ring->rx_jumbo_max_pending = MAX_JUMBO_RCV_DESCRIPTORS_1G;
	} else {
		ring->rx_max_pending = MAX_RCV_DESCRIPTORS_10G;
		ring->rx_jumbo_max_pending = MAX_JUMBO_RCV_DESCRIPTORS_10G;
	}

	ring->tx_max_pending = MAX_CMD_DESCRIPTORS;
}

static u32
netxen_validate_ringparam(u32 val, u32 min, u32 max, char *r_name)
{
	u32 num_desc;
	num_desc = max(val, min);
	num_desc = min(num_desc, max);
	num_desc = roundup_pow_of_two(num_desc);

	if (val != num_desc) {
		printk(KERN_INFO "%s: setting %s ring size %d instead of %d\n",
		       netxen_nic_driver_name, r_name, num_desc, val);
	}

	return num_desc;
}

static int
netxen_nic_set_ringparam(struct net_device *dev,
			 struct ethtool_ringparam *ring,
			 struct kernel_ethtool_ringparam *kernel_ring,
			 struct netlink_ext_ack *extack)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	u16 max_rcv_desc = MAX_RCV_DESCRIPTORS_10G;
	u16 max_jumbo_desc = MAX_JUMBO_RCV_DESCRIPTORS_10G;
	u16 num_rxd, num_jumbo_rxd, num_txd;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return -EOPNOTSUPP;

	if (ring->rx_mini_pending)
		return -EOPNOTSUPP;

	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		max_rcv_desc = MAX_RCV_DESCRIPTORS_1G;
		max_jumbo_desc = MAX_JUMBO_RCV_DESCRIPTORS_10G;
	}

	num_rxd = netxen_validate_ringparam(ring->rx_pending,
			MIN_RCV_DESCRIPTORS, max_rcv_desc, "rx");

	num_jumbo_rxd = netxen_validate_ringparam(ring->rx_jumbo_pending,
			MIN_JUMBO_DESCRIPTORS, max_jumbo_desc, "rx jumbo");

	num_txd = netxen_validate_ringparam(ring->tx_pending,
			MIN_CMD_DESCRIPTORS, MAX_CMD_DESCRIPTORS, "tx");

	if (num_rxd == adapter->num_rxd && num_txd == adapter->num_txd &&
			num_jumbo_rxd == adapter->num_jumbo_rxd)
		return 0;

	adapter->num_rxd = num_rxd;
	adapter->num_jumbo_rxd = num_jumbo_rxd;
	adapter->num_txd = num_txd;

	return netxen_nic_reset_context(adapter);
}

static void
netxen_nic_get_pauseparam(struct net_device *dev,
			  struct ethtool_pauseparam *pause)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	__u32 val;
	int port = adapter->physical_port;

	pause->autoneg = 0;

	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		if ((port < 0) || (port >= NETXEN_NIU_MAX_GBE_PORTS))
			return;
		/* get flow control settings */
		val = NXRD32(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port));
		pause->rx_pause = netxen_gb_get_rx_flowctl(val);
		val = NXRD32(adapter, NETXEN_NIU_GB_PAUSE_CTL);
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
		if ((port < 0) || (port >= NETXEN_NIU_MAX_XG_PORTS))
			return;
		pause->rx_pause = 1;
		val = NXRD32(adapter, NETXEN_NIU_XG_PAUSE_CTL);
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

	/* not supported */
	if (pause->autoneg)
		return -EINVAL;

	/* read mode */
	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		if ((port < 0) || (port >= NETXEN_NIU_MAX_GBE_PORTS))
			return -EIO;
		/* set flow control */
		val = NXRD32(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port));

		if (pause->rx_pause)
			netxen_gb_rx_flowctl(val);
		else
			netxen_gb_unset_rx_flowctl(val);

		NXWR32(adapter, NETXEN_NIU_GB_MAC_CONFIG_0(port),
				val);
		/* set autoneg */
		val = NXRD32(adapter, NETXEN_NIU_GB_PAUSE_CTL);
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
		NXWR32(adapter, NETXEN_NIU_GB_PAUSE_CTL, val);
	} else if (adapter->ahw.port_type == NETXEN_NIC_XGBE) {
		if ((port < 0) || (port >= NETXEN_NIU_MAX_XG_PORTS))
			return -EIO;
		val = NXRD32(adapter, NETXEN_NIU_XG_PAUSE_CTL);
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
		NXWR32(adapter, NETXEN_NIU_XG_PAUSE_CTL, val);
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

	data_read = NXRD32(adapter, NETXEN_PCIX_PH_REG(0));
	if ((data_read & 0xffff) != adapter->pdev->vendor)
		return 1;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return 0;

	data_written = (u32)0xa5a5a5a5;

	NXWR32(adapter, CRB_SCRATCHPAD_TEST, data_written);
	data_read = NXRD32(adapter, CRB_SCRATCHPAD_TEST);
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
		     u64 *data)
{
	memset(data, 0, sizeof(uint64_t) * NETXEN_NIC_TEST_LEN);
	if ((data[0] = netxen_nic_reg_test(dev)))
		eth_test->flags |= ETH_TEST_FL_FAILED;
	/* link test */
	if ((data[1] = (u64) netxen_nic_test_link(dev)))
		eth_test->flags |= ETH_TEST_FL_FAILED;
}

static void
netxen_nic_get_strings(struct net_device *dev, u32 stringset, u8 *data)
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
			     struct ethtool_stats *stats, u64 *data)
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

static void
netxen_nic_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	u32 wol_cfg = 0;

	wol->supported = 0;
	wol->wolopts = 0;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return;

	wol_cfg = NXRD32(adapter, NETXEN_WOL_CONFIG_NV);
	if (wol_cfg & (1UL << adapter->portnum))
		wol->supported |= WAKE_MAGIC;

	wol_cfg = NXRD32(adapter, NETXEN_WOL_CONFIG);
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

	wol_cfg = NXRD32(adapter, NETXEN_WOL_CONFIG_NV);
	if (!(wol_cfg & (1 << adapter->portnum)))
		return -EOPNOTSUPP;

	wol_cfg = NXRD32(adapter, NETXEN_WOL_CONFIG);
	if (wol->wolopts & WAKE_MAGIC)
		wol_cfg |= 1UL << adapter->portnum;
	else
		wol_cfg &= ~(1UL << adapter->portnum);
	NXWR32(adapter, NETXEN_WOL_CONFIG, wol_cfg);

	return 0;
}

/*
 * Set the coalescing parameters. Currently only normal is supported.
 * If rx_coalesce_usecs == 0 or rx_max_coalesced_frames == 0 then set the
 * firmware coalescing to default.
 */
static int netxen_set_intr_coalesce(struct net_device *netdev,
				    struct ethtool_coalesce *ethcoal,
				    struct kernel_ethtool_coalesce *kernel_coal,
				    struct netlink_ext_ack *extack)
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
		ethcoal->tx_max_coalesced_frames > 0xffff)
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
				    struct ethtool_coalesce *ethcoal,
				    struct kernel_ethtool_coalesce *kernel_coal,
				    struct netlink_ext_ack *extack)
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

static int
netxen_get_dump_flag(struct net_device *netdev, struct ethtool_dump *dump)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct netxen_minidump *mdump = &adapter->mdump;
	if (adapter->fw_mdump_rdy)
		dump->len = mdump->md_dump_size;
	else
		dump->len = 0;

	if (!mdump->md_enabled)
		dump->flag = ETH_FW_DUMP_DISABLE;
	else
		dump->flag = mdump->md_capture_mask;

	dump->version = adapter->fw_version;
	return 0;
}

/* Fw dump levels */
static const u32 FW_DUMP_LEVELS[] = { 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff };

static int
netxen_set_dump(struct net_device *netdev, struct ethtool_dump *val)
{
	int i;
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct netxen_minidump *mdump = &adapter->mdump;

	switch (val->flag) {
	case NX_FORCE_FW_DUMP_KEY:
		if (!mdump->md_enabled) {
			netdev_info(netdev, "FW dump not enabled\n");
			return 0;
		}
		if (adapter->fw_mdump_rdy) {
			netdev_info(netdev, "Previous dump not cleared, not forcing dump\n");
			return 0;
		}
		netdev_info(netdev, "Forcing a fw dump\n");
		nx_dev_request_reset(adapter);
		break;
	case NX_DISABLE_FW_DUMP:
		if (mdump->md_enabled) {
			netdev_info(netdev, "Disabling FW Dump\n");
			mdump->md_enabled = 0;
		}
		break;
	case NX_ENABLE_FW_DUMP:
		if (!mdump->md_enabled) {
			netdev_info(netdev, "Enabling FW dump\n");
			mdump->md_enabled = 1;
		}
		break;
	case NX_FORCE_FW_RESET:
		netdev_info(netdev, "Forcing FW reset\n");
		nx_dev_request_reset(adapter);
		adapter->flags &= ~NETXEN_FW_RESET_OWNER;
		break;
	default:
		for (i = 0; i < ARRAY_SIZE(FW_DUMP_LEVELS); i++) {
			if (val->flag == FW_DUMP_LEVELS[i]) {
				mdump->md_capture_mask = val->flag;
				netdev_info(netdev,
					"Driver mask changed to: 0x%x\n",
					mdump->md_capture_mask);
				return 0;
			}
		}
		netdev_info(netdev,
			"Invalid dump level: 0x%x\n", val->flag);
		return -EINVAL;
	}

	return 0;
}

static int
netxen_get_dump_data(struct net_device *netdev, struct ethtool_dump *dump,
			void *buffer)
{
	int i, copy_sz;
	u32 *hdr_ptr, *data;
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct netxen_minidump *mdump = &adapter->mdump;


	if (!adapter->fw_mdump_rdy) {
		netdev_info(netdev, "Dump not available\n");
		return -EINVAL;
	}
	/* Copy template header first */
	copy_sz = mdump->md_template_size;
	hdr_ptr = (u32 *) mdump->md_template;
	data = buffer;
	for (i = 0; i < copy_sz/sizeof(u32); i++)
		*data++ = cpu_to_le32(*hdr_ptr++);

	/* Copy captured dump data */
	memcpy(buffer + copy_sz,
		mdump->md_capture_buff + mdump->md_template_size,
			mdump->md_capture_size);
	dump->len = copy_sz + mdump->md_capture_size;
	dump->flag = mdump->md_capture_mask;

	/* Free dump area once data has been captured */
	vfree(mdump->md_capture_buff);
	mdump->md_capture_buff = NULL;
	adapter->fw_mdump_rdy = 0;
	netdev_info(netdev, "extracted the fw dump Successfully\n");
	return 0;
}

const struct ethtool_ops netxen_nic_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES,
	.get_drvinfo = netxen_nic_get_drvinfo,
	.get_regs_len = netxen_nic_get_regs_len,
	.get_regs = netxen_nic_get_regs,
	.get_link = ethtool_op_get_link,
	.get_eeprom_len = netxen_nic_get_eeprom_len,
	.get_eeprom = netxen_nic_get_eeprom,
	.get_ringparam = netxen_nic_get_ringparam,
	.set_ringparam = netxen_nic_set_ringparam,
	.get_pauseparam = netxen_nic_get_pauseparam,
	.set_pauseparam = netxen_nic_set_pauseparam,
	.get_wol = netxen_nic_get_wol,
	.set_wol = netxen_nic_set_wol,
	.self_test = netxen_nic_diag_test,
	.get_strings = netxen_nic_get_strings,
	.get_ethtool_stats = netxen_nic_get_ethtool_stats,
	.get_sset_count = netxen_get_sset_count,
	.get_coalesce = netxen_get_intr_coalesce,
	.set_coalesce = netxen_set_intr_coalesce,
	.get_dump_flag = netxen_get_dump_flag,
	.get_dump_data = netxen_get_dump_data,
	.set_dump = netxen_set_dump,
	.get_link_ksettings = netxen_nic_get_link_ksettings,
	.set_link_ksettings = netxen_nic_set_link_ksettings,
};
