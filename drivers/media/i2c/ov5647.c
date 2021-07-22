// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for OmniVision OV5647 cameras.
 *
 * Based on Samsung S5K6AAFX SXGA 1/6" 1.3M CMOS Image Sensor driver
 * Copyright (C) 2011 Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * Based on Omnivision OV7670 Camera Driver
 * Copyright (C) 2006-7 Jonathan Corbet <corbet@lwn.net>
 *
 * Copyright (C) 2016, Synopsys, Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>

/*
 * From the datasheet, "20ms after PWDN goes low or 20ms after RESETB goes
 * high if reset is inserted after PWDN goes high, host can access sensor's
 * SCCB to initialize sensor."
 */
#define PWDN_ACTIVE_DELAY_MS	20

#define MIPI_CTRL00_CLOCK_LANE_GATE		BIT(5)
#define MIPI_CTRL00_LINE_SYNC_ENABLE		BIT(4)
#define MIPI_CTRL00_BUS_IDLE			BIT(2)
#define MIPI_CTRL00_CLOCK_LANE_DISABLE		BIT(0)

#define OV5647_SW_STANDBY		0x0100
#define OV5647_SW_RESET			0x0103
#define OV5647_REG_CHIPID_H		0x300a
#define OV5647_REG_CHIPID_L		0x300b
#define OV5640_REG_PAD_OUT		0x300d
#define OV5647_REG_EXP_HI		0x3500
#define OV5647_REG_EXP_MID		0x3501
#define OV5647_REG_EXP_LO		0x3502
#define OV5647_REG_AEC_AGC		0x3503
#define OV5647_REG_GAIN_HI		0x350a
#define OV5647_REG_GAIN_LO		0x350b
#define OV5647_REG_VTS_HI		0x380e
#define OV5647_REG_VTS_LO		0x380f
#define OV5647_REG_FRAME_OFF_NUMBER	0x4202
#define OV5647_REG_MIPI_CTRL00		0x4800
#define OV5647_REG_MIPI_CTRL14		0x4814
#define OV5647_REG_AWB			0x5001

#define REG_TERM 0xfffe
#define VAL_TERM 0xfe
#define REG_DLY  0xffff

/* OV5647 native and active pixel array size */
#define OV5647_NATIVE_WIDTH		2624U
#define OV5647_NATIVE_HEIGHT		1956U

#define OV5647_PIXEL_ARRAY_LEFT		16U
#define OV5647_PIXEL_ARRAY_TOP		16U
#define OV5647_PIXEL_ARRAY_WIDTH	2592U
#define OV5647_PIXEL_ARRAY_HEIGHT	1944U

#define OV5647_VBLANK_MIN		4
#define OV5647_VTS_MAX			32767

#define OV5647_EXPOSURE_MIN		4
#define OV5647_EXPOSURE_STEP		1
#define OV5647_EXPOSURE_DEFAULT		1000
#define OV5647_EXPOSURE_MAX		65535

struct regval_list {
	u16 addr;
	u8 data;
};

struct ov5647_mode {
	struct v4l2_mbus_framefmt	format;
	struct v4l2_rect		crop;
	u64				pixel_rate;
	int				hts;
	int				vts;
	const struct regval_list	*reg_list;
	unsigned int			num_regs;
};

struct ov5647 {
	struct v4l2_subdev		sd;
	struct media_pad		pad;
	struct mutex			lock;
	struct clk			*xclk;
	struct gpio_desc		*pwdn;
	bool				clock_ncont;
	struct v4l2_ctrl_handler	ctrls;
	const struct ov5647_mode	*mode;
	struct v4l2_ctrl		*pixel_rate;
	struct v4l2_ctrl		*hblank;
	struct v4l2_ctrl		*vblank;
	struct v4l2_ctrl		*exposure;
	bool				streaming;
};

static inline struct ov5647 *to_sensor(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov5647, sd);
}

static const struct regval_list sensor_oe_disable_regs[] = {
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
};

static const struct regval_list sensor_oe_enable_regs[] = {
	{0x3000, 0x0f},
	{0x3001, 0xff},
	{0x3002, 0xe4},
};

