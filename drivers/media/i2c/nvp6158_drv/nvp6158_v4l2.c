// SPDX-License-Identifier: GPL-2.0
/*
 * nvp6158_v4l2 interface driver
 *
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01
 * 1. add workqueue to detect ahd state.
 * 2. add more resolution support.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/mfd/syscon.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <linux/rk-preisp.h>

#include "nvp6158_common.h"
#include "nvp6158_video.h"
#include "nvp6158_coax_protocol.h"
#include "nvp6158_motion.h"
#include "nvp6158_video_eq.h"
#include "nvp6158_drv.h"
#include "nvp6158_audio.h"
#include "nvp6158_video_auto_detect.h"
#include "nvp6158_drv.h"

//#define WORK_QUEUE

#ifdef WORK_QUEUE
#include <linux/workqueue.h>

struct sensor_state_check_work {
	struct workqueue_struct *state_check_wq;
	struct delayed_work d_work;
};

#endif

#define DRIVER_VERSION				KERNEL_VERSION(0, 0x01, 0x1)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN			V4L2_CID_GAIN
#endif

#define NVP6158_XVCLK_FREQ			24000000
#define NVP6158_BITS_PER_SAMPLE			8

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define NVP6158_PIXEL_RATE			297000000LL

#define OF_CAMERA_PINCTRL_STATE_DEFAULT		"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP		"rockchip,camera_sleep"

#define OF_CAMERA_MODULE_REGULATORS		"rockchip,regulator-names"
#define OF_CAMERA_MODULE_REGULATOR_VOLTAGES	"rockchip,regulator-voltages"

/* DVP MODE, BT1120 or BT656 */
#define RK_CAMERA_MODULE_DVP_MODE		"rockchip,dvp_mode"
#define RK_CAMERA_MODULE_CHANNEL_NUMS		"rockchip,channel_nums"
#define RK_CAMERA_MODULE_DUAL_EDGE		"rockchip,dual_edge"
#define RK_CAMERA_MODULE_DEFAULT_RECT		"rockchip,default_rect"

#define NVP6158_DEFAULT_DVP_MODE		"BT1120"
#define NVP6158_DEFAULT_CHANNEL_NUMS		4U
#define NVP6158_DEFAULT_DUAL_EDGE		0U
#define NVP6158_NAME				"nvp6158"
#define NVP6158_DEFAULT_WIDTH			1920
#define NVP6158_DEFAULT_HEIGHT			1080

struct nvp6158_gpio {
	int pltfrm_gpio;
	const char *label;
	enum of_gpio_flags active_low;
};

struct nvp6158_regulator {
	struct regulator *regulator;
	u32 min_uV;
	u32 max_uV;
};

struct nvp6158_regulators {
	u32 cnt;
	struct nvp6158_regulator *regulator;
};

struct nvp6158_pixfmt {
	u32 code;
};

struct nvp6158_framesize {
	u16 width;
	u16 height;
	NC_VIVO_CH_FORMATDEF fmt_idx;
	struct v4l2_fract max_fps;
};

struct nvp6158_default_rect {
	unsigned int width;
	unsigned int height;
};

#ifdef WORK_QUEUE
enum nvp6158_hot_plug_state {
	PLUG_IN = 0,
	PLUG_OUT,
	PLUG_STATE_MAX,
};
#endif

struct nvp6158 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*pwr_gpio;
	struct gpio_desc	*pwr2_gpio;
	struct gpio_desc	*rst_gpio;
	struct gpio_desc	*rst2_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct gpio_desc	*pwdn2_gpio;

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad[PAD_MAX];
	struct v4l2_ctrl_handler ctrl_handler;
	struct mutex		mutex;
	bool			power_on;
	struct nvp6158_regulators regulators;

	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	const char		*dvp_mode;
	NVP6158_DVP_MODE	mode;
	u32			ch_nums;
	u32			dual_edge;

	struct v4l2_mbus_framefmt format;
	const struct nvp6158_framesize *frame_size;
	int streaming;
	struct nvp6158_default_rect defrect;
#ifdef WORK_QUEUE
	struct sensor_state_check_work plug_state_check;
	u8 cur_detect_status;
	u8 last_detect_status;
#endif
	bool hot_plug;
	u8 is_reset;

};

#define to_nvp6158(sd) container_of(sd, struct nvp6158, subdev)

static const struct nvp6158_framesize nvp6158_framesizes[] = {
	{
		.width		= 1280,
		.height		= 720,
		.fmt_idx	= AHD20_720P_30P,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
	}, {
		.width		= 1920,
		.height		= 1080,
		.fmt_idx	= AHD20_1080P_25P,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
	}, {
		.width		= 2048,
		.height		= 1536,
		.fmt_idx	= AHD30_3M_18P,
		.max_fps = {
			.numerator = 10000,
			.denominator = 180000,
		},
	}, {
		.width		= 1280,
		.height		= 1440,
		.fmt_idx	= AHD30_4M_30P,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
	}, {
		.width		= 2560,
		.height		= 1440,
		.fmt_idx	= AHD30_4M_15P,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
	}, {
		.width		= 2592,
		.height		= 1944,
		.fmt_idx	= AHD30_5M_12_5P,
		.max_fps = {
			.numerator = 10000,
			.denominator = 125000,
		},
	}, {
		.width		= 3840,
		.height		= 2160,
		.fmt_idx	= AHD30_8M_7_5P,
		.max_fps = {
			.numerator = 10000,
			.denominator = 75000,
		},
	}, {/* test modes, Interlace mode*/
		.width		= 720,
		.height		= 480,
		.fmt_idx	= AHD20_SD_SH720_NT,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
	}, {
		.width		= 720,
		.height		= 576,
		.fmt_idx	= AHD20_SD_SH720_PAL,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
	}, {
		.width		= 960,
		.height		= 576,
		.fmt_idx	= AHD20_SD_H960_PAL,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
	}, {
		.width		= 1920,
		.height		= 576,
		.fmt_idx	= AHD20_SD_H960_EX_PAL,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
	}
};

