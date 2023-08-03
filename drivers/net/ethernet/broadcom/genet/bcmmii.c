// SPDX-License-Identifier: GPL-2.0-only
/*
 * Broadcom GENET MDIO routines
 *
 * Copyright (c) 2014-2017 Broadcom
 */

#include <linux/acpi.h>
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
#include <linux/platform_data/mdio-bcm-unimac.h>

#include "bcmgenet.h"

/* setup netdev link state when PHY link status change and
 * update UMAC and RGMII block when link up
 */
void bcmgenet_mii_setup(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
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
		if (reg & CMD_SW_RESET) {
			reg &= ~CMD_SW_RESET;
			bcmgenet_umac_writel(priv, reg, UMAC_CMD);
			udelay(2);
			reg |= CMD_TX_EN | CMD_RX_EN;
		}
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
	struct bcmgenet_priv *priv;
	u32 reg;

	if (dev && dev->phydev && status) {
		priv = netdev_priv(dev);
		reg = bcmgenet_umac_readl(priv, UMAC_MODE);
		status->link = !!(reg & MODE_LINK_STATUS);
	}

	return 0;
}

void bcmgenet_phy_power_set(struct net_device *dev, bool enable)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	u32 reg = 0;

	/* EXT_GPHY_CTRL is only valid for GENETv4 and onward */
	if (GENET_IS_V4(priv)) {
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
			reg |= EXT_CFG_IDDQ_BIAS | EXT_CFG_PWR_DOWN |
			       EXT_GPHY_RESET;
			bcmgenet_ext_writel(priv, reg, EXT_GPHY_CTRL);
			mdelay(1);
			reg |= EXT_CK25_DIS;
		}
		bcmgenet_ext_writel(priv, reg, EXT_GPHY_CTRL);
		udelay(60);
	} else {
		mdelay(1);
	}
}

static void bcmgenet_moca_phy_setup(struct bcmgenet_priv *priv)
{
	if (priv->hw_params->flags & GENET_HAS_MOCA_LINK_DET)
		fixed_phy_set_link_update(priv->dev->phydev,
					  bcmgenet_fixed_phy_link_update);
}

int bcmgenet_mii_config(struct net_device *dev, bool init)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	struct device *kdev = &priv->pdev->dev;
	const char *phy_name = NULL;
	u32 id_mode_dis = 0;
	u32 port_ctrl;
	u32 reg;

	switch (priv->phy_interface) {
	case PHY_INTERFACE_MODE_INTERNAL:
		phy_name = "internal PHY";
		fallthrough;
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

		if (!phy_name) {
			phy_name = "MoCA";
			if (!GENET_IS_V5(priv))
				port_ctrl |= LED_ACT_SOURCE_MAC;
			bcmgenet_moca_phy_setup(priv);
		}
		break;

	case PHY_INTERFACE_MODE_MII:
		phy_name = "external MII";
		phy_set_max_speed(phydev, SPEED_100);
		port_ctrl = PORT_MODE_EXT_EPHY;
		break;

	case PHY_INTERFACE_MODE_REVMII:
		phy_name = "external RvMII";
		/* of_mdiobus_register took care of reading the 'max-speed'
		 * PHY property for us, effectively limiting the PHY supported
		 * capabilities, use that knowledge to also configure the
		 * Reverse MII interface correctly.
		 */
		if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
				      dev->phydev->supported))
			port_ctrl = PORT_MODE_EXT_RVMII_50;
		else
			port_ctrl = PORT_MODE_EXT_RVMII_25;
		break;

	case PHY_INTERFACE_MODE_RGMII:
		/* RGMII_NO_ID: TXC transitions at the same time as TXD
		 *		(requires PCB or receiver-side delay)
		 *
		 * ID is implicitly disabled for 100Mbps (RG)MII operation.
		 */
		phy_name = "external RGMII (no delay)";
		id_mode_dis = BIT(16);
		port_ctrl = PORT_MODE_EXT_GPHY;
		break;

	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* RGMII_TXID:	Add 2ns delay on TXC (90 degree shift) */
		phy_name = "external RGMII (TX delay)";
		port_ctrl = PORT_MODE_EXT_GPHY;
		break;

	case PHY_INTERFACE_MODE_RGMII_RXID:
		phy_name = "external RGMII (RX delay)";
		port_ctrl = PORT_MODE_EXT_GPHY;
		break;
	default:
		dev_err(kdev, "unknown phy mode: %d\n", priv->phy_interface);
		return -EINVAL;
	}

	bcmgenet_sys_writel(priv, port_ctrl, SYS_PORT_CTRL);

	priv->ext_phy = !priv->internal_phy &&
			(priv->phy_interface != PHY_INTERFACE_MODE_MOCA);

	/* This is an external PHY (xMII), so we need to enable the RGMII
	 * block for the interface to work
	 */
	if (priv->ext_phy) {
		reg = bcmgenet_ext_readl(priv, EXT_RGMII_OOB_CTRL);
		reg &= ~ID_MODE_DIS;
		reg |= id_mode_dis;
		if (GENET_IS_V1(priv) || GENET_IS_V2(priv) || GENET_IS_V3(priv))
			reg |= RGMII_MODE_EN_V123;
		else
			reg |= RGMII_MODE_EN;
		bcmgenet_ext_writel(priv, reg, EXT_RGMII_OOB_CTRL);
	}

	if (init)
		dev_info(kdev, "configuring instance for %s\n", phy_name);

	return 0;
}

