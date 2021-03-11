// SPDX-License-Identifier: GPL-2.0
/*
 * gc5024 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 init driver.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * TODO: add OTP function.
 * V0.0X01.0X04 add quick stream on/off
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_gpio.h>

#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

//#define IMAGE_NORMAL
#define IMAGE_H_MIRROR
//#define IMAGE_V_MIRROR
//#define IMAGE_HV_MIRROR

#ifdef IMAGE_NORMAL
#define MIRROR		0xd4
#define PH_SWITCH	0x1b
#define STARTX		0x0d
#define STARTY		0x03
#endif
#ifdef IMAGE_H_MIRROR
#define MIRROR		0xd5
#define PH_SWITCH	0x1a
#define STARTX		0x02
#define STARTY		0x03
#endif
#ifdef IMAGE_V_MIRROR
#define MIRROR		0xd6
#define PH_SWITCH	0x1b
#define STARTX		0x0d
#define STARTY		0x02
#endif
#ifdef IMAGE_HV_MIRROR
#define MIRROR		0xd7
#define PH_SWITCH	0x1a
#define STARTX		0x02
#define STARTY		0x02
#endif

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC5024_LANES			2
#define GC5024_BITS_PER_SAMPLE		10
#define MIPI_FREQ		420000000LL
/* pixel rate = link frequency * 1 * lanes / BITS_PER_SAMPLE */
#define GC5024_PIXEL_RATE		(MIPI_FREQ * 2LL * 2LL / 10)
#define GC5024_XVCLK_FREQ		24000000

#define CHIP_ID				0x5024
#define GC5024_REG_CHIP_ID_H		0xf0
#define GC5024_REG_CHIP_ID_L		0xf1
#define SENSOR_ID(_msb, _lsb)		((_msb) << 8 | (_lsb))

#define GC5024_PAGE_SELECT		0xfe
#define GC5024_MODE_SELECT		0x10
#define GC5024_MODE_SW_STANDBY		0x00
#define GC5024_MODE_STREAMING		0x91

#define GC5024_REG_EXPOSURE_H		0x03
#define GC5024_REG_EXPOSURE_L		0x04
#define	GC5024_EXPOSURE_MIN		4
#define	GC5024_EXPOSURE_STEP		1
#define GC5024_VTS_MAX			0x7fff

#define GC5024_ANALOG_GAIN_1 64    /*1.00x*/
#define GC5024_ANALOG_GAIN_2 88    /*1.375x*/
#define GC5024_ANALOG_GAIN_3 122   /*1.90x*/
#define GC5024_ANALOG_GAIN_4 168   /*2.625x*/
#define GC5024_ANALOG_GAIN_5 239   /*3.738x*/
#define GC5024_ANALOG_GAIN_6 330   /*5.163x*/
#define GC5024_ANALOG_GAIN_7 470   /*7.350x*/

#define GC5024_ANALOG_GAIN_REG		0xb6
#define GC5024_PREGAIN_H_REG		0xb1
#define GC5024_PREGAIN_L_REG		0xb2

#define GC5024_GAIN_MIN			0x40
#define GC5024_GAIN_MAX			0x200
#define GC5024_GAIN_STEP		1
#define GC5024_GAIN_DEFAULT		0x80

#define GC5024_REG_VTS_H			0x07
#define GC5024_REG_VTS_L			0x08

#define REG_NULL			0xFFFF

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define GC5024_NAME			"gc5024"
#define GC5024_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SBGGR10_1X10

