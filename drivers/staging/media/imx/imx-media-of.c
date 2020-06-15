// SPDX-License-Identifier: GPL-2.0+
/*
 * Media driver for Freescale i.MX5/6 SOC
 *
 * Open Firmware parsing.
 *
 * Copyright (c) 2016 Mentor Graphics Inc.
 */
#include <linux/of_platform.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/of_graph.h>
#include <video/imx-ipu-v3.h>
#include "imx-media.h"

int imx_media_of_add_csi(struct imx_media_dev *imxmd,
			 struct device_node *csi_np)
{
	struct v4l2_async_subdev *asd;
	int ret = 0;

	if (!of_device_is_available(csi_np)) {
		dev_dbg(imxmd->md.dev, "%s: %pOFn not enabled\n", __func__,
			csi_np);
		return -ENODEV;
	}

	/* add CSI fwnode to async notifier */
	asd = v4l2_async_notifier_add_fwnode_subdev(&imxmd->notifier,
						    of_fwnode_handle(csi_np),
						    sizeof(*asd));
	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		if (ret == -EEXIST)
			dev_dbg(imxmd->md.dev, "%s: already added %pOFn\n",
				__func__, csi_np);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(imx_media_of_add_csi);

int imx_media_add_of_subdevs(struct imx_media_dev *imxmd,
			     struct device_node *np)
{
	struct device_node *csi_np;
	int i, ret;

	for (i = 0; ; i++) {
		csi_np = of_parse_phandle(np, "ports", i);
		if (!csi_np)
			break;

		ret = imx_media_of_add_csi(imxmd, csi_np);
		if (ret) {
			/* unavailable or already added is not an error */
			if (ret == -ENODEV || ret == -EEXIST) {
				of_node_put(csi_np);
				continue;
			}

			/* other error, can't continue */
			goto err_out;
		}
	}

	return 0;

err_out:
	of_node_put(csi_np);
	return ret;
}
EXPORT_SYMBOL_GPL(imx_media_add_of_subdevs);