int bcmgenet_mii_probe(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device *kdev = &priv->pdev->dev;
	struct device_node *dn = kdev->of_node;
	struct phy_device *phydev;
	u32 phy_flags = 0;
	int ret;

	/* Communicate the integrated PHY revision */
	if (priv->internal_phy)
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
		if (has_acpi_companion(kdev)) {
			char mdio_bus_id[MII_BUS_ID_SIZE];
			struct mii_bus *unimacbus;

			snprintf(mdio_bus_id, MII_BUS_ID_SIZE, "%s-%d",
				 UNIMAC_MDIO_DRV_NAME, priv->pdev->id);

			unimacbus = mdio_find_bus(mdio_bus_id);
			if (!unimacbus) {
				pr_err("Unable to find mii\n");
				return -ENODEV;
			}
			phydev = phy_find_first(unimacbus);
			put_device(&unimacbus->dev);
			if (!phydev) {
				pr_err("Unable to find PHY\n");
				return -ENODEV;
			}
		} else {
			phydev = dev->phydev;
		}
		phydev->dev_flags = phy_flags;

		ret = phy_connect_direct(dev, phydev, bcmgenet_mii_setup,
					 priv->phy_interface);
		if (ret) {
			pr_err("could not attach to PHY\n");
			return -ENODEV;
		}
	}

	/* Configure port multiplexer based on what the probed PHY device since
	 * reading the 'max-speed' property determines the maximum supported
	 * PHY speed which is needed for bcmgenet_mii_config() to configure
	 * things appropriately.
	 */
	ret = bcmgenet_mii_config(dev, true);
	if (ret) {
		phy_disconnect(dev->phydev);
		return ret;
	}

	linkmode_copy(phydev->advertising, phydev->supported);

	/* The internal PHY has its link interrupts routed to the
	 * Ethernet MAC ISRs. On GENETv5 there is a hardware issue
	 * that prevents the signaling of link UP interrupts when
	 * the link operates at 10Mbps, so fallback to polling for
	 * those versions of GENET.
	 */
	if (priv->internal_phy && !GENET_IS_V5(priv))
		dev->phydev->irq = PHY_IGNORE_INTERRUPT;

	return 0;
}

static struct device_node *bcmgenet_mii_of_find_mdio(struct bcmgenet_priv *priv)
{
	struct device_node *dn = priv->pdev->dev.of_node;
	struct device *kdev = &priv->pdev->dev;
	char *compat;

	compat = kasprintf(GFP_KERNEL, "brcm,genet-mdio-v%d", priv->version);
	if (!compat)
		return NULL;

	priv->mdio_dn = of_get_compatible_child(dn, compat);
	kfree(compat);
	if (!priv->mdio_dn) {
		dev_err(kdev, "unable to find MDIO bus node\n");
		return NULL;
	}

	return priv->mdio_dn;
}

static void bcmgenet_mii_pdata_init(struct bcmgenet_priv *priv,
				    struct unimac_mdio_pdata *ppd)
{
	struct device *kdev = &priv->pdev->dev;
	struct bcmgenet_platform_data *pd = kdev->platform_data;

	if (pd->phy_interface != PHY_INTERFACE_MODE_MOCA && pd->mdio_enabled) {
		/*
		 * Internal or external PHY with MDIO access
		 */
		if (pd->phy_address >= 0 && pd->phy_address < PHY_MAX_ADDR)
			ppd->phy_mask = 1 << pd->phy_address;
		else
			ppd->phy_mask = 0;
	}
}

static int bcmgenet_mii_wait(void *wait_func_data)
{
	struct bcmgenet_priv *priv = wait_func_data;

	wait_event_timeout(priv->wq,
			   !(bcmgenet_umac_readl(priv, UMAC_MDIO_CMD)
			   & MDIO_START_BUSY),
			   HZ / 100);
	return 0;
}