static const char * const gc5024_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define GC5024_NUM_SUPPLIES ARRAY_SIZE(gc5024_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct gc5024_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct gc5024 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[GC5024_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct gc5024_mode *cur_mode;
	unsigned int		lane_num;
	unsigned int		cfg_num;
	unsigned int		pixel_rate;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_gc5024(sd) container_of(sd, struct gc5024, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval gc5024_global_regs[] = {
	/*SYS*/
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xf7, 0x01},
	{0xf8, 0x0e},
	{0xf9, 0xae},
	{0xfa, 0x84},
	{0xfc, 0xae},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0x88, 0x03},
	{0xe7, 0xc0},
	/*Analog*/
	{0xfe, 0x00},
	{0x03, 0x08},
	{0x04, 0xca},
	{0x05, 0x01},
	{0x06, 0xf4},
	{0x07, 0x00},
	{0x08, 0x08},
	{0x0a, 0x00},
	{0x0c, 0x00},
	{0x0d, 0x07},
	{0x0e, 0xa8},
	{0x0f, 0x0a},
	{0x10, 0x40},
	{0x11, 0x31},
	{0x12, 0x28},
	{0x13, 0x10},
	{0x17, MIRROR},
	{0x18, 0x02},
	{0x19, 0x0d},
	{0x1a, PH_SWITCH},
	{0x1b, 0x41},
	{0x1c, 0x2b},
	{0x21, 0x0f},
	{0x24, 0xb0},
	{0x29, 0x38},
	{0x2d, 0x16},
	{0x2f, 0x16},
	{0x32, 0x49},
	{0xcd, 0xaa},
	{0xd0, 0xc2},
	{0xd1, 0xc4},
	{0xd2, 0xcb},
	{0xd3, 0x73},
	{0xd8, 0x18},
	{0xdc, 0xba},
	{0xe2, 0x20},
	{0xe4, 0x78},
	{0xe6, 0x08},
	/*ISP*/
	{0x80, 0x50},//50
	{0x8d, 0x07},
	{0x90, 0x01},
	{0x92, STARTY},
	{0x94, STARTX},
	{0x95, 0x07},
	{0x96, 0x98},
	{0x97, 0x0a},
	{0x98, 0x20},
	/*Gain */
	{0x99, 0x01},
	{0x9a, 0x02},
	{0x9b, 0x03},
	{0x9c, 0x04},
	{0x9d, 0x0d},
	{0x9e, 0x15},
	{0x9f, 0x1d},
	{0xb0, 0x4b},
	{0xb1, 0x01},
	{0xb2, 0x00},
	{0xb6, 0x00},
	/*Blk*/
	{0x40, 0x22},
	{0x4e, 0x3c},
	{0x4f, 0x00},
	{0x60, 0x00},
	{0x61, 0x80},
	{0xfe, 0x02},
	{0xa4, 0x30},
	{0xa5, 0x00},
	/*Dark Sun*/
	{0x40, 0x00},//96 20160527
	{0x42, 0x0f},
	{0x45, 0xca},
	{0x47, 0xff},
	{0x48, 0xc8},
	/*DD*/
	{0x80, 0x98},
	{0x81, 0x50},
	{0x82, 0x60},
	{0x84, 0x20},
	{0x85, 0x10},
	{0x86, 0x04},
	{0x87, 0x20},
	{0x88, 0x10},
	{0x89, 0x04},
	/*Degrid*/
	{0x8a, 0x0a},
	/*MIPI*/
	{0xfe, 0x03},
	{0x01, 0x07},
	{0x02, 0x34}, //0x34
	{0x03, 0x13}, //0x13
	{0x04, 0x04},
	{0x05, 0x00},
	{0x06, 0x80},
	{0x11, 0x2b},
	{0x12, 0xa8},
	{0x13, 0x0c},
	{0x15, 0x00},
	{0x16, 0x09},
	{0x18, 0x01},
	{0x21, 0x10},
	{0x22, 0x05},
	{0x23, 0x30},
	{0x24, 0x10},
	{0x25, 0x14},
	{0x26, 0x08},
	{0x29, 0x05},
	{0x2a, 0x0a},
	{0x2b, 0x08},
	{0x42, 0x20},
	{0x43, 0x0a},
	{0xfe, 0x00},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 1008Mbps
 */
static const struct regval gc5024_2592x1944_regs[] = {
	{REG_NULL, 0x00},
};

static const struct gc5024_mode supported_modes_2lane[] = {
	{
		.width = 2592,
		.height = 1944,
		.max_fps = {
			.numerator = 10000,
			.denominator = 200000,
		},
		.exp_def = 0x07C0,
		.hts_def = 0x12C0,
		.vts_def = 0x07D0,
		.reg_list = gc5024_2592x1944_regs,
	},
};

static const struct gc5024_mode *supported_modes;

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ
};

/* sensor register write */
static int gc5024_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	dev_dbg(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);
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
		"gc5024 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

/* sensor register read */
static int gc5024_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"gc5024 read reg:0x%x failed !\n", reg);

	return ret;
}

static int gc5024_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc5024_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

