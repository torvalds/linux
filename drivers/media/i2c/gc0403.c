// SPDX-License-Identifier: GPL-2.0
/*
 * gc0403 driver
 *
 * Copyright (C) 2019 Fuzhou Rockchip Electronics Co.,Ltd.
 * V0.0X01.0X02 add enum_frame_interval function.
 * V0.0X01.0X03 add quick stream on/off
 * V0.0X01.0X04 add function g_mbus_config
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
#include <linux/rk-camera-module.h>
#include <linux/version.h>

/*
 * GC0403 register definitions
 */
#define GC0403_REG_EXP_H		0x03
#define GC0403_REG_EXP_L		0x04
#define GC0403_REG_VBLK_H		0x07
#define GC0403_REG_VBLK_L		0x08
#define GC0403_REG_MIPI_EN		0x10
#define GC0403_REG_DGAIN_H		0xb1
#define GC0403_REG_DGAIN_L		0xb2
#define GC0403_REG_AGAIN		0xb6
#define GC0403_REG_CHIP_ID_H		0xf0
#define GC0403_REG_CHIP_ID_L		0xf1
#define PAGE_SELECT_REG			0xfe
#define REG_NULL			0xff

#define GC0403_CHIP_ID			0x0403
#define SENSOR_ID(_msb, _lsb)	((_msb) << 8 | (_lsb))

#define GC0403_ANALOG_GAIN_MIN		0
#define GC0403_ANALOG_GAIN_MAX		0x0a
#define GC0403_ANALOG_GAIN_STP		1
#define GC0403_ANALOG_GAIN_DFT		1
/* gain[4:0]  [0x00,0x0a]*/
#define GC0403_FETCH_ANALOG_GAIN(VAL) ((VAL) & 0x0F)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN	V4L2_CID_GAIN
#endif

#define GC0403_DIGI_GAIN_MIN		0x40
#define GC0403_DIGI_GAIN_MAX		0x3ff
#define GC0403_DIGI_GAIN_STP		1
#define GC0403_DIGI_GAIN_DFT		0x40
/* gain[9:6] */
#define GC0403_FETCH_DIGITAL_GAIN_HIGH(VAL) (((VAL) >> 6) & 0x0F)
/* gain[5:0] */
#define GC0403_FETCH_DIGITAL_GAIN_LOW(VAL) (((VAL) << 2) & 0xFC)

#define GC0403_TOTAL_GAIN_MIN		100
#define GC0403_TOTAL_GAIN_MAX		3200
#define GC0403_TOTAL_GAIN_STEP		1

#define GC0403_EXPOSURE_MAX		8191
#define GC0403_EXPOSURE_MIN		1

#define GC0403_NAME			"gc0403"
#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x03)

#define GC0403_XVCLK_FREQ		24000000
#define GC0403_LINK_FREQ		96000000
#define GC0403_PIXEL_RATE		(GC0403_LINK_FREQ * 2 * 1 / 10)

#define GC0403_LANES			1

static const s64 link_freq_menu_items[] = {
	GC0403_LINK_FREQ
};

static const char * const gc0403_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
};

#define GC0403_NUM_SUPPLIES	ARRAY_SIZE(gc0403_supply_names)

struct regval {
	u8 addr;
	u8 val;
};

struct gc0403_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct gc0403 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[GC0403_NUM_SUPPLIES];

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	/* mutex lock, protect current operation */
	struct mutex		mutex;
	bool			streaming;
	const struct gc0403_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_gc0403(sd) container_of(sd, struct gc0403, subdev)

/*
 * MCLK=24Mhz
 * MIPI_CLOCK=192Mbps
 * Actual_window_size=640*480
 * HD=1362
 * VD=586
 * row_time=56.75us,FPS = 30fps;
 */
