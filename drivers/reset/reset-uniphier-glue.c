// SPDX-License-Identifier: GPL-2.0
//
// reset-uniphier-glue.c - Glue layer reset driver for UniPhier
// Copyright 2018 Socionext Inc.
// Author: Kunihiko Hayashi <hayashi.kunihiko@socionext.com>

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "reset-simple.h"

#define MAX_CLKS	2
#define MAX_RSTS	2

struct uniphier_glue_reset_soc_data {
	int nclks;
	const char * const *clock_names;
	int nrsts;
	const char * const *reset_names;
};

struct uniphier_glue_reset_priv {
	struct clk_bulk_data clk[MAX_CLKS];
	struct reset_control *rst[MAX_RSTS];
	struct reset_simple_data rdata;
	const struct uniphier_glue_reset_soc_data *data;
};

static int uniphier_glue_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_glue_reset_priv *priv;
	struct resource *res;
	resource_size_t size;
	const char *name;
	int i, ret, nr;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->data = of_device_get_match_data(dev);
	if (WARN_ON(!priv->data || priv->data->nclks > MAX_CLKS ||
		    priv->data->nrsts > MAX_RSTS))
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	size = resource_size(res);
	priv->rdata.membase = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->rdata.membase))
		return PTR_ERR(priv->rdata.membase);

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

	spin_lock_init(&priv->rdata.lock);
	priv->rdata.rcdev.owner = THIS_MODULE;
	priv->rdata.rcdev.nr_resets = size * BITS_PER_BYTE;
	priv->rdata.rcdev.ops = &reset_simple_ops;
	priv->rdata.rcdev.of_node = dev->of_node;
	priv->rdata.active_low = true;

	platform_set_drvdata(pdev, priv);

	ret = devm_reset_controller_register(dev, &priv->rdata.rcdev);
	if (ret)
		goto out_rst_assert;

	return 0;

out_rst_assert:
	while (nr--)
		reset_control_assert(priv->rst[nr]);

	clk_bulk_disable_unprepare(priv->data->nclks, priv->clk);

	return ret;
}

static int uniphier_glue_reset_remove(struct platform_device *pdev)
{
	struct uniphier_glue_reset_priv *priv = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < priv->data->nrsts; i++)
		reset_control_assert(priv->rst[i]);

	clk_bulk_disable_unprepare(priv->data->nclks, priv->clk);

	return 0;
}

static const char * const uniphier_pro4_clock_reset_names[] = {
	"gio", "link",
};

static const struct uniphier_glue_reset_soc_data uniphier_pro4_data = {
	.nclks = ARRAY_SIZE(uniphier_pro4_clock_reset_names),
	.clock_names = uniphier_pro4_clock_reset_names,
	.nrsts = ARRAY_SIZE(uniphier_pro4_clock_reset_names),
	.reset_names = uniphier_pro4_clock_reset_names,
};

static const char * const uniphier_pxs2_clock_reset_names[] = {
	"link",
};

static const struct uniphier_glue_reset_soc_data uniphier_pxs2_data = {
	.nclks = ARRAY_SIZE(uniphier_pxs2_clock_reset_names),
	.clock_names = uniphier_pxs2_clock_reset_names,
	.nrsts = ARRAY_SIZE(uniphier_pxs2_clock_reset_names),
	.reset_names = uniphier_pxs2_clock_reset_names,
};

static const struct of_device_id uniphier_glue_reset_match[] = {
	{
		.compatible = "socionext,uniphier-pro4-usb3-reset",
		.data = &uniphier_pro4_data,
	},
	{
		.compatible = "socionext,uniphier-pxs2-usb3-reset",
		.data = &uniphier_pxs2_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-usb3-reset",
		.data = &uniphier_pxs2_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-usb3-reset",
		.data = &uniphier_pxs2_data,
	},
	{
		.compatible = "socionext,uniphier-pro4-ahci-reset",
		.data = &uniphier_pro4_data,
	},
	{
		.compatible = "socionext,uniphier-pxs2-ahci-reset",
		.data = &uniphier_pxs2_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-ahci-reset",
		.data = &uniphier_pxs2_data,
	},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_glue_reset_match);

static struct platform_driver uniphier_glue_reset_driver = {
	.probe = uniphier_glue_reset_probe,
	.remove = uniphier_glue_reset_remove,
	.driver = {
		.name = "uniphier-glue-reset",
		.of_match_table = uniphier_glue_reset_match,
	},
};
module_platform_driver(uniphier_glue_reset_driver);

MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_DESCRIPTION("UniPhier Glue layer reset driver");
MODULE_LICENSE("GPL");
