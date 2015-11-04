/**********************************************************************
* Author: Cavium, Inc.
*
* Contact: support@cavium.com
*          Please include "LiquidIO" in the subject.
*
* Copyright (c) 2003-2015 Cavium, Inc.
*
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, Version 2, as
* published by the Free Software Foundation.
*
* This file is distributed in the hope that it will be useful, but
* AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
* NONINFRINGEMENT.  See the GNU General Public License for more
* details.
*
* This file may also be available under a different license from Cavium.
* Contact Cavium, Inc. for more information
**********************************************************************/
#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/ethtool.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include "octeon_config.h"
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
#include "cn68xx_regs.h"
#include "cn68xx_device.h"
#include "liquidio_image.h"

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
};

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))
#define OCT_ETHTOOL_REGDUMP_LEN  4096
#define OCT_ETHTOOL_REGSVER  1

static const char oct_iq_stats_strings[][ETH_GSTRING_LEN] = {
	"Instr posted",
	"Instr processed",
	"Instr dropped",
	"Bytes Sent",
	"Sgentry_sent",
	"Inst cntreg",
	"Tx done",
	"Tx Iq busy",
	"Tx dropped",
	"Tx bytes",
};

static const char oct_droq_stats_strings[][ETH_GSTRING_LEN] = {
	"OQ Pkts Received",
	"OQ Bytes Received",
	"Dropped no dispatch",
	"Dropped nomem",
	"Dropped toomany",
	"Stack RX cnt",
	"Stack RX Bytes",
	"RX dropped",
};

#define OCTNIC_NCMD_AUTONEG_ON  0x1
#define OCTNIC_NCMD_PHY_ON      0x2