static const struct regval gc0403_vga_regs[] = {
	/****SYS****/
	{0xfe, 0x80},
	{0xfe, 0x80},
	{0xfe, 0x80},
	{0xf2, 0x00},	/* sync_pad_io_ebi */
	{0xf6, 0x00},	/*up down */
	{0xfc, 0xc6},
	{0xf7, 0x19},	/* pll enable */
	{0xf8, 0x01},	/* Pll mode 2 */
	{0xf9, 0x3e},	/* [0] pll enable solve IOVDD large current problem */
	{0xfe, 0x03},
	{0x06, 0x80},
	{0x06, 0x00},
	{0xfe, 0x00},
	{0xf9, 0x2e},
	{0xfe, 0x00},
	{0xfa, 0x00},	/* div */
	{0xfe, 0x00},
	/**ANALOG&CISCTL**/
	{0x03, 0x02},
	{0x04, 0x55},
	{0x05, 0x01},	/* H blank */
	{0x06, 0x49},	/* H blank=bb=187 */
	{0x07, 0x00},	/* VB */
	{0x08, 0x5a},	/* VB=E8=232 */
	{0x09, 0x00},
	{0x0a, 0x2c},
	{0x0b, 0x00},
	{0x0c, 0x3c},
	{0x0d, 0x01},	/* win_height */
	{0x0e, 0xf0},	/* 496 */
	{0x0f, 0x02},	/* win_width */
	{0x10, 0x90},	/* 656 */
	{0x11, 0x23},	/* 44FPN abnormal column */
	{0x12, 0x10},
	{0x13, 0x11},
	{0x14, 0x01},
	{0x15, 0x00},
	{0x16, 0xc0},
	{0x17, 0x14},
	{0x18, 0x02},
	{0x19, 0x38},
	{0x1a, 0x11},
	{0x1b, 0x06},
	{0x1c, 0x04},
	{0x1d, 0x00},
	{0x1e, 0xfc},
	{0x1f, 0x09},
	{0x20, 0xb5},
	{0x21, 0x3f},
	{0x22, 0xe6},
	{0x23, 0x32},
	{0x24, 0x2f},
	{0x27, 0x00},
	{0x28, 0x00},
	{0x2a, 0x00},
	{0x2b, 0x00},
	{0x2c, 0x00},
	{0x2d, 0x01},
	{0x2e, 0xf0},
	{0x2f, 0x01},
	{0x25, 0xc0},
	{0x3d, 0xe0},
	{0x3e, 0x45},
	{0x3f, 0x1f},
	{0xc2, 0x17},
	{0x30, 0x22},
	{0x31, 0x23},
	{0x32, 0x02},
	{0x33, 0x03},
	{0x34, 0x04},
	{0x35, 0x05},
	{0x36, 0x06},
	{0x37, 0x07},
	{0x38, 0x0f},
	{0x39, 0x17},
	{0x3a, 0x1f},
	/****ISP****/
	{0xfe, 0x00},
	{0x8a, 0x00},
	{0x8c, 0x07},
	{0x8e, 0x02},	/* luma value not normal */
	{0x90, 0x01},
	{0x94, 0x02},
	{0x95, 0x01},
	{0x96, 0xe0},	/* 480 */
	{0x97, 0x02},
	{0x98, 0x80},	/* 640 */
	/****BLK****/
	{0xfe, 0x00},
	{0x18, 0x02},
	{0x40, 0x22},
	{0x41, 0x01},
	{0x5e, 0x00},
	{0x66, 0x20},
	/****MIPI****/
	{0xfe, 0x03},
	{0x01, 0x83},
	{0x02, 0x11},
	{0x03, 0x96},
	{0x04, 0x01},
	{0x05, 0x00},
	{0x06, 0xa4},
	{0x10, 0x90},
	{0x11, 0x2b},
	{0x12, 0x20},
	{0x13, 0x03},
	{0x15, 0x00},
	{0x21, 0x10},
	{0x22, 0x03},
	{0x23, 0x20},
	{0x24, 0x02},
	{0x25, 0x10},
	{0x26, 0x05},
	{0x21, 0x10},
	{0x29, 0x03},
	{0x2a, 0x0a},
	{0x2b, 0x04},
	{0xfe, 0x00},
	{0xb0, 0x50},
	{0xb6, 0x09},
	{REG_NULL, 0x00},
};

/*
 * MCLK=24Mhz
 * MIPI_CLOCK=192Mbps
 * Actual_window_size=768*576
 * HD=1206
 * VD=663
 * row_time=50.25us,FPS = 30fps;
 */
