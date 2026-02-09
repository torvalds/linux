// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#include <linux/phylink.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include <linux/phy.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_lib.h"
#include "../libwx/wx_ptp.h"
#include "../libwx/wx_hw.h"
#include "../libwx/wx_sriov.h"
#include "txgbe_type.h"
#include "txgbe_aml.h"
#include "txgbe_hw.h"

void txgbe_gpio_init_aml(struct wx *wx)
{
	u32 status, mod_rst;

	if (wx->mac.type == wx_mac_aml40)
		mod_rst = TXGBE_GPIOBIT_4;
	else
		mod_rst = TXGBE_GPIOBIT_2;

	wr32(wx, WX_GPIO_INTTYPE_LEVEL, mod_rst);
	wr32(wx, WX_GPIO_INTEN, mod_rst);

	status = rd32(wx, WX_GPIO_INTSTATUS);
	for (int i = 0; i < 6; i++) {
		if (status & BIT(i))
			wr32(wx, WX_GPIO_EOI, BIT(i));
	}
}

irqreturn_t txgbe_gpio_irq_handler_aml(int irq, void *data)
{
	struct txgbe *txgbe = data;
	struct wx *wx = txgbe->wx;
	u32 status, mod_rst;

	if (wx->mac.type == wx_mac_aml40)
		mod_rst = TXGBE_GPIOBIT_4;
	else
		mod_rst = TXGBE_GPIOBIT_2;

	wr32(wx, WX_GPIO_INTMASK, 0xFF);
	status = rd32(wx, WX_GPIO_INTSTATUS);
	if (status & mod_rst) {
		set_bit(WX_FLAG_NEED_MODULE_RESET, wx->flags);
		wr32(wx, WX_GPIO_EOI, mod_rst);
		wx_service_event_schedule(wx);
	}

	wr32(wx, WX_GPIO_INTMASK, 0);
	return IRQ_HANDLED;
}

int txgbe_test_hostif(struct wx *wx)
{
	struct txgbe_hic_ephy_getlink buffer;

	if (wx->mac.type == wx_mac_sp)
		return 0;

	buffer.hdr.cmd = FW_PHY_GET_LINK_CMD;
	buffer.hdr.buf_len = sizeof(struct txgbe_hic_ephy_getlink) -
			     sizeof(struct wx_hic_hdr);
	buffer.hdr.cmd_or_resp.cmd_resv = FW_CEM_CMD_RESERVED;

	return wx_host_interface_command(wx, (u32 *)&buffer, sizeof(buffer),
					 WX_HI_COMMAND_TIMEOUT, false);
}

int txgbe_read_eeprom_hostif(struct wx *wx,
			     struct txgbe_hic_i2c_read *buffer,
			     u32 length, u8 *data)
{
	u32 dword_len, offset, value, i;
	int err;

	buffer->hdr.cmd = FW_READ_EEPROM_CMD;
	buffer->hdr.buf_len = sizeof(struct txgbe_hic_i2c_read) -
			      sizeof(struct wx_hic_hdr);
	buffer->hdr.cmd_or_resp.cmd_resv = FW_CEM_CMD_RESERVED;

	err = wx_host_interface_command(wx, (u32 *)buffer,
					sizeof(struct txgbe_hic_i2c_read),
					WX_HI_COMMAND_TIMEOUT, false);
	if (err != 0)
		return err;

	/* buffer length offset to read return data */
	offset = sizeof(struct txgbe_hic_i2c_read) >> 2;
	dword_len = round_up(length, 4) >> 2;

	for (i = 0; i < dword_len; i++) {
		value = rd32a(wx, WX_FW2SW_MBOX, i + offset);
		le32_to_cpus(&value);

		memcpy(data, &value, 4);
		data += 4;
	}

	return 0;
}

static int txgbe_identify_module_hostif(struct wx *wx,
					struct txgbe_hic_get_module_info *buffer)
{
	buffer->hdr.cmd = FW_GET_MODULE_INFO_CMD;
	buffer->hdr.buf_len = sizeof(struct txgbe_hic_get_module_info) -
			      sizeof(struct wx_hic_hdr);
	buffer->hdr.cmd_or_resp.cmd_resv = FW_CEM_CMD_RESERVED;

