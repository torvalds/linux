// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#include <linux/phylink.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include <linux/phy.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_lib.h"
#include "../libwx/wx_hw.h"
#include "txgbe_type.h"
#include "txgbe_aml.h"
#include "txgbe_hw.h"

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
}

static void txgbe_mac_link_down_aml(struct phylink_config *config,
				    unsigned int mode,
				    phy_interface_t interface)
{
	struct wx *wx = phylink_to_wx(config);

	wr32m(wx, TXGBE_AML_MAC_TX_CFG, TXGBE_AML_MAC_TX_CFG_TE, 0);
	wr32m(wx, WX_MAC_RX_CFG, WX_MAC_RX_CFG_RE, 0);

	wx->speed = SPEED_UNKNOWN;
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
