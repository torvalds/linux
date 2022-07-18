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
#include <linux/rk-camera-module.h>
#include "rk1608_dphy.h"
#include <linux/compat.h>

#define RK1608_DPHY_NAME	"RK1608-dphy"

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

static DEFINE_MUTEX(rk1608_dphy_mutex);
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

#define RK1608_MAX_BITRATE (1500000000)
static int rk1608_get_link_sensor_timing(struct rk1608_dphy *pdata)
{
	int ret = 0;
	u32 i;
	u32 idx = pdata->fmt_inf_idx;
	struct rk1608_fmt_inf *fmt_inf = &pdata->fmt_inf[idx];
	int sub_sensor_num = pdata->sub_sensor_num;
	u32 width = 0, height = 0, out_width, out_height;
	struct v4l2_subdev *link_sensor;
	u32 id = pdata->sd.grp_id;
	struct v4l2_subdev_frame_interval fi;
	int max_fps = 30;
	u64 bps;

	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = 0,
	};

	if (!IS_ERR_OR_NULL(pdata->link_sensor_client)) {

		link_sensor = i2c_get_clientdata(
				pdata->link_sensor_client);
		if (IS_ERR_OR_NULL(link_sensor)) {
			dev_err(pdata->dev, "can not get link sensor i2c client\n");
			return -EINVAL;
		}

		ret = v4l2_subdev_call(link_sensor, pad, get_fmt, NULL, &fmt);
		if (ret) {
			dev_info(pdata->dev, "get link fmt fail\n");
			return -EINVAL;
		}

		width = fmt.format.width;
		height = fmt.format.height;
		dev_info(pdata->dev, "phy[%d] get fmt w:%d h:%d\n",
				id, width, height);

		memset(&fi, 0, sizeof(fi));
		ret = v4l2_subdev_call(link_sensor, video, g_frame_interval, &fi);
		if (ret) {
			dev_info(pdata->dev, "get link interval fail\n");
			return -EINVAL;
		}

		max_fps = fi.interval.denominator / fi.interval.numerator;
		dev_info(pdata->dev, "phy[%d] get fps:%d (%d/%d)\n",
				id, max_fps, fi.interval.denominator, fi.interval.numerator);

	} else {
		width = fmt_inf->mf.width;
		height = fmt_inf->mf.height;
		dev_info(pdata->dev, "phy[%d] no link sensor\n", id);
	}

	if (!width || !height) {
		dev_err(pdata->dev, "phy[%d] get fmt error!\n", id);
		return -EINVAL;
	}

	for (i = 0; i < 4; i++) {
		if (fmt_inf->in_ch[i].width == 0)
			break;

		fmt_inf->in_ch[i].width = width;
		fmt_inf->in_ch[i].height = height;
	}

	out_width = width;
	out_height = height * (sub_sensor_num + 1); /* sub add main */
	for (i = 0; i < 4; i++) {
		if (fmt_inf->out_ch[i].width == 0)
			break;

		fmt_inf->out_ch[i].width = out_width;
		fmt_inf->out_ch[i].height = out_height;
	}

	fmt_inf->hactive = out_width;
	fmt_inf->vactive = out_height;
	fmt_inf->htotal = out_width + (width * 1 / 3); //1.33
	fmt_inf->vtotal = out_height + (height >> 4);

	/* max 30 fps, raw 10 */
	bps = fmt_inf->htotal * fmt_inf->vtotal
		/ fmt_inf->mipi_lane_out * 10 * max_fps;

	/* add extra timing */
	bps = bps * 105;
	do_div(bps, 100);

	if (bps > RK1608_MAX_BITRATE)
		bps = RK1608_MAX_BITRATE;

	pdata->link_freqs = (u32)(bps/2);
	dev_info(pdata->dev, "target mipi bps:%lld\n", bps);

	return 0;
}

static int rk1608_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rk1608_dphy *pdata = to_state(sd);

	if (enable && pdata->sub_sensor_num)
		rk1608_get_link_sensor_timing(pdata);

	pdata->rk1608_sd->grp_id = sd->grp_id;
	v4l2_subdev_call(pdata->rk1608_sd, video, s_stream, enable);
	return 0;
}

static int rk1608_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct rk1608_dphy *pdata = to_state(sd);

	if (code->index >= pdata->fmt_inf_num)
		return -EINVAL;

	code->code = pdata->fmt_inf[code->index].mf.code;

	return 0;
}

