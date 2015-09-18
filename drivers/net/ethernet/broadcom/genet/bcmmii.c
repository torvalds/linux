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
#include <linux/platform_data/bcmgenet.h>

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

	/* Some broken devices are known not to release the line during
	 * turn-around, e.g: Broadcom BCM53125 external switches, so check for
	 * that condition here and ignore the MDIO controller read failure
	 * indication.
	 */
	if (!(bus->phy_ignore_ta_mask & 1 << phy_id) && (ret & MDIO_READ_FAIL))
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
void bcmgenet_mii_setup(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;
	u32 reg, cmd_bits = 0;
	bool status_changed = false;

	if (priv->old_link != phydev->link) {
		status_changed = true;
		priv->old_link = phydev->link;
	}

	if (phydev->link) {
		/* check speed/duplex/pause changes */
		if (priv->old_speed != phydev->speed) {
			status_changed = true;
			priv->old_speed = phydev->speed;
		}

		if (priv->old_duplex != phydev->duplex) {
			status_changed = true;
			priv->old_duplex = phydev->duplex;
		}

		if (priv->old_pause != phydev->pause) {
			status_changed = true;
			priv->old_pause = phydev->pause;
		}

		/* done if nothing has changed */
		if (!status_changed)
			return;

		/* speed */
		if (phydev->speed == SPEED_1000)
			cmd_bits = UMAC_SPEED_1000;
		else if (phydev->speed == SPEED_100)
			cmd_bits = UMAC_SPEED_100;
		else
			cmd_bits = UMAC_SPEED_10;
		cmd_bits <<= CMD_SPEED_SHIFT;

		/* duplex */
		if (phydev->duplex != DUPLEX_FULL)
			cmd_bits |= CMD_HD_EN;

		/* pause capability */
		if (!phydev->pause)
			cmd_bits |= CMD_RX_PAUSE_IGNORE | CMD_TX_PAUSE_IGNORE;

		/*
		 * Program UMAC and RGMII block based on established
		 * link speed, duplex, and pause. The speed set in
		 * umac->cmd tell RGMII block which clock to use for
		 * transmit -- 25MHz(100Mbps) or 125MHz(1Gbps).
		 * Receive clock is provided by the PHY.
		 */
		reg = bcmgenet_ext_readl(priv, EXT_RGMII_OOB_CTRL);
		reg &= ~OOB_DISABLE;
		reg |= RGMII_LINK;
		bcmgenet_ext_writel(priv, reg, EXT_RGMII_OOB_CTRL);

		reg = bcmgenet_umac_readl(priv, UMAC_CMD);
		reg &= ~((CMD_SPEED_MASK << CMD_SPEED_SHIFT) |
			       CMD_HD_EN |
			       CMD_RX_PAUSE_IGNORE | CMD_TX_PAUSE_IGNORE);
		reg |= cmd_bits;
		bcmgenet_umac_writel(priv, reg, UMAC_CMD);
	} else {
		/* done if nothing has changed */
		if (!status_changed)
			return;

		/* needed for MoCA fixed PHY to reflect correct link status */
		netif_carrier_off(dev);
	}

	phy_print_status(phydev);
}

static int bcmgenet_fixed_phy_link_update(struct net_device *dev,
					  struct fixed_phy_status *status)
{
	if (dev && dev->phydev && status)
		status->link = dev->phydev->link;

	return 0;
}

void bcmgenet_phy_power_set(struct net_device *dev, bool enable)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 reg = 0;

	/* EXT_GPHY_CTRL is only valid for GENETv4 and onward */
	if (!GENET_IS_V4(priv))
		return;

	reg = bcmgenet_ext_readl(priv, EXT_GPHY_CTRL);
	if (enable) {
		reg &= ~EXT_CK25_DIS;
		bcmgenet_ext_writel(priv, reg, EXT_GPHY_CTRL);
		mdelay(1);

		reg &= ~(EXT_CFG_IDDQ_BIAS | EXT_CFG_PWR_DOWN);
		reg |= EXT_GPHY_RESET;
		bcmgenet_ext_writel(priv, reg, EXT_GPHY_CTRL);
		mdelay(1);

		reg &= ~EXT_GPHY_RESET;
	} else {
		reg |= EXT_CFG_IDDQ_BIAS | EXT_CFG_PWR_DOWN | EXT_GPHY_RESET;
		bcmgenet_ext_writel(priv, reg, EXT_GPHY_CTRL);
		mdelay(1);
		reg |= EXT_CK25_DIS;
	}
	bcmgenet_ext_writel(priv, reg, EXT_GPHY_CTRL);
	udelay(60);
}

static void bcmgenet_internal_phy_setup(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 reg;

	/* Power up PHY */
	bcmgenet_phy_power_set(dev, true);
	/* enable APD */
	reg = bcmgenet_ext_readl(priv, EXT_EXT_PWR_MGMT);
	reg |= EXT_PWR_DN_EN_LD;
	bcmgenet_ext_writel(priv, reg, EXT_EXT_PWR_MGMT);
}