static const struct regval gc0403_768x576_regs[] = {
	/****SYS****/
	{0xfe, 0x80},
	{0xfe, 0x80},
	{0xfe, 0x80},
	{0xf2, 0x00},	/* sync_pad_io_ebi */
	{0xf6, 0x00},	/* up down */
	{0xfc, 0xc6},
	{0xf7, 0x19},	/* pll enable */
	{0xf8, 0x01},	/* Pll mode 2 */
	{0xf9, 0x3e},	/* [0] pll enable solve IOVDD large current problem */
	{0xfe, 0x03},
	{0x06, 0x80},
	{0x06, 0x00},
	{0xfe, 0x00},
	{0xf9, 0x2e},
	{0xfe, 0x00},
	{0xfa, 0x00},	/* div */
	{0xfe, 0x00},
	/**ANALOG&CISCTL**/
	{0x03, 0x02},
	{0x04, 0x55},
	{0x05, 0x00},	/* H blank */
	{0x06, 0xbb},	/* H blank=bb=187 */
	{0x07, 0x00},	/* VB */
	{0x08, 0x46},	/* VB=E8=232 */
	{0x0c, 0x04},
	{0x0d, 0x02},	/* win_height */
	{0x0e, 0x48},	/* 584 */
	{0x0f, 0x03},	/* win_width */
	{0x10, 0x08},	/* 776 */
	{0x11, 0x23},	/* 44FPN abnormal column */
	{0x12, 0x10},
	{0x13, 0x11},
	{0x14, 0x01},
	{0x15, 0x00},
	{0x16, 0xc0},
	{0x17, 0x14},
	{0x18, 0x02},
	{0x19, 0x38},
	{0x1a, 0x11},
	{0x1b, 0x06},
	{0x1c, 0x04},
	{0x1d, 0x00},
	{0x1e, 0xfc},
	{0x1f, 0x09},
	{0x20, 0xb5},
	{0x21, 0x3f},
	{0x22, 0xe6},
	{0x23, 0x32},
	{0x24, 0x2f},
	{0x27, 0x00},
	{0x28, 0x00},
	{0x2a, 0x00},
	{0x2b, 0x00},
	{0x2c, 0x00},
	{0x2d, 0x01},
	{0x2e, 0xf0},
	{0x2f, 0x01},
	{0x25, 0xc0},
	{0x3d, 0xe0},
	{0x3e, 0x45},
	{0x3f, 0x1f},
	{0xc2, 0x17},
	{0x30, 0x22},
	{0x31, 0x23},
	{0x32, 0x02},
	{0x33, 0x03},
	{0x34, 0x04},
	{0x35, 0x05},
	{0x36, 0x06},
	{0x37, 0x07},
	{0x38, 0x0f},
	{0x39, 0x17},
	{0x3a, 0x1f},
	/****ISP****/
	{0xfe, 0x00},
	{0x8a, 0x00},
	{0x8c, 0x07},
	{0x8e, 0x02},	/* luma value not normal */
	{0x90, 0x01},
	{0x94, 0x02},
	{0x95, 0x02},
	{0x96, 0x40},	/* 576 */
	{0x97, 0x03},
	{0x98, 0x00},	/* 768 */
	/****BLK****/
	{0xfe, 0x00},
	{0x18, 0x02},
	{0x40, 0x22},
	{0x41, 0x01},
	{0x5e, 0x00},
	{0x66, 0x20},
	/****MIPI****/
	{0xfe, 0x03},
	{0x01, 0x83},
	{0x02, 0x11},
	{0x03, 0x96},
	{0x04, 0x01},
	{0x05, 0x00},
	{0x06, 0xa4},
	{0x10, 0x80},
	{0x11, 0x2b},
	{0x12, 0xc0},
	{0x13, 0x03},
	{0x15, 0x00},
	{0x21, 0x10},
	{0x22, 0x03},
	{0x23, 0x20},
	{0x24, 0x02},
	{0x25, 0x10},
	{0x26, 0x05},
	{0x21, 0x10},
	{0x29, 0x01},
	{0x2a, 0x0a},
	{0x2b, 0x04},
	{0xfe, 0x00},
	{0xb0, 0x50},
	{0xb6, 0x01},
	{REG_NULL, 0x00},
};

