// SPDX-License-Identifier: GPL-2.0
/*
 * techpoint v4l2 driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 */

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "techpoint_dev.h"

#define TECHPOINT_NAME  "techpoint"

#define OF_CAMERA_PINCTRL_STATE_DEFAULT		"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP		"rockchip,camera_sleep"

#define I2S				0
#define DSP				1
#define AUDIO_FORMAT			I2S

#define SAMPLE_8K			0
#define SAMPLE_16K			1
#define SAMPLE_RATE			SAMPLE_8K

#define DATA_16BIT			0
#define DATA_8BIT			1
#define DATA_BIT			DATA_16BIT

#define AUDIO_CHN			8
#define MAX_CHIPS			4
#define MAX_SLAVES			(MAX_CHIPS - 1)

#define TECHPOINT_I2C_CHIP_ADDRESS_0	0x44
#define TECHPOINT_I2C_CHIP_ADDRESS_1	0x45

static int g_idx;
static struct techpoint *g_techpoints[MAX_CHIPS];

static const char *const techpoint_supply_names[] = {
	"dovdd",		/* Digital I/O power */
	"avdd",			/* Analog power */
	"dvdd",			/* Digital power */
};

#define TECHPOINT_NUM_SUPPLIES ARRAY_SIZE(techpoint_supply_names)

#define to_techpoint(sd) container_of(sd, struct techpoint, subdev)

static int techpoint_get_regulators(struct techpoint *techpoint)
{
	unsigned int i;
	struct i2c_client *client = techpoint->client;
	struct device *dev = &techpoint->client->dev;

	if (!techpoint->supplies)
		techpoint->supplies = devm_kzalloc(dev,
						   sizeof(struct
							  regulator_bulk_data) *
						   TECHPOINT_NUM_SUPPLIES,
						   GFP_KERNEL);

	for (i = 0; i < TECHPOINT_NUM_SUPPLIES; i++)
		techpoint->supplies[i].supply = techpoint_supply_names[i];
	return devm_regulator_bulk_get(&client->dev,
				       TECHPOINT_NUM_SUPPLIES,
				       techpoint->supplies);
}

static int techpoint_analyze_dts(struct techpoint *techpoint)
{
	int ret;
	struct i2c_client *client = techpoint->client;
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct device_node *endpoint;
	struct fwnode_handle *fwnode;
	int rval;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &techpoint->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &techpoint->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &techpoint->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &techpoint->len_name);
	if (ret) {
		dev_err(dev, "could not get %s!\n", RKMODULE_CAMERA_LENS_NAME);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, TECHPOINT_CAMERA_XVCLK_FREQ,
				   &techpoint->xvclk_freq_value);
	if (ret)
		techpoint->xvclk_freq_value = 27000000;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	fwnode = of_fwnode_handle(endpoint);
	rval = fwnode_property_read_u32_array(fwnode, "data-lanes", NULL, 0);
	if (rval <= 0) {
		dev_err(dev, " Get mipi lane num failed!\n");
		return -EINVAL;
	}
	techpoint->data_lanes = rval;

	techpoint->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(techpoint->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	techpoint->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_HIGH);
	if (IS_ERR(techpoint->power_gpio))
		dev_warn(dev, "Failed to get power-gpios\n");
	else
		gpiod_set_value_cansleep(techpoint->power_gpio, 1);

	techpoint_get_regulators(techpoint);
	ret =
	    regulator_bulk_enable(TECHPOINT_NUM_SUPPLIES, techpoint->supplies);
	if (ret < 0)
		dev_warn(dev, "Failed to enable regulators\n");

	techpoint->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(techpoint->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");
	else
		gpiod_set_value_cansleep(techpoint->reset_gpio, 0);

	techpoint->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(techpoint->pinctrl)) {
		techpoint->pins_default =
		    pinctrl_lookup_state(techpoint->pinctrl,
					 OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(techpoint->pins_default))
			dev_warn(dev, "could not get default pinstate\n");

		techpoint->pins_sleep =
		    pinctrl_lookup_state(techpoint->pinctrl,
					 OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(techpoint->pins_sleep))
			dev_warn(dev, "could not get sleep pinstate\n");
	} else {
		dev_warn(dev, "no pinctrl\n");
	}

	return 0;
}

