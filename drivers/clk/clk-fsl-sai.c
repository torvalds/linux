// SPDX-License-Identifier: GPL-2.0
/*
 * Freescale SAI BCLK as a generic clock driver
 *
 * Copyright 2020 Michael Walle <michael@walle.cc>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#define I2S_CSR		0x00
#define I2S_CR2		0x08
#define CSR_BCE_BIT	28
#define CR2_BCD		BIT(24)
#define CR2_DIV_SHIFT	0
#define CR2_DIV_WIDTH	8

struct fsl_sai_clk {
	struct clk_divider div;
	struct clk_gate gate;
	spinlock_t lock;
};

static int fsl_sai_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fsl_sai_clk *sai_clk;
	struct clk_parent_data pdata = { .index = 0 };
	void __iomem *base;
	struct clk_hw *hw;
	struct resource *res;

	sai_clk = devm_kzalloc(dev, sizeof(*sai_clk), GFP_KERNEL);
	if (!sai_clk)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	spin_lock_init(&sai_clk->lock);

	sai_clk->gate.reg = base + I2S_CSR;
	sai_clk->gate.bit_idx = CSR_BCE_BIT;
	sai_clk->gate.lock = &sai_clk->lock;

	sai_clk->div.reg = base + I2S_CR2;
	sai_clk->div.shift = CR2_DIV_SHIFT;
	sai_clk->div.width = CR2_DIV_WIDTH;
	sai_clk->div.lock = &sai_clk->lock;

	/* set clock direction, we are the BCLK master */
	writel(CR2_BCD, base + I2S_CR2);

	hw = devm_clk_hw_register_composite_pdata(dev, dev->of_node->name,
						  &pdata, 1, NULL, NULL,
						  &sai_clk->div.hw,
						  &clk_divider_ops,
						  &sai_clk->gate.hw,
						  &clk_gate_ops,
						  CLK_SET_RATE_GATE);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
}

static const struct of_device_id of_fsl_sai_clk_ids[] = {
	{ .compatible = "fsl,vf610-sai-clock" },
	{ }
};
MODULE_DEVICE_TABLE(of, of_fsl_sai_clk_ids);

static struct platform_driver fsl_sai_clk_driver = {
	.probe = fsl_sai_clk_probe,
	.driver		= {
		.name	= "fsl-sai-clk",
		.of_match_table = of_fsl_sai_clk_ids,
	},
};
module_platform_driver(fsl_sai_clk_driver);

MODULE_DESCRIPTION("Freescale SAI bitclock-as-a-clock driver");
MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_ALIAS("platform:fsl-sai-clk");