static int lio_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct oct_link_info *linfo;

	linfo = &lio->linfo;

	if (linfo->link.s.interface == INTERFACE_MODE_XAUI ||
	    linfo->link.s.interface == INTERFACE_MODE_RXAUI) {
		ecmd->port = PORT_FIBRE;
		ecmd->supported =
			(SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE |
			 SUPPORTED_Pause);
		ecmd->advertising =
			(ADVERTISED_10000baseT_Full | ADVERTISED_Pause);
		ecmd->transceiver = XCVR_EXTERNAL;
		ecmd->autoneg = AUTONEG_DISABLE;

	} else {
		dev_err(&oct->pci_dev->dev, "Unknown link interface reported\n");
	}

	if (linfo->link.s.status) {
		ethtool_cmd_speed_set(ecmd, linfo->link.s.speed);
		ecmd->duplex = linfo->link.s.duplex;
	} else {
		ethtool_cmd_speed_set(ecmd, SPEED_UNKNOWN);
		ecmd->duplex = DUPLEX_UNKNOWN;
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
lio_ethtool_get_channels(struct net_device *dev,
			 struct ethtool_channels *channel)
{
	struct lio *lio = GET_LIO(dev);
	struct octeon_device *oct = lio->oct_dev;
	u32 max_rx = 0, max_tx = 0, tx_count = 0, rx_count = 0;

	if (OCTEON_CN6XXX(oct)) {
		struct octeon_config *conf6x = CHIP_FIELD(oct, cn6xxx, conf);

		max_rx = CFG_GET_OQ_MAX_Q(conf6x);
		max_tx = CFG_GET_IQ_MAX_Q(conf6x);
		rx_count = CFG_GET_NUM_RXQS_NIC_IF(conf6x, lio->ifidx);
		tx_count = CFG_GET_NUM_TXQS_NIC_IF(conf6x, lio->ifidx);
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
	int len;

	if (eeprom->offset != 0)
		return -EINVAL;

	eeprom->magic = oct_dev->pci_dev->vendor;
	board_info = (struct octeon_board_info *)(&oct_dev->boardinfo);
	len =
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
	struct octnic_ctrl_params nparams;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_GPIO_ACCESS;
	nctrl.ncmd.s.param1 = lio->linfo.ifidx;
	nctrl.ncmd.s.param2 = addr;
	nctrl.ncmd.s.param3 = val;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	nparams.resp_order = OCTEON_RESP_ORDERED;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl, nparams);
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
	struct oct_mdio_cmd_resp *mdio_cmd_rsp;
	struct oct_mdio_cmd_context *mdio_cmd_ctx;
	struct octeon_soft_command *sc = (struct octeon_soft_command *)buf;

	mdio_cmd_rsp = (struct oct_mdio_cmd_resp *)sc->virtrptr;
	mdio_cmd_ctx = (struct oct_mdio_cmd_context *)sc->ctxptr;

	oct = lio_get_device(mdio_cmd_ctx->octeon_id);
	if (status) {
		dev_err(&oct->pci_dev->dev, "MIDO instruction failed. Status: %llx\n",
			CVM_CAST64(status));
		ACCESS_ONCE(mdio_cmd_ctx->cond) = -1;
	} else {
		ACCESS_ONCE(mdio_cmd_ctx->cond) = 1;
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

	ACCESS_ONCE(mdio_cmd_ctx->cond) = 0;
	mdio_cmd_ctx->octeon_id = lio_get_device_id(oct_dev);
	mdio_cmd->op = op;
	mdio_cmd->mdio_addr = loc;
	if (op)
		mdio_cmd->value1 = *value;
	mdio_cmd->value2 = lio->linfo.ifidx;
	octeon_swap_8B_data((u64 *)mdio_cmd, sizeof(struct oct_mdio_cmd) / 8);

	octeon_prepare_soft_command(oct_dev, sc, OPCODE_NIC, OPCODE_NIC_MDIO45,
				    0, 0, 0);

	sc->wait_time = 1000;
	sc->callback = octnet_mdio_resp_callback;
	sc->callback_arg = sc;

	init_waitqueue_head(&mdio_cmd_ctx->wc);

	retval = octeon_send_soft_command(oct_dev, sc);

	if (retval) {
		dev_err(&oct_dev->pci_dev->dev,
			"octnet_mdio45_access instruction failed status: %x\n",
			retval);
		retval =  -EBUSY;
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

			if (ACCESS_ONCE(mdio_cmd_ctx->cond) == 1) {
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
			ret =
				octnet_mdio45_access(lio, 1,
						     LIO68XX_LED_BEACON_ADDR,
						     &value);
			if (ret)
				return ret;

			value = LIO68XX_LED_CTRL_CFGON;
			ret =
				octnet_mdio45_access(lio, 1,
						     LIO68XX_LED_CTRL_ADDR,
						     &value);
			if (ret)
				return ret;
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
		struct octeon_config *conf6x = CHIP_FIELD(oct, cn6xxx, conf);

		tx_max_pending = CN6XXX_MAX_IQ_DESCRIPTORS;
		rx_max_pending = CN6XXX_MAX_OQ_DESCRIPTORS;
		rx_pending = CFG_GET_NUM_RX_DESCS_NIC_IF(conf6x, lio->ifidx);
		tx_pending = CFG_GET_NUM_TX_DESCS_NIC_IF(conf6x, lio->ifidx);
	}

	if (lio->mtu > OCTNET_DEFAULT_FRM_SIZE) {
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
					     OCTNET_CMD_VERBOSE_ENABLE);
		else
			liquidio_set_feature(netdev,
					     OCTNET_CMD_VERBOSE_DISABLE);
	}

	lio->msg_enable = msglvl;
}

static void
lio_get_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *pause)
{
	/* Notes: Not supporting any auto negotiation in these
	 * drivers. Just report pause frame support.
	 */
	pause->tx_pause = 1;
	pause->rx_pause = 1;    /* TODO: Need to support RX pause frame!!. */
}

static void
lio_get_ethtool_stats(struct net_device *netdev,
		      struct ethtool_stats *stats, u64 *data)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	int i = 0, j;

	for (j = 0; j < MAX_OCTEON_INSTR_QUEUES; j++) {
		if (!(oct_dev->io_qmask.iq & (1UL << j)))
			continue;
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.instr_posted);
		data[i++] =
			CVM_CAST64(
				oct_dev->instr_queue[j]->stats.instr_processed);
		data[i++] =
			CVM_CAST64(
				oct_dev->instr_queue[j]->stats.instr_dropped);
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.bytes_sent);
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.sgentry_sent);
		data[i++] =
			readl(oct_dev->instr_queue[j]->inst_cnt_reg);
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_done);
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_iq_busy);
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_dropped);
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_tot_bytes);
	}

	/* for (j = 0; j < oct_dev->num_oqs; j++){ */
	for (j = 0; j < MAX_OCTEON_OUTPUT_QUEUES; j++) {
		if (!(oct_dev->io_qmask.oq & (1UL << j)))
			continue;
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.pkts_received);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.bytes_received);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.dropped_nodispatch);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.dropped_nomem);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.dropped_toomany);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_pkts_received);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_bytes_received);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_dropped);
	}
}