static struct regval_list ov5647_2592x1944_10bpp[] = {
	{0x0100, 0x00},
	{0x0103, 0x01},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x3036, 0x69},
	{0x303c, 0x11},
	{0x3106, 0xf5},
	{0x3821, 0x06},
	{0x3820, 0x00},
	{0x3827, 0xec},
	{0x370c, 0x03},
	{0x3612, 0x5b},
	{0x3618, 0x04},
	{0x5000, 0x06},
	{0x5002, 0x41},
	{0x5003, 0x08},
	{0x5a00, 0x08},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3016, 0x08},
	{0x3017, 0xe0},
	{0x3018, 0x44},
	{0x301c, 0xf8},
	{0x301d, 0xf0},
	{0x3a18, 0x00},
	{0x3a19, 0xf8},
	{0x3c01, 0x80},
	{0x3b07, 0x0c},
	{0x380c, 0x0b},
	{0x380d, 0x1c},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3708, 0x64},
	{0x3709, 0x12},
	{0x3808, 0x0a},
	{0x3809, 0x20},
	{0x380a, 0x07},
	{0x380b, 0x98},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xa3},
	{0x3811, 0x10},
	{0x3813, 0x06},
	{0x3630, 0x2e},
	{0x3632, 0xe2},
	{0x3633, 0x23},
	{0x3634, 0x44},
	{0x3636, 0x06},
	{0x3620, 0x64},
	{0x3621, 0xe0},
	{0x3600, 0x37},
	{0x3704, 0xa0},
	{0x3703, 0x5a},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x3731, 0x02},
	{0x370b, 0x60},
	{0x3705, 0x1a},
	{0x3f05, 0x02},
	{0x3f06, 0x10},
	{0x3f01, 0x0a},
	{0x3a08, 0x01},
	{0x3a09, 0x28},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0d, 0x08},
	{0x3a0e, 0x06},
	{0x3a0f, 0x58},
	{0x3a10, 0x50},
	{0x3a1b, 0x58},
	{0x3a1e, 0x50},
	{0x3a11, 0x60},
	{0x3a1f, 0x28},
	{0x4001, 0x02},
	{0x4004, 0x04},
	{0x4000, 0x09},
	{0x4837, 0x19},
	{0x4800, 0x24},
	{0x3503, 0x03},
	{0x0100, 0x01},
};

static struct regval_list ov5647_1080p30_10bpp[] = {
	{0x0100, 0x00},
	{0x0103, 0x01},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x3036, 0x62},
	{0x303c, 0x11},
	{0x3106, 0xf5},
	{0x3821, 0x06},
	{0x3820, 0x00},
	{0x3827, 0xec},
	{0x370c, 0x03},
	{0x3612, 0x5b},
	{0x3618, 0x04},
	{0x5000, 0x06},
	{0x5002, 0x41},
	{0x5003, 0x08},
	{0x5a00, 0x08},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3016, 0x08},
	{0x3017, 0xe0},
	{0x3018, 0x44},
	{0x301c, 0xf8},
	{0x301d, 0xf0},
	{0x3a18, 0x00},
	{0x3a19, 0xf8},
	{0x3c01, 0x80},
	{0x3b07, 0x0c},
	{0x380c, 0x09},
	{0x380d, 0x70},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3708, 0x64},
	{0x3709, 0x12},
	{0x3808, 0x07},
	{0x3809, 0x80},
	{0x380a, 0x04},
	{0x380b, 0x38},
	{0x3800, 0x01},
	{0x3801, 0x5c},
	{0x3802, 0x01},
	{0x3803, 0xb2},
	{0x3804, 0x08},
	{0x3805, 0xe3},
	{0x3806, 0x05},
	{0x3807, 0xf1},
	{0x3811, 0x04},
	{0x3813, 0x02},
	{0x3630, 0x2e},
	{0x3632, 0xe2},
	{0x3633, 0x23},
	{0x3634, 0x44},
	{0x3636, 0x06},
	{0x3620, 0x64},
	{0x3621, 0xe0},
	{0x3600, 0x37},
	{0x3704, 0xa0},
	{0x3703, 0x5a},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x3731, 0x02},
	{0x370b, 0x60},
	{0x3705, 0x1a},
	{0x3f05, 0x02},
	{0x3f06, 0x10},
	{0x3f01, 0x0a},
	{0x3a08, 0x01},
	{0x3a09, 0x4b},
	{0x3a0a, 0x01},
	{0x3a0b, 0x13},
	{0x3a0d, 0x04},
	{0x3a0e, 0x03},
	{0x3a0f, 0x58},
	{0x3a10, 0x50},
	{0x3a1b, 0x58},
	{0x3a1e, 0x50},
	{0x3a11, 0x60},
	{0x3a1f, 0x28},
	{0x4001, 0x02},
	{0x4004, 0x04},
	{0x4000, 0x09},
	{0x4837, 0x19},
	{0x4800, 0x34},
	{0x3503, 0x03},
	{0x0100, 0x01},
};

static struct regval_list ov5647_2x2binned_10bpp[] = {
	{0x0100, 0x00},
	{0x0103, 0x01},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x3036, 0x62},
	{0x303c, 0x11},
	{0x3106, 0xf5},
	{0x3827, 0xec},
	{0x370c, 0x03},
	{0x3612, 0x59},
	{0x3618, 0x00},
	{0x5000, 0x06},
	{0x5002, 0x41},
	{0x5003, 0x08},
	{0x5a00, 0x08},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3016, 0x08},
	{0x3017, 0xe0},
	{0x3018, 0x44},
	{0x301c, 0xf8},
	{0x301d, 0xf0},
	{0x3a18, 0x00},
	{0x3a19, 0xf8},
	{0x3c01, 0x80},
	{0x3b07, 0x0c},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xa3},
	{0x3808, 0x05},
	{0x3809, 0x10},
	{0x380a, 0x03},
	{0x380b, 0xcc},
	{0x380c, 0x07},
	{0x380d, 0x68},
	{0x3811, 0x0c},
	{0x3813, 0x06},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3630, 0x2e},
	{0x3632, 0xe2},
	{0x3633, 0x23},
	{0x3634, 0x44},
	{0x3636, 0x06},
	{0x3620, 0x64},
	{0x3621, 0xe0},
	{0x3600, 0x37},
	{0x3704, 0xa0},
	{0x3703, 0x5a},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x3731, 0x02},
	{0x370b, 0x60},
	{0x3705, 0x1a},
	{0x3f05, 0x02},
	{0x3f06, 0x10},
	{0x3f01, 0x0a},
	{0x3a08, 0x01},
	{0x3a09, 0x28},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0d, 0x08},
	{0x3a0e, 0x06},
	{0x3a0f, 0x58},
	{0x3a10, 0x50},
	{0x3a1b, 0x58},
	{0x3a1e, 0x50},
	{0x3a11, 0x60},
	{0x3a1f, 0x28},
	{0x4001, 0x02},
	{0x4004, 0x04},
	{0x4000, 0x09},
	{0x4837, 0x16},
	{0x4800, 0x24},
	{0x3503, 0x03},
	{0x3820, 0x41},
	{0x3821, 0x07},
	{0x350a, 0x00},
	{0x350b, 0x10},
	{0x3500, 0x00},
	{0x3501, 0x1a},
	{0x3502, 0xf0},
	{0x3212, 0xa0},
	{0x0100, 0x01},
};

