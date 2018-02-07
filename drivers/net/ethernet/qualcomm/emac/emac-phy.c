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

#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/iopoll.h>
#include <linux/acpi.h>
#include "emac.h"

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
#define MDIO_STATUS_DELAY_TIME                                       1

static int emac_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	struct emac_adapter *adpt = bus->priv;
	u32 reg;

	emac_reg_update32(adpt->base + EMAC_PHY_STS, PHY_ADDR_BMSK,
			  (addr << PHY_ADDR_SHFT));

	reg = SUP_PREAMBLE |
	      ((MDIO_CLK_25_4 << MDIO_CLK_SEL_SHFT) & MDIO_CLK_SEL_BMSK) |
	      ((regnum << MDIO_REG_ADDR_SHFT) & MDIO_REG_ADDR_BMSK) |
	      MDIO_START | MDIO_RD_NWR;

	writel(reg, adpt->base + EMAC_MDIO_CTRL);

	if (readl_poll_timeout(adpt->base + EMAC_MDIO_CTRL, reg,
			       !(reg & (MDIO_START | MDIO_BUSY)),
			       MDIO_STATUS_DELAY_TIME, MDIO_WAIT_TIMES * 100))
		return -EIO;

	return (reg >> MDIO_DATA_SHFT) & MDIO_DATA_BMSK;
}

static int emac_mdio_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	struct emac_adapter *adpt = bus->priv;
	u32 reg;

	emac_reg_update32(adpt->base + EMAC_PHY_STS, PHY_ADDR_BMSK,
			  (addr << PHY_ADDR_SHFT));

	reg = SUP_PREAMBLE |
		((MDIO_CLK_25_4 << MDIO_CLK_SEL_SHFT) & MDIO_CLK_SEL_BMSK) |
		((regnum << MDIO_REG_ADDR_SHFT) & MDIO_REG_ADDR_BMSK) |
		((val << MDIO_DATA_SHFT) & MDIO_DATA_BMSK) |
		MDIO_START;

	writel(reg, adpt->base + EMAC_MDIO_CTRL);

	if (readl_poll_timeout(adpt->base + EMAC_MDIO_CTRL, reg,
			       !(reg & (MDIO_START | MDIO_BUSY)),
			       MDIO_STATUS_DELAY_TIME, MDIO_WAIT_TIMES * 100))
		return -EIO;

	return 0;
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

		/* of_phy_find_device() claims a reference to the phydev,
		 * so we do that here manually as well. When the driver
		 * later unloads, it can unilaterally drop the reference
		 * without worrying about ACPI vs DT.
		 */
		if (adpt->phydev)
			get_device(&adpt->phydev->mdio.dev);
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

	return 0;
}
