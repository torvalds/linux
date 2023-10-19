// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 NovaTech LLC
 * George McCollister <george.mccollister@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/if_vlan.h>
#include <linux/of.h>
#include "xrs700x.h"
#include "xrs700x_reg.h"

#define XRS_MDIO_IBA0	0x10
#define XRS_MDIO_IBA1	0x11
#define XRS_MDIO_IBD	0x14

#define XRS_IB_READ	0x0
#define XRS_IB_WRITE	0x1

static int xrs700x_mdio_reg_read(void *context, unsigned int reg,
				 unsigned int *val)
{
	struct mdio_device *mdiodev = context;
	struct device *dev = &mdiodev->dev;
	u16 uval;
	int ret;

	uval = (u16)FIELD_GET(GENMASK(31, 16), reg);

	ret = mdiodev_write(mdiodev, XRS_MDIO_IBA1, uval);
	if (ret < 0) {
		dev_err(dev, "xrs mdiobus_write returned %d\n", ret);
		return ret;
	}

	uval = (u16)((reg & GENMASK(15, 1)) | XRS_IB_READ);

	ret = mdiodev_write(mdiodev, XRS_MDIO_IBA0, uval);
	if (ret < 0) {
		dev_err(dev, "xrs mdiobus_write returned %d\n", ret);
		return ret;
	}

	ret = mdiodev_read(mdiodev, XRS_MDIO_IBD);
	if (ret < 0) {
		dev_err(dev, "xrs mdiobus_read returned %d\n", ret);
		return ret;
	}

	*val = (unsigned int)ret;

	return 0;
}

static int xrs700x_mdio_reg_write(void *context, unsigned int reg,
				  unsigned int val)
{
	struct mdio_device *mdiodev = context;
	struct device *dev = &mdiodev->dev;
	u16 uval;
	int ret;

	ret = mdiodev_write(mdiodev, XRS_MDIO_IBD, (u16)val);
	if (ret < 0) {
		dev_err(dev, "xrs mdiobus_write returned %d\n", ret);
		return ret;
	}

	uval = (u16)FIELD_GET(GENMASK(31, 16), reg);

	ret = mdiodev_write(mdiodev, XRS_MDIO_IBA1, uval);
	if (ret < 0) {
		dev_err(dev, "xrs mdiobus_write returned %d\n", ret);
		return ret;
	}

	uval = (u16)((reg & GENMASK(15, 1)) | XRS_IB_WRITE);

	ret = mdiodev_write(mdiodev, XRS_MDIO_IBA0, uval);
	if (ret < 0) {
		dev_err(dev, "xrs mdiobus_write returned %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct regmap_config xrs700x_mdio_regmap_config = {
	.val_bits = 16,
	.reg_stride = 2,
	.reg_bits = 32,
	.pad_bits = 0,
	.write_flag_mask = 0,
	.read_flag_mask = 0,
	.reg_read = xrs700x_mdio_reg_read,
	.reg_write = xrs700x_mdio_reg_write,
	.max_register = XRS_VLAN(VLAN_N_VID - 1),
	.cache_type = REGCACHE_NONE,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG
};

static int xrs700x_mdio_probe(struct mdio_device *mdiodev)
{
	struct xrs700x *priv;
	int ret;

	priv = xrs700x_switch_alloc(&mdiodev->dev, mdiodev);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init(&mdiodev->dev, NULL, mdiodev,
					&xrs700x_mdio_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(&mdiodev->dev, "Failed to initialize regmap: %d\n", ret);
		return ret;
	}

	dev_set_drvdata(&mdiodev->dev, priv);

	ret = xrs700x_switch_register(priv);

	/* Main DSA driver may not be started yet. */
	if (ret)
		return ret;

	return 0;
}

static void xrs700x_mdio_remove(struct mdio_device *mdiodev)
{
	struct xrs700x *priv = dev_get_drvdata(&mdiodev->dev);

	if (!priv)
		return;

	xrs700x_switch_remove(priv);
}

static void xrs700x_mdio_shutdown(struct mdio_device *mdiodev)
{
	struct xrs700x *priv = dev_get_drvdata(&mdiodev->dev);

	if (!priv)
		return;

	xrs700x_switch_shutdown(priv);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

static const struct of_device_id __maybe_unused xrs700x_mdio_dt_ids[] = {
	{ .compatible = "arrow,xrs7003e", .data = &xrs7003e_info },
	{ .compatible = "arrow,xrs7003f", .data = &xrs7003f_info },
	{ .compatible = "arrow,xrs7004e", .data = &xrs7004e_info },
	{ .compatible = "arrow,xrs7004f", .data = &xrs7004f_info },
	{},
};
MODULE_DEVICE_TABLE(of, xrs700x_mdio_dt_ids);

static struct mdio_driver xrs700x_mdio_driver = {
	.mdiodrv.driver = {
		.name	= "xrs700x-mdio",
		.of_match_table = of_match_ptr(xrs700x_mdio_dt_ids),
	},
	.probe	= xrs700x_mdio_probe,
	.remove	= xrs700x_mdio_remove,
	.shutdown = xrs700x_mdio_shutdown,
};

mdio_module_driver(xrs700x_mdio_driver);

MODULE_AUTHOR("George McCollister <george.mccollister@gmail.com>");
MODULE_DESCRIPTION("Arrow SpeedChips XRS700x DSA MDIO driver");
MODULE_LICENSE("GPL v2");
