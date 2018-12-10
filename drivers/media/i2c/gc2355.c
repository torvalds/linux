/*SPDX-License-Identifier: GPL-2.0 */
/*
 * gc2355 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 */
#define DEBUG 1
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
#include <linux/pinctrl/consumer.h>

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC2355_LINK_FREQ_420MHZ		420000000
/* pixel rate = link frequency * 1 * lanes / BITS_PER_SAMPLE */
#define GC2355_PIXEL_RATE		(GC2355_LINK_FREQ_420MHZ * 2 * 1 / 10)
#define GC2355_XVCLK_FREQ		24000000

#define CHIP_ID				0x2355
#define GC2355_REG_CHIP_ID_H		0xf0
#define GC2355_REG_CHIP_ID_L		0xf1
#define SENSOR_ID(_msb, _lsb)		((_msb) << 8 | (_lsb))

#define GC2355_PAGE_SELECT		0xfe
#define GC2355_MODE_SELECT		0x10
#define GC2355_MODE_SW_STANDBY		0x00
#define GC2355_MODE_STREAMING		0x90

#define GC2355_REG_EXPOSURE_H		0x03
#define GC2355_REG_EXPOSURE_L		0x04
#define	GC2355_EXPOSURE_MIN		4
#define	GC2355_EXPOSURE_STEP		1
#define GC2355_VTS_MAX			0x7fff

#define GC2355_ANALOG_GAIN_1 64    /*1.00x*/
#define GC2355_ANALOG_GAIN_2 88    /*1.375x*/
#define GC2355_ANALOG_GAIN_3 122   /*1.90x*/
#define GC2355_ANALOG_GAIN_4 168   /*2.625x*/
#define GC2355_ANALOG_GAIN_5 239   /*3.738x*/
#define GC2355_ANALOG_GAIN_6 330   /*5.163x*/
#define GC2355_ANALOG_GAIN_7 470   /*7.350x*/

#define GC2355_ANALOG_GAIN_REG		0xb6
#define GC2355_PREGAIN_H_REG		0xb1
#define GC2355_PREGAIN_L_REG		0xb2

#define GC2355_GAIN_MIN			0x40
#define GC2355_GAIN_MAX			0x200
#define GC2355_GAIN_STEP		1
#define GC2355_GAIN_DEFAULT		0x80

#define GC2355_REG_VTS_H			0x03
#define GC2355_REG_VTS_L			0x04

#define REG_NULL			0xFFFF

#define GC2355_LANES			1
#define GC2355_BITS_PER_SAMPLE		10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

