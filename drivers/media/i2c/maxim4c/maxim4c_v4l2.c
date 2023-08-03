// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Quad GMSL Deserializer V4L2 driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include <linux/interrupt.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/compat.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "maxim4c_api.h"

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_PHY_FREQ_MHZ(x)		((x) * 1000000UL)

/* link freq = index * MIPI_PHY_FREQ_MHZ(50) */
static const s64 link_freq_items[] = {
	MIPI_PHY_FREQ_MHZ(0),
	MIPI_PHY_FREQ_MHZ(50),
	MIPI_PHY_FREQ_MHZ(100),
	MIPI_PHY_FREQ_MHZ(150),
	MIPI_PHY_FREQ_MHZ(200),
	MIPI_PHY_FREQ_MHZ(250),
	MIPI_PHY_FREQ_MHZ(300),
	MIPI_PHY_FREQ_MHZ(350),
	MIPI_PHY_FREQ_MHZ(400),
	MIPI_PHY_FREQ_MHZ(450),
	MIPI_PHY_FREQ_MHZ(500),
	MIPI_PHY_FREQ_MHZ(550),
	MIPI_PHY_FREQ_MHZ(600),
	MIPI_PHY_FREQ_MHZ(650),
	MIPI_PHY_FREQ_MHZ(700),
	MIPI_PHY_FREQ_MHZ(750),
	MIPI_PHY_FREQ_MHZ(800),
	MIPI_PHY_FREQ_MHZ(850),
	MIPI_PHY_FREQ_MHZ(900),
	MIPI_PHY_FREQ_MHZ(950),
	MIPI_PHY_FREQ_MHZ(1000),
	MIPI_PHY_FREQ_MHZ(1050),
	MIPI_PHY_FREQ_MHZ(1100),
	MIPI_PHY_FREQ_MHZ(1150),
	MIPI_PHY_FREQ_MHZ(1200),
	MIPI_PHY_FREQ_MHZ(1250),
};

static const struct maxim4c_mode maxim4c_def_mode = {
	.width = 1920,
	.height = 1080,
	.max_fps = {
		.numerator = 10000,
		.denominator = 300000,
	},
	.link_freq_idx = 15,
	.bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
	.bpp = 16,
	.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
	.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
	.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
};

static struct rkmodule_csi_dphy_param rk3588_dcphy_param = {
	.vendor = PHY_VENDOR_SAMSUNG,
	.lp_vol_ref = 3,
	.lp_hys_sw = {3, 0, 0, 0},
	.lp_escclk_pol_sel = {1, 0, 0, 0},
	.skew_data_cal_clk = {0, 0, 0, 0},
	.clk_hs_term_sel = 2,
	.data_hs_term_sel = {2, 2, 2, 2},
	.reserved = {0},
};

