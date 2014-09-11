/*
 * Broadcom GENET MDIO routines
 *
 * Copyright (c) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/types.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/bitops.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/brcmphy.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>

#include "bcmgenet.h"

/* read a value from the MII */
static int bcmgenet_mii_read(struct mii_bus *bus, int phy_id, int location)
{
	int ret;
	struct net_device *dev = bus->priv;
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 reg;

	bcmgenet_umac_writel(priv, (MDIO_RD | (phy_id << MDIO_PMD_SHIFT) |
			     (location << MDIO_REG_SHIFT)), UMAC_MDIO_CMD);
	/* Start MDIO transaction*/
	reg = bcmgenet_umac_readl(priv, UMAC_MDIO_CMD);
	reg |= MDIO_START_BUSY;
	bcmgenet_umac_writel(priv, reg, UMAC_MDIO_CMD);
	wait_event_timeout(priv->wq,
			   !(bcmgenet_umac_readl(priv, UMAC_MDIO_CMD)
			   & MDIO_START_BUSY),
			   HZ / 100);
	ret = bcmgenet_umac_readl(priv, UMAC_MDIO_CMD);

	if (ret & MDIO_READ_FAIL)
		return -EIO;

	return ret & 0xffff;
}

/* write a value to the MII */
static int bcmgenet_mii_write(struct mii_bus *bus, int phy_id,
			      int location, u16 val)
{
	struct net_device *dev = bus->priv;
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 reg;

	bcmgenet_umac_writel(priv, (MDIO_WR | (phy_id << MDIO_PMD_SHIFT) |
			     (location << MDIO_REG_SHIFT) | (0xffff & val)),
			     UMAC_MDIO_CMD);
	reg = bcmgenet_umac_readl(priv, UMAC_MDIO_CMD);
	reg |= MDIO_START_BUSY;
	bcmgenet_umac_writel(priv, reg, UMAC_MDIO_CMD);
	wait_event_timeout(priv->wq,
			   !(bcmgenet_umac_readl(priv, UMAC_MDIO_CMD) &
			   MDIO_START_BUSY),
			   HZ / 100);

	return 0;
}

/* setup netdev link state when PHY link status change and
 * update UMAC and RGMII block when link up
 */
static void bcmgenet_mii_setup(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;
	u32 reg, cmd_bits = 0;
	unsigned int status_changed = 0;

	if (priv->old_link != phydev->link) {
		status_changed = 1;
		priv->old_link = phydev->link;
	}

	if (phydev->link) {
		/* program UMAC and RGMII block based on established link
		 * speed, pause, and duplex.
		 * the speed set in umac->cmd tell RGMII block which clock
		 * 25MHz(100Mbps)/125MHz(1Gbps) to use for transmit.
		 * receive clock is provided by PHY.
		 */
		reg = bcmgenet_ext_readl(priv, EXT_RGMII_OOB_CTRL);
		reg &= ~OOB_DISABLE;
		reg |= RGMII_LINK;
		bcmgenet_ext_writel(priv, reg, EXT_RGMII_OOB_CTRL);

		/* speed */
		if (phydev->speed == SPEED_1000)
			cmd_bits = UMAC_SPEED_1000;
		else if (phydev->speed == SPEED_100)
			cmd_bits = UMAC_SPEED_100;
		else
			cmd_bits = UMAC_SPEED_10;
		cmd_bits <<= CMD_SPEED_SHIFT;

		if (priv->old_duplex != phydev->duplex) {
			status_changed = 1;
			priv->old_duplex = phydev->duplex;
		}

		/* duplex */
		if (phydev->duplex != DUPLEX_FULL)
			cmd_bits |= CMD_HD_EN;

		if (priv->old_pause != phydev->pause) {
			status_changed = 1;
			priv->old_pause = phydev->pause;
		}

		/* pause capability */
		if (!phydev->pause)
			cmd_bits |= CMD_RX_PAUSE_IGNORE | CMD_TX_PAUSE_IGNORE;
	}

	if (!status_changed)
		return;

	if (phydev->link) {
		reg = bcmgenet_umac_readl(priv, UMAC_CMD);
		reg &= ~((CMD_SPEED_MASK << CMD_SPEED_SHIFT) |
			       CMD_HD_EN |
			       CMD_RX_PAUSE_IGNORE | CMD_TX_PAUSE_IGNORE);
		reg |= cmd_bits;
		bcmgenet_umac_writel(priv, reg, UMAC_CMD);

	}

	phy_print_status(phydev);
}

void bcmgenet_mii_reset(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	if (priv->phydev) {
		phy_init_hw(priv->phydev);
		phy_start_aneg(priv->phydev);
	}
}