static const char * const gc2355_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define GC2355_NUM_SUPPLIES ARRAY_SIZE(gc2355_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct gc2355_mode {
	u32 width;
	u32 height;
	u32 max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct gc2355 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[GC2355_NUM_SUPPLIES];

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
	const struct gc2355_mode *cur_mode;
};

#define to_gc2355(sd) container_of(sd, struct gc2355, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval gc2355_global_regs[] = {
	/////////////////////////////////////////////////////
//////////////////////  SYS  //////////////////////
/////////////////////////////////////////////////////
	{0xfe, 0x80},
	{0xfe, 0x80},
	{0xfe, 0x80},
	{0xf2, 0x00}, //sync_pad_io_ebi
	{0xf6, 0x00}, //up down
	{0xfc, 0x06},
	{0xf7, 0x19}, //19 //clk_double pll enable
	{0xf8, 0x06}, //Pll mode 2
	{0xf9, 0x0e}, //de//[0] pll enable
	{0xfa, 0x00}, //div
	{0xfe, 0x00},

/////////////////////////////////////////////////////
////////////////  ANALOG & CISCTL  ////////////////
/////////////////////////////////////////////////////
	{0x03, 0x04},
	{0x04, 0x5f},
	{0x05, 0x01}, //HB
	{0x06, 0x22},
	{0x07, 0x00}, //VB
	{0x08, 0x0b},
	{0x0a, 0x00}, //row start
	{0x0c, 0x04}, //0c//col start
	{0x0d, 0x04},
	{0x0e, 0xc0},
	{0x0f, 0x06},
	{0x10, 0x50}, //Window setting 1616x1216
	{0x17, 0x14},
	{0x19, 0x0b}, //09
	{0x1b, 0x49}, //48
	{0x1c, 0x12},
	{0x1d, 0x10}, //double reset
	{0x1e, 0xbc}, //a8//col_r/rowclk_mode/rsthigh_en FPN
	{0x1f, 0xc8}, //08//rsgl_s_mode/vpix_s_mode
	{0x20, 0x71},
	{0x21, 0x20}, //rsg
	{0x22, 0xa0},
	{0x23, 0x51}, //01
	{0x24, 0x19}, //0b //55
	{0x27, 0x20},
	{0x28, 0x00},
	{0x2b, 0x81}, //80 //00 sf_s_mode FPN
	{0x2c, 0x38}, //50 //5c ispg FPN
	{0x2e, 0x16}, //05//eq width
	{0x2f, 0x14}, //[3:0]tx_width
	{0x30, 0x00},
	{0x31, 0x01},
	{0x32, 0x02},
	{0x33, 0x03},
	{0x34, 0x07},
	{0x35, 0x0b},
	{0x36, 0x0f},

/////////////////////////////////////////////////////
//////////////////////	 gain	/////////////////////
/////////////////////////////////////////////////////
	{0xb0, 0x50}, //1.25x
	{0xb1, 0x02},
	{0xb2, 0xe0}, //2.86x
	{0xb3, 0x40},
	{0xb4, 0x40},
	{0xb5, 0x40},
	{0xb6, 0x03}, //2.8x

/////////////////////////////////////////////////////
//////////////////////	 crop	/////////////////////
/////////////////////////////////////////////////////
	{0x92, 0x02},
	{0x95, 0x04},
	{0x96, 0xb0},
	{0x97, 0x06},
	{0x98, 0x40}, //out window set 1600x1200

/////////////////////////////////////////////////////
//////////////////////	BLK	/////////////////////
/////////////////////////////////////////////////////
	{0x18, 0x02},
	{0x1a, 0x01},
	{0x40, 0x42},
	{0x41, 0x00},

	{0x44, 0x00},
	{0x45, 0x00},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4a, 0x00},
	{0x4b, 0x00}, //clear offset

	{0x4e, 0x3c}, //BLK select
	{0x4f, 0x00},
	{0x5e, 0x00}, //offset ratio
	{0x66, 0x20}, //dark ratio

	{0x6a, 0x02},
	{0x6b, 0x02},
	{0x6c, 0x00},
	{0x6d, 0x00},
	{0x6e, 0x00},
	{0x6f, 0x00},
	{0x70, 0x02},
	{0x71, 0x02}, //manual offset

/////////////////////////////////////////////////////
//////////////////  Dark sun  /////////////////////
/////////////////////////////////////////////////////
	{0x87, 0x03}, //
	{0xe0, 0xe7}, //dark sun en/extend mode
	{0xe3, 0xc0}, //clamp

/////////////////////////////////////////////////////
//////////////////////	 MIPI	/////////////////////
/////////////////////////////////////////////////////
	{0xfe, 0x03},
	{0x01, 0x83}, //0x87 2lane
	{0x02, 0x00},
	{0x03, 0x90},
	{0x04, 0x01},
	{0x05, 0x00},
	{0x06, 0xa2},
	{0x10, 0x00}, //94//1lane raw8
	{0x11, 0x2b},
	{0x12, 0xd0},
	{0x13, 0x07},

/* p3:0x15 [1:0]clklane_mode
 * 00 : Enter LP mode between Frame;
 * 01: Enter LP mode between Row;
 * 10: Continuous HS mode
 */
	{0x15, 0x60},
	{0x21, 0x10},
	{0x22, 0x05},
	{0x23, 0x30},
	{0x24, 0x02},
	{0x25, 0x15},
	{0x26, 0x08},
	{0x27, 0x06},
	{0x29, 0x06},
	{0x2a, 0x0a},
	{0x2b, 0x08},
	{0x40, 0x00},
	{0x41, 0x00},
	{0x42, 0x40},
	{0x43, 0x06},
	{0xfe, 0x00},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 1008Mbps
 */
static const struct regval gc2355_1600x1200_regs[] = {
	{REG_NULL, 0x00},
};

static const struct gc2355_mode supported_modes[] = {
	{
		.width = 1600,
		.height = 1200,
		.max_fps = 30,
		.exp_def = 0x04d0,
		.hts_def = 0x08cc,
		.vts_def = 0x04d9,
		.reg_list = gc2355_1600x1200_regs,
	},
};

static const s64 link_freq_menu_items[] = {
	GC2355_LINK_FREQ_420MHZ
};

/* sensor register write */
static int gc2355_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	dev_info(&client->dev, "%s(%d) enter!\n", __func__, __LINE__);
	dev_info(&client->dev, "gc2355 write reg(0x%x val:0x%x)!\n", reg, val);
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
		"gc2355 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

