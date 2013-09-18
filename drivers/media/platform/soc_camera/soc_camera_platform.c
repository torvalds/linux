/*
 * Generic Platform Camera Driver
 *
 * Copyright (C) 2008 Magnus Damm
 * Based on mt9m001 driver,
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>
#include <media/soc_camera.h>
#include <media/soc_camera_platform.h>

struct soc_camera_platform_priv {
	struct v4l2_subdev subdev;
};

static struct soc_camera_platform_priv *get_priv(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev = platform_get_drvdata(pdev);
	return container_of(subdev, struct soc_camera_platform_priv, subdev);
}

static int soc_camera_platform_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(sd);
	return p->set_capture(p, enable);
}

static int soc_camera_platform_fill_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *mf)
{
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(sd);

	mf->width	= p->format.width;
	mf->height	= p->format.height;
	mf->code	= p->format.code;
	mf->colorspace	= p->format.colorspace;
	mf->field	= p->format.field;

	return 0;
}

static int soc_camera_platform_s_power(struct v4l2_subdev *sd, int on)
{
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(sd);

	return soc_camera_set_power(p->icd->control, &p->icd->sdesc->subdev_desc, NULL, on);
}

static struct v4l2_subdev_core_ops platform_subdev_core_ops = {
	.s_power = soc_camera_platform_s_power,
};

static int soc_camera_platform_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
					enum v4l2_mbus_pixelcode *code)
{
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(sd);

	if (index)
		return -EINVAL;

	*code = p->format.code;
	return 0;
}

static int soc_camera_platform_g_crop(struct v4l2_subdev *sd,
				      struct v4l2_crop *a)
{
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(sd);

	a->c.left	= 0;
	a->c.top	= 0;
	a->c.width	= p->format.width;
	a->c.height	= p->format.height;
	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int soc_camera_platform_cropcap(struct v4l2_subdev *sd,
				       struct v4l2_cropcap *a)
{
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(sd);

	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= p->format.width;
	a->bounds.height		= p->format.height;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int soc_camera_platform_g_mbus_config(struct v4l2_subdev *sd,
					     struct v4l2_mbus_config *cfg)
{
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(sd);

	cfg->flags = p->mbus_param;
	cfg->type = p->mbus_type;

	return 0;
}

static struct v4l2_subdev_video_ops platform_subdev_video_ops = {
	.s_stream	= soc_camera_platform_s_stream,
	.enum_mbus_fmt	= soc_camera_platform_enum_fmt,
	.cropcap	= soc_camera_platform_cropcap,
	.g_crop		= soc_camera_platform_g_crop,
	.try_mbus_fmt	= soc_camera_platform_fill_fmt,
	.g_mbus_fmt	= soc_camera_platform_fill_fmt,
	.s_mbus_fmt	= soc_camera_platform_fill_fmt,
	.g_mbus_config	= soc_camera_platform_g_mbus_config,
};

static struct v4l2_subdev_ops platform_subdev_ops = {
	.core	= &platform_subdev_core_ops,
	.video	= &platform_subdev_video_ops,
};

static int soc_camera_platform_probe(struct platform_device *pdev)
{
	struct soc_camera_host *ici;
	struct soc_camera_platform_priv *priv;
	struct soc_camera_platform_info *p = pdev->dev.platform_data;
	struct soc_camera_device *icd;

	if (!p)
		return -EINVAL;

	if (!p->icd) {
		dev_err(&pdev->dev,
			"Platform has not set soc_camera_device pointer!\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	icd = p->icd;

	/* soc-camera convention: control's drvdata points to the subdev */
	platform_set_drvdata(pdev, &priv->subdev);
	/* Set the control device reference */
	icd->control = &pdev->dev;

	ici = to_soc_camera_host(icd->parent);

	v4l2_subdev_init(&priv->subdev, &platform_subdev_ops);
	v4l2_set_subdevdata(&priv->subdev, p);
	strncpy(priv->subdev.name, dev_name(&pdev->dev), V4L2_SUBDEV_NAME_SIZE);

	return v4l2_device_register_subdev(&ici->v4l2_dev, &priv->subdev);
}

static int soc_camera_platform_remove(struct platform_device *pdev)
{
	struct soc_camera_platform_priv *priv = get_priv(pdev);
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(&priv->subdev);

	p->icd->control = NULL;
	v4l2_device_unregister_subdev(&priv->subdev);
	return 0;
}

static struct platform_driver soc_camera_platform_driver = {
	.driver		= {
		.name	= "soc_camera_platform",
		.owner	= THIS_MODULE,
	},
	.probe		= soc_camera_platform_probe,
	.remove		= soc_camera_platform_remove,
};

module_platform_driver(soc_camera_platform_driver);

MODULE_DESCRIPTION("SoC Camera Platform driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:soc_camera_platform");
