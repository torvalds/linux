// SPDX-License-Identifier: GPL-2.0
/*
 * gc2385 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 add quick stream on/off
 * V0.0X01.0X05 add function g_mbus_config
 * V0.0X01.0X06 set max framerate to strictly 30FPS for cts
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
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x05)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC2385_LANES			1
#define GC2385_BITS_PER_SAMPLE		10
#define GC2385_LINK_FREQ_MHZ	328000000
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define GC2385_PIXEL_RATE		(GC2385_LINK_FREQ_MHZ * 2 * 1 / 10)
#define GC2385_XVCLK_FREQ		24000000

#define CHIP_ID				0x2385
#define GC2385_REG_CHIP_ID_H		0xf0
#define GC2385_REG_CHIP_ID_L		0xf1

#define GC2385_REG_SET_PAGE		0xfe
#define GC2385_SET_PAGE_ONE		0x00

#define GC2385_REG_CTRL_MODE		0xed
#define GC2385_MODE_SW_STANDBY	0x00
#define GC2385_MODE_STREAMING		0x90

#define GC2385_REG_EXPOSURE_H		0x03
#define GC2385_REG_EXPOSURE_L		0x04
#define GC2385_FETCH_HIGH_BYTE_EXP(VAL) (((VAL) >> 8) & 0x3F)	/* 6 Bits */
#define GC2385_FETCH_LOW_BYTE_EXP(VAL) ((VAL) & 0xFF)	/* 8 Bits */
#define	GC2385_EXPOSURE_MIN		4
#define	GC2385_EXPOSURE_STEP		1
#define GC2385_VTS_MAX			0x1fff

#define GC2385_REG_AGAIN		0xb6
#define GC2385_REG_DGAIN_INT		0xb1
#define GC2385_REG_DGAIN_FRAC		0xb2
#define GC2385_GAIN_MIN		64
#define GC2385_GAIN_MAX		1092
#define GC2385_GAIN_STEP		1
#define GC2385_GAIN_DEFAULT		64

#define GC2385_REG_VTS_H			0x07
#define GC2385_REG_VTS_L			0x08

#define REG_NULL			0xFF

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define GC2385_NAME			"gc2385"

static const char * const gc2385_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define GC2385_NUM_SUPPLIES ARRAY_SIZE(gc2385_supply_names)

struct regval {
	u8 addr;
	u8 val;
};

struct gc2385_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct gc2385 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[GC2385_NUM_SUPPLIES];
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;
	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct gc2385_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_gc2385(sd) container_of(sd, struct gc2385, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval gc2385_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 656Mbps
 */
static const struct regval gc2385_1600x1200_regs[] = {
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xf2, 0x02},
	{0xf4, 0x03},
	{0xf7, 0x01},
	{0xf8, 0x28},
	{0xf9, 0x02},
	{0xfa, 0x08},
	{0xfc, 0x8e},
	{0xe7, 0xcc},
	{0x88, 0x03},
	{0x03, 0x04},
	{0x04, 0x80},
	{0x05, 0x02},
	{0x06, 0x86},
	{0x07, 0x00},
	{0x08, 0x10},
	{0x09, 0x00},
	{0x0a, 0x04},
	{0x0b, 0x00},
	{0x0c, 0x02},
	{0x17, 0xd4},
	{0x18, 0x02},
	{0x19, 0x17},
	{0x1c, 0x18},
	{0x20, 0x73},
	{0x21, 0x38},
	{0x22, 0xa2},
	{0x29, 0x20},
	{0x2f, 0x14},
	{0x3f, 0x40},
	{0xcd, 0x94},
	{0xce, 0x45},
	{0xd1, 0x0c},
	{0xd7, 0x9b},
	{0xd8, 0x99},
	{0xda, 0x3b},
	{0xd9, 0xb5},
	{0xdb, 0x75},
	{0xe3, 0x1b},
	{0xe4, 0xf8},
	{0x40, 0x22},
	{0x43, 0x07},
	{0x4e, 0x3c},
	{0x4f, 0x00},
	{0x68, 0x00},
	{0xb0, 0x46},
	{0xb1, 0x01},
	{0xb2, 0x00},
	{0xb6, 0x00},
	{0x90, 0x01},
	{0x92, 0x03},
	{0x94, 0x05},
	{0x95, 0x04},
	{0x96, 0xb0},
	{0x97, 0x06},
	{0x98, 0x40},
	{0xfe, 0x00},
	{0xed, 0x00},
	{0xfe, 0x03},
	{0x01, 0x03},
	{0x02, 0x82},
	{0x03, 0xd0},
	{0x04, 0x04},
	{0x05, 0x00},
	{0x06, 0x80},
	{0x11, 0x2b},
	{0x12, 0xd0},
	{0x13, 0x07},
	{0x15, 0x00},
	{0x1b, 0x10},
	{0x1c, 0x10},
	{0x21, 0x08},
	{0x22, 0x05},
	{0x23, 0x13},
	{0x24, 0x02},
	{0x25, 0x13},
	{0x26, 0x06},
	{0x29, 0x06},
	{0x2a, 0x08},
	{0x2b, 0x06},
	{0xfe, 0x00},
	{REG_NULL, 0x00},
};

