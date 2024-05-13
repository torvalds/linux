// SPDX-License-Identifier: GPL-2.0
//
// reset-uniphier-glue.c - Glue layer reset driver for UniPhier
// Copyright 2018 Socionext Inc.
// Author: Kunihiko Hayashi <hayashi.kunihiko@socionext.com>

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/reset/reset-simple.h>

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
	struct reset_control_bulk_data rst[MAX_RSTS];
	struct reset_simple_data rdata;
	const struct uniphier_glue_reset_soc_data *data;
};

static void uniphier_clk_disable(void *_priv)
{
	struct uniphier_glue_reset_priv *priv = _priv;

	clk_bulk_disable_unprepare(priv->data->nclks, priv->clk);
}

static void uniphier_rst_assert(void *_priv)
{
	struct uniphier_glue_reset_priv *priv = _priv;

	reset_control_bulk_assert(priv->data->nrsts, priv->rst);
}

static int uniphier_glue_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_glue_reset_priv *priv;
	struct resource *res;
	int i, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->data = of_device_get_match_data(dev);
	if (WARN_ON(!priv->data || priv->data->nclks > MAX_CLKS ||
		    priv->data->nrsts > MAX_RSTS))
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->rdata.membase = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->rdata.membase))
		return PTR_ERR(priv->rdata.membase);

	for (i = 0; i < priv->data->nclks; i++)
		priv->clk[i].id = priv->data->clock_names[i];
	ret = devm_clk_bulk_get(dev, priv->data->nclks, priv->clk);
	if (ret)
		return ret;

	for (i = 0; i < priv->data->nrsts; i++)
		priv->rst[i].id = priv->data->reset_names[i];
	ret = devm_reset_control_bulk_get_shared(dev, priv->data->nrsts,
						 priv->rst);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(priv->data->nclks, priv->clk);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, uniphier_clk_disable, priv);
	if (ret)
		return ret;

	ret = reset_control_bulk_deassert(priv->data->nrsts, priv->rst);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, uniphier_rst_assert, priv);
	if (ret)
		return ret;

	spin_lock_init(&priv->rdata.lock);
	priv->rdata.rcdev.owner = THIS_MODULE;
	priv->rdata.rcdev.nr_resets = resource_size(res) * BITS_PER_BYTE;
	priv->rdata.rcdev.ops = &reset_simple_ops;
	priv->rdata.rcdev.of_node = dev->of_node;
	priv->rdata.active_low = true;

	return devm_reset_controller_register(dev, &priv->rdata.rcdev);
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
		.compatible = "socionext,uniphier-pro5-usb3-reset",
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
		.compatible = "socionext,uniphier-nx1-usb3-reset",
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
	.driver = {
		.name = "uniphier-glue-reset",
		.of_match_table = uniphier_glue_reset_match,
	},
};
module_platform_driver(uniphier_glue_reset_driver);

MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_DESCRIPTION("UniPhier Glue layer reset driver");
MODULE_LICENSE("GPL");
