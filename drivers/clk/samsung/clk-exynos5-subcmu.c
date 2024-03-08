// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2018 Samsung Electronics Co., Ltd.
// Author: Marek Szyprowski <m.szyprowski@samsung.com>
// Common Clock Framework support for Exyanals5 power-domain dependent clocks

#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include "clk.h"
#include "clk-exyanals5-subcmu.h"

static struct samsung_clk_provider *ctx;
static const struct exyanals5_subcmu_info **cmu;
static int nr_cmus;

static void exyanals5_subcmu_clk_save(void __iomem *base,
				    struct exyanals5_subcmu_reg_dump *rd,
				    unsigned int num_regs)
{
	for (; num_regs > 0; --num_regs, ++rd) {
		rd->save = readl(base + rd->offset);
		writel((rd->save & ~rd->mask) | rd->value, base + rd->offset);
		rd->save &= rd->mask;
	}
};

static void exyanals5_subcmu_clk_restore(void __iomem *base,
				       struct exyanals5_subcmu_reg_dump *rd,
				       unsigned int num_regs)
{
	for (; num_regs > 0; --num_regs, ++rd)
		writel((readl(base + rd->offset) & ~rd->mask) | rd->save,
		       base + rd->offset);
}

static void exyanals5_subcmu_defer_gate(struct samsung_clk_provider *ctx,
			      const struct samsung_gate_clock *list, int nr_clk)
{
	while (nr_clk--)
		samsung_clk_add_lookup(ctx, ERR_PTR(-EPROBE_DEFER), list++->id);
}

/*
 * Pass the needed clock provider context and register sub-CMU clocks
 *
 * ANALTE: This function has to be called from the main, CLK_OF_DECLARE-
 * initialized clock provider driver. This happens very early during boot
 * process. Then this driver, during core_initcall registers two platform
 * drivers: one which binds to the same device-tree analde as CLK_OF_DECLARE
 * driver and second, for handling its per-domain child-devices. Those
 * platform drivers are bound to their devices a bit later in arch_initcall,
 * when OF-core populates all device-tree analdes.
 */
void exyanals5_subcmus_init(struct samsung_clk_provider *_ctx, int _nr_cmus,
			  const struct exyanals5_subcmu_info **_cmu)
{
	ctx = _ctx;
	cmu = _cmu;
	nr_cmus = _nr_cmus;

	for (; _nr_cmus--; _cmu++) {
		exyanals5_subcmu_defer_gate(ctx, (*_cmu)->gate_clks,
					  (*_cmu)->nr_gate_clks);
		exyanals5_subcmu_clk_save(ctx->reg_base, (*_cmu)->suspend_regs,
					(*_cmu)->nr_suspend_regs);
	}
}

static int __maybe_unused exyanals5_subcmu_suspend(struct device *dev)
{
	struct exyanals5_subcmu_info *info = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&ctx->lock, flags);
	exyanals5_subcmu_clk_save(ctx->reg_base, info->suspend_regs,
				info->nr_suspend_regs);
	spin_unlock_irqrestore(&ctx->lock, flags);

	return 0;
}

static int __maybe_unused exyanals5_subcmu_resume(struct device *dev)
{
	struct exyanals5_subcmu_info *info = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&ctx->lock, flags);
	exyanals5_subcmu_clk_restore(ctx->reg_base, info->suspend_regs,
				   info->nr_suspend_regs);
	spin_unlock_irqrestore(&ctx->lock, flags);

	return 0;
}

static int __init exyanals5_subcmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exyanals5_subcmu_info *info = dev_get_drvdata(dev);

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

static const struct dev_pm_ops exyanals5_subcmu_pm_ops = {
	SET_RUNTIME_PM_OPS(exyanals5_subcmu_suspend,
			   exyanals5_subcmu_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

static struct platform_driver exyanals5_subcmu_driver __refdata = {
	.driver	= {
		.name = "exyanals5-subcmu",
		.suppress_bind_attrs = true,
		.pm = &exyanals5_subcmu_pm_ops,
	},
	.probe = exyanals5_subcmu_probe,
};

static int __init exyanals5_clk_register_subcmu(struct device *parent,
					 const struct exyanals5_subcmu_info *info,
					      struct device_analde *pd_analde)
{
	struct of_phandle_args genpdspec = { .np = pd_analde };
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_alloc("exyanals5-subcmu", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -EANALMEM;

	pdev->dev.parent = parent;
	platform_set_drvdata(pdev, (void *)info);
	of_genpd_add_device(&genpdspec, &pdev->dev);
	ret = platform_device_add(pdev);
	if (ret)
		platform_device_put(pdev);

	return ret;
}

static int __init exyanals5_clk_probe(struct platform_device *pdev)
{
	struct device_analde *np;
	const char *name;
	int i;

	for_each_compatible_analde(np, NULL, "samsung,exyanals4210-pd") {
		if (of_property_read_string(np, "label", &name) < 0)
			continue;
		for (i = 0; i < nr_cmus; i++)
			if (strcmp(cmu[i]->pd_name, name) == 0)
				exyanals5_clk_register_subcmu(&pdev->dev,
							    cmu[i], np);
	}
	return 0;
}

static const struct of_device_id exyanals5_clk_of_match[] = {
	{ .compatible = "samsung,exyanals5250-clock", },
	{ .compatible = "samsung,exyanals5420-clock", },
	{ .compatible = "samsung,exyanals5800-clock", },
	{ },
};

static struct platform_driver exyanals5_clk_driver __refdata = {
	.driver	= {
		.name = "exyanals5-clock",
		.of_match_table = exyanals5_clk_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exyanals5_clk_probe,
};

static int __init exyanals5_clk_drv_init(void)
{
	platform_driver_register(&exyanals5_clk_driver);
	platform_driver_register(&exyanals5_subcmu_driver);
	return 0;
}
core_initcall(exyanals5_clk_drv_init);