static int techpoint_initialize_controls(struct techpoint *techpoint)
{
	int ret;
	u64 pixel_rate;
	struct v4l2_ctrl_handler *handler;
	const struct techpoint_video_modes *mode;
	struct device *dev = &techpoint->client->dev;

	handler = &techpoint->ctrl_handler;
	mode = techpoint->cur_video_mode;

	if (techpoint->input_type == TECHPOINT_DVP_BT1120) {
		ret = v4l2_ctrl_handler_init(handler, 1);
		if (ret)
			return ret;
		handler->lock = &techpoint->mutex;
		pixel_rate = mode->link_freq_value;
		techpoint->pixel_rate_ctrl = v4l2_ctrl_new_std(handler, NULL,
							       V4L2_CID_PIXEL_RATE,
							       0, pixel_rate, 1,
							       pixel_rate);
		dev_dbg(dev, "initialize pixel_rate %lld\n", pixel_rate);
	} else if (techpoint->input_type == TECHPOINT_MIPI) {
		ret = v4l2_ctrl_handler_init(handler, 2);
		if (ret)
			return ret;
		handler->lock = &techpoint->mutex;
		techpoint->link_freq_ctrl =
		    v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ, 0,
					   0, &mode->link_freq_value);
		__v4l2_ctrl_s_ctrl(techpoint->link_freq_ctrl, 0);
		dev_dbg(dev, "initialize link_freq %lld\n",
			mode->link_freq_value);

		pixel_rate =
		    (u32) mode->link_freq_value / mode->bpp * 2 * mode->lane;
		techpoint->pixel_rate_ctrl =
		    v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE, 0,
				      pixel_rate, 1, pixel_rate);
		dev_dbg(dev, "initialize pixel_rate %lld\n", pixel_rate);
	}

	if (handler->error) {
		ret = handler->error;
		dev_err(dev, "Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	techpoint->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int __techpoint_power_on(struct techpoint *techpoint)
{
	int ret;
	struct device *dev = &techpoint->client->dev;

	if (!IS_ERR_OR_NULL(techpoint->pins_default)) {
		ret = pinctrl_select_state(techpoint->pinctrl,
					   techpoint->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins. ret=%d\n", ret);
	}

	if (!IS_ERR(techpoint->power_gpio)) {
		gpiod_set_value_cansleep(techpoint->power_gpio, 1);
		usleep_range(25 * 1000, 30 * 1000);
	}

	usleep_range(1500, 2000);

	if (!IS_ERR(techpoint->xvclk)) {
		ret =
		    clk_set_rate(techpoint->xvclk, techpoint->xvclk_freq_value);
		if (ret < 0)
			dev_warn(dev, "Failed to set xvclk rate\n");
		if (clk_get_rate(techpoint->xvclk) !=
		    techpoint->xvclk_freq_value)
			dev_warn(dev, "xvclk mismatched\n");
		ret = clk_prepare_enable(techpoint->xvclk);
		if (ret < 0) {
			dev_err(dev, "Failed to enable xvclk\n");
			goto err_clk;
		}
	}

	if (!IS_ERR(techpoint->reset_gpio)) {
		gpiod_set_value_cansleep(techpoint->reset_gpio, 0);
		usleep_range(10 * 1000, 20 * 1000);
		gpiod_set_value_cansleep(techpoint->reset_gpio, 1);
		usleep_range(10 * 1000, 20 * 1000);
		gpiod_set_value_cansleep(techpoint->reset_gpio, 0);
	}

	usleep_range(10 * 1000, 20 * 1000);

	return 0;

err_clk:
	if (!IS_ERR(techpoint->reset_gpio))
		gpiod_set_value_cansleep(techpoint->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(techpoint->pins_sleep))
		pinctrl_select_state(techpoint->pinctrl, techpoint->pins_sleep);

	if (!IS_ERR(techpoint->power_gpio))
		gpiod_set_value_cansleep(techpoint->power_gpio, 0);

	return ret;
}

static void __techpoint_power_off(struct techpoint *techpoint)
{
	int ret;

#if TECHPOINT_SHARING_POWER
	return;
#endif

	if (!IS_ERR(techpoint->reset_gpio))
		gpiod_set_value_cansleep(techpoint->reset_gpio, 1);

	if (IS_ERR(techpoint->xvclk))
		clk_disable_unprepare(techpoint->xvclk);

	if (!IS_ERR_OR_NULL(techpoint->pins_sleep)) {
		ret = pinctrl_select_state(techpoint->pinctrl,
					   techpoint->pins_sleep);
		if (ret < 0)
			dev_err(&techpoint->client->dev, "could not set pins\n");
	}

	if (!IS_ERR(techpoint->power_gpio))
		gpiod_set_value_cansleep(techpoint->power_gpio, 0);
}

static int techpoint_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct techpoint *techpoint = to_techpoint(sd);

	return __techpoint_power_on(techpoint);
}

static int techpoint_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct techpoint *techpoint = to_techpoint(sd);

	__techpoint_power_off(techpoint);

	return 0;
}

static int techpoint_power(struct v4l2_subdev *sd, int on)
{
	struct techpoint *techpoint = to_techpoint(sd);
	struct i2c_client *client = techpoint->client;
	int ret = 0;

	mutex_lock(&techpoint->mutex);

	/* If the power state is not modified - no work to do. */
	if (techpoint->power_on == !!on)
		goto exit;

	dev_dbg(&client->dev, "%s: on %d\n", __func__, on);

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto exit;
		}
		techpoint->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		techpoint->power_on = false;
	}

exit:
	mutex_unlock(&techpoint->mutex);

	return ret;
}

