/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/pci.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_nic.h"
#include "octeon_main.h"
#include "octeon_network.h"
#include "cn66xx_regs.h"
#include "cn66xx_device.h"
#include "cn23xx_pf_device.h"
#include "cn23xx_vf_device.h"

static int octnet_get_link_stats(struct net_device *netdev);

struct oct_intrmod_context {
	int octeon_id;
	wait_queue_head_t wc;
	int cond;
	int status;
};

struct oct_intrmod_resp {
	u64     rh;
	struct oct_intrmod_cfg intrmod;
	u64     status;
};

struct oct_mdio_cmd_context {
	int octeon_id;
	wait_queue_head_t wc;
	int cond;
};

struct oct_mdio_cmd_resp {
	u64 rh;
	struct oct_mdio_cmd resp;
	u64 status;
};

#define OCT_MDIO45_RESP_SIZE   (sizeof(struct oct_mdio_cmd_resp))

/* Octeon's interface mode of operation */
enum {
	INTERFACE_MODE_DISABLED,
	INTERFACE_MODE_RGMII,
	INTERFACE_MODE_GMII,
	INTERFACE_MODE_SPI,
	INTERFACE_MODE_PCIE,
	INTERFACE_MODE_XAUI,
	INTERFACE_MODE_SGMII,
	INTERFACE_MODE_PICMG,
	INTERFACE_MODE_NPI,
	INTERFACE_MODE_LOOP,
	INTERFACE_MODE_SRIO,
	INTERFACE_MODE_ILK,
	INTERFACE_MODE_RXAUI,
	INTERFACE_MODE_QSGMII,
	INTERFACE_MODE_AGL,
	INTERFACE_MODE_XLAUI,
	INTERFACE_MODE_XFI,
	INTERFACE_MODE_10G_KR,
	INTERFACE_MODE_40G_KR4,
	INTERFACE_MODE_MIXED,
};

#define OCT_ETHTOOL_REGDUMP_LEN  4096
#define OCT_ETHTOOL_REGDUMP_LEN_23XX  (4096 * 11)
#define OCT_ETHTOOL_REGDUMP_LEN_23XX_VF  (4096 * 2)
#define OCT_ETHTOOL_REGSVER  1

/* statistics of PF */
static const char oct_stats_strings[][ETH_GSTRING_LEN] = {
	"rx_packets",
	"tx_packets",
	"rx_bytes",
	"tx_bytes",
	"rx_errors",	/*jabber_err+l2_err+frame_err */
	"tx_errors",	/*fw_err_pko+fw_err_link+fw_err_drop */
	"rx_dropped",   /*st->fromwire.total_rcvd - st->fromwire.fw_total_rcvd +
			 *st->fromwire.dmac_drop + st->fromwire.fw_err_drop
			 */
	"tx_dropped",

	"tx_total_sent",
	"tx_total_fwd",
	"tx_err_pko",
	"tx_err_link",
	"tx_err_drop",

	"tx_tso",
	"tx_tso_packets",
	"tx_tso_err",
	"tx_vxlan",

	"mac_tx_total_pkts",
	"mac_tx_total_bytes",
	"mac_tx_mcast_pkts",
	"mac_tx_bcast_pkts",
	"mac_tx_ctl_packets",	/*oct->link_stats.fromhost.ctl_sent */
	"mac_tx_total_collisions",
	"mac_tx_one_collision",
	"mac_tx_multi_collison",
	"mac_tx_max_collision_fail",
	"mac_tx_max_deferal_fail",
	"mac_tx_fifo_err",
	"mac_tx_runts",

	"rx_total_rcvd",
	"rx_total_fwd",
	"rx_jabber_err",
	"rx_l2_err",
	"rx_frame_err",
	"rx_err_pko",
	"rx_err_link",
	"rx_err_drop",

	"rx_vxlan",
	"rx_vxlan_err",

	"rx_lro_pkts",
	"rx_lro_bytes",
	"rx_total_lro",

	"rx_lro_aborts",
	"rx_lro_aborts_port",
	"rx_lro_aborts_seq",
	"rx_lro_aborts_tsval",
	"rx_lro_aborts_timer",
	"rx_fwd_rate",

	"mac_rx_total_rcvd",
	"mac_rx_bytes",
	"mac_rx_total_bcst",
	"mac_rx_total_mcst",
	"mac_rx_runts",
	"mac_rx_ctl_packets",
	"mac_rx_fifo_err",
	"mac_rx_dma_drop",
	"mac_rx_fcs_err",

	"link_state_changes",
};

/* statistics of VF */
static const char oct_vf_stats_strings[][ETH_GSTRING_LEN] = {
	"rx_packets",
	"tx_packets",
	"rx_bytes",
	"tx_bytes",
	"rx_errors", /* jabber_err + l2_err+frame_err */
	"tx_errors", /* fw_err_pko + fw_err_link+fw_err_drop */
	"rx_dropped", /* total_rcvd - fw_total_rcvd + dmac_drop + fw_err_drop */
	"tx_dropped",
	"link_state_changes",
};

/* statistics of host tx queue */
static const char oct_iq_stats_strings[][ETH_GSTRING_LEN] = {
	"packets",		/*oct->instr_queue[iq_no]->stats.tx_done*/
	"bytes",		/*oct->instr_queue[iq_no]->stats.tx_tot_bytes*/
	"dropped",
	"iq_busy",
	"sgentry_sent",

	"fw_instr_posted",
	"fw_instr_processed",
	"fw_instr_dropped",
	"fw_bytes_sent",

	"tso",
	"vxlan",
	"txq_restart",
};

/* statistics of host rx queue */
static const char oct_droq_stats_strings[][ETH_GSTRING_LEN] = {
	"packets",		/*oct->droq[oq_no]->stats.rx_pkts_received */
	"bytes",		/*oct->droq[oq_no]->stats.rx_bytes_received */
	"dropped",		/*oct->droq[oq_no]->stats.rx_dropped+
				 *oct->droq[oq_no]->stats.dropped_nodispatch+
				 *oct->droq[oq_no]->stats.dropped_toomany+
				 *oct->droq[oq_no]->stats.dropped_nomem
				 */
	"dropped_nomem",
	"dropped_toomany",
	"fw_dropped",
	"fw_pkts_received",
	"fw_bytes_received",
	"fw_dropped_nodispatch",

	"vxlan",
	"buffer_alloc_failure",
};

/* LiquidIO driver private flags */
static const char oct_priv_flags_strings[][ETH_GSTRING_LEN] = {
};

#define OCTNIC_NCMD_AUTONEG_ON  0x1
#define OCTNIC_NCMD_PHY_ON      0x2

static int lio_get_link_ksettings(struct net_device *netdev,
				  struct ethtool_link_ksettings *ecmd)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct oct_link_info *linfo;
	u32 supported = 0, advertising = 0;

	linfo = &lio->linfo;

	if (linfo->link.s.if_mode == INTERFACE_MODE_XAUI ||
	    linfo->link.s.if_mode == INTERFACE_MODE_RXAUI ||
	    linfo->link.s.if_mode == INTERFACE_MODE_XLAUI ||
	    linfo->link.s.if_mode == INTERFACE_MODE_XFI) {
		ecmd->base.port = PORT_FIBRE;

		if (linfo->link.s.speed == SPEED_10000) {
			supported = SUPPORTED_10000baseT_Full;
			advertising = ADVERTISED_10000baseT_Full;
		}

		supported |= SUPPORTED_FIBRE | SUPPORTED_Pause;
		advertising |= ADVERTISED_Pause;
		ethtool_convert_legacy_u32_to_link_mode(
			ecmd->link_modes.supported, supported);
		ethtool_convert_legacy_u32_to_link_mode(
			ecmd->link_modes.advertising, advertising);
		ecmd->base.autoneg = AUTONEG_DISABLE;

	} else {
		dev_err(&oct->pci_dev->dev, "Unknown link interface reported %d\n",
			linfo->link.s.if_mode);
	}

	if (linfo->link.s.link_up) {
		ecmd->base.speed = linfo->link.s.speed;
		ecmd->base.duplex = linfo->link.s.duplex;
	} else {
		ecmd->base.speed = SPEED_UNKNOWN;
		ecmd->base.duplex = DUPLEX_UNKNOWN;
	}

	return 0;
}

static void
lio_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct lio *lio;
	struct octeon_device *oct;

	lio = GET_LIO(netdev);
	oct = lio->oct_dev;

	memset(drvinfo, 0, sizeof(struct ethtool_drvinfo));
	strcpy(drvinfo->driver, "liquidio");
	strcpy(drvinfo->version, LIQUIDIO_VERSION);
	strncpy(drvinfo->fw_version, oct->fw_info.liquidio_firmware_version,
		ETHTOOL_FWVERS_LEN);
	strncpy(drvinfo->bus_info, pci_name(oct->pci_dev), 32);
}

static void
lio_get_vf_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct octeon_device *oct;
	struct lio *lio;

	lio = GET_LIO(netdev);
	oct = lio->oct_dev;

	memset(drvinfo, 0, sizeof(struct ethtool_drvinfo));
	strcpy(drvinfo->driver, "liquidio_vf");
	strcpy(drvinfo->version, LIQUIDIO_VERSION);
	strncpy(drvinfo->fw_version, oct->fw_info.liquidio_firmware_version,
		ETHTOOL_FWVERS_LEN);
	strncpy(drvinfo->bus_info, pci_name(oct->pci_dev), 32);
}

static void
lio_ethtool_get_channels(struct net_device *dev,
			 struct ethtool_channels *channel)
{
	struct lio *lio = GET_LIO(dev);
	struct octeon_device *oct = lio->oct_dev;
	u32 max_rx = 0, max_tx = 0, tx_count = 0, rx_count = 0;

	if (OCTEON_CN6XXX(oct)) {
		struct octeon_config *conf6x = CHIP_CONF(oct, cn6xxx);

		max_rx = CFG_GET_OQ_MAX_Q(conf6x);
		max_tx = CFG_GET_IQ_MAX_Q(conf6x);
		rx_count = CFG_GET_NUM_RXQS_NIC_IF(conf6x, lio->ifidx);
		tx_count = CFG_GET_NUM_TXQS_NIC_IF(conf6x, lio->ifidx);
	} else if (OCTEON_CN23XX_PF(oct)) {

		max_rx = oct->sriov_info.num_pf_rings;
		max_tx = oct->sriov_info.num_pf_rings;
		rx_count = lio->linfo.num_rxpciq;
		tx_count = lio->linfo.num_txpciq;
	} else if (OCTEON_CN23XX_VF(oct)) {
		max_tx = oct->sriov_info.rings_per_vf;
		max_rx = oct->sriov_info.rings_per_vf;
		rx_count = lio->linfo.num_rxpciq;
		tx_count = lio->linfo.num_txpciq;
	}

	channel->max_rx = max_rx;
	channel->max_tx = max_tx;
	channel->rx_count = rx_count;
	channel->tx_count = tx_count;
}