static void lio_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	int num_iq_stats, num_oq_stats, i, j;

	num_iq_stats = ARRAY_SIZE(oct_iq_stats_strings);
	for (i = 0; i < MAX_OCTEON_INSTR_QUEUES; i++) {
		if (!(oct_dev->io_qmask.iq & (1UL << i)))
			continue;
		for (j = 0; j < num_iq_stats; j++) {
			sprintf(data, "IQ%d %s", i, oct_iq_stats_strings[j]);
			data += ETH_GSTRING_LEN;
		}
	}

	num_oq_stats = ARRAY_SIZE(oct_droq_stats_strings);
	/* for (i = 0; i < oct_dev->num_oqs; i++) { */
	for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES; i++) {
		if (!(oct_dev->io_qmask.oq & (1UL << i)))
			continue;
		for (j = 0; j < num_oq_stats; j++) {
			sprintf(data, "OQ%d %s", i, oct_droq_stats_strings[j]);
			data += ETH_GSTRING_LEN;
		}
	}
}

static int lio_get_sset_count(struct net_device *netdev, int sset)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;

	return (ARRAY_SIZE(oct_iq_stats_strings) * oct_dev->num_iqs) +
	       (ARRAY_SIZE(oct_droq_stats_strings) * oct_dev->num_oqs);
}

static int lio_get_intr_coalesce(struct net_device *netdev,
				 struct ethtool_coalesce *intr_coal)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_cn6xxx *cn6xxx = (struct octeon_cn6xxx *)oct->chip;
	struct octeon_instr_queue *iq;
	struct oct_intrmod_cfg *intrmod_cfg;

	intrmod_cfg = &oct->intrmod;

	switch (oct->chip_id) {
	/* case OCTEON_CN73XX: Todo */
	/*      break; */
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		if (!intrmod_cfg->intrmod_enable) {
			intr_coal->rx_coalesce_usecs =
				CFG_GET_OQ_INTR_TIME(cn6xxx->conf);
			intr_coal->rx_max_coalesced_frames =
				CFG_GET_OQ_INTR_PKT(cn6xxx->conf);
		} else {
			intr_coal->use_adaptive_rx_coalesce =
				intrmod_cfg->intrmod_enable;
			intr_coal->rate_sample_interval =
				intrmod_cfg->intrmod_check_intrvl;
			intr_coal->pkt_rate_high =
				intrmod_cfg->intrmod_maxpkt_ratethr;
			intr_coal->pkt_rate_low =
				intrmod_cfg->intrmod_minpkt_ratethr;
			intr_coal->rx_max_coalesced_frames_high =
				intrmod_cfg->intrmod_maxcnt_trigger;
			intr_coal->rx_coalesce_usecs_high =
				intrmod_cfg->intrmod_maxtmr_trigger;
			intr_coal->rx_coalesce_usecs_low =
				intrmod_cfg->intrmod_mintmr_trigger;
			intr_coal->rx_max_coalesced_frames_low =
				intrmod_cfg->intrmod_mincnt_trigger;
		}

		iq = oct->instr_queue[lio->linfo.txpciq[0]];
		intr_coal->tx_max_coalesced_frames = iq->fill_threshold;
		break;

	default:
		netif_info(lio, drv, lio->netdev, "Unknown Chip !!\n");
		return -EINVAL;
	}

	return 0;
}

/* Callback function for intrmod */
static void octnet_intrmod_callback(struct octeon_device *oct_dev,
				    u32 status,
				    void *ptr)
{
	struct oct_intrmod_cmd *cmd = ptr;
	struct octeon_soft_command *sc = cmd->sc;

	oct_dev = cmd->oct_dev;

	if (status)
		dev_err(&oct_dev->pci_dev->dev, "intrmod config failed. Status: %llx\n",
			CVM_CAST64(status));
	else
		dev_info(&oct_dev->pci_dev->dev,
			 "Rx-Adaptive Interrupt moderation enabled:%llx\n",
			 oct_dev->intrmod.intrmod_enable);

	octeon_free_soft_command(oct_dev, sc);
}