static int bcmgenet_mii_register(struct bcmgenet_priv *priv)
{
	struct platform_device *pdev = priv->pdev;
	struct bcmgenet_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *dn = pdev->dev.of_node;
	struct unimac_mdio_pdata ppd;
	struct platform_device *ppdev;
	struct resource *pres, res;
	int id, ret;

	pres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!pres) {
		dev_err(&pdev->dev, "Invalid resource\n");
		return -EINVAL;
	}
	memset(&res, 0, sizeof(res));
	memset(&ppd, 0, sizeof(ppd));

	ppd.wait_func = bcmgenet_mii_wait;
	ppd.wait_func_data = priv;
	ppd.bus_name = "bcmgenet MII bus";

	/* Unimac MDIO bus controller starts at UniMAC offset + MDIO_CMD
	 * and is 2 * 32-bits word long, 8 bytes total.
	 */
	res.start = pres->start + GENET_UMAC_OFF + UMAC_MDIO_CMD;
	res.end = res.start + 8;
	res.flags = IORESOURCE_MEM;

	if (dn)
		id = of_alias_get_id(dn, "eth");
	else
		id = pdev->id;

	ppdev = platform_device_alloc(UNIMAC_MDIO_DRV_NAME, id);
	if (!ppdev)
		return -ENOMEM;

	/* Retain this platform_device pointer for later cleanup */
	priv->mii_pdev = ppdev;
	ppdev->dev.parent = &pdev->dev;
	if (dn)
		ppdev->dev.of_node = bcmgenet_mii_of_find_mdio(priv);
	else if (pdata)
		bcmgenet_mii_pdata_init(priv, &ppd);
	else
		ppd.phy_mask = ~0;

	ret = platform_device_add_resources(ppdev, &res, 1);
	if (ret)
		goto out;

	ret = platform_device_add_data(ppdev, &ppd, sizeof(ppd));
	if (ret)
		goto out;

	ret = platform_device_add(ppdev);
	if (ret)
		goto out;

	return 0;
out:
	platform_device_put(ppdev);
	return ret;
}

static int bcmgenet_phy_interface_init(struct bcmgenet_priv *priv)
{
	struct device *kdev = &priv->pdev->dev;
	int phy_mode = device_get_phy_mode(kdev);

	if (phy_mode < 0) {
		dev_err(kdev, "invalid PHY mode property\n");
		return phy_mode;
	}

	priv->phy_interface = phy_mode;

	/* We need to specifically look up whether this PHY interface is
	 * internal or not *before* we even try to probe the PHY driver
	 * over MDIO as we may have shut down the internal PHY for power
	 * saving purposes.
	 */
	if (priv->phy_interface == PHY_INTERFACE_MODE_INTERNAL)
		priv->internal_phy = true;

	return 0;
}

static int bcmgenet_mii_of_init(struct bcmgenet_priv *priv)
{
	struct device_node *dn = priv->pdev->dev.of_node;
	struct phy_device *phydev;
	int ret;

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
	ret = bcmgenet_phy_interface_init(priv);
	if (ret)
		return ret;

	/* Make sure we initialize MoCA PHYs with a link down */
	if (priv->phy_interface == PHY_INTERFACE_MODE_MOCA) {
		phydev = of_phy_find_device(dn);
		if (phydev) {
			phydev->link = 0;
			put_device(&phydev->mdio.dev);
		}
	}

	return 0;
}

static int bcmgenet_mii_pd_init(struct bcmgenet_priv *priv)
{
	struct device *kdev = &priv->pdev->dev;
	struct bcmgenet_platform_data *pd = kdev->platform_data;
	char phy_name[MII_BUS_ID_SIZE + 3];
	char mdio_bus_id[MII_BUS_ID_SIZE];
	struct phy_device *phydev;

	snprintf(mdio_bus_id, MII_BUS_ID_SIZE, "%s-%d",
		 UNIMAC_MDIO_DRV_NAME, priv->pdev->id);

	if (pd->phy_interface != PHY_INTERFACE_MODE_MOCA && pd->mdio_enabled) {
		snprintf(phy_name, MII_BUS_ID_SIZE, PHY_ID_FMT,
			 mdio_bus_id, pd->phy_address);

		/*
		 * Internal or external PHY with MDIO access
		 */
		phydev = phy_attach(priv->dev, phy_name, pd->phy_interface);
		if (!phydev) {
			dev_err(kdev, "failed to register PHY device\n");
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

		phydev = fixed_phy_register(PHY_POLL, &fphy_status, NULL);
		if (!phydev || IS_ERR(phydev)) {
			dev_err(kdev, "failed to register fixed PHY device\n");
			return -ENODEV;
		}

		/* Make sure we initialize MoCA PHYs with a link down */
		phydev->link = 0;

	}

	priv->phy_interface = pd->phy_interface;

	return 0;
}

static int bcmgenet_mii_bus_init(struct bcmgenet_priv *priv)
{
	struct device *kdev = &priv->pdev->dev;
	struct device_node *dn = kdev->of_node;

	if (dn)
		return bcmgenet_mii_of_init(priv);
	else if (has_acpi_companion(kdev))
		return bcmgenet_phy_interface_init(priv);
	else
		return bcmgenet_mii_pd_init(priv);
}

int bcmgenet_mii_init(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	int ret;

	ret = bcmgenet_mii_register(priv);
	if (ret)
		return ret;

	ret = bcmgenet_mii_bus_init(priv);
	if (ret)
		goto out;

	return 0;

out:
	bcmgenet_mii_exit(dev);
	return ret;
}

void bcmgenet_mii_exit(struct net_device *dev)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device_node *dn = priv->pdev->dev.of_node;

	if (of_phy_is_fixed_link(dn))
		of_phy_deregister_fixed_link(dn);
	of_node_put(priv->phy_dn);
	clk_prepare_enable(priv->clk);
	platform_device_unregister(priv->mii_pdev);
	clk_disable_unprepare(priv->clk);
}