	return wx_host_interface_command(wx, (u32 *)buffer,
					 sizeof(struct txgbe_hic_get_module_info),
					 WX_HI_COMMAND_TIMEOUT, true);
}

static int txgbe_set_phy_link_hostif(struct wx *wx, int speed, int autoneg, int duplex)
{
	struct txgbe_hic_ephy_setlink buffer;

	buffer.hdr.cmd = FW_PHY_SET_LINK_CMD;
	buffer.hdr.buf_len = sizeof(struct txgbe_hic_ephy_setlink) -
			     sizeof(struct wx_hic_hdr);
	buffer.hdr.cmd_or_resp.cmd_resv = FW_CEM_CMD_RESERVED;

	switch (speed) {
	case SPEED_40000:
		buffer.speed = TXGBE_LINK_SPEED_40GB_FULL;
		break;
	case SPEED_25000:
		buffer.speed = TXGBE_LINK_SPEED_25GB_FULL;
		break;
	case SPEED_10000:
		buffer.speed = TXGBE_LINK_SPEED_10GB_FULL;
		break;
	default:
		buffer.speed = TXGBE_LINK_SPEED_UNKNOWN;
		break;
	}

	buffer.fec_mode = TXGBE_PHY_FEC_AUTO;
	buffer.autoneg = autoneg;
	buffer.duplex = duplex;

	return wx_host_interface_command(wx, (u32 *)&buffer, sizeof(buffer),
					 WX_HI_COMMAND_TIMEOUT, false);
}

static void txgbe_get_link_capabilities(struct wx *wx, int *speed,
					int *autoneg, int *duplex)
{
	struct txgbe *txgbe = wx->priv;

	if (test_bit(PHY_INTERFACE_MODE_XLGMII, txgbe->link_interfaces))
		*speed = SPEED_40000;
	else if (test_bit(PHY_INTERFACE_MODE_25GBASER, txgbe->link_interfaces))
		*speed = SPEED_25000;
	else if (test_bit(PHY_INTERFACE_MODE_10GBASER, txgbe->link_interfaces))
		*speed = SPEED_10000;
	else
		*speed = SPEED_UNKNOWN;

	*autoneg = phylink_test(txgbe->advertising, Autoneg);
	*duplex = *speed == SPEED_UNKNOWN ? DUPLEX_HALF : DUPLEX_FULL;
}

static void txgbe_get_mac_link(struct wx *wx, int *speed)
{
	u32 status;

	status = rd32(wx, TXGBE_CFG_PORT_ST);
	if (!(status & TXGBE_CFG_PORT_ST_LINK_UP))
		*speed = SPEED_UNKNOWN;
	else if (status & TXGBE_CFG_PORT_ST_LINK_AML_40G)
		*speed = SPEED_40000;
	else if (status & TXGBE_CFG_PORT_ST_LINK_AML_25G)
		*speed = SPEED_25000;
	else if (status & TXGBE_CFG_PORT_ST_LINK_AML_10G)
		*speed = SPEED_10000;
	else
		*speed = SPEED_UNKNOWN;
}

int txgbe_set_phy_link(struct wx *wx)
{
	int speed, autoneg, duplex, err;

	txgbe_get_link_capabilities(wx, &speed, &autoneg, &duplex);

	err = txgbe_set_phy_link_hostif(wx, speed, autoneg, duplex);
	if (err) {
		wx_err(wx, "Failed to setup link\n");
		return err;
	}

	return 0;
}

