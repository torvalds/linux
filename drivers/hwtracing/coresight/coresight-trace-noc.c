// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/io.h>
#include <linux/coresight.h>
#include <linux/of.h>

#include "coresight-priv.h"
#include "coresight-common.h"
#include "coresight-trace-noc.h"

static ssize_t flush_req_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;
	u32 reg;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (!drvdata->enable) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	if (val) {
		reg = readl_relaxed(drvdata->base + TRACE_NOC_CTRL);
		reg = reg | TRACE_NOC_CTRL_FLUSHREQ;
		writel_relaxed(reg, drvdata->base + TRACE_NOC_CTRL);
	}
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(flush_req);

static ssize_t flush_status_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);
	u32 val;

	spin_lock(&drvdata->spinlock);
	if (!drvdata->enable) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	val = readl_relaxed(drvdata->base + TRACE_NOC_CTRL);
	spin_unlock(&drvdata->spinlock);
	return scnprintf(buf, PAGE_SIZE, "%x\n", BMVAL(val, 2, 2));
}
static DEVICE_ATTR_RO(flush_status);

static ssize_t flag_type_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "%x\n", drvdata->flagType);
}

static ssize_t flag_type_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t size)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (val)
		drvdata->flagType = FLAG_TS;
	else
		drvdata->flagType = FLAG;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(flag_type);

static ssize_t freq_type_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "%x\n", drvdata->freqType);
}

static ssize_t freq_type_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t size)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (val)
		drvdata->freqType = FREQ_TS;
	else
		drvdata->freqType = FREQ;
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(freq_type);

static ssize_t freq_req_val_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "%x\n", drvdata->freq_req_val);
}

static ssize_t freq_req_val_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t size)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val) {
		spin_lock(&drvdata->spinlock);
		drvdata->freq_req_val = val;
		spin_unlock(&drvdata->spinlock);
	}

	return size;
}
static DEVICE_ATTR_RW(freq_req_val);

static ssize_t freq_ts_req_store(struct device *dev,
					  struct device_attribute *attr,
					const char *buf,
					size_t size)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;
	u32 reg;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock(&drvdata->spinlock);
	if (!drvdata->enable) {
		spin_unlock(&drvdata->spinlock);
		return -EPERM;
	}

	if (val) {
		reg = readl_relaxed(drvdata->base + TRACE_NOC_CTRL);
		reg = reg | TRACE_NOC_CTRL_FREQTSREQ;
		writel_relaxed(reg, drvdata->base + TRACE_NOC_CTRL);
	}
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_WO(freq_ts_req);

static struct attribute *trace_noc_attrs[] = {
	&dev_attr_flush_req.attr,
	&dev_attr_flush_status.attr,
	&dev_attr_flag_type.attr,
	&dev_attr_freq_type.attr,
	&dev_attr_freq_req_val.attr,
	&dev_attr_freq_ts_req.attr,
	NULL,
};

static struct attribute_group trace_noc_attr_grp = {
	.attrs = trace_noc_attrs,
};

static const struct attribute_group *trace_noc_attr_grps[] = {
	&trace_noc_attr_grp,
	NULL,
};

static int trace_noc_enable(struct coresight_device *csdev, int inport, int outport)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	int ret;
	u32 val;

	ret = pm_runtime_get_sync(drvdata->dev);
	if (ret < 0) {
		pm_runtime_put(drvdata->dev);
		return ret;
	}

	spin_lock(&drvdata->spinlock);
	/* Set ATID */
	writel_relaxed(drvdata->atid, drvdata->base + TRACE_NOC_XLD);

	/* Config sync CR */
	writel_relaxed(0xffff, drvdata->base + TRACE_NOC_SYNCR);

	/* Set frequency value */
	if (drvdata->freq_req_val)
		writel_relaxed(drvdata->freq_req_val,
				drvdata->base + TRACE_NOC_FREQVAL);

	/* Set Ctrl register */
	val = readl_relaxed(drvdata->base + TRACE_NOC_CTRL);
	if (drvdata->flagType == FLAG_TS)
		val = val | TRACE_NOC_CTRL_FLAGTYPE;
	else
		val = val & ~TRACE_NOC_CTRL_FLAGTYPE;
	if (drvdata->freqType == FREQ_TS)
		val = val | TRACE_NOC_CTRL_FREQTYPE;
	else
		val = val & ~TRACE_NOC_CTRL_FREQTYPE;
	val = val | TRACE_NOC_CTRL_PORTEN;
	writel_relaxed(val, drvdata->base + TRACE_NOC_CTRL);

	drvdata->enable = true;
	spin_unlock(&drvdata->spinlock);

	dev_info(drvdata->dev, "Trace NOC is enabled\n");
	return 0;
}