static struct regval_list ov5647_640x480_10bpp[] = {
	{0x0100, 0x00},
	{0x0103, 0x01},
	{0x3035, 0x11},
	{0x3036, 0x46},
	{0x303c, 0x11},
	{0x3821, 0x07},
	{0x3820, 0x41},
	{0x370c, 0x03},
	{0x3612, 0x59},
	{0x3618, 0x00},
	{0x5000, 0x06},
	{0x5003, 0x08},
	{0x5a00, 0x08},
	{0x3000, 0xff},
	{0x3001, 0xff},
	{0x3002, 0xff},
	{0x301d, 0xf0},
	{0x3a18, 0x00},
	{0x3a19, 0xf8},
	{0x3c01, 0x80},
	{0x3b07, 0x0c},
	{0x380c, 0x07},
	{0x380d, 0x3c},
	{0x3814, 0x35},
	{0x3815, 0x35},
	{0x3708, 0x64},
	{0x3709, 0x52},
	{0x3808, 0x02},
	{0x3809, 0x80},
	{0x380a, 0x01},
	{0x380b, 0xe0},
	{0x3800, 0x00},
	{0x3801, 0x10},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x2f},
	{0x3806, 0x07},
	{0x3807, 0x9f},
	{0x3630, 0x2e},
	{0x3632, 0xe2},
	{0x3633, 0x23},
	{0x3634, 0x44},
	{0x3620, 0x64},
	{0x3621, 0xe0},
	{0x3600, 0x37},
	{0x3704, 0xa0},
	{0x3703, 0x5a},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x3731, 0x02},
	{0x370b, 0x60},
	{0x3705, 0x1a},
	{0x3f05, 0x02},
	{0x3f06, 0x10},
	{0x3f01, 0x0a},
	{0x3a08, 0x01},
	{0x3a09, 0x2e},
	{0x3a0a, 0x00},
	{0x3a0b, 0xfb},
	{0x3a0d, 0x02},
	{0x3a0e, 0x01},
	{0x3a0f, 0x58},
	{0x3a10, 0x50},
	{0x3a1b, 0x58},
	{0x3a1e, 0x50},
	{0x3a11, 0x60},
	{0x3a1f, 0x28},
	{0x4001, 0x02},
	{0x4004, 0x02},
	{0x4000, 0x09},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3017, 0xe0},
	{0x301c, 0xfc},
	{0x3636, 0x06},
	{0x3016, 0x08},
	{0x3827, 0xec},
	{0x3018, 0x44},
	{0x3035, 0x21},
	{0x3106, 0xf5},
	{0x3034, 0x1a},
	{0x301c, 0xf8},
	{0x4800, 0x34},
	{0x3503, 0x03},
	{0x0100, 0x01},
};