static int lio_get_eeprom_len(struct net_device *netdev)
{
	u8 buf[128];
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	struct octeon_board_info *board_info;
	int len;

	board_info = (struct octeon_board_info *)(&oct_dev->boardinfo);
	len = sprintf(buf, "boardname:%s serialnum:%s maj:%lld min:%lld\n",
		      board_info->name, board_info->serial_number,
		      board_info->major, board_info->minor);

	return len;
}

static int
lio_get_eeprom(struct net_device *netdev, struct ethtool_eeprom *eeprom,
	       u8 *bytes)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	struct octeon_board_info *board_info;

	if (eeprom->offset)
		return -EINVAL;

	eeprom->magic = oct_dev->pci_dev->vendor;
	board_info = (struct octeon_board_info *)(&oct_dev->boardinfo);
	sprintf((char *)bytes,
		"boardname:%s serialnum:%s maj:%lld min:%lld\n",
		board_info->name, board_info->serial_number,
		board_info->major, board_info->minor);

	return 0;
}

static int octnet_gpio_access(struct net_device *netdev, int addr, int val)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_GPIO_ACCESS;
	nctrl.ncmd.s.param1 = addr;
	nctrl.ncmd.s.param2 = val;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "Failed to configure gpio value\n");
		return -EINVAL;
	}

	return 0;
}

static int octnet_id_active(struct net_device *netdev, int val)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_ID_ACTIVE;
	nctrl.ncmd.s.param1 = val;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "Failed to configure gpio value\n");
		return -EINVAL;
	}

	return 0;
}

/* Callback for when mdio command response arrives
 */
static void octnet_mdio_resp_callback(struct octeon_device *oct,
				      u32 status,
				      void *buf)
{
	struct oct_mdio_cmd_context *mdio_cmd_ctx;
	struct octeon_soft_command *sc = (struct octeon_soft_command *)buf;

	mdio_cmd_ctx = (struct oct_mdio_cmd_context *)sc->ctxptr;

	oct = lio_get_device(mdio_cmd_ctx->octeon_id);
	if (status) {
		dev_err(&oct->pci_dev->dev, "MIDO instruction failed. Status: %llx\n",
			CVM_CAST64(status));
		WRITE_ONCE(mdio_cmd_ctx->cond, -1);
	} else {
		WRITE_ONCE(mdio_cmd_ctx->cond, 1);
	}
	wake_up_interruptible(&mdio_cmd_ctx->wc);
}

/* This routine provides PHY access routines for
 * mdio  clause45 .
 */
static int
octnet_mdio45_access(struct lio *lio, int op, int loc, int *value)
{
	struct octeon_device *oct_dev = lio->oct_dev;
	struct octeon_soft_command *sc;
	struct oct_mdio_cmd_resp *mdio_cmd_rsp;
	struct oct_mdio_cmd_context *mdio_cmd_ctx;
	struct oct_mdio_cmd *mdio_cmd;
	int retval = 0;

	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct_dev,
					  sizeof(struct oct_mdio_cmd),
					  sizeof(struct oct_mdio_cmd_resp),
					  sizeof(struct oct_mdio_cmd_context));

	if (!sc)
		return -ENOMEM;

	mdio_cmd_ctx = (struct oct_mdio_cmd_context *)sc->ctxptr;
	mdio_cmd_rsp = (struct oct_mdio_cmd_resp *)sc->virtrptr;
	mdio_cmd = (struct oct_mdio_cmd *)sc->virtdptr;

	WRITE_ONCE(mdio_cmd_ctx->cond, 0);
	mdio_cmd_ctx->octeon_id = lio_get_device_id(oct_dev);
	mdio_cmd->op = op;
	mdio_cmd->mdio_addr = loc;
	if (op)
		mdio_cmd->value1 = *value;
	octeon_swap_8B_data((u64 *)mdio_cmd, sizeof(struct oct_mdio_cmd) / 8);

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	octeon_prepare_soft_command(oct_dev, sc, OPCODE_NIC, OPCODE_NIC_MDIO45,
				    0, 0, 0);

	sc->wait_time = 1000;
	sc->callback = octnet_mdio_resp_callback;
	sc->callback_arg = sc;

	init_waitqueue_head(&mdio_cmd_ctx->wc);

	retval = octeon_send_soft_command(oct_dev, sc);

	if (retval == IQ_SEND_FAILED) {
		dev_err(&oct_dev->pci_dev->dev,
			"octnet_mdio45_access instruction failed status: %x\n",
			retval);
		retval = -EBUSY;
	} else {
		/* Sleep on a wait queue till the cond flag indicates that the
		 * response arrived
		 */
		sleep_cond(&mdio_cmd_ctx->wc, &mdio_cmd_ctx->cond);
		retval = mdio_cmd_rsp->status;
		if (retval) {
			dev_err(&oct_dev->pci_dev->dev, "octnet mdio45 access failed\n");
			retval = -EBUSY;
		} else {
			octeon_swap_8B_data((u64 *)(&mdio_cmd_rsp->resp),
					    sizeof(struct oct_mdio_cmd) / 8);

			if (READ_ONCE(mdio_cmd_ctx->cond) == 1) {
				if (!op)
					*value = mdio_cmd_rsp->resp.value1;
			} else {
				retval = -EINVAL;
			}
		}
	}

	octeon_free_soft_command(oct_dev, sc);

	return retval;
}

static int lio_set_phys_id(struct net_device *netdev,
			   enum ethtool_phys_id_state state)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	int value, ret;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		if (oct->chip_id == OCTEON_CN66XX) {
			octnet_gpio_access(netdev, VITESSE_PHY_GPIO_CFG,
					   VITESSE_PHY_GPIO_DRIVEON);
			return 2;

		} else if (oct->chip_id == OCTEON_CN68XX) {
			/* Save the current LED settings */
			ret = octnet_mdio45_access(lio, 0,
						   LIO68XX_LED_BEACON_ADDR,
						   &lio->phy_beacon_val);
			if (ret)
				return ret;

			ret = octnet_mdio45_access(lio, 0,
						   LIO68XX_LED_CTRL_ADDR,
						   &lio->led_ctrl_val);
			if (ret)
				return ret;

			/* Configure Beacon values */
			value = LIO68XX_LED_BEACON_CFGON;
			ret = octnet_mdio45_access(lio, 1,
						   LIO68XX_LED_BEACON_ADDR,
						   &value);
			if (ret)
				return ret;

			value = LIO68XX_LED_CTRL_CFGON;
			ret = octnet_mdio45_access(lio, 1,
						   LIO68XX_LED_CTRL_ADDR,
						   &value);
			if (ret)
				return ret;
		} else if (oct->chip_id == OCTEON_CN23XX_PF_VID) {
			octnet_id_active(netdev, LED_IDENTIFICATION_ON);

			/* returns 0 since updates are asynchronous */
			return 0;
		} else {
			return -EINVAL;
		}
		break;

	case ETHTOOL_ID_ON:
		if (oct->chip_id == OCTEON_CN66XX) {
			octnet_gpio_access(netdev, VITESSE_PHY_GPIO_CFG,
					   VITESSE_PHY_GPIO_HIGH);

		} else if (oct->chip_id == OCTEON_CN68XX) {
			return -EINVAL;
		} else {
			return -EINVAL;
		}
		break;

	case ETHTOOL_ID_OFF:
		if (oct->chip_id == OCTEON_CN66XX)
			octnet_gpio_access(netdev, VITESSE_PHY_GPIO_CFG,
					   VITESSE_PHY_GPIO_LOW);
		else if (oct->chip_id == OCTEON_CN68XX)
			return -EINVAL;
		else
			return -EINVAL;

		break;

	case ETHTOOL_ID_INACTIVE:
		if (oct->chip_id == OCTEON_CN66XX) {
			octnet_gpio_access(netdev, VITESSE_PHY_GPIO_CFG,
					   VITESSE_PHY_GPIO_DRIVEOFF);
		} else if (oct->chip_id == OCTEON_CN68XX) {
			/* Restore LED settings */
			ret = octnet_mdio45_access(lio, 1,
						   LIO68XX_LED_CTRL_ADDR,
						   &lio->led_ctrl_val);
			if (ret)
				return ret;

			ret = octnet_mdio45_access(lio, 1,
						   LIO68XX_LED_BEACON_ADDR,
						   &lio->phy_beacon_val);
			if (ret)
				return ret;
		} else if (oct->chip_id == OCTEON_CN23XX_PF_VID) {
			octnet_id_active(netdev, LED_IDENTIFICATION_OFF);

			return 0;
		} else {
			return -EINVAL;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void
lio_ethtool_get_ringparam(struct net_device *netdev,
			  struct ethtool_ringparam *ering)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	u32 tx_max_pending = 0, rx_max_pending = 0, tx_pending = 0,
	    rx_pending = 0;

	if (OCTEON_CN6XXX(oct)) {
		struct octeon_config *conf6x = CHIP_CONF(oct, cn6xxx);

		tx_max_pending = CN6XXX_MAX_IQ_DESCRIPTORS;
		rx_max_pending = CN6XXX_MAX_OQ_DESCRIPTORS;
		rx_pending = CFG_GET_NUM_RX_DESCS_NIC_IF(conf6x, lio->ifidx);
		tx_pending = CFG_GET_NUM_TX_DESCS_NIC_IF(conf6x, lio->ifidx);
	} else if (OCTEON_CN23XX_PF(oct)) {
		struct octeon_config *conf23 = CHIP_CONF(oct, cn23xx_pf);

		tx_max_pending = CN23XX_MAX_IQ_DESCRIPTORS;
		rx_max_pending = CN23XX_MAX_OQ_DESCRIPTORS;
		rx_pending = CFG_GET_NUM_RX_DESCS_NIC_IF(conf23, lio->ifidx);
		tx_pending = CFG_GET_NUM_TX_DESCS_NIC_IF(conf23, lio->ifidx);
	}

	if (lio->mtu > OCTNET_DEFAULT_FRM_SIZE - OCTNET_FRM_HEADER_SIZE) {
		ering->rx_pending = 0;
		ering->rx_max_pending = 0;
		ering->rx_mini_pending = 0;
		ering->rx_jumbo_pending = rx_pending;
		ering->rx_mini_max_pending = 0;
		ering->rx_jumbo_max_pending = rx_max_pending;
	} else {
		ering->rx_pending = rx_pending;
		ering->rx_max_pending = rx_max_pending;
		ering->rx_mini_pending = 0;
		ering->rx_jumbo_pending = 0;
		ering->rx_mini_max_pending = 0;
		ering->rx_jumbo_max_pending = 0;
	}

	ering->tx_pending = tx_pending;
	ering->tx_max_pending = tx_max_pending;
}

static u32 lio_get_msglevel(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	return lio->msg_enable;
}

static void lio_set_msglevel(struct net_device *netdev, u32 msglvl)
{
	struct lio *lio = GET_LIO(netdev);

	if ((msglvl ^ lio->msg_enable) & NETIF_MSG_HW) {
		if (msglvl & NETIF_MSG_HW)
			liquidio_set_feature(netdev,
					     OCTNET_CMD_VERBOSE_ENABLE, 0);
		else
			liquidio_set_feature(netdev,
					     OCTNET_CMD_VERBOSE_DISABLE, 0);
	}

	lio->msg_enable = msglvl;
}

