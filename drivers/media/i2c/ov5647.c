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
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-cci.h>
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

#define OV5647_SW_STANDBY		CCI_REG8(0x0100)
#define OV5647_SW_RESET			CCI_REG8(0x0103)
#define OV5647_REG_CHIPID		CCI_REG16(0x300a)
#define OV5640_REG_PAD_OUT		CCI_REG8(0x300d)
#define OV5647_REG_EXPOSURE		CCI_REG24(0x3500)
#define OV5647_REG_AEC_AGC		CCI_REG8(0x3503)
#define OV5647_REG_GAIN			CCI_REG16(0x350a)
#define OV5647_REG_HTS			CCI_REG16(0x380c)
#define OV5647_REG_VTS			CCI_REG16(0x380e)
#define OV5647_REG_TIMING_TC_V		CCI_REG8(0x3820)
#define OV5647_REG_TIMING_TC_H		CCI_REG8(0x3821)
#define OV5647_REG_FRAME_OFF_NUMBER	CCI_REG8(0x4202)
#define OV5647_REG_MIPI_CTRL00		CCI_REG8(0x4800)
#define OV5647_REG_MIPI_CTRL14		CCI_REG8(0x4814)
#define OV5647_REG_MIPI_CTRL14_CHANNEL_MASK	GENMASK(7, 6)
#define OV5647_REG_MIPI_CTRL14_CHANNEL_SHIFT	6
#define OV5647_REG_AWB			CCI_REG8(0x5001)
#define OV5647_REG_ISPCTRL3D		CCI_REG8(0x503d)

#define OV5647_CHIP_ID 0x5647
#define REG_TERM 0xfffe
#define VAL_TERM 0xfe
#define REG_DLY  0xffff

/* OV5647 native and active pixel array size */
#define OV5647_NATIVE_WIDTH		2624U
#define OV5647_NATIVE_HEIGHT		1956U

#define OV5647_PIXEL_ARRAY_LEFT		16U
#define OV5647_PIXEL_ARRAY_TOP		6U
#define OV5647_PIXEL_ARRAY_WIDTH	2592U
#define OV5647_PIXEL_ARRAY_HEIGHT	1944U

#define OV5647_VBLANK_MIN		24
#define OV5647_VTS_MAX			32767

#define OV5647_HTS_MAX			0x1fff

#define OV5647_EXPOSURE_MIN		4
#define OV5647_EXPOSURE_STEP		1
#define OV5647_EXPOSURE_DEFAULT		1000
#define OV5647_EXPOSURE_MAX		65535

/* regulator supplies */
static const char * const ov5647_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV5647_NUM_SUPPLIES ARRAY_SIZE(ov5647_supply_names)

#define FREQ_INDEX_FULL		0
#define FREQ_INDEX_VGA		1
static const s64 ov5647_link_freqs[] = {
	[FREQ_INDEX_FULL]	= 218750000,
	[FREQ_INDEX_VGA]	= 145833300,
};

struct ov5647_mode {
	struct v4l2_mbus_framefmt	format;
	struct v4l2_rect		crop;
	u64				pixel_rate;
	unsigned int			link_freq_index;
	int				hts;
	int				vts;
	const struct reg_sequence	*reg_list;
	unsigned int			num_regs;
};

struct ov5647 {
	struct v4l2_subdev		sd;
	struct regmap			*regmap;
	struct media_pad		pad;
	struct clk			*xclk;
	struct gpio_desc		*pwdn;
	struct regulator_bulk_data	supplies[OV5647_NUM_SUPPLIES];
	bool				clock_ncont;
	struct v4l2_ctrl_handler	ctrls;
	const struct ov5647_mode	*mode;
	struct v4l2_ctrl		*pixel_rate;
	struct v4l2_ctrl		*hblank;
	struct v4l2_ctrl		*vblank;
	struct v4l2_ctrl		*exposure;
	struct v4l2_ctrl		*hflip;
	struct v4l2_ctrl		*vflip;
	struct v4l2_ctrl		*link_freq;
};

static inline struct ov5647 *to_sensor(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov5647, sd);
}

static const char * const ov5647_test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Color Squares",
	"Random Data",
};