static int maxim4c_support_mode_init(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	struct device_node *node = NULL;
	struct maxim4c_mode *mode = NULL;
	u32 value = 0, vc_array[PAD_MAX];
	int ret = 0, i = 0, array_size = 0;

	dev_info(dev, "=== maxim4c support mode init ===\n");

	maxim4c->cfg_modes_num = 1;
	maxim4c->cur_mode = &maxim4c->supported_mode;
	mode = &maxim4c->supported_mode;

	// init using def mode
	memcpy(mode, &maxim4c_def_mode, sizeof(struct maxim4c_mode));

	node = of_get_child_by_name(dev->of_node, "support-mode-config");
	if (IS_ERR_OR_NULL(node)) {
		dev_info(dev, "no mode config node, using default config.\n");

		return 0;
	}

	if (!of_device_is_available(node)) {
		dev_info(dev, "%pOF is disabled, using default config.\n", node);

		of_node_put(node);

		return 0;
	}

	ret = of_property_read_u32(node, "sensor-width", &value);
	if (ret == 0) {
		dev_info(dev, "sensor-width property: %d\n", value);
		mode->width = value;
	}
	dev_info(dev, "support mode: width = %d\n", mode->width);

	ret = of_property_read_u32(node, "sensor-height", &value);
	if (ret == 0) {
		dev_info(dev, "sensor-height property: %d\n", value);
		mode->height = value;
	}
	dev_info(dev, "support mode: height = %d\n", mode->height);

	ret = of_property_read_u32(node, "bus-format", &value);
	if (ret == 0) {
		dev_info(dev, "bus-format property: %d\n", value);
		mode->bus_fmt = value;
	}
	dev_info(dev, "support mode: bus_fmt = 0x%x\n", mode->bus_fmt);

	ret = of_property_read_u32(node, "bpp", &value);
	if (ret == 0) {
		dev_info(dev, "bpp property: %d\n", value);
		mode->bpp = value;
	}
	dev_info(dev, "support mode: bpp = %d\n", mode->bpp);

	ret = of_property_read_u32(node, "max-fps-numerator", &value);
	if (ret == 0) {
		dev_info(dev, "max-fps-numerator property: %d\n", value);
		mode->max_fps.numerator = value;
	}
	dev_info(dev, "support mode: numerator = %d\n", mode->max_fps.numerator);

	ret = of_property_read_u32(node, "max-fps-denominator", &value);
	if (ret == 0) {
		dev_info(dev, "max-fps-denominator property: %d\n", value);
		mode->max_fps.denominator = value;
	}
	dev_info(dev, "support mode: denominator = %d\n", mode->max_fps.denominator);

	ret = of_property_read_u32(node, "link-freq-idx", &value);
	if (ret == 0) {
		dev_info(dev, "link-freq-idx property: %d\n", value);
		mode->link_freq_idx = value;
	}
	dev_info(dev, "support mode: link_freq_idx = %d\n", mode->link_freq_idx);

	ret = of_property_read_u32(node, "hts-def", &value);
	if (ret == 0) {
		dev_info(dev, "hts-def property: %d\n", value);
		mode->hts_def = value;
	}
	dev_info(dev, "support mode: hts_def = %d\n", mode->hts_def);

	ret = of_property_read_u32(node, "vts-def", &value);
	if (ret == 0) {
		dev_info(dev, "vts-def property: %d\n", value);
		mode->vts_def = value;
	}
	dev_info(dev, "support mode: vts_def = %d\n", mode->vts_def);

	ret = of_property_read_u32(node, "exp-def", &value);
	if (ret == 0) {
		dev_info(dev, "exp-def property: %d\n", value);
		mode->exp_def = value;
	}
	dev_info(dev, "support mode: exp_def = %d\n", mode->exp_def);

	array_size = of_property_read_variable_u32_array(node,
				"vc-array", vc_array, 1, PAD_MAX);
	if (array_size > 0) {
		if (array_size > PAD_MAX)
			array_size = PAD_MAX;

		for (i = 0; i < array_size; i++) {
			dev_info(dev, "vc-array[%d] property: 0x%x\n", i, vc_array[i]);
			mode->vc[i] = vc_array[i];
		}
	}
	for (i = 0; i < PAD_MAX; i++)
		dev_info(dev, "support mode: vc[%d] = 0x%x\n", i, mode->vc[i]);

	of_node_put(node);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int maxim4c_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct maxim4c_mode *def_mode = &maxim4c->supported_mode;

	mutex_lock(&maxim4c->mutex);

	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&maxim4c->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int maxim4c_s_power(struct v4l2_subdev *sd, int on)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	struct i2c_client *client = maxim4c->client;
	int ret = 0;

	mutex_lock(&maxim4c->mutex);

	/* If the power state is not modified - no work to do. */
	if (maxim4c->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		maxim4c->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		maxim4c->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&maxim4c->mutex);

	return ret;
}

static void maxim4c_get_module_inf(struct maxim4c *maxim4c,
					struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, MAXIM4C_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, maxim4c->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, maxim4c->len_name, sizeof(inf->base.lens));
}

static void maxim4c_get_vicap_rst_inf(struct maxim4c *maxim4c,
				struct rkmodule_vicap_reset_info *rst_info)
{
	struct i2c_client *client = maxim4c->client;

	rst_info->is_reset = maxim4c->hot_plug;
	maxim4c->hot_plug = false;
	rst_info->src = RKCIF_RESET_SRC_ERR_HOTPLUG;

