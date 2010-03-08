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

static struct soc_camera_platform_info *get_info(struct soc_camera_device *icd)
{
	struct platform_device *pdev =
		to_platform_device(to_soc_camera_control(icd));
	return pdev->dev.platform_data;
}

static int soc_camera_platform_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(sd);
	return p->set_capture(p, enable);
}

static int soc_camera_platform_set_bus_param(struct soc_camera_device *icd,
					     unsigned long flags)
{
	return 0;
}

static unsigned long
soc_camera_platform_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_platform_info *p = get_info(icd);
	return p->bus_param;
}

static int soc_camera_platform_try_fmt(struct v4l2_subdev *sd,
				       struct v4l2_mbus_framefmt *mf)
{
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(sd);

	mf->width	= p->format.width;
	mf->height	= p->format.height;
	mf->code	= p->format.code;
	mf->colorspace	= p->format.colorspace;

	return 0;
}

static struct v4l2_subdev_core_ops platform_subdev_core_ops;

static int soc_camera_platform_enum_fmt(struct v4l2_subdev *sd, int index,
					enum v4l2_mbus_pixelcode *code)
{
	struct soc_camera_platform_info *p = v4l2_get_subdevdata(sd);

	if (index)
		return -EINVAL;

	*code = p->format.code;
	return 0;
}

static struct v4l2_subdev_video_ops platform_subdev_video_ops = {
	.s_stream	= soc_camera_platform_s_stream,
	.try_mbus_fmt	= soc_camera_platform_try_fmt,
	.enum_mbus_fmt	= soc_camera_platform_enum_fmt,
};

static struct v4l2_subdev_ops platform_subdev_ops = {
	.core	= &platform_subdev_core_ops,
	.video	= &platform_subdev_video_ops,
};

static struct soc_camera_ops soc_camera_platform_ops = {
	.set_bus_param		= soc_camera_platform_set_bus_param,
	.query_bus_param	= soc_camera_platform_query_bus_param,
};

static int soc_camera_platform_probe(struct platform_device *pdev)
{
	struct soc_camera_host *ici;
	struct soc_camera_platform_priv *priv;
	struct soc_camera_platform_info *p = pdev->dev.platform_data;
	struct soc_camera_device *icd;
	int ret;

	if (!p)
		return -EINVAL;

	if (!p->dev) {
		dev_err(&pdev->dev,
			"Platform has not set soc_camera_device pointer!\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	icd = to_soc_camera_dev(p->dev);

	/* soc-camera convention: control's drvdata points to the subdev */
	platform_set_drvdata(pdev, &priv->subdev);
	/* Set the control device reference */
	dev_set_drvdata(&icd->dev, &pdev->dev);

	icd->ops = &soc_camera_platform_ops;

	ici = to_soc_camera_host(icd->dev.parent);

	v4l2_subdev_init(&priv->subdev, &platform_subdev_ops);
	v4l2_set_subdevdata(&priv->subdev, p);
	strncpy(priv->subdev.name, dev_name(&pdev->dev), V4L2_SUBDEV_NAME_SIZE);

	ret = v4l2_device_register_subdev(&ici->v4l2_dev, &priv->subdev);
	if (ret)
		goto evdrs;

	return ret;

evdrs:
	icd->ops = NULL;
	platform_set_drvdata(pdev, NULL);
	kfree(priv);
	return ret;
}

static int soc_camera_platform_remove(struct platform_device *pdev)
{
	struct soc_camera_platform_priv *priv = get_priv(pdev);
	struct soc_camera_platform_info *p = pdev->dev.platform_data;
	struct soc_camera_device *icd = to_soc_camera_dev(p->dev);

	v4l2_device_unregister_subdev(&priv->subdev);
	icd->ops = NULL;
	platform_set_drvdata(pdev, NULL);
	kfree(priv);
	return 0;
}

static struct platform_driver soc_camera_platform_driver = {
	.driver 	= {
		.name	= "soc_camera_platform",
		.owner	= THIS_MODULE,
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
MODULE_ALIAS("platform:soc_camera_platform");
