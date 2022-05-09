// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Aspeed I2C Global Controller Driver.
 *
 * Copyright (C) ASPEED Technology Inc.
 */
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/io.h>
#include "i2c-ast2600-global.h"

struct ast2600_i2c_global {
	void __iomem		*base;
	struct reset_control	*rst;
};

static const struct of_device_id ast2600_i2c_global_of_match[] = {
	{ .compatible = "aspeed,ast2600-i2c-global", },
	{ },
};
MODULE_DEVICE_TABLE(of, ast2600_i2c_global_of_match);

#define AST2600_GLOBAL_INIT					\
			(AST2600_I2CG_SLAVE_PKT_NAK |	\
			AST2600_I2CG_CTRL_NEW_REG |		\
			AST2600_I2CG_CTRL_NEW_CLK_DIV)
#define I2CCG_DIV_CTRL 0xC6411208
/*
 * APB clk : 100Mhz
 * div  : scl       : baseclk [APB/((div/2) + 1)] : tBuf [1/bclk * 16]
 * I2CG10[31:24] base clk4 for i2c auto recovery timeout counter (0xC6)
 * I2CG10[23:16] base clk3 for Standard-mode (100Khz) min tBuf 4.7us
 * 0x3c : 100.8Khz  : 3.225Mhz                    : 4.96us
 * 0x3d : 99.2Khz   : 3.174Mhz                    : 5.04us
 * 0x3e : 97.65Khz  : 3.125Mhz                    : 5.12us
 * 0x40 : 97.75Khz  : 3.03Mhz                     : 5.28us
 * 0x41 : 99.5Khz   : 2.98Mhz                     : 5.36us (default)
 * I2CG10[15:8] base clk2 for Fast-mode (400Khz) min tBuf 1.3us
 * 0x12 : 400Khz    : 10Mhz                       : 1.6us
 * I2CG10[7:0] base clk1 for Fast-mode Plus (1Mhz) min tBuf 0.5us
 * 0x08 : 1Mhz      : 20Mhz                       : 0.8us
 */

static int ast2600_i2c_global_probe(struct platform_device *pdev)
{
	struct ast2600_i2c_global *i2c_global;
	struct resource *res;

	i2c_global = devm_kzalloc(&pdev->dev, sizeof(*i2c_global), GFP_KERNEL);
	if (IS_ERR(i2c_global))
		return PTR_ERR(i2c_global);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c_global->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c_global->base))
		return PTR_ERR(i2c_global->base);

	i2c_global->rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(i2c_global->rst))
		return IS_ERR(i2c_global->rst);

	reset_control_assert(i2c_global->rst);
	udelay(3);
	reset_control_deassert(i2c_global->rst);

	writel(AST2600_GLOBAL_INIT, i2c_global->base + AST2600_I2CG_CTRL);
	writel(I2CCG_DIV_CTRL, i2c_global->base + AST2600_I2CG_CLK_DIV_CTRL);

	pr_info("i2c global registered\n");

	return 0;
}

static struct platform_driver ast2600_i2c_global_driver = {
	.probe  = ast2600_i2c_global_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = ast2600_i2c_global_of_match,
	},
};

static int __init ast2600_i2c_global_init(void)
{
	return platform_driver_register(&ast2600_i2c_global_driver);
}
postcore_initcall(ast2600_i2c_global_init);

MODULE_AUTHOR("Ryan Chen");
MODULE_DESCRIPTION("AST2600 I2C Global Driver");
MODULE_LICENSE("GPL");
