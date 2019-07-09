// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2018 Samsung Electronics Co., Ltd.
// Author: Marek Szyprowski <m.szyprowski@samsung.com>
// Common Clock Framework support for Exynos5 power-domain dependent clocks

#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include "clk.h"
#include "clk-exynos5-subcmu.h"

static struct samsung_clk_provider *ctx;
static const struct exynos5_subcmu_info *cmu;
static int nr_cmus;

static void exynos5_subcmu_clk_save(void __iomem *base,
				    struct exynos5_subcmu_reg_dump *rd,
				    unsigned int num_regs)
{
	for (; num_regs > 0; --num_regs, ++rd) {
		rd->save = readl(base + rd->offset);
		writel((rd->save & ~rd->mask) | rd->value, base + rd->offset);
		rd->save &= rd->mask;
	}
};

static void exynos5_subcmu_clk_restore(void __iomem *base,
				       struct exynos5_subcmu_reg_dump *rd,
				       unsigned int num_regs)
{
	for (; num_regs > 0; --num_regs, ++rd)
		writel((readl(base + rd->offset) & ~rd->mask) | rd->save,
		       base + rd->offset);
}

static void exynos5_subcmu_defer_gate(struct samsung_clk_provider *ctx,
			      const struct samsung_gate_clock *list, int nr_clk)
{
	while (nr_clk--)
		samsung_clk_add_lookup(ctx, ERR_PTR(-EPROBE_DEFER), list++->id);
}

/*
 * Pass the needed clock provider context and register sub-CMU clocks
 *
 * NOTE: This function has to be called from the main, OF_CLK_DECLARE-
 * initialized clock provider driver. This happens very early during boot
 * process. Then this driver, during core_initcall registers two platform
 * drivers: one which binds to the same device-tree node as OF_CLK_DECLARE
 * driver and second, for handling its per-domain child-devices. Those
 * platform drivers are bound to their devices a bit later in arch_initcall,
 * when OF-core populates all device-tree nodes.
 */
void exynos5_subcmus_init(struct samsung_clk_provider *_ctx, int _nr_cmus,
			  const struct exynos5_subcmu_info *_cmu)
{
	ctx = _ctx;
	cmu = _cmu;
	nr_cmus = _nr_cmus;

	for (; _nr_cmus--; _cmu++) {
		exynos5_subcmu_defer_gate(ctx, _cmu->gate_clks,
					  _cmu->nr_gate_clks);
		exynos5_subcmu_clk_save(ctx->reg_base, _cmu->suspend_regs,
					_cmu->nr_suspend_regs);
	}
}

static int __maybe_unused exynos5_subcmu_suspend(struct device *dev)
{
	struct exynos5_subcmu_info *info = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&ctx->lock, flags);
	exynos5_subcmu_clk_save(ctx->reg_base, info->suspend_regs,
				info->nr_suspend_regs);
	spin_unlock_irqrestore(&ctx->lock, flags);

	return 0;
}

static int __maybe_unused exynos5_subcmu_resume(struct device *dev)
{
	struct exynos5_subcmu_info *info = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&ctx->lock, flags);
	exynos5_subcmu_clk_restore(ctx->reg_base, info->suspend_regs,
				   info->nr_suspend_regs);
	spin_unlock_irqrestore(&ctx->lock, flags);

	return 0;
}

static int __init exynos5_subcmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos5_subcmu_info *info = dev_get_drvdata(dev);

	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);
	pm_runtime_get(dev);

	ctx->dev = dev;
	samsung_clk_register_div(ctx, info->div_clks, info->nr_div_clks);
	samsung_clk_register_gate(ctx, info->gate_clks, info->nr_gate_clks);
	ctx->dev = NULL;

	pm_runtime_put_sync(dev);

	return 0;
}

static const struct dev_pm_ops exynos5_subcmu_pm_ops = {
	SET_RUNTIME_PM_OPS(exynos5_subcmu_suspend,
			   exynos5_subcmu_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

static struct platform_driver exynos5_subcmu_driver __refdata = {
	.driver	= {
		.name = "exynos5-subcmu",
		.suppress_bind_attrs = true,
		.pm = &exynos5_subcmu_pm_ops,
	},
	.probe = exynos5_subcmu_probe,
};

static int __init exynos5_clk_register_subcmu(struct device *parent,
					 const struct exynos5_subcmu_info *info,
					      struct device_node *pd_node)
{
	struct of_phandle_args genpdspec = { .np = pd_node };
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_alloc("exynos5-subcmu", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;

	pdev->dev.parent = parent;
	platform_set_drvdata(pdev, (void *)info);
	of_genpd_add_device(&genpdspec, &pdev->dev);
	ret = platform_device_add(pdev);
	if (ret)
		platform_device_put(pdev);

	return ret;
}

static int __init exynos5_clk_probe(struct platform_device *pdev)
{
	struct device_node *np;
	const char *name;
	int i;

	for_each_compatible_node(np, NULL, "samsung,exynos4210-pd") {
		if (of_property_read_string(np, "label", &name) < 0)
			continue;
		for (i = 0; i < nr_cmus; i++)
			if (strcmp(cmu[i].pd_name, name) == 0)
				exynos5_clk_register_subcmu(&pdev->dev,
							    &cmu[i], np);
	}
	return 0;
}

static const struct of_device_id exynos5_clk_of_match[] = {
	{ .compatible = "samsung,exynos5250-clock", },
	{ .compatible = "samsung,exynos5420-clock", },
	{ .compatible = "samsung,exynos5800-clock", },
	{ },
};

static struct platform_driver exynos5_clk_driver __refdata = {
	.driver	= {
		.name = "exynos5-clock",
		.of_match_table = exynos5_clk_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exynos5_clk_probe,
};

static int __init exynos5_clk_drv_init(void)
{
	platform_driver_register(&exynos5_clk_driver);
	platform_driver_register(&exynos5_subcmu_driver);
	return 0;
}
core_initcall(exynos5_clk_drv_init);