/*  Configure interrupt moderation parameters */
static int octnet_set_intrmod_cfg(void *oct, struct oct_intrmod_cfg *intr_cfg)
{
	struct octeon_soft_command *sc;
	struct oct_intrmod_cmd *cmd;
	struct oct_intrmod_cfg *cfg;
	int retval;
	struct octeon_device *oct_dev = (struct octeon_device *)oct;

	/* Alloc soft command */
	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct_dev,
					  sizeof(struct oct_intrmod_cfg),
					  0,
					  sizeof(struct oct_intrmod_cmd));

	if (!sc)
		return -ENOMEM;

	cmd = (struct oct_intrmod_cmd *)sc->ctxptr;
	cfg = (struct oct_intrmod_cfg *)sc->virtdptr;

	memcpy(cfg, intr_cfg, sizeof(struct oct_intrmod_cfg));
	octeon_swap_8B_data((u64 *)cfg, (sizeof(struct oct_intrmod_cfg)) / 8);
	cmd->sc = sc;
	cmd->cfg = cfg;
	cmd->oct_dev = oct_dev;

	octeon_prepare_soft_command(oct_dev, sc, OPCODE_NIC,
				    OPCODE_NIC_INTRMOD_CFG, 0, 0, 0);

	sc->callback = octnet_intrmod_callback;
	sc->callback_arg = cmd;
	sc->wait_time = 1000;

	retval = octeon_send_soft_command(oct_dev, sc);
	if (retval) {
		octeon_free_soft_command(oct_dev, sc);
		return -EINVAL;
	}

	return 0;
}

/* Enable/Disable auto interrupt Moderation */
static int oct_cfg_adaptive_intr(struct lio *lio, struct ethtool_coalesce
				 *intr_coal, int adaptive)
{
	int ret = 0;
	struct octeon_device *oct = lio->oct_dev;
	struct oct_intrmod_cfg *intrmod_cfg;

	intrmod_cfg = &oct->intrmod;

	if (adaptive) {
		if (intr_coal->rate_sample_interval)
			intrmod_cfg->intrmod_check_intrvl =
				intr_coal->rate_sample_interval;
		else
			intrmod_cfg->intrmod_check_intrvl =
				LIO_INTRMOD_CHECK_INTERVAL;

		if (intr_coal->pkt_rate_high)
			intrmod_cfg->intrmod_maxpkt_ratethr =
				intr_coal->pkt_rate_high;
		else
			intrmod_cfg->intrmod_maxpkt_ratethr =
				LIO_INTRMOD_MAXPKT_RATETHR;

		if (intr_coal->pkt_rate_low)
			intrmod_cfg->intrmod_minpkt_ratethr =
				intr_coal->pkt_rate_low;
		else
			intrmod_cfg->intrmod_minpkt_ratethr =
				LIO_INTRMOD_MINPKT_RATETHR;

		if (intr_coal->rx_max_coalesced_frames_high)
			intrmod_cfg->intrmod_maxcnt_trigger =
				intr_coal->rx_max_coalesced_frames_high;
		else
			intrmod_cfg->intrmod_maxcnt_trigger =
				LIO_INTRMOD_MAXCNT_TRIGGER;

		if (intr_coal->rx_coalesce_usecs_high)
			intrmod_cfg->intrmod_maxtmr_trigger =
				intr_coal->rx_coalesce_usecs_high;
		else
			intrmod_cfg->intrmod_maxtmr_trigger =
				LIO_INTRMOD_MAXTMR_TRIGGER;

		if (intr_coal->rx_coalesce_usecs_low)
			intrmod_cfg->intrmod_mintmr_trigger =
				intr_coal->rx_coalesce_usecs_low;
		else
			intrmod_cfg->intrmod_mintmr_trigger =
				LIO_INTRMOD_MINTMR_TRIGGER;

		if (intr_coal->rx_max_coalesced_frames_low)
			intrmod_cfg->intrmod_mincnt_trigger =
				intr_coal->rx_max_coalesced_frames_low;
		else
			intrmod_cfg->intrmod_mincnt_trigger =
				LIO_INTRMOD_MINCNT_TRIGGER;
	}

	intrmod_cfg->intrmod_enable = adaptive;
	ret = octnet_set_intrmod_cfg(oct, intrmod_cfg);

	return ret;
}