static const struct ov5647_mode ov5647_modes[] = {
	/* 2592x1944 full resolution full FOV 10-bit mode. */
	{
		.format = {
			.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
			.colorspace	= V4L2_COLORSPACE_SRGB,
			.field		= V4L2_FIELD_NONE,
			.width		= 2592,
			.height		= 1944
		},
		.crop = {
			.left		= OV5647_PIXEL_ARRAY_LEFT,
			.top		= OV5647_PIXEL_ARRAY_TOP,
			.width		= 2592,
			.height		= 1944
		},
		.pixel_rate	= 87500000,
		.hts		= 2844,
		.vts		= 0x7b0,
		.reg_list	= ov5647_2592x1944_10bpp,
		.num_regs	= ARRAY_SIZE(ov5647_2592x1944_10bpp)
	},
	/* 1080p30 10-bit mode. Full resolution centre-cropped down to 1080p. */
	{
		.format = {
			.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
			.colorspace	= V4L2_COLORSPACE_SRGB,
			.field		= V4L2_FIELD_NONE,
			.width		= 1920,
			.height		= 1080
		},
		.crop = {
			.left		= 348 + OV5647_PIXEL_ARRAY_LEFT,
			.top		= 434 + OV5647_PIXEL_ARRAY_TOP,
			.width		= 1928,
			.height		= 1080,
		},
		.pixel_rate	= 81666700,
		.hts		= 2416,
		.vts		= 0x450,
		.reg_list	= ov5647_1080p30_10bpp,
		.num_regs	= ARRAY_SIZE(ov5647_1080p30_10bpp)
	},
	/* 2x2 binned full FOV 10-bit mode. */
	{
		.format = {
			.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
			.colorspace	= V4L2_COLORSPACE_SRGB,
			.field		= V4L2_FIELD_NONE,
			.width		= 1296,
			.height		= 972
		},
		.crop = {
			.left		= OV5647_PIXEL_ARRAY_LEFT,
			.top		= OV5647_PIXEL_ARRAY_TOP,
			.width		= 2592,
			.height		= 1944,
		},
		.pixel_rate	= 81666700,
		.hts		= 1896,
		.vts		= 0x59b,
		.reg_list	= ov5647_2x2binned_10bpp,
		.num_regs	= ARRAY_SIZE(ov5647_2x2binned_10bpp)
	},
	/* 10-bit VGA full FOV 60fps. 2x2 binned and subsampled down to VGA. */
	{
		.format = {
			.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
			.colorspace	= V4L2_COLORSPACE_SRGB,
			.field		= V4L2_FIELD_NONE,
			.width		= 640,
			.height		= 480
		},
		.crop = {
			.left		= 16 + OV5647_PIXEL_ARRAY_LEFT,
			.top		= OV5647_PIXEL_ARRAY_TOP,
			.width		= 2560,
			.height		= 1920,
		},
		.pixel_rate	= 55000000,
		.hts		= 1852,
		.vts		= 0x1f8,
		.reg_list	= ov5647_640x480_10bpp,
		.num_regs	= ARRAY_SIZE(ov5647_640x480_10bpp)
	},
};

/* Default sensor mode is 2x2 binned 640x480 SBGGR10_1X10. */
#define OV5647_DEFAULT_MODE	(&ov5647_modes[3])
#define OV5647_DEFAULT_FORMAT	(ov5647_modes[3].format)

static int ov5647_write16(struct v4l2_subdev *sd, u16 reg, u16 val)
{
	unsigned char data[4] = { reg >> 8, reg & 0xff, val >> 8, val & 0xff};
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = i2c_master_send(client, data, 4);
	if (ret < 0) {
		dev_dbg(&client->dev, "%s: i2c write error, reg: %x\n",
			__func__, reg);
		return ret;
	}

	return 0;
}

static int ov5647_write(struct v4l2_subdev *sd, u16 reg, u8 val)
{
	unsigned char data[3] = { reg >> 8, reg & 0xff, val};
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = i2c_master_send(client, data, 3);
	if (ret < 0) {
		dev_dbg(&client->dev, "%s: i2c write error, reg: %x\n",
				__func__, reg);
		return ret;
	}

	return 0;
}

static int ov5647_read(struct v4l2_subdev *sd, u16 reg, u8 *val)
{
	unsigned char data_w[2] = { reg >> 8, reg & 0xff };
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = i2c_master_send(client, data_w, 2);
	if (ret < 0) {
		dev_dbg(&client->dev, "%s: i2c write error, reg: %x\n",
			__func__, reg);
		return ret;
	}

	ret = i2c_master_recv(client, val, 1);
	if (ret < 0) {
		dev_dbg(&client->dev, "%s: i2c read error, reg: %x\n",
				__func__, reg);
		return ret;
	}

	return 0;
}

