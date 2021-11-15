// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Aspeed I2C Interrupt Controller.
 *
 * Copyright (C) ASPEED Technology Inc.
 * Ryan Chen <ryan_chen@aspeedtech.com>
 */
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>
#include "ast2600-i2c-global.h"

struct aspeed_i2c_ic {
	void __iomem		*base;
	int			parent_irq;
	u32			i2c_irq_mask;
	struct reset_control	*rst;
	struct irq_domain	*irq_domain;
	int			bus_num;
};

static const struct of_device_id aspeed_i2c_ic_of_match[] = {
	{ .compatible = "aspeed,ast2600-i2c-global", .data = (void *)0},
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_i2c_ic_of_match);

struct aspeed_i2c_base_clk {
	const char	*name;
	unsigned long	base_freq;
};

#define BASE_CLK_COUNT 4

static const struct aspeed_i2c_base_clk i2c_base_clk[BASE_CLK_COUNT] = {
	/* name	target_freq */
	{  "base_clk3",	20000000 },	/* 20M */
	{  "base_clk2",	10000000 },	/* 10M */
	{  "base_clk1",	3250000 },	/* 3.25M */
	{  "base_clk0",	1000000 },	/* 1M */
};

static u32 aspeed_i2c_ic_get_new_clk_divider(unsigned long base_clk, struct device_node *node)
{
	struct clk_hw_onecell_data *onecell;
	unsigned long base_freq;
	u32 clk_divider = 0;
	struct clk_hw *hw;
	int err;
	int i;
	int j;

	onecell = kzalloc(sizeof(*onecell) +
			  (BASE_CLK_COUNT * sizeof(struct clk_hw *)),
			  GFP_KERNEL);

	if (!onecell) {
		pr_err("allocate clk_hw\n");
		return 0;
	}

	onecell->num = BASE_CLK_COUNT;

	pr_debug("base_clk %ld\n", base_clk);
	for (j = 0; j < BASE_CLK_COUNT; j++) {
		pr_debug("target clk : %ld\n", i2c_base_clk[j].base_freq);
		for (i = 0; i < 0xff; i++) {
			/*
			 * i maps to div:
			 * 0x00: div 1
			 * 0x01: div 1.5
			 * 0x02: div 2
			 * 0x03: div 2.5
			 * 0x04: div 3
			 * ...
			 * 0xFE: div 128
			 * 0xFF: div 128.5
			 */
			base_freq = base_clk * 2 / (2 + i);
			if (base_freq <= i2c_base_clk[j].base_freq)
				break;
		}
		pr_info("i2cg - %s : %ld\n", i2c_base_clk[j].name, base_freq);
		hw = clk_hw_register_fixed_rate(NULL, i2c_base_clk[j].name, NULL, 0, base_freq);
		if (IS_ERR(hw)) {
			pr_err("failed to register input clock: %ld\n", PTR_ERR(hw));
			break;
		}
		onecell->hws[j] = hw;
		clk_divider |= (i << (8 * j));
	}

	err = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, onecell);
	if (err)
		pr_err("failed to add i2c base clk provider: %d\n", err);

	return clk_divider;
}

static int aspeed_i2c_global_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	unsigned long	parent_clk_frequency;
	struct aspeed_i2c_ic *i2c_ic;
	struct clk *parent_clk;
	struct resource *res;
	u32 clk_divider;

	i2c_ic = devm_kzalloc(&pdev->dev, sizeof(*i2c_ic), GFP_KERNEL);
	if (IS_ERR(i2c_ic))
		return PTR_ERR(i2c_ic);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c_ic->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c_ic->base))
		return PTR_ERR(i2c_ic->base);

	i2c_ic->bus_num = (int)device_get_match_data(&pdev->dev);
	if (i2c_ic->bus_num) {
		i2c_ic->parent_irq = platform_get_irq(pdev, 0);
		if (i2c_ic->parent_irq < 0)
			return i2c_ic->parent_irq;
	}

	i2c_ic->rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(i2c_ic->rst)) {
		dev_dbg(&pdev->dev,
			"missing or invalid reset controller device tree entry");
	} else {
		/* SCU I2C Reset */
		reset_control_assert(i2c_ic->rst);
		udelay(3);
		reset_control_deassert(i2c_ic->rst);
	}

	/* ast2600 init */
	writel(ASPEED_I2CG_SLAVE_PKT_NAK | ASPEED_I2CG_CTRL_NEW_REG | ASPEED_I2CG_CTRL_NEW_CLK_DIV,
	       i2c_ic->base + ASPEED_I2CG_CTRL);
	parent_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(parent_clk))
		return PTR_ERR(parent_clk);
	parent_clk_frequency = clk_get_rate(parent_clk);
	pr_debug("parent_clk_frequency %ld\n", parent_clk_frequency);
	clk_divider = aspeed_i2c_ic_get_new_clk_divider(parent_clk_frequency, node);
	writel(clk_divider, i2c_ic->base + ASPEED_I2CG_CLK_DIV_CTRL);

	pr_info("i2c global registered\n");

	return 0;
}

static struct platform_driver aspeed_i2c_ic_driver = {
	.probe  = aspeed_i2c_global_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = aspeed_i2c_ic_of_match,
	},
};

static int __init aspeed_i2c_global_init(void)
{
	return platform_driver_register(&aspeed_i2c_ic_driver);
}
postcore_initcall(aspeed_i2c_global_init);

MODULE_AUTHOR("Ryan Chen");
MODULE_DESCRIPTION("ASPEED I2C Global Driver");
MODULE_LICENSE("GPL v2");
