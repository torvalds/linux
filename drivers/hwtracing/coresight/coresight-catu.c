// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Arm Limited. All rights reserved.
 *
 * Coresight Address Translation Unit support
 *
 * Author: Suzuki K Poulose <suzuki.poulose@arm.com>
 */

#include <linux/amba/bus.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "coresight-catu.h"
#include "coresight-priv.h"

#define csdev_to_catu_drvdata(csdev)	\
	dev_get_drvdata(csdev->dev.parent)

coresight_simple_reg32(struct catu_drvdata, devid, CORESIGHT_DEVID);
coresight_simple_reg32(struct catu_drvdata, control, CATU_CONTROL);
coresight_simple_reg32(struct catu_drvdata, status, CATU_STATUS);
coresight_simple_reg32(struct catu_drvdata, mode, CATU_MODE);
coresight_simple_reg32(struct catu_drvdata, axictrl, CATU_AXICTRL);
coresight_simple_reg32(struct catu_drvdata, irqen, CATU_IRQEN);
coresight_simple_reg64(struct catu_drvdata, sladdr,
		       CATU_SLADDRLO, CATU_SLADDRHI);
coresight_simple_reg64(struct catu_drvdata, inaddr,
		       CATU_INADDRLO, CATU_INADDRHI);

static struct attribute *catu_mgmt_attrs[] = {
	&dev_attr_devid.attr,
	&dev_attr_control.attr,
	&dev_attr_status.attr,
	&dev_attr_mode.attr,
	&dev_attr_axictrl.attr,
	&dev_attr_irqen.attr,
	&dev_attr_sladdr.attr,
	&dev_attr_inaddr.attr,
	NULL,
};

static const struct attribute_group catu_mgmt_group = {
	.attrs = catu_mgmt_attrs,
	.name = "mgmt",
};

static const struct attribute_group *catu_groups[] = {
	&catu_mgmt_group,
	NULL,
};


static inline int catu_wait_for_ready(struct catu_drvdata *drvdata)
{
	return coresight_timeout(drvdata->base,
				 CATU_STATUS, CATU_STATUS_READY, 1);
}

static int catu_enable_hw(struct catu_drvdata *drvdata, void *__unused)
{
	u32 control;

	if (catu_wait_for_ready(drvdata))
		dev_warn(drvdata->dev, "Timeout while waiting for READY\n");

	control = catu_read_control(drvdata);
	if (control & BIT(CATU_CONTROL_ENABLE)) {
		dev_warn(drvdata->dev, "CATU is already enabled\n");
		return -EBUSY;
	}

	control |= BIT(CATU_CONTROL_ENABLE);
	catu_write_mode(drvdata, CATU_MODE_PASS_THROUGH);
	catu_write_control(drvdata, control);
	dev_dbg(drvdata->dev, "Enabled in Pass through mode\n");
	return 0;
}

static int catu_enable(struct coresight_device *csdev, void *data)
{
	int rc;
	struct catu_drvdata *catu_drvdata = csdev_to_catu_drvdata(csdev);

	CS_UNLOCK(catu_drvdata->base);
	rc = catu_enable_hw(catu_drvdata, data);
	CS_LOCK(catu_drvdata->base);
	return rc;
}

static int catu_disable_hw(struct catu_drvdata *drvdata)
{
	int rc = 0;

	catu_write_control(drvdata, 0);
	if (catu_wait_for_ready(drvdata)) {
		dev_info(drvdata->dev, "Timeout while waiting for READY\n");
		rc = -EAGAIN;
	}

	dev_dbg(drvdata->dev, "Disabled\n");
	return rc;
}

static int catu_disable(struct coresight_device *csdev, void *__unused)
{
	int rc;
	struct catu_drvdata *catu_drvdata = csdev_to_catu_drvdata(csdev);

	CS_UNLOCK(catu_drvdata->base);
	rc = catu_disable_hw(catu_drvdata);
	CS_LOCK(catu_drvdata->base);
	return rc;
}

const struct coresight_ops_helper catu_helper_ops = {
	.enable = catu_enable,
	.disable = catu_disable,
};

const struct coresight_ops catu_ops = {
	.helper_ops = &catu_helper_ops,
};

static int catu_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret = 0;
	u32 dma_mask;
	struct catu_drvdata *drvdata;
	struct coresight_desc catu_desc;
	struct coresight_platform_data *pdata = NULL;
	struct device *dev = &adev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *base;

	if (np) {
		pdata = of_get_coresight_platform_data(dev, np);
		if (IS_ERR(pdata)) {
			ret = PTR_ERR(pdata);
			goto out;
		}
		dev->platform_data = pdata;
	}

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		ret = -ENOMEM;
		goto out;
	}

	drvdata->dev = dev;
	dev_set_drvdata(dev, drvdata);
	base = devm_ioremap_resource(dev, &adev->res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto out;
	}

	/* Setup dma mask for the device */
	dma_mask = readl_relaxed(base + CORESIGHT_DEVID) & 0x3f;
	switch (dma_mask) {
	case 32:
	case 40:
	case 44:
	case 48:
	case 52:
	case 56:
	case 64:
		break;
	default:
		/* Default to the 40bits as supported by TMC-ETR */
		dma_mask = 40;
	}
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(dma_mask));
	if (ret)
		goto out;

	drvdata->base = base;
	catu_desc.pdata = pdata;
	catu_desc.dev = dev;
	catu_desc.groups = catu_groups;
	catu_desc.type = CORESIGHT_DEV_TYPE_HELPER;
	catu_desc.subtype.helper_subtype = CORESIGHT_DEV_SUBTYPE_HELPER_CATU;
	catu_desc.ops = &catu_ops;
	drvdata->csdev = coresight_register(&catu_desc);
	if (IS_ERR(drvdata->csdev))
		ret = PTR_ERR(drvdata->csdev);
out:
	pm_runtime_put(&adev->dev);
	return ret;
}

static struct amba_id catu_ids[] = {
	{
		.id	= 0x000bb9ee,
		.mask	= 0x000fffff,
	},
	{},
};

static struct amba_driver catu_driver = {
	.drv = {
		.name			= "coresight-catu",
		.owner			= THIS_MODULE,
		.suppress_bind_attrs	= true,
	},
	.probe				= catu_probe,
	.id_table			= catu_ids,
};

builtin_amba_driver(catu_driver);