static const struct gc2385_mode supported_modes[] = {
	{
		.width = 1600,
		.height = 1200,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0480,
		.hts_def = 0x10DC,
		.vts_def = 0x04F0,
		.reg_list = gc2385_1600x1200_regs,
	},
};

static const s64 link_freq_menu_items[] = {
	GC2385_LINK_FREQ_MHZ
};

/* Write registers up to 4 at a time */
static int gc2385_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

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
		"gc2385 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int gc2385_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i = 0;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc2385_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int gc2385_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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

	dev_err(&client->dev,
		"gc2385 read reg:0x%x failed !\n", reg);

	return ret;
}

static int gc2385_get_reso_dist(const struct gc2385_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct gc2385_mode *
gc2385_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = gc2385_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc2385_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc2385 *gc2385 = to_gc2385(sd);
	const struct gc2385_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc2385->mutex);

	mode = gc2385_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc2385->mutex);
		return -ENOTTY;
#endif
	} else {
		gc2385->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc2385->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc2385->vblank, vblank_def,
					 GC2385_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&gc2385->mutex);

	return 0;
}

static int gc2385_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct gc2385 *gc2385 = to_gc2385(sd);
	const struct gc2385_mode *mode = gc2385->cur_mode;

	mutex_lock(&gc2385->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc2385->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc2385->mutex);

	return 0;
}

static int gc2385_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int gc2385_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int gc2385_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct gc2385 *gc2385 = to_gc2385(sd);
	const struct gc2385_mode *mode = gc2385->cur_mode;

	mutex_lock(&gc2385->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc2385->mutex);

	return 0;
}

static void gc2385_get_module_inf(struct gc2385 *gc2385,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, GC2385_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc2385->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, gc2385->len_name, sizeof(inf->base.lens));
}