static void bcmgenet_moca_phy_setup(struct bcmgenet_priv *priv)
{
	u32 reg;

	/* Speed settings are set in bcmgenet_mii_setup() */
	reg = bcmgenet_sys_readl(priv, SYS_PORT_CTRL);
	reg |= LED_ACT_SOURCE_MAC;
	bcmgenet_sys_writel(priv, reg, SYS_PORT_CTRL);

	if (priv->hw_params->flags & GENET_HAS_MOCA_LINK_DET)
		fixed_phy_set_link_update(priv->phydev,
					  bcmgenet_fixed_phy_link_update);
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

	priv->ext_phy = !priv->internal_phy &&
			(priv->phy_interface != PHY_INTERFACE_MODE_MOCA);

	if (priv->internal_phy)
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

		if (priv->internal_phy) {
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
		bcmgenet_sys_writel(priv,
				    PORT_MODE_EXT_GPHY, SYS_PORT_CTRL);
		break;
	default:
		dev_err(kdev, "unknown phy mode: %d\n", priv->phy_interface);
		return -EINVAL;
	}

	/* This is an external PHY (xMII), so we need to enable the RGMII
	 * block for the interface to work
	 */
	if (priv->ext_phy) {
		reg = bcmgenet_ext_readl(priv, EXT_RGMII_OOB_CTRL);
		reg |= RGMII_MODE_EN | id_mode_dis;
		bcmgenet_ext_writel(priv, reg, EXT_RGMII_OOB_CTRL);
	}

	dev_info_once(kdev, "configuring instance for %s\n", phy_name);

	return 0;
}

int bcmgenet_mii_probe(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device_node *dn = priv->pdev->dev.of_node;
	struct phy_device *phydev;
	u32 phy_flags;
	int ret;

	/* Communicate the integrated PHY revision */
	phy_flags = priv->gphy_rev;

	/* Initialize link state variables that bcmgenet_mii_setup() uses */
	priv->old_link = -1;
	priv->old_speed = -1;
	priv->old_duplex = -1;
	priv->old_pause = -1;

	if (dn) {
		phydev = of_phy_connect(dev, priv->phy_dn, bcmgenet_mii_setup,
					phy_flags, priv->phy_interface);
		if (!phydev) {
			pr_err("could not attach to PHY\n");
			return -ENODEV;
		}
	} else {
		phydev = priv->phydev;
		phydev->dev_flags = phy_flags;

		ret = phy_connect_direct(dev, phydev, bcmgenet_mii_setup,
					 priv->phy_interface);
		if (ret) {
			pr_err("could not attach to PHY\n");
			return -ENODEV;
		}
	}

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

	phydev->advertising = phydev->supported;

	/* The internal PHY has its link interrupts routed to the
	 * Ethernet MAC ISRs
	 */
	if (priv->internal_phy)
		priv->mii_bus->irq[phydev->addr] = PHY_IGNORE_INTERRUPT;
	else
		priv->mii_bus->irq[phydev->addr] = PHY_POLL;

	return 0;
}

/* Workaround for integrated BCM7xxx Gigabit PHYs which have a problem with
 * their internal MDIO management controller making them fail to successfully
 * be read from or written to for the first transaction.  We insert a dummy
 * BMSR read here to make sure that phy_get_device() and get_phy_id() can
 * correctly read the PHY MII_PHYSID1/2 registers and successfully register a
 * PHY device for this peripheral.
 *
 * Once the PHY driver is registered, we can workaround subsequent reads from
 * there (e.g: during system-wide power management).
 *
 * bus->reset is invoked before mdiobus_scan during mdiobus_register and is
 * therefore the right location to stick that workaround. Since we do not want
 * to read from non-existing PHYs, we either use bus->phy_mask or do a manual
 * Device Tree scan to limit the search area.
 */
