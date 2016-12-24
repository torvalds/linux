/* Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Qualcomm Technologies, Inc. EMAC PHY Controller driver.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/iopoll.h>
#include <linux/acpi.h>
#include "emac.h"
#include "emac-mac.h"
#include "emac-phy.h"
#include "emac-sgmii.h"

/* EMAC base register offsets */
#define EMAC_MDIO_CTRL                                        0x001414
#define EMAC_PHY_STS                                          0x001418
#define EMAC_MDIO_EX_CTRL                                     0x001440

/* EMAC_MDIO_CTRL */
#define MDIO_MODE                                              BIT(30)
#define MDIO_PR                                                BIT(29)
#define MDIO_AP_EN                                             BIT(28)
#define MDIO_BUSY                                              BIT(27)
#define MDIO_CLK_SEL_BMSK                                    0x7000000
#define MDIO_CLK_SEL_SHFT                                           24
#define MDIO_START                                             BIT(23)
#define SUP_PREAMBLE                                           BIT(22)
#define MDIO_RD_NWR                                            BIT(21)
#define MDIO_REG_ADDR_BMSK                                    0x1f0000
#define MDIO_REG_ADDR_SHFT                                          16
#define MDIO_DATA_BMSK                                          0xffff
#define MDIO_DATA_SHFT                                               0

/* EMAC_PHY_STS */
#define PHY_ADDR_BMSK                                         0x1f0000
#define PHY_ADDR_SHFT                                               16

#define MDIO_CLK_25_4                                                0
#define MDIO_CLK_25_28                                               7

#define MDIO_WAIT_TIMES                                           1000

#define EMAC_LINK_SPEED_DEFAULT (\
		EMAC_LINK_SPEED_10_HALF  |\
		EMAC_LINK_SPEED_10_FULL  |\
		EMAC_LINK_SPEED_100_HALF |\
		EMAC_LINK_SPEED_100_FULL |\
		EMAC_LINK_SPEED_1GB_FULL)

/**
 * emac_phy_mdio_autopoll_disable() - disable mdio autopoll
 * @adpt: the emac adapter
 *
 * The autopoll feature takes over the MDIO bus.  In order for
 * the PHY driver to be able to talk to the PHY over the MDIO
 * bus, we need to temporarily disable the autopoll feature.
 */
static int emac_phy_mdio_autopoll_disable(struct emac_adapter *adpt)
{
	u32 val;

	/* disable autopoll */
	emac_reg_update32(adpt->base + EMAC_MDIO_CTRL, MDIO_AP_EN, 0);

	/* wait for any mdio polling to complete */
	if (!readl_poll_timeout(adpt->base + EMAC_MDIO_CTRL, val,
				!(val & MDIO_BUSY), 100, MDIO_WAIT_TIMES * 100))
		return 0;

	/* failed to disable; ensure it is enabled before returning */
	emac_reg_update32(adpt->base + EMAC_MDIO_CTRL, 0, MDIO_AP_EN);

	return -EBUSY;
}

/**
 * emac_phy_mdio_autopoll_disable() - disable mdio autopoll
 * @adpt: the emac adapter
 *
 * The EMAC has the ability to poll the external PHY on the MDIO
 * bus for link state changes.  This eliminates the need for the
 * driver to poll the phy.  If if the link state does change,
 * the EMAC issues an interrupt on behalf of the PHY.
 */
static void emac_phy_mdio_autopoll_enable(struct emac_adapter *adpt)
{
	emac_reg_update32(adpt->base + EMAC_MDIO_CTRL, 0, MDIO_AP_EN);
}