static long gc2385_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc2385 *gc2385 = to_gc2385(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc2385_get_module_inf(gc2385, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream) {
			ret = gc2385_write_reg(gc2385->client,
				 GC2385_REG_SET_PAGE,
				 GC2385_SET_PAGE_ONE);
			ret |= gc2385_write_reg(gc2385->client,
				 GC2385_REG_CTRL_MODE,
				 GC2385_MODE_STREAMING);
		} else {
			ret = gc2385_write_reg(gc2385->client,
				 GC2385_REG_SET_PAGE, GC2385_SET_PAGE_ONE);
			ret |= gc2385_write_reg(gc2385->client,
				 GC2385_REG_CTRL_MODE, GC2385_MODE_SW_STANDBY);
		}
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc2385_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc2385_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
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
			ret = gc2385_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc2385_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __gc2385_start_stream(struct gc2385 *gc2385)
{
	int ret;

	ret = gc2385_write_array(gc2385->client, gc2385->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&gc2385->mutex);
	ret = v4l2_ctrl_handler_setup(&gc2385->ctrl_handler);
	mutex_lock(&gc2385->mutex);
	if (ret)
		return ret;
	ret = gc2385_write_reg(gc2385->client,
				 GC2385_REG_SET_PAGE,
				 GC2385_SET_PAGE_ONE);
	ret |= gc2385_write_reg(gc2385->client,
				 GC2385_REG_CTRL_MODE,
				 GC2385_MODE_STREAMING);
	return ret;
}

static int __gc2385_stop_stream(struct gc2385 *gc2385)
{
	int ret;

	ret = gc2385_write_reg(gc2385->client,
		GC2385_REG_SET_PAGE, GC2385_SET_PAGE_ONE);
	ret |= gc2385_write_reg(gc2385->client,
		GC2385_REG_CTRL_MODE, GC2385_MODE_SW_STANDBY);
	return ret;
}

static int gc2385_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc2385 *gc2385 = to_gc2385(sd);
	struct i2c_client *client = gc2385->client;
	int ret = 0;

	mutex_lock(&gc2385->mutex);
	on = !!on;
	if (on == gc2385->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc2385_start_stream(gc2385);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc2385_stop_stream(gc2385);
		pm_runtime_put(&client->dev);
	}

	gc2385->streaming = on;

unlock_and_return:
	mutex_unlock(&gc2385->mutex);

	return ret;
}

static int gc2385_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc2385 *gc2385 = to_gc2385(sd);
	struct i2c_client *client = gc2385->client;
	int ret = 0;

	mutex_lock(&gc2385->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc2385->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc2385_write_array(gc2385->client, gc2385_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc2385->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc2385->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc2385->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc2385_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC2385_XVCLK_FREQ / 1000 / 1000);
}

static int __gc2385_power_on(struct gc2385 *gc2385)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc2385->client->dev;

	if (!IS_ERR_OR_NULL(gc2385->pins_default)) {
		ret = pinctrl_select_state(gc2385->pinctrl,
					   gc2385->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc2385->xvclk, GC2385_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc2385->xvclk) != GC2385_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc2385->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(gc2385->reset_gpio))
		gpiod_set_value_cansleep(gc2385->reset_gpio, 1);

	ret = regulator_bulk_enable(GC2385_NUM_SUPPLIES, gc2385->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	usleep_range(1000, 1100);
	if (!IS_ERR(gc2385->reset_gpio))
		gpiod_set_value_cansleep(gc2385->reset_gpio, 0);

	usleep_range(500, 1000);
	if (!IS_ERR(gc2385->pwdn_gpio))
		gpiod_set_value_cansleep(gc2385->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc2385_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc2385->xvclk);

	return ret;
}

static void __gc2385_power_off(struct gc2385 *gc2385)
{
	int ret = 0;

	if (!IS_ERR(gc2385->pwdn_gpio))
		gpiod_set_value_cansleep(gc2385->pwdn_gpio, 0);
	clk_disable_unprepare(gc2385->xvclk);
	if (!IS_ERR(gc2385->reset_gpio))
		gpiod_set_value_cansleep(gc2385->reset_gpio, 1);
	if (!IS_ERR_OR_NULL(gc2385->pins_sleep)) {
		ret = pinctrl_select_state(gc2385->pinctrl,
					   gc2385->pins_sleep);
		if (ret < 0)
			dev_dbg(&gc2385->client->dev, "could not set pins\n");
	}
	regulator_bulk_disable(GC2385_NUM_SUPPLIES, gc2385->supplies);
}

static int gc2385_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2385 *gc2385 = to_gc2385(sd);

	return __gc2385_power_on(gc2385);
}