static char *nvp6158_dvp_mode_lists[] = {
	[BT601] = "BT601",
	[BT656_1MUX] = "BT656_1MUX",
	[BT656_2MUX] = "BT656_2MUX",
	[BT656_4MUX] = "BT656_4MUX",
	[BT1120_1MUX] = "BT1120_1MUX",
	[BT1120_2MUX] = "BT1120_2MUX",
	[BT1120_4MUX] = "BT1120_4MUX",
	[BT656I_TEST_MODES] = "BT656I_TEST_MODES"
};

static const struct nvp6158_pixfmt nvp6158_formats[] = {
	{
		.code = MEDIA_BUS_FMT_UYVY8_2X8
	},
};

static int nvp6158_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct nvp6158 *nvp6158 = to_nvp6158(sd);

	if ((nvp6158->mode > BT656I_TEST_MODES) &&
		(nvp6158->mode < NVP6158_DVP_MODES_END)) {
		/* for vicap detect bt1120 */
		*std = V4L2_STD_ATSC;
	} else {
		*std = V4L2_STD_PAL;
	}
	return 0;
}

/* sensor register write */
static int nvp6158_write(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	dev_info(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);
	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev,
		"nvp6158 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

/* sensor register read */
static int nvp6158_read(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev, "nvp6158 read reg(0x%x) failed !\n", reg);

	return ret;
}