/* sensor register read */
static int gc2355_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"gc2355 read reg:0x%x failed !\n", reg);

	return ret;
}

static int gc2355_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc2355_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

static int gc2355_get_reso_dist(const struct gc2355_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct gc2355_mode *
gc2355_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = gc2355_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc2355_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc2355 *gc2355 = to_gc2355(sd);
	const struct gc2355_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc2355->mutex);

	mode = gc2355_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc2355->mutex);
		return -ENOTTY;
#endif
	} else {
		gc2355->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc2355->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc2355->vblank, vblank_def,
					 GC2355_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&gc2355->mutex);

	return 0;
}

static int gc2355_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc2355 *gc2355 = to_gc2355(sd);
	const struct gc2355_mode *mode = gc2355->cur_mode;

	mutex_lock(&gc2355->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc2355->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc2355->mutex);

	return 0;
}

static int gc2355_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SRGGB10_1X10;

	return 0;
}

static int gc2355_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int gc2355_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc2355 *gc2355 = to_gc2355(sd);
	const struct gc2355_mode *mode = gc2355->cur_mode;

	mutex_lock(&gc2355->mutex);
	fi->interval.numerator = 10000;
	fi->interval.denominator = mode->max_fps * 10000;
	mutex_unlock(&gc2355->mutex);

	return 0;
}

static int __gc2355_start_stream(struct gc2355 *gc2355)
{
	int ret;

	ret = gc2355_write_array(gc2355->client, gc2355_global_regs);
	if (ret)
		return ret;

	ret = gc2355_write_array(gc2355->client, gc2355->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&gc2355->mutex);
	ret = v4l2_ctrl_handler_setup(&gc2355->ctrl_handler);
	mutex_lock(&gc2355->mutex);
	if (ret)
		return ret;

	ret = gc2355_write_reg(gc2355->client, GC2355_PAGE_SELECT, 0x03);
	ret = gc2355_write_reg(gc2355->client, GC2355_MODE_SELECT,
				 GC2355_MODE_STREAMING);
	ret = gc2355_write_reg(gc2355->client, GC2355_PAGE_SELECT, 0x00);
	return ret;
}

static int __gc2355_stop_stream(struct gc2355 *gc2355)
{
	int ret;

	ret = gc2355_write_reg(gc2355->client, GC2355_PAGE_SELECT, 0x03);
	ret = gc2355_write_reg(gc2355->client, GC2355_MODE_SELECT,
				 GC2355_MODE_SW_STANDBY);
	ret = gc2355_write_reg(gc2355->client, GC2355_PAGE_SELECT, 0x00);
	return ret;
}

static int gc2355_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc2355 *gc2355 = to_gc2355(sd);
	struct i2c_client *client = gc2355->client;
	int ret = 0;

	mutex_lock(&gc2355->mutex);
	on = !!on;
	if (on == gc2355->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc2355_start_stream(gc2355);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc2355_stop_stream(gc2355);
		pm_runtime_put(&client->dev);
	}

	gc2355->streaming = on;

unlock_and_return:
	mutex_unlock(&gc2355->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc2355_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC2355_XVCLK_FREQ / 1000 / 1000);
}