static int gc2385_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2385 *gc2385 = to_gc2385(sd);

	__gc2385_power_off(gc2385);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc2385_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc2385 *gc2385 = to_gc2385(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc2385_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc2385->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc2385->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int gc2385_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int gc2385_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (GC2385_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops gc2385_pm_ops = {
	SET_RUNTIME_PM_OPS(gc2385_runtime_suspend,
			   gc2385_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc2385_internal_ops = {
	.open = gc2385_open,
};
#endif

static const struct v4l2_subdev_core_ops gc2385_core_ops = {
	.s_power = gc2385_s_power,
	.ioctl = gc2385_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc2385_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc2385_video_ops = {
	.s_stream = gc2385_s_stream,
	.g_frame_interval = gc2385_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc2385_pad_ops = {
	.enum_mbus_code = gc2385_enum_mbus_code,
	.enum_frame_size = gc2385_enum_frame_sizes,
	.enum_frame_interval = gc2385_enum_frame_interval,
	.get_fmt = gc2385_get_fmt,
	.set_fmt = gc2385_set_fmt,
	.get_mbus_config = gc2385_g_mbus_config,
};

static const struct v4l2_subdev_ops gc2385_subdev_ops = {
	.core	= &gc2385_core_ops,
	.video	= &gc2385_video_ops,
	.pad	= &gc2385_pad_ops,
};

#define GC2385_ANALOG_GAIN_1 64    /*1.00x*/
#define GC2385_ANALOG_GAIN_2 92   // 1.43x
#define GC2385_ANALOG_GAIN_3 127  // 1.99x
#define GC2385_ANALOG_GAIN_4 183  // 2.86x
#define GC2385_ANALOG_GAIN_5 257  // 4.01x
#define GC2385_ANALOG_GAIN_6 369  // 5.76x
#define GC2385_ANALOG_GAIN_7 531  //8.30x
#define GC2385_ANALOG_GAIN_8 750  // 11.72x
#define GC2385_ANALOG_GAIN_9 1092 // 17.06x

static int gc2385_set_gain_reg(struct gc2385 *gc2385, u32 a_gain)
{
	int ret = 0;
	u32 temp = 0;

	ret = gc2385_write_reg(gc2385->client,
		GC2385_REG_SET_PAGE, GC2385_SET_PAGE_ONE);
	if (a_gain >= GC2385_ANALOG_GAIN_1 &&
		a_gain < GC2385_ANALOG_GAIN_2) {
		ret |= gc2385_write_reg(gc2385->client, 0x20, 0x73);
		ret |= gc2385_write_reg(gc2385->client, 0x22, 0xa2);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_AGAIN, 0x0);
		temp = 256 * a_gain / GC2385_ANALOG_GAIN_1;
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_INT, (temp >> 8) & 0xff);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_FRAC, temp & 0xff);
	} else if (a_gain >= GC2385_ANALOG_GAIN_2 &&
		a_gain < GC2385_ANALOG_GAIN_3) {
		ret |= gc2385_write_reg(gc2385->client, 0x20, 0x73);
		ret |= gc2385_write_reg(gc2385->client, 0x22, 0xa2);
		ret |= gc2385_write_reg(gc2385->client,
				GC2385_REG_AGAIN, 0x1);
		temp = 256 * a_gain / GC2385_ANALOG_GAIN_2;
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_INT, (temp >> 8) & 0xff);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_FRAC, temp & 0xff);
	} else if (a_gain >= GC2385_ANALOG_GAIN_3 &&
		a_gain < GC2385_ANALOG_GAIN_4) {
		ret |= gc2385_write_reg(gc2385->client, 0x20, 0x73);
		ret |= gc2385_write_reg(gc2385->client, 0x22, 0xa2);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_AGAIN, 0x2);
		temp = 256 * a_gain / GC2385_ANALOG_GAIN_3;
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_INT, (temp >> 8) & 0xff);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_FRAC, temp & 0xff);
	} else if (a_gain >= GC2385_ANALOG_GAIN_4 &&
		a_gain < GC2385_ANALOG_GAIN_5) {
		ret |= gc2385_write_reg(gc2385->client, 0x20, 0x73);
		ret |= gc2385_write_reg(gc2385->client, 0x22, 0xa2);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_AGAIN, 0x3);
		temp = 256 * a_gain / GC2385_ANALOG_GAIN_4;
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_INT, (temp >> 8) & 0xff);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_FRAC, temp & 0xff);
	} else if (a_gain >= GC2385_ANALOG_GAIN_5 &&
		a_gain < GC2385_ANALOG_GAIN_6) {
		ret |= gc2385_write_reg(gc2385->client, 0x20, 0x73);
		ret |= gc2385_write_reg(gc2385->client, 0x22, 0xa3);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_AGAIN, 0x4);
		temp = 256 * a_gain / GC2385_ANALOG_GAIN_5;
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_INT, (temp >> 8) & 0xff);
		ret |= gc2385_write_reg(gc2385->client,
		GC2385_REG_DGAIN_FRAC, temp & 0xff);
	} else if (a_gain >= GC2385_ANALOG_GAIN_6 &&
		a_gain < GC2385_ANALOG_GAIN_7) {
		ret |= gc2385_write_reg(gc2385->client, 0x20, 0x73);
		ret |= gc2385_write_reg(gc2385->client, 0x22, 0xa3);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_AGAIN, 0x5);
		temp = 256 * a_gain / GC2385_ANALOG_GAIN_6;
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_INT, (temp >> 8) & 0xff);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_FRAC, temp & 0xff);
	} else if (a_gain >= GC2385_ANALOG_GAIN_7 &&
		a_gain < GC2385_ANALOG_GAIN_8) {
		ret |= gc2385_write_reg(gc2385->client, 0x20, 0x74);
		ret |= gc2385_write_reg(gc2385->client, 0x22, 0xa3);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_AGAIN, 0x6);
		temp = 256 * a_gain / GC2385_ANALOG_GAIN_7;
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_INT, (temp >> 8) & 0xff);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_FRAC, temp & 0xff);
	} else if (a_gain >= GC2385_ANALOG_GAIN_8 &&
		a_gain < GC2385_ANALOG_GAIN_9) {
		ret |= gc2385_write_reg(gc2385->client, 0x20, 0x74);
		ret |= gc2385_write_reg(gc2385->client, 0x22, 0xa3);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_AGAIN, 0x7);
		temp = 256 * a_gain / GC2385_ANALOG_GAIN_8;
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_INT, (temp >> 8) & 0xff);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_FRAC, temp & 0xff);
	} else {
		ret |= gc2385_write_reg(gc2385->client, 0x20, 0x75);
		ret |= gc2385_write_reg(gc2385->client, 0x22, 0xa4);
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_AGAIN, 0x8);
		temp = 256 * a_gain / GC2385_ANALOG_GAIN_9;
		ret |= gc2385_write_reg(gc2385->client,
			GC2385_REG_DGAIN_INT, (temp >> 8) & 0xff);
			ret |= gc2385_write_reg(gc2385->client,
				GC2385_REG_DGAIN_FRAC, temp & 0xff);
	}
	return ret;
}