static int __nvp6158_power_on(struct nvp6158 *nvp6158)
{
	u32 i;
	int ret;
	struct nvp6158_regulator *regulator;
	struct device *dev = &nvp6158->client->dev;

	dev_info(dev, "%s(%d)\n", __func__, __LINE__);

	if (!IS_ERR_OR_NULL(nvp6158->pins_default)) {
		ret = pinctrl_select_state(nvp6158->pinctrl,
					   nvp6158->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins. ret=%d\n", ret);
	}

	ret = clk_prepare_enable(nvp6158->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (nvp6158->regulators.regulator) {
		for (i = 0; i < nvp6158->regulators.cnt; i++) {
			regulator = nvp6158->regulators.regulator + i;
			if (IS_ERR(regulator->regulator))
				continue;
			regulator_set_voltage(
				regulator->regulator,
				regulator->min_uV,
				regulator->max_uV);
			if (regulator_enable(regulator->regulator)) {
				dev_err(dev,
					"regulator_enable failed!\n");
				goto disable_clk;
			}
		}
	}
	usleep_range(3000, 5000);

	if (!IS_ERR(nvp6158->pwr_gpio)) {
		gpiod_direction_output(nvp6158->pwr_gpio, 1);
		usleep_range(3000, 5000);
	}

	if (!IS_ERR(nvp6158->pwr2_gpio)) {
		gpiod_direction_output(nvp6158->pwr2_gpio, 1);
		usleep_range(3000, 5000);
	}

	if (!IS_ERR(nvp6158->pwdn_gpio)) {
		gpiod_direction_output(nvp6158->pwdn_gpio, 1);
		usleep_range(1500, 2000);
	}

	if (!IS_ERR(nvp6158->pwdn2_gpio)) {
		gpiod_direction_output(nvp6158->pwdn2_gpio, 1);
		usleep_range(1500, 2000);
	}

	if (!IS_ERR(nvp6158->rst_gpio)) {
		gpiod_direction_output(nvp6158->rst_gpio, 0);
		usleep_range(50000, 100000);
		gpiod_direction_output(nvp6158->rst_gpio, 1);
		usleep_range(3000, 5000);
	}

	if (!IS_ERR(nvp6158->rst2_gpio)) {
		gpiod_direction_output(nvp6158->rst2_gpio, 0);
		usleep_range(1500, 2000);
		gpiod_direction_output(nvp6158->rst2_gpio, 1);
		usleep_range(3000, 5000);
	}

	return 0;

disable_clk:
	clk_disable_unprepare(nvp6158->xvclk);

	return ret;
}

static void __nvp6158_power_off(struct nvp6158 *nvp6158)
{
	u32 i;
	int ret;
	struct nvp6158_regulator *regulator;
	struct device *dev = &nvp6158->client->dev;

	dev_info(dev, "%s(%d)\n", __func__, __LINE__);
	clk_disable_unprepare(nvp6158->xvclk);

	if (!IS_ERR(nvp6158->rst_gpio))
		gpiod_direction_output(nvp6158->rst_gpio, 0);

	if (!IS_ERR(nvp6158->rst2_gpio))
		gpiod_direction_output(nvp6158->rst2_gpio, 0);

	if (!IS_ERR(nvp6158->pwdn_gpio))
		gpiod_direction_output(nvp6158->pwdn_gpio, 0);

	if (!IS_ERR(nvp6158->pwdn_gpio))
		gpiod_direction_output(nvp6158->pwdn2_gpio, 0);

	if (!IS_ERR(nvp6158->pwr_gpio))
		gpiod_direction_output(nvp6158->pwr_gpio, 0);

	if (!IS_ERR(nvp6158->pwr2_gpio))
		gpiod_direction_output(nvp6158->pwr2_gpio, 0);

	if (!IS_ERR_OR_NULL(nvp6158->pins_sleep)) {
		ret = pinctrl_select_state(nvp6158->pinctrl,
					   nvp6158->pins_sleep);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	if (nvp6158->regulators.regulator) {
		for (i = 0; i < nvp6158->regulators.cnt; i++) {
			regulator = nvp6158->regulators.regulator + i;
			if (IS_ERR(regulator->regulator))
				continue;
			regulator_disable(regulator->regulator);
		}
	}
}

static int nvp6158_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct nvp6158 *nvp6158 = to_nvp6158(sd);
	int ret = 0;

	dev_info(&client->dev, "%s: on %d\n", __func__, on);
	mutex_lock(&nvp6158->mutex);

	/* If the power state is not modified - no work to do. */
	if (nvp6158->power_on == !!on)
		goto exit;

	if (on) {
		ret = __nvp6158_power_on(nvp6158);
		if (ret < 0)
			goto exit;

		nvp6158->power_on = true;
	} else {
		__nvp6158_power_off(nvp6158);
		nvp6158->power_on = false;
	}

exit:
	mutex_unlock(&nvp6158->mutex);

	return ret;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */
static int nvp6158_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	struct nvp6158 *nvp6158 = to_nvp6158(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = CROP_START(0, 0);
		sel->r.width = nvp6158->frame_size->width;
		sel->r.top = CROP_START(0, 0);
		sel->r.height = nvp6158->frame_size->height;
		return 0;
	}
	return -EINVAL;
}

static int nvp6158_initialize_controls(struct nvp6158 *nvp6158)
{
	struct v4l2_ctrl_handler *handler;
	int ret;

	handler = &nvp6158->ctrl_handler;
	ret = v4l2_ctrl_handler_init(handler, 2);
	if (ret)
		return ret;
	handler->lock = &nvp6158->mutex;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, NVP6158_PIXEL_RATE, 1, NVP6158_PIXEL_RATE);

	if (handler->error) {
		ret = handler->error;
		dev_err(&nvp6158->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	nvp6158->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}
static void nvp6158_get_default_format(struct nvp6158 *nvp6158)
{

	const struct nvp6158_framesize *fsize = &nvp6158_framesizes[0];
	const struct nvp6158_framesize *match = NULL;
	int i = ARRAY_SIZE(nvp6158_framesizes);
	unsigned int min_err = UINT_MAX;
	struct v4l2_mbus_framefmt *format = &nvp6158->format;
	struct nvp6158_default_rect *rect =  &nvp6158->defrect;

	while (i--) {
		unsigned int err = abs(fsize->width - rect->width)
				+ abs(fsize->height - rect->height);
		if (err < min_err) {
			min_err = err;
			match = fsize;
		}
		fsize++;
	}

	if (!match)
		match = &nvp6158_framesizes[0];

	format->width = match->width;
	format->height = match->height;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->code = nvp6158_formats[0].code;
	if (BT656I_TEST_MODES == nvp6158->mode)
		format->field = V4L2_FIELD_INTERLACED;
	else
		format->field = V4L2_FIELD_NONE;
	nvp6158->frame_size = match;
}

static int nvp6158_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct nvp6158 *nvp6158 = to_nvp6158(sd);
	video_init_all video_init;
	NC_VIVO_CH_FORMATDEF fmt_idx;
	int ch;

	dev_info(&client->dev, "%s: on: %d, %dx%d\n", __func__, on,
				nvp6158->frame_size->width,
				nvp6158->frame_size->height);

	mutex_lock(&nvp6158->mutex);
	on = !!on;

	if (nvp6158->streaming == on)
		goto unlock;

	if (on) {
		for (ch = 0; ch < 4; ch++) {
			fmt_idx = nvp6158->frame_size->fmt_idx;
			video_init.ch_param[ch].ch = ch;
			video_init.ch_param[ch].format = fmt_idx;
		}
		video_init.mode = nvp6158->mode;
		nvp6158_start(&video_init, nvp6158->dual_edge ? true : false);
#ifdef WORK_QUEUE
		if (nvp6158->plug_state_check.state_check_wq) {
			dev_info(&client->dev, "%s queue_delayed_work 1000ms", __func__);
			queue_delayed_work(nvp6158->plug_state_check.state_check_wq,
					   &nvp6158->plug_state_check.d_work,
					   msecs_to_jiffies(1000));
		}
#endif
	} else {
#ifdef WORK_QUEUE
		cancel_delayed_work_sync(&nvp6158->plug_state_check.d_work);
		dev_info(&client->dev, "cancle_queue_delayed_work");
#endif
		nvp6158_stop();
	}

	nvp6158->streaming = on;

unlock:
	mutex_unlock(&nvp6158->mutex);

	return 0;
}

static int nvp6158_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct nvp6158 *nvp6158 = to_nvp6158(sd);
	struct i2c_client *client = nvp6158->client;

	dev_dbg(&client->dev, "%s enter.\n", __func__);

	if (fie->index >= ARRAY_SIZE(nvp6158_framesizes))
		return -EINVAL;

	fie->width = nvp6158_framesizes[fie->index].width;
	fie->height = nvp6158_framesizes[fie->index].height;
	fie->interval = nvp6158_framesizes[fie->index].max_fps;
	return 0;
}

static int nvp6158_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(nvp6158_formats))
		return -EINVAL;

	code->code = nvp6158_formats[code->index].code;

	return 0;
}

