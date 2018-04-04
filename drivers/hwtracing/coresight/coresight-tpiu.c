/* Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
 *
 * Description: CoreSight Trace Port Interface Unit driver
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
#include <linux/init.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/coresight.h>
#include <linux/amba/bus.h>
#include <linux/clk.h>

#include "coresight-priv.h"

#define TPIU_SUPP_PORTSZ	0x000
#define TPIU_CURR_PORTSZ	0x004
#define TPIU_SUPP_TRIGMODES	0x100
#define TPIU_TRIG_CNTRVAL	0x104
#define TPIU_TRIG_MULT		0x108
#define TPIU_SUPP_TESTPATM	0x200
#define TPIU_CURR_TESTPATM	0x204
#define TPIU_TEST_PATREPCNTR	0x208
#define TPIU_FFSR		0x300
#define TPIU_FFCR		0x304
#define TPIU_FSYNC_CNTR		0x308
#define TPIU_EXTCTL_INPORT	0x400
#define TPIU_EXTCTL_OUTPORT	0x404
#define TPIU_ITTRFLINACK	0xee4
#define TPIU_ITTRFLIN		0xee8
#define TPIU_ITATBDATA0		0xeec
#define TPIU_ITATBCTR2		0xef0
#define TPIU_ITATBCTR1		0xef4
#define TPIU_ITATBCTR0		0xef8

/** register definition **/
/* FFSR - 0x300 */
#define FFSR_FT_STOPPED		BIT(1)
/* FFCR - 0x304 */
#define FFCR_FON_MAN		BIT(6)
#define FFCR_STOP_FI		BIT(12)

/**
 * @base:	memory mapped base address for this component.
 * @dev:	the device entity associated to this component.
 * @atclk:	optional clock for the core parts of the TPIU.
 * @csdev:	component vitals needed by the framework.
 */
struct tpiu_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct clk		*atclk;
	struct coresight_device	*csdev;
};

static void tpiu_enable_hw(struct tpiu_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	/* TODO: fill this up */

	CS_LOCK(drvdata->base);
}

static int tpiu_enable(struct coresight_device *csdev, u32 mode)
{
	struct tpiu_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	tpiu_enable_hw(drvdata);

	dev_info(drvdata->dev, "TPIU enabled\n");
	return 0;
}

static void tpiu_disable_hw(struct tpiu_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	/* Clear formatter and stop on flush */
	writel_relaxed(FFCR_STOP_FI, drvdata->base + TPIU_FFCR);
	/* Generate manual flush */
	writel_relaxed(FFCR_STOP_FI | FFCR_FON_MAN, drvdata->base + TPIU_FFCR);
	/* Wait for flush to complete */
	coresight_timeout(drvdata->base, TPIU_FFCR, FFCR_FON_MAN, 0);
	/* Wait for formatter to stop */
	coresight_timeout(drvdata->base, TPIU_FFSR, FFSR_FT_STOPPED, 1);

	CS_LOCK(drvdata->base);
}

static void tpiu_disable(struct coresight_device *csdev)
{
	struct tpiu_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	tpiu_disable_hw(drvdata);

	dev_info(drvdata->dev, "TPIU disabled\n");
}

static const struct coresight_ops_sink tpiu_sink_ops = {
	.enable		= tpiu_enable,
	.disable	= tpiu_disable,
};

static const struct coresight_ops tpiu_cs_ops = {
	.sink_ops	= &tpiu_sink_ops,
};

static int tpiu_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;
	void __iomem *base;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata = NULL;
	struct tpiu_drvdata *drvdata;
	struct resource *res = &adev->res;
	struct coresight_desc desc = { 0 };
	struct device_node *np = adev->dev.of_node;

	if (np) {
		pdata = of_get_coresight_platform_data(dev, np);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
		adev->dev.platform_data = pdata;
	}

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &adev->dev;
	drvdata->atclk = devm_clk_get(&adev->dev, "atclk"); /* optional */
	if (!IS_ERR(drvdata->atclk)) {
		ret = clk_prepare_enable(drvdata->atclk);
		if (ret)
			return ret;
	}
	dev_set_drvdata(dev, drvdata);

	/* Validity for the resource is already checked by the AMBA core */
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	drvdata->base = base;

	/* Disable tpiu to support older devices */
	tpiu_disable_hw(drvdata);

	pm_runtime_put(&adev->dev);

	desc.type = CORESIGHT_DEV_TYPE_SINK;
	desc.subtype.sink_subtype = CORESIGHT_DEV_SUBTYPE_SINK_PORT;
	desc.ops = &tpiu_cs_ops;
	desc.pdata = pdata;
	desc.dev = dev;
	drvdata->csdev = coresight_register(&desc);

	return PTR_ERR_OR_ZERO(drvdata->csdev);
}

#ifdef CONFIG_PM
static int tpiu_runtime_suspend(struct device *dev)
{
	struct tpiu_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata && !IS_ERR(drvdata->atclk))
		clk_disable_unprepare(drvdata->atclk);

	return 0;
}

static int tpiu_runtime_resume(struct device *dev)
{
	struct tpiu_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata && !IS_ERR(drvdata->atclk))
		clk_prepare_enable(drvdata->atclk);

	return 0;
}
#endif

static const struct dev_pm_ops tpiu_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(tpiu_runtime_suspend, tpiu_runtime_resume, NULL)
};

static const struct amba_id tpiu_ids[] = {
	{
		.id	= 0x000bb912,
		.mask	= 0x000fffff,
	},
	{
		.id	= 0x0004b912,
		.mask	= 0x0007ffff,
	},
	{
		/* Coresight SoC-600 */
		.id	= 0x000bb9e7,
		.mask	= 0x000fffff,
	},
	{ 0, 0},
};

static struct amba_driver tpiu_driver = {
	.drv = {
		.name	= "coresight-tpiu",
		.owner	= THIS_MODULE,
		.pm	= &tpiu_dev_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe		= tpiu_probe,
	.id_table	= tpiu_ids,
};
builtin_amba_driver(tpiu_driver);
