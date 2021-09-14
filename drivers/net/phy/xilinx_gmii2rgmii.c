// SPDX-License-Identifier: GPL-2.0+
/* Xilinx GMII2RGMII Converter driver
 *
 * Copyright (C) 2016 Xilinx, Inc.
 * Copyright (C) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * Author: Andrew Lunn <andrew@lunn.ch>
 * Author: Kedareswara rao Appana <appanad@xilinx.com>
 *
 * Description:
 * This driver is developed for Xilinx GMII2RGMII Converter
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/of_mdio.h>

#define XILINX_GMII2RGMII_REG		0x10
#define XILINX_GMII2RGMII_SPEED_MASK	(BMCR_SPEED1000 | BMCR_SPEED100)

struct gmii2rgmii {
	struct phy_device *phy_dev;
	struct phy_driver *phy_drv;
	struct phy_driver conv_phy_drv;
	struct mdio_device *mdio;
};

static void xgmiitorgmii_configure(struct gmii2rgmii *priv, int speed)
{
	struct mii_bus *bus = priv->mdio->bus;
	int addr = priv->mdio->addr;
	u16 val;

	val = mdiobus_read(bus, addr, XILINX_GMII2RGMII_REG);
	val &= ~XILINX_GMII2RGMII_SPEED_MASK;

	if (speed == SPEED_1000)
		val |= BMCR_SPEED1000;
	else if (speed == SPEED_100)
		val |= BMCR_SPEED100;
	else
		val |= BMCR_SPEED10;

	mdiobus_write(bus, addr, XILINX_GMII2RGMII_REG, val);
}

static int xgmiitorgmii_read_status(struct phy_device *phydev)
{
	struct gmii2rgmii *priv = mdiodev_get_drvdata(&phydev->mdio);
	int err;

	if (priv->phy_drv->read_status)
		err = priv->phy_drv->read_status(phydev);
	else
		err = genphy_read_status(phydev);
	if (err < 0)
		return err;

	xgmiitorgmii_configure(priv, phydev->speed);

	return 0;
}

static int xgmiitorgmii_set_loopback(struct phy_device *phydev, bool enable)
{
	struct gmii2rgmii *priv = mdiodev_get_drvdata(&phydev->mdio);
	int err;

	if (priv->phy_drv->set_loopback)
		err = priv->phy_drv->set_loopback(phydev, enable);
	else
		err = genphy_loopback(phydev, enable);
	if (err < 0)
		return err;

	xgmiitorgmii_configure(priv, phydev->speed);

	return 0;
}

static int xgmiitorgmii_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct device_node *np = dev->of_node, *phy_node;
	struct gmii2rgmii *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phy_node = of_parse_phandle(np, "phy-handle", 0);
	if (!phy_node) {
		dev_err(dev, "Couldn't parse phy-handle\n");
		return -ENODEV;
	}

	priv->phy_dev = of_phy_find_device(phy_node);
	of_node_put(phy_node);
	if (!priv->phy_dev) {
		dev_info(dev, "Couldn't find phydev\n");
		return -EPROBE_DEFER;
	}

	if (!priv->phy_dev->drv) {
		dev_info(dev, "Attached phy not ready\n");
		return -EPROBE_DEFER;
	}

	priv->mdio = mdiodev;
	priv->phy_drv = priv->phy_dev->drv;
	memcpy(&priv->conv_phy_drv, priv->phy_dev->drv,
	       sizeof(struct phy_driver));
	priv->conv_phy_drv.read_status = xgmiitorgmii_read_status;
	priv->conv_phy_drv.set_loopback = xgmiitorgmii_set_loopback;
	mdiodev_set_drvdata(&priv->phy_dev->mdio, priv);
	priv->phy_dev->drv = &priv->conv_phy_drv;

	return 0;
}

static const struct of_device_id xgmiitorgmii_of_match[] = {
	{ .compatible = "xlnx,gmii-to-rgmii-1.0" },
	{},
};
MODULE_DEVICE_TABLE(of, xgmiitorgmii_of_match);

static struct mdio_driver xgmiitorgmii_driver = {
	.probe	= xgmiitorgmii_probe,
	.mdiodrv.driver = {
		.name = "xgmiitorgmii",
		.of_match_table = xgmiitorgmii_of_match,
	},
};

mdio_module_driver(xgmiitorgmii_driver);

MODULE_DESCRIPTION("Xilinx GMII2RGMII converter driver");
MODULE_LICENSE("GPL");
