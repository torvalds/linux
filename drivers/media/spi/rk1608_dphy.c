// SPDX-License-Identifier: GPL-2.0
/**
 * Rockchip rk1608 driver
 *
 * Copyright (C) 2017-2018 Rockchip Electronics Co., Ltd.
 *
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/mfd/syscon.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/types.h>
#include <linux/rk-preisp.h>
#include <linux/rkisp1-config.h>
#include "rk1608_dphy.h"

/**
 * Rk1608 is used as the Pre-ISP to link on Soc, which mainly has two
 * functions. One is to download the firmware of RK1608, and the other
 * is to match the extra sensor such as camera and enable sensor by
 * calling sensor's s_power.
 *	|-----------------------|
 *	|     Sensor Camera     |
 *	|-----------------------|
 *	|-----------||----------|
 *	|-----------||----------|
 *	|-----------\/----------|
 *	|     Pre-ISP RK1608    |
 *	|-----------------------|
 *	|-----------||----------|
 *	|-----------||----------|
 *	|-----------\/----------|
 *	|      Rockchip Soc     |
 *	|-----------------------|
 * Data Transfer As shown above. In RK1608, the data received from the
 * extra sensor,and it is passed to the Soc through ISP.
 */

static inline struct rk1608_dphy *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rk1608_dphy, sd);
}

static int rk1608_s_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_ctrl *remote_ctrl;
	struct rk1608_dphy *pdata = to_state(sd);

	pdata->rk1608_sd->grp_id = pdata->sd.grp_id;
	remote_ctrl = v4l2_ctrl_find(pdata->rk1608_sd->ctrl_handler,
				     V4L2_CID_HBLANK);
	if (remote_ctrl) {
		v4l2_ctrl_g_ctrl(remote_ctrl);
		__v4l2_ctrl_modify_range(pdata->hblank,
					 remote_ctrl->minimum,
					 remote_ctrl->maximum,
					 remote_ctrl->step,
					 remote_ctrl->default_value);
	}

	remote_ctrl = v4l2_ctrl_find(pdata->rk1608_sd->ctrl_handler,
				     V4L2_CID_VBLANK);
	if (remote_ctrl) {
		v4l2_ctrl_g_ctrl(remote_ctrl);
		__v4l2_ctrl_modify_range(pdata->vblank,
					 remote_ctrl->minimum,
					 remote_ctrl->maximum,
					 remote_ctrl->step,
					 remote_ctrl->default_value);
	}

	return 0;
}

static int rk1608_s_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct rk1608_dphy *pdata = to_state(sd);

	pdata->rk1608_sd->grp_id = sd->grp_id;

	return 0;
}

static int rk1608_sensor_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;
	struct rk1608_dphy *pdata = to_state(sd);

	pdata->rk1608_sd->grp_id = sd->grp_id;
	ret = v4l2_subdev_call(pdata->rk1608_sd, core, s_power, on);

	return ret;
}

static int rk1608_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rk1608_dphy *pdata = to_state(sd);

	pdata->rk1608_sd->grp_id = sd->grp_id;
	v4l2_subdev_call(pdata->rk1608_sd, video, s_stream, enable);
	return 0;
}

static int rk1608_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct rk1608_dphy *pdata = to_state(sd);

	if (code->index > 0)
		return -EINVAL;

	code->code = pdata->mf.code;

	return 0;
}

static int rk1608_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	struct rk1608_dphy *pdata = to_state(sd);

	mf->code = pdata->mf.code;
	mf->width = pdata->mf.width;
	mf->height = pdata->mf.height;
	mf->field = pdata->mf.field;
	mf->colorspace = pdata->mf.colorspace;

	return 0;
}

