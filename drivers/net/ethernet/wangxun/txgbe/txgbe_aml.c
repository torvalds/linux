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
	u32 status;

	wr32(wx, WX_GPIO_INTTYPE_LEVEL, TXGBE_GPIOBIT_2 | TXGBE_GPIOBIT_3);
	wr32(wx, WX_GPIO_INTEN, TXGBE_GPIOBIT_2 | TXGBE_GPIOBIT_3);

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
	u32 status;

	wr32(wx, WX_GPIO_INTMASK, 0xFF);
	status = rd32(wx, WX_GPIO_INTSTATUS);
	if (status & TXGBE_GPIOBIT_2) {
		set_bit(WX_FLAG_NEED_SFP_RESET, wx->flags);
		wr32(wx, WX_GPIO_EOI, TXGBE_GPIOBIT_2);
		wx_service_event_schedule(wx);
	}
	if (status & TXGBE_GPIOBIT_3) {
		set_bit(WX_FLAG_NEED_LINK_CONFIG, wx->flags);
		wx_service_event_schedule(wx);
		wr32(wx, WX_GPIO_EOI, TXGBE_GPIOBIT_3);
	}

	wr32(wx, WX_GPIO_INTMASK, 0);
	return IRQ_HANDLED;
}

int txgbe_test_hostif(struct wx *wx)
{
	struct txgbe_hic_ephy_getlink buffer;

	if (wx->mac.type != wx_mac_aml)
		return 0;

	buffer.hdr.cmd = FW_PHY_GET_LINK_CMD;
	buffer.hdr.buf_len = sizeof(struct txgbe_hic_ephy_getlink) -
			     sizeof(struct wx_hic_hdr);
	buffer.hdr.cmd_or_resp.cmd_resv = FW_CEM_CMD_RESERVED;

	return wx_host_interface_command(wx, (u32 *)&buffer, sizeof(buffer),
					WX_HI_COMMAND_TIMEOUT, true);
}

static int txgbe_identify_sfp_hostif(struct wx *wx, struct txgbe_hic_i2c_read *buffer)
{
	buffer->hdr.cmd = FW_READ_SFP_INFO_CMD;
	buffer->hdr.buf_len = sizeof(struct txgbe_hic_i2c_read) -
			      sizeof(struct wx_hic_hdr);
	buffer->hdr.cmd_or_resp.cmd_resv = FW_CEM_CMD_RESERVED;

	return wx_host_interface_command(wx, (u32 *)buffer,
					 sizeof(struct txgbe_hic_i2c_read),
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
	case SPEED_25000:
		buffer.speed = TXGBE_LINK_SPEED_25GB_FULL;
		break;
	case SPEED_10000:
		buffer.speed = TXGBE_LINK_SPEED_10GB_FULL;
		break;
	}

	buffer.fec_mode = TXGBE_PHY_FEC_AUTO;
	buffer.autoneg = autoneg;
	buffer.duplex = duplex;

	return wx_host_interface_command(wx, (u32 *)&buffer, sizeof(buffer),
					 WX_HI_COMMAND_TIMEOUT, true);
}

static void txgbe_get_link_capabilities(struct wx *wx)
{
	struct txgbe *txgbe = wx->priv;

	if (test_bit(PHY_INTERFACE_MODE_25GBASER, txgbe->sfp_interfaces))
		wx->adv_speed = SPEED_25000;
	else if (test_bit(PHY_INTERFACE_MODE_10GBASER, txgbe->sfp_interfaces))
		wx->adv_speed = SPEED_10000;
	else
		wx->adv_speed = SPEED_UNKNOWN;

	wx->adv_duplex = wx->adv_speed == SPEED_UNKNOWN ?
			 DUPLEX_HALF : DUPLEX_FULL;
}

static void txgbe_get_phy_link(struct wx *wx, int *speed)
{
	u32 status;

	status = rd32(wx, TXGBE_CFG_PORT_ST);
	if (!(status & TXGBE_CFG_PORT_ST_LINK_UP))
		*speed = SPEED_UNKNOWN;
	else if (status & TXGBE_CFG_PORT_ST_LINK_AML_25G)
		*speed = SPEED_25000;
	else if (status & TXGBE_CFG_PORT_ST_LINK_AML_10G)
		*speed = SPEED_10000;
	else
		*speed = SPEED_UNKNOWN;
}