static int __gc2355_power_on(struct gc2355 *gc2355)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc2355->client->dev;

	if (!IS_ERR_OR_NULL(gc2355->pins_default)) {
		ret = pinctrl_select_state(gc2355->pinctrl,
					   gc2355->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = clk_prepare_enable(gc2355->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(gc2355->reset_gpio))
		gpiod_set_value_cansleep(gc2355->reset_gpio, 0);

	ret = regulator_bulk_enable(GC2355_NUM_SUPPLIES, gc2355->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(gc2355->reset_gpio))
		gpiod_set_value_cansleep(gc2355->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(gc2355->pwdn_gpio))
		gpiod_set_value_cansleep(gc2355->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc2355_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc2355->xvclk);

	return ret;
}

static void __gc2355_power_off(struct gc2355 *gc2355)
{
	int ret;
	struct device *dev = &gc2355->client->dev;

	if (!IS_ERR(gc2355->pwdn_gpio))
		gpiod_set_value_cansleep(gc2355->pwdn_gpio, 0);
	clk_disable_unprepare(gc2355->xvclk);
	if (!IS_ERR(gc2355->reset_gpio))
		gpiod_set_value_cansleep(gc2355->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(gc2355->pins_sleep)) {
		ret = pinctrl_select_state(gc2355->pinctrl,
					   gc2355->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(GC2355_NUM_SUPPLIES, gc2355->supplies);
}

static int gc2355_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2355 *gc2355 = to_gc2355(sd);

	return __gc2355_power_on(gc2355);
}

static int gc2355_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2355 *gc2355 = to_gc2355(sd);

	__gc2355_power_off(gc2355);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc2355_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc2355 *gc2355 = to_gc2355(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc2355_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc2355->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc2355->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static const struct dev_pm_ops gc2355_pm_ops = {
	SET_RUNTIME_PM_OPS(gc2355_runtime_suspend,
			   gc2355_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc2355_internal_ops = {
	.open = gc2355_open,
};
#endif

static const struct v4l2_subdev_video_ops gc2355_video_ops = {
	.s_stream = gc2355_s_stream,
	.g_frame_interval = gc2355_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc2355_pad_ops = {
	.enum_mbus_code = gc2355_enum_mbus_code,
	.enum_frame_size = gc2355_enum_frame_sizes,
	.get_fmt = gc2355_get_fmt,
	.set_fmt = gc2355_set_fmt,
};

static const struct v4l2_subdev_ops gc2355_subdev_ops = {
	.video	= &gc2355_video_ops,
	.pad	= &gc2355_pad_ops,
};

static int gc2355_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc2355 *gc2355 = container_of(ctrl->handler,
					     struct gc2355, ctrl_handler);
	struct i2c_client *client = gc2355->client;
	s64 max;
	int ret = 0;
	s32 usGain, temp;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = gc2355->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc2355->exposure,
					 gc2355->exposure->minimum, max,
					 gc2355->exposure->step,
					 gc2355->exposure->default_value);
		break;
	}

	if (pm_runtime_get(&client->dev) <= 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = gc2355_write_reg(gc2355->client,
					 GC2355_PAGE_SELECT,
					0x00);
		ret = gc2355_write_reg(gc2355->client,
					 GC2355_REG_EXPOSURE_H,
					 (ctrl->val >> 8) & 0x3f);
		ret = gc2355_write_reg(gc2355->client,
					 GC2355_REG_EXPOSURE_L,
					 ctrl->val & 0xff);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		usGain = ctrl->val;
		ret = gc2355_write_reg(gc2355->client,
					 GC2355_PAGE_SELECT,
					 0x03);
		if ((usGain >= GC2355_ANALOG_GAIN_1) &&
			 (usGain < GC2355_ANALOG_GAIN_2)) {
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_ANALOG_GAIN_REG,
				 0x00);
			temp = usGain;
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_PREGAIN_H_REG,
				 temp >> 6);
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_PREGAIN_L_REG,
				 (temp << 2) & 0xfc);
		} else if ((usGain >= GC2355_ANALOG_GAIN_2) &&
			 (usGain < GC2355_ANALOG_GAIN_3)) {
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_ANALOG_GAIN_REG,
				 0x01);
			temp = 64 * usGain / GC2355_ANALOG_GAIN_2;
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_PREGAIN_H_REG,
				 temp >> 6);
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_PREGAIN_L_REG,
				 (temp << 2) & 0xfc);
		} else if ((usGain >= GC2355_ANALOG_GAIN_3) &&
			 (usGain < GC2355_ANALOG_GAIN_4)) {
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_ANALOG_GAIN_REG,
				 0x02);
			temp = 64 * usGain / GC2355_ANALOG_GAIN_3;
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_PREGAIN_H_REG,
				 temp >> 6);
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_PREGAIN_L_REG,
				 (temp << 2) & 0xfc);
		} else if (usGain >= GC2355_ANALOG_GAIN_4) {
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_ANALOG_GAIN_REG,
				 0x03);
			temp = 64 * usGain / GC2355_ANALOG_GAIN_4;
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_PREGAIN_H_REG,
				 temp >> 6);
			ret = gc2355_write_reg(gc2355->client,
				 GC2355_PREGAIN_L_REG,
				 (temp << 2) & 0xfc);
		}
		break;
	case V4L2_CID_VBLANK:
		ret = gc2355_write_reg(gc2355->client,
			 GC2355_PAGE_SELECT,
			 0x00);
		ret = gc2355_write_reg(gc2355->client,
			 GC2355_REG_VTS_H,
			 ((ctrl->val + gc2355->cur_mode->height) >> 8) & 0x3f);
		ret = gc2355_write_reg(gc2355->client, GC2355_REG_VTS_L,
				       (ctrl->val + gc2355->cur_mode->height) &
					   0xff);
		break;

	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc2355_ctrl_ops = {
	.s_ctrl = gc2355_set_ctrl,
};

