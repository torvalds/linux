/*
 * Copyright (c) 2017 Samsung Electronics Co., Ltd.
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for Exynos4412 ISP module.
*/

#include <dt-bindings/clock/exynos4.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "clk.h"

/* Exynos4x12 specific registers, which belong to ISP power domain */
#define E4X12_DIV_ISP0		0x0300
#define E4X12_DIV_ISP1		0x0304
#define E4X12_GATE_ISP0		0x0800
#define E4X12_GATE_ISP1		0x0804

/*
 * Support for CMU save/restore across system suspends
 */
static struct samsung_clk_reg_dump *exynos4x12_save_isp;

static const unsigned long exynos4x12_clk_isp_save[] __initconst = {
	E4X12_DIV_ISP0,
	E4X12_DIV_ISP1,
	E4X12_GATE_ISP0,
	E4X12_GATE_ISP1,
};

static struct samsung_div_clock exynos4x12_isp_div_clks[] = {
	DIV(CLK_ISP_DIV_ISP0, "div_isp0", "aclk200", E4X12_DIV_ISP0, 0, 3),
	DIV(CLK_ISP_DIV_ISP1, "div_isp1", "aclk200", E4X12_DIV_ISP0, 4, 3),
	DIV(CLK_ISP_DIV_MCUISP0, "div_mcuisp0", "aclk400_mcuisp",
	    E4X12_DIV_ISP1, 4, 3),
	DIV(CLK_ISP_DIV_MCUISP1, "div_mcuisp1", "div_mcuisp0",
	    E4X12_DIV_ISP1, 8, 3),
	DIV(0, "div_mpwm", "div_isp1", E4X12_DIV_ISP1, 0, 3),
};

static struct samsung_gate_clock exynos4x12_isp_gate_clks[] = {
	GATE(CLK_ISP_FIMC_ISP, "isp", "aclk200", E4X12_GATE_ISP0, 0, 0, 0),
	GATE(CLK_ISP_FIMC_DRC, "drc", "aclk200", E4X12_GATE_ISP0, 1, 0, 0),
	GATE(CLK_ISP_FIMC_FD, "fd", "aclk200", E4X12_GATE_ISP0, 2, 0, 0),
	GATE(CLK_ISP_FIMC_LITE0, "lite0", "aclk200", E4X12_GATE_ISP0, 3, 0, 0),
	GATE(CLK_ISP_FIMC_LITE1, "lite1", "aclk200", E4X12_GATE_ISP0, 4, 0, 0),
	GATE(CLK_ISP_MCUISP, "mcuisp", "aclk200", E4X12_GATE_ISP0, 5, 0, 0),
	GATE(CLK_ISP_GICISP, "gicisp", "aclk200", E4X12_GATE_ISP0, 7, 0, 0),
	GATE(CLK_ISP_SMMU_ISP, "smmu_isp", "aclk200", E4X12_GATE_ISP0, 8, 0, 0),
	GATE(CLK_ISP_SMMU_DRC, "smmu_drc", "aclk200", E4X12_GATE_ISP0, 9, 0, 0),
	GATE(CLK_ISP_SMMU_FD, "smmu_fd", "aclk200", E4X12_GATE_ISP0, 10, 0, 0),
	GATE(CLK_ISP_SMMU_LITE0, "smmu_lite0", "aclk200", E4X12_GATE_ISP0, 11,
	     0, 0),
	GATE(CLK_ISP_SMMU_LITE1, "smmu_lite1", "aclk200", E4X12_GATE_ISP0, 12,
	     0, 0),
	GATE(CLK_ISP_PPMUISPMX, "ppmuispmx", "aclk200", E4X12_GATE_ISP0, 20,
	     0, 0),
	GATE(CLK_ISP_PPMUISPX, "ppmuispx", "aclk200", E4X12_GATE_ISP0, 21,
	     0, 0),
	GATE(CLK_ISP_MCUCTL_ISP, "mcuctl_isp", "aclk200", E4X12_GATE_ISP0, 23,
	     0, 0),
	GATE(CLK_ISP_MPWM_ISP, "mpwm_isp", "aclk200", E4X12_GATE_ISP0, 24,
	     0, 0),
	GATE(CLK_ISP_I2C0_ISP, "i2c0_isp", "aclk200", E4X12_GATE_ISP0, 25,
	     0, 0),
	GATE(CLK_ISP_I2C1_ISP, "i2c1_isp", "aclk200", E4X12_GATE_ISP0, 26,
	     0, 0),
	GATE(CLK_ISP_MTCADC_ISP, "mtcadc_isp", "aclk200", E4X12_GATE_ISP0, 27,
	     0, 0),
	GATE(CLK_ISP_PWM_ISP, "pwm_isp", "aclk200", E4X12_GATE_ISP0, 28, 0, 0),
	GATE(CLK_ISP_WDT_ISP, "wdt_isp", "aclk200", E4X12_GATE_ISP0, 30, 0, 0),
	GATE(CLK_ISP_UART_ISP, "uart_isp", "aclk200", E4X12_GATE_ISP0, 31,
	     0, 0),
	GATE(CLK_ISP_ASYNCAXIM, "asyncaxim", "aclk200", E4X12_GATE_ISP1, 0,
	     0, 0),
	GATE(CLK_ISP_SMMU_ISPCX, "smmu_ispcx", "aclk200", E4X12_GATE_ISP1, 4,
	     0, 0),
	GATE(CLK_ISP_SPI0_ISP, "spi0_isp", "aclk200", E4X12_GATE_ISP1, 12,
	     0, 0),
	GATE(CLK_ISP_SPI1_ISP, "spi1_isp", "aclk200", E4X12_GATE_ISP1, 13,
	     0, 0),
};