int txgbe_set_phy_link(struct wx *wx)
{
	int speed, err;
	u32 gpio;

	/* Check RX signal */
	gpio = rd32(wx, WX_GPIO_EXT);
	if (gpio & TXGBE_GPIOBIT_3)
		return -ENODEV;

	txgbe_get_link_capabilities(wx);
	if (wx->adv_speed == SPEED_UNKNOWN)
		return -ENODEV;

	txgbe_get_phy_link(wx, &speed);
	if (speed == wx->adv_speed)
		return 0;

	err = txgbe_set_phy_link_hostif(wx, wx->adv_speed, 0, wx->adv_duplex);
	if (err) {
		wx_err(wx, "Failed to setup link\n");
		return err;
	}

	return 0;
}

static int txgbe_sfp_to_linkmodes(struct wx *wx, struct txgbe_sfp_id *id)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(modes) = { 0, };
	DECLARE_PHY_INTERFACE_MASK(interfaces);
	struct txgbe *txgbe = wx->priv;

	if (id->com_25g_code & (TXGBE_SFF_25GBASESR_CAPABLE |
				TXGBE_SFF_25GBASEER_CAPABLE |
				TXGBE_SFF_25GBASELR_CAPABLE)) {
		phylink_set(modes, 25000baseSR_Full);
		__set_bit(PHY_INTERFACE_MODE_25GBASER, interfaces);
	}
	if (id->com_10g_code & TXGBE_SFF_10GBASESR_CAPABLE) {
		phylink_set(modes, 10000baseSR_Full);
		__set_bit(PHY_INTERFACE_MODE_10GBASER, interfaces);
	}
	if (id->com_10g_code & TXGBE_SFF_10GBASELR_CAPABLE) {
		phylink_set(modes, 10000baseLR_Full);
		__set_bit(PHY_INTERFACE_MODE_10GBASER, interfaces);
	}

	if (phy_interface_empty(interfaces)) {
		wx_err(wx, "unsupported SFP module\n");
		return -EINVAL;
	}

	phylink_set(modes, Pause);
	phylink_set(modes, Asym_Pause);
	phylink_set(modes, FIBRE);
	txgbe->link_port = PORT_FIBRE;

	if (!linkmode_equal(txgbe->sfp_support, modes)) {
		linkmode_copy(txgbe->sfp_support, modes);
		phy_interface_and(txgbe->sfp_interfaces,
				  wx->phylink_config.supported_interfaces,
				  interfaces);
		linkmode_copy(txgbe->advertising, modes);

		set_bit(WX_FLAG_NEED_LINK_CONFIG, wx->flags);
	}

	return 0;
}

int txgbe_identify_sfp(struct wx *wx)
{
	struct txgbe_hic_i2c_read buffer;
	struct txgbe_sfp_id *id;
	int err = 0;
	u32 gpio;

	gpio = rd32(wx, WX_GPIO_EXT);
	if (gpio & TXGBE_GPIOBIT_2)
		return -ENODEV;

	err = txgbe_identify_sfp_hostif(wx, &buffer);
	if (err) {
		wx_err(wx, "Failed to identify SFP module\n");
		return err;
	}

	id = &buffer.id;
	if (id->identifier != TXGBE_SFF_IDENTIFIER_SFP) {
		wx_err(wx, "Invalid SFP module\n");
		return -ENODEV;
	}

	err = txgbe_sfp_to_linkmodes(wx, id);
	if (err)
		return err;

	if (gpio & TXGBE_GPIOBIT_3)
		set_bit(WX_FLAG_NEED_LINK_CONFIG, wx->flags);

	return 0;
}

void txgbe_setup_link(struct wx *wx)
{
	struct txgbe *txgbe = wx->priv;

	phy_interface_zero(txgbe->sfp_interfaces);
	linkmode_zero(txgbe->sfp_support);

	txgbe_identify_sfp(wx);
}

static void txgbe_get_link_state(struct phylink_config *config,
				 struct phylink_link_state *state)
{
	struct wx *wx = phylink_to_wx(config);
	int speed;

	txgbe_get_phy_link(wx, &speed);
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

	txcfg = rd32(wx, TXGBE_AML_MAC_TX_CFG);
	txcfg &= ~TXGBE_AML_MAC_TX_CFG_SPEED_MASK;

	switch (speed) {
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

	phy_mode = PHY_INTERFACE_MODE_25GBASER;
	__set_bit(PHY_INTERFACE_MODE_25GBASER, config->supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_10GBASER, config->supported_interfaces);

	phylink = phylink_create(config, NULL, phy_mode, &txgbe_mac_ops_aml);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	state.speed = SPEED_25000;
	state.duplex = DUPLEX_FULL;
	err = phylink_set_fixed_link(phylink, &state);
	if (err) {
		wx_err(wx, "Failed to set fixed link\n");
		return err;
	}

	wx->phylink = phylink;

	return 0;
}
