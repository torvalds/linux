// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/amba/bus.h>
#include <linux/bitfield.h>
#include <linux/coresight.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "coresight-priv.h"
#include "coresight-tpda.h"
#include "coresight-trace-id.h"

DEFINE_CORESIGHT_DEVLIST(tpda_devs, "tpda");

/* Settings pre enabling port control register */
static void tpda_enable_pre_port(struct tpda_drvdata *drvdata)
{
	u32 val;

	val = readl_relaxed(drvdata->base + TPDA_CR);
	val &= ~TPDA_CR_ATID;
	val |= FIELD_PREP(TPDA_CR_ATID, drvdata->atid);
	writel_relaxed(val, drvdata->base + TPDA_CR);
}

static void tpda_enable_port(struct tpda_drvdata *drvdata, int port)
{
	u32 val;

	val = readl_relaxed(drvdata->base + TPDA_Pn_CR(port));
	/* Enable the port */
	val |= TPDA_Pn_CR_ENA;
	writel_relaxed(val, drvdata->base + TPDA_Pn_CR(port));
}

static void __tpda_enable(struct tpda_drvdata *drvdata, int port)
{
	CS_UNLOCK(drvdata->base);

	if (!drvdata->csdev->enable)
		tpda_enable_pre_port(drvdata);

	tpda_enable_port(drvdata, port);

	CS_LOCK(drvdata->base);
}

static int tpda_enable(struct coresight_device *csdev, int inport, int outport)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock(&drvdata->spinlock);
	if (atomic_read(&csdev->refcnt[inport]) == 0)
		__tpda_enable(drvdata, inport);

	atomic_inc(&csdev->refcnt[inport]);
	spin_unlock(&drvdata->spinlock);

	dev_dbg(drvdata->dev, "TPDA inport %d enabled.\n", inport);
	return 0;
}

static void __tpda_disable(struct tpda_drvdata *drvdata, int port)
{
	u32 val;

	CS_UNLOCK(drvdata->base);

	val = readl_relaxed(drvdata->base + TPDA_Pn_CR(port));
	val &= ~TPDA_Pn_CR_ENA;
	writel_relaxed(val, drvdata->base + TPDA_Pn_CR(port));

	CS_LOCK(drvdata->base);
}

static void tpda_disable(struct coresight_device *csdev, int inport,
			   int outport)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock(&drvdata->spinlock);
	if (atomic_dec_return(&csdev->refcnt[inport]) == 0)
		__tpda_disable(drvdata, inport);

	spin_unlock(&drvdata->spinlock);

	dev_dbg(drvdata->dev, "TPDA inport %d disabled\n", inport);
}

static const struct coresight_ops_link tpda_link_ops = {
	.enable		= tpda_enable,
	.disable	= tpda_disable,
};

static const struct coresight_ops tpda_cs_ops = {
	.link_ops	= &tpda_link_ops,
};

static int tpda_init_default_data(struct tpda_drvdata *drvdata)
{
	int atid;
	/*
	 * TPDA must has a unique atid. This atid can uniquely
	 * identify the TPDM trace source connected to the TPDA.
	 * The TPDMs which are connected to same TPDA share the
	 * same trace-id. When TPDA does packetization, different
	 * port will have unique channel number for decoding.
	 */
	atid = coresight_trace_id_get_system_id();
	if (atid < 0)
		return atid;

	drvdata->atid = atid;
	return 0;
}

static int tpda_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata;
	struct tpda_drvdata *drvdata;
	struct coresight_desc desc = { 0 };
	void __iomem *base;

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	adev->dev.platform_data = pdata;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	base = devm_ioremap_resource(dev, &adev->res);
	if (IS_ERR(base))
		return PTR_ERR(base);
	drvdata->base = base;

	spin_lock_init(&drvdata->spinlock);

	ret = tpda_init_default_data(drvdata);
	if (ret)
		return ret;

	desc.name = coresight_alloc_device_name(&tpda_devs, dev);
	if (!desc.name)
		return -ENOMEM;
	desc.type = CORESIGHT_DEV_TYPE_LINK;
	desc.subtype.link_subtype = CORESIGHT_DEV_SUBTYPE_LINK_MERG;
	desc.ops = &tpda_cs_ops;
	desc.pdata = adev->dev.platform_data;
	desc.dev = &adev->dev;
	desc.access = CSDEV_ACCESS_IOMEM(base);
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	pm_runtime_put(&adev->dev);

	dev_dbg(drvdata->dev, "TPDA initialized\n");
	return 0;
}

static void tpda_remove(struct amba_device *adev)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(&adev->dev);

	coresight_trace_id_put_system_id(drvdata->atid);
	coresight_unregister(drvdata->csdev);
}

/*
 * Different TPDA has different periph id.
 * The difference is 0-7 bits' value. So ignore 0-7 bits.
 */
static struct amba_id tpda_ids[] = {
	{
		.id     = 0x000f0f00,
		.mask   = 0x000fff00,
	},
	{ 0, 0},
};

static struct amba_driver tpda_driver = {
	.drv = {
		.name   = "coresight-tpda",
		.owner	= THIS_MODULE,
		.suppress_bind_attrs = true,
	},
	.probe          = tpda_probe,
	.remove		= tpda_remove,
	.id_table	= tpda_ids,
};

module_amba_driver(tpda_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Trace, Profiling & Diagnostic Aggregator driver");
