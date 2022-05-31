// SPDX-License-Identifier: GPL-2.0+
/*
 * V4L2 Media Controller Driver for Freescale i.MX5/6 SOC
 *
 * Copyright (c) 2016-2019 Mentor Graphics Inc.
 */
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-event.h>
#include <media/imx.h>
#include "imx-media.h"

static inline struct imx_media_dev *notifier2dev(struct v4l2_async_notifier *n)
{
	return container_of(n, struct imx_media_dev, notifier);
}

/* async subdev bound notifier */
static int imx_media_subdev_bound(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	struct imx_media_dev *imxmd = notifier2dev(notifier);
	int ret;

	if (sd->grp_id & IMX_MEDIA_GRP_ID_IPU_CSI) {
		/* register the IPU internal subdevs */
		ret = imx_media_register_ipu_internal_subdevs(imxmd, sd);
		if (ret)
			return ret;
	}

	dev_dbg(imxmd->md.dev, "subdev %s bound\n", sd->name);

	return 0;
}

/* async subdev complete notifier */
static int imx6_media_probe_complete(struct v4l2_async_notifier *notifier)
{
	struct imx_media_dev *imxmd = notifier2dev(notifier);
	int ret;

	/* call the imx5/6/7 common probe completion handler */
	ret = imx_media_probe_complete(notifier);
	if (ret)
		return ret;

	mutex_lock(&imxmd->mutex);

	imxmd->m2m_vdev = imx_media_csc_scaler_device_init(imxmd);
	if (IS_ERR(imxmd->m2m_vdev)) {
		ret = PTR_ERR(imxmd->m2m_vdev);
		imxmd->m2m_vdev = NULL;
		goto unlock;
	}

	ret = imx_media_csc_scaler_device_register(imxmd->m2m_vdev);
unlock:
	mutex_unlock(&imxmd->mutex);
	return ret;
}

/* async subdev complete notifier */
static const struct v4l2_async_notifier_operations imx_media_notifier_ops = {
	.bound = imx_media_subdev_bound,
	.complete = imx6_media_probe_complete,
};

static int imx_media_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct imx_media_dev *imxmd;
	int ret;

	imxmd = imx_media_dev_init(dev, NULL);
	if (IS_ERR(imxmd))
		return PTR_ERR(imxmd);

	ret = imx_media_add_of_subdevs(imxmd, node);
	if (ret) {
		v4l2_err(&imxmd->v4l2_dev,
			 "add_of_subdevs failed with %d\n", ret);
		goto cleanup;
	}

	ret = imx_media_dev_notifier_register(imxmd, &imx_media_notifier_ops);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	v4l2_async_nf_cleanup(&imxmd->notifier);
	v4l2_device_unregister(&imxmd->v4l2_dev);
	media_device_cleanup(&imxmd->md);

	return ret;
}

static int imx_media_remove(struct platform_device *pdev)
{
	struct imx_media_dev *imxmd =
		(struct imx_media_dev *)platform_get_drvdata(pdev);

	v4l2_info(&imxmd->v4l2_dev, "Removing imx-media\n");

	if (imxmd->m2m_vdev) {
		imx_media_csc_scaler_device_unregister(imxmd->m2m_vdev);
		imxmd->m2m_vdev = NULL;
	}

	v4l2_async_nf_unregister(&imxmd->notifier);
	imx_media_unregister_ipu_internal_subdevs(imxmd);
	v4l2_async_nf_cleanup(&imxmd->notifier);
	media_device_unregister(&imxmd->md);
	v4l2_device_unregister(&imxmd->v4l2_dev);
	media_device_cleanup(&imxmd->md);

	return 0;
}

static const struct of_device_id imx_media_dt_ids[] = {
	{ .compatible = "fsl,imx-capture-subsystem" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_media_dt_ids);

static struct platform_driver imx_media_pdrv = {
	.probe		= imx_media_probe,
	.remove		= imx_media_remove,
	.driver		= {
		.name	= "imx-media",
		.of_match_table	= imx_media_dt_ids,
	},
};

module_platform_driver(imx_media_pdrv);

MODULE_DESCRIPTION("i.MX5/6 v4l2 media controller driver");
MODULE_AUTHOR("Steve Longerbeam <steve_longerbeam@mentor.com>");
MODULE_LICENSE("GPL");