static void lio_vf_set_msglevel(struct net_device *netdev, u32 msglvl)
{
	struct lio *lio = GET_LIO(netdev);

	lio->msg_enable = msglvl;
}

static void
lio_get_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *pause)
{
	/* Notes: Not supporting any auto negotiation in these
	 * drivers. Just report pause frame support.
	 */
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	pause->autoneg = 0;

	pause->tx_pause = oct->tx_pause;
	pause->rx_pause = oct->rx_pause;
}

static int
lio_set_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *pause)
{
	/* Notes: Not supporting any auto negotiation in these
	 * drivers.
	 */
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	struct oct_link_info *linfo = &lio->linfo;

	int ret = 0;

	if (oct->chip_id != OCTEON_CN23XX_PF_VID)
		return -EINVAL;

	if (linfo->link.s.duplex == 0) {
		/*no flow control for half duplex*/
		if (pause->rx_pause || pause->tx_pause)
			return -EINVAL;
	}

	/*do not support autoneg of link flow control*/
	if (pause->autoneg == AUTONEG_ENABLE)
		return -EINVAL;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_SET_FLOW_CTL;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	if (pause->rx_pause) {
		/*enable rx pause*/
		nctrl.ncmd.s.param1 = 1;
	} else {
		/*disable rx pause*/
		nctrl.ncmd.s.param1 = 0;
	}

	if (pause->tx_pause) {
		/*enable tx pause*/
		nctrl.ncmd.s.param2 = 1;
	} else {
		/*disable tx pause*/
		nctrl.ncmd.s.param2 = 0;
	}

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "Failed to set pause parameter\n");
		return -EINVAL;
	}

	oct->rx_pause = pause->rx_pause;
	oct->tx_pause = pause->tx_pause;

	return 0;
}

static void
lio_get_ethtool_stats(struct net_device *netdev,
		      struct ethtool_stats *stats  __attribute__((unused)),
		      u64 *data)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	struct net_device_stats *netstats = &netdev->stats;
	int i = 0, j;

	netdev->netdev_ops->ndo_get_stats(netdev);
	octnet_get_link_stats(netdev);

	/*sum of oct->droq[oq_no]->stats->rx_pkts_received */
	data[i++] = CVM_CAST64(netstats->rx_packets);
	/*sum of oct->instr_queue[iq_no]->stats.tx_done */
	data[i++] = CVM_CAST64(netstats->tx_packets);
	/*sum of oct->droq[oq_no]->stats->rx_bytes_received */
	data[i++] = CVM_CAST64(netstats->rx_bytes);
	/*sum of oct->instr_queue[iq_no]->stats.tx_tot_bytes */
	data[i++] = CVM_CAST64(netstats->tx_bytes);
	data[i++] = CVM_CAST64(netstats->rx_errors);
	data[i++] = CVM_CAST64(netstats->tx_errors);
	/*sum of oct->droq[oq_no]->stats->rx_dropped +
	 *oct->droq[oq_no]->stats->dropped_nodispatch +
	 *oct->droq[oq_no]->stats->dropped_toomany +
	 *oct->droq[oq_no]->stats->dropped_nomem
	 */
	data[i++] = CVM_CAST64(netstats->rx_dropped);
	/*sum of oct->instr_queue[iq_no]->stats.tx_dropped */
	data[i++] = CVM_CAST64(netstats->tx_dropped);

	/* firmware tx stats */
	/*per_core_stats[cvmx_get_core_num()].link_stats[mdata->from_ifidx].
	 *fromhost.fw_total_sent
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_total_sent);
	/*per_core_stats[i].link_stats[port].fromwire.fw_total_fwd */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_total_fwd);
	/*per_core_stats[j].link_stats[i].fromhost.fw_err_pko */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_err_pko);
	/*per_core_stats[j].link_stats[i].fromhost.fw_err_link */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_err_link);
	/*per_core_stats[cvmx_get_core_num()].link_stats[idx].fromhost.
	 *fw_err_drop
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_err_drop);

	/*per_core_stats[cvmx_get_core_num()].link_stats[idx].fromhost.fw_tso */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_tso);
	/*per_core_stats[cvmx_get_core_num()].link_stats[idx].fromhost.
	 *fw_tso_fwd
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_tso_fwd);
	/*per_core_stats[cvmx_get_core_num()].link_stats[idx].fromhost.
	 *fw_err_tso
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_err_tso);
	/*per_core_stats[cvmx_get_core_num()].link_stats[idx].fromhost.
	 *fw_tx_vxlan
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_tx_vxlan);

	/* mac tx statistics */
	/*CVMX_BGXX_CMRX_TX_STAT5 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.total_pkts_sent);
	/*CVMX_BGXX_CMRX_TX_STAT4 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.total_bytes_sent);
	/*CVMX_BGXX_CMRX_TX_STAT15 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.mcast_pkts_sent);
	/*CVMX_BGXX_CMRX_TX_STAT14 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.bcast_pkts_sent);
	/*CVMX_BGXX_CMRX_TX_STAT17 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.ctl_sent);
	/*CVMX_BGXX_CMRX_TX_STAT0 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.total_collisions);
	/*CVMX_BGXX_CMRX_TX_STAT3 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.one_collision_sent);
	/*CVMX_BGXX_CMRX_TX_STAT2 */
	data[i++] =
		CVM_CAST64(oct_dev->link_stats.fromhost.multi_collision_sent);
	/*CVMX_BGXX_CMRX_TX_STAT0 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.max_collision_fail);
	/*CVMX_BGXX_CMRX_TX_STAT1 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.max_deferral_fail);
	/*CVMX_BGXX_CMRX_TX_STAT16 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fifo_err);
	/*CVMX_BGXX_CMRX_TX_STAT6 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.runts);

	/* RX firmware stats */
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_total_rcvd
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_total_rcvd);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_total_fwd
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_total_fwd);
	/*per_core_stats[core_id].link_stats[ifidx].fromwire.jabber_err */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.jabber_err);
	/*per_core_stats[core_id].link_stats[ifidx].fromwire.l2_err */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.l2_err);
	/*per_core_stats[core_id].link_stats[ifidx].fromwire.frame_err */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.frame_err);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_err_pko
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_err_pko);
	/*per_core_stats[j].link_stats[i].fromwire.fw_err_link */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_err_link);
	/*per_core_stats[cvmx_get_core_num()].link_stats[lro_ctx->ifidx].
	 *fromwire.fw_err_drop
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_err_drop);

	/*per_core_stats[cvmx_get_core_num()].link_stats[lro_ctx->ifidx].
	 *fromwire.fw_rx_vxlan
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_rx_vxlan);
	/*per_core_stats[cvmx_get_core_num()].link_stats[lro_ctx->ifidx].
	 *fromwire.fw_rx_vxlan_err
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_rx_vxlan_err);

	/* LRO */
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_pkts
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_pkts);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_octs
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_octs);
	/*per_core_stats[j].link_stats[i].fromwire.fw_total_lro */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_total_lro);
	/*per_core_stats[j].link_stats[i].fromwire.fw_lro_aborts */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_aborts);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_aborts_port
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_aborts_port);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_aborts_seq
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_aborts_seq);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_aborts_tsval
	 */
	data[i++] =
		CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_aborts_tsval);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_aborts_timer
	 */
	/* intrmod: packet forward rate */
	data[i++] =
		CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_aborts_timer);
	/*per_core_stats[j].link_stats[i].fromwire.fw_lro_aborts */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fwd_rate);

	/* mac: link-level stats */
	/*CVMX_BGXX_CMRX_RX_STAT0 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.total_rcvd);
	/*CVMX_BGXX_CMRX_RX_STAT1 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.bytes_rcvd);
	/*CVMX_PKI_STATX_STAT5 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.total_bcst);
	/*CVMX_PKI_STATX_STAT5 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.total_mcst);
	/*wqe->word2.err_code or wqe->word2.err_level */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.runts);
	/*CVMX_BGXX_CMRX_RX_STAT2 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.ctl_rcvd);
	/*CVMX_BGXX_CMRX_RX_STAT6 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fifo_err);
	/*CVMX_BGXX_CMRX_RX_STAT4 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.dmac_drop);
	/*wqe->word2.err_code or wqe->word2.err_level */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fcs_err);
	/*lio->link_changes*/
	data[i++] = CVM_CAST64(lio->link_changes);

	for (j = 0; j < MAX_OCTEON_INSTR_QUEUES(oct_dev); j++) {
		if (!(oct_dev->io_qmask.iq & BIT_ULL(j)))
			continue;
		/*packets to network port*/
		/*# of packets tx to network */
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_done);
		/*# of bytes tx to network */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_tot_bytes);
		/*# of packets dropped */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_dropped);
		/*# of tx fails due to queue full */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_iq_busy);
		/*XXX gather entries sent */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.sgentry_sent);

		/*instruction to firmware: data and control */
		/*# of instructions to the queue */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.instr_posted);
		/*# of instructions processed */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.instr_processed);
		/*# of instructions could not be processed */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.instr_dropped);
		/*bytes sent through the queue */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.bytes_sent);

		/*tso request*/
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_gso);
		/*vxlan request*/
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_vxlan);
		/*txq restart*/
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_restart);
	}

	/* RX */
	for (j = 0; j < MAX_OCTEON_OUTPUT_QUEUES(oct_dev); j++) {
		if (!(oct_dev->io_qmask.oq & BIT_ULL(j)))
			continue;

		/*packets send to TCP/IP network stack */
		/*# of packets to network stack */
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_pkts_received);
		/*# of bytes to network stack */
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_bytes_received);
		/*# of packets dropped */
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.dropped_nomem +
				       oct_dev->droq[j]->stats.dropped_toomany +
				       oct_dev->droq[j]->stats.rx_dropped);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.dropped_nomem);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.dropped_toomany);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_dropped);

		/*control and data path*/
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.pkts_received);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.bytes_received);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.dropped_nodispatch);

		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_vxlan);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_alloc_failure);
	}
}

