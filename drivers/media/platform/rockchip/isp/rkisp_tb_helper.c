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

static struct platform_device *rkisp_tb_pdev;
static int __maybe_unused rkisp_tb_clocks_loader_protect(void)
{
	if (rkisp_tb_pdev) {
		pm_runtime_enable(&rkisp_tb_pdev->dev);
		pm_runtime_get_sync(&rkisp_tb_pdev->dev);
	}
	return 0;
}

static int __maybe_unused rkisp_tb_clocks_loader_unprotect(void)
{
	if (rkisp_tb_pdev) {
		pm_runtime_put_sync(&rkisp_tb_pdev->dev);
		pm_runtime_disable(&rkisp_tb_pdev->dev);
	}
	return 0;
}

static int __maybe_unused rkisp_tb_runtime_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused rkisp_tb_runtime_resume(struct device *dev)
{
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
	rkisp_tb_pdev = pdev;
	rkisp_tb_clocks_loader_protect();
	return 0;
}

static int rkisp_tb_plat_remove(struct platform_device *pdev)
{
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