	dev_info(&client->dev, "%s: rst_info->is_reset:%d.\n",
		__func__, rst_info->is_reset);
}

static void maxim4c_set_vicap_rst_inf(struct maxim4c *maxim4c,
				struct rkmodule_vicap_reset_info rst_info)
{
	maxim4c->is_reset = rst_info.is_reset;
}

static long maxim4c_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	struct rkmodule_csi_dphy_param *dphy_param;
	long ret = 0;

	dev_dbg(&maxim4c->client->dev, "ioctl cmd = 0x%08x\n", cmd);

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		maxim4c_get_module_inf(maxim4c, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		maxim4c_get_vicap_rst_inf(maxim4c,
			(struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		maxim4c_set_vicap_rst_inf(maxim4c,
			*(struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_SET_CSI_DPHY_PARAM:
		dphy_param = (struct rkmodule_csi_dphy_param *)arg;
		if (dphy_param->vendor == rk3588_dcphy_param.vendor)
			rk3588_dcphy_param = *dphy_param;
		dev_dbg(&maxim4c->client->dev, "set dcphy param\n");
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = (struct rkmodule_csi_dphy_param *)arg;
		if (dphy_param->vendor == rk3588_dcphy_param.vendor)
			*dphy_param = rk3588_dcphy_param;
		dev_dbg(&maxim4c->client->dev, "get dcphy param\n");
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long maxim4c_compat_ioctl32(struct v4l2_subdev *sd, unsigned int cmd,
					unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_vicap_reset_info *vicap_rst_inf;
	struct rkmodule_csi_dphy_param *dphy_param;
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = maxim4c_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = maxim4c_ioctl(sd, cmd, vicap_rst_inf);
		if (!ret) {
			ret = copy_to_user(up, vicap_rst_inf, sizeof(*vicap_rst_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(vicap_rst_inf, up, sizeof(*vicap_rst_inf));
		if (!ret)
			ret = maxim4c_ioctl(sd, cmd, vicap_rst_inf);
		else
			ret = -EFAULT;
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_SET_CSI_DPHY_PARAM:
		dphy_param = kzalloc(sizeof(*dphy_param), GFP_KERNEL);
		if (!dphy_param) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(dphy_param, up, sizeof(*dphy_param));
		if (!ret)
			ret = maxim4c_ioctl(sd, cmd, dphy_param);
		else
			ret = -EFAULT;
		kfree(dphy_param);
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = kzalloc(sizeof(*dphy_param), GFP_KERNEL);
		if (!dphy_param) {
			ret = -ENOMEM;
			return ret;
		}

		ret = maxim4c_ioctl(sd, cmd, dphy_param);
		if (!ret) {
			ret = copy_to_user(up, dphy_param, sizeof(*dphy_param));
			if (ret)
				ret = -EFAULT;
		}
		kfree(dphy_param);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif /* CONFIG_COMPAT */

static int __maxim4c_start_stream(struct maxim4c *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	int ret = 0;
	s64 link_freq_hz = 0;
	u8 link_mask = 0, link_freq_idx = 0;
	u8 video_pipe_mask = 0;

	link_mask = maxim4c->gmsl_link.link_enable_mask;
	video_pipe_mask = maxim4c->video_pipe.pipe_enable_mask;

	// disable all remote control
	ret = maxim4c_link_select_remote_control(maxim4c, 0);
	if (ret) {
		dev_err(dev, "link disable remote control error\n");
		return ret;
	}

	// disable all video pipe
	ret = maxim4c_video_pipe_mask_enable(maxim4c, video_pipe_mask, false);
	if (ret) {
		dev_err(dev, "video pipe disable error\n");
		return ret;
	}

	ret = maxim4c_link_select_remote_enable(maxim4c, link_mask);
	if (ret) {
		dev_err(dev, "link select enable error, mask = 0x%x\n", link_mask);
		return ret;
	}

	link_mask = maxim4c->gmsl_link.link_locked_mask;
	ret = maxim4c_remote_devices_init(maxim4c, link_mask);
	if (ret) {
		dev_err(dev, "remote devices init error\n");
		return ret;
	}

	// mipi txphy enable setting: standby or enable
	ret = maxim4c_mipi_txphy_enable(maxim4c, true);
	if (ret) {
		dev_err(dev, "mipi txphy enable error\n");
		return ret;
	}

	// mipi txphy dpll setting
	link_freq_idx = maxim4c->cur_mode->link_freq_idx;
	link_freq_hz = link_freq_items[link_freq_idx];
	ret = maxim4c_dphy_dpll_predef_set(maxim4c, link_freq_hz);
	if (ret) {
		dev_err(dev, "mipi txphy dpll setting error\n");
		return ret;
	}

	// enable video pipe
	ret = maxim4c_video_pipe_mask_enable(maxim4c, video_pipe_mask, true);
	if (ret) {
		dev_err(dev, "video pipe enable error\n");
		return ret;
	}

	ret = maxim4c_link_select_remote_control(maxim4c, link_mask);
	if (ret) {
		dev_err(dev, "remote control enable error\n");
		return ret;
	}

	/* In case these controls are set before streaming */
	mutex_unlock(&maxim4c->mutex);
	ret = v4l2_ctrl_handler_setup(&maxim4c->ctrl_handler);
	mutex_lock(&maxim4c->mutex);
	if (ret)
		return ret;

#if MAXIM4C_TEST_PATTERN
	ret = maxim4c_pattern_enable(maxim4c, true);
	if (ret) {
		dev_err(dev, "test pattern setting error\n");
		return ret;
	}
#endif /* MAXIM4C_TEST_PATTERN */

	ret = maxim4c_mipi_csi_output(maxim4c, true);
	if (ret) {
		dev_err(dev, "mipi csi output error\n");
		return ret;
	}

	if (maxim4c->hot_plug_irq > 0)
		enable_irq(maxim4c->hot_plug_irq);

	return 0;
}

static int __maxim4c_stop_stream(struct maxim4c *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	u8 link_mask = 0, pipe_mask = 0;
	int ret = 0;

	link_mask = maxim4c->gmsl_link.link_enable_mask;
	pipe_mask = maxim4c->video_pipe.pipe_enable_mask;

	if (maxim4c->hot_plug_irq > 0)
		disable_irq(maxim4c->hot_plug_irq);

	if (maxim4c->hot_plug_work.state_check_wq)
		cancel_delayed_work_sync(&maxim4c->hot_plug_work.state_d_work);

	ret |= maxim4c_mipi_csi_output(maxim4c, false);
	ret |= maxim4c_mipi_txphy_enable(maxim4c, false);

#if MAXIM4C_TEST_PATTERN
	ret |= maxim4c_pattern_enable(maxim4c, false);
#endif /* MAXIM4C_TEST_PATTERN */

	ret |= maxim4c_video_pipe_mask_enable(maxim4c, pipe_mask, false);

	ret |= maxim4c_remote_devices_deinit(maxim4c, link_mask);

	ret |= maxim4c_link_select_remote_control(maxim4c, 0);
	ret |= maxim4c_link_mask_enable(maxim4c, link_mask, false);

	if (ret) {
		dev_err(dev, "stop stream error\n");
		return ret;
	}

	return 0;
}

static int maxim4c_s_stream(struct v4l2_subdev *sd, int on)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	struct i2c_client *client = maxim4c->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
		maxim4c->cur_mode->width, maxim4c->cur_mode->height,
		DIV_ROUND_CLOSEST(maxim4c->cur_mode->max_fps.denominator,
				maxim4c->cur_mode->max_fps.numerator));

	mutex_lock(&maxim4c->mutex);
	on = !!on;
	if (on == maxim4c->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __maxim4c_start_stream(maxim4c);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__maxim4c_stop_stream(maxim4c);
		pm_runtime_put(&client->dev);
	}

	maxim4c->streaming = on;

unlock_and_return:
	mutex_unlock(&maxim4c->mutex);

	return ret;
}

static int maxim4c_g_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *fi)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	const struct maxim4c_mode *mode = maxim4c->cur_mode;

	mutex_lock(&maxim4c->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&maxim4c->mutex);

	return 0;
}

static int maxim4c_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *code)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	const struct maxim4c_mode *mode = maxim4c->cur_mode;

	if (code->index != 0)
		return -EINVAL;
	code->code = mode->bus_fmt;

	return 0;
}

static int maxim4c_enum_frame_sizes(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);

	if (fse->index >= maxim4c->cfg_modes_num)
		return -EINVAL;

	if (fse->code != maxim4c->supported_mode.bus_fmt)
		return -EINVAL;

	fse->min_width  = maxim4c->supported_mode.width;
	fse->max_width  = maxim4c->supported_mode.width;
	fse->max_height = maxim4c->supported_mode.height;
	fse->min_height = maxim4c->supported_mode.height;

	return 0;
}

static int
maxim4c_enum_frame_interval(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_frame_interval_enum *fie)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);

	if (fie->index >= maxim4c->cfg_modes_num)
		return -EINVAL;

	fie->code = maxim4c->supported_mode.bus_fmt;
	fie->width = maxim4c->supported_mode.width;
	fie->height = maxim4c->supported_mode.height;
	fie->interval = maxim4c->supported_mode.max_fps;

	return 0;
}