static void lio_vf_get_ethtool_stats(struct net_device *netdev,
				     struct ethtool_stats *stats
				     __attribute__((unused)),
				     u64 *data)
{
	struct net_device_stats *netstats = &netdev->stats;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	int i = 0, j, vj;

	netdev->netdev_ops->ndo_get_stats(netdev);
	/* sum of oct->droq[oq_no]->stats->rx_pkts_received */
	data[i++] = CVM_CAST64(netstats->rx_packets);
	/* sum of oct->instr_queue[iq_no]->stats.tx_done */
	data[i++] = CVM_CAST64(netstats->tx_packets);
	/* sum of oct->droq[oq_no]->stats->rx_bytes_received */
	data[i++] = CVM_CAST64(netstats->rx_bytes);
	/* sum of oct->instr_queue[iq_no]->stats.tx_tot_bytes */
	data[i++] = CVM_CAST64(netstats->tx_bytes);
	data[i++] = CVM_CAST64(netstats->rx_errors);
	data[i++] = CVM_CAST64(netstats->tx_errors);
	 /* sum of oct->droq[oq_no]->stats->rx_dropped +
	  * oct->droq[oq_no]->stats->dropped_nodispatch +
	  * oct->droq[oq_no]->stats->dropped_toomany +
	  * oct->droq[oq_no]->stats->dropped_nomem
	  */
	data[i++] = CVM_CAST64(netstats->rx_dropped);
	/* sum of oct->instr_queue[iq_no]->stats.tx_dropped */
	data[i++] = CVM_CAST64(netstats->tx_dropped);
	/* lio->link_changes */
	data[i++] = CVM_CAST64(lio->link_changes);

	for (vj = 0; vj < lio->linfo.num_txpciq; vj++) {
		j = lio->linfo.txpciq[vj].s.q_no;

		/* packets to network port */
		/* # of packets tx to network */
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_done);
		 /* # of bytes tx to network */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.tx_tot_bytes);
		/* # of packets dropped */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.tx_dropped);
		/* # of tx fails due to queue full */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.tx_iq_busy);
		/* XXX gather entries sent */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.sgentry_sent);

		/* instruction to firmware: data and control */
		/* # of instructions to the queue */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.instr_posted);
		/* # of instructions processed */
		data[i++] =
		    CVM_CAST64(oct_dev->instr_queue[j]->stats.instr_processed);
		/* # of instructions could not be processed */
		data[i++] =
		    CVM_CAST64(oct_dev->instr_queue[j]->stats.instr_dropped);
		/* bytes sent through the queue */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.bytes_sent);
		/* tso request */
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_gso);
		/* vxlan request */
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_vxlan);
		/* txq restart */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.tx_restart);
	}

	/* RX */
	for (vj = 0; vj < lio->linfo.num_rxpciq; vj++) {
		j = lio->linfo.rxpciq[vj].s.q_no;

		/* packets send to TCP/IP network stack */
		/* # of packets to network stack */
		data[i++] = CVM_CAST64(
				oct_dev->droq[j]->stats.rx_pkts_received);
		/* # of bytes to network stack */
		data[i++] = CVM_CAST64(
				oct_dev->droq[j]->stats.rx_bytes_received);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.dropped_nomem +
				       oct_dev->droq[j]->stats.dropped_toomany +
				       oct_dev->droq[j]->stats.rx_dropped);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.dropped_nomem);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.dropped_toomany);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.rx_dropped);

		/* control and data path */
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.pkts_received);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.bytes_received);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.dropped_nodispatch);

		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.rx_vxlan);
		data[i++] =
		    CVM_CAST64(oct_dev->droq[j]->stats.rx_alloc_failure);
	}
}

static void lio_get_priv_flags_strings(struct lio *lio, u8 *data)
{
	struct octeon_device *oct_dev = lio->oct_dev;
	int i;

	switch (oct_dev->chip_id) {
	case OCTEON_CN23XX_PF_VID:
	case OCTEON_CN23XX_VF_VID:
		for (i = 0; i < ARRAY_SIZE(oct_priv_flags_strings); i++) {
			sprintf(data, "%s", oct_priv_flags_strings[i]);
			data += ETH_GSTRING_LEN;
		}
		break;
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		break;
	default:
		netif_info(lio, drv, lio->netdev, "Unknown Chip !!\n");
		break;
	}
}

static void lio_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	int num_iq_stats, num_oq_stats, i, j;
	int num_stats;

	switch (stringset) {
	case ETH_SS_STATS:
		num_stats = ARRAY_SIZE(oct_stats_strings);
		for (j = 0; j < num_stats; j++) {
			sprintf(data, "%s", oct_stats_strings[j]);
			data += ETH_GSTRING_LEN;
		}

		num_iq_stats = ARRAY_SIZE(oct_iq_stats_strings);
		for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct_dev); i++) {
			if (!(oct_dev->io_qmask.iq & BIT_ULL(i)))
				continue;
			for (j = 0; j < num_iq_stats; j++) {
				sprintf(data, "tx-%d-%s", i,
					oct_iq_stats_strings[j]);
				data += ETH_GSTRING_LEN;
			}
		}

		num_oq_stats = ARRAY_SIZE(oct_droq_stats_strings);
		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct_dev); i++) {
			if (!(oct_dev->io_qmask.oq & BIT_ULL(i)))
				continue;
			for (j = 0; j < num_oq_stats; j++) {
				sprintf(data, "rx-%d-%s", i,
					oct_droq_stats_strings[j]);
				data += ETH_GSTRING_LEN;
			}
		}
		break;

	case ETH_SS_PRIV_FLAGS:
		lio_get_priv_flags_strings(lio, data);
		break;
	default:
		netif_info(lio, drv, lio->netdev, "Unknown Stringset !!\n");
		break;
	}
}

static void lio_vf_get_strings(struct net_device *netdev, u32 stringset,
			       u8 *data)
{
	int num_iq_stats, num_oq_stats, i, j;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	int num_stats;

	switch (stringset) {
	case ETH_SS_STATS:
		num_stats = ARRAY_SIZE(oct_vf_stats_strings);
		for (j = 0; j < num_stats; j++) {
			sprintf(data, "%s", oct_vf_stats_strings[j]);
			data += ETH_GSTRING_LEN;
		}

		num_iq_stats = ARRAY_SIZE(oct_iq_stats_strings);
		for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct_dev); i++) {
			if (!(oct_dev->io_qmask.iq & BIT_ULL(i)))
				continue;
			for (j = 0; j < num_iq_stats; j++) {
				sprintf(data, "tx-%d-%s", i,
					oct_iq_stats_strings[j]);
				data += ETH_GSTRING_LEN;
			}
		}

		num_oq_stats = ARRAY_SIZE(oct_droq_stats_strings);
		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct_dev); i++) {
			if (!(oct_dev->io_qmask.oq & BIT_ULL(i)))
				continue;
			for (j = 0; j < num_oq_stats; j++) {
				sprintf(data, "rx-%d-%s", i,
					oct_droq_stats_strings[j]);
				data += ETH_GSTRING_LEN;
			}
		}
		break;

	case ETH_SS_PRIV_FLAGS:
		lio_get_priv_flags_strings(lio, data);
		break;
	default:
		netif_info(lio, drv, lio->netdev, "Unknown Stringset !!\n");
		break;
	}
}

static int lio_get_priv_flags_ss_count(struct lio *lio)
{
	struct octeon_device *oct_dev = lio->oct_dev;

	switch (oct_dev->chip_id) {
	case OCTEON_CN23XX_PF_VID:
	case OCTEON_CN23XX_VF_VID:
		return ARRAY_SIZE(oct_priv_flags_strings);
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		return -EOPNOTSUPP;
	default:
		netif_info(lio, drv, lio->netdev, "Unknown Chip !!\n");
		return -EOPNOTSUPP;
	}
}

static int lio_get_sset_count(struct net_device *netdev, int sset)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;

	switch (sset) {
	case ETH_SS_STATS:
		return (ARRAY_SIZE(oct_stats_strings) +
			ARRAY_SIZE(oct_iq_stats_strings) * oct_dev->num_iqs +
			ARRAY_SIZE(oct_droq_stats_strings) * oct_dev->num_oqs);
	case ETH_SS_PRIV_FLAGS:
		return lio_get_priv_flags_ss_count(lio);
	default:
		return -EOPNOTSUPP;
	}
}

static int lio_vf_get_sset_count(struct net_device *netdev, int sset)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;

	switch (sset) {
	case ETH_SS_STATS:
		return (ARRAY_SIZE(oct_vf_stats_strings) +
			ARRAY_SIZE(oct_iq_stats_strings) * oct_dev->num_iqs +
			ARRAY_SIZE(oct_droq_stats_strings) * oct_dev->num_oqs);
	case ETH_SS_PRIV_FLAGS:
		return lio_get_priv_flags_ss_count(lio);
	default:
		return -EOPNOTSUPP;
	}
}

/* Callback function for intrmod */
static void octnet_intrmod_callback(struct octeon_device *oct_dev,
				    u32 status,
				    void *ptr)
{
	struct octeon_soft_command *sc = (struct octeon_soft_command *)ptr;
	struct oct_intrmod_context *ctx;

	ctx  = (struct oct_intrmod_context *)sc->ctxptr;

	ctx->status = status;

	WRITE_ONCE(ctx->cond, 1);

	/* This barrier is required to be sure that the response has been
	 * written fully before waking up the handler
	 */
	wmb();

	wake_up_interruptible(&ctx->wc);
}

/*  get interrupt moderation parameters */
static int octnet_get_intrmod_cfg(struct lio *lio,
				  struct oct_intrmod_cfg *intr_cfg)
{
	struct octeon_soft_command *sc;
	struct oct_intrmod_context *ctx;
	struct oct_intrmod_resp *resp;
	int retval;
	struct octeon_device *oct_dev = lio->oct_dev;

	/* Alloc soft command */
	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct_dev,
					  0,
					  sizeof(struct oct_intrmod_resp),
					  sizeof(struct oct_intrmod_context));

	if (!sc)
		return -ENOMEM;

	resp = (struct oct_intrmod_resp *)sc->virtrptr;
	memset(resp, 0, sizeof(struct oct_intrmod_resp));

	ctx = (struct oct_intrmod_context *)sc->ctxptr;
	memset(ctx, 0, sizeof(struct oct_intrmod_context));
	WRITE_ONCE(ctx->cond, 0);
	ctx->octeon_id = lio_get_device_id(oct_dev);
	init_waitqueue_head(&ctx->wc);

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	octeon_prepare_soft_command(oct_dev, sc, OPCODE_NIC,
				    OPCODE_NIC_INTRMOD_PARAMS, 0, 0, 0);

	sc->callback = octnet_intrmod_callback;
	sc->callback_arg = sc;
	sc->wait_time = 1000;

	retval = octeon_send_soft_command(oct_dev, sc);
	if (retval == IQ_SEND_FAILED) {
		octeon_free_soft_command(oct_dev, sc);
		return -EINVAL;
	}

	/* Sleep on a wait queue till the cond flag indicates that the
	 * response arrived or timed-out.
	 */
	if (sleep_cond(&ctx->wc, &ctx->cond) == -EINTR) {
		dev_err(&oct_dev->pci_dev->dev, "Wait interrupted\n");
		goto intrmod_info_wait_intr;
	}

	retval = ctx->status || resp->status;
	if (retval) {
		dev_err(&oct_dev->pci_dev->dev,
			"Get interrupt moderation parameters failed\n");
		goto intrmod_info_wait_fail;
	}

	octeon_swap_8B_data((u64 *)&resp->intrmod,
			    (sizeof(struct oct_intrmod_cfg)) / 8);
	memcpy(intr_cfg, &resp->intrmod, sizeof(struct oct_intrmod_cfg));
	octeon_free_soft_command(oct_dev, sc);

	return 0;

intrmod_info_wait_fail:

	octeon_free_soft_command(oct_dev, sc);

intrmod_info_wait_intr:

	return -ENODEV;
}

