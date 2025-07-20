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
#include "coresight-trace-id.h"

struct dummy_drvdata {
	struct device			*dev;
	struct coresight_device		*csdev;
	u8				traceid;
};

DEFINE_CORESIGHT_DEVLIST(source_devs, "dummy_source");
DEFINE_CORESIGHT_DEVLIST(sink_devs, "dummy_sink");

static int dummy_source_enable(struct coresight_device *csdev,
			       struct perf_event *event, enum cs_mode mode,
			       __maybe_unused struct coresight_path *path)
{
	if (!coresight_take_mode(csdev, mode))
		return -EBUSY;

	dev_dbg(csdev->dev.parent, "Dummy source enabled\n");

	return 0;
}

static void dummy_source_disable(struct coresight_device *csdev,
				 struct perf_event *event)
{
	coresight_set_mode(csdev, CS_MODE_DISABLED);
	dev_dbg(csdev->dev.parent, "Dummy source disabled\n");
}

static int dummy_source_trace_id(struct coresight_device *csdev, __maybe_unused enum cs_mode mode,
				 __maybe_unused struct coresight_device *sink)
{
	struct dummy_drvdata *drvdata;

	drvdata = dev_get_drvdata(csdev->dev.parent);

	return drvdata->traceid;
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
	.trace_id	= dummy_source_trace_id,
	.source_ops	= &dummy_source_ops,
};

static const struct coresight_ops_sink dummy_sink_ops = {
	.enable	= dummy_sink_enable,
	.disable = dummy_sink_disable,
};

static const struct coresight_ops dummy_sink_cs_ops = {
	.sink_ops = &dummy_sink_ops,
};

/* User can get the trace id of dummy source from this node. */
static ssize_t traceid_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct dummy_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->traceid;
	return sysfs_emit(buf, "%#lx\n", val);
}
static DEVICE_ATTR_RO(traceid);

static struct attribute *coresight_dummy_attrs[] = {
	&dev_attr_traceid.attr,
	NULL,
};

static const struct attribute_group coresight_dummy_group = {
	.attrs = coresight_dummy_attrs,
};

static const struct attribute_group *coresight_dummy_groups[] = {
	&coresight_dummy_group,
	NULL,
};

static int dummy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct coresight_platform_data *pdata;
	struct dummy_drvdata *drvdata;
	struct coresight_desc desc = { 0 };
	int ret = 0, trace_id = 0;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	if (of_device_is_compatible(node, "arm,coresight-dummy-source")) {

		desc.name = coresight_alloc_device_name(&source_devs, dev);
		if (!desc.name)
			return -ENOMEM;

		desc.type = CORESIGHT_DEV_TYPE_SOURCE;
		desc.subtype.source_subtype =
					CORESIGHT_DEV_SUBTYPE_SOURCE_OTHERS;
		desc.ops = &dummy_source_cs_ops;
		desc.groups = coresight_dummy_groups;

		ret = coresight_get_static_trace_id(dev, &trace_id);
		if (!ret) {
			/* Get the static id if id is set in device tree. */
			ret = coresight_trace_id_get_static_system_id(trace_id);
			if (ret < 0) {
				dev_err(dev, "Fail to get static id.\n");
				return ret;
			}
		} else {
			/* Get next available id if id is not set in device tree. */
			trace_id = coresight_trace_id_get_system_id();
			if (trace_id < 0) {
				ret = trace_id;
				return ret;
			}
		}
		drvdata->traceid = (u8)trace_id;

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
	if (IS_ERR(pdata)) {
		ret = PTR_ERR(pdata);
		goto free_id;
	}
	pdev->dev.platform_data = pdata;

	drvdata->dev = &pdev->dev;
	platform_set_drvdata(pdev, drvdata);

	desc.pdata = pdev->dev.platform_data;
	desc.dev = &pdev->dev;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto free_id;
	}

	pm_runtime_enable(dev);
	dev_dbg(dev, "Dummy device initialized\n");

	ret = 0;
	goto out;

free_id:
	if (IS_VALID_CS_TRACE_ID(drvdata->traceid))
		coresight_trace_id_put_system_id(drvdata->traceid);

out:
	return ret;
}

static void dummy_remove(struct platform_device *pdev)
{
	struct dummy_drvdata *drvdata = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	if (IS_VALID_CS_TRACE_ID(drvdata->traceid))
		coresight_trace_id_put_system_id(drvdata->traceid);
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
	.remove = dummy_remove,
	.driver	= {
		.name   = "coresight-dummy",
		.of_match_table = dummy_match,
	},
};

module_platform_driver(dummy_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CoreSight dummy driver");
