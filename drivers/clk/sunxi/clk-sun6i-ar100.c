/*
 * Copyright (C) 2014 Free Electrons
 *
 * License Terms: GNU General Public License v2
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * Allwinner A31 AR100 clock driver
 *
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "clk-factors.h"

/**
 * sun6i_get_ar100_factors - Calculates factors p, m for AR100
 *
 * AR100 rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */
static void sun6i_get_ar100_factors(struct factors_request *req)
{
	unsigned long div;
	int shift;

	/* clock only divides */
	if (req->rate > req->parent_rate)
		req->rate = req->parent_rate;

	div = DIV_ROUND_UP(req->parent_rate, req->rate);

	if (div < 32)
		shift = 0;
	else if (div >> 1 < 32)
		shift = 1;
	else if (div >> 2 < 32)
		shift = 2;
	else
		shift = 3;

	div >>= shift;

	if (div > 32)
		div = 32;

	req->rate = (req->parent_rate >> shift) / div;
	req->m = div - 1;
	req->p = shift;
}

static const struct clk_factors_config sun6i_ar100_config = {
	.mwidth = 5,
	.mshift = 8,
	.pwidth = 2,
	.pshift = 4,
};

static const struct factors_data sun6i_ar100_data = {
	.mux = 16,
	.muxmask = GENMASK(1, 0),
	.table = &sun6i_ar100_config,
	.getter = sun6i_get_ar100_factors,
};

static DEFINE_SPINLOCK(sun6i_ar100_lock);

static int sun6i_a31_ar100_clk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *r;
	void __iomem *reg;
	struct clk *clk;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	clk = sunxi_factors_register(np, &sun6i_ar100_data, &sun6i_ar100_lock,
				     reg);
	if (!clk)
		return -ENOMEM;

	platform_set_drvdata(pdev, clk);

	return 0;
}

static int sun6i_a31_ar100_clk_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct clk *clk = platform_get_drvdata(pdev);

	sunxi_factors_unregister(np, clk);

	return 0;
}

static const struct of_device_id sun6i_a31_ar100_clk_dt_ids[] = {
	{ .compatible = "allwinner,sun6i-a31-ar100-clk" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun6i_a31_ar100_clk_dt_ids);

static struct platform_driver sun6i_a31_ar100_clk_driver = {
	.driver = {
		.name = "sun6i-a31-ar100-clk",
		.of_match_table = sun6i_a31_ar100_clk_dt_ids,
	},
	.probe = sun6i_a31_ar100_clk_probe,
	.remove = sun6i_a31_ar100_clk_remove,
};
module_platform_driver(sun6i_a31_ar100_clk_driver);

MODULE_AUTHOR("Boris BREZILLON <boris.brezillon@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A31 AR100 clock Driver");
MODULE_LICENSE("GPL v2");
