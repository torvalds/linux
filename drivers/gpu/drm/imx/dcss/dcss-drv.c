// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <drm/drm_module.h>
#include <drm/drm_of.h>

#include "dcss-dev.h"
#include "dcss-kms.h"

struct dcss_drv {
	struct dcss_dev *dcss;
	struct dcss_kms_dev *kms;
};

struct dcss_dev *dcss_drv_dev_to_dcss(struct device *dev)
{
	struct dcss_drv *mdrv = dev_get_drvdata(dev);

	return mdrv ? mdrv->dcss : NULL;
}

struct drm_device *dcss_drv_dev_to_drm(struct device *dev)
{
	struct dcss_drv *mdrv = dev_get_drvdata(dev);

	return mdrv ? &mdrv->kms->base : NULL;
}

static int dcss_drv_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *remote;
	struct dcss_drv *mdrv;
	int err = 0;
	bool hdmi_output = true;

	if (!dev->of_node)
		return -ENODEV;

	remote = of_graph_get_remote_node(dev->of_node, 0, 0);
	if (!remote)
		return -ENODEV;

	hdmi_output = !of_device_is_compatible(remote, "fsl,imx8mq-nwl-dsi");

	of_node_put(remote);

	mdrv = kzalloc(sizeof(*mdrv), GFP_KERNEL);
	if (!mdrv)
		return -ENOMEM;

	mdrv->dcss = dcss_dev_create(dev, hdmi_output);
	if (IS_ERR(mdrv->dcss)) {
		err = PTR_ERR(mdrv->dcss);
		goto err;
	}

	dev_set_drvdata(dev, mdrv);

	mdrv->kms = dcss_kms_attach(mdrv->dcss);
	if (IS_ERR(mdrv->kms)) {
		err = PTR_ERR(mdrv->kms);
		dev_err_probe(dev, err, "Failed to initialize KMS\n");
		goto dcss_shutoff;
	}

	return 0;

dcss_shutoff:
	dcss_dev_destroy(mdrv->dcss);

err:
	kfree(mdrv);
	return err;
}

static int dcss_drv_platform_remove(struct platform_device *pdev)
{
	struct dcss_drv *mdrv = dev_get_drvdata(&pdev->dev);

	dcss_kms_detach(mdrv->kms);
	dcss_dev_destroy(mdrv->dcss);

	kfree(mdrv);

	return 0;
}

static struct dcss_type_data dcss_types[] = {
	[DCSS_IMX8MQ] = {
		.name = "DCSS_IMX8MQ",
		.blkctl_ofs = 0x2F000,
		.ctxld_ofs = 0x23000,
		.dtg_ofs = 0x20000,
		.scaler_ofs = 0x1C000,
		.ss_ofs = 0x1B000,
		.dpr_ofs = 0x18000,
	},
};

static const struct of_device_id dcss_of_match[] = {
	{ .compatible = "nxp,imx8mq-dcss", .data = &dcss_types[DCSS_IMX8MQ], },
	{},
};

MODULE_DEVICE_TABLE(of, dcss_of_match);

static struct platform_driver dcss_platform_driver = {
	.probe	= dcss_drv_platform_probe,
	.remove	= dcss_drv_platform_remove,
	.driver	= {
		.name = "imx-dcss",
		.of_match_table	= dcss_of_match,
		.pm = pm_ptr(&dcss_dev_pm_ops),
	},
};

drm_module_platform_driver(dcss_platform_driver);

MODULE_AUTHOR("Laurentiu Palcu <laurentiu.palcu@nxp.com>");
MODULE_DESCRIPTION("DCSS driver for i.MX8MQ");
MODULE_LICENSE("GPL v2");