static int nvp6158_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i = ARRAY_SIZE(nvp6158_formats);

	dev_dbg(&client->dev, "%s: enter!\n", __func__);

	if (fse->index >= ARRAY_SIZE(nvp6158_framesizes))
		return -EINVAL;

	while (--i)
		if (fse->code == nvp6158_formats[i].code)
			break;

	fse->code = nvp6158_formats[i].code;

	fse->min_width  = nvp6158_framesizes[fse->index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = nvp6158_framesizes[fse->index].height;
	fse->min_height = fse->max_height;

	return 0;
}

/* indicate N4 no signal channel */
static inline bool nvp6158_no_signal(struct v4l2_subdev *sd, u8 *novid)
{
	struct nvp6158 *nvp6158 = to_nvp6158(sd);
	struct i2c_client *client = nvp6158->client;
	u8 videoloss = 0;
	int ret;
	bool no_signal = false;

	nvp6158_write(client, 0xff, 0x00);
	ret = nvp6158_read(client, 0xa8, &videoloss);
	if (ret < 0)
		dev_err(&client->dev, "Failed to read videoloss state!\n");

	*novid = videoloss;
	dev_info(&client->dev, "%s: video loss status:0x%x.\n", __func__, videoloss);
	if (videoloss == 0xf) {
		dev_info(&client->dev, "%s: all channels No Video detected.\n", __func__);
		no_signal = true;
	} else {
		dev_info(&client->dev, "%s: channel has some video detection.\n", __func__);
		no_signal = false;
	}
	return no_signal;
}

/* indicate N4 channel locked status */
static inline bool nvp6158_sync(struct v4l2_subdev *sd, u8 *lock_st)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 video_lock_status = 0;
	int ret;
	bool has_sync = false;

	nvp6158_write(client, 0xff, 0x00);
	ret = nvp6158_read(client, 0xe0, &video_lock_status);
	if (ret < 0)
		dev_err(&client->dev, "Failed to read sync state!\n");

	dev_info(&client->dev, "%s: video AGC LOCK status:0x%x.\n",
			__func__, video_lock_status);
	*lock_st = video_lock_status;
	if (video_lock_status) {
		dev_info(&client->dev, "%s: channel has AGC LOCK.\n", __func__);
		has_sync = true;
	} else {
		dev_info(&client->dev, "%s: channel has no AGC LOCK.\n", __func__);
		has_sync = false;
	}
	return has_sync;
}

#ifdef WORK_QUEUE
static void nvp6158_plug_state_check_work(struct work_struct *work)
{
	struct sensor_state_check_work *params_check =
		container_of(work, struct sensor_state_check_work, d_work.work);
	struct nvp6158 *nvp6158 =
		container_of(params_check, struct nvp6158, plug_state_check);
	struct i2c_client *client = nvp6158->client;
	struct v4l2_subdev *sd = &nvp6158->subdev;
	u8 novid_status = 0x00;
	u8 sync_status = 0x00;

	nvp6158_no_signal(sd, &novid_status);
	nvp6158_sync(sd, &sync_status);
	nvp6158->cur_detect_status = novid_status;

	/* detect state change to determine is there has plug motion */
	novid_status = nvp6158->cur_detect_status ^ nvp6158->last_detect_status;
	if (novid_status)
		nvp6158->hot_plug = true;
	else
		nvp6158->hot_plug = false;
	nvp6158->last_detect_status = nvp6158->cur_detect_status;

	dev_info(&client->dev, "%s has plug motion? (%s)", __func__,
			 nvp6158->hot_plug ? "true" : "false");
	if (nvp6158->hot_plug) {
		dev_info(&client->dev, "queue_delayed_work 1500ms, if has hot plug motion.");
		queue_delayed_work(nvp6158->plug_state_check.state_check_wq,
				   &nvp6158->plug_state_check.d_work, msecs_to_jiffies(1500));
		nvp6158_write(client, 0xFF, 0x20);
		nvp6158_write(client, 0x00, (sync_status << 4) | sync_status);
		usleep_range(3000, 5000);
		nvp6158_write(client, 0x00, 0xFF);
	} else {
		dev_info(&client->dev, "queue_delayed_work 100ms, if no hot plug motion.");
		queue_delayed_work(nvp6158->plug_state_check.state_check_wq,
				   &nvp6158->plug_state_check.d_work, msecs_to_jiffies(100));
	}
}
#endif