static int maxim4c_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *fmt)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	const struct maxim4c_mode *mode = maxim4c->cur_mode;

	mutex_lock(&maxim4c->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&maxim4c->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && fmt->pad >= PAD0)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&maxim4c->mutex);

	return 0;
}

static int maxim4c_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *fmt)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	struct device *dev = &maxim4c->client->dev;
	const struct maxim4c_mode *mode = NULL;
	u64 link_freq = 0, pixel_rate = 0;
	u8 data_lanes;

	mutex_lock(&maxim4c->mutex);

	mode = &maxim4c->supported_mode;

	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&maxim4c->mutex);
		return -ENOTTY;
#endif
	} else {
		if (maxim4c->streaming) {
			mutex_unlock(&maxim4c->mutex);
			return -EBUSY;
		}

		maxim4c->cur_mode = mode;

		__v4l2_ctrl_s_ctrl(maxim4c->link_freq, mode->link_freq_idx);

		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		link_freq = link_freq_items[mode->link_freq_idx];
		data_lanes = maxim4c->bus_cfg.bus.mipi_csi2.num_data_lanes;
		pixel_rate = (u32)link_freq / mode->bpp * 2 * data_lanes;
		__v4l2_ctrl_s_ctrl_int64(maxim4c->pixel_rate, pixel_rate);

		dev_info(dev, "mipi_freq_idx = %d, mipi_link_freq = %lld\n",
					mode->link_freq_idx, link_freq);
		dev_info(dev, "pixel_rate = %lld, bpp = %d\n",
					pixel_rate, mode->bpp);
	}

	mutex_unlock(&maxim4c->mutex);

	return 0;
}

