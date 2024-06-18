// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/coresight.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "coresight-priv.h"

struct dummy_drvdata {
	struct device			*dev;
	struct coresight_device		*csdev;
};

DEFINE_CORESIGHT_DEVLIST(source_devs, "dummy_source");
DEFINE_CORESIGHT_DEVLIST(sink_devs, "dummy_sink");

static int dummy_source_enable(struct coresight_device *csdev,
			       struct perf_event *event, enum cs_mode mode)
{
	dev_dbg(csdev->dev.parent, "Dummy source enabled\n");

	return 0;
}

static void dummy_source_disable(struct coresight_device *csdev,
				 struct perf_event *event)
{
	dev_dbg(csdev->dev.parent, "Dummy source disabled\n");
}

static int dummy_sink_enable(struct coresight_device *csdev, enum cs_mode mode,
				void *data)
{
	dev_dbg(csdev->dev.parent, "Dummy sink enabled\n");

	return 0;
}

static int dummy_sink_disable(struct coresight_device *csdev)
{
	dev_dbg(csdev->dev.parent, "Dummy sink disabled\n");

	return 0;
}

static const struct coresight_ops_source dummy_source_ops = {
	.enable	= dummy_source_enable,
	.disable = dummy_source_disable,
};

static const struct coresight_ops dummy_source_cs_ops = {
	.source_ops = &dummy_source_ops,
};

static const struct coresight_ops_sink dummy_sink_ops = {
	.enable	= dummy_sink_enable,
	.disable = dummy_sink_disable,
};

static const struct coresight_ops dummy_sink_cs_ops = {
	.sink_ops = &dummy_sink_ops,
};

static int dummy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct coresight_platform_data *pdata;
	struct dummy_drvdata *drvdata;
	struct coresight_desc desc = { 0 };

	if (of_device_is_compatible(node, "arm,coresight-dummy-source")) {

		desc.name = coresight_alloc_device_name(&source_devs, dev);
		if (!desc.name)
			return -ENOMEM;

		desc.type = CORESIGHT_DEV_TYPE_SOURCE;
		desc.subtype.source_subtype =
					CORESIGHT_DEV_SUBTYPE_SOURCE_OTHERS;
		desc.ops = &dummy_source_cs_ops;
	} else if (of_device_is_compatible(node, "arm,coresight-dummy-sink")) {
		desc.name = coresight_alloc_device_name(&sink_devs, dev);
		if (!desc.name)
			return -ENOMEM;

		desc.type = CORESIGHT_DEV_TYPE_SINK;
		desc.subtype.sink_subtype = CORESIGHT_DEV_SUBTYPE_SINK_DUMMY;
		desc.ops = &dummy_sink_cs_ops;
	} else {
		dev_err(dev, "Device type not set\n");
		return -EINVAL;
	}

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	pdev->dev.platform_data = pdata;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &pdev->dev;
	platform_set_drvdata(pdev, drvdata);

	desc.pdata = pdev->dev.platform_data;
	desc.dev = &pdev->dev;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	pm_runtime_enable(dev);
	dev_dbg(dev, "Dummy device initialized\n");

	return 0;
}

static void dummy_remove(struct platform_device *pdev)
{
	struct dummy_drvdata *drvdata = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	pm_runtime_disable(dev);
	coresight_unregister(drvdata->csdev);
}

static const struct of_device_id dummy_match[] = {
	{.compatible = "arm,coresight-dummy-source"},
	{.compatible = "arm,coresight-dummy-sink"},
	{},
};

static struct platform_driver dummy_driver = {
	.probe	= dummy_probe,
	.remove_new = dummy_remove,
	.driver	= {
		.name   = "coresight-dummy",
		.of_match_table = dummy_match,
	},
};

module_platform_driver(dummy_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CoreSight dummy driver");
