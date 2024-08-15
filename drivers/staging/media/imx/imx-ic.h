/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * V4L2 Image Converter Subdev for Freescale i.MX5/6 SOC
 *
 * Copyright (c) 2016 Mentor Graphics Inc.
 */
#ifndef _IMX_IC_H
#define _IMX_IC_H

#include <media/v4l2-subdev.h>

struct imx_ic_priv {
	struct device *ipu_dev;
	struct ipu_soc *ipu;
	struct v4l2_subdev sd;
	int    task_id;
	void   *task_priv;
};

struct imx_ic_ops {
	const struct v4l2_subdev_ops *subdev_ops;
	const struct v4l2_subdev_internal_ops *internal_ops;
	const struct media_entity_operations *entity_ops;

	int (*init)(struct imx_ic_priv *ic_priv);
	void (*remove)(struct imx_ic_priv *ic_priv);
};

extern struct imx_ic_ops imx_ic_prp_ops;
extern struct imx_ic_ops imx_ic_prpencvf_ops;

#endif