static int emac_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	struct emac_adapter *adpt = bus->priv;
	u32 reg;
	int ret;

	ret = emac_phy_mdio_autopoll_disable(adpt);
	if (ret)
		return ret;

	emac_reg_update32(adpt->base + EMAC_PHY_STS, PHY_ADDR_BMSK,
			  (addr << PHY_ADDR_SHFT));

	reg = SUP_PREAMBLE |
	      ((MDIO_CLK_25_4 << MDIO_CLK_SEL_SHFT) & MDIO_CLK_SEL_BMSK) |
	      ((regnum << MDIO_REG_ADDR_SHFT) & MDIO_REG_ADDR_BMSK) |
	      MDIO_START | MDIO_RD_NWR;

	writel(reg, adpt->base + EMAC_MDIO_CTRL);

	if (readl_poll_timeout(adpt->base + EMAC_MDIO_CTRL, reg,
			       !(reg & (MDIO_START | MDIO_BUSY)),
			       100, MDIO_WAIT_TIMES * 100))
		ret = -EIO;
	else
		ret = (reg >> MDIO_DATA_SHFT) & MDIO_DATA_BMSK;

	emac_phy_mdio_autopoll_enable(adpt);

	return ret;
}

static int emac_mdio_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	struct emac_adapter *adpt = bus->priv;
	u32 reg;
	int ret;

	ret = emac_phy_mdio_autopoll_disable(adpt);
	if (ret)
		return ret;

	emac_reg_update32(adpt->base + EMAC_PHY_STS, PHY_ADDR_BMSK,
			  (addr << PHY_ADDR_SHFT));

	reg = SUP_PREAMBLE |
		((MDIO_CLK_25_4 << MDIO_CLK_SEL_SHFT) & MDIO_CLK_SEL_BMSK) |
		((regnum << MDIO_REG_ADDR_SHFT) & MDIO_REG_ADDR_BMSK) |
		((val << MDIO_DATA_SHFT) & MDIO_DATA_BMSK) |
		MDIO_START;

	writel(reg, adpt->base + EMAC_MDIO_CTRL);

	if (readl_poll_timeout(adpt->base + EMAC_MDIO_CTRL, reg,
			       !(reg & (MDIO_START | MDIO_BUSY)), 100,
			       MDIO_WAIT_TIMES * 100))
		ret = -EIO;

	emac_phy_mdio_autopoll_enable(adpt);

	return ret;
}

/* Configure the MDIO bus and connect the external PHY */
int emac_phy_config(struct platform_device *pdev, struct emac_adapter *adpt)
{
	struct device_node *np = pdev->dev.of_node;
	struct mii_bus *mii_bus;
	int ret;

	/* Create the mii_bus object for talking to the MDIO bus */
	adpt->mii_bus = mii_bus = devm_mdiobus_alloc(&pdev->dev);
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "emac-mdio";
	snprintf(mii_bus->id, MII_BUS_ID_SIZE, "%s", pdev->name);
	mii_bus->read = emac_mdio_read;
	mii_bus->write = emac_mdio_write;
	mii_bus->parent = &pdev->dev;
	mii_bus->priv = adpt;

	if (has_acpi_companion(&pdev->dev)) {
		u32 phy_addr;

		ret = mdiobus_register(mii_bus);
		if (ret) {
			dev_err(&pdev->dev, "could not register mdio bus\n");
			return ret;
		}
		ret = device_property_read_u32(&pdev->dev, "phy-channel",
					       &phy_addr);
		if (ret)
			/* If we can't read a valid phy address, then assume
			 * that there is only one phy on this mdio bus.
			 */
			adpt->phydev = phy_find_first(mii_bus);
		else
			adpt->phydev = mdiobus_get_phy(mii_bus, phy_addr);

	} else {
		struct device_node *phy_np;

		ret = of_mdiobus_register(mii_bus, np);
		if (ret) {
			dev_err(&pdev->dev, "could not register mdio bus\n");
			return ret;
		}

		phy_np = of_parse_phandle(np, "phy-handle", 0);
		adpt->phydev = of_phy_find_device(phy_np);
		of_node_put(phy_np);
	}

	if (!adpt->phydev) {
		dev_err(&pdev->dev, "could not find external phy\n");
		mdiobus_unregister(mii_bus);
		return -ENODEV;
	}

	if (adpt->phydev->drv)
		phy_attached_print(adpt->phydev, NULL);

	return 0;
}