static int maxim4c_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = maxim4c->cur_mode->width;
		sel->r.top = 0;
		sel->r.height = maxim4c->cur_mode->height;
		return 0;
	}

	return -EINVAL;
}

static int maxim4c_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *config)
{
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	u32 val = 0;
	u8 data_lanes = maxim4c->bus_cfg.bus.mipi_csi2.num_data_lanes;

	val |= V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	val |= (1 << (data_lanes - 1));
	switch (data_lanes) {
	case 4:
		val |= V4L2_MBUS_CSI2_CHANNEL_3;
		fallthrough;
	case 3:
		val |= V4L2_MBUS_CSI2_CHANNEL_2;
		fallthrough;
	case 2:
		val |= V4L2_MBUS_CSI2_CHANNEL_1;
		fallthrough;
	case 1:
	default:
		val |= V4L2_MBUS_CSI2_CHANNEL_0;
		break;
	}

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops maxim4c_internal_ops = {
	.open = maxim4c_open,
};
#endif

static const struct v4l2_subdev_core_ops maxim4c_core_ops = {
	.s_power = maxim4c_s_power,
	.ioctl = maxim4c_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = maxim4c_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops maxim4c_video_ops = {
	.s_stream = maxim4c_s_stream,
	.g_frame_interval = maxim4c_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops maxim4c_pad_ops = {
	.enum_mbus_code = maxim4c_enum_mbus_code,
	.enum_frame_size = maxim4c_enum_frame_sizes,
	.enum_frame_interval = maxim4c_enum_frame_interval,
	.get_fmt = maxim4c_get_fmt,
	.set_fmt = maxim4c_set_fmt,
	.get_selection = maxim4c_get_selection,
	.get_mbus_config = maxim4c_g_mbus_config,
};

static const struct v4l2_subdev_ops maxim4c_subdev_ops = {
	.core = &maxim4c_core_ops,
	.video = &maxim4c_video_ops,
	.pad = &maxim4c_pad_ops,
};

static int maxim4c_initialize_controls(struct maxim4c *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	const struct maxim4c_mode *mode;
	struct v4l2_ctrl_handler *handler;
	u64 link_freq = 0, pixel_rate = 0;
	u8 data_lanes;
	int ret = 0;

	handler = &maxim4c->ctrl_handler;

	ret = v4l2_ctrl_handler_init(handler, 2);
	if (ret)
		return ret;
	handler->lock = &maxim4c->mutex;

	mode = maxim4c->cur_mode;
	maxim4c->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_items) - 1, 0,
				link_freq_items);
	__v4l2_ctrl_s_ctrl(maxim4c->link_freq, mode->link_freq_idx);

	link_freq = link_freq_items[mode->link_freq_idx];
	dev_info(dev, "mipi_freq_idx = %d, mipi_link_freq = %lld\n",
				mode->link_freq_idx, link_freq);

	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	data_lanes = maxim4c->bus_cfg.bus.mipi_csi2.num_data_lanes;
	pixel_rate = (u32)link_freq / mode->bpp * 2 * data_lanes;
	maxim4c->pixel_rate =
		v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE, 0,
				pixel_rate, 1, pixel_rate);
	dev_info(dev, "pixel_rate = %lld, bpp = %d\n",
				pixel_rate, mode->bpp);

	if (handler->error) {
		ret = handler->error;
		dev_err(dev, "Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	maxim4c->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int maxim4c_mipi_data_lanes_parse(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	struct device_node *endpoint;
	u8 mipi_data_lanes;
	int ret = 0;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint),
		&maxim4c->bus_cfg);
	if (ret) {
		dev_err(dev, "Failed to get bus config\n");
		return -EINVAL;
	}
	mipi_data_lanes = maxim4c->bus_cfg.bus.mipi_csi2.num_data_lanes;
	dev_info(dev, "mipi csi2 phy data lanes = %d\n", mipi_data_lanes);

	return 0;
}

