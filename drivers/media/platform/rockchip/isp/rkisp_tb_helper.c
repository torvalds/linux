// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Rockchip Electronics Co., Ltd. */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>

#include "rkisp_tb_helper.h"

static const char *const loader_protect_clocks[] = {
	"aclk_isp",
	"hclk_isp",
	"clk_isp",
	"pclk_csiphy0",
	"pclk_csiphy1",
	"clk_mipicsi_out2io",
	"clk_scr1",
	"clk_scr1_core",
	"clk_scr1_rtc",
	"clk_scr1_jtag"
};

static bool rkisp_tb_firstboot;
static struct clk **loader_clocks;
static int __maybe_unused rkisp_tb_clocks_loader_protect(void)
{
	int nclocks = ARRAY_SIZE(loader_protect_clocks);
	struct clk *clk;
	int i;

	loader_clocks = kcalloc(nclocks, sizeof(void *), GFP_KERNEL);
	if (!loader_clocks)
		return -ENOMEM;

	for (i = 0; i < nclocks; i++) {
		clk = __clk_lookup(loader_protect_clocks[i]);

		if (clk) {
			loader_clocks[i] = clk;
			clk_prepare_enable(clk);
		}
	}

	return 0;
}

static int __maybe_unused rkisp_tb_clocks_loader_unprotect(void)
{
	int i;

	if (!loader_clocks)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(loader_protect_clocks); i++) {
		struct clk *clk = loader_clocks[i];

		if (clk)
			clk_disable_unprepare(clk);
	}

	kfree(loader_clocks);
	loader_clocks = NULL;

	return 0;
}

static int __maybe_unused rkisp_tb_runtime_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused rkisp_tb_runtime_resume(struct device *dev)
{
	if (rkisp_tb_firstboot) {
		rkisp_tb_firstboot = false;
		rkisp_tb_clocks_loader_protect();
	}
	return 0;
}

static const struct dev_pm_ops rkisp_tb_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkisp_tb_runtime_suspend,
			   rkisp_tb_runtime_resume, NULL)
};

static const struct of_device_id rkisp_tb_plat_of_match[] = {
	{
		.compatible = "rockchip,thunder-boot-rkisp",
	},
	{},
};

static int rkisp_tb_plat_probe(struct platform_device *pdev)
{
	rkisp_tb_firstboot = true;
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	return 0;
}

static int rkisp_tb_plat_remove(struct platform_device *pdev)
{
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static struct platform_driver __maybe_unused rkisp_tb_plat_drv = {
	.driver = {
		.name = "rkisp_thunderboot",
		.of_match_table = of_match_ptr(rkisp_tb_plat_of_match),
		.pm = &rkisp_tb_plat_pm_ops,
	},
	.probe = rkisp_tb_plat_probe,
	.remove = rkisp_tb_plat_remove,
};

static int __init rkisp_tb_plat_drv_init(void)
{
	return platform_driver_register(&rkisp_tb_plat_drv);
}

arch_initcall_sync(rkisp_tb_plat_drv_init);

void rkisp_tb_unprotect_clk(void)
{
	rkisp_tb_clocks_loader_unprotect();
}
EXPORT_SYMBOL(rkisp_tb_unprotect_clk);

static enum rkisp_tb_state tb_state;

void rkisp_tb_set_state(enum rkisp_tb_state result)
{
	tb_state = result;
}
EXPORT_SYMBOL(rkisp_tb_set_state);

enum rkisp_tb_state rkisp_tb_get_state(void)
{
	return tb_state;
}
EXPORT_SYMBOL(rkisp_tb_get_state);
