/*
 * abx500 clock implementation for ux500 platform.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500/ab8500-sysctrl.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/mfd/dbx500-prcmu.h>
#include "clk.h"

/* Clock definitions for ab8500 */
static int ab8500_reg_clks(struct device *dev)
{
	int ret;
	struct clk *clk;

	const char *intclk_parents[] = {"ab8500_sysclk", "ulpclk"};
	u16 intclk_reg_sel[] = {0 , AB8500_SYSULPCLKCTRL1};
	u8 intclk_reg_mask[] = {0 , AB8500_SYSULPCLKCTRL1_SYSULPCLKINTSEL_MASK};
	u8 intclk_reg_bits[] = {
		0 ,
		(1 << AB8500_SYSULPCLKCTRL1_SYSULPCLKINTSEL_SHIFT)
	};

	dev_info(dev, "register clocks for ab850x\n");

	/* Enable SWAT */
	ret = ab8500_sysctrl_set(AB8500_SWATCTRL, AB8500_SWATCTRL_SWATENABLE);
	if (ret)
		return ret;

	/* ab8500_sysclk */
	clk = clk_reg_prcmu_gate("ab8500_sysclk", NULL, PRCMU_SYSCLK,
				CLK_IS_ROOT);
	clk_register_clkdev(clk, "sysclk", "ab8500-usb.0");
	clk_register_clkdev(clk, "sysclk", "ab-iddet.0");
	clk_register_clkdev(clk, "sysclk", "ab85xx-codec.0");
	clk_register_clkdev(clk, "sysclk", "shrm_bus");

	/* ab8500_sysclk2 */
	clk = clk_reg_sysctrl_gate(dev , "ab8500_sysclk2", "ab8500_sysclk",
		AB8500_SYSULPCLKCTRL1, AB8500_SYSULPCLKCTRL1_SYSCLKBUF2REQ,
		AB8500_SYSULPCLKCTRL1_SYSCLKBUF2REQ, 0, 0);
	clk_register_clkdev(clk, "sysclk", "0-0070");

	/* ab8500_sysclk3 */
	clk = clk_reg_sysctrl_gate(dev , "ab8500_sysclk3", "ab8500_sysclk",
		AB8500_SYSULPCLKCTRL1, AB8500_SYSULPCLKCTRL1_SYSCLKBUF3REQ,
		AB8500_SYSULPCLKCTRL1_SYSCLKBUF3REQ, 0, 0);
	clk_register_clkdev(clk, "sysclk", "cg1960_core.0");

	/* ab8500_sysclk4 */
	clk = clk_reg_sysctrl_gate(dev , "ab8500_sysclk4", "ab8500_sysclk",
		AB8500_SYSULPCLKCTRL1, AB8500_SYSULPCLKCTRL1_SYSCLKBUF4REQ,
		AB8500_SYSULPCLKCTRL1_SYSCLKBUF4REQ, 0, 0);

	/* ab_ulpclk */
	clk = clk_reg_sysctrl_gate_fixed_rate(dev, "ulpclk", NULL,
		AB8500_SYSULPCLKCTRL1, AB8500_SYSULPCLKCTRL1_ULPCLKREQ,
		AB8500_SYSULPCLKCTRL1_ULPCLKREQ,
		38400000, 9000, CLK_IS_ROOT);
	clk_register_clkdev(clk, "ulpclk", "ab85xx-codec.0");

	/* ab8500_intclk */
	clk = clk_reg_sysctrl_set_parent(dev , "intclk", intclk_parents, 2,
		intclk_reg_sel, intclk_reg_mask, intclk_reg_bits, 0);
	clk_register_clkdev(clk, "intclk", "ab85xx-codec.0");
	clk_register_clkdev(clk, NULL, "ab8500-pwm.1");

	/* ab8500_audioclk */
	clk = clk_reg_sysctrl_gate(dev , "audioclk", "intclk",
		AB8500_SYSULPCLKCTRL1, AB8500_SYSULPCLKCTRL1_AUDIOCLKENA,
		AB8500_SYSULPCLKCTRL1_AUDIOCLKENA, 0, 0);
	clk_register_clkdev(clk, "audioclk", "ab85xx-codec.0");

	return 0;
}

/* Clock definitions for ab8540 */
static int ab8540_reg_clks(struct device *dev)
{
	return 0;
}

/* Clock definitions for ab9540 */
static int ab9540_reg_clks(struct device *dev)
{
	return 0;
}

static int abx500_clk_probe(struct platform_device *pdev)
{
	struct ab8500 *parent = dev_get_drvdata(pdev->dev.parent);
	int ret;

	if (is_ab8500(parent) || is_ab8505(parent)) {
		ret = ab8500_reg_clks(&pdev->dev);
	} else if (is_ab8540(parent)) {
		ret = ab8540_reg_clks(&pdev->dev);
	} else if (is_ab9540(parent)) {
		ret = ab9540_reg_clks(&pdev->dev);
	} else {
		dev_err(&pdev->dev, "non supported plf id\n");
		return -ENODEV;
	}

	return ret;
}

static struct platform_driver abx500_clk_driver = {
	.driver = {
		.name = "abx500-clk",
		.owner = THIS_MODULE,
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