static int rk1608_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct v4l2_ctrl *remote_ctrl;
	struct rk1608_dphy *pdata = to_state(sd);

	pdata->rk1608_sd->grp_id = pdata->sd.grp_id;
	remote_ctrl = v4l2_ctrl_find(pdata->rk1608_sd->ctrl_handler,
						 V4L2_CID_HBLANK);
	if (remote_ctrl) {
		v4l2_ctrl_g_ctrl(remote_ctrl);
		__v4l2_ctrl_modify_range(pdata->hblank,
					 remote_ctrl->minimum,
					 remote_ctrl->maximum,
					 remote_ctrl->step,
					 remote_ctrl->default_value);
	}

	remote_ctrl = v4l2_ctrl_find(pdata->rk1608_sd->ctrl_handler,
					 V4L2_CID_VBLANK);
	if (remote_ctrl) {
		v4l2_ctrl_g_ctrl(remote_ctrl);
		__v4l2_ctrl_modify_range(pdata->vblank,
					 remote_ctrl->minimum,
					 remote_ctrl->maximum,
					 remote_ctrl->step,
					 remote_ctrl->default_value);
	}

	return 0;
}

static int rk1608_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct rk1608_dphy *pdata = to_state(sd);

	pdata->rk1608_sd->grp_id = sd->grp_id;
	v4l2_subdev_call(pdata->rk1608_sd,
			 video,
			 g_frame_interval,
			 fi);

	return 0;
}

static long rk1608_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct rk1608_dphy *pdata = to_state(sd);
	long ret;

	switch (cmd) {
	case PREISP_CMD_SAVE_HDRAE_PARAM:
	case PREISP_CMD_SET_HDRAE_EXP:
		pdata->rk1608_sd->grp_id = pdata->sd.grp_id;
		ret = v4l2_subdev_call(pdata->rk1608_sd, core, ioctl,
				       cmd, arg);
		return ret;
	}
	return -ENOTTY;
}

#ifdef CONFIG_COMPAT
static long rk1608_compat_ioctl32(struct v4l2_subdev *sd,
		     unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct preisp_hdrae_exp_s hdrae_exp;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		if (copy_from_user(&hdrae_exp, up, sizeof(hdrae_exp)))
			return -EFAULT;

		return rk1608_ioctl(sd, cmd, &hdrae_exp);
	}

	return -ENOTTY;
}
#endif

static int rk1608_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_ctrl *remote_ctrl;
	struct rk1608_dphy *pdata =
		container_of(ctrl->handler,
			     struct rk1608_dphy, ctrl_handler);

	pdata->rk1608_sd->grp_id = pdata->sd.grp_id;
	remote_ctrl = v4l2_ctrl_find(pdata->rk1608_sd->ctrl_handler,
				     ctrl->id);
	if (remote_ctrl) {
		ctrl->val = v4l2_ctrl_g_ctrl(remote_ctrl);
		__v4l2_ctrl_modify_range(ctrl,
					 remote_ctrl->minimum,
					 remote_ctrl->maximum,
					 remote_ctrl->step,
					 remote_ctrl->default_value);
	}

	return 0;
}

static int rk1608_set_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct v4l2_ctrl *remote_ctrl;
	struct rk1608_dphy *pdata =
		container_of(ctrl->handler,
			     struct rk1608_dphy, ctrl_handler);

	pdata->rk1608_sd->grp_id = pdata->sd.grp_id;
	remote_ctrl = v4l2_ctrl_find(pdata->rk1608_sd->ctrl_handler,
				     ctrl->id);
	if (remote_ctrl)
		ret = v4l2_ctrl_s_ctrl(remote_ctrl, ctrl->val);

	return ret;
}

static const struct v4l2_ctrl_ops rk1608_ctrl_ops = {
	.g_volatile_ctrl = rk1608_g_volatile_ctrl,
	.s_ctrl = rk1608_set_ctrl,
};

static const struct v4l2_ctrl_config rk1608_priv_ctrls[] = {
	{
		.ops	= NULL,
		.id	= CIFISP_CID_EMB_VC,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "Embedded visual channel",
		.min	= 0,
		.max	= 3,
		.def	= 0,
		.step	= 1,
	}, {
		.ops	= NULL,
		.id	= CIFISP_CID_EMB_DT,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "Embedded data type",
		.min	= 0,
		.max	= 0xff,
		.def	= 0x30,
		.step	= 1,
	}
};