static int techpoint_get_reso_dist(struct techpoint_video_modes *mode,
				   struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static struct techpoint_video_modes *
techpoint_find_best_fit(struct techpoint *techpoint,
			struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < techpoint->video_modes_num; i++) {
		dist =
		    techpoint_get_reso_dist(&techpoint->video_modes[i],
					    framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
		    techpoint->video_modes[i].bus_fmt == framefmt->code) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &techpoint->video_modes[cur_best_fit];
}

static int techpoint_set_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct techpoint *techpoint = to_techpoint(sd);
	struct techpoint_video_modes *mode;

	mutex_lock(&techpoint->mutex);

	mode = techpoint_find_best_fit(techpoint, fmt);
	techpoint->cur_video_mode = mode;
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_SRGB;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&techpoint->mutex);
		return -ENOTTY;
#endif
	} else {
		if (techpoint->streaming) {
			mutex_unlock(&techpoint->mutex);
			return -EBUSY;
		}
	}

	mutex_unlock(&techpoint->mutex);
	return 0;
}

static int techpoint_get_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct techpoint *techpoint = to_techpoint(sd);
	struct i2c_client *client = techpoint->client;
	const struct techpoint_video_modes *mode = techpoint->cur_video_mode;

	mutex_lock(&techpoint->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&techpoint->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX) {
			if (mode->channel_reso[fmt->pad] == TECHPOINT_S_RESO_SD)
				fmt->format.field = V4L2_FIELD_INTERLACED;
			fmt->reserved[0] = mode->vc[fmt->pad];
		}
	}
	mutex_unlock(&techpoint->mutex);

	dev_dbg(&client->dev, "%s: %x %dx%d\n",
		__func__, fmt->format.code,
		fmt->format.width, fmt->format.height);

	return 0;
}

static int techpoint_enum_mbus_code(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_mbus_code_enum *code)
{
	struct techpoint *techpoint = to_techpoint(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = techpoint->cur_video_mode->bus_fmt;

	return 0;
}

static int techpoint_enum_frame_sizes(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_frame_size_enum *fse)
{
	struct techpoint *techpoint = to_techpoint(sd);

	if (fse->index >= techpoint->video_modes_num)
		return -EINVAL;

	if (fse->code != techpoint->video_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width = techpoint->video_modes[fse->index].width;
	fse->max_width = techpoint->video_modes[fse->index].width;
	fse->max_height = techpoint->video_modes[fse->index].height;
	fse->min_height = techpoint->video_modes[fse->index].height;

	return 0;
}

static int techpoint_g_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_frame_interval *fi)
{
	struct techpoint *techpoint = to_techpoint(sd);

	mutex_lock(&techpoint->mutex);
	fi->interval = techpoint->cur_video_mode->max_fps;
	mutex_unlock(&techpoint->mutex);

	return 0;
}

static int techpoint_g_mbus_config(struct v4l2_subdev *sd,
				   unsigned int pad_id,
				   struct v4l2_mbus_config *cfg)
{
	struct techpoint *techpoint = to_techpoint(sd);

	if (techpoint->input_type == TECHPOINT_DVP_BT1120) {
		cfg->type = V4L2_MBUS_BT656;
		cfg->flags = RKMODULE_CAMERA_BT656_CHANNELS |
			     V4L2_MBUS_PCLK_SAMPLE_RISING |
			     V4L2_MBUS_PCLK_SAMPLE_FALLING;
	} else if (techpoint->input_type == TECHPOINT_MIPI) {
		cfg->type = V4L2_MBUS_CSI2_DPHY;
		if (techpoint->data_lanes == 4) {
			cfg->flags = V4L2_MBUS_CSI2_4_LANE | V4L2_MBUS_CSI2_CHANNELS;
		} else if (techpoint->data_lanes == 2) {
			cfg->flags = V4L2_MBUS_CSI2_2_LANE |
				     V4L2_MBUS_CSI2_CHANNEL_0 |
				     V4L2_MBUS_CSI2_CHANNEL_1;
		}
	}

	return 0;
}

static int techpoint_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct techpoint *techpoint = to_techpoint(sd);

	if (techpoint->input_type == TECHPOINT_DVP_BT1120)
		*std = V4L2_STD_ATSC;

	return 0;
}

static __maybe_unused void techpoint_get_module_inf(struct techpoint *techpoint,
						    struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, TECHPOINT_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, techpoint->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, techpoint->len_name, sizeof(inf->base.lens));
}

static __maybe_unused void
techpoint_get_bt656_module_inf(struct techpoint *techpoint,
			       struct rkmodule_bt656_mbus_info *inf)
{
	memset(inf, 0, sizeof(*inf));
	if (techpoint->input_type == TECHPOINT_DVP_BT1120) {
		inf->id_en_bits = RKMODULE_CAMERA_BT656_ID_EN_BITS_2;
		inf->flags = RKMODULE_CAMERA_BT656_PARSE_ID_LSB |
			     RKMODULE_CAMERA_BT656_CHANNELS;
	}
}

static void techpoint_get_vicap_rst_inf(struct techpoint *techpoint,
					struct rkmodule_vicap_reset_info *rst_info)
{
	rst_info->is_reset = techpoint->do_reset;
	rst_info->src = RKCIF_RESET_SRC_ERR_HOTPLUG;
}

