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
#include <media/v4l2-common.h>
#include <media/soc_camera.h>
#include <media/soc_camera_platform.h>

struct soc_camera_platform_priv {
	struct soc_camera_platform_info *info;
	struct soc_camera_device icd;
	struct soc_camera_data_format format;
};

static struct soc_camera_platform_info *
soc_camera_platform_get_info(struct soc_camera_device *icd)
{
	struct soc_camera_platform_priv *priv;
	priv = container_of(icd, struct soc_camera_platform_priv, icd);
	return priv->info;
}

static int soc_camera_platform_init(struct soc_camera_device *icd)
{
	struct soc_camera_platform_info *p = soc_camera_platform_get_info(icd);

	if (p->power)
		p->power(1);

	return 0;
}

static int soc_camera_platform_release(struct soc_camera_device *icd)
{
	struct soc_camera_platform_info *p = soc_camera_platform_get_info(icd);

	if (p->power)
		p->power(0);

	return 0;
}

static int soc_camera_platform_start_capture(struct soc_camera_device *icd)
{
	struct soc_camera_platform_info *p = soc_camera_platform_get_info(icd);
	return p->set_capture(p, 1);
}

static int soc_camera_platform_stop_capture(struct soc_camera_device *icd)
{
	struct soc_camera_platform_info *p = soc_camera_platform_get_info(icd);
	return p->set_capture(p, 0);
}

static int soc_camera_platform_set_bus_param(struct soc_camera_device *icd,
					     unsigned long flags)
{
	return 0;
}

static unsigned long
soc_camera_platform_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_platform_info *p = soc_camera_platform_get_info(icd);
	return p->bus_param;
}

static int soc_camera_platform_set_fmt(struct soc_camera_device *icd,
				       __u32 pixfmt, struct v4l2_rect *rect)
{
	return 0;
}

static int soc_camera_platform_try_fmt(struct soc_camera_device *icd,
				       struct v4l2_format *f)
{
	struct soc_camera_platform_info *p = soc_camera_platform_get_info(icd);
	struct v4l2_pix_format *pix = &f->fmt.pix;

	pix->width = p->format.width;
	pix->height = p->format.height;
	return 0;
}

static int soc_camera_platform_video_probe(struct soc_camera_device *icd)
{
	struct soc_camera_platform_priv *priv;
	priv = container_of(icd, struct soc_camera_platform_priv, icd);

	priv->format.name = priv->info->format_name;
	priv->format.depth = priv->info->format_depth;
	priv->format.fourcc = priv->info->format.pixelformat;
	priv->format.colorspace = priv->info->format.colorspace;

	icd->formats = &priv->format;
	icd->num_formats = 1;

	return soc_camera_video_start(icd);
}

static void soc_camera_platform_video_remove(struct soc_camera_device *icd)
{
	soc_camera_video_stop(icd);
}

static struct soc_camera_ops soc_camera_platform_ops = {
	.owner			= THIS_MODULE,
	.probe			= soc_camera_platform_video_probe,
	.remove			= soc_camera_platform_video_remove,
	.init			= soc_camera_platform_init,
	.release		= soc_camera_platform_release,
	.start_capture		= soc_camera_platform_start_capture,
	.stop_capture		= soc_camera_platform_stop_capture,
	.set_fmt		= soc_camera_platform_set_fmt,
	.try_fmt		= soc_camera_platform_try_fmt,
	.set_bus_param		= soc_camera_platform_set_bus_param,
	.query_bus_param	= soc_camera_platform_query_bus_param,
};

static int soc_camera_platform_probe(struct platform_device *pdev)
{
	struct soc_camera_platform_priv *priv;
	struct soc_camera_platform_info *p;
	struct soc_camera_device *icd;
	int ret;

	p = pdev->dev.platform_data;
	if (!p)
		return -EINVAL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info = p;
	platform_set_drvdata(pdev, priv);

	icd = &priv->icd;
	icd->ops	= &soc_camera_platform_ops;
	icd->control	= &pdev->dev;
	icd->width_min	= 0;
	icd->width_max	= priv->info->format.width;
	icd->height_min	= 0;
	icd->height_max	= priv->info->format.height;
	icd->y_skip_top	= 0;
	icd->iface	= priv->info->iface;

	ret = soc_camera_device_register(icd);
	if (ret)
		kfree(priv);

	return ret;
}

static int soc_camera_platform_remove(struct platform_device *pdev)
{
	struct soc_camera_platform_priv *priv = platform_get_drvdata(pdev);

	soc_camera_device_unregister(&priv->icd);
	kfree(priv);
	return 0;
}

static struct platform_driver soc_camera_platform_driver = {
	.driver 	= {
		.name	= "soc_camera_platform",
	},
	.probe		= soc_camera_platform_probe,
	.remove		= soc_camera_platform_remove,
};

static int __init soc_camera_platform_module_init(void)
{
	return platform_driver_register(&soc_camera_platform_driver);
}

static void __exit soc_camera_platform_module_exit(void)
{
	platform_driver_unregister(&soc_camera_platform_driver);
}

module_init(soc_camera_platform_module_init);
module_exit(soc_camera_platform_module_exit);

MODULE_DESCRIPTION("SoC Camera Platform driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
