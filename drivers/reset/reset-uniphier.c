/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

struct uniphier_reset_data {
	unsigned int id;
	unsigned int reg;
	unsigned int bit;
	unsigned int flags;
#define UNIPHIER_RESET_ACTIVE_LOW		BIT(0)
};

#define UNIPHIER_RESET_ID_END		(unsigned int)(-1)

#define UNIPHIER_RESET_END				\
	{ .id = UNIPHIER_RESET_ID_END }

#define UNIPHIER_RESET(_id, _reg, _bit)			\
	{						\
		.id = (_id),				\
		.reg = (_reg),				\
		.bit = (_bit),				\
	}

#define UNIPHIER_RESETX(_id, _reg, _bit)		\
	{						\
		.id = (_id),				\
		.reg = (_reg),				\
		.bit = (_bit),				\
		.flags = UNIPHIER_RESET_ACTIVE_LOW,	\
	}

/* System reset data */
static const struct uniphier_reset_data uniphier_ld4_sys_reset_data[] = {
	UNIPHIER_RESETX(2, 0x2000, 2),		/* NAND */
	UNIPHIER_RESETX(8, 0x2000, 10),		/* STDMAC (Ether, HSC, MIO) */
	UNIPHIER_RESET_END,
};

static const struct uniphier_reset_data uniphier_pro4_sys_reset_data[] = {
	UNIPHIER_RESETX(2, 0x2000, 2),		/* NAND */
	UNIPHIER_RESETX(6, 0x2000, 12),		/* Ether */
	UNIPHIER_RESETX(8, 0x2000, 10),		/* STDMAC (HSC, MIO, RLE) */
	UNIPHIER_RESETX(12, 0x2000, 6),		/* GIO (Ether, SATA, USB3) */
	UNIPHIER_RESETX(14, 0x2000, 17),	/* USB30 */
	UNIPHIER_RESETX(15, 0x2004, 17),	/* USB31 */
	UNIPHIER_RESETX(40, 0x2000, 13),	/* AIO */
	UNIPHIER_RESET_END,
};

static const struct uniphier_reset_data uniphier_pro5_sys_reset_data[] = {
	UNIPHIER_RESETX(2, 0x2000, 2),		/* NAND */
	UNIPHIER_RESETX(8, 0x2000, 10),		/* STDMAC (HSC) */
	UNIPHIER_RESETX(12, 0x2000, 6),		/* GIO (PCIe, USB3) */
	UNIPHIER_RESETX(14, 0x2000, 17),	/* USB30 */
	UNIPHIER_RESETX(15, 0x2004, 17),	/* USB31 */
	UNIPHIER_RESETX(40, 0x2000, 13),	/* AIO */
	UNIPHIER_RESET_END,
};

static const struct uniphier_reset_data uniphier_pxs2_sys_reset_data[] = {
	UNIPHIER_RESETX(2, 0x2000, 2),		/* NAND */
	UNIPHIER_RESETX(6, 0x2000, 12),		/* Ether */
	UNIPHIER_RESETX(8, 0x2000, 10),		/* STDMAC (HSC, RLE) */
	UNIPHIER_RESETX(14, 0x2000, 17),	/* USB30 */
	UNIPHIER_RESETX(15, 0x2004, 17),	/* USB31 */
	UNIPHIER_RESETX(16, 0x2014, 4),		/* USB30-PHY0 */
	UNIPHIER_RESETX(17, 0x2014, 0),		/* USB30-PHY1 */
	UNIPHIER_RESETX(18, 0x2014, 2),		/* USB30-PHY2 */
	UNIPHIER_RESETX(20, 0x2014, 5),		/* USB31-PHY0 */
	UNIPHIER_RESETX(21, 0x2014, 1),		/* USB31-PHY1 */
	UNIPHIER_RESETX(28, 0x2014, 12),	/* SATA */
	UNIPHIER_RESET(29, 0x2014, 8),		/* SATA-PHY (active high) */
	UNIPHIER_RESETX(40, 0x2000, 13),	/* AIO */
	UNIPHIER_RESET_END,
};

static const struct uniphier_reset_data uniphier_ld11_sys_reset_data[] = {
	UNIPHIER_RESETX(2, 0x200c, 0),		/* NAND */
	UNIPHIER_RESETX(4, 0x200c, 2),		/* eMMC */
	UNIPHIER_RESETX(6, 0x200c, 6),		/* Ether */
	UNIPHIER_RESETX(8, 0x200c, 8),		/* STDMAC (HSC, MIO) */
	UNIPHIER_RESETX(40, 0x2008, 0),		/* AIO */
	UNIPHIER_RESETX(41, 0x2008, 1),		/* EVEA */
	UNIPHIER_RESETX(42, 0x2010, 2),		/* EXIV */
	UNIPHIER_RESET_END,
};