/*  Configure interrupt moderation parameters */
static int octnet_set_intrmod_cfg(struct lio *lio,
				  struct oct_intrmod_cfg *intr_cfg)
{
	struct octeon_soft_command *sc;
	struct oct_intrmod_context *ctx;
	struct oct_intrmod_cfg *cfg;
	int retval;
	struct octeon_device *oct_dev = lio->oct_dev;

	/* Alloc soft command */
	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct_dev,
					  sizeof(struct oct_intrmod_cfg),
					  0,
					  sizeof(struct oct_intrmod_context));

	if (!sc)
		return -ENOMEM;

	ctx = (struct oct_intrmod_context *)sc->ctxptr;

	WRITE_ONCE(ctx->cond, 0);
	ctx->octeon_id = lio_get_device_id(oct_dev);
	init_waitqueue_head(&ctx->wc);

	cfg = (struct oct_intrmod_cfg *)sc->virtdptr;

	memcpy(cfg, intr_cfg, sizeof(struct oct_intrmod_cfg));
	octeon_swap_8B_data((u64 *)cfg, (sizeof(struct oct_intrmod_cfg)) / 8);

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	octeon_prepare_soft_command(oct_dev, sc, OPCODE_NIC,
				    OPCODE_NIC_INTRMOD_CFG, 0, 0, 0);

	sc->callback = octnet_intrmod_callback;
	sc->callback_arg = sc;
	sc->wait_time = 1000;

	retval = octeon_send_soft_command(oct_dev, sc);
	if (retval == IQ_SEND_FAILED) {
		octeon_free_soft_command(oct_dev, sc);
		return -EINVAL;
	}

	/* Sleep on a wait queue till the cond flag indicates that the
	 * response arrived or timed-out.
	 */
	if (sleep_cond(&ctx->wc, &ctx->cond) != -EINTR) {
		retval = ctx->status;
		if (retval)
			dev_err(&oct_dev->pci_dev->dev,
				"intrmod config failed. Status: %llx\n",
				CVM_CAST64(retval));
		else
			dev_info(&oct_dev->pci_dev->dev,
				 "Rx-Adaptive Interrupt moderation %s\n",
				 (intr_cfg->rx_enable) ?
				 "enabled" : "disabled");

		octeon_free_soft_command(oct_dev, sc);

		return ((retval) ? -ENODEV : 0);
	}

	dev_err(&oct_dev->pci_dev->dev, "iq/oq config failed\n");

	return -EINTR;
}

static void
octnet_nic_stats_callback(struct octeon_device *oct_dev,
			  u32 status, void *ptr)
{
	struct octeon_soft_command *sc = (struct octeon_soft_command *)ptr;
	struct oct_nic_stats_resp *resp =
	    (struct oct_nic_stats_resp *)sc->virtrptr;
	struct oct_nic_stats_ctrl *ctrl =
	    (struct oct_nic_stats_ctrl *)sc->ctxptr;
	struct nic_rx_stats *rsp_rstats = &resp->stats.fromwire;
	struct nic_tx_stats *rsp_tstats = &resp->stats.fromhost;

	struct nic_rx_stats *rstats = &oct_dev->link_stats.fromwire;
	struct nic_tx_stats *tstats = &oct_dev->link_stats.fromhost;

	if ((status != OCTEON_REQUEST_TIMEOUT) && !resp->status) {
		octeon_swap_8B_data((u64 *)&resp->stats,
				    (sizeof(struct oct_link_stats)) >> 3);

		/* RX link-level stats */
		rstats->total_rcvd = rsp_rstats->total_rcvd;
		rstats->bytes_rcvd = rsp_rstats->bytes_rcvd;
		rstats->total_bcst = rsp_rstats->total_bcst;
		rstats->total_mcst = rsp_rstats->total_mcst;
		rstats->runts      = rsp_rstats->runts;
		rstats->ctl_rcvd   = rsp_rstats->ctl_rcvd;
		/* Accounts for over/under-run of buffers */
		rstats->fifo_err  = rsp_rstats->fifo_err;
		rstats->dmac_drop = rsp_rstats->dmac_drop;
		rstats->fcs_err   = rsp_rstats->fcs_err;
		rstats->jabber_err = rsp_rstats->jabber_err;
		rstats->l2_err    = rsp_rstats->l2_err;
		rstats->frame_err = rsp_rstats->frame_err;

		/* RX firmware stats */
		rstats->fw_total_rcvd = rsp_rstats->fw_total_rcvd;
		rstats->fw_total_fwd = rsp_rstats->fw_total_fwd;
		rstats->fw_err_pko = rsp_rstats->fw_err_pko;
		rstats->fw_err_link = rsp_rstats->fw_err_link;
		rstats->fw_err_drop = rsp_rstats->fw_err_drop;
		rstats->fw_rx_vxlan = rsp_rstats->fw_rx_vxlan;
		rstats->fw_rx_vxlan_err = rsp_rstats->fw_rx_vxlan_err;

		/* Number of packets that are LROed      */
		rstats->fw_lro_pkts = rsp_rstats->fw_lro_pkts;
		/* Number of octets that are LROed       */
		rstats->fw_lro_octs = rsp_rstats->fw_lro_octs;
		/* Number of LRO packets formed          */
		rstats->fw_total_lro = rsp_rstats->fw_total_lro;
		/* Number of times lRO of packet aborted */
		rstats->fw_lro_aborts = rsp_rstats->fw_lro_aborts;
		rstats->fw_lro_aborts_port = rsp_rstats->fw_lro_aborts_port;
		rstats->fw_lro_aborts_seq = rsp_rstats->fw_lro_aborts_seq;
		rstats->fw_lro_aborts_tsval = rsp_rstats->fw_lro_aborts_tsval;
		rstats->fw_lro_aborts_timer = rsp_rstats->fw_lro_aborts_timer;
		/* intrmod: packet forward rate */
		rstats->fwd_rate = rsp_rstats->fwd_rate;

		/* TX link-level stats */
		tstats->total_pkts_sent = rsp_tstats->total_pkts_sent;
		tstats->total_bytes_sent = rsp_tstats->total_bytes_sent;
		tstats->mcast_pkts_sent = rsp_tstats->mcast_pkts_sent;
		tstats->bcast_pkts_sent = rsp_tstats->bcast_pkts_sent;
		tstats->ctl_sent = rsp_tstats->ctl_sent;
		/* Packets sent after one collision*/
		tstats->one_collision_sent = rsp_tstats->one_collision_sent;
		/* Packets sent after multiple collision*/
		tstats->multi_collision_sent = rsp_tstats->multi_collision_sent;
		/* Packets not sent due to max collisions */
		tstats->max_collision_fail = rsp_tstats->max_collision_fail;
		/* Packets not sent due to max deferrals */
		tstats->max_deferral_fail = rsp_tstats->max_deferral_fail;
		/* Accounts for over/under-run of buffers */
		tstats->fifo_err = rsp_tstats->fifo_err;
		tstats->runts = rsp_tstats->runts;
		/* Total number of collisions detected */
		tstats->total_collisions = rsp_tstats->total_collisions;

		/* firmware stats */
		tstats->fw_total_sent = rsp_tstats->fw_total_sent;
		tstats->fw_total_fwd = rsp_tstats->fw_total_fwd;
		tstats->fw_err_pko = rsp_tstats->fw_err_pko;
		tstats->fw_err_link = rsp_tstats->fw_err_link;
		tstats->fw_err_drop = rsp_tstats->fw_err_drop;
		tstats->fw_tso = rsp_tstats->fw_tso;
		tstats->fw_tso_fwd = rsp_tstats->fw_tso_fwd;
		tstats->fw_err_tso = rsp_tstats->fw_err_tso;
		tstats->fw_tx_vxlan = rsp_tstats->fw_tx_vxlan;

		resp->status = 1;
	} else {
		resp->status = -1;
	}
	complete(&ctrl->complete);
}

/*  Configure interrupt moderation parameters */
static int octnet_get_link_stats(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;

	struct octeon_soft_command *sc;
	struct oct_nic_stats_ctrl *ctrl;
	struct oct_nic_stats_resp *resp;

	int retval;

	/* Alloc soft command */
	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct_dev,
					  0,
					  sizeof(struct oct_nic_stats_resp),
					  sizeof(struct octnic_ctrl_pkt));

	if (!sc)
		return -ENOMEM;

	resp = (struct oct_nic_stats_resp *)sc->virtrptr;
	memset(resp, 0, sizeof(struct oct_nic_stats_resp));

	ctrl = (struct oct_nic_stats_ctrl *)sc->ctxptr;
	memset(ctrl, 0, sizeof(struct oct_nic_stats_ctrl));
	ctrl->netdev = netdev;
	init_completion(&ctrl->complete);

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	octeon_prepare_soft_command(oct_dev, sc, OPCODE_NIC,
				    OPCODE_NIC_PORT_STATS, 0, 0, 0);

	sc->callback = octnet_nic_stats_callback;
	sc->callback_arg = sc;
	sc->wait_time = 500;	/*in milli seconds*/

	retval = octeon_send_soft_command(oct_dev, sc);
	if (retval == IQ_SEND_FAILED) {
		octeon_free_soft_command(oct_dev, sc);
		return -EINVAL;
	}

	wait_for_completion_timeout(&ctrl->complete, msecs_to_jiffies(1000));

	if (resp->status != 1) {
		octeon_free_soft_command(oct_dev, sc);

		return -EINVAL;
	}

	octeon_free_soft_command(oct_dev, sc);

	return 0;
}

static int lio_get_intr_coalesce(struct net_device *netdev,
				 struct ethtool_coalesce *intr_coal)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_instr_queue *iq;
	struct oct_intrmod_cfg intrmod_cfg;

	if (octnet_get_intrmod_cfg(lio, &intrmod_cfg))
		return -ENODEV;

	switch (oct->chip_id) {
	case OCTEON_CN23XX_PF_VID:
	case OCTEON_CN23XX_VF_VID: {
		if (!intrmod_cfg.rx_enable) {
			intr_coal->rx_coalesce_usecs = oct->rx_coalesce_usecs;
			intr_coal->rx_max_coalesced_frames =
				oct->rx_max_coalesced_frames;
		}
		if (!intrmod_cfg.tx_enable)
			intr_coal->tx_max_coalesced_frames =
				oct->tx_max_coalesced_frames;
		break;
	}
	case OCTEON_CN68XX:
	case OCTEON_CN66XX: {
		struct octeon_cn6xxx *cn6xxx =
			(struct octeon_cn6xxx *)oct->chip;

		if (!intrmod_cfg.rx_enable) {
			intr_coal->rx_coalesce_usecs =
				CFG_GET_OQ_INTR_TIME(cn6xxx->conf);
			intr_coal->rx_max_coalesced_frames =
				CFG_GET_OQ_INTR_PKT(cn6xxx->conf);
		}
		iq = oct->instr_queue[lio->linfo.txpciq[0].s.q_no];
		intr_coal->tx_max_coalesced_frames = iq->fill_threshold;
		break;
	}
	default:
		netif_info(lio, drv, lio->netdev, "Unknown Chip !!\n");
		return -EINVAL;
	}
	if (intrmod_cfg.rx_enable) {
		intr_coal->use_adaptive_rx_coalesce =
			intrmod_cfg.rx_enable;
		intr_coal->rate_sample_interval =
			intrmod_cfg.check_intrvl;
		intr_coal->pkt_rate_high =
			intrmod_cfg.maxpkt_ratethr;
		intr_coal->pkt_rate_low =
			intrmod_cfg.minpkt_ratethr;
		intr_coal->rx_max_coalesced_frames_high =
			intrmod_cfg.rx_maxcnt_trigger;
		intr_coal->rx_coalesce_usecs_high =
			intrmod_cfg.rx_maxtmr_trigger;
		intr_coal->rx_coalesce_usecs_low =
			intrmod_cfg.rx_mintmr_trigger;
		intr_coal->rx_max_coalesced_frames_low =
			intrmod_cfg.rx_mincnt_trigger;
	}
	if ((OCTEON_CN23XX_PF(oct) || OCTEON_CN23XX_VF(oct)) &&
	    (intrmod_cfg.tx_enable)) {
		intr_coal->use_adaptive_tx_coalesce =
			intrmod_cfg.tx_enable;
		intr_coal->tx_max_coalesced_frames_high =
			intrmod_cfg.tx_maxcnt_trigger;
		intr_coal->tx_max_coalesced_frames_low =
			intrmod_cfg.tx_mincnt_trigger;
	}
	return 0;
}

