// SPDX-License-Identifier: GPL-2.0
//
// Regulator controller driver for UniPhier SoC
// Copyright 2018 Socionext Inc.
// Author: Kunihiko Hayashi <hayashi.kunihiko@socionext.com>

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/reset.h>

#define MAX_CLKS	2
#define MAX_RSTS	2

struct uniphier_regulator_soc_data {
	int nclks;
	const char * const *clock_names;
	int nrsts;
	const char * const *reset_names;
	const struct regulator_desc *desc;
	const struct regmap_config *regconf;
};

struct uniphier_regulator_priv {
	struct clk_bulk_data clk[MAX_CLKS];
	struct reset_control *rst[MAX_RSTS];
	const struct uniphier_regulator_soc_data *data;
};

static struct regulator_ops uniphier_regulator_ops = {
	.enable     = regulator_enable_regmap,
	.disable    = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static int uniphier_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_regulator_priv *priv;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regmap *regmap;
	struct resource *res;
	void __iomem *base;
	const char *name;
	int i, ret, nr;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->data = of_device_get_match_data(dev);
	if (WARN_ON(!priv->data))
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	for (i = 0; i < priv->data->nclks; i++)
		priv->clk[i].id = priv->data->clock_names[i];
	ret = devm_clk_bulk_get(dev, priv->data->nclks, priv->clk);
	if (ret)
		return ret;

	for (i = 0; i < priv->data->nrsts; i++) {
		name = priv->data->reset_names[i];
		priv->rst[i] = devm_reset_control_get_shared(dev, name);
		if (IS_ERR(priv->rst[i]))
			return PTR_ERR(priv->rst[i]);
	}

	ret = clk_bulk_prepare_enable(priv->data->nclks, priv->clk);
	if (ret)
		return ret;

	for (nr = 0; nr < priv->data->nrsts; nr++) {
		ret = reset_control_deassert(priv->rst[nr]);
		if (ret)
			goto out_rst_assert;
	}

	regmap = devm_regmap_init_mmio(dev, base, priv->data->regconf);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	config.dev = dev;
	config.driver_data = priv;
	config.of_node = dev->of_node;
	config.regmap = regmap;
	config.init_data = of_get_regulator_init_data(dev, dev->of_node,
						      priv->data->desc);
	rdev = devm_regulator_register(dev, priv->data->desc, &config);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		goto out_rst_assert;
	}

	platform_set_drvdata(pdev, priv);

	return 0;

out_rst_assert:
	while (nr--)
		reset_control_assert(priv->rst[nr]);

	clk_bulk_disable_unprepare(priv->data->nclks, priv->clk);

	return ret;
}

static int uniphier_regulator_remove(struct platform_device *pdev)
{
	struct uniphier_regulator_priv *priv = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < priv->data->nrsts; i++)
		reset_control_assert(priv->rst[i]);

	clk_bulk_disable_unprepare(priv->data->nclks, priv->clk);

	return 0;
}

/* USB3 controller data */
#define USB3VBUS_OFFSET		0x0
#define USB3VBUS_REG		BIT(4)
#define USB3VBUS_REG_EN		BIT(3)
static const struct regulator_desc uniphier_usb3_regulator_desc = {
	.name = "vbus",
	.of_match = of_match_ptr("vbus"),
	.ops = &uniphier_regulator_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.enable_reg  = USB3VBUS_OFFSET,
	.enable_mask = USB3VBUS_REG_EN | USB3VBUS_REG,
	.enable_val  = USB3VBUS_REG_EN | USB3VBUS_REG,
	.disable_val = USB3VBUS_REG_EN,
};

static const struct regmap_config uniphier_usb3_regulator_regconf = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 1,
};

static const char * const uniphier_pro4_clock_reset_names[] = {
	"gio", "link",
};

static const struct uniphier_regulator_soc_data uniphier_pro4_usb3_data = {
	.nclks = ARRAY_SIZE(uniphier_pro4_clock_reset_names),
	.clock_names = uniphier_pro4_clock_reset_names,
	.nrsts = ARRAY_SIZE(uniphier_pro4_clock_reset_names),
	.reset_names = uniphier_pro4_clock_reset_names,
	.desc = &uniphier_usb3_regulator_desc,
	.regconf = &uniphier_usb3_regulator_regconf,
};

static const char * const uniphier_pxs2_clock_reset_names[] = {
	"link",
};

static const struct uniphier_regulator_soc_data uniphier_pxs2_usb3_data = {
	.nclks = ARRAY_SIZE(uniphier_pxs2_clock_reset_names),
	.clock_names = uniphier_pxs2_clock_reset_names,
	.nrsts = ARRAY_SIZE(uniphier_pxs2_clock_reset_names),
	.reset_names = uniphier_pxs2_clock_reset_names,
	.desc = &uniphier_usb3_regulator_desc,
	.regconf = &uniphier_usb3_regulator_regconf,
};

static const struct of_device_id uniphier_regulator_match[] = {
	/* USB VBUS */
	{
		.compatible = "socionext,uniphier-pro4-usb3-regulator",
		.data = &uniphier_pro4_usb3_data,
	},
	{
		.compatible = "socionext,uniphier-pxs2-usb3-regulator",
		.data = &uniphier_pxs2_usb3_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-usb3-regulator",
		.data = &uniphier_pxs2_usb3_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-usb3-regulator",
		.data = &uniphier_pxs2_usb3_data,
	},
	{ /* Sentinel */ },
};

static struct platform_driver uniphier_regulator_driver = {
	.probe = uniphier_regulator_probe,
	.remove = uniphier_regulator_remove,
	.driver = {
		.name  = "uniphier-regulator",
		.of_match_table = uniphier_regulator_match,
	},
};
module_platform_driver(uniphier_regulator_driver);

MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_DESCRIPTION("UniPhier Regulator Controller Driver");
MODULE_LICENSE("GPL");