static const struct uniphier_reset_data uniphier_ld20_sys_reset_data[] = {
	UNIPHIER_RESETX(2, 0x200c, 0),		/* NAND */
	UNIPHIER_RESETX(4, 0x200c, 2),		/* eMMC */
	UNIPHIER_RESETX(6, 0x200c, 6),		/* Ether */
	UNIPHIER_RESETX(8, 0x200c, 8),		/* STDMAC (HSC) */
	UNIPHIER_RESETX(12, 0x200c, 5),		/* GIO (PCIe, USB3) */
	UNIPHIER_RESETX(16, 0x200c, 12),	/* USB30-PHY0 */
	UNIPHIER_RESETX(17, 0x200c, 13),	/* USB30-PHY1 */
	UNIPHIER_RESETX(18, 0x200c, 14),	/* USB30-PHY2 */
	UNIPHIER_RESETX(19, 0x200c, 15),	/* USB30-PHY3 */
	UNIPHIER_RESETX(40, 0x2008, 0),		/* AIO */
	UNIPHIER_RESETX(41, 0x2008, 1),		/* EVEA */
	UNIPHIER_RESETX(42, 0x2010, 2),		/* EXIV */
	UNIPHIER_RESET_END,
};

static const struct uniphier_reset_data uniphier_pxs3_sys_reset_data[] = {
	UNIPHIER_RESETX(2, 0x200c, 0),		/* NAND */
	UNIPHIER_RESETX(4, 0x200c, 2),		/* eMMC */
	UNIPHIER_RESETX(6, 0x200c, 9),		/* Ether0 */
	UNIPHIER_RESETX(7, 0x200c, 10),		/* Ether1 */
	UNIPHIER_RESETX(8, 0x200c, 12),		/* STDMAC */
	UNIPHIER_RESETX(12, 0x200c, 4),		/* USB30 link (GIO0) */
	UNIPHIER_RESETX(13, 0x200c, 5),		/* USB31 link (GIO1) */
	UNIPHIER_RESETX(16, 0x200c, 16),	/* USB30-PHY0 */
	UNIPHIER_RESETX(17, 0x200c, 18),	/* USB30-PHY1 */
	UNIPHIER_RESETX(18, 0x200c, 20),	/* USB30-PHY2 */
	UNIPHIER_RESETX(20, 0x200c, 17),	/* USB31-PHY0 */
	UNIPHIER_RESETX(21, 0x200c, 19),	/* USB31-PHY1 */
	UNIPHIER_RESET_END,
};

/* Media I/O reset data */
#define UNIPHIER_MIO_RESET_SD(id, ch)			\
	UNIPHIER_RESETX((id), 0x110 + 0x200 * (ch), 0)

#define UNIPHIER_MIO_RESET_SD_BRIDGE(id, ch)		\
	UNIPHIER_RESETX((id), 0x110 + 0x200 * (ch), 26)

#define UNIPHIER_MIO_RESET_EMMC_HW_RESET(id, ch)	\
	UNIPHIER_RESETX((id), 0x80 + 0x200 * (ch), 0)

#define UNIPHIER_MIO_RESET_USB2(id, ch)			\
	UNIPHIER_RESETX((id), 0x114 + 0x200 * (ch), 0)

#define UNIPHIER_MIO_RESET_USB2_BRIDGE(id, ch)		\
	UNIPHIER_RESETX((id), 0x110 + 0x200 * (ch), 24)

#define UNIPHIER_MIO_RESET_DMAC(id)			\
	UNIPHIER_RESETX((id), 0x110, 17)

static const struct uniphier_reset_data uniphier_ld4_mio_reset_data[] = {
	UNIPHIER_MIO_RESET_SD(0, 0),
	UNIPHIER_MIO_RESET_SD(1, 1),
	UNIPHIER_MIO_RESET_SD(2, 2),
	UNIPHIER_MIO_RESET_SD_BRIDGE(3, 0),
	UNIPHIER_MIO_RESET_SD_BRIDGE(4, 1),
	UNIPHIER_MIO_RESET_SD_BRIDGE(5, 2),
	UNIPHIER_MIO_RESET_EMMC_HW_RESET(6, 1),
	UNIPHIER_MIO_RESET_DMAC(7),
	UNIPHIER_MIO_RESET_USB2(8, 0),
	UNIPHIER_MIO_RESET_USB2(9, 1),
	UNIPHIER_MIO_RESET_USB2(10, 2),
	UNIPHIER_MIO_RESET_USB2_BRIDGE(12, 0),
	UNIPHIER_MIO_RESET_USB2_BRIDGE(13, 1),
	UNIPHIER_MIO_RESET_USB2_BRIDGE(14, 2),
	UNIPHIER_RESET_END,
};

