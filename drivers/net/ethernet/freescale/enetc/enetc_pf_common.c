// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2024 NXP */

#include <linux/fsl/enetc_mdio.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/pcs-lynx.h>

#include "enetc_pf_common.h"

int enetc_pf_set_mac_addr(struct net_device *ndev, void *addr)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct sockaddr *saddr = addr;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(ndev, saddr->sa_data);
	enetc_pf_set_primary_mac_addr(&priv->si->hw, 0, saddr->sa_data);

	return 0;
}

static int enetc_setup_mac_address(struct device_node *np, struct enetc_pf *pf,
				   int si)
{
	struct device *dev = &pf->si->pdev->dev;
	struct enetc_hw *hw = &pf->si->hw;
	u8 mac_addr[ETH_ALEN] = { 0 };
	int err;

	/* (1) try to get the MAC address from the device tree */
	if (np) {
		err = of_get_mac_address(np, mac_addr);
		if (err == -EPROBE_DEFER)
			return err;
	}

	/* (2) bootloader supplied MAC address */
	if (is_zero_ether_addr(mac_addr))
		enetc_pf_get_primary_mac_addr(hw, si, mac_addr);

	/* (3) choose a random one */
	if (is_zero_ether_addr(mac_addr)) {
		eth_random_addr(mac_addr);
		dev_info(dev, "no MAC address specified for SI%d, using %pM\n",
			 si, mac_addr);
	}

	enetc_pf_set_primary_mac_addr(hw, si, mac_addr);

	return 0;
}

int enetc_setup_mac_addresses(struct device_node *np, struct enetc_pf *pf)
{
	int err, i;

	/* The PF might take its MAC from the device tree */
	err = enetc_setup_mac_address(np, pf, 0);
	if (err)
		return err;

	for (i = 0; i < pf->total_vfs; i++) {
		err = enetc_setup_mac_address(NULL, pf, i + 1);
		if (err)
			return err;
	}

	return 0;
}

void enetc_pf_netdev_setup(struct enetc_si *si, struct net_device *ndev,
			   const struct net_device_ops *ndev_ops)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	SET_NETDEV_DEV(ndev, &si->pdev->dev);
	priv->ndev = ndev;
	priv->si = si;
	priv->dev = &si->pdev->dev;
	si->ndev = ndev;

	priv->msg_enable = (NETIF_MSG_WOL << 1) - 1;
	ndev->netdev_ops = ndev_ops;
	enetc_set_ethtool_ops(ndev);
	ndev->watchdog_timeo = 5 * HZ;
	ndev->max_mtu = ENETC_MAX_MTU;

	ndev->hw_features = NETIF_F_SG | NETIF_F_RXCSUM |
			    NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
			    NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_LOOPBACK |
			    NETIF_F_HW_CSUM | NETIF_F_TSO | NETIF_F_TSO6;
	ndev->features = NETIF_F_HIGHDMA | NETIF_F_SG | NETIF_F_RXCSUM |
			 NETIF_F_HW_VLAN_CTAG_TX |
			 NETIF_F_HW_VLAN_CTAG_RX |
			 NETIF_F_HW_CSUM | NETIF_F_TSO | NETIF_F_TSO6;
	ndev->vlan_features = NETIF_F_SG | NETIF_F_HW_CSUM |
			      NETIF_F_TSO | NETIF_F_TSO6;

	if (si->num_rss)
		ndev->hw_features |= NETIF_F_RXHASH;

	ndev->priv_flags |= IFF_UNICAST_FLT;
	ndev->xdp_features = NETDEV_XDP_ACT_BASIC | NETDEV_XDP_ACT_REDIRECT |
			     NETDEV_XDP_ACT_NDO_XMIT | NETDEV_XDP_ACT_RX_SG |
			     NETDEV_XDP_ACT_NDO_XMIT_SG;

	if (si->hw_features & ENETC_SI_F_PSFP && !enetc_psfp_enable(priv)) {
		priv->active_offloads |= ENETC_F_QCI;
		ndev->features |= NETIF_F_HW_TC;
		ndev->hw_features |= NETIF_F_HW_TC;
	}

	/* pick up primary MAC address from SI */
	enetc_load_primary_mac_addr(&si->hw, ndev);
}

