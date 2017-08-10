/*
 * Copyright (C) 2015 - 2016 Samsung Electronics Co., Ltd.
 *
 * Authors: Inha Song <ideal.song@samsung.com>
 *          Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * Samsung Exynos SoC series Low Power Audio Subsystem driver.
 *
 * This module provides regmap for the Top SFR region and instantiates
 * devices for IP blocks like DMAC, I2S, UART.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>
#include <linux/types.h>

/* LPASS Top register definitions */
#define SFR_LPASS_CORE_SW_RESET		0x08
#define  LPASS_SB_SW_RESET		BIT(11)
#define  LPASS_UART_SW_RESET		BIT(10)
#define  LPASS_PCM_SW_RESET		BIT(9)
#define  LPASS_I2S_SW_RESET		BIT(8)
#define  LPASS_WDT1_SW_RESET		BIT(4)
#define  LPASS_WDT0_SW_RESET		BIT(3)
#define  LPASS_TIMER_SW_RESET		BIT(2)
#define  LPASS_MEM_SW_RESET		BIT(1)
#define  LPASS_DMA_SW_RESET		BIT(0)

#define SFR_LPASS_INTR_CA5_MASK		0x48
#define SFR_LPASS_INTR_CPU_MASK		0x58
#define  LPASS_INTR_APM			BIT(9)
#define  LPASS_INTR_MIF			BIT(8)
#define  LPASS_INTR_TIMER		BIT(7)
#define  LPASS_INTR_DMA			BIT(6)
#define  LPASS_INTR_GPIO		BIT(5)
#define  LPASS_INTR_I2S			BIT(4)
#define  LPASS_INTR_PCM			BIT(3)
#define  LPASS_INTR_SLIMBUS		BIT(2)
#define  LPASS_INTR_UART		BIT(1)
#define  LPASS_INTR_SFR			BIT(0)

struct exynos_lpass {
	/* pointer to the LPASS TOP regmap */
	struct regmap *top;
	struct clk *sfr0_clk;
};

static void exynos_lpass_core_sw_reset(struct exynos_lpass *lpass, int mask)
{
	unsigned int val = 0;

	regmap_read(lpass->top, SFR_LPASS_CORE_SW_RESET, &val);

	val &= ~mask;
	regmap_write(lpass->top, SFR_LPASS_CORE_SW_RESET, val);

	usleep_range(100, 150);

	val |= mask;
	regmap_write(lpass->top, SFR_LPASS_CORE_SW_RESET, val);
}

static void exynos_lpass_enable(struct exynos_lpass *lpass)
{
	clk_prepare_enable(lpass->sfr0_clk);

	/* Unmask SFR, DMA and I2S interrupt */
	regmap_write(lpass->top, SFR_LPASS_INTR_CA5_MASK,
		     LPASS_INTR_SFR | LPASS_INTR_DMA | LPASS_INTR_I2S);

	regmap_write(lpass->top, SFR_LPASS_INTR_CPU_MASK,
		     LPASS_INTR_SFR | LPASS_INTR_DMA | LPASS_INTR_I2S);

	exynos_lpass_core_sw_reset(lpass, LPASS_I2S_SW_RESET);
	exynos_lpass_core_sw_reset(lpass, LPASS_DMA_SW_RESET);
	exynos_lpass_core_sw_reset(lpass, LPASS_MEM_SW_RESET);
}

static void exynos_lpass_disable(struct exynos_lpass *lpass)
{
	/* Mask any unmasked IP interrupt sources */
	regmap_write(lpass->top, SFR_LPASS_INTR_CPU_MASK, 0);
	regmap_write(lpass->top, SFR_LPASS_INTR_CA5_MASK, 0);

	clk_disable_unprepare(lpass->sfr0_clk);
}

static const struct regmap_config exynos_lpass_reg_conf = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xfc,
	.fast_io	= true,
};

static int exynos_lpass_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_lpass *lpass;
	void __iomem *base_top;
	struct resource *res;

	lpass = devm_kzalloc(dev, sizeof(*lpass), GFP_KERNEL);
	if (!lpass)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base_top = devm_ioremap_resource(dev, res);
	if (IS_ERR(base_top))
		return PTR_ERR(base_top);

	lpass->sfr0_clk = devm_clk_get(dev, "sfr0_ctrl");
	if (IS_ERR(lpass->sfr0_clk))
		return PTR_ERR(lpass->sfr0_clk);

	lpass->top = regmap_init_mmio(dev, base_top,
					&exynos_lpass_reg_conf);
	if (IS_ERR(lpass->top)) {
		dev_err(dev, "LPASS top regmap initialization failed\n");
		return PTR_ERR(lpass->top);
	}

	platform_set_drvdata(pdev, lpass);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	exynos_lpass_enable(lpass);

	return devm_of_platform_populate(dev);
}

static int exynos_lpass_remove(struct platform_device *pdev)
{
	struct exynos_lpass *lpass = platform_get_drvdata(pdev);

	exynos_lpass_disable(lpass);
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		exynos_lpass_disable(lpass);
	regmap_exit(lpass->top);

	return 0;
}

static int __maybe_unused exynos_lpass_suspend(struct device *dev)
{
	struct exynos_lpass *lpass = dev_get_drvdata(dev);

	exynos_lpass_disable(lpass);

	return 0;
}

static int __maybe_unused exynos_lpass_resume(struct device *dev)
{
	struct exynos_lpass *lpass = dev_get_drvdata(dev);

	exynos_lpass_enable(lpass);

	return 0;
}

static const struct dev_pm_ops lpass_pm_ops = {
	SET_RUNTIME_PM_OPS(exynos_lpass_suspend, exynos_lpass_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

static const struct of_device_id exynos_lpass_of_match[] = {
	{ .compatible = "samsung,exynos5433-lpass" },
	{ },
};
MODULE_DEVICE_TABLE(of, exynos_lpass_of_match);

static struct platform_driver exynos_lpass_driver = {
	.driver = {
		.name		= "exynos-lpass",
		.pm		= &lpass_pm_ops,
		.of_match_table	= exynos_lpass_of_match,
	},
	.probe	= exynos_lpass_probe,
	.remove	= exynos_lpass_remove,
};
module_platform_driver(exynos_lpass_driver);

MODULE_DESCRIPTION("Samsung Low Power Audio Subsystem driver");
MODULE_LICENSE("GPL v2");