static int
oct_cfg_rx_intrcnt(struct lio *lio, struct ethtool_coalesce *intr_coal)
{
	int ret;
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_cn6xxx *cn6xxx = (struct octeon_cn6xxx *)oct->chip;
	u32 rx_max_coalesced_frames;

	if (!intr_coal->rx_max_coalesced_frames)
		rx_max_coalesced_frames = CN6XXX_OQ_INTR_PKT;
	else
		rx_max_coalesced_frames = intr_coal->rx_max_coalesced_frames;

	/* Disable adaptive interrupt modulation */
	ret = oct_cfg_adaptive_intr(lio, intr_coal, 0);
	if (ret)
		return ret;

	/* Config Cnt based interrupt values */
	octeon_write_csr(oct, CN6XXX_SLI_OQ_INT_LEVEL_PKTS,
			 rx_max_coalesced_frames);
	CFG_SET_OQ_INTR_PKT(cn6xxx->conf, rx_max_coalesced_frames);
	return 0;
}

static int oct_cfg_rx_intrtime(struct lio *lio, struct ethtool_coalesce
			       *intr_coal)
{
	int ret;
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_cn6xxx *cn6xxx = (struct octeon_cn6xxx *)oct->chip;
	u32 time_threshold, rx_coalesce_usecs;

	if (!intr_coal->rx_coalesce_usecs)
		rx_coalesce_usecs = CN6XXX_OQ_INTR_TIME;
	else
		rx_coalesce_usecs = intr_coal->rx_coalesce_usecs;

	/* Disable adaptive interrupt modulation */
	ret = oct_cfg_adaptive_intr(lio, intr_coal, 0);
	if (ret)
		return ret;

	/* Config Time based interrupt values */
	time_threshold = lio_cn6xxx_get_oq_ticks(oct, rx_coalesce_usecs);
	octeon_write_csr(oct, CN6XXX_SLI_OQ_INT_LEVEL_TIME, time_threshold);
	CFG_SET_OQ_INTR_TIME(cn6xxx->conf, rx_coalesce_usecs);

	return 0;
}

static int lio_set_intr_coalesce(struct net_device *netdev,
				 struct ethtool_coalesce *intr_coal)
{
	struct lio *lio = GET_LIO(netdev);
	int ret;
	struct octeon_device *oct = lio->oct_dev;
	u32 j, q_no;

	if ((intr_coal->tx_max_coalesced_frames >= CN6XXX_DB_MIN) &&
	    (intr_coal->tx_max_coalesced_frames <= CN6XXX_DB_MAX)) {
		for (j = 0; j < lio->linfo.num_txpciq; j++) {
			q_no = lio->linfo.txpciq[j];
			oct->instr_queue[q_no]->fill_threshold =
				intr_coal->tx_max_coalesced_frames;
		}
	} else {
		dev_err(&oct->pci_dev->dev,
			"LIQUIDIO: Invalid tx-frames:%d. Range is min:%d max:%d\n",
			intr_coal->tx_max_coalesced_frames, CN6XXX_DB_MIN,
			CN6XXX_DB_MAX);
		return -EINVAL;
	}

	/* User requested adaptive-rx on */
	if (intr_coal->use_adaptive_rx_coalesce) {
		ret = oct_cfg_adaptive_intr(lio, intr_coal, 1);
		if (ret)
			goto ret_intrmod;
	}

	/* User requested adaptive-rx off and rx coalesce */
	if ((intr_coal->rx_coalesce_usecs) &&
	    (!intr_coal->use_adaptive_rx_coalesce)) {
		ret = oct_cfg_rx_intrtime(lio, intr_coal);
		if (ret)
			goto ret_intrmod;
	}

	/* User requested adaptive-rx off and rx coalesce */
	if ((intr_coal->rx_max_coalesced_frames) &&
	    (!intr_coal->use_adaptive_rx_coalesce)) {
		ret = oct_cfg_rx_intrcnt(lio, intr_coal);
		if (ret)
			goto ret_intrmod;
	}

	/* User requested adaptive-rx off, so use default coalesce params */
	if ((!intr_coal->rx_max_coalesced_frames) &&
	    (!intr_coal->use_adaptive_rx_coalesce) &&
	    (!intr_coal->rx_coalesce_usecs)) {
		dev_info(&oct->pci_dev->dev,
			 "Turning off adaptive-rx interrupt moderation\n");
		dev_info(&oct->pci_dev->dev,
			 "Using RX Coalesce Default values rx_coalesce_usecs:%d rx_max_coalesced_frames:%d\n",
			 CN6XXX_OQ_INTR_TIME, CN6XXX_OQ_INTR_PKT);
		ret = oct_cfg_rx_intrtime(lio, intr_coal);
		if (ret)
			goto ret_intrmod;

		ret = oct_cfg_rx_intrcnt(lio, intr_coal);
		if (ret)
			goto ret_intrmod;
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
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_RAW_HARDWARE;

	if (lio->ptp_clock)
		info->phc_index = ptp_clock_index(lio->ptp_clock);
	else
		info->phc_index = -1;

	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);

	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT);

	return 0;
}