static int gc2355_initialize_controls(struct gc2355 *gc2355)
{
	const struct gc2355_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	struct device *dev = &gc2355->client->dev;

	dev_info(dev, "Enter %s(%d) !\n", __func__, __LINE__);
	handler = &gc2355->ctrl_handler;
	mode = gc2355->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &gc2355->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, GC2355_PIXEL_RATE, 1, GC2355_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	gc2355->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (gc2355->hblank)
		gc2355->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc2355->vblank = v4l2_ctrl_new_std(handler, &gc2355_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				GC2355_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc2355->exposure = v4l2_ctrl_new_std(handler, &gc2355_ctrl_ops,
				V4L2_CID_EXPOSURE, GC2355_EXPOSURE_MIN,
				exposure_max, GC2355_EXPOSURE_STEP,
				mode->exp_def);

	gc2355->anal_gain = v4l2_ctrl_new_std(handler, &gc2355_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, GC2355_GAIN_MIN,
				GC2355_GAIN_MAX, GC2355_GAIN_STEP,
				GC2355_GAIN_DEFAULT);

	if (handler->error) {
		ret = handler->error;
		dev_err(&gc2355->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc2355->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc2355_check_sensor_id(struct gc2355 *gc2355,
				  struct i2c_client *client)
{
	struct device *dev = &gc2355->client->dev;
	u8 pid, ver = 0x00;
	int ret;
	unsigned short id;

	ret = gc2355_read_reg(client, GC2355_REG_CHIP_ID_H, &pid);
	if (ret) {
		dev_err(dev, "Read chip ID H register error\n");
		return ret;
	}

	ret = gc2355_read_reg(client, GC2355_REG_CHIP_ID_L, &ver);
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

static int gc2355_configure_regulators(struct gc2355 *gc2355)
{
	unsigned int i;

	for (i = 0; i < GC2355_NUM_SUPPLIES; i++)
		gc2355->supplies[i].supply = gc2355_supply_names[i];

	return devm_regulator_bulk_get(&gc2355->client->dev,
				       GC2355_NUM_SUPPLIES,
				       gc2355->supplies);
}

static int gc2355_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct gc2355 *gc2355;
	struct v4l2_subdev *sd;
	int ret;

	gc2355 = devm_kzalloc(dev, sizeof(*gc2355), GFP_KERNEL);
	if (!gc2355)
		return -ENOMEM;

	gc2355->client = client;
	gc2355->cur_mode = &supported_modes[0];

	gc2355->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc2355->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}
	ret = clk_set_rate(gc2355->xvclk, GC2355_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(gc2355->xvclk) != GC2355_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	gc2355->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc2355->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc2355->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc2355->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	gc2355->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc2355->pinctrl)) {
		gc2355->pins_default =
			pinctrl_lookup_state(gc2355->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc2355->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc2355->pins_sleep =
			pinctrl_lookup_state(gc2355->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc2355->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = gc2355_configure_regulators(gc2355);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&gc2355->mutex);

	sd = &gc2355->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc2355_subdev_ops);
	ret = gc2355_initialize_controls(gc2355);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc2355_power_on(gc2355);
	if (ret)
		goto err_free_handler;

	ret = gc2355_check_sensor_id(gc2355, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc2355_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc2355->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&sd->entity, 1, &gc2355->pad, 0);
	if (ret < 0)
		goto err_power_off;
#endif

	ret = v4l2_async_register_subdev(sd);
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
	__gc2355_power_off(gc2355);
err_free_handler:
	v4l2_ctrl_handler_free(&gc2355->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc2355->mutex);

	return ret;
}

static int gc2355_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2355 *gc2355 = to_gc2355(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc2355->ctrl_handler);
	mutex_destroy(&gc2355->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc2355_power_off(gc2355);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc2355_of_match[] = {
	{ .compatible = "galaxycore,gc2355" },
	{},
};
MODULE_DEVICE_TABLE(of, gc2355_of_match);
#endif

static const struct i2c_device_id gc2355_match_id[] = {
	{ "galaxycore,gc2355", 0 },
	{ },
};

static struct i2c_driver gc2355_i2c_driver = {
	.driver = {
		.name = "gc2355",
		.pm = &gc2355_pm_ops,
		.of_match_table = of_match_ptr(gc2355_of_match),
	},
	.probe		= &gc2355_probe,
	.remove		= &gc2355_remove,
	.id_table	= gc2355_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc2355_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc2355_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GC2355 CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