static int bcmgenet_mii_bus_reset(struct mii_bus *bus)
{
	struct net_device *dev = bus->priv;
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device_node *np = priv->mdio_dn;
	struct device_node *child = NULL;
	u32 read_mask = 0;
	int addr = 0;

	if (!np) {
		read_mask = 1 << priv->phy_addr;
	} else {
		for_each_available_child_of_node(np, child) {
			addr = of_mdio_parse_addr(&dev->dev, child);
			if (addr < 0)
				continue;

			read_mask |= 1 << addr;
		}
	}

	for (addr = 0; addr < PHY_MAX_ADDR; addr++) {
		if (read_mask & 1 << addr) {
			dev_dbg(&dev->dev, "Workaround for PHY @ %d\n", addr);
			mdiobus_read(bus, addr, MII_BMSR);
		}
	}

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
	bus->reset = bcmgenet_mii_bus_reset;
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
	const char *phy_mode_str = NULL;
	struct phy_device *phydev = NULL;
	char *compat;
	int phy_mode;
	int ret;

	compat = kasprintf(GFP_KERNEL, "brcm,genet-mdio-v%d", priv->version);
	if (!compat)
		return -ENOMEM;

	priv->mdio_dn = of_find_compatible_node(dn, NULL, compat);
	kfree(compat);
	if (!priv->mdio_dn) {
		dev_err(kdev, "unable to find MDIO bus node\n");
		return -ENODEV;
	}

	ret = of_mdiobus_register(priv->mii_bus, priv->mdio_dn);
	if (ret) {
		dev_err(kdev, "failed to register MDIO bus\n");
		return ret;
	}

	/* Fetch the PHY phandle */
	priv->phy_dn = of_parse_phandle(dn, "phy-handle", 0);

	/* In the case of a fixed PHY, the DT node associated
	 * to the PHY is the Ethernet MAC DT node.
	 */
	if (!priv->phy_dn && of_phy_is_fixed_link(dn)) {
		ret = of_phy_register_fixed_link(dn);
		if (ret)
			return ret;

		priv->phy_dn = of_node_get(dn);
	}

	/* Get the link mode */
	phy_mode = of_get_phy_mode(dn);
	priv->phy_interface = phy_mode;

	/* We need to specifically look up whether this PHY interface is internal
	 * or not *before* we even try to probe the PHY driver over MDIO as we
	 * may have shut down the internal PHY for power saving purposes.
	 */
	if (phy_mode < 0) {
		ret = of_property_read_string(dn, "phy-mode", &phy_mode_str);
		if (ret < 0) {
			dev_err(kdev, "invalid PHY mode property\n");
			return ret;
		}

		priv->phy_interface = PHY_INTERFACE_MODE_NA;
		if (!strcasecmp(phy_mode_str, "internal"))
			priv->internal_phy = true;
	}

	/* Make sure we initialize MoCA PHYs with a link down */
	if (phy_mode == PHY_INTERFACE_MODE_MOCA) {
		phydev = of_phy_find_device(dn);
		if (phydev)
			phydev->link = 0;
	}

	return 0;
}

static int bcmgenet_mii_pd_init(struct bcmgenet_priv *priv)
{
	struct device *kdev = &priv->pdev->dev;
	struct bcmgenet_platform_data *pd = kdev->platform_data;
	struct mii_bus *mdio = priv->mii_bus;
	struct phy_device *phydev;
	int ret;

	if (pd->phy_interface != PHY_INTERFACE_MODE_MOCA && pd->mdio_enabled) {
		/*
		 * Internal or external PHY with MDIO access
		 */
		if (pd->phy_address >= 0 && pd->phy_address < PHY_MAX_ADDR)
			mdio->phy_mask = ~(1 << pd->phy_address);
		else
			mdio->phy_mask = 0;

		ret = mdiobus_register(mdio);
		if (ret) {
			dev_err(kdev, "failed to register MDIO bus\n");
			return ret;
		}

		if (pd->phy_address >= 0 && pd->phy_address < PHY_MAX_ADDR)
			phydev = mdio->phy_map[pd->phy_address];
		else
			phydev = phy_find_first(mdio);

		if (!phydev) {
			dev_err(kdev, "failed to register PHY device\n");
			mdiobus_unregister(mdio);
			return -ENODEV;
		}
	} else {
		/*
		 * MoCA port or no MDIO access.
		 * Use fixed PHY to represent the link layer.
		 */
		struct fixed_phy_status fphy_status = {
			.link = 1,
			.speed = pd->phy_speed,
			.duplex = pd->phy_duplex,
			.pause = 0,
			.asym_pause = 0,
		};

		phydev = fixed_phy_register(PHY_POLL, &fphy_status, -1, NULL);
		if (!phydev || IS_ERR(phydev)) {
			dev_err(kdev, "failed to register fixed PHY device\n");
			return -ENODEV;
		}

		/* Make sure we initialize MoCA PHYs with a link down */
		phydev->link = 0;

	}

	priv->phydev = phydev;
	priv->phy_interface = pd->phy_interface;

	return 0;
}

static int bcmgenet_mii_bus_init(struct bcmgenet_priv *priv)
{
	struct device_node *dn = priv->pdev->dev.of_node;

	if (dn)
		return bcmgenet_mii_of_init(priv);
	else
		return bcmgenet_mii_pd_init(priv);
}

int bcmgenet_mii_init(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int ret;

	ret = bcmgenet_mii_alloc(priv);
	if (ret)
		return ret;

	ret = bcmgenet_mii_bus_init(priv);
	if (ret)
		goto out;

	return 0;

out:
	of_node_put(priv->phy_dn);
	mdiobus_unregister(priv->mii_bus);
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