static const u8 ov5647_test_pattern_val[] = {
	0x00,	/* Disabled */
	0x80,	/* Color Bars */
	0x82,	/* Color Squares */
	0x81,	/* Random Data */
};

static const struct reg_sequence sensor_oe_disable_regs[] = {
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
};

static const struct reg_sequence sensor_oe_enable_regs[] = {
	{0x3000, 0x0f},
	{0x3001, 0xff},
	{0x3002, 0xe4},
};

static const struct reg_sequence ov5647_common_regs[] = {
	{0x0100, 0x00},
	{0x0103, 0x01},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x303c, 0x11},
	{0x3106, 0xf5},
	{0x3827, 0xec},
	{0x370c, 0x03},
	{0x5000, 0x06},
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
	{0x3a0f, 0x58},
	{0x3a10, 0x50},
	{0x3a1b, 0x58},
	{0x3a1e, 0x50},
	{0x3a11, 0x60},
	{0x3a1f, 0x28},
	{0x4001, 0x02},
	{0x4000, 0x09},
	{0x3503, 0x03},
};

static const struct reg_sequence ov5647_2592x1944_10bpp[] = {
	{0x3036, 0x69},
	{0x3821, 0x02},
	{0x3820, 0x00},
	{0x3612, 0x5b},
	{0x3618, 0x04},
	{0x5002, 0x41},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3708, 0x64},
	{0x3709, 0x12},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xa3},
	{0x3808, 0x0a},
	{0x3809, 0x20},
	{0x380a, 0x07},
	{0x380b, 0x98},
	{0x3811, 0x10},
	{0x3813, 0x06},
	{0x3a09, 0x28},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0d, 0x08},
	{0x3a0e, 0x06},
	{0x4004, 0x04},
	{0x4837, 0x19},
	{0x4800, 0x24},
	{0x0100, 0x01},
};

static const struct reg_sequence ov5647_1080p30_10bpp[] = {
	{0x3036, 0x69},
	{0x3821, 0x02},
	{0x3820, 0x00},
	{0x3612, 0x5b},
	{0x3618, 0x04},
	{0x5002, 0x41},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3708, 0x64},
	{0x3709, 0x12},
	{0x3800, 0x01},
	{0x3801, 0x5c},
	{0x3802, 0x01},
	{0x3803, 0xb2},
	{0x3804, 0x08},
	{0x3805, 0xe3},
	{0x3806, 0x05},
	{0x3807, 0xf1},
	{0x3808, 0x07},
	{0x3809, 0x80},
	{0x380a, 0x04},
	{0x380b, 0x38},
	{0x3811, 0x04},
	{0x3813, 0x02},
	{0x3a09, 0x4b},
	{0x3a0a, 0x01},
	{0x3a0b, 0x13},
	{0x3a0d, 0x04},
	{0x3a0e, 0x03},
	{0x4004, 0x04},
	{0x4837, 0x19},
	{0x4800, 0x34},
	{0x0100, 0x01},
};

static const struct reg_sequence ov5647_2x2binned_10bpp[] = {
	{0x3036, 0x69},
	{0x3821, 0x03},
	{0x3820, 0x41},
	{0x3612, 0x59},
	{0x3618, 0x00},
	{0x5002, 0x41},
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
	{0x3811, 0x0c},
	{0x3813, 0x06},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3a09, 0x28},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0d, 0x08},
	{0x3a0e, 0x06},
	{0x4004, 0x04},
	{0x4837, 0x16},
	{0x4800, 0x24},
	{0x350a, 0x00},
	{0x350b, 0x10},
	{0x3500, 0x00},
	{0x3501, 0x1a},
	{0x3502, 0xf0},
	{0x3212, 0xa0},
	{0x0100, 0x01},
};

static const struct reg_sequence ov5647_640x480_10bpp[] = {
	{0x3036, 0x46},
	{0x3821, 0x03},
	{0x3820, 0x41},
	{0x3612, 0x59},
	{0x3618, 0x00},
	{0x3814, 0x35},
	{0x3815, 0x35},
	{0x3708, 0x64},
	{0x3709, 0x52},
	{0x3800, 0x00},
	{0x3801, 0x10},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x2f},
	{0x3806, 0x07},
	{0x3807, 0x9f},
	{0x3808, 0x02},
	{0x3809, 0x80},
	{0x380a, 0x01},
	{0x380b, 0xe0},
	{0x3a09, 0x2e},
	{0x3a0a, 0x00},
	{0x3a0b, 0xfb},
	{0x3a0d, 0x02},
	{0x3a0e, 0x01},
	{0x4004, 0x02},
	{0x4800, 0x34},
	{0x0100, 0x01},
};