static const struct uniphier_reset_data uniphier_pro5_sd_reset_data[] = {
	UNIPHIER_MIO_RESET_SD(0, 0),
	UNIPHIER_MIO_RESET_SD(1, 1),
	UNIPHIER_MIO_RESET_EMMC_HW_RESET(6, 1),
	UNIPHIER_RESET_END,
};

/* Peripheral reset data */
#define UNIPHIER_PERI_RESET_UART(id, ch)		\
	UNIPHIER_RESETX((id), 0x114, 19 + (ch))

#define UNIPHIER_PERI_RESET_I2C(id, ch)			\
	UNIPHIER_RESETX((id), 0x114, 5 + (ch))

#define UNIPHIER_PERI_RESET_FI2C(id, ch)		\
	UNIPHIER_RESETX((id), 0x114, 24 + (ch))

static const struct uniphier_reset_data uniphier_ld4_peri_reset_data[] = {
	UNIPHIER_PERI_RESET_UART(0, 0),
	UNIPHIER_PERI_RESET_UART(1, 1),
	UNIPHIER_PERI_RESET_UART(2, 2),
	UNIPHIER_PERI_RESET_UART(3, 3),
	UNIPHIER_PERI_RESET_I2C(4, 0),
	UNIPHIER_PERI_RESET_I2C(5, 1),
	UNIPHIER_PERI_RESET_I2C(6, 2),
	UNIPHIER_PERI_RESET_I2C(7, 3),
	UNIPHIER_PERI_RESET_I2C(8, 4),
	UNIPHIER_RESET_END,
};

static const struct uniphier_reset_data uniphier_pro4_peri_reset_data[] = {
	UNIPHIER_PERI_RESET_UART(0, 0),
	UNIPHIER_PERI_RESET_UART(1, 1),
	UNIPHIER_PERI_RESET_UART(2, 2),
	UNIPHIER_PERI_RESET_UART(3, 3),
	UNIPHIER_PERI_RESET_FI2C(4, 0),
	UNIPHIER_PERI_RESET_FI2C(5, 1),
	UNIPHIER_PERI_RESET_FI2C(6, 2),
	UNIPHIER_PERI_RESET_FI2C(7, 3),
	UNIPHIER_PERI_RESET_FI2C(8, 4),
	UNIPHIER_PERI_RESET_FI2C(9, 5),
	UNIPHIER_PERI_RESET_FI2C(10, 6),
	UNIPHIER_RESET_END,
};

/* Analog signal amplifiers reset data */
static const struct uniphier_reset_data uniphier_ld11_adamv_reset_data[] = {
	UNIPHIER_RESETX(0, 0x10, 6), /* EVEA */
	UNIPHIER_RESET_END,
};

/* core implementaton */
struct uniphier_reset_priv {
	struct reset_controller_dev rcdev;
	struct device *dev;
	struct regmap *regmap;
	const struct uniphier_reset_data *data;
};

#define to_uniphier_reset_priv(_rcdev) \
			container_of(_rcdev, struct uniphier_reset_priv, rcdev)

static int uniphier_reset_update(struct reset_controller_dev *rcdev,
				 unsigned long id, int assert)
{
	struct uniphier_reset_priv *priv = to_uniphier_reset_priv(rcdev);
	const struct uniphier_reset_data *p;

	for (p = priv->data; p->id != UNIPHIER_RESET_ID_END; p++) {
		unsigned int mask, val;

		if (p->id != id)
			continue;

		mask = BIT(p->bit);

		if (assert)
			val = mask;
		else
			val = ~mask;

		if (p->flags & UNIPHIER_RESET_ACTIVE_LOW)
			val = ~val;

		return regmap_write_bits(priv->regmap, p->reg, mask, val);
	}

	dev_err(priv->dev, "reset_id=%lu was not handled\n", id);
	return -EINVAL;
}

static int uniphier_reset_assert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return uniphier_reset_update(rcdev, id, 1);
}

static int uniphier_reset_deassert(struct reset_controller_dev *rcdev,
				   unsigned long id)
{
	return uniphier_reset_update(rcdev, id, 0);
}

static int uniphier_reset_status(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	struct uniphier_reset_priv *priv = to_uniphier_reset_priv(rcdev);
	const struct uniphier_reset_data *p;

	for (p = priv->data; p->id != UNIPHIER_RESET_ID_END; p++) {
		unsigned int val;
		int ret, asserted;

		if (p->id != id)
			continue;

		ret = regmap_read(priv->regmap, p->reg, &val);
		if (ret)
			return ret;

		asserted = !!(val & BIT(p->bit));

		if (p->flags & UNIPHIER_RESET_ACTIVE_LOW)
			asserted = !asserted;

		return asserted;
	}

	dev_err(priv->dev, "reset_id=%lu was not found\n", id);
	return -EINVAL;
}

