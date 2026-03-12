// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

 #include <linux/amba/bus.h>
 #include <linux/coresight.h>
 #include <linux/device.h>
 #include <linux/io.h>
 #include <linux/kernel.h>
 #include <linux/module.h>
 #include <linux/of.h>
 #include <linux/platform_device.h>

#include "coresight-priv.h"
#include "coresight-trace-id.h"

#define TRACE_NOC_CTRL      0x008
#define TRACE_NOC_XLD       0x010
#define TRACE_NOC_FREQVAL   0x018
#define TRACE_NOC_SYNCR     0x020

/* Enable generation of output ATB traffic.*/
#define TRACE_NOC_CTRL_PORTEN   BIT(0)
/* Sets the type of issued ATB FLAG packets.*/
#define TRACE_NOC_CTRL_FLAGTYPE BIT(7)
/* Sets the type of issued ATB FREQ packet*/
#define TRACE_NOC_CTRL_FREQTYPE BIT(8)

#define TRACE_NOC_SYNC_INTERVAL	0xFFFF

/*
 * struct trace_noc_drvdata - specifics associated to a trace noc component
 * @base:      memory mapped base address for this component.
 * @dev:       device node for trace_noc_drvdata.
 * @csdev:     component vitals needed by the framework.
 * @pclk:	APB clock if present, otherwise NULL
 * @spinlock:  serialize enable/disable operation.
 * @atid:      id for the trace packet.
 */
struct trace_noc_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	struct clk		*pclk;
	spinlock_t		spinlock;
	int			atid;
};

DEFINE_CORESIGHT_DEVLIST(trace_noc_devs, "traceNoc");

static void trace_noc_enable_hw(struct trace_noc_drvdata *drvdata)
{
	u32 val;

	/* No valid ATID, simply enable the unit */
	if (drvdata->atid == -EOPNOTSUPP) {
		writel(TRACE_NOC_CTRL_PORTEN, drvdata->base + TRACE_NOC_CTRL);
		return;
	}

	/* Set ATID */
	writel_relaxed(drvdata->atid, drvdata->base + TRACE_NOC_XLD);

	/* Set the data word count between 'SYNC' packets */
	writel_relaxed(TRACE_NOC_SYNC_INTERVAL, drvdata->base + TRACE_NOC_SYNCR);

	/* Set the Control register:
	 * - Set the FLAG packets to 'FLAG' packets
	 * - Set the FREQ packets to 'FREQ_TS' packets
	 * - Enable generation of output ATB traffic
	 */

	val = readl_relaxed(drvdata->base + TRACE_NOC_CTRL);

	val &= ~TRACE_NOC_CTRL_FLAGTYPE;
	val |= TRACE_NOC_CTRL_FREQTYPE;
	val |= TRACE_NOC_CTRL_PORTEN;

	writel(val, drvdata->base + TRACE_NOC_CTRL);
}

static int trace_noc_enable(struct coresight_device *csdev, struct coresight_connection *inport,
			    struct coresight_connection *outport)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	scoped_guard(spinlock, &drvdata->spinlock) {
		if (csdev->refcnt == 0)
			trace_noc_enable_hw(drvdata);

		csdev->refcnt++;
	}

	dev_dbg(drvdata->dev, "Trace NOC is enabled\n");
	return 0;
}

static void trace_noc_disable(struct coresight_device *csdev, struct coresight_connection *inport,
			      struct coresight_connection *outport)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	scoped_guard(spinlock, &drvdata->spinlock) {
		if (--csdev->refcnt == 0)
			writel(0x0, drvdata->base + TRACE_NOC_CTRL);
	}
	dev_dbg(drvdata->dev, "Trace NOC is disabled\n");
}

static int trace_noc_id(struct coresight_device *csdev, __maybe_unused enum cs_mode mode,
			__maybe_unused struct coresight_device *sink)
{
	struct trace_noc_drvdata *drvdata;

	drvdata = dev_get_drvdata(csdev->dev.parent);

	return drvdata->atid;
}

static const struct coresight_ops_link trace_noc_link_ops = {
	.enable		= trace_noc_enable,
	.disable	= trace_noc_disable,
};

static const struct coresight_ops trace_noc_cs_ops = {
	.trace_id	= trace_noc_id,
	.link_ops	= &trace_noc_link_ops,
};

static int trace_noc_init_default_data(struct trace_noc_drvdata *drvdata)
{
	int atid;

	if (!dev_is_amba(drvdata->dev)) {
		drvdata->atid = -EOPNOTSUPP;
		return 0;
	}

	atid = coresight_trace_id_get_system_id();
	if (atid < 0)
		return atid;

	drvdata->atid = atid;

	return 0;
}