static int txgbe_sfp_to_linkmodes(struct wx *wx, struct txgbe_sff_id *id)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(modes) = { 0, };
	DECLARE_PHY_INTERFACE_MASK(interfaces);
	struct txgbe *txgbe = wx->priv;

	if (id->cable_tech & TXGBE_SFF_DA_PASSIVE_CABLE) {
		txgbe->link_port = PORT_DA;
		phylink_set(modes, Autoneg);
		if (id->com_25g_code == TXGBE_SFF_25GBASECR_91FEC ||
		    id->com_25g_code == TXGBE_SFF_25GBASECR_74FEC ||
		    id->com_25g_code == TXGBE_SFF_25GBASECR_NOFEC) {
			phylink_set(modes, 25000baseCR_Full);
			phylink_set(modes, 10000baseCR_Full);
			__set_bit(PHY_INTERFACE_MODE_25GBASER, interfaces);
			__set_bit(PHY_INTERFACE_MODE_10GBASER, interfaces);
		} else {
			phylink_set(modes, 10000baseCR_Full);
			__set_bit(PHY_INTERFACE_MODE_10GBASER, interfaces);
		}
	} else if (id->cable_tech & TXGBE_SFF_DA_ACTIVE_CABLE) {
		txgbe->link_port = PORT_DA;
		phylink_set(modes, Autoneg);
		phylink_set(modes, 25000baseCR_Full);
		__set_bit(PHY_INTERFACE_MODE_25GBASER, interfaces);
	} else {
		if (id->com_25g_code == TXGBE_SFF_25GBASESR_CAPABLE ||
		    id->com_25g_code == TXGBE_SFF_25GBASEER_CAPABLE ||
		    id->com_25g_code == TXGBE_SFF_25GBASELR_CAPABLE) {
			txgbe->link_port = PORT_FIBRE;
			phylink_set(modes, 25000baseSR_Full);
			__set_bit(PHY_INTERFACE_MODE_25GBASER, interfaces);
		}
		if (id->com_10g_code & TXGBE_SFF_10GBASESR_CAPABLE) {
			txgbe->link_port = PORT_FIBRE;
			phylink_set(modes, 10000baseSR_Full);
			__set_bit(PHY_INTERFACE_MODE_10GBASER, interfaces);
		}
		if (id->com_10g_code & TXGBE_SFF_10GBASELR_CAPABLE) {
			txgbe->link_port = PORT_FIBRE;
			phylink_set(modes, 10000baseLR_Full);
			__set_bit(PHY_INTERFACE_MODE_10GBASER, interfaces);
		}
	}

	if (phy_interface_empty(interfaces)) {
		wx_err(wx, "unsupported SFP module\n");
		return -EINVAL;
	}

	phylink_set(modes, Pause);
	phylink_set(modes, Asym_Pause);
	phylink_set(modes, FIBRE);

	if (!linkmode_equal(txgbe->link_support, modes)) {
		linkmode_copy(txgbe->link_support, modes);
		phy_interface_and(txgbe->link_interfaces,
				  wx->phylink_config.supported_interfaces,
				  interfaces);
		linkmode_copy(txgbe->advertising, modes);

		set_bit(WX_FLAG_NEED_LINK_CONFIG, wx->flags);
	}

	return 0;
}

static int txgbe_qsfp_to_linkmodes(struct wx *wx, struct txgbe_sff_id *id)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(modes) = { 0, };
	DECLARE_PHY_INTERFACE_MASK(interfaces);
	struct txgbe *txgbe = wx->priv;

	if (id->transceiver_type & TXGBE_SFF_ETHERNET_40G_CR4) {
		txgbe->link_port = PORT_DA;
		phylink_set(modes, Autoneg);
		phylink_set(modes, 40000baseCR4_Full);
		phylink_set(modes, 10000baseCR_Full);
		__set_bit(PHY_INTERFACE_MODE_XLGMII, interfaces);
		__set_bit(PHY_INTERFACE_MODE_10GBASER, interfaces);
	}
	if (id->transceiver_type & TXGBE_SFF_ETHERNET_40G_SR4) {
		txgbe->link_port = PORT_FIBRE;
		phylink_set(modes, 40000baseSR4_Full);
		__set_bit(PHY_INTERFACE_MODE_XLGMII, interfaces);
	}
	if (id->transceiver_type & TXGBE_SFF_ETHERNET_40G_LR4) {
		txgbe->link_port = PORT_FIBRE;
		phylink_set(modes, 40000baseLR4_Full);
		__set_bit(PHY_INTERFACE_MODE_XLGMII, interfaces);
	}
	if (id->transceiver_type & TXGBE_SFF_ETHERNET_40G_ACTIVE) {
		txgbe->link_port = PORT_DA;
		phylink_set(modes, Autoneg);
		phylink_set(modes, 40000baseCR4_Full);
		__set_bit(PHY_INTERFACE_MODE_XLGMII, interfaces);
	}
	if (id->transceiver_type & TXGBE_SFF_ETHERNET_RSRVD) {
		if (id->sff_opt1 & TXGBE_SFF_ETHERNET_100G_CR4) {
			txgbe->link_port = PORT_DA;
			phylink_set(modes, Autoneg);
			phylink_set(modes, 40000baseCR4_Full);
			phylink_set(modes, 25000baseCR_Full);
			phylink_set(modes, 10000baseCR_Full);
			__set_bit(PHY_INTERFACE_MODE_XLGMII, interfaces);
			__set_bit(PHY_INTERFACE_MODE_25GBASER, interfaces);
			__set_bit(PHY_INTERFACE_MODE_10GBASER, interfaces);
		}
	}

	if (phy_interface_empty(interfaces)) {
		wx_err(wx, "unsupported QSFP module\n");
		return -EINVAL;
	}

	phylink_set(modes, Pause);
	phylink_set(modes, Asym_Pause);
	phylink_set(modes, FIBRE);

	if (!linkmode_equal(txgbe->link_support, modes)) {
		linkmode_copy(txgbe->link_support, modes);
		phy_interface_and(txgbe->link_interfaces,
				  wx->phylink_config.supported_interfaces,
				  interfaces);
		linkmode_copy(txgbe->advertising, modes);

		set_bit(WX_FLAG_NEED_LINK_CONFIG, wx->flags);
	}

	return 0;
}

