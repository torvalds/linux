// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/phy.h>
#include <linux/phylink.h>

#include "fbnic.h"
#include "fbnic_mac.h"
#include "fbnic_netdev.h"

static phy_interface_t fbnic_phylink_select_interface(u8 aui)
{
	switch (aui) {
	case FBNIC_AUI_100GAUI2:
		return PHY_INTERFACE_MODE_100GBASEP;
	case FBNIC_AUI_50GAUI1:
		return PHY_INTERFACE_MODE_50GBASER;
	case FBNIC_AUI_LAUI2:
		return PHY_INTERFACE_MODE_LAUI;
	case FBNIC_AUI_25GAUI:
		return PHY_INTERFACE_MODE_25GBASER;
	}

	return PHY_INTERFACE_MODE_NA;
}

void fbnic_phylink_get_pauseparam(struct net_device *netdev,
				  struct ethtool_pauseparam *pause)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	phylink_ethtool_get_pauseparam(fbn->phylink, pause);
}

int fbnic_phylink_set_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	return phylink_ethtool_set_pauseparam(fbn->phylink, pause);
}

static void
fbnic_phylink_get_supported_fec_modes(unsigned long *supported)
{
	/* The NIC can support up to 8 possible combinations.
	 * Either 50G-CR, or 100G-CR2
	 *   This is with RS FEC mode only
	 * Either 25G-CR, or 50G-CR2
	 *   This is with No FEC, RS, or Base-R
	 */
	if (phylink_test(supported, 100000baseCR2_Full) ||
	    phylink_test(supported, 50000baseCR_Full))
		phylink_set(supported, FEC_RS);
	if (phylink_test(supported, 50000baseCR2_Full) ||
	    phylink_test(supported, 25000baseCR_Full)) {
		phylink_set(supported, FEC_BASER);
		phylink_set(supported, FEC_NONE);
		phylink_set(supported, FEC_RS);
	}
}

int fbnic_phylink_ethtool_ksettings_get(struct net_device *netdev,
					struct ethtool_link_ksettings *cmd)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	int err;

	err = phylink_ethtool_ksettings_get(fbn->phylink, cmd);
	if (!err) {
		unsigned long *supp = cmd->link_modes.supported;

		cmd->base.port = PORT_DA;
		cmd->lanes = (fbn->aui & FBNIC_AUI_MODE_R2) ? 2 : 1;

		fbnic_phylink_get_supported_fec_modes(supp);
	}

	return err;
}

int fbnic_phylink_get_fecparam(struct net_device *netdev,
			       struct ethtool_fecparam *fecparam)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	if (fbn->fec & FBNIC_FEC_RS) {
		fecparam->active_fec = ETHTOOL_FEC_RS;
		fecparam->fec = ETHTOOL_FEC_RS;
	} else if (fbn->fec & FBNIC_FEC_BASER) {
		fecparam->active_fec = ETHTOOL_FEC_BASER;
		fecparam->fec = ETHTOOL_FEC_BASER;
	} else {
		fecparam->active_fec = ETHTOOL_FEC_OFF;
		fecparam->fec = ETHTOOL_FEC_OFF;
	}

	if (fbn->aui & FBNIC_AUI_MODE_PAM4)
		fecparam->fec |= ETHTOOL_FEC_AUTO;

	return 0;
}

static struct fbnic_net *
fbnic_pcs_to_net(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct fbnic_net, phylink_pcs);
}

static void
fbnic_phylink_pcs_get_state(struct phylink_pcs *pcs, unsigned int neg_mode,
			    struct phylink_link_state *state)
{
	struct fbnic_net *fbn = fbnic_pcs_to_net(pcs);
	struct fbnic_dev *fbd = fbn->fbd;

	switch (fbn->aui) {
	case FBNIC_AUI_25GAUI:
		state->speed = SPEED_25000;
		break;
	case FBNIC_AUI_LAUI2:
	case FBNIC_AUI_50GAUI1:
		state->speed = SPEED_50000;
		break;
	case FBNIC_AUI_100GAUI2:
		state->speed = SPEED_100000;
		break;
	default:
		state->link = 0;
		return;
	}