static const struct ov5647_mode ov5647_modes[] = {
	/* 2592x1944 full resolution full FOV 10-bit mode. */
	{
		.format = {
			.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
			.colorspace	= V4L2_COLORSPACE_RAW,
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
		.link_freq_index = FREQ_INDEX_FULL,
		.hts		= 2844,
		.vts		= 0x7b0,
		.reg_list	= ov5647_2592x1944_10bpp,
		.num_regs	= ARRAY_SIZE(ov5647_2592x1944_10bpp)
	},
	/* 1080p30 10-bit mode. Full resolution centre-cropped down to 1080p. */
	{
		.format = {
			.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
			.colorspace	= V4L2_COLORSPACE_RAW,
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
		.pixel_rate	= 87500000,
		.link_freq_index = FREQ_INDEX_FULL,
		.hts		= 2416,
		.vts		= 0x450,
		.reg_list	= ov5647_1080p30_10bpp,
		.num_regs	= ARRAY_SIZE(ov5647_1080p30_10bpp)
	},
	/* 2x2 binned full FOV 10-bit mode. */
	{
		.format = {
			.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
			.colorspace	= V4L2_COLORSPACE_RAW,
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
		.pixel_rate	= 87500000,
		.link_freq_index = FREQ_INDEX_FULL,
		.hts		= 1896,
		.vts		= 0x59b,
		.reg_list	= ov5647_2x2binned_10bpp,
		.num_regs	= ARRAY_SIZE(ov5647_2x2binned_10bpp)
	},
	/* 10-bit VGA full FOV 60fps. 2x2 binned and subsampled down to VGA. */
	{
		.format = {
			.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
			.colorspace	= V4L2_COLORSPACE_RAW,
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
		.pixel_rate	= 58333000,
		.link_freq_index = FREQ_INDEX_VGA,
		.hts		= 1852,
		.vts		= 0x1f8,
		.reg_list	= ov5647_640x480_10bpp,
		.num_regs	= ARRAY_SIZE(ov5647_640x480_10bpp)
	},
};

/* Default sensor mode is 2x2 binned 640x480 SBGGR10_1X10. */
#define OV5647_DEFAULT_MODE	(&ov5647_modes[3])
#define OV5647_DEFAULT_FORMAT	(ov5647_modes[3].format)

static int ov5647_set_virtual_channel(struct v4l2_subdev *sd, int channel)
{
	struct ov5647 *sensor = to_sensor(sd);

	return cci_update_bits(sensor->regmap, OV5647_REG_MIPI_CTRL14,
			       OV5647_REG_MIPI_CTRL14_CHANNEL_MASK,
			       channel << OV5647_REG_MIPI_CTRL14_CHANNEL_SHIFT,
			       NULL);
}

static int ov5647_set_mode(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_sensor(sd);
	u64 resetval, rdval;
	int ret;

	ret = cci_read(sensor->regmap, OV5647_SW_STANDBY, &rdval, NULL);
	if (ret < 0)
		return ret;

	ret = regmap_multi_reg_write(sensor->regmap, ov5647_common_regs,
				     ARRAY_SIZE(ov5647_common_regs));
	if (ret < 0) {
		dev_err(&client->dev, "write sensor common regs error\n");
		return ret;
	}

	ret = regmap_multi_reg_write(sensor->regmap, sensor->mode->reg_list,
				     sensor->mode->num_regs);
	if (ret < 0) {
		dev_err(&client->dev, "write sensor default regs error\n");
		return ret;
	}

	ret = ov5647_set_virtual_channel(sd, 0);
	if (ret < 0)
		return ret;

	ret = cci_read(sensor->regmap, OV5647_SW_STANDBY, &resetval, NULL);
	if (ret < 0)
		return ret;

	if (!(resetval & 0x01)) {
		dev_err(&client->dev, "Device was in SW standby");
		ret = cci_write(sensor->regmap, OV5647_SW_STANDBY, 0x01, NULL);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int ov5647_stream_stop(struct ov5647 *sensor)
{
	int ret = 0;

	cci_write(sensor->regmap, OV5647_REG_MIPI_CTRL00,
		  MIPI_CTRL00_CLOCK_LANE_GATE | MIPI_CTRL00_BUS_IDLE |
		  MIPI_CTRL00_CLOCK_LANE_DISABLE, &ret);
	cci_write(sensor->regmap, OV5647_REG_FRAME_OFF_NUMBER, 0x0f, &ret);
	cci_write(sensor->regmap, OV5640_REG_PAD_OUT, 0x01, &ret);

	return ret;
}

static int ov5647_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_sensor(sd);
	u8 val = MIPI_CTRL00_BUS_IDLE;
	int ret;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret < 0)
		return ret;

	ret = ov5647_set_mode(sd);
	if (ret) {
		dev_err(&client->dev, "Failed to program sensor mode: %d\n", ret);
		goto done;
	}

	/* Apply customized values from user when stream starts. */
	ret =  __v4l2_ctrl_handler_setup(sd->ctrl_handler);
	if (ret)
		goto done;

	if (sensor->clock_ncont)
		val |= MIPI_CTRL00_CLOCK_LANE_GATE |
		       MIPI_CTRL00_LINE_SYNC_ENABLE;

	cci_write(sensor->regmap, OV5647_REG_MIPI_CTRL00, val, &ret);
	cci_write(sensor->regmap, OV5647_REG_FRAME_OFF_NUMBER, 0x00, &ret);
	cci_write(sensor->regmap, OV5640_REG_PAD_OUT, 0x00, &ret);

done:
	if (ret)
		pm_runtime_put(&client->dev);

	return ret;
}

static int ov5647_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_sensor(sd);
	int ret;

	ret = ov5647_stream_stop(sensor);

	pm_runtime_put(&client->dev);

	return ret;
}

static int ov5647_power_on(struct device *dev)
{
	struct ov5647 *sensor = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "OV5647 power on\n");

	ret = regulator_bulk_enable(OV5647_NUM_SUPPLIES, sensor->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ret = gpiod_set_value_cansleep(sensor->pwdn, 0);
	if (ret < 0) {
		dev_err(dev, "pwdn gpio set value failed: %d\n", ret);
		goto error_reg_disable;
	}

	msleep(PWDN_ACTIVE_DELAY_MS);

	ret = clk_prepare_enable(sensor->xclk);
	if (ret < 0) {
		dev_err(dev, "clk prepare enable failed\n");
		goto error_pwdn;
	}

	ret = regmap_multi_reg_write(sensor->regmap, sensor_oe_enable_regs,
				     ARRAY_SIZE(sensor_oe_enable_regs));
	if (ret < 0) {
		dev_err(dev, "write sensor_oe_enable_regs error\n");
		goto error_clk_disable;
	}

	/* Stream off to coax lanes into LP-11 state. */
	ret = ov5647_stream_stop(sensor);
	if (ret < 0) {
		dev_err(dev, "camera not available, check power\n");
		goto error_clk_disable;
	}

	return 0;

error_clk_disable:
	clk_disable_unprepare(sensor->xclk);
error_pwdn:
	gpiod_set_value_cansleep(sensor->pwdn, 1);
error_reg_disable:
	regulator_bulk_disable(OV5647_NUM_SUPPLIES, sensor->supplies);

	return ret;
}

static int ov5647_power_off(struct device *dev)
{
	struct ov5647 *sensor = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "OV5647 power off\n");

	ret = regmap_multi_reg_write(sensor->regmap, sensor_oe_disable_regs,
				     ARRAY_SIZE(sensor_oe_disable_regs));
	if (ret < 0)
		dev_dbg(dev, "disable oe failed\n");

	/* Enter software standby */
	ret = cci_update_bits(sensor->regmap, OV5647_SW_STANDBY, 0x01, 0x00, NULL);
	if (ret < 0)
		dev_dbg(dev, "software standby failed\n");

	clk_disable_unprepare(sensor->xclk);
	gpiod_set_value_cansleep(sensor->pwdn, 1);
	regulator_bulk_disable(OV5647_NUM_SUPPLIES, sensor->supplies);

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5647_sensor_get_register(struct v4l2_subdev *sd,
				      struct v4l2_dbg_register *reg)
{
	struct ov5647 *sensor = to_sensor(sd);
	int ret;
	u64 val;

	ret = cci_read(sensor->regmap, reg->reg & 0xff, &val, NULL);
	if (ret < 0)
		return ret;

	reg->val = val;
	reg->size = 1;

	return 0;
}

static int ov5647_sensor_set_register(struct v4l2_subdev *sd,
				      const struct v4l2_dbg_register *reg)
{
	struct ov5647 *sensor = to_sensor(sd);

	return cci_write(sensor->regmap, reg->reg & 0xff, reg->val & 0xff, NULL);
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
		return v4l2_subdev_state_get_crop(sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &ov5647->mode->crop;
	}

	return NULL;
}

static const struct v4l2_subdev_video_ops ov5647_subdev_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

/*
 * This function returns the mbus code for the current settings of the HFLIP
 * and VFLIP controls.
 */
static u32 ov5647_get_mbus_code(struct v4l2_subdev *sd)
{
	struct ov5647 *sensor = to_sensor(sd);
	/* The control values are only 0 or 1. */
	int index =  sensor->hflip->val | (sensor->vflip->val << 1);

	static const u32 codes[4] = {
		MEDIA_BUS_FMT_SGBRG10_1X10,
		MEDIA_BUS_FMT_SBGGR10_1X10,
		MEDIA_BUS_FMT_SRGGB10_1X10,
		MEDIA_BUS_FMT_SGRBG10_1X10
	};

	return codes[index];
}

static int ov5647_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = ov5647_get_mbus_code(sd);

	return 0;
}

static int ov5647_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	const struct v4l2_mbus_framefmt *fmt;

	if (fse->code != ov5647_get_mbus_code(sd) ||
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

	switch (format->which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		sensor_format = v4l2_subdev_state_get_format(sd_state,
							     format->pad);
		break;
	default:
		sensor_format = &sensor->mode->format;
		break;
	}

	*fmt = *sensor_format;
	/* The code we pass back must reflect the current h/vflips. */
	fmt->code = ov5647_get_mbus_code(sd);

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
	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(sd_state, format->pad) = mode->format;
	} else {
		int exposure_max, exposure_def;
		int hblank, vblank;

		sensor->mode = mode;
		__v4l2_ctrl_modify_range(sensor->pixel_rate, mode->pixel_rate,
					 mode->pixel_rate, 1, mode->pixel_rate);

		hblank = mode->hts - mode->format.width;
		__v4l2_ctrl_modify_range(sensor->hblank, hblank,
					 OV5647_HTS_MAX - mode->format.width, 1,
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

		__v4l2_ctrl_s_ctrl(sensor->link_freq, mode->link_freq_index);
	}
	*fmt = mode->format;
	/* The code we pass back must reflect the current h/vflips. */
	fmt->code = ov5647_get_mbus_code(sd);

	return 0;
}

static int ov5647_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		struct ov5647 *sensor = to_sensor(sd);

		sel->r = *__ov5647_get_pad_crop(sensor, sd_state, sel->pad,
						sel->which);

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
	.enable_streams		= ov5647_enable_streams,
	.disable_streams	= ov5647_disable_streams,
};

static const struct v4l2_subdev_ops ov5647_subdev_ops = {
	.core		= &ov5647_subdev_core_ops,
	.video		= &ov5647_subdev_video_ops,
	.pad		= &ov5647_subdev_pad_ops,
};

static int ov5647_detect(struct v4l2_subdev *sd)
{
	struct ov5647 *sensor = to_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u64 read;
	int ret;

	ret = cci_write(sensor->regmap, OV5647_SW_RESET, 0x01, NULL);
	if (ret < 0)
		return ret;

	ret = cci_read(sensor->regmap, OV5647_REG_CHIPID, &read, NULL);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret,
				     "failed to read chip id %x\n",
				     OV5647_REG_CHIPID);

	if (read != OV5647_CHIP_ID) {
		dev_err(&client->dev, "Chip ID expected 0x5647 got 0x%llx", read);
		return -ENODEV;
	}

	return cci_write(sensor->regmap, OV5647_SW_RESET, 0x00, NULL);
}

