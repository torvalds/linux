/*
 * Copyright (C) 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>

#define NSP_USB3_RST_CTRL_OFFSET	0x3f8

/* mdio reg access */
#define NSP_USB3_PHY_BASE_ADDR_REG	0x1f

#define NSP_USB3_PHY_PLL30_BLOCK	0x8000
#define NSP_USB3_PLL_CONTROL		0x01
#define NSP_USB3_PLLA_CONTROL0		0x0a
#define NSP_USB3_PLLA_CONTROL1		0x0b

#define NSP_USB3_PHY_TX_PMD_BLOCK	0x8040
#define NSP_USB3_TX_PMD_CONTROL1	0x01

#define NSP_USB3_PHY_PIPE_BLOCK		0x8060
#define NSP_USB3_LFPS_CMP		0x02
#define NSP_USB3_LFPS_DEGLITCH		0x03

struct nsp_usb3_phy {
	struct regmap *usb3_ctrl;
	struct phy *phy;
	struct mdio_device *mdiodev;
};

static int nsp_usb3_phy_init(struct phy *phy)
{
	struct nsp_usb3_phy *iphy = phy_get_drvdata(phy);
	struct mii_bus *bus = iphy->mdiodev->bus;
	int addr = iphy->mdiodev->addr;
	u32 data;
	int rc;

	rc = regmap_read(iphy->usb3_ctrl, 0, &data);
	if (rc)
		return rc;
	data |= 1;
	rc = regmap_write(iphy->usb3_ctrl, 0, data);
	if (rc)
		return rc;

	rc = regmap_write(iphy->usb3_ctrl, NSP_USB3_RST_CTRL_OFFSET, 1);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_PHY_BASE_ADDR_REG,
			   NSP_USB3_PHY_PLL30_BLOCK);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_PLL_CONTROL, 0x1000);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_PLLA_CONTROL0, 0x6400);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_PLLA_CONTROL1, 0xc000);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_PLLA_CONTROL1, 0x8000);
	if (rc)
		return rc;

	rc = regmap_write(iphy->usb3_ctrl, NSP_USB3_RST_CTRL_OFFSET, 0);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_PLL_CONTROL, 0x9000);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_PHY_BASE_ADDR_REG,
			   NSP_USB3_PHY_PIPE_BLOCK);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_LFPS_CMP, 0xf30d);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_LFPS_DEGLITCH, 0x6302);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_PHY_BASE_ADDR_REG,
			   NSP_USB3_PHY_TX_PMD_BLOCK);
	if (rc)
		return rc;

	rc = mdiobus_write(bus, addr, NSP_USB3_TX_PMD_CONTROL1, 0x1003);

	return rc;
}

static struct phy_ops nsp_usb3_phy_ops = {
	.init	= nsp_usb3_phy_init,
	.owner	= THIS_MODULE,
};

static int nsp_usb3_phy_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct phy_provider *provider;
	struct nsp_usb3_phy *iphy;

	iphy = devm_kzalloc(dev, sizeof(*iphy), GFP_KERNEL);
	if (!iphy)
		return -ENOMEM;
	iphy->mdiodev = mdiodev;

	iphy->usb3_ctrl = syscon_regmap_lookup_by_phandle(dev->of_node,
						 "usb3-ctrl-syscon");
	if (IS_ERR(iphy->usb3_ctrl))
		return PTR_ERR(iphy->usb3_ctrl);

	iphy->phy = devm_phy_create(dev, dev->of_node, &nsp_usb3_phy_ops);
	if (IS_ERR(iphy->phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(iphy->phy);
	}

	phy_set_drvdata(iphy->phy, iphy);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "could not register PHY provider\n");
		return PTR_ERR(provider);
	}

	return 0;
}

static const struct of_device_id nsp_usb3_phy_of_match[] = {
	{.compatible = "brcm,nsp-usb3-phy",},
	{ /* sentinel */ }
};

static struct mdio_driver nsp_usb3_phy_driver = {
	.mdiodrv = {
		.driver = {
			.name = "nsp-usb3-phy",
			.of_match_table = nsp_usb3_phy_of_match,
		},
	},
	.probe = nsp_usb3_phy_probe,
};

mdio_module_driver(nsp_usb3_phy_driver);

MODULE_DESCRIPTION("Broadcom NSP USB3 PHY driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yendapally Reddy Dhananjaya Reddy <yendapally.reddy@broadcom.com");
