/* Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/coresight.h>

#include "coresight-priv.h"

/**
 * struct replicator_drvdata - specifics associated to a replicator component
 * @dev:	the device entity associated with this component
 * @atclk:	optional clock for the core parts of the replicator.
 * @csdev:	component vitals needed by the framework
 */
struct replicator_drvdata {
	struct device		*dev;
	struct clk		*atclk;
	struct coresight_device	*csdev;
};

static int replicator_enable(struct coresight_device *csdev, int inport,
			     int outport)
{
	struct replicator_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	pm_runtime_get_sync(drvdata->dev);
	dev_info(drvdata->dev, "REPLICATOR enabled\n");
	return 0;
}

static void replicator_disable(struct coresight_device *csdev, int inport,
			       int outport)
{
	struct replicator_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	pm_runtime_put(drvdata->dev);
	dev_info(drvdata->dev, "REPLICATOR disabled\n");
}

static const struct coresight_ops_link replicator_link_ops = {
	.enable		= replicator_enable,
	.disable	= replicator_disable,
};

static const struct coresight_ops replicator_cs_ops = {
	.link_ops	= &replicator_link_ops,
};

static int replicator_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct coresight_platform_data *pdata = NULL;
	struct replicator_drvdata *drvdata;
	struct coresight_desc *desc;
	struct device_node *np = pdev->dev.of_node;

	if (np) {
		pdata = of_get_coresight_platform_data(dev, np);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
		pdev->dev.platform_data = pdata;
	}

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &pdev->dev;
	drvdata->atclk = devm_clk_get(&pdev->dev, "atclk"); /* optional */
	if (!IS_ERR(drvdata->atclk)) {
		ret = clk_prepare_enable(drvdata->atclk);
		if (ret)
			return ret;
	}
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	platform_set_drvdata(pdev, drvdata);

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc) {
		ret = -ENOMEM;
		goto out_disable_pm;
	}

	desc->type = CORESIGHT_DEV_TYPE_LINK;
	desc->subtype.link_subtype = CORESIGHT_DEV_SUBTYPE_LINK_SPLIT;
	desc->ops = &replicator_cs_ops;
	desc->pdata = pdev->dev.platform_data;
	desc->dev = &pdev->dev;
	drvdata->csdev = coresight_register(desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto out_disable_pm;
	}

	pm_runtime_put(&pdev->dev);

	dev_info(dev, "REPLICATOR initialized\n");
	return 0;

out_disable_pm:
	if (!IS_ERR(drvdata->atclk))
		clk_disable_unprepare(drvdata->atclk);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return ret;
}

#ifdef CONFIG_PM
static int replicator_runtime_suspend(struct device *dev)
{
	struct replicator_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata && !IS_ERR(drvdata->atclk))
		clk_disable_unprepare(drvdata->atclk);

	return 0;
}

static int replicator_runtime_resume(struct device *dev)
{
	struct replicator_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata && !IS_ERR(drvdata->atclk))
		clk_prepare_enable(drvdata->atclk);

	return 0;
}
#endif

static const struct dev_pm_ops replicator_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(replicator_runtime_suspend,
			   replicator_runtime_resume, NULL)
};

static const struct of_device_id replicator_match[] = {
	{.compatible = "arm,coresight-replicator"},
	{}
};

static struct platform_driver replicator_driver = {
	.probe          = replicator_probe,
	.driver         = {
		.name   = "coresight-replicator",
		.of_match_table = replicator_match,
		.pm	= &replicator_dev_pm_ops,
		.suppress_bind_attrs = true,
	},
};

builtin_platform_driver(replicator_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CoreSight Replicator driver");
