// SPDX-License-Identifier: GPL-2.0-only
/*
 * abx500 clock implementation for ux500 platform.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500/ab8500-sysctrl.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <dt-bindings/clock/ste-ab8500.h>
#include "clk.h"

#define AB8500_NUM_CLKS 6

static struct clk *ab8500_clks[AB8500_NUM_CLKS];
static struct clk_onecell_data ab8500_clk_data;

/* Clock definitions for ab8500 */
static int ab8500_reg_clks(struct device *dev)
{
	int ret;
	struct clk *clk;
	struct device_node *np = dev->of_node;
	const char *intclk_parents[] = {"ab8500_sysclk", "ulpclk"};
	u16 intclk_reg_sel[] = {0 , AB8500_SYSULPCLKCTRL1};
	u8 intclk_reg_mask[] = {0 , AB8500_SYSULPCLKCTRL1_SYSULPCLKINTSEL_MASK};
	u8 intclk_reg_bits[] = {
		0 ,
		(1 << AB8500_SYSULPCLKCTRL1_SYSULPCLKINTSEL_SHIFT)
	};

	/* Enable SWAT */
	ret = ab8500_sysctrl_set(AB8500_SWATCTRL, AB8500_SWATCTRL_SWATENABLE);
	if (ret)
		return ret;

	/* ab8500_sysclk2 */
	clk = clk_reg_sysctrl_gate(dev , "ab8500_sysclk2", "ab8500_sysclk",
		AB8500_SYSULPCLKCTRL1, AB8500_SYSULPCLKCTRL1_SYSCLKBUF2REQ,
		AB8500_SYSULPCLKCTRL1_SYSCLKBUF2REQ, 0, 0);
	ab8500_clks[AB8500_SYSCLK_BUF2] = clk;

	/* ab8500_sysclk3 */
	clk = clk_reg_sysctrl_gate(dev , "ab8500_sysclk3", "ab8500_sysclk",
		AB8500_SYSULPCLKCTRL1, AB8500_SYSULPCLKCTRL1_SYSCLKBUF3REQ,
		AB8500_SYSULPCLKCTRL1_SYSCLKBUF3REQ, 0, 0);
	ab8500_clks[AB8500_SYSCLK_BUF3] = clk;

	/* ab8500_sysclk4 */
	clk = clk_reg_sysctrl_gate(dev , "ab8500_sysclk4", "ab8500_sysclk",
		AB8500_SYSULPCLKCTRL1, AB8500_SYSULPCLKCTRL1_SYSCLKBUF4REQ,
		AB8500_SYSULPCLKCTRL1_SYSCLKBUF4REQ, 0, 0);
	ab8500_clks[AB8500_SYSCLK_BUF4] = clk;

	/* ab_ulpclk */
	clk = clk_reg_sysctrl_gate_fixed_rate(dev, "ulpclk", NULL,
		AB8500_SYSULPCLKCTRL1, AB8500_SYSULPCLKCTRL1_ULPCLKREQ,
		AB8500_SYSULPCLKCTRL1_ULPCLKREQ,
		38400000, 9000, 0);
	ab8500_clks[AB8500_SYSCLK_ULP] = clk;

	/* ab8500_intclk */
	clk = clk_reg_sysctrl_set_parent(dev , "intclk", intclk_parents, 2,
		intclk_reg_sel, intclk_reg_mask, intclk_reg_bits, 0);
	ab8500_clks[AB8500_SYSCLK_INT] = clk;

	/* ab8500_audioclk */
	clk = clk_reg_sysctrl_gate(dev , "audioclk", "intclk",
		AB8500_SYSULPCLKCTRL1, AB8500_SYSULPCLKCTRL1_AUDIOCLKENA,
		AB8500_SYSULPCLKCTRL1_AUDIOCLKENA, 0, 0);
	ab8500_clks[AB8500_SYSCLK_AUDIO] = clk;

	ab8500_clk_data.clks = ab8500_clks;
	ab8500_clk_data.clk_num = ARRAY_SIZE(ab8500_clks);
	of_clk_add_provider(np, of_clk_src_onecell_get, &ab8500_clk_data);

	dev_info(dev, "registered clocks for ab850x\n");

	return 0;
}

static int abx500_clk_probe(struct platform_device *pdev)
{
	struct ab8500 *parent = dev_get_drvdata(pdev->dev.parent);
	int ret;

	if (is_ab8500(parent) || is_ab8505(parent)) {
		ret = ab8500_reg_clks(&pdev->dev);
	} else {
		dev_err(&pdev->dev, "non supported plf id\n");
		return -ENODEV;
	}

	return ret;
}

static const struct of_device_id abx500_clk_match[] = {
	{ .compatible = "stericsson,ab8500-clk", },
	{}
};

static struct platform_driver abx500_clk_driver = {
	.driver = {
		.name = "abx500-clk",
		.of_match_table = abx500_clk_match,
	},
	.probe	= abx500_clk_probe,
};

static int __init abx500_clk_init(void)
{
	return platform_driver_register(&abx500_clk_driver);
}
arch_initcall(abx500_clk_init);

MODULE_AUTHOR("Ulf Hansson <ulf.hansson@linaro.org");
MODULE_DESCRIPTION("ABX500 clk driver");
MODULE_LICENSE("GPL v2");
