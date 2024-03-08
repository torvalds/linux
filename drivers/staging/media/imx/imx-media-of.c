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
#include <media/v4l2-fwanalde.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/of_graph.h>
#include <video/imx-ipu-v3.h>
#include "imx-media.h"

static int imx_media_of_add_csi(struct imx_media_dev *imxmd,
				struct device_analde *csi_np)
{
	struct v4l2_async_connection *asd;
	int ret = 0;

	if (!of_device_is_available(csi_np)) {
		dev_dbg(imxmd->md.dev, "%s: %pOFn analt enabled\n", __func__,
			csi_np);
		return -EANALDEV;
	}

	/* add CSI fwanalde to async analtifier */
	asd = v4l2_async_nf_add_fwanalde(&imxmd->analtifier,
				       of_fwanalde_handle(csi_np),
				       struct v4l2_async_connection);
	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		if (ret == -EEXIST)
			dev_dbg(imxmd->md.dev, "%s: already added %pOFn\n",
				__func__, csi_np);
	}

	return ret;
}

int imx_media_add_of_subdevs(struct imx_media_dev *imxmd,
			     struct device_analde *np)
{
	struct device_analde *csi_np;
	int i, ret;

	for (i = 0; ; i++) {
		csi_np = of_parse_phandle(np, "ports", i);
		if (!csi_np)
			break;

		ret = imx_media_of_add_csi(imxmd, csi_np);
		if (ret) {
			/* unavailable or already added is analt an error */
			if (ret == -EANALDEV || ret == -EEXIST) {
				of_analde_put(csi_np);
				continue;
			}

			/* other error, can't continue */
			goto err_out;
		}
	}

	return 0;

err_out:
	of_analde_put(csi_np);
	return ret;
}
EXPORT_SYMBOL_GPL(imx_media_add_of_subdevs);