static void bcmgenet_ephy_power_up(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 reg = 0;

	/* EXT_GPHY_CTRL is only valid for GENETv4 and onward */
	if (!GENET_IS_V4(priv))
		return;

	reg = bcmgenet_ext_readl(priv, EXT_GPHY_CTRL);
	reg &= ~(EXT_CFG_IDDQ_BIAS | EXT_CFG_PWR_DOWN);
	reg |= EXT_GPHY_RESET;
	bcmgenet_ext_writel(priv, reg, EXT_GPHY_CTRL);
	mdelay(2);

	reg &= ~EXT_GPHY_RESET;
	bcmgenet_ext_writel(priv, reg, EXT_GPHY_CTRL);
	udelay(20);
}

static void bcmgenet_internal_phy_setup(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 reg;

	/* Power up EPHY */
	bcmgenet_ephy_power_up(dev);
	/* enable APD */
	reg = bcmgenet_ext_readl(priv, EXT_EXT_PWR_MGMT);
	reg |= EXT_PWR_DN_EN_LD;
	bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);
	bcmgenet_mii_reset(dev);
}

static void bcmgenet_moca_phy_setup(struct bcmgenet_priv *priv)
{
	u32 reg;

	/* Speed settings are set in bcmgenet_mii_setup() */
	reg = bcmgenet_sys_readl(priv, SYS_PORT_CTRL);
	reg |= LED_ACT_SOURCE_MAC;
	bcmgenet_sys_writel(priv, reg, SYS_PORT_CTRL);
}

int bcmgenet_mii_config(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;
	struct device *kdev = &priv->pdev->dev;
	const char *phy_name = NULL;
	u32 id_mode_dis = 0;
	u32 port_ctrl;
	u32 reg;

	priv->ext_phy = !phy_is_internal(priv->phydev) &&
			(priv->phy_interface != PHY_INTERFACE_MODE_MOCA);

	if (phy_is_internal(priv->phydev))
		priv->phy_interface = PHY_INTERFACE_MODE_NA;

	switch (priv->phy_interface) {
	case PHY_INTERFACE_MODE_NA:
	case PHY_INTERFACE_MODE_MOCA:
		/* Irrespective of the actually configured PHY speed (100 or
		 * 1000) GENETv4 only has an internal GPHY so we will just end
		 * up masking the Gigabit features from what we support, not
		 * switching to the EPHY
		 */
		if (GENET_IS_V4(priv))
			port_ctrl = PORT_MODE_INT_GPHY;
		else
			port_ctrl = PORT_MODE_INT_EPHY;

		bcmgenet_sys_writel(priv, port_ctrl, SYS_PORT_CTRL);

		if (phy_is_internal(priv->phydev)) {
			phy_name = "internal PHY";
			bcmgenet_internal_phy_setup(dev);
		} else if (priv->phy_interface == PHY_INTERFACE_MODE_MOCA) {
			phy_name = "MoCA";
			bcmgenet_moca_phy_setup(priv);
		}
		break;

	case PHY_INTERFACE_MODE_MII:
		phy_name = "external MII";
		phydev->supported &= PHY_BASIC_FEATURES;
		bcmgenet_sys_writel(priv,
				    PORT_MODE_EXT_EPHY, SYS_PORT_CTRL);
		break;

	case PHY_INTERFACE_MODE_REVMII:
		phy_name = "external RvMII";
		/* of_mdiobus_register took care of reading the 'max-speed'
		 * PHY property for us, effectively limiting the PHY supported
		 * capabilities, use that knowledge to also configure the
		 * Reverse MII interface correctly.
		 */
		if ((priv->phydev->supported & PHY_BASIC_FEATURES) ==
				PHY_BASIC_FEATURES)
			port_ctrl = PORT_MODE_EXT_RVMII_25;
		else
			port_ctrl = PORT_MODE_EXT_RVMII_50;
		bcmgenet_sys_writel(priv, port_ctrl, SYS_PORT_CTRL);
		break;

	case PHY_INTERFACE_MODE_RGMII:
		/* RGMII_NO_ID: TXC transitions at the same time as TXD
		 *		(requires PCB or receiver-side delay)
		 * RGMII:	Add 2ns delay on TXC (90 degree shift)
		 *
		 * ID is implicitly disabled for 100Mbps (RG)MII operation.
		 */
		id_mode_dis = BIT(16);
		/* fall through */
	case PHY_INTERFACE_MODE_RGMII_TXID:
		if (id_mode_dis)
			phy_name = "external RGMII (no delay)";
		else
			phy_name = "external RGMII (TX delay)";
		reg = bcmgenet_ext_readl(priv, EXT_RGMII_OOB_CTRL);
		reg |= RGMII_MODE_EN | id_mode_dis;
		bcmgenet_ext_writel(priv, reg, EXT_RGMII_OOB_CTRL);
		bcmgenet_sys_writel(priv,
				    PORT_MODE_EXT_GPHY, SYS_PORT_CTRL);
		break;
	default:
		dev_err(kdev, "unknown phy mode: %d\n", priv->phy_interface);
		return -EINVAL;
	}

	dev_info(kdev, "configuring instance for %s\n", phy_name);

	return 0;
}