static int ov5647_write_array(struct v4l2_subdev *sd,
			      const struct regval_list *regs, int array_size)
{
	int i, ret;

	for (i = 0; i < array_size; i++) {
		ret = ov5647_write(sd, regs[i].addr, regs[i].data);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int ov5647_set_virtual_channel(struct v4l2_subdev *sd, int channel)
{
	u8 channel_id;
	int ret;

	ret = ov5647_read(sd, OV5647_REG_MIPI_CTRL14, &channel_id);
	if (ret < 0)
		return ret;

	channel_id &= ~(3 << 6);

	return ov5647_write(sd, OV5647_REG_MIPI_CTRL14,
			    channel_id | (channel << 6));
}

static int ov5647_set_mode(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_sensor(sd);
	u8 resetval, rdval;
	int ret;

	ret = ov5647_read(sd, OV5647_SW_STANDBY, &rdval);
	if (ret < 0)
		return ret;

	ret = ov5647_write_array(sd, sensor->mode->reg_list,
				 sensor->mode->num_regs);
	if (ret < 0) {
		dev_err(&client->dev, "write sensor default regs error\n");
		return ret;
	}

	ret = ov5647_set_virtual_channel(sd, 0);
	if (ret < 0)
		return ret;

	ret = ov5647_read(sd, OV5647_SW_STANDBY, &resetval);
	if (ret < 0)
		return ret;

	if (!(resetval & 0x01)) {
		dev_err(&client->dev, "Device was in SW standby");
		ret = ov5647_write(sd, OV5647_SW_STANDBY, 0x01);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int ov5647_stream_on(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_sensor(sd);
	u8 val = MIPI_CTRL00_BUS_IDLE;
	int ret;

	ret = ov5647_set_mode(sd);
	if (ret) {
		dev_err(&client->dev, "Failed to program sensor mode: %d\n", ret);
		return ret;
	}

	/* Apply customized values from user when stream starts. */
	ret =  __v4l2_ctrl_handler_setup(sd->ctrl_handler);
	if (ret)
		return ret;

	if (sensor->clock_ncont)
		val |= MIPI_CTRL00_CLOCK_LANE_GATE |
		       MIPI_CTRL00_LINE_SYNC_ENABLE;

	ret = ov5647_write(sd, OV5647_REG_MIPI_CTRL00, val);
	if (ret < 0)
		return ret;

	ret = ov5647_write(sd, OV5647_REG_FRAME_OFF_NUMBER, 0x00);
	if (ret < 0)
		return ret;

	return ov5647_write(sd, OV5640_REG_PAD_OUT, 0x00);
}

static int ov5647_stream_off(struct v4l2_subdev *sd)
{
	int ret;

	ret = ov5647_write(sd, OV5647_REG_MIPI_CTRL00,
			   MIPI_CTRL00_CLOCK_LANE_GATE | MIPI_CTRL00_BUS_IDLE |
			   MIPI_CTRL00_CLOCK_LANE_DISABLE);
	if (ret < 0)
		return ret;

	ret = ov5647_write(sd, OV5647_REG_FRAME_OFF_NUMBER, 0x0f);
	if (ret < 0)
		return ret;

	return ov5647_write(sd, OV5640_REG_PAD_OUT, 0x01);
}

static int ov5647_power_on(struct device *dev)
{
	struct ov5647 *sensor = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "OV5647 power on\n");

	if (sensor->pwdn) {
		gpiod_set_value_cansleep(sensor->pwdn, 0);
		msleep(PWDN_ACTIVE_DELAY_MS);
	}

	ret = clk_prepare_enable(sensor->xclk);
	if (ret < 0) {
		dev_err(dev, "clk prepare enable failed\n");
		goto error_pwdn;
	}

	ret = ov5647_write_array(&sensor->sd, sensor_oe_enable_regs,
				 ARRAY_SIZE(sensor_oe_enable_regs));
	if (ret < 0) {
		dev_err(dev, "write sensor_oe_enable_regs error\n");
		goto error_clk_disable;
	}

	/* Stream off to coax lanes into LP-11 state. */
	ret = ov5647_stream_off(&sensor->sd);
	if (ret < 0) {
		dev_err(dev, "camera not available, check power\n");
		goto error_clk_disable;
	}

	return 0;

error_clk_disable:
	clk_disable_unprepare(sensor->xclk);
error_pwdn:
	gpiod_set_value_cansleep(sensor->pwdn, 1);

	return ret;
}

static int ov5647_power_off(struct device *dev)
{
	struct ov5647 *sensor = dev_get_drvdata(dev);
	u8 rdval;
	int ret;

	dev_dbg(dev, "OV5647 power off\n");

	ret = ov5647_write_array(&sensor->sd, sensor_oe_disable_regs,
				 ARRAY_SIZE(sensor_oe_disable_regs));
	if (ret < 0)
		dev_dbg(dev, "disable oe failed\n");

	/* Enter software standby */
	ret = ov5647_read(&sensor->sd, OV5647_SW_STANDBY, &rdval);
	if (ret < 0)
		dev_dbg(dev, "software standby failed\n");

	rdval &= ~0x01;
	ret = ov5647_write(&sensor->sd, OV5647_SW_STANDBY, rdval);
	if (ret < 0)
		dev_dbg(dev, "software standby failed\n");

	clk_disable_unprepare(sensor->xclk);
	gpiod_set_value_cansleep(sensor->pwdn, 1);

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5647_sensor_get_register(struct v4l2_subdev *sd,
				      struct v4l2_dbg_register *reg)
{
	int ret;
	u8 val;

	ret = ov5647_read(sd, reg->reg & 0xff, &val);
	if (ret < 0)
		return ret;

	reg->val = val;
	reg->size = 1;

	return 0;
}

static int ov5647_sensor_set_register(struct v4l2_subdev *sd,
				      const struct v4l2_dbg_register *reg)
{
	return ov5647_write(sd, reg->reg & 0xff, reg->val & 0xff);
}
#endif

/* Subdev core operations registration */
static const struct v4l2_subdev_core_ops ov5647_subdev_core_ops = {
	.subscribe_event	= v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event	= v4l2_event_subdev_unsubscribe,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register		= ov5647_sensor_get_register,
	.s_register		= ov5647_sensor_set_register,
#endif
};

static const struct v4l2_rect *
__ov5647_get_pad_crop(struct ov5647 *ov5647,
		      struct v4l2_subdev_state *sd_state,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&ov5647->sd, sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &ov5647->mode->crop;
	}

	return NULL;
}

static int ov5647_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_sensor(sd);
	int ret;

	mutex_lock(&sensor->lock);
	if (sensor->streaming == enable) {
		mutex_unlock(&sensor->lock);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto error_unlock;

		ret = ov5647_stream_on(sd);
		if (ret < 0) {
			dev_err(&client->dev, "stream start failed: %d\n", ret);
			goto error_pm;
		}
	} else {
		ret = ov5647_stream_off(sd);
		if (ret < 0) {
			dev_err(&client->dev, "stream stop failed: %d\n", ret);
			goto error_pm;
		}
		pm_runtime_put(&client->dev);
	}

	sensor->streaming = enable;
	mutex_unlock(&sensor->lock);

	return 0;

error_pm:
	pm_runtime_put(&client->dev);
error_unlock:
	mutex_unlock(&sensor->lock);

	return ret;
}

static const struct v4l2_subdev_video_ops ov5647_subdev_video_ops = {
	.s_stream =		ov5647_s_stream,
};

static int ov5647_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov5647_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	const struct v4l2_mbus_framefmt *fmt;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10 ||
	    fse->index >= ARRAY_SIZE(ov5647_modes))
		return -EINVAL;

	fmt = &ov5647_modes[fse->index].format;
	fse->min_width = fmt->width;
	fse->max_width = fmt->width;
	fse->min_height = fmt->height;
	fse->max_height = fmt->height;

	return 0;
}