static void techpoint_set_vicap_rst_inf(struct techpoint *techpoint,
					struct rkmodule_vicap_reset_info *rst_info)
{
	techpoint->do_reset = rst_info->is_reset;
}

static long techpoint_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct techpoint *techpoint = to_techpoint(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		techpoint_get_module_inf(techpoint, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_BT656_MBUS_INFO:
		techpoint_get_bt656_module_inf(techpoint,
					       (struct rkmodule_bt656_mbus_info
						*)arg);
		break;
	case RKMODULE_GET_VC_FMT_INFO:
		if (!techpoint->streaming)
			techpoint_get_vc_fmt_inf(techpoint,
						 (struct rkmodule_vc_fmt_info *)
						 arg);
		else
			__techpoint_get_vc_fmt_inf(techpoint,
						   (struct rkmodule_vc_fmt_info
						    *)arg);
		break;
	case RKMODULE_GET_VC_HOTPLUG_INFO:
		techpoint_get_vc_hotplug_inf(techpoint,
					     (struct rkmodule_vc_hotplug_info *)
					     arg);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		*(int *)arg = RKMODULE_START_STREAM_FRONT;
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		techpoint_get_vicap_rst_inf(techpoint,
					    (struct rkmodule_vicap_reset_info *)
					    arg);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		techpoint_set_vicap_rst_inf(techpoint,
					    (struct rkmodule_vicap_reset_info *)
					    arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		techpoint_set_quick_stream(techpoint, *((u32 *)arg));
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long techpoint_compat_ioctl32(struct v4l2_subdev *sd,
				     unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_bt656_mbus_info *bt565_inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_vc_fmt_info *vc_fmt_inf;
	struct rkmodule_vc_hotplug_info *vc_hp_inf;
	struct rkmodule_vicap_reset_info *vicap_rst_inf;
	int *stream_seq;
	u32 stream;
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = techpoint_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(cfg, up, sizeof(*cfg))) {
			kfree(cfg);
			return -EFAULT;
		}
		ret = techpoint_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_VC_FMT_INFO:
		vc_fmt_inf = kzalloc(sizeof(*vc_fmt_inf), GFP_KERNEL);
		if (!vc_fmt_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = techpoint_ioctl(sd, cmd, vc_fmt_inf);
		if (!ret) {
			ret = copy_to_user(up, vc_fmt_inf, sizeof(*vc_fmt_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(vc_fmt_inf);
		break;
	case RKMODULE_GET_VC_HOTPLUG_INFO:
		vc_hp_inf = kzalloc(sizeof(*vc_hp_inf), GFP_KERNEL);
		if (!vc_hp_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = techpoint_ioctl(sd, cmd, vc_hp_inf);
		if (!ret) {
			ret = copy_to_user(up, vc_hp_inf, sizeof(*vc_hp_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(vc_hp_inf);
		break;
	case RKMODULE_GET_BT656_MBUS_INFO:
		bt565_inf = kzalloc(sizeof(*bt565_inf), GFP_KERNEL);
		if (!bt565_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = techpoint_ioctl(sd, cmd, bt565_inf);
		if (!ret) {
			ret = copy_to_user(up, bt565_inf, sizeof(*bt565_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(bt565_inf);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = techpoint_ioctl(sd, cmd, vicap_rst_inf);
		if (!ret) {
			ret = copy_to_user(up, vicap_rst_inf,
					   sizeof(*vicap_rst_inf));
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

		if (copy_from_user(vicap_rst_inf, up, sizeof(*vicap_rst_inf))) {
			kfree(vicap_rst_inf);
			return -EFAULT;
		}
			ret = techpoint_ioctl(sd, cmd, vicap_rst_inf);
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		stream_seq = kzalloc(sizeof(*stream_seq), GFP_KERNEL);
		if (!stream_seq) {
			ret = -ENOMEM;
			return ret;
		}

		ret = techpoint_ioctl(sd, cmd, stream_seq);
		if (!ret) {
			ret = copy_to_user(up, stream_seq, sizeof(*stream_seq));
			if (ret)
				return -EFAULT;
		}
		kfree(stream_seq);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;
		ret = techpoint_ioctl(sd, cmd, &stream);
		if (ret)
			ret = -EFAULT;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static __maybe_unused int __techpoint_start_stream(struct techpoint *techpoint)
{
	techpoint_start_video_stream(techpoint);
	return 0;
}

static __maybe_unused int __techpoint_stop_stream(struct techpoint *techpoint)
{
	techpoint_stop_video_stream(techpoint);
	return 0;
}

static int techpoint_stream(struct v4l2_subdev *sd, int on)
{
	struct techpoint *techpoint = to_techpoint(sd);
	struct i2c_client *client = techpoint->client;

	dev_dbg(&client->dev, "s_stream: %d. %dx%d\n", on,
		techpoint->cur_video_mode->width,
		techpoint->cur_video_mode->height);

	mutex_lock(&techpoint->mutex);
	on = !!on;
	if (techpoint->streaming == on)
		goto unlock;

	if (on)
		__techpoint_start_stream(techpoint);
	else
		__techpoint_stop_stream(techpoint);

	techpoint->streaming = on;

unlock:
	mutex_unlock(&techpoint->mutex);
	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int techpoint_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return 0;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops techpoint_internal_ops = {
	.open = techpoint_open,
};
#endif

static const struct v4l2_subdev_video_ops techpoint_video_ops = {
	.s_stream = techpoint_stream,
	.g_frame_interval = techpoint_g_frame_interval,
	.querystd = techpoint_querystd,
};

static const struct v4l2_subdev_pad_ops techpoint_subdev_pad_ops = {
	.enum_mbus_code = techpoint_enum_mbus_code,
	.enum_frame_size = techpoint_enum_frame_sizes,
	.get_fmt = techpoint_get_fmt,
	.set_fmt = techpoint_set_fmt,
	.get_mbus_config = techpoint_g_mbus_config,
};

static const struct v4l2_subdev_core_ops techpoint_core_ops = {
	.s_power = techpoint_power,
	.ioctl = techpoint_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = techpoint_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_ops techpoint_subdev_ops = {
	.core = &techpoint_core_ops,
	.video = &techpoint_video_ops,
	.pad = &techpoint_subdev_pad_ops,
};

static const struct dev_pm_ops techpoint_pm_ops = {
	SET_RUNTIME_PM_OPS(techpoint_runtime_suspend,
			   techpoint_runtime_resume, NULL)
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id techpoint_of_match[] = {
	{ .compatible = "techpoint,tp2855" },
	{ .compatible = "techpoint,tp2815" },
	{ .compatible = "techpoint,tp9930" },
	{ .compatible = "techpoint,tp9950" },
	{ .compatible = "techpoint,tp9951" },
	{ },
};

MODULE_DEVICE_TABLE(of, techpoint_of_match);
#endif

static const struct i2c_device_id techpoint_match_id[] = {
	{ "techpoint", 0 },
	{ },
};

static int techpoint_9930_audio_init(struct techpoint *techpoint);

static struct snd_soc_dai_driver techpoint_audio_dai = {
	.name = "techpoint",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 16,
		.rates = SNDRV_PCM_RATE_8000_384000,
		.formats = (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE),
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 16,
		.rates = SNDRV_PCM_RATE_8000_384000,
		.formats = (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE),
	},
};

static ssize_t i2c_rdwr_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct techpoint *techpoint =
		container_of(dev, struct techpoint, dev);
	unsigned char op_type;
	unsigned int reg, v;
	int ret;

	ret = sscanf(buf, "%c %x %x", &op_type, &reg, &v);
	if (ret != 3) {
		dev_err(&techpoint->client->dev, "%s sscanf failed: %d\n",
			__func__, ret);
		return -EFAULT;
	}

	if (op_type == 'r')
		techpoint_read_reg(techpoint->client, reg, (unsigned char *)&v);
	else if (op_type == 'w')
		techpoint_write_reg(techpoint->client, reg, v);
	else if (op_type == 'd')
		techpoint_9930_audio_init(techpoint);

	return count;
}

static const struct device_attribute techpoint_attrs[] = {
	__ATTR_WO(i2c_rdwr),
};

static int techpoint_codec_probe(struct snd_soc_component *component)
{
	return 0;
}

static void techpoint_codec_remove(struct snd_soc_component *component)
{
}

static const struct snd_soc_component_driver techpoint_codec_driver = {
	.probe			= techpoint_codec_probe,
	.remove			= techpoint_codec_remove,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int tp2833_audio_config_rmpos(struct i2c_client *client,
		unsigned int chip, unsigned int format, unsigned int chn_num)
{
	int i = 0;
	unsigned char v;

	/* clear first */
	for (i = 0; i < 20; i++)
		techpoint_write_reg(client, i, 0x00);

	switch (chn_num) {

	case 2:
		if (format == DSP) {
			techpoint_write_reg(client, 0x0, 1);
			techpoint_write_reg(client, 0x1, 2);
		} else {
			techpoint_write_reg(client, 0x0, 1);
			techpoint_write_reg(client, 0x8, 2);
		}
		break;

	case 4:
		if (format == DSP) {
			techpoint_write_reg(client, 0x0, 1);
			techpoint_write_reg(client, 0x1, 2);
			techpoint_write_reg(client, 0x2, 3);
			techpoint_write_reg(client, 0x3, 4);
		} else {
			techpoint_write_reg(client, 0x0, 1);
			techpoint_write_reg(client, 0x1, 3);
			techpoint_write_reg(client, 0x8, 2);
			techpoint_write_reg(client, 0x9, 4);
		}
		break;

	case 8:
		if (chip % 4 == 0) {
			if (format == DSP) {
				techpoint_write_reg(client, 0x0, 1);
				techpoint_write_reg(client, 0x1, 2);
				techpoint_write_reg(client, 0x2, 3);
				techpoint_write_reg(client, 0x3, 4);
				techpoint_write_reg(client, 0x4, 5);
				techpoint_write_reg(client, 0x5, 6);
				techpoint_write_reg(client, 0x6, 7);
				techpoint_write_reg(client, 0x7, 8);
			} else {
				techpoint_write_reg(client, 0x0, 1);
				techpoint_write_reg(client, 0x1, 2);
				techpoint_write_reg(client, 0x2, 3);
				techpoint_write_reg(client, 0x3, 4);
				techpoint_write_reg(client, 0x8, 5);
				techpoint_write_reg(client, 0x9, 6);
				techpoint_write_reg(client, 0xa, 7);
				techpoint_write_reg(client, 0xb, 8);
			}
		} else if (chip % 4 == 1) {
			if (format == DSP) {
				techpoint_write_reg(client, 0x0, 0);
				techpoint_write_reg(client, 0x1, 0);
				techpoint_write_reg(client, 0x2, 0);
				techpoint_write_reg(client, 0x3, 0);
				techpoint_write_reg(client, 0x4, 1);
				techpoint_write_reg(client, 0x5, 2);
				techpoint_write_reg(client, 0x6, 3);
				techpoint_write_reg(client, 0x7, 4);
			} else {
				techpoint_write_reg(client, 0x0, 0);
				techpoint_write_reg(client, 0x1, 0);
				techpoint_write_reg(client, 0x2, 1);
				techpoint_write_reg(client, 0x3, 2);
				techpoint_write_reg(client, 0x8, 0);
				techpoint_write_reg(client, 0x9, 0);
				techpoint_write_reg(client, 0xa, 3);
				techpoint_write_reg(client, 0xb, 4);
				techpoint_read_reg(client, 0x3, &v);
			}
		}
		break;

	case 16:
		if (chip % 4 == 0) {
			for (i = 0; i < 16; i++)
				techpoint_write_reg(client, i, i+1);
		} else if (chip % 4 == 1) {
			for (i = 4; i < 16; i++)
				techpoint_write_reg(client, i, i+1 - 4);
		} else if (chip % 4 == 2) {
			for (i = 8; i < 16; i++)
				techpoint_write_reg(client, i, i+1 - 8);
		} else {
			for (i = 12; i < 16; i++)
				techpoint_write_reg(client, i, i+1 - 12);
		}
		break;

	case 20:
		for (i = 0; i < 20; i++)
			techpoint_write_reg(client, i, i+1);
		break;

	default:
		for (i = 0; i < 20; i++)
			techpoint_write_reg(client, i, i+1);
		break;
	}

	mdelay(10);
	return 0;
}

static int techpoint_2855_audio_init(struct techpoint *techpoint)
{
	struct i2c_client *client = techpoint->client;

	unsigned char bank;
	unsigned char chip_id_h = 0xFF, chip_id_l = 0xFF;

	techpoint_read_reg(client, CHIP_ID_H_REG, &chip_id_h);
	techpoint_read_reg(client, CHIP_ID_L_REG, &chip_id_l);

	techpoint_read_reg(client, 0x40, &bank);
	techpoint_write_reg(client, 0x40, 0x40);

	tp2833_audio_config_rmpos(client, 0, AUDIO_FORMAT, AUDIO_CHN);

	techpoint_write_reg(client, 0x17, 0x00|(DATA_BIT<<2));
	techpoint_write_reg(client, 0x1B, 0x01|(DATA_BIT<<6));

#if (AUDIO_CHN == 20)
	techpoint_write_reg(client, 0x18, 0x90|(SAMPLE_RATE));
#else
	techpoint_write_reg(client, 0x18, 0x80|(SAMPLE_RATE));
#endif

#if (AUDIO_CHN >= 8)
	techpoint_write_reg(client, 0x19, 0x1F);
#else
	techpoint_write_reg(client, 0x19, 0x0F);
#endif

	techpoint_write_reg(client, 0x1A, 0x15);

	techpoint_write_reg(client, 0x37, 0x20);
	techpoint_write_reg(client, 0x38, 0x38);
	techpoint_write_reg(client, 0x3E, 0x00);

	/* audio reset */
	techpoint_write_reg(client, 0x3d, 0x01);

	techpoint_write_reg(client, 0x40, bank);

	return 0;
}

static int techpoint_9930_audio_init(struct techpoint *techpoint)
{
	struct i2c_client *client = techpoint->client;

	unsigned char bank;
	unsigned char chip_id_h = 0xFF;
	unsigned char chip_id_l = 0xFF;

	techpoint_read_reg(client, CHIP_ID_H_REG, &chip_id_h);
	techpoint_read_reg(client, CHIP_ID_L_REG, &chip_id_l);

	techpoint_read_reg(client, 0x40, &bank);
	techpoint_write_reg(client, 0x40, 0x40);

	tp2833_audio_config_rmpos(client, 1, AUDIO_FORMAT, AUDIO_CHN);

	techpoint_write_reg(client, 0x17, 0x00|(DATA_BIT<<2));
	techpoint_write_reg(client, 0x1B, 0x01|(DATA_BIT<<6));

#if (AUDIO_CHN == 20)
	techpoint_write_reg(client, 0x18, 0x90|(SAMPLE_RATE));
#else
	techpoint_write_reg(client, 0x18, 0x80|(SAMPLE_RATE));
#endif

#if (AUDIO_CHN >= 8)
	techpoint_write_reg(client, 0x19, 0x1F);
#else
	techpoint_write_reg(client, 0x19, 0x0F);
#endif

	techpoint_write_reg(client, 0x1A, 0x15);
	techpoint_write_reg(client, 0x37, 0x20);
	techpoint_write_reg(client, 0x38, 0x38);
	techpoint_write_reg(client, 0x3E, 0x00);

	/* reset audio */
	techpoint_write_reg(client, 0x3d, 0x01);

	techpoint_write_reg(client, 0x40, bank);

	return 0;
}

static int techpoint_audio_init(struct techpoint *techpoint)
{
	if (techpoint)
		techpoint_2855_audio_init(techpoint);

	if (techpoint && techpoint->audio_in)
		techpoint_9930_audio_init(techpoint->audio_in->slave_tp[0]);

	return 0;
}

static int techpoint_audio_dt_parse(struct techpoint *techpoint)
{
	struct device *dev = &techpoint->client->dev;
	struct device_node *node = dev->of_node;
	const char *str;
	u32 v;

	/* Parse audio parts */
	techpoint->audio_in = NULL;
	if (!of_property_read_string(node, "techpoint,audio-in-format", &str)) {
		struct techpoint_audio *audio_stream;

		techpoint->audio_in = devm_kzalloc(dev, sizeof(struct techpoint_audio),
						   GFP_KERNEL);
		if (!techpoint->audio_in)
			return -ENOMEM;

		audio_stream = techpoint->audio_in;

		if (strcmp(str, "i2s") == 0)
			audio_stream->audfmt = AUDFMT_I2S;
		else if (strcmp(str, "dsp") == 0)
			audio_stream->audfmt = AUDFMT_DSP;
		else {
			dev_err(dev, "techpoint,audio-in-format invalid\n");
			return -EINVAL;
		}

		if (!of_property_read_u32(node, "techpoint,audio-in-mclk-fs", &v)) {
			switch (v) {
			case 256:
				break;
			default:
				dev_err(dev,
					"techpoint,audio-in-mclk-fs invalid\n");
				return -EINVAL;
			}
			audio_stream->mclk_fs = v;
		}

		if (!of_property_read_u32(node, "techpoint,audio-in-cascade-num", &v))
			audio_stream->cascade_num = v;

		if (!of_property_read_u32(node, "techpoint,audio-in-cascade-order", &v)) {
			if (v > 1)
				dev_err(dev,
					"audio-in-cascade-order should be 1st chip, otherwise without cascade (is 0)\n");
			else
				audio_stream->cascade_order = v;
		}

		if (audio_stream->cascade_order == 1) {
			struct device_node *np;
			int i, count;

			count = of_count_phandle_with_args(node,
							   "techpoint,audio-in-cascade-slaves",
							   NULL);
			if (count < 0 || count > MAX_SLAVES)
				return -EINVAL;

			for (i = 0; i < count; i++) {
				np = of_parse_phandle(node, "techpoint,audio-in-cascade-slaves", i);
				if (!np)
					return -ENODEV;
			}

			for (i = 0; i < g_idx; i++) {
				struct techpoint *tp = g_techpoints[i];

				if (tp->i2c_idx != techpoint->i2c_idx) {
					audio_stream->slave_tp[i] = tp;
					audio_stream->slave_num++;
				}
			}
		}
	}

	techpoint->audio_out = NULL;
	if (!of_property_read_string(node, "techpoint,audio-out-format", &str)) {
		struct techpoint_audio *audio_stream;

		techpoint->audio_out = devm_kzalloc(dev, sizeof(struct techpoint_audio),
						    GFP_KERNEL);
		if (!techpoint->audio_out)
			return -ENOMEM;

		audio_stream = techpoint->audio_out;

		if (strcmp(str, "i2s") == 0)
			audio_stream->audfmt = AUDFMT_I2S;
		else if (strcmp(str, "dsp") == 0)
			audio_stream->audfmt = AUDFMT_DSP;
		else {
			dev_err(dev, "techpoint,audio-out-format invalid\n");
			return -EINVAL;
		}

		if (!of_property_read_u32(node, "techpoint,audio-out-mclk-fs", &v)) {
			switch (v) {
			case 256:
				break;
			default:
				dev_err(dev,
					"techpoint,audio-out-mclk-fs invalid\n");
				return -EINVAL;
			}
			audio_stream->mclk_fs = v;
		}

		if (!of_property_read_u32(node, "techpoint,audio-out-cascade-num", &v))
			audio_stream->cascade_num = v;

		if (!of_property_read_u32(node, "techpoint,audio-out-cascade-order", &v)) {
			if (v > 1)
				dev_err(dev,
					"audio-out-cascade-order should be 1st chip, otherwise without cascade (is 0)\n");
			else
				audio_stream->cascade_order = v;
		}
	}

	if (!techpoint->audio_in && !techpoint->audio_out)
		return -ENODEV;

	return 0;
}

static int techpoint_audio_probe(struct techpoint *techpoint)
{
	struct device *dev = &techpoint->client->dev;
	int ret;
	unsigned char i;

	switch (techpoint->chip_id) {
	case CHIP_TP9930:
		techpoint_9930_audio_init(techpoint);
		break;
	case CHIP_TP2855:
		techpoint_2855_audio_init(techpoint);
		break;
	default:
		break;
	}

	if (techpoint->chip_id == CHIP_TP9930) {
		techpoint_write_reg(techpoint->client, 0x40, 0x00);
		for (i = 0; i < 0xff; i++)
			techpoint_write_reg(techpoint->client, i, 0xbb);
	}

	ret = techpoint_audio_dt_parse(techpoint);
	if (ret) {
		dev_info(dev, "hasn't audio DT nodes\n");
		return 0;
	}

	ret = techpoint_audio_init(techpoint);
	if (ret) {
		dev_info(dev, "audio init failed(%d)\n", ret);
		return 0;
	}

	ret = devm_snd_soc_register_component(dev,
					      &techpoint_codec_driver,
					      &techpoint_audio_dai, 1);
	if (ret) {
		dev_err(dev, "register audio codec failed\n");
		return -EINVAL;
	}

	dev_info(dev, "registered audio codec\n");

	return 0;
}

static void techpoint_device_release(struct device *dev)
{

}

static int techpoint_sysfs_init(struct i2c_client *client,
				struct techpoint *techpoint)
{
	struct device *dev = &techpoint->dev;
	int i;

	dev->release = techpoint_device_release;
	dev->parent = &client->dev;
	set_dev_node(dev, dev_to_node(&client->dev));
	dev_set_name(dev, "techpoint-dev");

	if (device_register(dev)) {
		dev_err(&client->dev,
			"Register 'techpoint-dev' failed\n");
		dev->parent = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(techpoint_attrs); i++) {
		if (device_create_file(dev, &techpoint_attrs[i])) {
			dev_err(&client->dev,
				"Create 'techpoint-dev' attr failed\n");
			device_unregister(dev);
			return -ENOMEM;
		}
	}

	return 0;
}

static int techpoint_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct techpoint *techpoint;
	struct v4l2_subdev *sd;
	__maybe_unused char facing[2];
	int ret = 0, index;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8, DRIVER_VERSION & 0x00ff);

	techpoint = devm_kzalloc(dev, sizeof(*techpoint), GFP_KERNEL);
	if (!techpoint)
		return -ENOMEM;

	techpoint->client = client;
	techpoint->supplies = NULL;

	techpoint_sysfs_init(client, techpoint);

	mutex_init(&techpoint->mutex);

	sd = &techpoint->subdev;
	v4l2_i2c_subdev_init(sd, client, &techpoint_subdev_ops);

	techpoint_analyze_dts(techpoint);

	ret = __techpoint_power_on(techpoint);
	if (ret) {
		dev_err(dev, "Failed to power on techpoint\n");
		goto err_destroy_mutex;
	}

	ret = techpoint_initialize_devices(techpoint);
	if (ret) {
		dev_err(dev, "Failed to initialize techpoint device\n");
		goto err_power_off;
	}

	ret = techpoint_initialize_controls(techpoint);
	if (ret) {
		dev_err(dev, "Failed to initialize controls techpoint\n");
		goto err_free_handler;
	}
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &techpoint_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	for (index = 0; index < PAD_MAX; index++)
		techpoint->pad[index].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, PAD_MAX, techpoint->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(techpoint->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 techpoint->module_index, facing,
		 TECHPOINT_NAME, dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	techpoint->i2c_idx = g_idx;
	g_techpoints[g_idx++] = techpoint;

	ret = techpoint_audio_probe(techpoint);
	if (ret) {
		dev_err(dev, "sound audio probe failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_free_handler:
	v4l2_ctrl_handler_free(&techpoint->ctrl_handler);
err_power_off:
	__techpoint_power_off(techpoint);
err_destroy_mutex:
	mutex_destroy(&techpoint->mutex);

	return ret;
}

static int techpoint_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct techpoint *techpoint = to_techpoint(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&techpoint->ctrl_handler);
	mutex_destroy(&techpoint->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__techpoint_power_off(techpoint);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static struct i2c_driver techpoint_i2c_driver = {
	.driver = {
		   .name = TECHPOINT_NAME,
		   .pm = &techpoint_pm_ops,
		   .of_match_table = of_match_ptr(techpoint_of_match),
		    },
	.probe = &techpoint_probe,
	.remove = &techpoint_remove,
	.id_table = techpoint_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&techpoint_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&techpoint_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_AUTHOR("Vicent Chi <vicent.chi@rock-chips.com>");
MODULE_DESCRIPTION("Techpoint decoder driver");
MODULE_LICENSE("GPL");