	state->duplex = DUPLEX_FULL;

	state->link = fbd->mac->pcs_get_link(fbd);
}

static int
fbnic_phylink_pcs_enable(struct phylink_pcs *pcs)
{
	struct fbnic_net *fbn = fbnic_pcs_to_net(pcs);
	struct fbnic_dev *fbd = fbn->fbd;

	return fbd->mac->pcs_enable(fbd);
}

static void
fbnic_phylink_pcs_disable(struct phylink_pcs *pcs)
{
	struct fbnic_net *fbn = fbnic_pcs_to_net(pcs);
	struct fbnic_dev *fbd = fbn->fbd;

	return fbd->mac->pcs_disable(fbd);
}

static int
fbnic_phylink_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			 phy_interface_t interface,
			 const unsigned long *advertising,
			 bool permit_pause_to_mac)
{
	return 0;
}

static const struct phylink_pcs_ops fbnic_phylink_pcs_ops = {
	.pcs_config = fbnic_phylink_pcs_config,
	.pcs_enable = fbnic_phylink_pcs_enable,
	.pcs_disable = fbnic_phylink_pcs_disable,
	.pcs_get_state = fbnic_phylink_pcs_get_state,
};

static struct phylink_pcs *
fbnic_phylink_mac_select_pcs(struct phylink_config *config,
			     phy_interface_t interface)
{
	struct net_device *netdev = to_net_dev(config->dev);
	struct fbnic_net *fbn = netdev_priv(netdev);

	return &fbn->phylink_pcs;
}

static void
fbnic_phylink_mac_config(struct phylink_config *config, unsigned int mode,
			 const struct phylink_link_state *state)
{
}

static void
fbnic_phylink_mac_link_down(struct phylink_config *config, unsigned int mode,
			    phy_interface_t interface)
{
	struct net_device *netdev = to_net_dev(config->dev);
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	fbd->mac->link_down(fbd);

	fbn->link_down_events++;
}

static void
fbnic_phylink_mac_link_up(struct phylink_config *config,
			  struct phy_device *phy, unsigned int mode,
			  phy_interface_t interface, int speed, int duplex,
			  bool tx_pause, bool rx_pause)
{
	struct net_device *netdev = to_net_dev(config->dev);
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	fbd->mac->link_up(fbd, tx_pause, rx_pause);
}

static const struct phylink_mac_ops fbnic_phylink_mac_ops = {
	.mac_select_pcs = fbnic_phylink_mac_select_pcs,
	.mac_config = fbnic_phylink_mac_config,
	.mac_link_down = fbnic_phylink_mac_link_down,
	.mac_link_up = fbnic_phylink_mac_link_up,
};

int fbnic_phylink_init(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;
	struct phylink *phylink;

	fbn->phylink_pcs.ops = &fbnic_phylink_pcs_ops;

	fbn->phylink_config.dev = &netdev->dev;
	fbn->phylink_config.type = PHYLINK_NETDEV;
	fbn->phylink_config.mac_capabilities = MAC_SYM_PAUSE | MAC_ASYM_PAUSE |
					       MAC_25000FD | MAC_50000FD |
					       MAC_100000FD;
	fbn->phylink_config.default_an_inband = true;

	__set_bit(PHY_INTERFACE_MODE_100GBASEP,
		  fbn->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_50GBASER,
		  fbn->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_LAUI,
		  fbn->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_25GBASER,
		  fbn->phylink_config.supported_interfaces);

	fbnic_mac_get_fw_settings(fbd, &fbn->aui, &fbn->fec);

	phylink = phylink_create(&fbn->phylink_config, NULL,
				 fbnic_phylink_select_interface(fbn->aui),
				 &fbnic_phylink_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	fbn->phylink = phylink;

	return 0;
}