static ssize_t traceid_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->atid;
	return sprintf(buf, "%#lx\n", val);
}
static DEVICE_ATTR_RO(traceid);

static struct attribute *coresight_tnoc_attrs[] = {
	&dev_attr_traceid.attr,
	NULL,
};

static umode_t trace_id_is_visible(struct kobject *kobj,
				   struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (attr == &dev_attr_traceid.attr && drvdata->atid < 0)
		return 0;

	return attr->mode;
}

static const struct attribute_group coresight_tnoc_group = {
	.attrs = coresight_tnoc_attrs,
	.is_visible = trace_id_is_visible,
};

static const struct attribute_group *coresight_tnoc_groups[] = {
	&coresight_tnoc_group,
	NULL,
};

static int _tnoc_probe(struct device *dev, struct resource *res)
{
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
	dev->platform_data = pdata;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = dev;
	dev_set_drvdata(dev, drvdata);

	ret = coresight_get_enable_clocks(dev, &drvdata->pclk, NULL);
	if (ret)
		return ret;

	drvdata->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	spin_lock_init(&drvdata->spinlock);

	ret = trace_noc_init_default_data(drvdata);
	if (ret)
		return ret;

	desc.ops = &trace_noc_cs_ops;
	desc.type = CORESIGHT_DEV_TYPE_LINK;
	desc.subtype.link_subtype = CORESIGHT_DEV_SUBTYPE_LINK_MERG;
	desc.pdata = pdata;
	desc.dev = dev;
	desc.access = CSDEV_ACCESS_IOMEM(drvdata->base);
	desc.groups = coresight_tnoc_groups;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev)) {
		if (drvdata->atid > 0)
			coresight_trace_id_put_system_id(drvdata->atid);
		return PTR_ERR(drvdata->csdev);
	}

	return 0;
}

static int trace_noc_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;

	ret = _tnoc_probe(&adev->dev, &adev->res);
	if (!ret)
		pm_runtime_put(&adev->dev);

	return ret;
}

static void trace_noc_remove(struct amba_device *adev)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(&adev->dev);

	coresight_unregister(drvdata->csdev);
	coresight_trace_id_put_system_id(drvdata->atid);
}

static struct amba_id trace_noc_ids[] = {
	{
		.id     = 0x000f0c00,
		.mask   = 0x00ffff00,
	},
	{
		.id     = 0x001f0c00,
		.mask   = 0x00ffff00,
	},
	{},
};
MODULE_DEVICE_TABLE(amba, trace_noc_ids);

static struct amba_driver trace_noc_driver = {
	.drv = {
		.name   = "coresight-trace-noc",
		.suppress_bind_attrs = true,
	},
	.probe          = trace_noc_probe,
	.remove		= trace_noc_remove,
	.id_table	= trace_noc_ids,
};

static int itnoc_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int ret;

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = _tnoc_probe(&pdev->dev, res);
	pm_runtime_put(&pdev->dev);
	if (ret)
		pm_runtime_disable(&pdev->dev);

	return ret;
}

static void itnoc_remove(struct platform_device *pdev)
{
	struct trace_noc_drvdata *drvdata = platform_get_drvdata(pdev);

	coresight_unregister(drvdata->csdev);
	pm_runtime_disable(&pdev->dev);
}

#ifdef CONFIG_PM
static int itnoc_runtime_suspend(struct device *dev)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev);

	clk_disable_unprepare(drvdata->pclk);

	return 0;
}

static int itnoc_runtime_resume(struct device *dev)
{
	struct trace_noc_drvdata *drvdata = dev_get_drvdata(dev);

	return clk_prepare_enable(drvdata->pclk);
}
#endif

static const struct dev_pm_ops itnoc_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(itnoc_runtime_suspend, itnoc_runtime_resume, NULL)
};

static const struct of_device_id itnoc_of_match[] = {
	{ .compatible = "qcom,coresight-itnoc" },
	{}
};
MODULE_DEVICE_TABLE(of, itnoc_of_match);

static struct platform_driver itnoc_driver = {
	.probe = itnoc_probe,
	.remove = itnoc_remove,
	.driver = {
		.name = "coresight-itnoc",
		.of_match_table = itnoc_of_match,
		.suppress_bind_attrs = true,
		.pm = &itnoc_dev_pm_ops,
	},
};

static int __init tnoc_init(void)
{
	return coresight_init_driver("tnoc", &trace_noc_driver, &itnoc_driver, THIS_MODULE);
}

static void __exit tnoc_exit(void)
{
	coresight_remove_driver(&trace_noc_driver, &itnoc_driver);
}
module_init(tnoc_init);
module_exit(tnoc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Trace NOC driver");