static int bcmgenet_mii_probe(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device_node *dn = priv->pdev->dev.of_node;
	struct phy_device *phydev;
	unsigned int phy_flags;
	int ret;

	if (priv->phydev) {
		pr_info("PHY already attached\n");
		return 0;
	}

	/* In the case of a fixed PHY, the DT node associated
	 * to the PHY is the Ethernet MAC DT node.
	 */
	if (!priv->phy_dn && of_phy_is_fixed_link(dn)) {
		ret = of_phy_register_fixed_link(dn);
		if (ret)
			return ret;

		priv->phy_dn = of_node_get(dn);
	}

	phydev = of_phy_connect(dev, priv->phy_dn, bcmgenet_mii_setup, 0,
				priv->phy_interface);
	if (!phydev) {
		pr_err("could not attach to PHY\n");
		return -ENODEV;
	}

	priv->old_link = -1;
	priv->old_duplex = -1;
	priv->old_pause = -1;
	priv->phydev = phydev;

	/* Configure port multiplexer based on what the probed PHY device since
	 * reading the 'max-speed' property determines the maximum supported
	 * PHY speed which is needed for bcmgenet_mii_config() to configure
	 * things appropriately.
	 */
	ret = bcmgenet_mii_config(dev);
	if (ret) {
		phy_disconnect(priv->phydev);
		return ret;
	}

	phy_flags = PHY_BRCM_100MBPS_WAR;

	/* workarounds are only needed for 100Mpbs PHYs, and
	 * never on GENET V1 hardware
	 */
	if ((phydev->supported & PHY_GBIT_FEATURES) || GENET_IS_V1(priv))
		phy_flags = 0;

	phydev->dev_flags |= phy_flags;
	phydev->advertising = phydev->supported;

	/* The internal PHY has its link interrupts routed to the
	 * Ethernet MAC ISRs
	 */
	if (phy_is_internal(priv->phydev))
		priv->mii_bus->irq[phydev->addr] = PHY_IGNORE_INTERRUPT;
	else
		priv->mii_bus->irq[phydev->addr] = PHY_POLL;

	pr_info("attached PHY at address %d [%s]\n",
		phydev->addr, phydev->drv->name);

	return 0;
}

static int bcmgenet_mii_alloc(struct bcmgenet_priv *priv)
{
	struct mii_bus *bus;

	if (priv->mii_bus)
		return 0;

	priv->mii_bus = mdiobus_alloc();
	if (!priv->mii_bus) {
		pr_err("failed to allocate\n");
		return -ENOMEM;
	}

	bus = priv->mii_bus;
	bus->priv = priv->dev;
	bus->name = "bcmgenet MII bus";
	bus->parent = &priv->pdev->dev;
	bus->read = bcmgenet_mii_read;
	bus->write = bcmgenet_mii_write;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-%d",
		 priv->pdev->name, priv->pdev->id);

	bus->irq = kcalloc(PHY_MAX_ADDR, sizeof(int), GFP_KERNEL);
	if (!bus->irq) {
		mdiobus_free(priv->mii_bus);
		return -ENOMEM;
	}

	return 0;
}

static int bcmgenet_mii_of_init(struct bcmgenet_priv *priv)
{
	struct device_node *dn = priv->pdev->dev.of_node;
	struct device *kdev = &priv->pdev->dev;
	struct device_node *mdio_dn;
	char *compat;
	int ret;

	compat = kasprintf(GFP_KERNEL, "brcm,genet-mdio-v%d", priv->version);
	if (!compat)
		return -ENOMEM;

	mdio_dn = of_find_compatible_node(dn, NULL, compat);
	kfree(compat);
	if (!mdio_dn) {
		dev_err(kdev, "unable to find MDIO bus node\n");
		return -ENODEV;
	}

	ret = of_mdiobus_register(priv->mii_bus, mdio_dn);
	if (ret) {
		dev_err(kdev, "failed to register MDIO bus\n");
		return ret;
	}

	/* Fetch the PHY phandle */
	priv->phy_dn = of_parse_phandle(dn, "phy-handle", 0);

	/* Get the link mode */
	priv->phy_interface = of_get_phy_mode(dn);

	return 0;
}

int bcmgenet_mii_init(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int ret;

	ret = bcmgenet_mii_alloc(priv);
	if (ret)
		return ret;

	ret = bcmgenet_mii_of_init(priv);
	if (ret)
		goto out_free;

	ret = bcmgenet_mii_probe(dev);
	if (ret)
		goto out;

	return 0;

out:
	of_node_put(priv->phy_dn);
	mdiobus_unregister(priv->mii_bus);
out_free:
	kfree(priv->mii_bus->irq);
	mdiobus_free(priv->mii_bus);
	return ret;
}

void bcmgenet_mii_exit(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);

	of_node_put(priv->phy_dn);
	mdiobus_unregister(priv->mii_bus);
	kfree(priv->mii_bus->irq);
	mdiobus_free(priv->mii_bus);
}