static const struct reset_control_ops uniphier_reset_ops = {
	.assert = uniphier_reset_assert,
	.deassert = uniphier_reset_deassert,
	.status = uniphier_reset_status,
};

static int uniphier_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_reset_priv *priv;
	const struct uniphier_reset_data *p, *data;
	struct regmap *regmap;
	struct device_node *parent;
	unsigned int nr_resets = 0;

	data = of_device_get_match_data(dev);
	if (WARN_ON(!data))
		return -EINVAL;

	parent = of_get_parent(dev->of_node); /* parent should be syscon node */
	regmap = syscon_node_to_regmap(parent);
	of_node_put(parent);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to get regmap (error %ld)\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	for (p = data; p->id != UNIPHIER_RESET_ID_END; p++)
		nr_resets = max(nr_resets, p->id + 1);

	priv->rcdev.ops = &uniphier_reset_ops;
	priv->rcdev.owner = dev->driver->owner;
	priv->rcdev.of_node = dev->of_node;
	priv->rcdev.nr_resets = nr_resets;
	priv->dev = dev;
	priv->regmap = regmap;
	priv->data = data;

	return devm_reset_controller_register(&pdev->dev, &priv->rcdev);
}

static const struct of_device_id uniphier_reset_match[] = {
	/* System reset */
	{
		.compatible = "socionext,uniphier-ld4-reset",
		.data = uniphier_ld4_sys_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pro4-reset",
		.data = uniphier_pro4_sys_reset_data,
	},
	{
		.compatible = "socionext,uniphier-sld8-reset",
		.data = uniphier_ld4_sys_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pro5-reset",
		.data = uniphier_pro5_sys_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pxs2-reset",
		.data = uniphier_pxs2_sys_reset_data,
	},
	{
		.compatible = "socionext,uniphier-ld11-reset",
		.data = uniphier_ld11_sys_reset_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-reset",
		.data = uniphier_ld20_sys_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-reset",
		.data = uniphier_pxs3_sys_reset_data,
	},
	/* Media I/O reset, SD reset */
	{
		.compatible = "socionext,uniphier-ld4-mio-reset",
		.data = uniphier_ld4_mio_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pro4-mio-reset",
		.data = uniphier_ld4_mio_reset_data,
	},
	{
		.compatible = "socionext,uniphier-sld8-mio-reset",
		.data = uniphier_ld4_mio_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pro5-sd-reset",
		.data = uniphier_pro5_sd_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pxs2-sd-reset",
		.data = uniphier_pro5_sd_reset_data,
	},
	{
		.compatible = "socionext,uniphier-ld11-mio-reset",
		.data = uniphier_ld4_mio_reset_data,
	},
	{
		.compatible = "socionext,uniphier-ld11-sd-reset",
		.data = uniphier_pro5_sd_reset_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-sd-reset",
		.data = uniphier_pro5_sd_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-sd-reset",
		.data = uniphier_pro5_sd_reset_data,
	},
	/* Peripheral reset */
	{
		.compatible = "socionext,uniphier-ld4-peri-reset",
		.data = uniphier_ld4_peri_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pro4-peri-reset",
		.data = uniphier_pro4_peri_reset_data,
	},
	{
		.compatible = "socionext,uniphier-sld8-peri-reset",
		.data = uniphier_ld4_peri_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pro5-peri-reset",
		.data = uniphier_pro4_peri_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pxs2-peri-reset",
		.data = uniphier_pro4_peri_reset_data,
	},
	{
		.compatible = "socionext,uniphier-ld11-peri-reset",
		.data = uniphier_pro4_peri_reset_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-peri-reset",
		.data = uniphier_pro4_peri_reset_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-peri-reset",
		.data = uniphier_pro4_peri_reset_data,
	},
	/* Analog signal amplifiers reset */
	{
		.compatible = "socionext,uniphier-ld11-adamv-reset",
		.data = uniphier_ld11_adamv_reset_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-adamv-reset",
		.data = uniphier_ld11_adamv_reset_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_reset_match);

static struct platform_driver uniphier_reset_driver = {
	.probe = uniphier_reset_probe,
	.driver = {
		.name = "uniphier-reset",
		.of_match_table = uniphier_reset_match,
	},
};
module_platform_driver(uniphier_reset_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier Reset Controller Driver");
MODULE_LICENSE("GPL");