static int gc2385_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc2385 *gc2385 = container_of(ctrl->handler,
					     struct gc2385, ctrl_handler);
	struct i2c_client *client = gc2385->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = gc2385->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc2385->exposure,
					 gc2385->exposure->minimum, max,
					 gc2385->exposure->step,
					 gc2385->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = gc2385_write_reg(gc2385->client,
					GC2385_REG_SET_PAGE,
					GC2385_SET_PAGE_ONE);
		ret |= gc2385_write_reg(gc2385->client,
					GC2385_REG_EXPOSURE_H,
					GC2385_FETCH_HIGH_BYTE_EXP(ctrl->val));
		ret |= gc2385_write_reg(gc2385->client,
					GC2385_REG_EXPOSURE_L,
					GC2385_FETCH_LOW_BYTE_EXP(ctrl->val));
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = gc2385_set_gain_reg(gc2385, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = gc2385_write_reg(gc2385->client,
					GC2385_REG_SET_PAGE,
					GC2385_SET_PAGE_ONE);
		ret |= gc2385_write_reg(gc2385->client,
					GC2385_REG_VTS_H,
					((ctrl->val - 32) >> 8) & 0xff);
		ret |= gc2385_write_reg(gc2385->client,
					GC2385_REG_VTS_L,
					(ctrl->val - 32) & 0xff);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc2385_ctrl_ops = {
	.s_ctrl = gc2385_set_ctrl,
};

static int gc2385_initialize_controls(struct gc2385 *gc2385)
{
	const struct gc2385_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc2385->ctrl_handler;
	mode = gc2385->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &gc2385->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, GC2385_PIXEL_RATE, 1, GC2385_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	gc2385->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (gc2385->hblank)
		gc2385->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc2385->vblank = v4l2_ctrl_new_std(handler, &gc2385_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				GC2385_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc2385->exposure = v4l2_ctrl_new_std(handler, &gc2385_ctrl_ops,
				V4L2_CID_EXPOSURE, GC2385_EXPOSURE_MIN,
				exposure_max, GC2385_EXPOSURE_STEP,
				mode->exp_def);

	gc2385->anal_gain = v4l2_ctrl_new_std(handler, &gc2385_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, GC2385_GAIN_MIN,
				GC2385_GAIN_MAX, GC2385_GAIN_STEP,
				GC2385_GAIN_DEFAULT);
	if (handler->error) {
		ret = handler->error;
		dev_err(&gc2385->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc2385->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc2385_check_sensor_id(struct gc2385 *gc2385,
				   struct i2c_client *client)
{
	struct device *dev = &gc2385->client->dev;
	u16 id = 0;
	u8 reg_H = 0;
	u8 reg_L = 0;
	int ret;

	ret = gc2385_write_reg(gc2385->client,
					GC2385_REG_SET_PAGE,
					GC2385_SET_PAGE_ONE);
	ret |= gc2385_read_reg(client, GC2385_REG_CHIP_ID_H, &reg_H);
	ret |= gc2385_read_reg(client, GC2385_REG_CHIP_ID_L, &reg_L);
	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	return ret;
}

static int gc2385_configure_regulators(struct gc2385 *gc2385)
{
	unsigned int i;

	for (i = 0; i < GC2385_NUM_SUPPLIES; i++)
		gc2385->supplies[i].supply = gc2385_supply_names[i];

	return devm_regulator_bulk_get(&gc2385->client->dev,
				       GC2385_NUM_SUPPLIES,
				       gc2385->supplies);
}

static int gc2385_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc2385 *gc2385;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc2385 = devm_kzalloc(dev, sizeof(*gc2385), GFP_KERNEL);
	if (!gc2385)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc2385->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc2385->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc2385->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc2385->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	gc2385->client = client;
	gc2385->cur_mode = &supported_modes[0];

	gc2385->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc2385->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc2385->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc2385->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc2385->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc2385->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = gc2385_configure_regulators(gc2385);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	gc2385->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc2385->pinctrl)) {
		gc2385->pins_default =
			pinctrl_lookup_state(gc2385->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc2385->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc2385->pins_sleep =
			pinctrl_lookup_state(gc2385->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc2385->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&gc2385->mutex);

	sd = &gc2385->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc2385_subdev_ops);
	ret = gc2385_initialize_controls(gc2385);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc2385_power_on(gc2385);
	if (ret)
		goto err_free_handler;

	ret = gc2385_check_sensor_id(gc2385, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc2385_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc2385->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc2385->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc2385->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc2385->module_index, facing,
		 GC2385_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
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
err_power_off:
	__gc2385_power_off(gc2385);
err_free_handler:
	v4l2_ctrl_handler_free(&gc2385->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc2385->mutex);

	return ret;
}

static int gc2385_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2385 *gc2385 = to_gc2385(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc2385->ctrl_handler);
	mutex_destroy(&gc2385->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc2385_power_off(gc2385);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc2385_of_match[] = {
	{ .compatible = "galaxycore,gc2385" },
	{},
};
MODULE_DEVICE_TABLE(of, gc2385_of_match);
#endif

static const struct i2c_device_id gc2385_match_id[] = {
	{ "galaxycore,gc2385", 0 },
	{ },
};

static struct i2c_driver gc2385_i2c_driver = {
	.driver = {
		.name = GC2385_NAME,
		.pm = &gc2385_pm_ops,
		.of_match_table = of_match_ptr(gc2385_of_match),
	},
	.probe		= &gc2385_probe,
	.remove		= &gc2385_remove,
	.id_table	= gc2385_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc2385_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc2385_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GalaxyCore gc2385 sensor driver");
MODULE_LICENSE("GPL v2");