static const struct gc0403_mode supported_modes[] = {
	{
		.width = 640,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 500,
		.hts_def = 1362,
		.vts_def = 586,
		.reg_list = gc0403_vga_regs,
	},
	{
		.width = 768,
		.height = 576,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 500,
		.hts_def = 1206,
		.vts_def = 663,
		.reg_list = gc0403_768x576_regs,
	}
};

/* sensor register write */
static int gc0403_write_reg(struct i2c_client *client, u8 reg, u8 val)
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
		"gc0403 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int gc0403_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	int i, ret = 0;

	i = 0;
	while (regs[i].addr != REG_NULL) {
		ret = gc0403_write_reg(client, regs[i].addr, regs[i].val);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}
		i++;
	}

	return ret;
}

/* sensor register read */
static int gc0403_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"gc0403 read reg(0x%x val:0x%x) failed !\n", reg, *val);

	return ret;
}

static int gc0403_get_reso_dist(const struct gc0403_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct gc0403_mode *
gc0403_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = gc0403_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc0403_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc0403 *gc0403 = to_gc0403(sd);
	const struct gc0403_mode *mode;

	mutex_lock(&gc0403->mutex);

	mode = gc0403_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc0403->mutex);
		return -ENOTTY;
#endif
	} else {
		gc0403->cur_mode = mode;
	}

	mutex_unlock(&gc0403->mutex);

	return 0;
}

static int gc0403_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc0403 *gc0403 = to_gc0403(sd);
	const struct gc0403_mode *mode = gc0403->cur_mode;

	mutex_lock(&gc0403->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc0403->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc0403->mutex);

	return 0;
}

static int gc0403_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SRGGB10_1X10;

	return 0;
}

static int gc0403_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int gc0403_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc0403 *gc0403 = to_gc0403(sd);
	const struct gc0403_mode *mode = gc0403->cur_mode;

	mutex_lock(&gc0403->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc0403->mutex);

	return 0;
}

static void gc0403_get_module_inf(struct gc0403 *gc0403,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, GC0403_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc0403->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, gc0403->len_name, sizeof(inf->base.lens));
}

static long gc0403_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc0403 *gc0403 = to_gc0403(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc0403_get_module_inf(gc0403, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream) {
			ret = gc0403_write_reg(gc0403->client, PAGE_SELECT_REG, 0x03);
			ret |= gc0403_write_reg(gc0403->client, GC0403_REG_MIPI_EN, 0x90);
			ret |= gc0403_write_reg(gc0403->client, PAGE_SELECT_REG, 0x00);
		} else {
			ret = gc0403_write_reg(gc0403->client, PAGE_SELECT_REG, 0x03);
			ret |= gc0403_write_reg(gc0403->client, GC0403_REG_MIPI_EN, 0x80);
			ret |= gc0403_write_reg(gc0403->client, PAGE_SELECT_REG, 0x00);
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc0403_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = gc0403_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret) {
				ret = -EFAULT;
				return ret;
				}
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
			ret = gc0403_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc0403_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __gc0403_start_stream(struct gc0403 *gc0403)
{
	int ret = 0;

	/* In case these controls are set before streaming */
	mutex_unlock(&gc0403->mutex);
	ret = v4l2_ctrl_handler_setup(&gc0403->ctrl_handler);
	mutex_lock(&gc0403->mutex);
	if (ret)
		return ret;
	ret = gc0403_write_reg(gc0403->client, PAGE_SELECT_REG, 0x03);
	ret |= gc0403_write_reg(gc0403->client, GC0403_REG_MIPI_EN, 0x90);
	ret |= gc0403_write_reg(gc0403->client, PAGE_SELECT_REG, 0x00);

	return ret;
}

static int __gc0403_stop_stream(struct gc0403 *gc0403)
{
	int ret = 0;

	ret = gc0403_write_reg(gc0403->client, PAGE_SELECT_REG, 0x03);
	ret |= gc0403_write_reg(gc0403->client, GC0403_REG_MIPI_EN, 0x80);
	ret |= gc0403_write_reg(gc0403->client, PAGE_SELECT_REG, 0x00);

	return ret;
}

static int __gc0403_power_on(struct gc0403 *gc0403);
static void __gc0403_power_off(struct gc0403 *gc0403);

static int gc0403_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc0403 *gc0403 = to_gc0403(sd);
	int ret = 0;

	mutex_lock(&gc0403->mutex);

	on = !!on;
	if (on)
		ret = pm_runtime_get_sync(&gc0403->client->dev);
	else
		ret = pm_runtime_put(&gc0403->client->dev);

	mutex_unlock(&gc0403->mutex);

	return ret;
}

