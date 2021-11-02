// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 */

#include <linux/module.h>
#include <linux/phylink.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/sfp.h>

#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_port.h"

static bool port_conf_has_changed(struct sparx5_port_config *a, struct sparx5_port_config *b)
{
	if (a->speed != b->speed ||
	    a->portmode != b->portmode ||
	    a->autoneg != b->autoneg ||
	    a->pause_adv != b->pause_adv ||
	    a->power_down != b->power_down ||
	    a->media != b->media)
		return true;
	return false;
}

static void sparx5_phylink_validate(struct phylink_config *config,
				    unsigned long *supported,
				    struct phylink_link_state *state)
{
	struct sparx5_port *port = netdev_priv(to_net_dev(config->dev));
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	phylink_set(mask, Autoneg);
	phylink_set_port_modes(mask);
	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);

	switch (state->interface) {
	case PHY_INTERFACE_MODE_5GBASER:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_25GBASER:
	case PHY_INTERFACE_MODE_NA:
		if (port->conf.bandwidth == SPEED_5000)
			phylink_set(mask, 5000baseT_Full);
		if (port->conf.bandwidth == SPEED_10000) {
			phylink_set(mask, 5000baseT_Full);
			phylink_set(mask, 10000baseT_Full);
			phylink_set(mask, 10000baseCR_Full);
			phylink_set(mask, 10000baseSR_Full);
			phylink_set(mask, 10000baseLR_Full);
			phylink_set(mask, 10000baseLRM_Full);
			phylink_set(mask, 10000baseER_Full);
		}
		if (port->conf.bandwidth == SPEED_25000) {
			phylink_set(mask, 5000baseT_Full);
			phylink_set(mask, 10000baseT_Full);
			phylink_set(mask, 10000baseCR_Full);
			phylink_set(mask, 10000baseSR_Full);
			phylink_set(mask, 10000baseLR_Full);
			phylink_set(mask, 10000baseLRM_Full);
			phylink_set(mask, 10000baseER_Full);
			phylink_set(mask, 25000baseCR_Full);
			phylink_set(mask, 25000baseSR_Full);
		}
		if (state->interface != PHY_INTERFACE_MODE_NA)
			break;
		fallthrough;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		phylink_set(mask, 10baseT_Half);
		phylink_set(mask, 10baseT_Full);
		phylink_set(mask, 100baseT_Half);
		phylink_set(mask, 100baseT_Full);
		phylink_set(mask, 1000baseT_Full);
		phylink_set(mask, 1000baseX_Full);
		if (state->interface != PHY_INTERFACE_MODE_NA)
			break;
		fallthrough;
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		if (state->interface != PHY_INTERFACE_MODE_2500BASEX) {
			phylink_set(mask, 1000baseT_Full);
			phylink_set(mask, 1000baseX_Full);
		}
		if (state->interface == PHY_INTERFACE_MODE_2500BASEX ||
		    state->interface == PHY_INTERFACE_MODE_NA) {
			phylink_set(mask, 2500baseT_Full);
			phylink_set(mask, 2500baseX_Full);
		}
		break;
	default:
		linkmode_zero(supported);
		return;
	}
	linkmode_and(supported, supported, mask);
	linkmode_and(state->advertising, state->advertising, mask);
}

static void sparx5_phylink_mac_config(struct phylink_config *config,
				      unsigned int mode,
				      const struct phylink_link_state *state)
{
	/* Currently not used */
}

static void sparx5_phylink_mac_link_up(struct phylink_config *config,
				       struct phy_device *phy,
				       unsigned int mode,
				       phy_interface_t interface,
				       int speed, int duplex,
				       bool tx_pause, bool rx_pause)
{
	struct sparx5_port *port = netdev_priv(to_net_dev(config->dev));
	struct sparx5_port_config conf;
	int err;

	conf = port->conf;
	conf.duplex = duplex;
	conf.pause = 0;
	conf.pause |= tx_pause ? MLO_PAUSE_TX : 0;
	conf.pause |= rx_pause ? MLO_PAUSE_RX : 0;
	conf.speed = speed;
	/* Configure the port to speed/duplex/pause */
	err = sparx5_port_config(port->sparx5, port, &conf);
	if (err)
		netdev_err(port->ndev, "port config failed: %d\n", err);
}

static void sparx5_phylink_mac_link_down(struct phylink_config *config,
					 unsigned int mode,
					 phy_interface_t interface)
{
	/* Currently not used */
}

static struct sparx5_port *sparx5_pcs_to_port(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct sparx5_port, phylink_pcs);
}

static void sparx5_pcs_get_state(struct phylink_pcs *pcs,
				 struct phylink_link_state *state)
{
	struct sparx5_port *port = sparx5_pcs_to_port(pcs);
	struct sparx5_port_status status;

	sparx5_get_port_status(port->sparx5, port, &status);
	state->link = status.link && !status.link_down;
	state->an_complete = status.an_complete;
	state->speed = status.speed;
	state->duplex = status.duplex;
	state->pause = status.pause;
}

static int sparx5_pcs_config(struct phylink_pcs *pcs,
			     unsigned int mode,
			     phy_interface_t interface,
			     const unsigned long *advertising,
			     bool permit_pause_to_mac)
{
	struct sparx5_port *port = sparx5_pcs_to_port(pcs);
	struct sparx5_port_config conf;
	int ret = 0;

	conf = port->conf;
	conf.power_down = false;
	conf.portmode = interface;
	conf.inband = phylink_autoneg_inband(mode);
	conf.autoneg = phylink_test(advertising, Autoneg);
	conf.pause_adv = 0;
	if (phylink_test(advertising, Pause))
		conf.pause_adv |= ADVERTISE_1000XPAUSE;
	if (phylink_test(advertising, Asym_Pause))
		conf.pause_adv |= ADVERTISE_1000XPSE_ASYM;
	if (sparx5_is_baser(interface)) {
		if (phylink_test(advertising, FIBRE))
			conf.media = PHY_MEDIA_SR;
		else
			conf.media = PHY_MEDIA_DAC;
	}
	if (!port_conf_has_changed(&port->conf, &conf))
		return ret;
	/* Enable the PCS matching this interface type */
	ret = sparx5_port_pcs_set(port->sparx5, port, &conf);
	if (ret)
		netdev_err(port->ndev, "port PCS config failed: %d\n", ret);
	return ret;
}

static void sparx5_pcs_aneg_restart(struct phylink_pcs *pcs)
{
	/* Currently not used */
}

const struct phylink_pcs_ops sparx5_phylink_pcs_ops = {
	.pcs_get_state = sparx5_pcs_get_state,
	.pcs_config = sparx5_pcs_config,
	.pcs_an_restart = sparx5_pcs_aneg_restart,
};

const struct phylink_mac_ops sparx5_phylink_mac_ops = {
	.validate = sparx5_phylink_validate,
	.mac_config = sparx5_phylink_mac_config,
	.mac_link_down = sparx5_phylink_mac_link_down,
	.mac_link_up = sparx5_phylink_mac_link_up,
};