static int gc5024_get_reso_dist(const struct gc5024_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct gc5024_mode *
gc5024_find_best_fit(struct gc5024 *gc5024,
			struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < gc5024->cfg_num; i++) {
		dist = gc5024_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc5024_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc5024 *gc5024 = to_gc5024(sd);
	const struct gc5024_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc5024->mutex);

	mode = gc5024_find_best_fit(gc5024, fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc5024->mutex);
		return -ENOTTY;
#endif
	} else {
		gc5024->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc5024->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc5024->vblank, vblank_def,
					 GC5024_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&gc5024->mutex);

	return 0;
}

static int gc5024_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc5024 *gc5024 = to_gc5024(sd);
	const struct gc5024_mode *mode = gc5024->cur_mode;

	mutex_lock(&gc5024->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc5024->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc5024->mutex);

	return 0;
}

static int gc5024_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int gc5024_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc5024 *gc5024 = to_gc5024(sd);

	if (fse->index >= gc5024->cfg_num)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int gc5024_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc5024 *gc5024 = to_gc5024(sd);
	const struct gc5024_mode *mode = gc5024->cur_mode;

	mutex_lock(&gc5024->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc5024->mutex);

	return 0;
}

static void gc5024_get_module_inf(struct gc5024 *gc5024,
				  struct rkmodule_inf *inf)
{
	strlcpy(inf->base.sensor,
		GC5024_NAME,
		sizeof(inf->base.sensor));
	strlcpy(inf->base.module,
		gc5024->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens,
		gc5024->len_name,
		sizeof(inf->base.lens));
}

static long gc5024_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc5024 *gc5024 = to_gc5024(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc5024_get_module_inf(gc5024, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream) {
			ret = gc5024_write_reg(gc5024->client, GC5024_PAGE_SELECT, 0x03);
			ret |= gc5024_write_reg(gc5024->client, GC5024_MODE_SELECT,
						GC5024_MODE_STREAMING);
			ret = gc5024_write_reg(gc5024->client, GC5024_PAGE_SELECT, 0x00);
		} else {
			ret = gc5024_write_reg(gc5024->client, GC5024_PAGE_SELECT, 0x03);
			ret |= gc5024_write_reg(gc5024->client, GC5024_MODE_SELECT,
						GC5024_MODE_SW_STANDBY);
			ret |= gc5024_write_reg(gc5024->client, GC5024_PAGE_SELECT, 0x00);
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc5024_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = gc5024_ioctl(sd, cmd, inf);
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
			ret = gc5024_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc5024_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __gc5024_start_stream(struct gc5024 *gc5024)
{
	int ret;

	ret = gc5024_write_array(gc5024->client, gc5024->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&gc5024->mutex);
	ret = v4l2_ctrl_handler_setup(&gc5024->ctrl_handler);
	mutex_lock(&gc5024->mutex);
	if (ret)
		return ret;

	ret = gc5024_write_reg(gc5024->client, GC5024_PAGE_SELECT, 0x03);
	ret |= gc5024_write_reg(gc5024->client, GC5024_MODE_SELECT,
				 GC5024_MODE_STREAMING);
	ret = gc5024_write_reg(gc5024->client, GC5024_PAGE_SELECT, 0x00);
	return ret;
}

static int __gc5024_stop_stream(struct gc5024 *gc5024)
{
	int ret;

	ret = gc5024_write_reg(gc5024->client, GC5024_PAGE_SELECT, 0x03);
	ret |= gc5024_write_reg(gc5024->client, GC5024_MODE_SELECT,
				 GC5024_MODE_SW_STANDBY);
	ret |= gc5024_write_reg(gc5024->client, GC5024_PAGE_SELECT, 0x00);
	return ret;
}

static int gc5024_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc5024 *gc5024 = to_gc5024(sd);
	struct i2c_client *client = gc5024->client;
	int ret = 0;

	mutex_lock(&gc5024->mutex);
	on = !!on;
	if (on == gc5024->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc5024_start_stream(gc5024);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc5024_stop_stream(gc5024);
		pm_runtime_put(&client->dev);
	}

	gc5024->streaming = on;

unlock_and_return:
	mutex_unlock(&gc5024->mutex);

	return ret;
}

static int gc5024_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc5024 *gc5024 = to_gc5024(sd);
	struct i2c_client *client = gc5024->client;
	int ret = 0;

	mutex_lock(&gc5024->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc5024->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc5024_write_array(gc5024->client, gc5024_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc5024->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc5024->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc5024->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc5024_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC5024_XVCLK_FREQ / 1000 / 1000);
}

static int __gc5024_power_on(struct gc5024 *gc5024)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc5024->client->dev;

	if (!IS_ERR_OR_NULL(gc5024->pins_default)) {
		ret = pinctrl_select_state(gc5024->pinctrl,
					   gc5024->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc5024->xvclk, GC5024_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc5024->xvclk) != GC5024_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc5024->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(gc5024->pwdn_gpio))
		gpiod_set_value_cansleep(gc5024->pwdn_gpio, 0);

	if (!IS_ERR(gc5024->reset_gpio))
		gpiod_set_value_cansleep(gc5024->reset_gpio, 0);

	usleep_range(500, 1000);

	ret = regulator_bulk_enable(GC5024_NUM_SUPPLIES, gc5024->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(gc5024->reset_gpio))
		gpiod_set_value_cansleep(gc5024->reset_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc5024_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc5024->xvclk);

	return ret;
}

