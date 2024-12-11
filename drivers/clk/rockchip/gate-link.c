// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2024 Collabora Ltd.
 * Author: Sebastian Reichel <sebastian.reichel@collabora.com>
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include "clk.h"

static int rk_clk_gate_link_register(struct device *dev,
				     struct rockchip_clk_provider *ctx,
				     struct rockchip_clk_branch *clkbr)
{
	unsigned long flags = clkbr->flags | CLK_SET_RATE_PARENT;
	struct clk *clk;

	clk = clk_register_gate(dev, clkbr->name, clkbr->parent_names[0],
				flags, ctx->reg_base + clkbr->gate_offset,
				clkbr->gate_shift, clkbr->gate_flags,
				&ctx->lock);

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	rockchip_clk_set_lookup(ctx, clk, clkbr->id);
	return 0;
}

static int rk_clk_gate_link_probe(struct platform_device *pdev)
{
	struct rockchip_gate_link_platdata *pdata;
	struct device *dev = &pdev->dev;
	struct clk *linked_clk;
	int ret;

	pdata = dev_get_platdata(dev);
	if (!pdata)
		return dev_err_probe(dev, -ENODEV, "missing platform data");

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = devm_pm_clk_create(dev);
	if (ret)
		return ret;

	linked_clk = rockchip_clk_get_lookup(pdata->ctx, pdata->clkbr->linked_clk_id);
	ret = pm_clk_add_clk(dev, linked_clk);
	if (ret)
		return ret;

	ret = rk_clk_gate_link_register(dev, pdata->ctx, pdata->clkbr);
	if (ret)
		goto err;

	return 0;

err:
	pm_clk_remove_clk(dev, linked_clk);
	return ret;
}

static const struct dev_pm_ops rk_clk_gate_link_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static struct platform_driver rk_clk_gate_link_driver = {
	.probe		= rk_clk_gate_link_probe,
	.driver		= {
		.name	= "rockchip-gate-link-clk",
		.pm = &rk_clk_gate_link_pm_ops,
		.suppress_bind_attrs = true,
	},
};

static int __init rk_clk_gate_link_drv_register(void)
{
	return platform_driver_register(&rk_clk_gate_link_driver);
}
core_initcall(rk_clk_gate_link_drv_register);