static int nvp6158_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				 struct v4l2_mbus_config *cfg)
{
	struct nvp6158 *nvp6158 = to_nvp6158(sd);

	cfg->type = V4L2_MBUS_BT656;
	if (nvp6158->dual_edge == 1) {
		cfg->flags = RKMODULE_CAMERA_BT656_CHANNELS |
			V4L2_MBUS_PCLK_SAMPLE_RISING |
			V4L2_MBUS_PCLK_SAMPLE_FALLING;
	} else {
		cfg->flags = RKMODULE_CAMERA_BT656_CHANNELS |
			V4L2_MBUS_PCLK_SAMPLE_RISING;
	}
	return 0;
}

static int nvp6158_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct nvp6158 *nvp6158 = to_nvp6158(sd);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		struct v4l2_mbus_framefmt *mf;

		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
		mutex_lock(&nvp6158->mutex);
		fmt->format = *mf;
		mutex_unlock(&nvp6158->mutex);
		return 0;
#else
	return -ENOTTY;
#endif
	}

	mutex_lock(&nvp6158->mutex);
	fmt->format = nvp6158->format;
	mutex_unlock(&nvp6158->mutex);

	dev_dbg(&client->dev, "%s: %x %dx%d\n", __func__,
		nvp6158->format.code, nvp6158->format.width,
		nvp6158->format.height);

	return 0;
}

static void __nvp6158_try_frame_size(struct v4l2_mbus_framefmt *mf,
				    const struct nvp6158_framesize **size)
{
	const struct nvp6158_framesize *fsize = &nvp6158_framesizes[0];
	const struct nvp6158_framesize *match = NULL;
	int i = ARRAY_SIZE(nvp6158_framesizes);
	unsigned int min_err = UINT_MAX;

	while (i--) {
		unsigned int err = abs(fsize->width - mf->width)
				+ abs(fsize->height - mf->height);
		if (err < min_err) {
			min_err = err;
			match = fsize;
		}
		fsize++;
	}

	if (!match)
		match = &nvp6158_framesizes[0];

	mf->width  = match->width;
	mf->height = match->height;

	if (size)
		*size = match;
}

static int nvp6158_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	int index = ARRAY_SIZE(nvp6158_formats);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	const struct nvp6158_framesize *size = NULL;
	struct nvp6158 *nvp6158 = to_nvp6158(sd);
	int ret = 0;

	__nvp6158_try_frame_size(mf, &size);

	while (--index >= 0)
		if (nvp6158_formats[index].code == mf->code)
			break;

	if (index < 0)
		return -EINVAL;

	mf->colorspace = V4L2_COLORSPACE_SRGB;
	mf->code = nvp6158_formats[index].code;
	mf->field = nvp6158->format.field;

	mutex_lock(&nvp6158->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*mf = fmt->format;
#else
		return -ENOTTY;
#endif
	} else {
		if (nvp6158->streaming) {
			mutex_unlock(&nvp6158->mutex);
			return -EBUSY;
		}

		nvp6158->frame_size = size;
		nvp6158->format = fmt->format;
	}

	mutex_unlock(&nvp6158->mutex);
	return ret;
}

static int nvp6158_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct nvp6158 *nvp6158 = to_nvp6158(sd);
	const struct nvp6158_framesize *size = nvp6158->frame_size;

	fi->interval = size->max_fps;

	return 0;
}

static void nvp6158_get_module_inf(struct nvp6158 *nvp6158,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, NVP6158_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, nvp6158->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, nvp6158->len_name, sizeof(inf->base.lens));
}

static __maybe_unused void
nvp6158_get_bt656_module_inf(struct nvp6158 *nvp6158,
			       struct rkmodule_bt656_mbus_info *inf)
{
	memset(inf, 0, sizeof(*inf));
	inf->flags = RKMODULE_CAMERA_BT656_PARSE_ID_LSB;
	switch (nvp6158->ch_nums) {
	case 1:
		inf->flags |= RKMODULE_CAMERA_BT656_CHANNEL_0;
		break;
	case 2:
		inf->flags |= RKMODULE_CAMERA_BT656_CHANNEL_0 |
			      RKMODULE_CAMERA_BT656_CHANNEL_1;
		break;
	case 4:
		inf->flags |= RKMODULE_CAMERA_BT656_CHANNELS;
		break;
	default:
		inf->flags |= RKMODULE_CAMERA_BT656_CHANNELS;
	}
}

static void nvp6158_get_vicap_rst_inf(struct nvp6158 *nvp6158,
				   struct rkmodule_vicap_reset_info *rst_info)
{
	struct i2c_client *client = nvp6158->client;

	rst_info->is_reset = nvp6158->hot_plug;
	nvp6158->hot_plug = false;
	rst_info->src = RKCIF_RESET_SRC_ERR_HOTPLUG;
	dev_info(&client->dev, "%s: rst_info->is_reset:%d.\n", __func__, rst_info->is_reset);
}

static void nvp6158_set_vicap_rst_inf(struct nvp6158 *nvp6158,
				   struct rkmodule_vicap_reset_info rst_info)
{
	nvp6158->is_reset = rst_info.is_reset;
}

