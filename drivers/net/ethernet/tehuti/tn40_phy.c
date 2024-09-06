// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) Tehuti Networks Ltd. */

#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/phylink.h>

#include "tn40.h"

static struct tn40_priv *tn40_config_to_priv(struct phylink_config *config)
{
	return container_of(config, struct tn40_priv, phylink_config);
}

static void tn40_link_up(struct phylink_config *config, struct phy_device *phy,
			 unsigned int mode, phy_interface_t interface,
			 int speed, int duplex, bool tx_pause, bool rx_pause)
{
	struct tn40_priv *priv = tn40_config_to_priv(config);

	tn40_set_link_speed(priv, speed);
	netif_wake_queue(priv->ndev);
}

static void tn40_link_down(struct phylink_config *config, unsigned int mode,
			   phy_interface_t interface)
{
	struct tn40_priv *priv = tn40_config_to_priv(config);

	netif_stop_queue(priv->ndev);
	tn40_set_link_speed(priv, 0);
}

static void tn40_mac_config(struct phylink_config *config, unsigned int mode,
			    const struct phylink_link_state *state)
{
}

static const struct phylink_mac_ops tn40_mac_ops = {
	.mac_config = tn40_mac_config,
	.mac_link_up = tn40_link_up,
	.mac_link_down = tn40_link_down,
};

int tn40_phy_register(struct tn40_priv *priv)
{
	struct phylink_config *config;
	struct phy_device *phydev;
	struct phylink *phylink;

	phydev = phy_find_first(priv->mdio);
	if (!phydev) {
		dev_err(&priv->pdev->dev, "PHY isn't found\n");
		return -ENODEV;
	}

	config = &priv->phylink_config;
	config->dev = &priv->ndev->dev;
	config->type = PHYLINK_NETDEV;
	config->mac_capabilities = MAC_10000FD;
	__set_bit(PHY_INTERFACE_MODE_XAUI, config->supported_interfaces);

	phylink = phylink_create(config, NULL, PHY_INTERFACE_MODE_XAUI,
				 &tn40_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	priv->phydev = phydev;
	priv->phylink = phylink;
	return 0;
}

void tn40_phy_unregister(struct tn40_priv *priv)
{
	phylink_destroy(priv->phylink);
}
