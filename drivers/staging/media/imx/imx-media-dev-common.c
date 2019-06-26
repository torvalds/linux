// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 Media Controller Driver for Freescale common i.MX5/6/7 SOC
 *
 * Copyright (c) 2019 Linaro Ltd
 * Copyright (c) 2016 Mentor Graphics Inc.
 */

#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include "imx-media.h"

static const struct v4l2_async_notifier_operations imx_media_subdev_ops = {
	.bound = imx_media_subdev_bound,
	.complete = imx_media_probe_complete,
};

static const struct media_device_ops imx_media_md_ops = {
	.link_notify = imx_media_link_notify,
};

struct imx_media_dev *imx_media_dev_init(struct device *dev)
{
	struct imx_media_dev *imxmd;
	int ret;

	imxmd = devm_kzalloc(dev, sizeof(*imxmd), GFP_KERNEL);
	if (!imxmd)
		return ERR_PTR(-ENOMEM);

	dev_set_drvdata(dev, imxmd);

	strlcpy(imxmd->md.model, "imx-media", sizeof(imxmd->md.model));
	imxmd->md.ops = &imx_media_md_ops;
	imxmd->md.dev = dev;

	mutex_init(&imxmd->mutex);

	imxmd->v4l2_dev.mdev = &imxmd->md;
	imxmd->v4l2_dev.notify = imx_media_notify;
	strlcpy(imxmd->v4l2_dev.name, "imx-media",
		sizeof(imxmd->v4l2_dev.name));

	media_device_init(&imxmd->md);

	ret = v4l2_device_register(dev, &imxmd->v4l2_dev);
	if (ret < 0) {
		v4l2_err(&imxmd->v4l2_dev,
			 "Failed to register v4l2_device: %d\n", ret);
		goto cleanup;
	}

	dev_set_drvdata(imxmd->v4l2_dev.dev, imxmd);

	INIT_LIST_HEAD(&imxmd->vdev_list);

	v4l2_async_notifier_init(&imxmd->notifier);

	return imxmd;

cleanup:
	media_device_cleanup(&imxmd->md);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(imx_media_dev_init);

int imx_media_dev_notifier_register(struct imx_media_dev *imxmd)
{
	int ret;

	/* no subdevs? just bail */
	if (list_empty(&imxmd->notifier.asd_list)) {
		v4l2_err(&imxmd->v4l2_dev, "no subdevs\n");
		return -ENODEV;
	}

	/* prepare the async subdev notifier and register it */
	imxmd->notifier.ops = &imx_media_subdev_ops;
	ret = v4l2_async_notifier_register(&imxmd->v4l2_dev,
					   &imxmd->notifier);
	if (ret) {
		v4l2_err(&imxmd->v4l2_dev,
			 "v4l2_async_notifier_register failed with %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_dev_notifier_register);