static void nvp6158_set_streaming(struct nvp6158 *nvp6158, int on)
{
	struct i2c_client *client = nvp6158->client;


	dev_info(&client->dev, "%s: on: %d\n", __func__, on);

	if (on) {
		//VDO2/VDO1 enabled VCLK_1_EN/VCLK_2_EN
		nvp6158_write(client, 0xFF, 0x01);
		nvp6158_write(client, 0xCA, 0x66);
	} else {
		//VDO2/VDO1 disable VCLK_1/VCLK_2_DISABLE
		nvp6158_write(client, 0xFF, 0x01);
		nvp6158_write(client, 0xCA, 0x00);
	}
}

static long nvp6158_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct nvp6158 *nvp6158 = to_nvp6158(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		nvp6158_get_module_inf(nvp6158, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_BT656_MBUS_INFO:
		nvp6158_get_bt656_module_inf(nvp6158,
					       (struct rkmodule_bt656_mbus_info
						*)arg);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		if ((nvp6158->mode > BT656_4MUX) &&
			(nvp6158->mode < NVP6158_DVP_MODES_END))
			*(int *)arg = RKMODULE_START_STREAM_FRONT;
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		nvp6158_get_vicap_rst_inf(nvp6158, (struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		nvp6158_set_vicap_rst_inf(nvp6158, *(struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		nvp6158_set_streaming(nvp6158, !!stream);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long nvp6158_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	struct rkmodule_bt656_mbus_info *bt565_inf;
	int *seq;
	struct rkmodule_vicap_reset_info *vicap_rst_inf;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = nvp6158_ioctl(sd, cmd, inf);
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

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = nvp6158_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_BT656_MBUS_INFO:
		bt565_inf = kzalloc(sizeof(*bt565_inf), GFP_KERNEL);
		if (!bt565_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = nvp6158_ioctl(sd, cmd, bt565_inf);
		if (!ret) {
			ret = copy_to_user(up, bt565_inf, sizeof(*bt565_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(bt565_inf);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		seq = kzalloc(sizeof(*seq), GFP_KERNEL);
		if (!seq) {
			ret = -ENOMEM;
			return ret;
		}
		ret = nvp6158_ioctl(sd, cmd, seq);
		if (!ret) {
			ret = copy_to_user(up, seq, sizeof(*seq));
			if (ret)
				ret = -EFAULT;
		}
		kfree(seq);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = nvp6158_ioctl(sd, cmd, vicap_rst_inf);
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
			ret = nvp6158_ioctl(sd, cmd, vicap_rst_inf);
		else
			ret = -EFAULT;
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = nvp6158_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int nvp6158_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct nvp6158 *nvp6158 = to_nvp6158(sd);

	return __nvp6158_power_on(nvp6158);
}

static int nvp6158_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct nvp6158 *nvp6158 = to_nvp6158(sd);

	__nvp6158_power_off(nvp6158);

	return 0;
}

static const struct dev_pm_ops nvp6158_pm_ops = {
	SET_RUNTIME_PM_OPS(nvp6158_runtime_suspend,
			   nvp6158_runtime_resume, NULL)
};

static const struct v4l2_subdev_video_ops nvp6158_video_ops = {
	.s_stream = nvp6158_stream,
	.querystd = nvp6158_querystd,
	.g_frame_interval = nvp6158_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops nvp6158_subdev_pad_ops = {
	.enum_mbus_code = nvp6158_enum_mbus_code,
	.enum_frame_size = nvp6158_enum_frame_sizes,
	.get_fmt = nvp6158_get_fmt,
	.set_fmt = nvp6158_set_fmt,
	.get_selection = nvp6158_get_selection,
	.enum_frame_interval = nvp6158_enum_frame_interval,
	.get_mbus_config = nvp6158_g_mbus_config,
};

static const struct v4l2_subdev_core_ops nvp6158_core_ops = {
	.s_power = nvp6158_power,
	.ioctl = nvp6158_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = nvp6158_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_ops nvp6158_subdev_ops = {
	.core = &nvp6158_core_ops,
	.video = &nvp6158_video_ops,
	.pad   = &nvp6158_subdev_pad_ops,
};

static void get_dvp_mode(struct nvp6158 *nvp6158)
{
	struct device *dev = &nvp6158->client->dev;
	char mode[128];
	u32 i;

	sprintf(mode, "%s_%dMUX", nvp6158->dvp_mode, nvp6158->ch_nums);
	dev_info(dev, "combined dvp mode is(%s)\n", mode);
	for (i = 0; i < NVP6158_DVP_MODES_END; i++) {
		if (!strcmp(mode, nvp6158_dvp_mode_lists[i]))
			break;
	}

	if (i < NVP6158_DVP_MODES_END)
		nvp6158->mode = i;
	else
		nvp6158->mode = BT656I_TEST_MODES;
	dev_info(dev, "get dvp mode (%s)\n", nvp6158_dvp_mode_lists[nvp6158->mode]);
}

static int nvp6158_parse_dts(struct nvp6158 *nvp6158)
{
	int ret;
	int elem_size, elem_index;
	const char *str = "";
	struct property *prop;
	struct nvp6158_regulator *regulator;
	struct device *dev = &nvp6158->client->dev;
	struct device_node *np = of_node_get(dev->of_node);

	nvp6158->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(nvp6158->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}
	ret = clk_set_rate(nvp6158->xvclk, NVP6158_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(nvp6158->xvclk) != NVP6158_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	nvp6158->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(nvp6158->pinctrl)) {
		nvp6158->pins_default =
			pinctrl_lookup_state(nvp6158->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(nvp6158->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		nvp6158->pins_sleep =
			pinctrl_lookup_state(nvp6158->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(nvp6158->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	elem_size = of_property_count_elems_of_size(
		np,
		OF_CAMERA_MODULE_REGULATOR_VOLTAGES,
		sizeof(u32));
	prop = of_find_property(
		np,
		OF_CAMERA_MODULE_REGULATORS,
		NULL);
	if (elem_size > 0 && !IS_ERR_OR_NULL(prop)) {
		nvp6158->regulators.regulator =
			devm_kzalloc(&nvp6158->client->dev,
				     elem_size * sizeof(struct nvp6158_regulator),
				     GFP_KERNEL);
		if (!nvp6158->regulators.regulator)
			dev_err(dev, "could not malloc nvp6158_regulator\n");

		nvp6158->regulators.cnt = elem_size;

		str = NULL;
		elem_index = 0;
		regulator = nvp6158->regulators.regulator;
		if (regulator) {
			do {
				str = of_prop_next_string(prop, str);
				if (!str) {
					dev_err(dev, "%s is not match %s in dts\n",
						OF_CAMERA_MODULE_REGULATORS,
						OF_CAMERA_MODULE_REGULATOR_VOLTAGES);
					break;
				}
				regulator->regulator =
					devm_regulator_get_optional(dev, str);
				if (IS_ERR(regulator->regulator))
					dev_err(dev, "devm_regulator_get %s failed\n",
						str);
				of_property_read_u32_index(
					np,
					OF_CAMERA_MODULE_REGULATOR_VOLTAGES,
					elem_index++,
					&regulator->min_uV);
				regulator->max_uV = regulator->min_uV;
				regulator++;
			} while (--elem_size);
		}
	}

	if (of_property_read_string(np,
		RK_CAMERA_MODULE_DVP_MODE,
		&nvp6158->dvp_mode)) {
		nvp6158->dvp_mode = NVP6158_DEFAULT_DVP_MODE;
		dev_warn(dev,
			"can not get module %s from dts, use default(%s)!\n",
			RK_CAMERA_MODULE_DVP_MODE,
			NVP6158_DEFAULT_DVP_MODE);
	} else {
		dev_info(dev,
			"get module %s from dts, dvp mode(%s)!\n",
			RK_CAMERA_MODULE_DVP_MODE, nvp6158->dvp_mode);
	}

	if (of_property_read_u32(np,
		RK_CAMERA_MODULE_CHANNEL_NUMS,
		&nvp6158->ch_nums)) {
		nvp6158->ch_nums = NVP6158_DEFAULT_CHANNEL_NUMS;
		dev_warn(dev,
			"can not get module %s from dts, use default(%d)!\n",
			RK_CAMERA_MODULE_CHANNEL_NUMS,
			NVP6158_DEFAULT_CHANNEL_NUMS);
	} else {
		dev_info(dev,
			"get module %s from dts, channel_nums(%d)!\n",
			RK_CAMERA_MODULE_DVP_MODE, nvp6158->ch_nums);
	}

	if (of_property_read_u32(np,
		RK_CAMERA_MODULE_DUAL_EDGE,
		&nvp6158->dual_edge)) {
		nvp6158->dual_edge = NVP6158_DEFAULT_DUAL_EDGE;
		dev_warn(dev,
			"can not get module %s from dts, use default(%d)!\n",
			RK_CAMERA_MODULE_DUAL_EDGE,
			NVP6158_DEFAULT_DUAL_EDGE);
	} else {
		dev_info(dev,
			"get module %s from dts, dual_edge(%d)!\n",
			RK_CAMERA_MODULE_DUAL_EDGE, nvp6158->dual_edge);
	}



	if (of_property_read_u32_array(np,
		RK_CAMERA_MODULE_DEFAULT_RECT,
		(unsigned int *)&nvp6158->defrect, 2)) {
		nvp6158->defrect.width = NVP6158_DEFAULT_WIDTH;
		nvp6158->defrect.height = NVP6158_DEFAULT_HEIGHT;
		dev_warn(dev,
			"can not get module %s from dts, use default wxh(%dx%d)!\n",
			RK_CAMERA_MODULE_DEFAULT_RECT,
			NVP6158_DEFAULT_WIDTH, NVP6158_DEFAULT_HEIGHT);
	} else {
		dev_info(dev,
			"get module %s from dts, wxh(%dx%d)!\n",
			RK_CAMERA_MODULE_DEFAULT_RECT,
			nvp6158->defrect.width,
			nvp6158->defrect.height);
	}

	/* AHD_PWR_EN */
	nvp6158->pwr_gpio = devm_gpiod_get(dev, "pwr", GPIOD_OUT_LOW);
	if (IS_ERR(nvp6158->pwr_gpio))
		dev_warn(dev, "can not find pd-gpios, error %ld\n",
			 PTR_ERR(nvp6158->pwr_gpio));
	/* AHD CAM_PWR_EN*/
	nvp6158->pwr2_gpio = devm_gpiod_get(dev, "pwr2", GPIOD_OUT_LOW);
	if (IS_ERR(nvp6158->pwr2_gpio))
		dev_warn(dev, "can not find pd2-gpios, error %ld\n",
			 PTR_ERR(nvp6158->pwr2_gpio));

	nvp6158->rst_gpio = devm_gpiod_get(dev, "rst", GPIOD_OUT_LOW);
	if (IS_ERR(nvp6158->rst_gpio))
		dev_warn(dev, "can not find rst-gpios, error %ld\n",
			 PTR_ERR(nvp6158->rst_gpio));

	nvp6158->rst2_gpio = devm_gpiod_get(dev, "rst2", GPIOD_OUT_LOW);
	if (IS_ERR(nvp6158->rst2_gpio))
		dev_warn(dev, "can not find rst2-gpios, error %ld\n",
			 PTR_ERR(nvp6158->rst2_gpio));

	nvp6158->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(nvp6158->pwdn_gpio))
		dev_warn(dev, "can not find pwd-gpios, error %ld\n",
			 PTR_ERR(nvp6158->pwdn_gpio));

	nvp6158->pwdn2_gpio = devm_gpiod_get(dev, "pwdn2", GPIOD_OUT_LOW);
	if (IS_ERR(nvp6158->pwdn2_gpio))
		dev_warn(dev, "can not find pwd2-gpios, error %ld\n",
			 PTR_ERR(nvp6158->pwdn2_gpio));

	return 0;
}


static int nvp6158_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct nvp6158 *nvp6158;
	struct v4l2_subdev *sd;
	__maybe_unused char facing[2];
	int ret, index;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	nvp6158 = devm_kzalloc(dev, sizeof(*nvp6158), GFP_KERNEL);
	if (!nvp6158)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &nvp6158->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &nvp6158->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &nvp6158->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &nvp6158->len_name);
	if (ret) {
		dev_err(dev, "could not get %s!\n", RKMODULE_CAMERA_LENS_NAME);
		return -EINVAL;
	}

	nvp6158->client = client;

	ret = nvp6158_parse_dts(nvp6158);
	if (ret) {
		dev_err(dev, "Failed to analyze dts\n");
		return ret;
	}
	get_dvp_mode(nvp6158);

	mutex_init(&nvp6158->mutex);
	nvp6158_get_default_format(nvp6158);

	sd = &nvp6158->subdev;
	v4l2_i2c_subdev_init(sd, client, &nvp6158_subdev_ops);
	ret = nvp6158_initialize_controls(nvp6158);
	if (ret)
		goto err_destroy_mutex;

	__nvp6158_power_on(nvp6158);
	ret = nvp6158_init(i2c_adapter_id(client->adapter));
	if (ret) {
		dev_err(dev, "Failed to init nvp6158\n");
		goto err_power_off;
	}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	for (index = 0; index < nvp6158->ch_nums; index++)
		nvp6158->pad[index].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, nvp6158->ch_nums, nvp6158->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(nvp6158->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 nvp6158->module_index, facing,
		 NVP6158_NAME, dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);
#ifdef WORK_QUEUE
	/* init work_queue for state_check */
	INIT_DELAYED_WORK(&nvp6158->plug_state_check.d_work, nvp6158_plug_state_check_work);
	nvp6158->plug_state_check.state_check_wq =
		create_singlethread_workqueue("nvp6158_work_queue");
	if (nvp6158->plug_state_check.state_check_wq == NULL) {
		dev_err(dev, "%s(%d): %s create failed.\n", __func__, __LINE__,
			  "nvp6158_work_queue");
	}
	nvp6158->cur_detect_status = 0x0;
	nvp6158->last_detect_status = 0x0;
	nvp6158->hot_plug = false;
	nvp6158->is_reset = 0;

#endif
	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__nvp6158_power_off(nvp6158);
err_destroy_mutex:
	mutex_destroy(&nvp6158->mutex);

	return ret;
}

static int nvp6158_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct nvp6158 *nvp6158 = to_nvp6158(sd);

	nvp6158_exit();
	v4l2_ctrl_handler_free(&nvp6158->ctrl_handler);
	mutex_destroy(&nvp6158->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__nvp6158_power_off(nvp6158);
	pm_runtime_set_suspended(&client->dev);
#ifdef WORK_QUEUE
	if (nvp6158->plug_state_check.state_check_wq != NULL)
		destroy_workqueue(nvp6158->plug_state_check.state_check_wq);
#endif
	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id nvp6158_of_match[] = {
	{ .compatible = "nvp6158-v4l2" },
	{},
};
MODULE_DEVICE_TABLE(of, nvp6158_of_match);
#endif

static const struct i2c_device_id nvp6158_match_id[] = {
	{ "nvp6158-v4l2", 0 },
	{ },
};

static struct i2c_driver nvp6158_i2c_driver = {
	.driver = {
		.name = NVP6158_NAME,
		.pm = &nvp6158_pm_ops,
		.of_match_table = of_match_ptr(nvp6158_of_match),
	},
	.probe		= &nvp6158_probe,
	.remove		= &nvp6158_remove,
	.id_table	= nvp6158_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&nvp6158_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&nvp6158_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("nvp6158 sensor driver");
MODULE_LICENSE("GPL v2");