/* Enable/Disable auto interrupt Moderation */
static int oct_cfg_adaptive_intr(struct lio *lio,
				 struct oct_intrmod_cfg *intrmod_cfg,
				 struct ethtool_coalesce *intr_coal)
{
	int ret = 0;

	if (intrmod_cfg->rx_enable || intrmod_cfg->tx_enable) {
		intrmod_cfg->check_intrvl = intr_coal->rate_sample_interval;
		intrmod_cfg->maxpkt_ratethr = intr_coal->pkt_rate_high;
		intrmod_cfg->minpkt_ratethr = intr_coal->pkt_rate_low;
	}
	if (intrmod_cfg->rx_enable) {
		intrmod_cfg->rx_maxcnt_trigger =
			intr_coal->rx_max_coalesced_frames_high;
		intrmod_cfg->rx_maxtmr_trigger =
			intr_coal->rx_coalesce_usecs_high;
		intrmod_cfg->rx_mintmr_trigger =
			intr_coal->rx_coalesce_usecs_low;
		intrmod_cfg->rx_mincnt_trigger =
			intr_coal->rx_max_coalesced_frames_low;
	}
	if (intrmod_cfg->tx_enable) {
		intrmod_cfg->tx_maxcnt_trigger =
			intr_coal->tx_max_coalesced_frames_high;
		intrmod_cfg->tx_mincnt_trigger =
			intr_coal->tx_max_coalesced_frames_low;
	}

	ret = octnet_set_intrmod_cfg(lio, intrmod_cfg);

	return ret;
}

static int
oct_cfg_rx_intrcnt(struct lio *lio,
		   struct oct_intrmod_cfg *intrmod,
		   struct ethtool_coalesce *intr_coal)
{
	struct octeon_device *oct = lio->oct_dev;
	u32 rx_max_coalesced_frames;

	/* Config Cnt based interrupt values */
	switch (oct->chip_id) {
	case OCTEON_CN68XX:
	case OCTEON_CN66XX: {
		struct octeon_cn6xxx *cn6xxx =
			(struct octeon_cn6xxx *)oct->chip;

		if (!intr_coal->rx_max_coalesced_frames)
			rx_max_coalesced_frames = CN6XXX_OQ_INTR_PKT;
		else
			rx_max_coalesced_frames =
				intr_coal->rx_max_coalesced_frames;
		octeon_write_csr(oct, CN6XXX_SLI_OQ_INT_LEVEL_PKTS,
				 rx_max_coalesced_frames);
		CFG_SET_OQ_INTR_PKT(cn6xxx->conf, rx_max_coalesced_frames);
		break;
	}
	case OCTEON_CN23XX_PF_VID: {
		int q_no;

		if (!intr_coal->rx_max_coalesced_frames)
			rx_max_coalesced_frames = intrmod->rx_frames;
		else
			rx_max_coalesced_frames =
			    intr_coal->rx_max_coalesced_frames;
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			q_no += oct->sriov_info.pf_srn;
			octeon_write_csr64(
			    oct, CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no),
			    (octeon_read_csr64(
				 oct, CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no)) &
			     (0x3fffff00000000UL)) |
				(rx_max_coalesced_frames - 1));
			/*consider setting resend bit*/
		}
		intrmod->rx_frames = rx_max_coalesced_frames;
		oct->rx_max_coalesced_frames = rx_max_coalesced_frames;
		break;
	}
	case OCTEON_CN23XX_VF_VID: {
		int q_no;

		if (!intr_coal->rx_max_coalesced_frames)
			rx_max_coalesced_frames = intrmod->rx_frames;
		else
			rx_max_coalesced_frames =
			    intr_coal->rx_max_coalesced_frames;
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			octeon_write_csr64(
			    oct, CN23XX_VF_SLI_OQ_PKT_INT_LEVELS(q_no),
			    (octeon_read_csr64(
				 oct, CN23XX_VF_SLI_OQ_PKT_INT_LEVELS(q_no)) &
			     (0x3fffff00000000UL)) |
				rx_max_coalesced_frames);
			/*consider writing to resend bit here*/
		}
		intrmod->rx_frames = rx_max_coalesced_frames;
		oct->rx_max_coalesced_frames = rx_max_coalesced_frames;
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int oct_cfg_rx_intrtime(struct lio *lio,
			       struct oct_intrmod_cfg *intrmod,
			       struct ethtool_coalesce *intr_coal)
{
	struct octeon_device *oct = lio->oct_dev;
	u32 time_threshold, rx_coalesce_usecs;

	/* Config Time based interrupt values */
	switch (oct->chip_id) {
	case OCTEON_CN68XX:
	case OCTEON_CN66XX: {
		struct octeon_cn6xxx *cn6xxx =
			(struct octeon_cn6xxx *)oct->chip;
		if (!intr_coal->rx_coalesce_usecs)
			rx_coalesce_usecs = CN6XXX_OQ_INTR_TIME;
		else
			rx_coalesce_usecs = intr_coal->rx_coalesce_usecs;

		time_threshold = lio_cn6xxx_get_oq_ticks(oct,
							 rx_coalesce_usecs);
		octeon_write_csr(oct,
				 CN6XXX_SLI_OQ_INT_LEVEL_TIME,
				 time_threshold);

		CFG_SET_OQ_INTR_TIME(cn6xxx->conf, rx_coalesce_usecs);
		break;
	}
	case OCTEON_CN23XX_PF_VID: {
		u64 time_threshold;
		int q_no;

		if (!intr_coal->rx_coalesce_usecs)
			rx_coalesce_usecs = intrmod->rx_usecs;
		else
			rx_coalesce_usecs = intr_coal->rx_coalesce_usecs;
		time_threshold =
		    cn23xx_pf_get_oq_ticks(oct, (u32)rx_coalesce_usecs);
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			q_no += oct->sriov_info.pf_srn;
			octeon_write_csr64(oct,
					   CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no),
					   (intrmod->rx_frames |
					    ((u64)time_threshold << 32)));
			/*consider writing to resend bit here*/
		}
		intrmod->rx_usecs = rx_coalesce_usecs;
		oct->rx_coalesce_usecs = rx_coalesce_usecs;
		break;
	}
	case OCTEON_CN23XX_VF_VID: {
		u64 time_threshold;
		int q_no;

		if (!intr_coal->rx_coalesce_usecs)
			rx_coalesce_usecs = intrmod->rx_usecs;
		else
			rx_coalesce_usecs = intr_coal->rx_coalesce_usecs;

		time_threshold =
		    cn23xx_vf_get_oq_ticks(oct, (u32)rx_coalesce_usecs);
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			octeon_write_csr64(
				oct, CN23XX_VF_SLI_OQ_PKT_INT_LEVELS(q_no),
				(intrmod->rx_frames |
				 ((u64)time_threshold << 32)));
			/*consider setting resend bit*/
		}
		intrmod->rx_usecs = rx_coalesce_usecs;
		oct->rx_coalesce_usecs = rx_coalesce_usecs;
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static int
oct_cfg_tx_intrcnt(struct lio *lio,
		   struct oct_intrmod_cfg *intrmod,
		   struct ethtool_coalesce *intr_coal)
{
	struct octeon_device *oct = lio->oct_dev;
	u32 iq_intr_pkt;
	void __iomem *inst_cnt_reg;
	u64 val;