static void trace_noc_disable(struct coresight_device *csdev, int inport, int outport)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock(&drvdata->spinlock);
	writel_relaxed(0x0, drvdata->base + TRACE_NOC_CTRL);
	drvdata->enable = false;
	spin_unlock(&drvdata->spinlock);

	pm_runtime_put(drvdata->dev);
	dev_info(drvdata->dev, "Trace NOC is disabled\n");
}

static const struct coresight_ops_link trace_noc_link_ops = {
	.enable		= trace_noc_enable,
	.disable	= trace_noc_disable,
};

static const struct coresight_ops trace_noc_cs_ops = {
	.link_ops	= &trace_noc_link_ops,
};

static int trace_noc_parse_of_data(struct trace_noc_drvdata *drvdata)
{
	int ret;
	struct device_node *node = drvdata->dev->of_node;

	ret = of_property_read_u32(node, "atid", &drvdata->atid);
	if (ret) {
		dev_err(drvdata->dev, "Trace Noc ATID is not specified\n");
		return -EINVAL;
	}

	dev_dbg(drvdata->dev, "Trace Noc ATID is %d\n", drvdata->atid);
	return 0;
}

static void trace_noc_init_default_data(struct trace_noc_drvdata *drvdata)
{
	drvdata->freqType = FREQ_TS;
	drvdata->freqTsReq = true;
}

static int trace_noc_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata;
	struct trace_noc_drvdata *drvdata;
	struct coresight_desc desc = { 0 };
	int ret;

	desc.name = coresight_alloc_device_name(&trace_noc_devs, dev);
	if (!desc.name)
		return -ENOMEM;
	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	adev->dev.platform_data = pdata;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	drvdata->base = devm_ioremap_resource(dev, &adev->res);
	if (!drvdata->base)
		return -ENOMEM;

	ret = trace_noc_parse_of_data(drvdata);
	if (ret)
		return ret;

	desc.type = CORESIGHT_DEV_TYPE_LINK;
	desc.subtype.link_subtype = CORESIGHT_DEV_SUBTYPE_LINK_MERG;
	desc.ops = &trace_noc_cs_ops;
	desc.pdata = adev->dev.platform_data;
	desc.dev = &adev->dev;
	desc.groups = trace_noc_attr_grps;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	pm_runtime_put(&adev->dev);

	spin_lock_init(&drvdata->spinlock);
	trace_noc_init_default_data(drvdata);

	dev_dbg(drvdata->dev, "Trace Noc initialized\n");
	return 0;
}

static void __exit trace_noc_remove(struct amba_device *adev)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(&adev->dev);

	coresight_unregister(drvdata->csdev);
}

static struct amba_id trace_noc_ids[] = {
	{
		.id     = 0x000f0c00,
		.mask   = 0x000fff00,
	},
	{ 0, 0},
};
MODULE_DEVICE_TABLE(amba, trace_noc_ids);

static struct amba_driver trace_noc_driver = {
	.drv = {
		.name   = "coresight-trace-noc",
		.owner	= THIS_MODULE,
		.suppress_bind_attrs = true,
	},
	.probe          = trace_noc_probe,
	.remove		= trace_noc_remove,
	.id_table	= trace_noc_ids,
};

module_amba_driver(trace_noc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Trace NOC driver");