static int ov5647_get_pad_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	const struct v4l2_mbus_framefmt *sensor_format;
	struct ov5647 *sensor = to_sensor(sd);

	mutex_lock(&sensor->lock);
	switch (format->which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		sensor_format = v4l2_subdev_get_try_format(sd, sd_state,
							   format->pad);
		break;
	default:
		sensor_format = &sensor->mode->format;
		break;
	}

	*fmt = *sensor_format;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int ov5647_set_pad_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct ov5647 *sensor = to_sensor(sd);
	const struct ov5647_mode *mode;

	mode = v4l2_find_nearest_size(ov5647_modes, ARRAY_SIZE(ov5647_modes),
				      format.width, format.height,
				      fmt->width, fmt->height);

	/* Update the sensor mode and apply at it at streamon time. */
	mutex_lock(&sensor->lock);
	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(sd, sd_state, format->pad) = mode->format;
	} else {
		int exposure_max, exposure_def;
		int hblank, vblank;

		sensor->mode = mode;
		__v4l2_ctrl_modify_range(sensor->pixel_rate, mode->pixel_rate,
					 mode->pixel_rate, 1, mode->pixel_rate);

		hblank = mode->hts - mode->format.width;
		__v4l2_ctrl_modify_range(sensor->hblank, hblank, hblank, 1,
					 hblank);

		vblank = mode->vts - mode->format.height;
		__v4l2_ctrl_modify_range(sensor->vblank, OV5647_VBLANK_MIN,
					 OV5647_VTS_MAX - mode->format.height,
					 1, vblank);
		__v4l2_ctrl_s_ctrl(sensor->vblank, vblank);

		exposure_max = mode->vts - 4;
		exposure_def = min(exposure_max, OV5647_EXPOSURE_DEFAULT);
		__v4l2_ctrl_modify_range(sensor->exposure,
					 sensor->exposure->minimum,
					 exposure_max, sensor->exposure->step,
					 exposure_def);
	}
	*fmt = mode->format;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int ov5647_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		struct ov5647 *sensor = to_sensor(sd);

		mutex_lock(&sensor->lock);
		sel->r = *__ov5647_get_pad_crop(sensor, sd_state, sel->pad,
						sel->which);
		mutex_unlock(&sensor->lock);

		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = OV5647_NATIVE_WIDTH;
		sel->r.height = OV5647_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = OV5647_PIXEL_ARRAY_TOP;
		sel->r.left = OV5647_PIXEL_ARRAY_LEFT;
		sel->r.width = OV5647_PIXEL_ARRAY_WIDTH;
		sel->r.height = OV5647_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_subdev_pad_ops ov5647_subdev_pad_ops = {
	.enum_mbus_code		= ov5647_enum_mbus_code,
	.enum_frame_size	= ov5647_enum_frame_size,
	.set_fmt		= ov5647_set_pad_fmt,
	.get_fmt		= ov5647_get_pad_fmt,
	.get_selection		= ov5647_get_selection,
};

static const struct v4l2_subdev_ops ov5647_subdev_ops = {
	.core		= &ov5647_subdev_core_ops,
	.video		= &ov5647_subdev_video_ops,
	.pad		= &ov5647_subdev_pad_ops,
};

static int ov5647_detect(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 read;
	int ret;

	ret = ov5647_write(sd, OV5647_SW_RESET, 0x01);
	if (ret < 0)
		return ret;

	ret = ov5647_read(sd, OV5647_REG_CHIPID_H, &read);
	if (ret < 0)
		return ret;

	if (read != 0x56) {
		dev_err(&client->dev, "ID High expected 0x56 got %x", read);
		return -ENODEV;
	}

	ret = ov5647_read(sd, OV5647_REG_CHIPID_L, &read);
	if (ret < 0)
		return ret;

	if (read != 0x47) {
		dev_err(&client->dev, "ID Low expected 0x47 got %x", read);
		return -ENODEV;
	}

	return ov5647_write(sd, OV5647_SW_RESET, 0x00);
}