	/* Config Cnt based interrupt values */
	switch (oct->chip_id) {
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		break;
	case OCTEON_CN23XX_VF_VID:
	case OCTEON_CN23XX_PF_VID: {
		int q_no;

		if (!intr_coal->tx_max_coalesced_frames)
			iq_intr_pkt = CN23XX_DEF_IQ_INTR_THRESHOLD &
				      CN23XX_PKT_IN_DONE_WMARK_MASK;
		else
			iq_intr_pkt = intr_coal->tx_max_coalesced_frames &
				      CN23XX_PKT_IN_DONE_WMARK_MASK;
		for (q_no = 0; q_no < oct->num_iqs; q_no++) {
			inst_cnt_reg = (oct->instr_queue[q_no])->inst_cnt_reg;
			val = readq(inst_cnt_reg);
			/*clear wmark and count.dont want to write count back*/
			val = (val & 0xFFFF000000000000ULL) |
			      ((u64)(iq_intr_pkt - 1)
			       << CN23XX_PKT_IN_DONE_WMARK_BIT_POS);
			writeq(val, inst_cnt_reg);
			/*consider setting resend bit*/
		}
		intrmod->tx_frames = iq_intr_pkt;
		oct->tx_max_coalesced_frames = iq_intr_pkt;
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int lio_set_intr_coalesce(struct net_device *netdev,
				 struct ethtool_coalesce *intr_coal)
{
	struct lio *lio = GET_LIO(netdev);
	int ret;
	struct octeon_device *oct = lio->oct_dev;
	struct oct_intrmod_cfg intrmod = {0};
	u32 j, q_no;
	int db_max, db_min;

	switch (oct->chip_id) {
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		db_min = CN6XXX_DB_MIN;
		db_max = CN6XXX_DB_MAX;
		if ((intr_coal->tx_max_coalesced_frames >= db_min) &&
		    (intr_coal->tx_max_coalesced_frames <= db_max)) {
			for (j = 0; j < lio->linfo.num_txpciq; j++) {
				q_no = lio->linfo.txpciq[j].s.q_no;
				oct->instr_queue[q_no]->fill_threshold =
					intr_coal->tx_max_coalesced_frames;
			}
		} else {
			dev_err(&oct->pci_dev->dev,
				"LIQUIDIO: Invalid tx-frames:%d. Range is min:%d max:%d\n",
				intr_coal->tx_max_coalesced_frames,
				db_min, db_max);
			return -EINVAL;
		}
		break;
	case OCTEON_CN23XX_PF_VID:
	case OCTEON_CN23XX_VF_VID:
		break;
	default:
		return -EINVAL;
	}

	intrmod.rx_enable = intr_coal->use_adaptive_rx_coalesce ? 1 : 0;
	intrmod.tx_enable = intr_coal->use_adaptive_tx_coalesce ? 1 : 0;
	intrmod.rx_frames = CFG_GET_OQ_INTR_PKT(octeon_get_conf(oct));
	intrmod.rx_usecs = CFG_GET_OQ_INTR_TIME(octeon_get_conf(oct));
	intrmod.tx_frames = CFG_GET_IQ_INTR_PKT(octeon_get_conf(oct));

	ret = oct_cfg_adaptive_intr(lio, &intrmod, intr_coal);

	if (!intr_coal->use_adaptive_rx_coalesce) {
		ret = oct_cfg_rx_intrtime(lio, &intrmod, intr_coal);
		if (ret)
			goto ret_intrmod;

		ret = oct_cfg_rx_intrcnt(lio, &intrmod, intr_coal);
		if (ret)
			goto ret_intrmod;
	} else {
		oct->rx_coalesce_usecs =
			CFG_GET_OQ_INTR_TIME(octeon_get_conf(oct));
		oct->rx_max_coalesced_frames =
			CFG_GET_OQ_INTR_PKT(octeon_get_conf(oct));
	}

	if (!intr_coal->use_adaptive_tx_coalesce) {
		ret = oct_cfg_tx_intrcnt(lio, &intrmod, intr_coal);
		if (ret)
			goto ret_intrmod;
	} else {
		oct->tx_max_coalesced_frames =
			CFG_GET_IQ_INTR_PKT(octeon_get_conf(oct));
	}

	return 0;
ret_intrmod:
	return ret;
}

static int lio_get_ts_info(struct net_device *netdev,
			   struct ethtool_ts_info *info)
{
	struct lio *lio = GET_LIO(netdev);

	info->so_timestamping =
#ifdef PTP_HARDWARE_TIMESTAMPING
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE |
		SOF_TIMESTAMPING_TX_SOFTWARE |
#endif
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE;

	if (lio->ptp_clock)
		info->phc_index = ptp_clock_index(lio->ptp_clock);
	else
		info->phc_index = -1;

#ifdef PTP_HARDWARE_TIMESTAMPING
	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);

	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT);
#endif

	return 0;
}

/* Return register dump len. */
static int lio_get_regs_len(struct net_device *dev)
{
	struct lio *lio = GET_LIO(dev);
	struct octeon_device *oct = lio->oct_dev;

	switch (oct->chip_id) {
	case OCTEON_CN23XX_PF_VID:
		return OCT_ETHTOOL_REGDUMP_LEN_23XX;
	case OCTEON_CN23XX_VF_VID:
		return OCT_ETHTOOL_REGDUMP_LEN_23XX_VF;
	default:
		return OCT_ETHTOOL_REGDUMP_LEN;
	}
}

static int cn23xx_read_csr_reg(char *s, struct octeon_device *oct)
{
	u32 reg;
	u8 pf_num = oct->pf_num;
	int len = 0;
	int i;

	/* PCI  Window Registers */

	len += sprintf(s + len, "\n\t Octeon CSR Registers\n\n");

	/*0x29030 or 0x29040*/
	reg = CN23XX_SLI_PKT_MAC_RINFO64(oct->pcie_port, oct->pf_num);
	len += sprintf(s + len,
		       "\n[%08x] (SLI_PKT_MAC%d_PF%d_RINFO): %016llx\n",
		       reg, oct->pcie_port, oct->pf_num,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x27080 or 0x27090*/
	reg = CN23XX_SLI_MAC_PF_INT_ENB64(oct->pcie_port, oct->pf_num);
	len +=
	    sprintf(s + len, "\n[%08x] (SLI_MAC%d_PF%d_INT_ENB): %016llx\n",
		    reg, oct->pcie_port, oct->pf_num,
		    (u64)octeon_read_csr64(oct, reg));

	/*0x27000 or 0x27010*/
	reg = CN23XX_SLI_MAC_PF_INT_SUM64(oct->pcie_port, oct->pf_num);
	len +=
	    sprintf(s + len, "\n[%08x] (SLI_MAC%d_PF%d_INT_SUM): %016llx\n",
		    reg, oct->pcie_port, oct->pf_num,
		    (u64)octeon_read_csr64(oct, reg));

	/*0x29120*/
	reg = 0x29120;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_MEM_CTL): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x27300*/
	reg = 0x27300 + oct->pcie_port * CN23XX_MAC_INT_OFFSET +
	      (oct->pf_num) * CN23XX_PF_INT_OFFSET;
	len += sprintf(
	    s + len, "\n[%08x] (SLI_MAC%d_PF%d_PKT_VF_INT): %016llx\n", reg,
	    oct->pcie_port, oct->pf_num, (u64)octeon_read_csr64(oct, reg));

	/*0x27200*/
	reg = 0x27200 + oct->pcie_port * CN23XX_MAC_INT_OFFSET +
	      (oct->pf_num) * CN23XX_PF_INT_OFFSET;
	len += sprintf(s + len,
		       "\n[%08x] (SLI_MAC%d_PF%d_PP_VF_INT): %016llx\n",
		       reg, oct->pcie_port, oct->pf_num,
		       (u64)octeon_read_csr64(oct, reg));

	/*29130*/
	reg = CN23XX_SLI_PKT_CNT_INT;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_CNT_INT): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x29140*/
	reg = CN23XX_SLI_PKT_TIME_INT;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_TIME_INT): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x29160*/
	reg = 0x29160;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_INT): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x29180*/
	reg = CN23XX_SLI_OQ_WMARK;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_OUTPUT_WMARK): %016llx\n",
		       reg, (u64)octeon_read_csr64(oct, reg));

	/*0x291E0*/
	reg = CN23XX_SLI_PKT_IOQ_RING_RST;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_RING_RST): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x29210*/
	reg = CN23XX_SLI_GBL_CONTROL;
	len += sprintf(s + len,
		       "\n[%08x] (SLI_PKT_GBL_CONTROL): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x29220*/
	reg = 0x29220;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_BIST_STATUS): %016llx\n",
		       reg, (u64)octeon_read_csr64(oct, reg));

	/*PF only*/
	if (pf_num == 0) {
		/*0x29260*/
		reg = CN23XX_SLI_OUT_BP_EN_W1S;
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_OUT_BP_EN_W1S):  %016llx\n",
			       reg, (u64)octeon_read_csr64(oct, reg));
	} else if (pf_num == 1) {
		/*0x29270*/
		reg = CN23XX_SLI_OUT_BP_EN2_W1S;
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_OUT_BP_EN2_W1S): %016llx\n",
			       reg, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_BUFF_INFO_SIZE(i);
		len +=
		    sprintf(s + len, "\n[%08x] (SLI_PKT%d_OUT_SIZE): %016llx\n",
			    reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x10040*/
	for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
		reg = CN23XX_SLI_IQ_INSTR_COUNT64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_IN_DONE%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x10080*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_PKTS_CREDIT(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_SLIST_BAOFF_DBELL): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x10090*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_SIZE(i);
		len += sprintf(
		    s + len, "\n[%08x] (SLI_PKT%d_SLIST_FIFO_RSIZE): %016llx\n",
		    reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x10050*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_PKT_CONTROL(i);
		len += sprintf(
			s + len,
			"\n[%08x] (SLI_PKT%d__OUTPUT_CONTROL): %016llx\n",
			reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x10070*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_BASE_ADDR64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_SLIST_BADDR): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x100a0*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_PKT_INT_LEVELS(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INT_LEVELS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x100b0*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_PKTS_SENT(i);
		len += sprintf(s + len, "\n[%08x] (SLI_PKT%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x100c0*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = 0x100c0 + i * CN23XX_OQ_OFFSET;
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_ERROR_INFO): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));

		/*0x10000*/
		for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
			reg = CN23XX_SLI_IQ_PKT_CONTROL64(i);
			len += sprintf(
				s + len,
				"\n[%08x] (SLI_PKT%d_INPUT_CONTROL): %016llx\n",
				reg, i, (u64)octeon_read_csr64(oct, reg));
		}

		/*0x10010*/
		for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
			reg = CN23XX_SLI_IQ_BASE_ADDR64(i);
			len += sprintf(
			    s + len,
			    "\n[%08x] (SLI_PKT%d_INSTR_BADDR): %016llx\n", reg,
			    i, (u64)octeon_read_csr64(oct, reg));
		}

		/*0x10020*/
		for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
			reg = CN23XX_SLI_IQ_DOORBELL(i);
			len += sprintf(
			    s + len,
			    "\n[%08x] (SLI_PKT%d_INSTR_BAOFF_DBELL): %016llx\n",
			    reg, i, (u64)octeon_read_csr64(oct, reg));
		}

		/*0x10030*/
		for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
			reg = CN23XX_SLI_IQ_SIZE(i);
			len += sprintf(
			    s + len,
			    "\n[%08x] (SLI_PKT%d_INSTR_FIFO_RSIZE): %016llx\n",
			    reg, i, (u64)octeon_read_csr64(oct, reg));
		}

		/*0x10040*/
		for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++)
			reg = CN23XX_SLI_IQ_INSTR_COUNT64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_IN_DONE%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	return len;
}

static int cn23xx_vf_read_csr_reg(char *s, struct octeon_device *oct)
{
	int len = 0;
	u32 reg;
	int i;

	/* PCI  Window Registers */

	len += sprintf(s + len, "\n\t Octeon CSR Registers\n\n");

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_BUFF_INFO_SIZE(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_OUT_SIZE): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_INSTR_COUNT64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_IN_DONE%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_PKTS_CREDIT(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_SLIST_BAOFF_DBELL): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_SIZE(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_SLIST_FIFO_RSIZE): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_PKT_CONTROL(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d__OUTPUT_CONTROL): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_BASE_ADDR64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_SLIST_BADDR): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_PKT_INT_LEVELS(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INT_LEVELS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_PKTS_SENT(i);
		len += sprintf(s + len, "\n[%08x] (SLI_PKT%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = 0x100c0 + i * CN23XX_VF_OQ_OFFSET;
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_ERROR_INFO): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = 0x100d0 + i * CN23XX_VF_IQ_OFFSET;
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_VF_INT_SUM): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_PKT_CONTROL64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INPUT_CONTROL): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_BASE_ADDR64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INSTR_BADDR): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_DOORBELL(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INSTR_BAOFF_DBELL): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_SIZE(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INSTR_FIFO_RSIZE): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_INSTR_COUNT64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_IN_DONE%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	return len;
}