static int enetc_mdio_probe(struct enetc_pf *pf, struct device_node *np)
{
	struct device *dev = &pf->si->pdev->dev;
	struct enetc_mdio_priv *mdio_priv;
	struct mii_bus *bus;
	int err;

	bus = devm_mdiobus_alloc_size(dev, sizeof(*mdio_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "Freescale ENETC MDIO Bus";
	bus->read = enetc_mdio_read_c22;
	bus->write = enetc_mdio_write_c22;
	bus->read_c45 = enetc_mdio_read_c45;
	bus->write_c45 = enetc_mdio_write_c45;
	bus->parent = dev;
	mdio_priv = bus->priv;
	mdio_priv->hw = &pf->si->hw;
	mdio_priv->mdio_base = ENETC_EMDIO_BASE;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	err = of_mdiobus_register(bus, np);
	if (err)
		return dev_err_probe(dev, err, "cannot register MDIO bus\n");

	pf->mdio = bus;

	return 0;
}

static void enetc_mdio_remove(struct enetc_pf *pf)
{
	if (pf->mdio)
		mdiobus_unregister(pf->mdio);
}

static int enetc_imdio_create(struct enetc_pf *pf)
{
	struct device *dev = &pf->si->pdev->dev;
	struct enetc_mdio_priv *mdio_priv;
	struct phylink_pcs *phylink_pcs;
	struct mii_bus *bus;
	int err;

	bus = mdiobus_alloc_size(sizeof(*mdio_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "Freescale ENETC internal MDIO Bus";
	bus->read = enetc_mdio_read_c22;
	bus->write = enetc_mdio_write_c22;
	bus->read_c45 = enetc_mdio_read_c45;
	bus->write_c45 = enetc_mdio_write_c45;
	bus->parent = dev;
	bus->phy_mask = ~0;
	mdio_priv = bus->priv;
	mdio_priv->hw = &pf->si->hw;
	mdio_priv->mdio_base = ENETC_PM_IMDIO_BASE;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-imdio", dev_name(dev));

	err = mdiobus_register(bus);
	if (err) {
		dev_err(dev, "cannot register internal MDIO bus (%d)\n", err);
		goto free_mdio_bus;
	}

	phylink_pcs = lynx_pcs_create_mdiodev(bus, 0);
	if (IS_ERR(phylink_pcs)) {
		err = PTR_ERR(phylink_pcs);
		dev_err(dev, "cannot create lynx pcs (%d)\n", err);
		goto unregister_mdiobus;
	}

	pf->imdio = bus;
	pf->pcs = phylink_pcs;

	return 0;

unregister_mdiobus:
	mdiobus_unregister(bus);
free_mdio_bus:
	mdiobus_free(bus);
	return err;
}

static void enetc_imdio_remove(struct enetc_pf *pf)
{
	if (pf->pcs)
		lynx_pcs_destroy(pf->pcs);

	if (pf->imdio) {
		mdiobus_unregister(pf->imdio);
		mdiobus_free(pf->imdio);
	}
}

static bool enetc_port_has_pcs(struct enetc_pf *pf)
{
	return (pf->if_mode == PHY_INTERFACE_MODE_SGMII ||
		pf->if_mode == PHY_INTERFACE_MODE_1000BASEX ||
		pf->if_mode == PHY_INTERFACE_MODE_2500BASEX ||
		pf->if_mode == PHY_INTERFACE_MODE_USXGMII);
}

int enetc_mdiobus_create(struct enetc_pf *pf, struct device_node *node)
{
	struct device_node *mdio_np;
	int err;

	mdio_np = of_get_child_by_name(node, "mdio");
	if (mdio_np) {
		err = enetc_mdio_probe(pf, mdio_np);

		of_node_put(mdio_np);
		if (err)
			return err;
	}

	if (enetc_port_has_pcs(pf)) {
		err = enetc_imdio_create(pf);
		if (err) {
			enetc_mdio_remove(pf);
			return err;
		}
	}

	return 0;
}

void enetc_mdiobus_destroy(struct enetc_pf *pf)
{
	enetc_mdio_remove(pf);
	enetc_imdio_remove(pf);
}

int enetc_phylink_create(struct enetc_ndev_priv *priv, struct device_node *node,
			 const struct phylink_mac_ops *ops)
{
	struct enetc_pf *pf = enetc_si_priv(priv->si);
	struct phylink *phylink;
	int err;

	pf->phylink_config.dev = &priv->ndev->dev;
	pf->phylink_config.type = PHYLINK_NETDEV;
	pf->phylink_config.mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
		MAC_10 | MAC_100 | MAC_1000 | MAC_2500FD;

	__set_bit(PHY_INTERFACE_MODE_INTERNAL,
		  pf->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_SGMII,
		  pf->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX,
		  pf->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX,
		  pf->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_USXGMII,
		  pf->phylink_config.supported_interfaces);
	phy_interface_set_rgmii(pf->phylink_config.supported_interfaces);

	phylink = phylink_create(&pf->phylink_config, of_fwnode_handle(node),
				 pf->if_mode, ops);
	if (IS_ERR(phylink)) {
		err = PTR_ERR(phylink);
		return err;
	}

	priv->phylink = phylink;

	return 0;
}

void enetc_phylink_destroy(struct enetc_ndev_priv *priv)
{
	phylink_destroy(priv->phylink);
}