static int ov5647_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format =
				v4l2_subdev_get_try_format(sd, fh->state, 0);
	struct v4l2_rect *crop = v4l2_subdev_get_try_crop(sd, fh->state, 0);

	crop->left = OV5647_PIXEL_ARRAY_LEFT;
	crop->top = OV5647_PIXEL_ARRAY_TOP;
	crop->width = OV5647_PIXEL_ARRAY_WIDTH;
	crop->height = OV5647_PIXEL_ARRAY_HEIGHT;

	*format = OV5647_DEFAULT_FORMAT;

	return 0;
}

static const struct v4l2_subdev_internal_ops ov5647_subdev_internal_ops = {
	.open = ov5647_open,
};

static int ov5647_s_auto_white_balance(struct v4l2_subdev *sd, u32 val)
{
	return ov5647_write(sd, OV5647_REG_AWB, val ? 1 : 0);
}

static int ov5647_s_autogain(struct v4l2_subdev *sd, u32 val)
{
	int ret;
	u8 reg;

	/* Non-zero turns on AGC by clearing bit 1.*/
	ret = ov5647_read(sd, OV5647_REG_AEC_AGC, &reg);
	if (ret)
		return ret;

	return ov5647_write(sd, OV5647_REG_AEC_AGC, val ? reg & ~BIT(1)
							: reg | BIT(1));
}

static int ov5647_s_exposure_auto(struct v4l2_subdev *sd, u32 val)
{
	int ret;
	u8 reg;

	/*
	 * Everything except V4L2_EXPOSURE_MANUAL turns on AEC by
	 * clearing bit 0.
	 */
	ret = ov5647_read(sd, OV5647_REG_AEC_AGC, &reg);
	if (ret)
		return ret;

	return ov5647_write(sd, OV5647_REG_AEC_AGC,
			    val == V4L2_EXPOSURE_MANUAL ? reg | BIT(0)
							: reg & ~BIT(0));
}

static int ov5647_s_analogue_gain(struct v4l2_subdev *sd, u32 val)
{
	int ret;

	/* 10 bits of gain, 2 in the high register. */
	ret = ov5647_write(sd, OV5647_REG_GAIN_HI, (val >> 8) & 3);
	if (ret)
		return ret;

	return ov5647_write(sd, OV5647_REG_GAIN_LO, val & 0xff);
}

static int ov5647_s_exposure(struct v4l2_subdev *sd, u32 val)
{
	int ret;

	/*
	 * Sensor has 20 bits, but the bottom 4 bits are fractions of a line
	 * which we leave as zero (and don't receive in "val").
	 */
	ret = ov5647_write(sd, OV5647_REG_EXP_HI, (val >> 12) & 0xf);
	if (ret)
		return ret;

	ret = ov5647_write(sd, OV5647_REG_EXP_MID, (val >> 4) & 0xff);
	if (ret)
		return ret;

	return ov5647_write(sd, OV5647_REG_EXP_LO, (val & 0xf) << 4);
}

static int ov5647_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov5647 *sensor = container_of(ctrl->handler,
					    struct ov5647, ctrls);
	struct v4l2_subdev *sd = &sensor->sd;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;


	/* v4l2_ctrl_lock() locks our own mutex */

	if (ctrl->id == V4L2_CID_VBLANK) {
		int exposure_max, exposure_def;

		/* Update max exposure while meeting expected vblanking */
		exposure_max = sensor->mode->format.height + ctrl->val - 4;
		exposure_def = min(exposure_max, OV5647_EXPOSURE_DEFAULT);
		__v4l2_ctrl_modify_range(sensor->exposure,
					 sensor->exposure->minimum,
					 exposure_max, sensor->exposure->step,
					 exposure_def);
	}

	/*
	 * If the device is not powered up do not apply any controls
	 * to H/W at this time. Instead the controls will be restored
	 * at s_stream(1) time.
	 */
	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = ov5647_s_auto_white_balance(sd, ctrl->val);
		break;
	case V4L2_CID_AUTOGAIN:
		ret = ov5647_s_autogain(sd, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ret = ov5647_s_exposure_auto(sd, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret =  ov5647_s_analogue_gain(sd, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = ov5647_s_exposure(sd, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = ov5647_write16(sd, OV5647_REG_VTS_HI,
				     sensor->mode->format.height + ctrl->val);
		break;

	/* Read-only, but we adjust it based on mode. */
	case V4L2_CID_PIXEL_RATE:
	case V4L2_CID_HBLANK:
		/* Read-only, but we adjust it based on mode. */
		break;

	default:
		dev_info(&client->dev,
			 "Control (id:0x%x, val:0x%x) not supported\n",
			 ctrl->id, ctrl->val);
		return -EINVAL;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov5647_ctrl_ops = {
	.s_ctrl = ov5647_s_ctrl,
};

static int ov5647_init_controls(struct ov5647 *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->sd);
	int hblank, exposure_max, exposure_def;

	v4l2_ctrl_handler_init(&sensor->ctrls, 8);

	v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
			  V4L2_CID_AUTOGAIN, 0, 1, 1, 0);

	v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
			  V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, 0);

	v4l2_ctrl_new_std_menu(&sensor->ctrls, &ov5647_ctrl_ops,
			       V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL,
			       0, V4L2_EXPOSURE_MANUAL);

	exposure_max = sensor->mode->vts - 4;
	exposure_def = min(exposure_max, OV5647_EXPOSURE_DEFAULT);
	sensor->exposure = v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     OV5647_EXPOSURE_MIN,
					     exposure_max, OV5647_EXPOSURE_STEP,
					     exposure_def);

	/* min: 16 = 1.0x; max (10 bits); default: 32 = 2.0x. */
	v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, 16, 1023, 1, 32);

	/* By default, PIXEL_RATE is read only, but it does change per mode */
	sensor->pixel_rate = v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       sensor->mode->pixel_rate,
					       sensor->mode->pixel_rate, 1,
					       sensor->mode->pixel_rate);

	/* By default, HBLANK is read only, but it does change per mode. */
	hblank = sensor->mode->hts - sensor->mode->format.width;
	sensor->hblank = v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
					   V4L2_CID_HBLANK, hblank, hblank, 1,
					   hblank);

	sensor->vblank = v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
					   V4L2_CID_VBLANK, OV5647_VBLANK_MIN,
					   OV5647_VTS_MAX -
					   sensor->mode->format.height, 1,
					   sensor->mode->vts -
					   sensor->mode->format.height);

	if (sensor->ctrls.error)
		goto handler_free;

	sensor->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	sensor->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	sensor->sd.ctrl_handler = &sensor->ctrls;

	return 0;