int txgbe_identify_module(struct wx *wx)
{
	struct txgbe_hic_get_module_info buffer;
	struct txgbe_sff_id *id;
	int err = 0;
	u32 mod_abs;
	u32 gpio;

	if (wx->mac.type == wx_mac_aml40)
		mod_abs = TXGBE_GPIOBIT_4;
	else
		mod_abs = TXGBE_GPIOBIT_2;

	gpio = rd32(wx, WX_GPIO_EXT);
	if (gpio & mod_abs)
		return -ENODEV;

	err = txgbe_identify_module_hostif(wx, &buffer);
	if (err) {
		wx_err(wx, "Failed to identify module\n");
		return err;
	}

	id = &buffer.id;
	if (id->identifier != TXGBE_SFF_IDENTIFIER_SFP &&
	    id->identifier != TXGBE_SFF_IDENTIFIER_QSFP &&
	    id->identifier != TXGBE_SFF_IDENTIFIER_QSFP_PLUS &&
	    id->identifier != TXGBE_SFF_IDENTIFIER_QSFP28) {
		wx_err(wx, "Invalid module\n");
		return -ENODEV;
	}

	if (id->transceiver_type == 0xFF)
		return txgbe_sfp_to_linkmodes(wx, id);

	return txgbe_qsfp_to_linkmodes(wx, id);
}

void txgbe_setup_link(struct wx *wx)
{
	struct txgbe *txgbe = wx->priv;

	phy_interface_zero(txgbe->link_interfaces);
	linkmode_zero(txgbe->link_support);

	set_bit(WX_FLAG_NEED_MODULE_RESET, wx->flags);
	wx_service_event_schedule(wx);
}

static void txgbe_get_link_state(struct phylink_config *config,
				 struct phylink_link_state *state)
{
	struct wx *wx = phylink_to_wx(config);
	int speed;

	txgbe_get_mac_link(wx, &speed);
	state->link = speed != SPEED_UNKNOWN;
	state->speed = speed;
	state->duplex = state->link ? DUPLEX_FULL : DUPLEX_UNKNOWN;
}

static void txgbe_reconfig_mac(struct wx *wx)
{
	u32 wdg, fc;

	wdg = rd32(wx, WX_MAC_WDG_TIMEOUT);
	fc = rd32(wx, WX_MAC_RX_FLOW_CTRL);

	wr32(wx, WX_MIS_RST, TXGBE_MIS_RST_MAC_RST(wx->bus.func));
	/* wait for MAC reset complete */
	usleep_range(1000, 1500);

	wr32m(wx, TXGBE_MAC_MISC_CTL, TXGBE_MAC_MISC_CTL_LINK_STS_MOD,
	      TXGBE_MAC_MISC_CTL_LINK_BOTH);
	wx_reset_mac(wx);

	wr32(wx, WX_MAC_WDG_TIMEOUT, wdg);
	wr32(wx, WX_MAC_RX_FLOW_CTRL, fc);
}

