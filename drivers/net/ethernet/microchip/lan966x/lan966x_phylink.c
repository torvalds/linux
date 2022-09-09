// SPDX-License-Identifier: GPL-2.0+

#include <linux/module.h>
#include <linux/phylink.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/phy/phy.h>
#include <linux/sfp.h>

#include "lan966x_main.h"

static struct phylink_pcs *lan966x_phylink_mac_select(struct phylink_config *config,
						      phy_interface_t interface)
{
	struct lan966x_port *port = netdev_priv(to_net_dev(config->dev));

	return &port->phylink_pcs;
}

static void lan966x_phylink_mac_config(struct phylink_config *config,
				       unsigned int mode,
				       const struct phylink_link_state *state)
{
}

static int lan966x_phylink_mac_prepare(struct phylink_config *config,
				       unsigned int mode,
				       phy_interface_t iface)
{
	struct lan966x_port *port = netdev_priv(to_net_dev(config->dev));
	int err;

	if (port->serdes) {
		err = phy_set_mode_ext(port->serdes, PHY_MODE_ETHERNET,
				       iface);
		if (err) {
			netdev_err(to_net_dev(config->dev),
				   "Could not set mode of SerDes\n");
			return err;
		}
	}

	return 0;
}

static void lan966x_phylink_mac_link_up(struct phylink_config *config,
					struct phy_device *phy,
					unsigned int mode,
					phy_interface_t interface,
					int speed, int duplex,
					bool tx_pause, bool rx_pause)
{
	struct lan966x_port *port = netdev_priv(to_net_dev(config->dev));
	struct lan966x_port_config *port_config = &port->config;

	port_config->duplex = duplex;
	port_config->speed = speed;
	port_config->pause = 0;
	port_config->pause |= tx_pause ? MLO_PAUSE_TX : 0;
	port_config->pause |= rx_pause ? MLO_PAUSE_RX : 0;

	lan966x_port_config_up(port);
}

static void lan966x_phylink_mac_link_down(struct phylink_config *config,
					  unsigned int mode,
					  phy_interface_t interface)
{
	struct lan966x_port *port = netdev_priv(to_net_dev(config->dev));
	struct lan966x *lan966x = port->lan966x;

	lan966x_port_config_down(port);

	/* Take PCS out of reset */
	lan_rmw(DEV_CLOCK_CFG_PCS_RX_RST_SET(0) |
		DEV_CLOCK_CFG_PCS_TX_RST_SET(0),
		DEV_CLOCK_CFG_PCS_RX_RST |
		DEV_CLOCK_CFG_PCS_TX_RST,
		lan966x, DEV_CLOCK_CFG(port->chip_port));
}

static struct lan966x_port *lan966x_pcs_to_port(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct lan966x_port, phylink_pcs);
}

static void lan966x_pcs_get_state(struct phylink_pcs *pcs,
				  struct phylink_link_state *state)
{
	struct lan966x_port *port = lan966x_pcs_to_port(pcs);

	lan966x_port_status_get(port, state);
}

static int lan966x_pcs_config(struct phylink_pcs *pcs,
			      unsigned int mode,
			      phy_interface_t interface,
			      const unsigned long *advertising,
			      bool permit_pause_to_mac)
{
	struct lan966x_port *port = lan966x_pcs_to_port(pcs);
	struct lan966x_port_config config;
	int ret;

	config = port->config;
	config.portmode = interface;
	config.inband = phylink_autoneg_inband(mode);
	config.autoneg = phylink_test(advertising, Autoneg);
	config.advertising = advertising;

	ret = lan966x_port_pcs_set(port, &config);
	if (ret)
		netdev_err(port->dev, "port PCS config failed: %d\n", ret);

	return ret;
}

static void lan966x_pcs_aneg_restart(struct phylink_pcs *pcs)
{
	/* Currently not used */
}

const struct phylink_mac_ops lan966x_phylink_mac_ops = {
	.validate = phylink_generic_validate,
	.mac_select_pcs = lan966x_phylink_mac_select,
	.mac_config = lan966x_phylink_mac_config,
	.mac_prepare = lan966x_phylink_mac_prepare,
	.mac_link_down = lan966x_phylink_mac_link_down,
	.mac_link_up = lan966x_phylink_mac_link_up,
};

const struct phylink_pcs_ops lan966x_phylink_pcs_ops = {
	.pcs_get_state = lan966x_pcs_get_state,
	.pcs_config = lan966x_pcs_config,
	.pcs_an_restart = lan966x_pcs_aneg_restart,
};
