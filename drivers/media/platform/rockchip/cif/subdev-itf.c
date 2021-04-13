// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip CIF Driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-fwnode.h>
#include "dev.h"

static inline struct sditf_priv *to_sditf_priv(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct sditf_priv, sd);
}

static int sditf_g_frame_interval(struct v4l2_subdev *sd,
				  struct v4l2_subdev_frame_interval *fi)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev *sensor_sd;

	if (!cif_dev->terminal_sensor.sd)
		rkcif_update_sensor_info(&cif_dev->stream[0]);

	if (cif_dev->terminal_sensor.sd) {
		sensor_sd = cif_dev->terminal_sensor.sd;
		return v4l2_subdev_call(sensor_sd, video, g_frame_interval, fi);
	}

	return -EINVAL;
}

static int sditf_g_mbus_config(struct v4l2_subdev *sd,
			       struct v4l2_mbus_config *config)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev *sensor_sd;

	if (!cif_dev->active_sensor)
		rkcif_update_sensor_info(&cif_dev->stream[0]);

	if (cif_dev->active_sensor) {
		sensor_sd = cif_dev->active_sensor->sd;
		return v4l2_subdev_call(sensor_sd, video, g_mbus_config, config);
	}

	return -EINVAL;
}

static int sditf_get_set_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev_selection input_sel;
	int ret = -EINVAL;

	if (!cif_dev->terminal_sensor.sd)
		rkcif_update_sensor_info(&cif_dev->stream[0]);

	if (cif_dev->terminal_sensor.sd) {
		ret = v4l2_subdev_call(cif_dev->terminal_sensor.sd, pad, get_fmt, NULL, fmt);
		if (ret) {
			v4l2_err(&priv->sd,
				 "%s: get sensor format failed\n", __func__);
			return ret;
		}

		input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
		ret = v4l2_subdev_call(cif_dev->terminal_sensor.sd,
				       pad, get_selection, NULL,
				       &input_sel);
		if (!ret) {
			fmt->format.width = input_sel.r.width;
			fmt->format.height = input_sel.r.height;
		}
	}

	return 0;
}

static int sditf_get_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	return -EINVAL;
}

static long sditf_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev *sensor_sd;

	if (!cif_dev->terminal_sensor.sd)
		rkcif_update_sensor_info(&cif_dev->stream[0]);

	if (cif_dev->terminal_sensor.sd) {
		sensor_sd = cif_dev->terminal_sensor.sd;
		return v4l2_subdev_call(sensor_sd, core, ioctl, cmd, arg);
	}

	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long sditf_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev *sensor_sd;

	if (!cif_dev->terminal_sensor.sd)
		rkcif_update_sensor_info(&cif_dev->stream[0]);

	if (cif_dev->terminal_sensor.sd) {
		sensor_sd = cif_dev->terminal_sensor.sd;
		return v4l2_subdev_call(sensor_sd, core, compat_ioctl32, cmd, arg);
	}

	return -EINVAL;
}
#endif

static const struct v4l2_subdev_pad_ops sditf_subdev_pad_ops = {
	.set_fmt = sditf_get_set_fmt,
	.get_fmt = sditf_get_set_fmt,
	.get_selection = sditf_get_selection,
};

static const struct v4l2_subdev_video_ops sditf_video_ops = {
	.g_frame_interval = sditf_g_frame_interval,
	.g_mbus_config = sditf_g_mbus_config,
};

static const struct v4l2_subdev_core_ops sditf_core_ops = {
	.ioctl = sditf_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sditf_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_ops sditf_subdev_ops = {
	.core = &sditf_core_ops,
	.video = &sditf_video_ops,
	.pad = &sditf_subdev_pad_ops,
};

static int rkcif_sditf_attach_cifdev(struct sditf_priv *sditf)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct rkcif_device *cif_dev;

	np = of_parse_phandle(sditf->dev->of_node, "rockchip,cif", 0);
	if (!np || !of_device_is_available(np)) {
		dev_err(sditf->dev, "failed to get cif dev node\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_err(sditf->dev, "failed to get cif dev from node\n");
		return -ENODEV;
	}

	cif_dev = platform_get_drvdata(pdev);
	if (!cif_dev) {
		dev_err(sditf->dev, "failed attach cif dev\n");
		return -EINVAL;
	}

	cif_dev->sditf = sditf;
	sditf->cif_dev = cif_dev;

	return 0;
}

static int rkcif_subdev_media_init(struct sditf_priv *priv)
{
	struct rkcif_device *cif_dev = priv->cif_dev;
	int ret;

	priv->pads.flags = MEDIA_PAD_FL_SOURCE;
	priv->sd.entity.function = MEDIA_ENT_F_PROC_VIDEO_COMPOSER;
	ret = media_entity_pads_init(&priv->sd.entity, 1, &priv->pads);
	if (ret < 0)
		return ret;

	strncpy(priv->sd.name, dev_name(cif_dev->dev), sizeof(priv->sd.name));

	return 0;
}

static int rkcif_subdev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_subdev *sd;
	struct sditf_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	sd = &priv->sd;
	v4l2_subdev_init(sd, &sditf_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "rockchip-cif-sditf");
	sd->dev = dev;

	platform_set_drvdata(pdev, &sd->entity);

	rkcif_sditf_attach_cifdev(priv);
	ret = rkcif_subdev_media_init(priv);
	if (ret < 0)
		return ret;

	pm_runtime_enable(&pdev->dev);
	return 0;
}

static int rkcif_subdev_remove(struct platform_device *pdev)
{
	struct media_entity *me = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(me);

	media_entity_cleanup(&sd->entity);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int sditf_runtime_suspend(struct device *dev)
{
	return 0;
}

static int sditf_runtime_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops rkcif_subdev_pm_ops = {
	SET_RUNTIME_PM_OPS(sditf_runtime_suspend,
			   sditf_runtime_resume, NULL)
};

static const struct of_device_id rkcif_subdev_match_id[] = {
	{
		.compatible = "rockchip,rkcif-sditf",
	},
	{}
};
MODULE_DEVICE_TABLE(of, rkcif_subdev_match_id);

struct platform_driver rkcif_subdev_driver = {
	.probe = rkcif_subdev_probe,
	.remove = rkcif_subdev_remove,
	.driver = {
		.name = "rkcif_sditf",
		.pm = &rkcif_subdev_pm_ops,
		.of_match_table = rkcif_subdev_match_id,
	},
};
EXPORT_SYMBOL(rkcif_subdev_driver);

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip CIF platform driver");
MODULE_LICENSE("GPL v2");