static int rk1608_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct rk1608_dphy *pdata = to_state(sd);

	if (fse->index >= pdata->fmt_inf_num)
		return -EINVAL;

	if (fse->code != pdata->fmt_inf[fse->index].mf.code)
		return -EINVAL;

	fse->min_width  = pdata->fmt_inf[fse->index].mf.width;
	fse->max_width  = pdata->fmt_inf[fse->index].mf.width;
	fse->max_height = pdata->fmt_inf[fse->index].mf.height;
	fse->min_height = pdata->fmt_inf[fse->index].mf.height;

	return 0;
}

static int rk1608_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	struct rk1608_dphy *pdata = to_state(sd);
	u32 idx = pdata->fmt_inf_idx;
	struct v4l2_subdev *link_sensor;
	int ret = -1;

	if (!IS_ERR_OR_NULL(pdata->link_sensor_client)) {
		link_sensor = i2c_get_clientdata(pdata->link_sensor_client);
		if (IS_ERR_OR_NULL(link_sensor)) {
			dev_err(pdata->dev, "can not get link sensor i2c client\n");
			goto exit;
		}

		ret = v4l2_subdev_call(link_sensor, pad, get_fmt, NULL, fmt);
		if (ret) {
			dev_info(pdata->dev, "get link fmt fail\n");
			goto exit;
		}

		dev_info(pdata->dev, "use link sensor fmt w:%d h:%d code:%d\n",
				mf->width, mf->height, mf->code);
	}

exit:
	if (ret || !mf->width || !mf->height) {
		mf->code = pdata->fmt_inf[idx].mf.code;
		mf->width = pdata->fmt_inf[idx].mf.width;
		mf->height = pdata->fmt_inf[idx].mf.height;
		mf->field = pdata->fmt_inf[idx].mf.field;
		mf->colorspace = pdata->fmt_inf[idx].mf.colorspace;
	} else {
		pdata->fmt_inf[idx].mf.code   = mf->code;
		pdata->fmt_inf[idx].mf.width  = mf->width;
		pdata->fmt_inf[idx].mf.height = mf->height;
		pdata->fmt_inf[idx].mf.field  = mf->field;
		pdata->fmt_inf[idx].mf.colorspace = mf->colorspace;
	}

	if (pdata->sub_sensor_num)
		rk1608_get_link_sensor_timing(pdata);

	return 0;
}

static int rk1608_get_reso_dist(struct rk1608_fmt_inf *fmt_inf,
				struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;

	return abs(fmt_inf->mf.width - framefmt->width) +
	       abs(fmt_inf->mf.height - framefmt->height);
}

static int rk1608_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct v4l2_ctrl *remote_ctrl;
	struct rk1608_dphy *pdata = to_state(sd);
	u32 i, idx = 0;
	int dist;
	int cur_best_fit_dist = -1;

	for (i = 0; i < pdata->fmt_inf_num; i++) {
		dist = rk1608_get_reso_dist(&pdata->fmt_inf[i], fmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			idx = i;
		}
	}

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return -ENOTTY;

	pdata->fmt_inf_idx = idx;

	pdata->rk1608_sd->grp_id = pdata->sd.grp_id;
	v4l2_subdev_call(pdata->rk1608_sd, pad, set_fmt, cfg, fmt);

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
	struct v4l2_subdev *link_sensor;
	int ret = 0;

	if (!IS_ERR_OR_NULL(pdata->link_sensor_client)) {
		link_sensor = i2c_get_clientdata(pdata->link_sensor_client);
		if (IS_ERR_OR_NULL(link_sensor)) {
			dev_err(pdata->dev, "can not get link sensor i2c client\n");
			return -EINVAL;
		}

		ret = v4l2_subdev_call(link_sensor,
				video,
				g_frame_interval,
				fi);
		if (ret)
			dev_info(pdata->dev, "get link interval fail\n");
		else
			return ret;
	}

	if (!(pdata->rk1608_sd)) {
		dev_info(pdata->dev, "pdata->rk1608_sd NULL\n");
		return -EFAULT;
	}
	pdata->rk1608_sd->grp_id = sd->grp_id;
	v4l2_subdev_call(pdata->rk1608_sd,
			 video,
			 g_frame_interval,
			 fi);

	return 0;
}

static int rk1608_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	return 0;
}