static void __gc5024_power_off(struct gc5024 *gc5024)
{
	int ret;
	struct device *dev = &gc5024->client->dev;

	if (!IS_ERR(gc5024->pwdn_gpio))
		gpiod_set_value_cansleep(gc5024->pwdn_gpio, 1);
	clk_disable_unprepare(gc5024->xvclk);
	if (!IS_ERR(gc5024->reset_gpio))
		gpiod_set_value_cansleep(gc5024->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(gc5024->pins_sleep)) {
		ret = pinctrl_select_state(gc5024->pinctrl,
					   gc5024->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(GC5024_NUM_SUPPLIES, gc5024->supplies);
}

static int gc5024_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5024 *gc5024 = to_gc5024(sd);

	return __gc5024_power_on(gc5024);
}

static int gc5024_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5024 *gc5024 = to_gc5024(sd);

	__gc5024_power_off(gc5024);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc5024_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc5024 *gc5024 = to_gc5024(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc5024_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc5024->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc5024->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sensor_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	struct gc5024 *sensor = to_gc5024(sd);
	struct device *dev = &sensor->client->dev;

	dev_info(dev, "%s(%d) enter!\n", __func__, __LINE__);

	if (2 == sensor->lane_num) {
		config->type = V4L2_MBUS_CSI2;
		config->flags = V4L2_MBUS_CSI2_2_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else {
		dev_err(&sensor->client->dev,
				"unsupported lane_num(%d)\n", sensor->lane_num);
	}
	return 0;
}

static int gc5024_enum_frame_interval(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc5024 *gc5024 = to_gc5024(sd);

	if (fie->index >= gc5024->cfg_num)
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static const struct dev_pm_ops gc5024_pm_ops = {
	SET_RUNTIME_PM_OPS(gc5024_runtime_suspend,
			   gc5024_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc5024_internal_ops = {
	.open = gc5024_open,
};
#endif

static const struct v4l2_subdev_core_ops gc5024_core_ops = {
	.s_power = gc5024_s_power,
	.ioctl = gc5024_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc5024_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc5024_video_ops = {
	.g_mbus_config = sensor_g_mbus_config,
	.s_stream = gc5024_s_stream,
	.g_frame_interval = gc5024_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc5024_pad_ops = {
	.enum_mbus_code = gc5024_enum_mbus_code,
	.enum_frame_size = gc5024_enum_frame_sizes,
	.enum_frame_interval = gc5024_enum_frame_interval,
	.get_fmt = gc5024_get_fmt,
	.set_fmt = gc5024_set_fmt,
};

static const struct v4l2_subdev_ops gc5024_subdev_ops = {
	.core	= &gc5024_core_ops,
	.video	= &gc5024_video_ops,
	.pad	= &gc5024_pad_ops,
};

static int gc5024_set_gain_reg(struct gc5024 *gc5024, u32 a_gain)
{
	int ret = 0;
	u32 temp = 0;

	ret = gc5024_write_reg(gc5024->client,
				 GC5024_PAGE_SELECT,
				 0x00);
	if (a_gain >= GC5024_ANALOG_GAIN_1 &&
		 a_gain < GC5024_ANALOG_GAIN_2) {
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_ANALOG_GAIN_REG,
			 0x00);
		temp = a_gain;
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC5024_ANALOG_GAIN_2 &&
		 a_gain < GC5024_ANALOG_GAIN_3) {
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_ANALOG_GAIN_REG,
			 0x01);
		temp = 64 * a_gain / GC5024_ANALOG_GAIN_2;
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC5024_ANALOG_GAIN_3 &&
		 a_gain < GC5024_ANALOG_GAIN_4) {
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_ANALOG_GAIN_REG,
			 0x02);
		temp = 64 * a_gain / GC5024_ANALOG_GAIN_3;
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC5024_ANALOG_GAIN_4 &&
		 a_gain < GC5024_ANALOG_GAIN_5) {
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_ANALOG_GAIN_REG,
			 0x03);
		temp = 64 * a_gain / GC5024_ANALOG_GAIN_4;
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC5024_ANALOG_GAIN_5 &&
		 a_gain < GC5024_ANALOG_GAIN_6) {
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_ANALOG_GAIN_REG,
			 0x04);
		temp = 64 * a_gain / GC5024_ANALOG_GAIN_5;
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC5024_ANALOG_GAIN_6 &&
		 a_gain < GC5024_ANALOG_GAIN_7) {
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_ANALOG_GAIN_REG,
			 0x05);
		temp = 64 * a_gain / GC5024_ANALOG_GAIN_6;
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else {
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_ANALOG_GAIN_REG,
			 0x06);
		temp = 64 * a_gain / GC5024_ANALOG_GAIN_7;
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc5024_write_reg(gc5024->client,
			 GC5024_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	}
	return ret;
}

static int gc5024_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc5024 *gc5024 = container_of(ctrl->handler,
					     struct gc5024, ctrl_handler);
	struct i2c_client *client = gc5024->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = gc5024->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc5024->exposure,
					 gc5024->exposure->minimum, max,
					 gc5024->exposure->step,
					 gc5024->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret |= gc5024_write_reg(gc5024->client,
					 GC5024_PAGE_SELECT,
					0x00);
		ret |= gc5024_write_reg(gc5024->client,
					 GC5024_REG_EXPOSURE_H,
					 (ctrl->val >> 8) & 0x3f);
		ret |= gc5024_write_reg(gc5024->client,
					 GC5024_REG_EXPOSURE_L,
					 ctrl->val & 0xff);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = gc5024_set_gain_reg(gc5024, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = gc5024_write_reg(gc5024->client,
			GC5024_PAGE_SELECT,
			0x00);
		ret |= gc5024_write_reg(gc5024->client,
			GC5024_REG_VTS_H,
			((ctrl->val) >> 8) & 0x1f);
		ret |= gc5024_write_reg(gc5024->client,
			GC5024_REG_VTS_L,
			(ctrl->val) & 0xff);
		break;

	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc5024_ctrl_ops = {
	.s_ctrl = gc5024_set_ctrl,
};

static int gc5024_initialize_controls(struct gc5024 *gc5024)
{
	const struct gc5024_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	struct device *dev = &gc5024->client->dev;

	dev_info(dev, "Enter %s(%d) !\n", __func__, __LINE__);
	handler = &gc5024->ctrl_handler;
	mode = gc5024->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &gc5024->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, GC5024_PIXEL_RATE, 1, GC5024_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	gc5024->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (gc5024->hblank)
		gc5024->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc5024->vblank = v4l2_ctrl_new_std(handler, &gc5024_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				GC5024_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc5024->exposure = v4l2_ctrl_new_std(handler, &gc5024_ctrl_ops,
				V4L2_CID_EXPOSURE, GC5024_EXPOSURE_MIN,
				exposure_max, GC5024_EXPOSURE_STEP,
				mode->exp_def);

	gc5024->anal_gain = v4l2_ctrl_new_std(handler, &gc5024_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, GC5024_GAIN_MIN,
				GC5024_GAIN_MAX, GC5024_GAIN_STEP,
				GC5024_GAIN_DEFAULT);

	if (handler->error) {
		ret = handler->error;
		dev_err(&gc5024->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc5024->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc5024_check_sensor_id(struct gc5024 *gc5024,
				  struct i2c_client *client)
{
	struct device *dev = &gc5024->client->dev;
	u8 pid, ver = 0x00;
	int ret;
	unsigned short id;

	ret = gc5024_read_reg(client, GC5024_REG_CHIP_ID_H, &pid);
	if (ret) {
		dev_err(dev, "Read chip ID H register error\n");
		return ret;
	}

	ret = gc5024_read_reg(client, GC5024_REG_CHIP_ID_L, &ver);
	if (ret) {
		dev_err(dev, "Read chip ID L register error\n");
		return ret;
	}

	id = SENSOR_ID(pid, ver);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return ret;
	}

	dev_info(dev, "detected gc%04x sensor\n", id);

	return 0;
}

static int gc5024_configure_regulators(struct gc5024 *gc5024)
{
	unsigned int i;

	for (i = 0; i < GC5024_NUM_SUPPLIES; i++)
		gc5024->supplies[i].supply = gc5024_supply_names[i];

	return devm_regulator_bulk_get(&gc5024->client->dev,
				       GC5024_NUM_SUPPLIES,
				       gc5024->supplies);
}

static int gc5024_parse_of(struct gc5024 *gc5024)
{
	struct device *dev = &gc5024->client->dev;
	struct device_node *endpoint;
	struct fwnode_handle *fwnode;
	int rval;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	fwnode = of_fwnode_handle(endpoint);
	rval = fwnode_property_read_u32_array(fwnode, "data-lanes", NULL, 0);
	if (rval <= 0) {
		dev_warn(dev, " Get mipi lane num failed!\n");
		return -1;
	}

	gc5024->lane_num = rval;
	if (2 == gc5024->lane_num) {
		gc5024->cur_mode = &supported_modes_2lane[0];
		supported_modes = supported_modes_2lane;
		gc5024->cfg_num = ARRAY_SIZE(supported_modes_2lane);

		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		gc5024->pixel_rate = MIPI_FREQ * 2U * gc5024->lane_num / 10U;
		dev_info(dev, "lane_num(%d)  pixel_rate(%u)\n",
				 gc5024->lane_num, gc5024->pixel_rate);
	} else {
		dev_err(dev, "unsupported lane_num(%d)\n", gc5024->lane_num);
		return -1;
	}
	return 0;
}

static int gc5024_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc5024 *gc5024;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc5024 = devm_kzalloc(dev, sizeof(*gc5024), GFP_KERNEL);
	if (!gc5024)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc5024->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc5024->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc5024->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc5024->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc5024->client = client;

	gc5024->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc5024->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc5024->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc5024->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc5024->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_HIGH);
	if (IS_ERR(gc5024->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = gc5024_parse_of(gc5024);
	if (ret != 0)
		return -EINVAL;
	gc5024->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc5024->pinctrl)) {
		gc5024->pins_default =
			pinctrl_lookup_state(gc5024->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc5024->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc5024->pins_sleep =
			pinctrl_lookup_state(gc5024->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc5024->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = gc5024_configure_regulators(gc5024);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&gc5024->mutex);

	sd = &gc5024->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc5024_subdev_ops);
	ret = gc5024_initialize_controls(gc5024);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc5024_power_on(gc5024);
	if (ret)
		goto err_free_handler;

	ret = gc5024_check_sensor_id(gc5024, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc5024_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc5024->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc5024->pad);

	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc5024->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc5024->module_index, facing,
		 GC5024_NAME, dev_name(sd->dev));
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
	__gc5024_power_off(gc5024);
err_free_handler:
	v4l2_ctrl_handler_free(&gc5024->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc5024->mutex);

	return ret;
}

static int gc5024_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5024 *gc5024 = to_gc5024(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc5024->ctrl_handler);
	mutex_destroy(&gc5024->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc5024_power_off(gc5024);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc5024_of_match[] = {
	{ .compatible = "galaxycore,gc5024" },
	{},
};
MODULE_DEVICE_TABLE(of, gc5024_of_match);
#endif

static const struct i2c_device_id gc5024_match_id[] = {
	{ "galaxycore,gc5024", 0 },
	{ },
};

static struct i2c_driver gc5024_i2c_driver = {
	.driver = {
		.name = GC5024_NAME,
		.pm = &gc5024_pm_ops,
		.of_match_table = of_match_ptr(gc5024_of_match),
	},
	.probe		= &gc5024_probe,
	.remove		= &gc5024_remove,
	.id_table	= gc5024_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc5024_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc5024_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GC5024 CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