static int gc0403_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc0403 *gc0403 = to_gc0403(sd);
	int ret = 0;

	mutex_lock(&gc0403->mutex);
	on = !!on;
	if (on == gc0403->streaming)
		goto unlock_and_return;

	if (on) {
		ret = __gc0403_start_stream(gc0403);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			goto unlock_and_return;
		}
	} else {
		__gc0403_stop_stream(gc0403);
		usleep_range(33 * 1000, 35 * 1000);
	}

	gc0403->streaming = on;

unlock_and_return:
	mutex_unlock(&gc0403->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static int __gc0403_power_on(struct gc0403 *gc0403)
{
	int ret;
	struct device *dev = &gc0403->client->dev;

	ret = clk_set_rate(gc0403->xvclk, GC0403_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(gc0403->xvclk) != GC0403_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched,modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc0403->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(GC0403_NUM_SUPPLIES, gc0403->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(gc0403->pwdn_gpio))
		gpiod_set_value_cansleep(gc0403->pwdn_gpio, 0);
	/* here usleep at least 10~15ms，will better */
	usleep_range(10 * 1000, 15 * 1000);

	ret = gc0403_write_array(gc0403->client, gc0403->cur_mode->reg_list);
	if (ret)
		return ret;
	usleep_range(10 * 1000, 20 * 1000);

	return 0;

disable_clk:
	clk_disable_unprepare(gc0403->xvclk);

	return ret;
}

static void __gc0403_power_off(struct gc0403 *gc0403)
{
	if (!IS_ERR(gc0403->pwdn_gpio))
		gpiod_set_value_cansleep(gc0403->pwdn_gpio, 1);
	clk_disable_unprepare(gc0403->xvclk);
	regulator_bulk_disable(GC0403_NUM_SUPPLIES, gc0403->supplies);
}

static int gc0403_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0403 *gc0403 = to_gc0403(sd);
	int ret;

	ret = __gc0403_power_on(gc0403);
	if (ret)
		return ret;

	if (gc0403->streaming) {
		ret = gc0403_s_stream(sd, 1);
		if (ret)
			__gc0403_power_off(gc0403);
	}

	return ret;
}

static int gc0403_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0403 *gc0403 = to_gc0403(sd);

	__gc0403_power_off(gc0403);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc0403_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc0403 *gc0403 = to_gc0403(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc0403_mode *def_mode = &supported_modes[1];

	mutex_lock(&gc0403->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc0403->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int gc0403_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int gc0403_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (GC0403_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops gc0403_pm_ops = {
	SET_RUNTIME_PM_OPS(gc0403_runtime_suspend,
			   gc0403_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc0403_internal_ops = {
	.open = gc0403_open,
};
#endif

static struct v4l2_subdev_core_ops gc0403_core_ops = {
	.s_power = gc0403_s_power,
	.ioctl = gc0403_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc0403_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc0403_video_ops = {
	.s_stream = gc0403_s_stream,
	.g_frame_interval = gc0403_g_frame_interval,
	.g_mbus_config = gc0403_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops gc0403_pad_ops = {
	.enum_mbus_code = gc0403_enum_mbus_code,
	.enum_frame_size = gc0403_enum_frame_sizes,
	.enum_frame_interval = gc0403_enum_frame_interval,
	.get_fmt = gc0403_get_fmt,
	.set_fmt = gc0403_set_fmt,
};

static const struct v4l2_subdev_ops gc0403_subdev_ops = {
	.core	= &gc0403_core_ops,
	.video	= &gc0403_video_ops,
	.pad	= &gc0403_pad_ops,
};

static int gc0403_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc0403 *gc0403 = container_of(ctrl->handler,
					     struct gc0403, ctrl_handler);
	struct i2c_client *client = gc0403->client;
	int ret = 0;
	int analog_gain_table[] = {100, 142, 250, 354, 490, 691, 970,
				   1363, 1945, 2704, 3889};
	int table_cnt = 11;
	int analog_gain_reg_value = 0x00;
	int digital_gain_reg_value = 0x00;
	int total_gain = 0;
	int analog_gain = 0;
	int i = 0;

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev,
			"gc0403: V4L2_CID_EXPOSURE exp val = 0x%x\n",
			ctrl->val);
		/* 4 least significant bits of expsoure are fractional part */
		ret = gc0403_write_reg(gc0403->client, GC0403_REG_EXP_H,
				       (ctrl->val >> 8) & 0x1f);
		ret |= gc0403_write_reg(gc0403->client, GC0403_REG_EXP_L,
				       ctrl->val & 0xff);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
	case V4L2_CID_DIGITAL_GAIN:
		total_gain = ctrl->val;
		for (i = 0; i < table_cnt; i++) {
			if (total_gain < analog_gain_table[i])
				break;
		}

		i = i - 1;
		if (i < 0)
			i = 0;

		analog_gain = analog_gain_table[i];
		analog_gain_reg_value = i;
		digital_gain_reg_value =  total_gain * 64 / analog_gain;

		if (analog_gain_reg_value < GC0403_ANALOG_GAIN_MIN)
			analog_gain_reg_value = GC0403_ANALOG_GAIN_MIN;
		if (analog_gain_reg_value > GC0403_ANALOG_GAIN_MAX)
			analog_gain_reg_value = GC0403_ANALOG_GAIN_MAX;

		if (digital_gain_reg_value < GC0403_DIGI_GAIN_MIN)
			digital_gain_reg_value = GC0403_DIGI_GAIN_MIN;
		if (digital_gain_reg_value > GC0403_DIGI_GAIN_MAX)
			digital_gain_reg_value = GC0403_DIGI_GAIN_MAX;

		ret = gc0403_write_reg(gc0403->client,
				GC0403_REG_AGAIN,
				GC0403_FETCH_ANALOG_GAIN
					(analog_gain_reg_value));
		ret |= gc0403_write_reg(gc0403->client,
				GC0403_REG_DGAIN_H,
				GC0403_FETCH_DIGITAL_GAIN_HIGH
					(digital_gain_reg_value));
		ret |= gc0403_write_reg(gc0403->client,
				GC0403_REG_DGAIN_L,
				GC0403_FETCH_DIGITAL_GAIN_LOW
					(digital_gain_reg_value));

		dev_dbg(&client->dev, "gc0403: gain: %d,a: %d,d: %d\n",
			total_gain, analog_gain_reg_value,
			digital_gain_reg_value);
		break;
	case V4L2_CID_VBLANK:
		ret = gc0403_write_reg(gc0403->client, GC0403_REG_VBLK_H,
				       (ctrl->val) >> 8);
		ret |= gc0403_write_reg(gc0403->client, GC0403_REG_VBLK_L,
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

static const struct v4l2_ctrl_ops gc0403_ctrl_ops = {
	.s_ctrl = gc0403_set_ctrl,
};

static int gc0403_initialize_controls(struct gc0403 *gc0403)
{
	const struct gc0403_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc0403->ctrl_handler;
	mode = gc0403->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 5);
	if (ret)
		return ret;
	handler->lock = &gc0403->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, GC0403_PIXEL_RATE, 1, GC0403_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	gc0403->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (gc0403->hblank)
		gc0403->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc0403->vblank = v4l2_ctrl_new_std(handler, &gc0403_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def, vblank_def,
				1, vblank_def);

	gc0403->exposure = v4l2_ctrl_new_std(handler, &gc0403_ctrl_ops,
				V4L2_CID_EXPOSURE,
				GC0403_EXPOSURE_MIN, GC0403_EXPOSURE_MAX,
				1, mode->exp_def);

	/* Anolog gain */
	gc0403->anal_gain = v4l2_ctrl_new_std(handler, &gc0403_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, GC0403_TOTAL_GAIN_MIN,
				GC0403_TOTAL_GAIN_MAX, GC0403_TOTAL_GAIN_STEP,
				GC0403_TOTAL_GAIN_MIN);

	/* Digital gain */
	gc0403->digi_gain = v4l2_ctrl_new_std(handler, &gc0403_ctrl_ops,
				V4L2_CID_DIGITAL_GAIN, GC0403_TOTAL_GAIN_MIN,
				GC0403_TOTAL_GAIN_MAX, GC0403_TOTAL_GAIN_STEP,
				GC0403_TOTAL_GAIN_MIN);

	if (handler->error) {
		ret = handler->error;
		dev_err(&gc0403->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc0403->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc0403_check_sensor_id(struct gc0403 *gc0403,
				  struct i2c_client *client)
{
	struct device *dev = &gc0403->client->dev;
	u8 pid = 0, ver = 0;
	u16 id = 0;
	int ret = 0;

	/* Check sensor revision */
	ret = gc0403_read_reg(client, GC0403_REG_CHIP_ID_H, &pid);
	ret |= gc0403_read_reg(client, GC0403_REG_CHIP_ID_L, &ver);
	if (ret) {
		dev_err(&client->dev, "gc0403_read_reg failed (%d)\n", ret);
		return ret;
	}

	id = SENSOR_ID(pid, ver);
	if (id != GC0403_CHIP_ID) {
		dev_err(&client->dev,
				"Sensor detection failed (%04X,%d)\n",
				id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected GC%04x sensor\n", id);

	return 0;
}

static int gc0403_configure_regulators(struct gc0403 *gc0403)
{
	unsigned int i;

	for (i = 0; i < GC0403_NUM_SUPPLIES; i++)
		gc0403->supplies[i].supply = gc0403_supply_names[i];

	return devm_regulator_bulk_get(&gc0403->client->dev,
				       GC0403_NUM_SUPPLIES,
				       gc0403->supplies);
}

static int gc0403_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	/* add a dev_node */
	struct device_node *node = dev->of_node;
	struct gc0403 *gc0403;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	/* add info */
	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc0403 = devm_kzalloc(dev, sizeof(*gc0403), GFP_KERNEL);
	if (!gc0403)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc0403->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc0403->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc0403->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc0403->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc0403->client = client;
	gc0403->cur_mode = &supported_modes[1];

	gc0403->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc0403->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc0403->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc0403->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = gc0403_configure_regulators(gc0403);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&gc0403->mutex);

	sd = &gc0403->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc0403_subdev_ops);
	ret = gc0403_initialize_controls(gc0403);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc0403_power_on(gc0403);
	if (ret)
		goto err_free_handler;

	ret = gc0403_check_sensor_id(gc0403, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc0403_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc0403->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc0403->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc0403->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc0403->module_index, facing,
		 GC0403_NAME, dev_name(sd->dev));

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
	__gc0403_power_off(gc0403);
err_free_handler:
	v4l2_ctrl_handler_free(&gc0403->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc0403->mutex);

	return ret;
}

static int gc0403_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0403 *gc0403 = to_gc0403(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc0403->ctrl_handler);
	mutex_destroy(&gc0403->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc0403_power_off(gc0403);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc0403_of_match[] = {
	{ .compatible = "galaxycore,gc0403" },
	{},
};
MODULE_DEVICE_TABLE(of, gc0403_of_match);
#endif

static const struct i2c_device_id gc0403_match_id[] = {
	{ "gc0403", 0 },
	{ },
};

static struct i2c_driver gc0403_i2c_driver = {
	.driver = {
		.name = "gc0403",
		.pm = &gc0403_pm_ops,
		.of_match_table = of_match_ptr(gc0403_of_match),
	},
	.probe		= &gc0403_probe,
	.remove		= &gc0403_remove,
	.id_table	= gc0403_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc0403_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc0403_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Galaxycore gc0403 sensor driver");
MODULE_LICENSE("GPL v2");