static int rk1608_initialize_controls(struct rk1608_dphy *dphy)
{
	u32 i;
	int ret;
	s64 pixel_rate, pixel_bit;
	struct v4l2_ctrl_handler *handler;
	unsigned long flags = V4L2_CTRL_FLAG_VOLATILE |
			      V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;

	handler = &dphy->ctrl_handler;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;

	dphy->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				       V4L2_CID_LINK_FREQ, 0,
				       0, &dphy->link_freqs);
	if (dphy->link_freq)
		dphy->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	switch (dphy->data_type) {
	case 0x2b:
		pixel_bit = 10;
		break;
	case 0x2c:
		pixel_bit = 12;
		break;
	default:
		pixel_bit = 8;
		break;
	}
	pixel_rate = V4L2_CID_LINK_FREQ * dphy->mipi_lane * 2 / pixel_bit;
	dphy->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
					     V4L2_CID_PIXEL_RATE,
					     0, pixel_rate, 1, pixel_rate);

	dphy->hblank = v4l2_ctrl_new_std(handler,
					 &rk1608_ctrl_ops,
					 V4L2_CID_HBLANK,
					 0, 0x7FFFFFFF, 1, 0);
	if (dphy->hblank)
		dphy->hblank->flags |= flags;

	dphy->vblank = v4l2_ctrl_new_std(handler,
					 &rk1608_ctrl_ops,
					 V4L2_CID_VBLANK,
					 0, 0x7FFFFFFF, 1, 0);
	if (dphy->vblank)
		dphy->vblank->flags |= flags;

	dphy->exposure = v4l2_ctrl_new_std(handler,
					   &rk1608_ctrl_ops,
					   V4L2_CID_EXPOSURE,
					   0, 0x7FFFFFFF, 1, 0);
	if (dphy->exposure)
		dphy->exposure->flags |= flags;

	dphy->gain = v4l2_ctrl_new_std(handler,
				       &rk1608_ctrl_ops,
				       V4L2_CID_ANALOGUE_GAIN,
				       0, 0x7FFFFFFF, 1, 0);
	if (dphy->gain)
		dphy->gain->flags |= flags;

	for (i = 0; i < ARRAY_SIZE(rk1608_priv_ctrls); i++)
		v4l2_ctrl_new_custom(handler, &rk1608_priv_ctrls[i], NULL);

	if (handler->error) {
		ret = handler->error;
		dev_err(dphy->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	dphy->sd.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static const struct v4l2_subdev_internal_ops dphy_subdev_internal_ops = {
	.open	= rk1608_s_open,
	.close	= rk1608_s_close,
};

static const struct v4l2_subdev_video_ops rk1608_subdev_video_ops = {
	.s_stream	= rk1608_s_stream,
	.g_frame_interval = rk1608_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops rk1608_subdev_pad_ops = {
	.enum_mbus_code	= rk1608_enum_mbus_code,
	.get_fmt	= rk1608_get_fmt,
	.set_fmt	= rk1608_set_fmt,
};

static const struct v4l2_subdev_core_ops rk1608_core_ops = {
	.s_power	= rk1608_sensor_power,
	.ioctl		= rk1608_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = rk1608_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_ops dphy_subdev_ops = {
	.core	= &rk1608_core_ops,
	.video	= &rk1608_subdev_video_ops,
	.pad	= &rk1608_subdev_pad_ops,
};

static int rk1608_dphy_dt_property(struct rk1608_dphy *dphy)
{
	int ret = 0;
	struct device_node *node = dphy->dev->of_node;

	ret = of_property_read_u32(node, "id", &dphy->sd.grp_id);
	if (ret)
		dev_warn(dphy->dev, "Can not get id!");
	ret = of_property_read_u32(node, "cam_nums", &dphy->cam_nums);
	if (ret)
		dev_warn(dphy->dev, "Can not get cam_nums!");
	ret = of_property_read_u32(node, "data_type", &dphy->data_type);
	if (ret)
		dev_warn(dphy->dev, "Can not get data_type!");
	ret = of_property_read_u32(node, "in_mipi", &dphy->in_mipi);
	if (ret)
		dev_warn(dphy->dev, "Can not get in_mipi!");
	ret = of_property_read_u32(node, "out_mipi", &dphy->out_mipi);
	if (ret)
		dev_warn(dphy->dev, "Can not get out_mipi!");
	ret = of_property_read_u32(node, "mipi_lane", &dphy->mipi_lane);
	if (ret)
		dev_warn(dphy->dev, "Can not get mipi_lane!");
	ret = of_property_read_u32(node, "field", &dphy->mf.field);
	if (ret)
		dev_warn(dphy->dev, "Can not get field!");
	ret = of_property_read_u32(node, "colorspace", &dphy->mf.colorspace);
	if (ret)
		dev_warn(dphy->dev, "Can not get colorspace!");
	ret = of_property_read_u32(node, "code", &dphy->mf.code);
	if (ret)
		dev_warn(dphy->dev, "Can not get code!");
	ret = of_property_read_u32(node, "width", &dphy->mf.width);
	if (ret)
		dev_warn(dphy->dev, "Can not get width!");
	ret = of_property_read_u32(node, "height", &dphy->mf.height);
	if (ret)
		dev_warn(dphy->dev, "Can not get height!");
	ret = of_property_read_u32(node, "htotal", &dphy->htotal);
	if (ret)
		dev_warn(dphy->dev, "Can not get htotal!");
	ret = of_property_read_u32(node, "vtotal", &dphy->vtotal);
	if (ret)
		dev_warn(dphy->dev, "Can not get vtotal!");
	ret = of_property_read_u64(node, "link-freqs", &dphy->link_freqs);
	if (ret)
		dev_warn(dphy->dev, "Can not get link_freqs!");

	return ret;
}

static int rk1608_dphy_probe(struct platform_device *pdev)
{
	struct rk1608_dphy *dphy;
	struct v4l2_subdev *sd;
	int ret = 0;

	dphy = devm_kzalloc(&pdev->dev, sizeof(*dphy), GFP_KERNEL);
	if (!dphy)
		return -ENOMEM;
	dphy->dev = &pdev->dev;
	platform_set_drvdata(pdev, dphy);
	sd = &dphy->sd;
	sd->dev = &pdev->dev;
	v4l2_subdev_init(sd, &dphy_subdev_ops);
	rk1608_dphy_dt_property(dphy);

	snprintf(sd->name, sizeof(sd->name), "RK1608-dphy%d", sd->grp_id);
	rk1608_initialize_controls(dphy);
	sd->internal_ops = &dphy_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dphy->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;

	ret = media_entity_init(&sd->entity, 1, &dphy->pad, 0);
	if (ret < 0)
		goto handler_err;
	ret = v4l2_async_register_subdev(sd);
	if (ret < 0)
		goto register_err;

	dev_info(dphy->dev, "RK1608-dphy(%d) probe success!\n", sd->grp_id);

	return 0;
register_err:
	media_entity_cleanup(&sd->entity);
handler_err:
	v4l2_ctrl_handler_free(dphy->sd.ctrl_handler);
	devm_kfree(&pdev->dev, dphy);
	return ret;
}

static int rk1608_dphy_remove(struct platform_device *pdev)
{
	struct rk1608_dphy *dphy = platform_get_drvdata(pdev);

	v4l2_async_unregister_subdev(&dphy->sd);
	media_entity_cleanup(&dphy->sd.entity);
	v4l2_ctrl_handler_free(&dphy->ctrl_handler);

	return 0;
}

static const struct of_device_id dphy_of_match[] = {
	{ .compatible = "rockchip,rk1608-dphy" },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, rk1608_of_match);

static struct platform_driver rk1608_dphy_drv = {
	.driver = {
		.of_match_table = of_match_ptr(dphy_of_match),
		.name	= "RK1608-dphy",
	},
	.probe		= rk1608_dphy_probe,
	.remove		= rk1608_dphy_remove,
};

module_platform_driver(rk1608_dphy_drv);

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("A DSP driver for rk1608 chip");
MODULE_LICENSE("GPL v2");