static int ov5647_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format =
				v4l2_subdev_state_get_format(fh->state, 0);
	struct v4l2_rect *crop = v4l2_subdev_state_get_crop(fh->state, 0);

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

static int ov5647_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov5647 *sensor = container_of(ctrl->handler,
					    struct ov5647, ctrls);
	struct v4l2_subdev *sd = &sensor->sd;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

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
		ret = cci_write(sensor->regmap, OV5647_REG_AWB,
				ctrl->val ? 1 : 0, NULL);
		break;
	case V4L2_CID_AUTOGAIN:
		/* Non-zero turns on AGC by clearing bit 1.*/
		return cci_update_bits(sensor->regmap, OV5647_REG_AEC_AGC, BIT(1),
				       ctrl->val ? 0 : BIT(1), NULL);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		/*
		 * Everything except V4L2_EXPOSURE_MANUAL turns on AEC by
		 * clearing bit 0.
		 */
		return cci_update_bits(sensor->regmap, OV5647_REG_AEC_AGC, BIT(0),
				       ctrl->val == V4L2_EXPOSURE_MANUAL ? BIT(0) : 0, NULL);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		/* 10 bits of gain, 2 in the high register. */
		return cci_write(sensor->regmap, OV5647_REG_GAIN,
				 ctrl->val & 0x3ff, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		/*
		 * Sensor has 20 bits, but the bottom 4 bits are fractions of a line
		 * which we leave as zero (and don't receive in "val").
		 */
		ret = cci_write(sensor->regmap, OV5647_REG_EXPOSURE,
				ctrl->val << 4, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(sensor->regmap, OV5647_REG_VTS,
				sensor->mode->format.height + ctrl->val, NULL);
		break;
	case V4L2_CID_HBLANK:
		ret = cci_write(sensor->regmap, OV5647_REG_HTS,
				sensor->mode->format.width + ctrl->val, &ret);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(sensor->regmap, OV5647_REG_ISPCTRL3D,
				ov5647_test_pattern_val[ctrl->val], NULL);
		break;
	case V4L2_CID_HFLIP:
		/* There's an in-built hflip in the sensor, so account for that here. */
		ret = cci_update_bits(sensor->regmap, OV5647_REG_TIMING_TC_H, BIT(1),
				      ctrl->val ? 0 : BIT(1), NULL);
		break;
	case V4L2_CID_VFLIP:
		ret = cci_update_bits(sensor->regmap, OV5647_REG_TIMING_TC_V, BIT(1),
				      ctrl->val ? BIT(1) : 0, NULL);
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

static int ov5647_configure_regulators(struct device *dev,
				       struct ov5647 *sensor)
{
	for (unsigned int i = 0; i < OV5647_NUM_SUPPLIES; i++)
		sensor->supplies[i].supply = ov5647_supply_names[i];

	return devm_regulator_bulk_get(dev, OV5647_NUM_SUPPLIES,
				       sensor->supplies);
}

static int ov5647_init_controls(struct ov5647 *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->sd);
	struct v4l2_fwnode_device_properties props;
	int hblank, exposure_max, exposure_def;
	struct device *dev = &client->dev;

	v4l2_ctrl_handler_init(&sensor->ctrls, 14);

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
	sensor->pixel_rate = v4l2_ctrl_new_std(&sensor->ctrls, NULL,
					       V4L2_CID_PIXEL_RATE,
					       sensor->mode->pixel_rate,
					       sensor->mode->pixel_rate, 1,
					       sensor->mode->pixel_rate);

	hblank = sensor->mode->hts - sensor->mode->format.width;
	sensor->hblank = v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
					   V4L2_CID_HBLANK, hblank,
					   OV5647_HTS_MAX -
					   sensor->mode->format.width, 1,
					   hblank);

	sensor->vblank = v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
					   V4L2_CID_VBLANK, OV5647_VBLANK_MIN,
					   OV5647_VTS_MAX -
					   sensor->mode->format.height, 1,
					   sensor->mode->vts -
					   sensor->mode->format.height);

	v4l2_ctrl_new_std_menu_items(&sensor->ctrls, &ov5647_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov5647_test_pattern_menu) - 1,
				     0, 0, ov5647_test_pattern_menu);

	sensor->hflip = v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);

	sensor->vflip = v4l2_ctrl_new_std(&sensor->ctrls, &ov5647_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);

	sensor->link_freq =
		v4l2_ctrl_new_int_menu(&sensor->ctrls, NULL, V4L2_CID_LINK_FREQ,
				       ARRAY_SIZE(ov5647_link_freqs) - 1,
				       sensor->mode->link_freq_index,
				       ov5647_link_freqs);
	if (sensor->link_freq)
		sensor->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_fwnode_device_parse(dev, &props);

	v4l2_ctrl_new_fwnode_properties(&sensor->ctrls, &ov5647_ctrl_ops,
					&props);

	if (sensor->ctrls.error)
		goto handler_free;

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
	struct device_node *ep __free(device_node) =
		of_graph_get_endpoint_by_regs(np, 0, -1);
	int ret;

	if (!ep)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &bus_cfg);
	if (ret)
		return ret;

	sensor->clock_ncont = bus_cfg.bus.mipi_csi2.flags &
			      V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;

	return 0;
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

	sensor->xclk = devm_v4l2_sensor_clk_get(dev, NULL);
	if (IS_ERR(sensor->xclk))
		return dev_err_probe(dev, PTR_ERR(sensor->xclk),
				     "could not get xclk\n");

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

	ret = ov5647_configure_regulators(dev, sensor);
	if (ret)
		dev_err_probe(dev, ret, "Failed to get power regulators\n");

	sensor->mode = OV5647_DEFAULT_MODE;

	sd = &sensor->sd;
	v4l2_i2c_subdev_init(sd, client, &ov5647_subdev_ops);
	sd->internal_ops = &ov5647_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	ret = ov5647_init_controls(sensor);
	if (ret)
		return ret;

	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sensor->pad);
	if (ret < 0)
		goto ctrl_handler_free;

	sensor->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(sensor->regmap)) {
		ret = dev_err_probe(dev, PTR_ERR(sensor->regmap),
				    "Failed to init CCI\n");
		goto entity_cleanup;
	}

	ret = ov5647_power_on(dev);
	if (ret)
		goto entity_cleanup;

	ret = ov5647_detect(sd);
	if (ret < 0)
		goto power_off;

	sd->state_lock = sensor->ctrls.lock;
	ret = v4l2_subdev_init_finalize(sd);
	if (ret < 0) {
		ret = dev_err_probe(dev, ret, "failed to init subdev\n");
		goto power_off;
	}

	/* Enable runtime PM and turn off the device */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = v4l2_async_register_subdev_sensor(sd);
	if (ret < 0)
		goto v4l2_subdev_cleanup;

	pm_runtime_idle(dev);

	dev_dbg(dev, "OmniVision OV5647 camera driver probed\n");

	return 0;

v4l2_subdev_cleanup:
	v4l2_subdev_cleanup(sd);
power_off:
	ov5647_power_off(dev);
entity_cleanup:
	media_entity_cleanup(&sd->entity);
ctrl_handler_free:
	v4l2_ctrl_handler_free(&sensor->ctrls);

	return ret;
}

static void ov5647_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5647 *sensor = to_sensor(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls);
	v4l2_device_unregister_subdev(sd);
	pm_runtime_disable(&client->dev);
}

static const struct dev_pm_ops ov5647_pm_ops = {
	SET_RUNTIME_PM_OPS(ov5647_power_off, ov5647_power_on, NULL)
};

static const struct i2c_device_id ov5647_id[] = {
	{ "ov5647" },
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
	.probe		= ov5647_probe,
	.remove		= ov5647_remove,
	.id_table	= ov5647_id,
};

module_i2c_driver(ov5647_driver);

MODULE_AUTHOR("Ramiro Oliveira <roliveir@synopsys.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision ov5647 sensors");
MODULE_LICENSE("GPL v2");
