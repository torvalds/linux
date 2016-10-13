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

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>

struct ns2_pci_phy {
	struct mdio_device *mdiodev;
	struct phy *phy;
};

#define BLK_ADDR_REG_OFFSET	0x1f
#define PLL_AFE1_100MHZ_BLK	0x2100
#define PLL_CLK_AMP_OFFSET	0x03
#define PLL_CLK_AMP_2P05V	0x2b18

static int ns2_pci_phy_init(struct phy *p)
{
	struct ns2_pci_phy *phy = phy_get_drvdata(p);
	int rc;

	/* select the AFE 100MHz block page */
	rc = mdiobus_write(phy->mdiodev->bus, phy->mdiodev->addr,
			   BLK_ADDR_REG_OFFSET, PLL_AFE1_100MHZ_BLK);
	if (rc)
		goto err;

	/* set the 100 MHz reference clock amplitude to 2.05 v */
	rc = mdiobus_write(phy->mdiodev->bus, phy->mdiodev->addr,
			   PLL_CLK_AMP_OFFSET, PLL_CLK_AMP_2P05V);
	if (rc)
		goto err;

	return 0;

err:
	dev_err(&phy->mdiodev->dev, "Error %d writing to phy\n", rc);
	return rc;
}

static struct phy_ops ns2_pci_phy_ops = {
	.init = ns2_pci_phy_init,
};

static int ns2_pci_phy_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct phy_provider *provider;
	struct ns2_pci_phy *p;
	struct phy *phy;

	phy = devm_phy_create(dev, dev->of_node, &ns2_pci_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create Phy\n");
		return PTR_ERR(phy);
	}

	p = devm_kmalloc(dev, sizeof(struct ns2_pci_phy),
			 GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->mdiodev = mdiodev;
	dev_set_drvdata(dev, p);

	p->phy = phy;
	phy_set_drvdata(phy, p);

	provider = devm_of_phy_provider_register(&phy->dev,
						 of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "failed to register Phy provider\n");
		return PTR_ERR(provider);
	}

	dev_info(dev, "%s PHY registered\n", dev_name(dev));

	return 0;
}

static const struct of_device_id ns2_pci_phy_of_match[] = {
	{ .compatible = "brcm,ns2-pcie-phy", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ns2_pci_phy_of_match);

static struct mdio_driver ns2_pci_phy_driver = {
	.mdiodrv = {
		.driver = {
			.name = "phy-bcm-ns2-pci",
			.of_match_table = ns2_pci_phy_of_match,
		},
	},
	.probe = ns2_pci_phy_probe,
};
mdio_module_driver(ns2_pci_phy_driver);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("Broadcom Northstar2 PCI Phy driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:phy-bcm-ns2-pci");