static void txgbe_mac_link_up_aml(struct phylink_config *config,
				  struct phy_device *phy,
				  unsigned int mode,
				  phy_interface_t interface,
				  int speed, int duplex,
				  bool tx_pause, bool rx_pause)
{
	struct wx *wx = phylink_to_wx(config);
	u32 txcfg;

	wx_fc_enable(wx, tx_pause, rx_pause);

	txgbe_reconfig_mac(wx);
	txgbe_enable_sec_tx_path(wx);

	txcfg = rd32(wx, TXGBE_AML_MAC_TX_CFG);
	txcfg &= ~TXGBE_AML_MAC_TX_CFG_SPEED_MASK;

	switch (speed) {
	case SPEED_40000:
		txcfg |= TXGBE_AML_MAC_TX_CFG_SPEED_40G;
		break;
	case SPEED_25000:
		txcfg |= TXGBE_AML_MAC_TX_CFG_SPEED_25G;
		break;
	case SPEED_10000:
		txcfg |= TXGBE_AML_MAC_TX_CFG_SPEED_10G;
		break;
	default:
		break;
	}

	wr32m(wx, WX_MAC_RX_CFG, WX_MAC_RX_CFG_RE, WX_MAC_RX_CFG_RE);
	wr32(wx, TXGBE_AML_MAC_TX_CFG, txcfg | TXGBE_AML_MAC_TX_CFG_TE);

	wx->speed = speed;
	wx->last_rx_ptp_check = jiffies;
	if (test_bit(WX_STATE_PTP_RUNNING, wx->state))
		wx_ptp_reset_cyclecounter(wx);
	/* ping all the active vfs to let them know we are going up */
	wx_ping_all_vfs_with_link_status(wx, true);
}

static void txgbe_mac_link_down_aml(struct phylink_config *config,
				    unsigned int mode,
				    phy_interface_t interface)
{
	struct wx *wx = phylink_to_wx(config);

	wr32m(wx, TXGBE_AML_MAC_TX_CFG, TXGBE_AML_MAC_TX_CFG_TE, 0);
	wr32m(wx, WX_MAC_RX_CFG, WX_MAC_RX_CFG_RE, 0);

	wx->speed = SPEED_UNKNOWN;
	if (test_bit(WX_STATE_PTP_RUNNING, wx->state))
		wx_ptp_reset_cyclecounter(wx);
	/* ping all the active vfs to let them know we are going down */
	wx_ping_all_vfs_with_link_status(wx, false);
}

static void txgbe_mac_config_aml(struct phylink_config *config, unsigned int mode,
				 const struct phylink_link_state *state)
{
}

static const struct phylink_mac_ops txgbe_mac_ops_aml = {
	.mac_config = txgbe_mac_config_aml,
	.mac_link_down = txgbe_mac_link_down_aml,
	.mac_link_up = txgbe_mac_link_up_aml,
};

int txgbe_phylink_init_aml(struct txgbe *txgbe)
{
	struct phylink_link_state state;
	struct phylink_config *config;
	struct wx *wx = txgbe->wx;
	phy_interface_t phy_mode;
	struct phylink *phylink;
	int err;

	config = &wx->phylink_config;
	config->dev = &wx->netdev->dev;
	config->type = PHYLINK_NETDEV;
	config->mac_capabilities = MAC_25000FD | MAC_10000FD |
				   MAC_SYM_PAUSE | MAC_ASYM_PAUSE;
	config->get_fixed_state = txgbe_get_link_state;

	if (wx->mac.type == wx_mac_aml40) {
		config->mac_capabilities |= MAC_40000FD;
		phy_mode = PHY_INTERFACE_MODE_XLGMII;
		__set_bit(PHY_INTERFACE_MODE_XLGMII, config->supported_interfaces);
		state.speed = SPEED_40000;
		state.duplex = DUPLEX_FULL;
	} else {
		phy_mode = PHY_INTERFACE_MODE_25GBASER;
		state.speed = SPEED_25000;
		state.duplex = DUPLEX_FULL;
	}

	__set_bit(PHY_INTERFACE_MODE_25GBASER, config->supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_10GBASER, config->supported_interfaces);

	phylink = phylink_create(config, NULL, phy_mode, &txgbe_mac_ops_aml);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	err = phylink_set_fixed_link(phylink, &state);
	if (err) {
		wx_err(wx, "Failed to set fixed link\n");
		return err;
	}

	wx->phylink = phylink;

	return 0;
}
