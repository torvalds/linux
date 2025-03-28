// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/phy.h>
#include <linux/phylink.h>

#include "fbnic.h"
#include "fbnic_mac.h"
#include "fbnic_netdev.h"

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

	/* For now we use hard-coded defaults and FW config to determine
	 * the current values. In future patches we will add support for
	 * reconfiguring these values and changing link settings.
	 */
	switch (fbd->fw_cap.link_speed) {
	case FBNIC_FW_LINK_SPEED_25R1:
		state->speed = SPEED_25000;
		break;
	case FBNIC_FW_LINK_SPEED_50R2:
		state->speed = SPEED_50000;
		break;
	case FBNIC_FW_LINK_SPEED_100R2:
		state->speed = SPEED_100000;
		break;
	default:
		state->speed = SPEED_UNKNOWN;
		break;
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
	struct phylink *phylink;

	fbn->phylink_pcs.ops = &fbnic_phylink_pcs_ops;

	fbn->phylink_config.dev = &netdev->dev;
	fbn->phylink_config.type = PHYLINK_NETDEV;
	fbn->phylink_config.mac_capabilities = MAC_SYM_PAUSE | MAC_ASYM_PAUSE |
					       MAC_10000FD | MAC_25000FD |
					       MAC_40000FD | MAC_50000FD |
					       MAC_100000FD;
	fbn->phylink_config.default_an_inband = true;

	__set_bit(PHY_INTERFACE_MODE_XGMII,
		  fbn->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_XLGMII,
		  fbn->phylink_config.supported_interfaces);

	phylink = phylink_create(&fbn->phylink_config, NULL,
				 PHY_INTERFACE_MODE_XLGMII,
				 &fbnic_phylink_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	fbn->phylink = phylink;

	return 0;
}