static int lio_set_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct oct_link_info *linfo;
	struct octnic_ctrl_pkt nctrl;
	struct octnic_ctrl_params nparams;
	int ret = 0;

	/* get the link info */
	linfo = &lio->linfo;

	if (ecmd->autoneg != AUTONEG_ENABLE && ecmd->autoneg != AUTONEG_DISABLE)
		return -EINVAL;

	if (ecmd->autoneg == AUTONEG_DISABLE && ((ecmd->speed != SPEED_100 &&
						  ecmd->speed != SPEED_10) ||
						 (ecmd->duplex != DUPLEX_HALF &&
						  ecmd->duplex != DUPLEX_FULL)))
		return -EINVAL;

	/* Ethtool Support is not provided for XAUI and RXAUI Interfaces
	 * as they operate at fixed Speed and Duplex settings
	 */
	if (linfo->link.s.interface == INTERFACE_MODE_XAUI ||
	    linfo->link.s.interface == INTERFACE_MODE_RXAUI) {
		dev_info(&oct->pci_dev->dev, "XAUI IFs settings cannot be modified.\n");
		return -EINVAL;
	}

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_SET_SETTINGS;
	nctrl.wait_time = 1000;
	nctrl.netpndev = (u64)netdev;
	nctrl.ncmd.s.param1 = lio->linfo.ifidx;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	/* Passing the parameters sent by ethtool like Speed, Autoneg & Duplex
	 * to SE core application using ncmd.s.more & ncmd.s.param
	 */
	if (ecmd->autoneg == AUTONEG_ENABLE) {
		/* Autoneg ON */
		nctrl.ncmd.s.more = OCTNIC_NCMD_PHY_ON |
				     OCTNIC_NCMD_AUTONEG_ON;
		nctrl.ncmd.s.param2 = ecmd->advertising;
	} else {
		/* Autoneg OFF */
		nctrl.ncmd.s.more = OCTNIC_NCMD_PHY_ON;

		nctrl.ncmd.s.param3 = ecmd->duplex;

		nctrl.ncmd.s.param2 = ecmd->speed;
	}

	nparams.resp_order = OCTEON_RESP_ORDERED;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl, nparams);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "Failed to set settings\n");
		return -1;
	}

	return 0;
}

static int lio_nway_reset(struct net_device *netdev)
{
	if (netif_running(netdev)) {
		struct ethtool_cmd ecmd;

		memset(&ecmd, 0, sizeof(struct ethtool_cmd));
		ecmd.autoneg = 0;
		ecmd.speed = 0;
		ecmd.duplex = 0;
		lio_set_settings(netdev, &ecmd);
	}
	return 0;
}

/* Return register dump len. */
static int lio_get_regs_len(struct net_device *dev)
{
	return OCT_ETHTOOL_REGDUMP_LEN;
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

	memset(regbuf, 0, OCT_ETHTOOL_REGDUMP_LEN);
	regs->version = OCT_ETHTOOL_REGSVER;

	switch (oct->chip_id) {
	/* case OCTEON_CN73XX: Todo */
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		len += cn6xxx_read_csr_reg(regbuf + len, oct);
		len += cn6xxx_read_config_reg(regbuf + len, oct);
		break;
	default:
		dev_err(&oct->pci_dev->dev, "%s Unknown chipid: %d\n",
			__func__, oct->chip_id);
	}
}

static const struct ethtool_ops lio_ethtool_ops = {
	.get_settings		= lio_get_settings,
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
	.get_regs_len		= lio_get_regs_len,
	.get_regs		= lio_get_regs,
	.get_msglevel		= lio_get_msglevel,
	.set_msglevel		= lio_set_msglevel,
	.get_sset_count		= lio_get_sset_count,
	.nway_reset		= lio_nway_reset,
	.set_settings		= lio_set_settings,
	.get_coalesce		= lio_get_intr_coalesce,
	.set_coalesce		= lio_set_intr_coalesce,
	.get_ts_info		= lio_get_ts_info,
};

void liquidio_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &lio_ethtool_ops;
}