static int __maybe_unused exynos4x12_isp_clk_suspend(struct device *dev)
{
	struct samsung_clk_provider *ctx = dev_get_drvdata(dev);

	samsung_clk_save(ctx->reg_base, exynos4x12_save_isp,
			 ARRAY_SIZE(exynos4x12_clk_isp_save));
	return 0;
}

static int __maybe_unused exynos4x12_isp_clk_resume(struct device *dev)
{
	struct samsung_clk_provider *ctx = dev_get_drvdata(dev);

	samsung_clk_restore(ctx->reg_base, exynos4x12_save_isp,
			    ARRAY_SIZE(exynos4x12_clk_isp_save));
	return 0;
}

static int __init exynos4x12_isp_clk_probe(struct platform_device *pdev)
{
	struct samsung_clk_provider *ctx;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *res;
	void __iomem *reg_base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(reg_base)) {
		dev_err(dev, "failed to map registers\n");
		return PTR_ERR(reg_base);
	}

	exynos4x12_save_isp = samsung_clk_alloc_reg_dump(exynos4x12_clk_isp_save,
					ARRAY_SIZE(exynos4x12_clk_isp_save));
	if (!exynos4x12_save_isp)
		return -ENOMEM;

	ctx = samsung_clk_init(np, reg_base, CLK_NR_ISP_CLKS);
	ctx->dev = dev;

	platform_set_drvdata(pdev, ctx);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	samsung_clk_register_div(ctx, exynos4x12_isp_div_clks,
				 ARRAY_SIZE(exynos4x12_isp_div_clks));
	samsung_clk_register_gate(ctx, exynos4x12_isp_gate_clks,
				  ARRAY_SIZE(exynos4x12_isp_gate_clks));

	samsung_clk_of_add_provider(np, ctx);
	pm_runtime_put(dev);

	return 0;
}

static const struct of_device_id exynos4x12_isp_clk_of_match[] = {
	{ .compatible = "samsung,exynos4412-isp-clock", },
	{ },
};

static const struct dev_pm_ops exynos4x12_isp_pm_ops = {
	SET_RUNTIME_PM_OPS(exynos4x12_isp_clk_suspend,
			   exynos4x12_isp_clk_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

static struct platform_driver exynos4x12_isp_clk_driver __refdata = {
	.driver	= {
		.name = "exynos4x12-isp-clk",
		.of_match_table = exynos4x12_isp_clk_of_match,
		.suppress_bind_attrs = true,
		.pm = &exynos4x12_isp_pm_ops,
	},
	.probe = exynos4x12_isp_clk_probe,
};

static int __init exynos4x12_isp_clk_init(void)
{
	return platform_driver_register(&exynos4x12_isp_clk_driver);
}
core_initcall(exynos4x12_isp_clk_init);
