// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include "dc-drv.h"
#include "dc-fu.h"
#include "dc-pe.h"

static int dc_pe_bind(struct device *dev, struct device *master, void *data)
{
	struct dc_drm_device *dc_drm = data;
	struct dc_pe *pe;
	int ret;

	pe = devm_kzalloc(dev, sizeof(*pe), GFP_KERNEL);
	if (!pe)
		return -ENOMEM;

	pe->clk_axi = devm_clk_get(dev, NULL);
	if (IS_ERR(pe->clk_axi))
		return dev_err_probe(dev, PTR_ERR(pe->clk_axi),
				     "failed to get AXI clock\n");

	pe->dev = dev;

	dev_set_drvdata(dev, pe);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	dc_drm->pe = pe;

	return 0;
}

/*
 * It's possible to get the child device pointers from the child component
 * bind callbacks, but it depends on the component helper behavior to bind
 * the pixel engine component first.  To avoid the dependency, post bind to
 * get the pointers from dc_drm in a safe manner.
 */
void dc_pe_post_bind(struct dc_drm_device *dc_drm)
{
	struct dc_pe *pe = dc_drm->pe;
	int i;

	for (i = 0; i < DC_DISPLAYS; i++) {
		pe->cf_safe[i] = dc_drm->cf_safe[i];
		pe->cf_cont[i] = dc_drm->cf_cont[i];
		pe->ed_safe[i] = dc_drm->ed_safe[i];
		pe->ed_cont[i] = dc_drm->ed_cont[i];
	}

	for (i = 0; i < DC_DISP_FU_CNT; i++)
		pe->fu_disp[i] = dc_drm->fu_disp[i];

	for (i = 0; i < DC_LB_CNT; i++)
		pe->lb[i] = dc_drm->lb[i];
}

static const struct component_ops dc_pe_ops = {
	.bind = dc_pe_bind,
};

static int dc_pe_probe(struct platform_device *pdev)
{
	int ret;

	ret = devm_of_platform_populate(&pdev->dev);
	if (ret < 0)
		return ret;

	ret = component_add(&pdev->dev, &dc_pe_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to add component\n");

	return 0;
}

static void dc_pe_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dc_pe_ops);
}

static int dc_pe_runtime_suspend(struct device *dev)
{
	struct dc_pe *pe = dev_get_drvdata(dev);

	clk_disable_unprepare(pe->clk_axi);

	return 0;
}

static int dc_pe_runtime_resume(struct device *dev)
{
	struct dc_pe *pe = dev_get_drvdata(dev);
	int i, ret;

	ret = clk_prepare_enable(pe->clk_axi);
	if (ret) {
		dev_err(dev, "failed to enable AXI clock: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(pe->cf_safe); i++)
		dc_cf_init(pe->cf_safe[i]);

	for (i = 0; i < ARRAY_SIZE(pe->cf_cont); i++)
		dc_cf_init(pe->cf_cont[i]);

	for (i = 0; i < ARRAY_SIZE(pe->ed_safe); i++)
		dc_ed_init(pe->ed_safe[i]);

	for (i = 0; i < ARRAY_SIZE(pe->ed_cont); i++)
		dc_ed_init(pe->ed_cont[i]);

	for (i = 0; i < ARRAY_SIZE(pe->fu_disp); i++)
		pe->fu_disp[i]->ops.init(pe->fu_disp[i]);

	for (i = 0; i < ARRAY_SIZE(pe->lb); i++)
		dc_lb_init(pe->lb[i]);

	return 0;
}

static const struct dev_pm_ops dc_pe_pm_ops = {
	RUNTIME_PM_OPS(dc_pe_runtime_suspend, dc_pe_runtime_resume, NULL)
};

static const struct of_device_id dc_pe_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-dc-pixel-engine", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_pe_dt_ids);

struct platform_driver dc_pe_driver = {
	.probe = dc_pe_probe,
	.remove = dc_pe_remove,
	.driver = {
		.name = "imx8-dc-pixel-engine",
		.suppress_bind_attrs = true,
		.of_match_table = dc_pe_dt_ids,
		.pm = pm_sleep_ptr(&dc_pe_pm_ops),
	},
};