int maxim4c_v4l2_subdev_init(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	struct v4l2_subdev *sd = NULL;
	char facing[2];
	int ret = 0;

	maxim4c_mipi_data_lanes_parse(maxim4c);

	maxim4c_support_mode_init(maxim4c);

	sd = &maxim4c->subdev;
	v4l2_i2c_subdev_init(sd, client, &maxim4c_subdev_ops);
	ret = maxim4c_initialize_controls(maxim4c);
	if (ret)
		goto err_free_handler;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &maxim4c_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	maxim4c->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &maxim4c->pad);
	if (ret < 0)
		goto err_free_handler;
#endif

	v4l2_set_subdevdata(sd, maxim4c);

	memset(facing, 0, sizeof(facing));
	if (strcmp(maxim4c->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 maxim4c->module_index, facing, MAXIM4C_NAME,
		 dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif

err_free_handler:
	v4l2_ctrl_handler_free(&maxim4c->ctrl_handler);

	return ret;
}
EXPORT_SYMBOL(maxim4c_v4l2_subdev_init);

void maxim4c_v4l2_subdev_deinit(maxim4c_t *maxim4c)
{
	struct v4l2_subdev *sd = &maxim4c->subdev;

	v4l2_async_unregister_subdev(sd);

#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif

	v4l2_ctrl_handler_free(&maxim4c->ctrl_handler);
}
EXPORT_SYMBOL(maxim4c_v4l2_subdev_deinit);