static int rk1608_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{

	struct rk1608_dphy *pdata = to_state(sd);
	u32 val = 0;

	val = 1 << (pdata->fmt_inf[pdata->fmt_inf_idx].mipi_lane_out - 1) |
	V4L2_MBUS_CSI2_CHANNEL_0 |
	V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static long rk1608_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct rk1608_dphy *pdata = to_state(sd);
	struct v4l2_subdev *link_sensor;
	long ret = 0;

	switch (cmd) {
	case PREISP_CMD_SAVE_HDRAE_PARAM:
		ret = v4l2_subdev_call(pdata->rk1608_sd, core, ioctl,
				       cmd, arg);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
	case RKMODULE_GET_MODULE_INFO:
	case RKMODULE_AWB_CFG:

	case PREISP_DISP_SET_FRAME_OUTPUT:
	case PREISP_DISP_SET_FRAME_FORMAT:
	case PREISP_DISP_SET_FRAME_TYPE:
	case PREISP_DISP_SET_PRO_TIME:
	case PREISP_DISP_SET_PRO_CURRENT:
	case PREISP_DISP_SET_DENOISE:
	case PREISP_DISP_WRITE_EEPROM:
	case PREISP_DISP_READ_EEPROM:
	case PREISP_DISP_SET_LED_ON_OFF:
	case RKMODULE_SET_QUICK_STREAM:
		mutex_lock(&rk1608_dphy_mutex);
		pdata->rk1608_sd->grp_id = pdata->sd.grp_id;
		ret = v4l2_subdev_call(pdata->rk1608_sd, core, ioctl,
				       cmd, arg);
		mutex_unlock(&rk1608_dphy_mutex);
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		if (!IS_ERR_OR_NULL(pdata->link_sensor_client)) {
			link_sensor = i2c_get_clientdata(pdata->link_sensor_client);
			if (IS_ERR_OR_NULL(link_sensor)) {
				dev_err(pdata->dev, "can not get link sensor i2c client\n");
				return -EINVAL;
			}
			ret = v4l2_subdev_call(link_sensor, core, ioctl, cmd, arg);
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

#ifdef CONFIG_COMPAT
static long rk1608_compat_ioctl32(struct v4l2_subdev *sd,
		     unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct preisp_hdrae_exp_s hdrae_exp;
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_csi_dphy_param *dphy_param;
	u32  stream;
	long ret = -EFAULT;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		if (copy_from_user(&hdrae_exp, up, sizeof(hdrae_exp)))
			return -EFAULT;

		return rk1608_ioctl(sd, cmd, &hdrae_exp);
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = rk1608_ioctl(sd, cmd, inf);
		if (!ret)
			if (copy_to_user(up, inf, sizeof(*inf))) {
				kfree(inf);
				return -EFAULT;
			}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}
		if (copy_from_user(cfg, up, sizeof(*cfg)))
			return -EFAULT;
		ret = rk1608_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = rk1608_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;

		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = kzalloc(sizeof(*dphy_param), GFP_KERNEL);
		if (!dphy_param) {
			ret = -ENOMEM;
			return ret;
		}
		if (copy_from_user(dphy_param, up, sizeof(*dphy_param)))
			return -EFAULT;
		ret = rk1608_ioctl(sd, cmd, dphy_param);
		kfree(dphy_param);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
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

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
static int rk1608_get_selection(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_selection *sel)
{
	struct rk1608_dphy *pdata = to_state(sd);
	u32 idx = pdata->fmt_inf_idx;
	u32 width = pdata->fmt_inf[idx].mf.width;
	u32 height = pdata->fmt_inf[idx].mf.height;
	struct v4l2_subdev *link_sensor;
	int ret = -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP_BOUNDS)
		return -EINVAL;

	if (!IS_ERR_OR_NULL(pdata->link_sensor_client)) {
		link_sensor = i2c_get_clientdata(pdata->link_sensor_client);
		if (IS_ERR_OR_NULL(link_sensor)) {
			dev_err(pdata->dev, "can not get link sensor i2c client\n");
			goto err;
		}

		ret = v4l2_subdev_call(link_sensor, pad, get_selection, NULL, sel);
		if (!ret)
			return 0;
	}

err:
	if (pdata->fmt_inf[idx].hcrop && pdata->fmt_inf[idx].vcrop) {
		width = pdata->fmt_inf[idx].hcrop;
		height = pdata->fmt_inf[idx].vcrop;
	}

	sel->r.left = CROP_START(pdata->fmt_inf[idx].mf.width, width);
	sel->r.top = CROP_START(pdata->fmt_inf[idx].mf.height, height);
	sel->r.width = width;
	sel->r.height = height;

	return 0;
}

static int rk1608_enum_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_interval_enum *fie)
{
	struct rk1608_dphy *pdata = to_state(sd);
	u32 idx = pdata->fmt_inf_idx;
	int ret = 0;
	struct v4l2_fract max_fps = {
		.numerator = 10000,
		.denominator = 300000,
	};
	struct v4l2_subdev *link_sensor;

	if (!IS_ERR_OR_NULL(pdata->link_sensor_client)) {
		link_sensor = i2c_get_clientdata(pdata->link_sensor_client);
		if (IS_ERR_OR_NULL(link_sensor)) {
			dev_err(pdata->dev, "can not get link sensor i2c client\n");
			goto err;
		}

		ret = v4l2_subdev_call(link_sensor,
				pad,
				enum_frame_interval,
				NULL,
				fie);
		return ret;
	}

err:
	if (fie->index >= pdata->fmt_inf_num)
		return -EINVAL;

	fie->code = pdata->fmt_inf[idx].mf.code;
	fie->width = pdata->fmt_inf[idx].mf.width;
	fie->height = pdata->fmt_inf[idx].mf.height;
	fie->interval = max_fps;

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
	u64 pixel_rate, pixel_bit;
	u32 idx = dphy->fmt_inf_idx;
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

	switch (dphy->fmt_inf[idx].data_type) {
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
	pixel_rate = dphy->link_freqs * dphy->fmt_inf[idx].mipi_lane * 2;
	do_div(pixel_rate, pixel_bit);
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
	.s_frame_interval = rk1608_s_frame_interval,
};

static const struct v4l2_subdev_pad_ops rk1608_subdev_pad_ops = {
	.enum_mbus_code	= rk1608_enum_mbus_code,
	.enum_frame_size = rk1608_enum_frame_sizes,
	.get_fmt	= rk1608_get_fmt,
	.set_fmt	= rk1608_set_fmt,
	.get_mbus_config = rk1608_g_mbus_config,
	.get_selection = rk1608_get_selection,
	.enum_frame_interval = rk1608_enum_frame_interval,
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
	struct device_node *parent_node = of_node_get(node);
	struct device_node *prev_node = NULL;
	struct i2c_client *link_sensor_client;
	u32 idx = 0;
	u32 sub_idx = 0;

	ret = of_property_read_u32(node, "id", &dphy->sd.grp_id);
	if (ret)
		dev_warn(dphy->dev, "Can not get id!");

	ret = of_property_read_u32(node, "cam_nums", &dphy->cam_nums);
	if (ret)
		dev_warn(dphy->dev, "Can not get cam_nums!");

	ret = of_property_read_u32(node, "in_mipi", &dphy->in_mipi);
	if (ret)
		dev_warn(dphy->dev, "Can not get in_mipi!");

	ret = of_property_read_u32(node, "out_mipi", &dphy->out_mipi);
	if (ret)
		dev_warn(dphy->dev, "Can not get out_mipi!");

	ret = of_property_read_u64(node, "link-freqs", &dphy->link_freqs);
	if (ret)
		dev_warn(dphy->dev, "Can not get link_freqs!");

	ret = of_property_read_u32(node, "sensor_i2c_bus", &dphy->i2c_bus);
	if (ret)
		dev_warn(dphy->dev, "Can not get sensor_i2c_bus!");

	ret = of_property_read_u32(node, "sensor_i2c_addr", &dphy->i2c_addr);
	if (ret)
		dev_warn(dphy->dev, "Can not get sensor_i2c_addr!");

	ret = of_property_read_string(node, "sensor-name", &dphy->sensor_name);
	if (ret)
		dev_warn(dphy->dev, "Can not get sensor-name!");

	node = NULL;
	while (!IS_ERR_OR_NULL(node =
				of_get_next_child(parent_node, prev_node))) {
		if (!strncasecmp(node->name,
				 "format-config",
				 strlen("format-config"))) {
			ret = of_property_read_u32(node, "data_type",
				&dphy->fmt_inf[idx].data_type);
			if (ret)
				dev_warn(dphy->dev, "Can not get data_type!");

			ret = of_property_read_u32(node, "mipi_lane",
				&dphy->fmt_inf[idx].mipi_lane);
			if (ret)
				dev_warn(dphy->dev, "Can not get mipi_lane!");

			ret = of_property_read_u32(node, "mipi_lane_out",
				&dphy->fmt_inf[idx].mipi_lane_out);
			if (ret)
				dev_warn(dphy->dev, "Can not get mipi_lane_out!");

			ret = of_property_read_u32(node, "field",
				&dphy->fmt_inf[idx].mf.field);
			if (ret)
				dev_warn(dphy->dev, "Can not get field!");

			ret = of_property_read_u32(node, "colorspace",
				&dphy->fmt_inf[idx].mf.colorspace);
			if (ret)
				dev_warn(dphy->dev, "Can not get colorspace!");

			ret = of_property_read_u32(node, "code",
				&dphy->fmt_inf[idx].mf.code);
			if (ret)
				dev_warn(dphy->dev, "Can not get code!");

			ret = of_property_read_u32(node, "width",
				&dphy->fmt_inf[idx].mf.width);
			if (ret)
				dev_warn(dphy->dev, "Can not get width!");

			ret = of_property_read_u32(node, "height",
				&dphy->fmt_inf[idx].mf.height);
			if (ret)
				dev_warn(dphy->dev, "Can not get height!");

			ret = of_property_read_u32(node, "hactive",
				&dphy->fmt_inf[idx].hactive);
			if (ret)
				dev_warn(dphy->dev, "Can not get hactive!");

			ret = of_property_read_u32(node, "vactive",
				&dphy->fmt_inf[idx].vactive);
			if (ret)
				dev_warn(dphy->dev, "Can not get vactive!");

			ret = of_property_read_u32(node, "htotal",
				&dphy->fmt_inf[idx].htotal);
			if (ret)
				dev_warn(dphy->dev, "Can not get htotal!");

			ret = of_property_read_u32(node, "vtotal",
				&dphy->fmt_inf[idx].vtotal);
			if (ret)
				dev_warn(dphy->dev, "Can not get vtotal!");

			ret = of_property_read_u32_array(node, "inch0-info",
				(u32 *)&dphy->fmt_inf[idx].in_ch[0], 5);
			if (ret)
				dev_warn(dphy->dev, "Can not get inch0-info!");

			ret = of_property_read_u32_array(node, "inch1-info",
				(u32 *)&dphy->fmt_inf[idx].in_ch[1], 5);
			if (ret)
				dev_info(dphy->dev, "Can not get inch1-info!");

			ret = of_property_read_u32_array(node, "inch2-info",
				(u32 *)&dphy->fmt_inf[idx].in_ch[2], 5);
			if (ret)
				dev_info(dphy->dev, "Can not get inch2-info!");

			ret = of_property_read_u32_array(node, "inch3-info",
				(u32 *)&dphy->fmt_inf[idx].in_ch[3], 5);
			if (ret)
				dev_info(dphy->dev, "Can not get inch3-info!");

			ret = of_property_read_u32_array(node, "outch0-info",
				(u32 *)&dphy->fmt_inf[idx].out_ch[0], 5);
			if (ret)
				dev_warn(dphy->dev, "Can not get outch0-info!");

			ret = of_property_read_u32_array(node, "outch1-info",
				(u32 *)&dphy->fmt_inf[idx].out_ch[1], 5);
			if (ret)
				dev_info(dphy->dev, "Can not get outch1-info!");

			ret = of_property_read_u32_array(node, "outch2-info",
				(u32 *)&dphy->fmt_inf[idx].out_ch[2], 5);
			if (ret)
				dev_info(dphy->dev, "Can not get outch2-info!");

			ret = of_property_read_u32_array(node, "outch3-info",
				(u32 *)&dphy->fmt_inf[idx].out_ch[3], 5);
			if (ret)
				dev_info(dphy->dev, "Can not get outch3-info!");

			ret = of_property_read_u32(node, "hcrop",
				&dphy->fmt_inf[idx].hcrop);
			if (ret)
				dev_warn(dphy->dev, "Can not get hcrop!");

			ret = of_property_read_u32(node, "vcrop",
				&dphy->fmt_inf[idx].vcrop);
			if (ret)
				dev_warn(dphy->dev, "Can not get vcrop!");

			idx++;
		}

		of_node_put(prev_node);
		prev_node = node;
	}
	dphy->fmt_inf_num = idx;

	prev_node = NULL;
	/* get virtual sub sensor */
	node = NULL;
	while (!IS_ERR_OR_NULL(node =
				of_get_next_child(parent_node, prev_node))) {
		if (!strncasecmp(node->name,
				 "virtual-sub-sensor-config",
				 strlen("virtual-sub-sensor-config"))) {

			if (sub_idx >= 4) {
				dev_err(dphy->dev, "get too mach sub_sensor node, max 4.\n");
				break;
			}

			ret = of_property_read_u32(node, "id",
				&dphy->sub_sensor[sub_idx].id);
			if (ret)
				dev_warn(dphy->dev, "Can not get sub sensor id!");
			else
				dev_info(dphy->dev, "get sub sensor id:%d",
						dphy->sub_sensor[sub_idx].id);

			ret = of_property_read_u32(node, "in_mipi",
				&dphy->sub_sensor[sub_idx].in_mipi);
			if (ret)
				dev_warn(dphy->dev, "Can not get sub sensor in_mipi!");
			else
				dev_info(dphy->dev, "get sub sensor in_mipi:%d",
						dphy->sub_sensor[sub_idx].in_mipi);

			ret = of_property_read_u32(node, "out_mipi",
				&dphy->sub_sensor[sub_idx].out_mipi);
			if (ret)
				dev_warn(dphy->dev, "Can not get sub sensor out_mipi!");
			else
				dev_info(dphy->dev, "get sub sensor out_mipi:%d",
						dphy->sub_sensor[sub_idx].out_mipi);

			sub_idx++;
		}

		of_node_put(prev_node);
		prev_node = node;
	}
	dphy->sub_sensor_num = sub_idx;

	node = of_parse_phandle(parent_node, "link-sensor", 0);
	if (node) {
		dev_info(dphy->dev, "get link sensor node:%s\n", node->full_name);
		link_sensor_client =
			of_find_i2c_device_by_node(node);
		of_node_put(node);
		if (IS_ERR_OR_NULL(link_sensor_client)) {
			dev_err(dphy->dev, "can not get link sensor node\n");
		} else {
			dphy->link_sensor_client = link_sensor_client;
			dev_info(dphy->dev, "get link sensor client\n");
		}
	} else {
		dev_err(dphy->dev, "can not get link-sensor node\n");
	}
	/* get virtual sub sensor end */

	of_node_put(prev_node);
	of_node_put(parent_node);

	return ret;
}

static int rk1608_dphy_probe(struct platform_device *pdev)
{
	struct rk1608_dphy *dphy;
	struct v4l2_subdev *sd;
	struct device_node *node = pdev->dev.of_node;
	char facing[2];
	int ret = 0;

	dphy = devm_kzalloc(&pdev->dev, sizeof(*dphy), GFP_KERNEL);
	if (!dphy)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &dphy->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &dphy->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &dphy->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &dphy->len_name);
	if (ret) {
		dev_err(dphy->dev,
			"could not get module information!\n");
		return -EINVAL;
	}

	dphy->dev = &pdev->dev;
	platform_set_drvdata(pdev, dphy);
	sd = &dphy->sd;
	sd->dev = &pdev->dev;
	v4l2_subdev_init(sd, &dphy_subdev_ops);
	rk1608_dphy_dt_property(dphy);

	memset(facing, 0, sizeof(facing));
	if (strcmp(dphy->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s RK1608-dphy%d",
		 dphy->module_index, facing,
		 RK1608_DPHY_NAME, sd->grp_id);
	rk1608_initialize_controls(dphy);
	sd->internal_ops = &dphy_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dphy->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&sd->entity, 1, &dphy->pad);
	if (ret < 0)
		goto handler_err;
	ret = v4l2_async_register_subdev_sensor_common(sd);
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

MODULE_DEVICE_TABLE(of, dphy_of_match);

static struct platform_driver rk1608_dphy_drv = {
	.driver = {
		.of_match_table = of_match_ptr(dphy_of_match),
		.name	= RK1608_DPHY_NAME,
	},
	.probe		= rk1608_dphy_probe,
	.remove		= rk1608_dphy_remove,
};

module_platform_driver(rk1608_dphy_drv);

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("A DSP driver for rk1608 chip");
MODULE_LICENSE("GPL v2");