handler_free:
	dev_err(&client->dev, "%s Controls initialization failed (%d)\n",
		__func__, sensor->ctrls.error);
	v4l2_ctrl_handler_free(&sensor->ctrls);

	return sensor->ctrls.error;
}

static int ov5647_parse_dt(struct ov5647 *sensor, struct device_node *np)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct device_node *ep;
	int ret;

	ep = of_graph_get_next_endpoint(np, NULL);
	if (!ep)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &bus_cfg);
	if (ret)
		goto out;

	sensor->clock_ncont = bus_cfg.bus.mipi_csi2.flags &
			      V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;

out:
	of_node_put(ep);

	return ret;
}

static int ov5647_probe(struct i2c_client *client)
{
	struct device_node *np = client->dev.of_node;
	struct device *dev = &client->dev;
	struct ov5647 *sensor;
	struct v4l2_subdev *sd;
	u32 xclk_freq;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_OF) && np) {
		ret = ov5647_parse_dt(sensor, np);
		if (ret) {
			dev_err(dev, "DT parsing error: %d\n", ret);
			return ret;
		}
	}

	sensor->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(sensor->xclk)) {
		dev_err(dev, "could not get xclk");
		return PTR_ERR(sensor->xclk);
	}

	xclk_freq = clk_get_rate(sensor->xclk);
	if (xclk_freq != 25000000) {
		dev_err(dev, "Unsupported clock frequency: %u\n", xclk_freq);
		return -EINVAL;
	}

	/* Request the power down GPIO asserted. */
	sensor->pwdn = devm_gpiod_get_optional(dev, "pwdn", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->pwdn)) {
		dev_err(dev, "Failed to get 'pwdn' gpio\n");
		return -EINVAL;
	}

	mutex_init(&sensor->lock);

	sensor->mode = OV5647_DEFAULT_MODE;

	ret = ov5647_init_controls(sensor);
	if (ret)
		goto mutex_destroy;

	sd = &sensor->sd;
	v4l2_i2c_subdev_init(sd, client, &ov5647_subdev_ops);
	sd->internal_ops = &ov5647_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sensor->pad);
	if (ret < 0)
		goto ctrl_handler_free;

	ret = ov5647_power_on(dev);
	if (ret)
		goto entity_cleanup;

	ret = ov5647_detect(sd);
	if (ret < 0)
		goto power_off;

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0)
		goto power_off;

	/* Enable runtime PM and turn off the device */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	dev_dbg(dev, "OmniVision OV5647 camera driver probed\n");

	return 0;

power_off:
	ov5647_power_off(dev);
entity_cleanup:
	media_entity_cleanup(&sd->entity);
ctrl_handler_free:
	v4l2_ctrl_handler_free(&sensor->ctrls);
mutex_destroy:
	mutex_destroy(&sensor->lock);

	return ret;
}

static int ov5647_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5647 *sensor = to_sensor(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls);
	v4l2_device_unregister_subdev(sd);
	pm_runtime_disable(&client->dev);
	mutex_destroy(&sensor->lock);

	return 0;
}

static const struct dev_pm_ops ov5647_pm_ops = {
	SET_RUNTIME_PM_OPS(ov5647_power_off, ov5647_power_on, NULL)
};

static const struct i2c_device_id ov5647_id[] = {
	{ "ov5647", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, ov5647_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov5647_of_match[] = {
	{ .compatible = "ovti,ov5647" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ov5647_of_match);
#endif

static struct i2c_driver ov5647_driver = {
	.driver = {
		.of_match_table = of_match_ptr(ov5647_of_match),
		.name	= "ov5647",
		.pm	= &ov5647_pm_ops,
	},
	.probe_new	= ov5647_probe,
	.remove		= ov5647_remove,
	.id_table	= ov5647_id,
};

module_i2c_driver(ov5647_driver);

MODULE_AUTHOR("Ramiro Oliveira <roliveir@synopsys.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision ov5647 sensors");
MODULE_LICENSE("GPL v2");