static int cn6xxx_read_csr_reg(char *s, struct octeon_device *oct)
{
	u32 reg;
	int i, len = 0;

	/* PCI  Window Registers */

	len += sprintf(s + len, "\n\t Octeon CSR Registers\n\n");
	reg = CN6XXX_WIN_WR_ADDR_LO;
	len += sprintf(s + len, "\n[%02x] (WIN_WR_ADDR_LO): %08x\n",
		       CN6XXX_WIN_WR_ADDR_LO, octeon_read_csr(oct, reg));
	reg = CN6XXX_WIN_WR_ADDR_HI;
	len += sprintf(s + len, "[%02x] (WIN_WR_ADDR_HI): %08x\n",
		       CN6XXX_WIN_WR_ADDR_HI, octeon_read_csr(oct, reg));
	reg = CN6XXX_WIN_RD_ADDR_LO;
	len += sprintf(s + len, "[%02x] (WIN_RD_ADDR_LO): %08x\n",
		       CN6XXX_WIN_RD_ADDR_LO, octeon_read_csr(oct, reg));
	reg = CN6XXX_WIN_RD_ADDR_HI;
	len += sprintf(s + len, "[%02x] (WIN_RD_ADDR_HI): %08x\n",
		       CN6XXX_WIN_RD_ADDR_HI, octeon_read_csr(oct, reg));
	reg = CN6XXX_WIN_WR_DATA_LO;
	len += sprintf(s + len, "[%02x] (WIN_WR_DATA_LO): %08x\n",
		       CN6XXX_WIN_WR_DATA_LO, octeon_read_csr(oct, reg));
	reg = CN6XXX_WIN_WR_DATA_HI;
	len += sprintf(s + len, "[%02x] (WIN_WR_DATA_HI): %08x\n",
		       CN6XXX_WIN_WR_DATA_HI, octeon_read_csr(oct, reg));
	len += sprintf(s + len, "[%02x] (WIN_WR_MASK_REG): %08x\n",
		       CN6XXX_WIN_WR_MASK_REG,
		       octeon_read_csr(oct, CN6XXX_WIN_WR_MASK_REG));

	/* PCI  Interrupt Register */
	len += sprintf(s + len, "\n[%x] (INT_ENABLE PORT 0): %08x\n",
		       CN6XXX_SLI_INT_ENB64_PORT0, octeon_read_csr(oct,
						CN6XXX_SLI_INT_ENB64_PORT0));
	len += sprintf(s + len, "\n[%x] (INT_ENABLE PORT 1): %08x\n",
		       CN6XXX_SLI_INT_ENB64_PORT1,
		       octeon_read_csr(oct, CN6XXX_SLI_INT_ENB64_PORT1));
	len += sprintf(s + len, "[%x] (INT_SUM): %08x\n", CN6XXX_SLI_INT_SUM64,
		       octeon_read_csr(oct, CN6XXX_SLI_INT_SUM64));

	/* PCI  Output queue registers */
	for (i = 0; i < oct->num_oqs; i++) {
		reg = CN6XXX_SLI_OQ_PKTS_SENT(i);
		len += sprintf(s + len, "\n[%x] (PKTS_SENT_%d): %08x\n",
			       reg, i, octeon_read_csr(oct, reg));
		reg = CN6XXX_SLI_OQ_PKTS_CREDIT(i);
		len += sprintf(s + len, "[%x] (PKT_CREDITS_%d): %08x\n",
			       reg, i, octeon_read_csr(oct, reg));
	}
	reg = CN6XXX_SLI_OQ_INT_LEVEL_PKTS;
	len += sprintf(s + len, "\n[%x] (PKTS_SENT_INT_LEVEL): %08x\n",
		       reg, octeon_read_csr(oct, reg));
	reg = CN6XXX_SLI_OQ_INT_LEVEL_TIME;
	len += sprintf(s + len, "[%x] (PKTS_SENT_TIME): %08x\n",
		       reg, octeon_read_csr(oct, reg));

	/* PCI  Input queue registers */
	for (i = 0; i <= 3; i++) {
		u32 reg;

		reg = CN6XXX_SLI_IQ_DOORBELL(i);
		len += sprintf(s + len, "\n[%x] (INSTR_DOORBELL_%d): %08x\n",
			       reg, i, octeon_read_csr(oct, reg));
		reg = CN6XXX_SLI_IQ_INSTR_COUNT(i);
		len += sprintf(s + len, "[%x] (INSTR_COUNT_%d): %08x\n",
			       reg, i, octeon_read_csr(oct, reg));
	}

	/* PCI  DMA registers */

	len += sprintf(s + len, "\n[%x] (DMA_CNT_0): %08x\n",
		       CN6XXX_DMA_CNT(0),
		       octeon_read_csr(oct, CN6XXX_DMA_CNT(0)));
	reg = CN6XXX_DMA_PKT_INT_LEVEL(0);
	len += sprintf(s + len, "[%x] (DMA_INT_LEV_0): %08x\n",
		       CN6XXX_DMA_PKT_INT_LEVEL(0), octeon_read_csr(oct, reg));
	reg = CN6XXX_DMA_TIME_INT_LEVEL(0);
	len += sprintf(s + len, "[%x] (DMA_TIME_0): %08x\n",
		       CN6XXX_DMA_TIME_INT_LEVEL(0),
		       octeon_read_csr(oct, reg));

	len += sprintf(s + len, "\n[%x] (DMA_CNT_1): %08x\n",
		       CN6XXX_DMA_CNT(1),
		       octeon_read_csr(oct, CN6XXX_DMA_CNT(1)));
	reg = CN6XXX_DMA_PKT_INT_LEVEL(1);
	len += sprintf(s + len, "[%x] (DMA_INT_LEV_1): %08x\n",
		       CN6XXX_DMA_PKT_INT_LEVEL(1),
		       octeon_read_csr(oct, reg));
	reg = CN6XXX_DMA_PKT_INT_LEVEL(1);
	len += sprintf(s + len, "[%x] (DMA_TIME_1): %08x\n",
		       CN6XXX_DMA_TIME_INT_LEVEL(1),
		       octeon_read_csr(oct, reg));

	/* PCI  Index registers */

	len += sprintf(s + len, "\n");

	for (i = 0; i < 16; i++) {
		reg = lio_pci_readq(oct, CN6XXX_BAR1_REG(i, oct->pcie_port));
		len += sprintf(s + len, "[%llx] (BAR1_INDEX_%02d): %08x\n",
			       CN6XXX_BAR1_REG(i, oct->pcie_port), i, reg);
	}

	return len;
}

static int cn6xxx_read_config_reg(char *s, struct octeon_device *oct)
{
	u32 val;
	int i, len = 0;

	/* PCI CONFIG Registers */

	len += sprintf(s + len,
		       "\n\t Octeon Config space Registers\n\n");

	for (i = 0; i <= 13; i++) {
		pci_read_config_dword(oct->pci_dev, (i * 4), &val);
		len += sprintf(s + len, "[0x%x] (Config[%d]): 0x%08x\n",
			       (i * 4), i, val);
	}

	for (i = 30; i <= 34; i++) {
		pci_read_config_dword(oct->pci_dev, (i * 4), &val);
		len += sprintf(s + len, "[0x%x] (Config[%d]): 0x%08x\n",
			       (i * 4), i, val);
	}

	return len;
}

/*  Return register dump user app.  */
static void lio_get_regs(struct net_device *dev,
			 struct ethtool_regs *regs, void *regbuf)
{
	struct lio *lio = GET_LIO(dev);
	int len = 0;
	struct octeon_device *oct = lio->oct_dev;

	regs->version = OCT_ETHTOOL_REGSVER;

	switch (oct->chip_id) {
	case OCTEON_CN23XX_PF_VID:
		memset(regbuf, 0, OCT_ETHTOOL_REGDUMP_LEN_23XX);
		len += cn23xx_read_csr_reg(regbuf + len, oct);
		break;
	case OCTEON_CN23XX_VF_VID:
		memset(regbuf, 0, OCT_ETHTOOL_REGDUMP_LEN_23XX_VF);
		len += cn23xx_vf_read_csr_reg(regbuf + len, oct);
		break;
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		memset(regbuf, 0, OCT_ETHTOOL_REGDUMP_LEN);
		len += cn6xxx_read_csr_reg(regbuf + len, oct);
		len += cn6xxx_read_config_reg(regbuf + len, oct);
		break;
	default:
		dev_err(&oct->pci_dev->dev, "%s Unknown chipid: %d\n",
			__func__, oct->chip_id);
	}
}

static u32 lio_get_priv_flags(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	return lio->oct_dev->priv_flags;
}

static int lio_set_priv_flags(struct net_device *netdev, u32 flags)
{
	struct lio *lio = GET_LIO(netdev);
	bool intr_by_tx_bytes = !!(flags & (0x1 << OCT_PRIV_FLAG_TX_BYTES));

	lio_set_priv_flag(lio->oct_dev, OCT_PRIV_FLAG_TX_BYTES,
			  intr_by_tx_bytes);
	return 0;
}

static const struct ethtool_ops lio_ethtool_ops = {
	.get_link_ksettings	= lio_get_link_ksettings,
	.get_link		= ethtool_op_get_link,
	.get_drvinfo		= lio_get_drvinfo,
	.get_ringparam		= lio_ethtool_get_ringparam,
	.get_channels		= lio_ethtool_get_channels,
	.set_phys_id		= lio_set_phys_id,
	.get_eeprom_len		= lio_get_eeprom_len,
	.get_eeprom		= lio_get_eeprom,
	.get_strings		= lio_get_strings,
	.get_ethtool_stats	= lio_get_ethtool_stats,
	.get_pauseparam		= lio_get_pauseparam,
	.set_pauseparam		= lio_set_pauseparam,
	.get_regs_len		= lio_get_regs_len,
	.get_regs		= lio_get_regs,
	.get_msglevel		= lio_get_msglevel,
	.set_msglevel		= lio_set_msglevel,
	.get_sset_count		= lio_get_sset_count,
	.get_coalesce		= lio_get_intr_coalesce,
	.set_coalesce		= lio_set_intr_coalesce,
	.get_priv_flags		= lio_get_priv_flags,
	.set_priv_flags		= lio_set_priv_flags,
	.get_ts_info		= lio_get_ts_info,
};

static const struct ethtool_ops lio_vf_ethtool_ops = {
	.get_link_ksettings	= lio_get_link_ksettings,
	.get_link		= ethtool_op_get_link,
	.get_drvinfo		= lio_get_vf_drvinfo,
	.get_ringparam		= lio_ethtool_get_ringparam,
	.get_channels		= lio_ethtool_get_channels,
	.get_strings		= lio_vf_get_strings,
	.get_ethtool_stats	= lio_vf_get_ethtool_stats,
	.get_regs_len		= lio_get_regs_len,
	.get_regs		= lio_get_regs,
	.get_msglevel		= lio_get_msglevel,
	.set_msglevel		= lio_vf_set_msglevel,
	.get_sset_count		= lio_vf_get_sset_count,
	.get_coalesce		= lio_get_intr_coalesce,
	.set_coalesce		= lio_set_intr_coalesce,
	.get_priv_flags		= lio_get_priv_flags,
	.set_priv_flags		= lio_set_priv_flags,
	.get_ts_info		= lio_get_ts_info,
};

void liquidio_set_ethtool_ops(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	if (OCTEON_CN23XX_VF(oct))
		netdev->ethtool_ops = &lio_vf_ethtool_ops;
	else
		netdev->ethtool_ops = &lio_ethtool_ops;
}
