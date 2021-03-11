// SPDX-License-Identifier: GPL-2.0
/*
 * ov5648 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 add quick stream on/off
 * V0.0X01.0X05 add function g_mbus_config
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
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/pinctrl/consumer.h>
#include <linux/version.h>
#include <media/v4l2-async.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#include <linux/rk-camera-module.h>

/* verify default register values */
//#define CHECK_REG_VALUE

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x05)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define MIPI_FREQ	210000000U
#define OV5648_PIXEL_RATE		(210000000LL * 2LL * 2LL / 10)
#define OV5648_XVCLK_FREQ		24000000

#define CHIP_ID				0x5648
#define OV5648_REG_CHIP_ID		0x300a

#define OV5648_REG_CTRL_MODE		0x0100
#define OV5648_MODE_SW_STANDBY		0x00
#define OV5648_MODE_STREAMING		0x01

#define OV5648_REG_EXPOSURE		0x3500
#define	OV5648_EXPOSURE_MIN		4
#define	OV5648_EXPOSURE_STEP		1
#define OV5648_VTS_MAX			0x7fff

#define OV5648_REG_ANALOG_GAIN		0x3509
#define	ANALOG_GAIN_MIN			0x10
#define	ANALOG_GAIN_MAX			0xf8
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0xf8

#define OV5648_REG_GAIN_H		0x350a
#define OV5648_REG_GAIN_L		0x350b
#define OV5648_GAIN_L_MASK		0xff
#define OV5648_GAIN_H_MASK		0x03
#define OV5648_DIGI_GAIN_H_SHIFT	8
#define OV5648_DIGI_GAIN_MIN		0
#define OV5648_DIGI_GAIN_MAX		(0x4000 - 1)
#define OV5648_DIGI_GAIN_STEP		1
#define OV5648_DIGI_GAIN_DEFAULT	1024

#define OV5648_REG_TEST_PATTERN		0x503d
#define	OV5648_TEST_PATTERN_ENABLE	0x80
#define	OV5648_TEST_PATTERN_DISABLE	0x0

#define OV5648_REG_VTS			0x380e

#define REG_NULL			0xFFFF

#define OV5648_REG_VALUE_08BIT		1
#define OV5648_REG_VALUE_16BIT		2
#define OV5648_REG_VALUE_24BIT		3

#define OV5648_LANES			2
#define OV5648_BITS_PER_SAMPLE		10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define OV5648_NAME			"ov5648"

static const char * const ov5648_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV5648_NUM_SUPPLIES ARRAY_SIZE(ov5648_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov5648_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct ov5648 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV5648_NUM_SUPPLIES];

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
	const struct ov5648_mode *cur_mode;
	unsigned int lane_num;
	unsigned int cfg_num;
	unsigned int pixel_rate;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_ov5648(sd) container_of(sd, struct ov5648, subdev)

/*
 * Xclk 24Mhz
 * Pclk 84Mhz
 * linelength 2816(0xb00)
 * framelength 1984(0x7c0)
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 15fps
 * mipi_datarate per lane 420Mbps
 */
static const struct regval ov5648_global_regs[] = {
	{0x0100, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3011, 0x02},
	{0x3017, 0x05},
	{0x3018, 0x4c}, //bit[7:5] 001: 1lane;010: 2lane
	{0x301c, 0xd2},
	{0x3022, 0x00},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x3036, 0x69},
	{0x3037, 0x03},
	{0x3038, 0x00},
	{0x3039, 0x00},
	{0x303a, 0x00},
	{0x303b, 0x19},
	{0x303c, 0x11},
	{0x303d, 0x30},
	{0x3105, 0x11},
	{0x3106, 0x05},
	{0x3304, 0x28},
	{0x3305, 0x41},
	{0x3306, 0x30},
	{0x3308, 0x00},
	{0x3309, 0xc8},
	{0x330a, 0x01},
	{0x330b, 0x90},
	{0x330c, 0x02},
	{0x330d, 0x58},
	{0x330e, 0x03},
	{0x330f, 0x20},
	{0x3300, 0x00},
	{0x3500, 0x00},
	{0x3501, 0x3d},
	{0x3502, 0x00},
	{0x3503, 0x07},
	{0x350a, 0x00},
	{0x350b, 0x40},
	{0x3601, 0x33},
	{0x3602, 0x00},
	{0x3611, 0x0e},
	{0x3612, 0x2b},
	{0x3614, 0x50},

	{0x3620, 0x33},
	{0x3622, 0x00},
	{0x3630, 0xad},
	{0x3631, 0x00},
	{0x3632, 0x94},
	{0x3633, 0x17},
	{0x3634, 0x14},
	{0x3704, 0xc0},
	{0x3705, 0x2a},
	{0x3708, 0x66},
	{0x3709, 0x52},
	{0x370b, 0x23},
	{0x370c, 0xcf},
	{0x370d, 0x00},
	{0x370e, 0x00},
	{0x371c, 0x07},
	{0x3739, 0xd2},
	{0x373c, 0x00},
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
	{0x380c, 0x0b},
	{0x380d, 0x00},
	{0x380e, 0x03},
	{0x380f, 0xe0},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3817, 0x00},
	{0x3820, 0x08},
	{0x3821, 0x07},
	{0x3826, 0x03},
	{0x3829, 0x00},
	{0x382b, 0x0b},
	{0x3830, 0x00},
	{0x3836, 0x00},
	{0x3837, 0x00},
	{0x3838, 0x00},
	{0x3839, 0x04},
	{0x383a, 0x00},
	{0x383b, 0x01},
	{0x3b00, 0x00},
	{0x3b02, 0x08},
	{0x3b03, 0x00},
	{0x3b04, 0x04},

	{0x3b05, 0x00},
	{0x3b06, 0x04},
	{0x3b07, 0x08},
	{0x3b08, 0x00},
	{0x3b09, 0x02},
	{0x3b0a, 0x04},
	{0x3b0b, 0x00},
	{0x3b0c, 0x3d},
	{0x3f01, 0x0d},
	{0x3f0f, 0xf5},
	{0x4000, 0x89},
	{0x4001, 0x02},
	{0x4002, 0x45},
	{0x4004, 0x02},
	{0x4005, 0x18},
	{0x4006, 0x08},
	{0x4007, 0x10},
	{0x4008, 0x00},
	{0x4050, 0x6e},
	{0x4051, 0x8f},
	{0x4300, 0xf8},
	{0x4303, 0xff},
	{0x4304, 0x00},
	{0x4307, 0xff},
	{0x4520, 0x00},
	{0x4521, 0x00},
	{0x4511, 0x22},
	{0x4801, 0x0f},
	{0x4814, 0x2a},
	{0x481f, 0x3c},
	{0x4823, 0x3c},
	{0x4826, 0x00},
	{0x481b, 0x3c},
	{0x4827, 0x32},
	{0x4837, 0x18},
	{0x4b00, 0x06},
	{0x4b01, 0x0a},
	{0x4b04, 0x10},
	{0x5000, 0xff},
	{0x5001, 0x00},
	{0x5002, 0x41},
	{0x5003, 0x0a},
	{0x5004, 0x00},
	{0x5043, 0x00},
	{0x5013, 0x00},
	{0x501f, 0x03},
	{0x503d, 0x00},
	{0x5780, 0xfc},
	{0x5781, 0x1f},
	{0x5782, 0x03},
	{0x5786, 0x20},
	{0x5787, 0x40},
	{0x5788, 0x08},
	{0x5789, 0x08},
	{0x578a, 0x02},
	{0x578b, 0x01},
	{0x578c, 0x01},

	{0x578d, 0x0c},
	{0x578e, 0x02},
	{0x578f, 0x01},
	{0x5790, 0x01},
	{0x5a00, 0x08},
	{0x5b00, 0x01},
	{0x5b01, 0x40},
	{0x5b02, 0x00},
	{0x5b03, 0xf0},
	//{0x0100, 0x01},

	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * Pclk 84Mhz
 * linelength 2816(0xb00)
 * framelength 1984(0x7c0)
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 15fps
 * mipi_datarate per lane 420Mbps
 */
static const struct regval ov5648_2592x1944_regs[] = {
	// 2592x1944 15fps 2 lane MIPI 420Mbps/lane
	{0x0100, 0x00},
	{0x3501, 0x7b}, // exposure
	{0x2502, 0x00}, // exposure
	{0x3708, 0x63},
	{0x3709, 0x12},
	{0x370c, 0xcc}, // changed by AM05d
	{0x3800, 0x00}, // xstart = 0
	{0x3801, 0x00}, // xstart
	{0x3802, 0x00}, // ystart = 0
	{0x3803, 0x00}, // ystart
	{0x3804, 0x0a}, // xend = 2623
	{0x3805, 0x3f}, // xend
	{0x3806, 0x07}, // yend = 1955
	{0x3807, 0xa3}, // yend
	{0x3808, 0x0a}, // x output size = 2592
	{0x3809, 0x20}, // x output size
	{0x380a, 0x07}, // y output size = 1944
	{0x380b, 0x98}, // y output size

	{0x380c, 0x0b}, // hts = 2816
	{0x380d, 0x00}, // hts
	{0x380e, 0x07}, // vts = 1984
	{0x380f, 0xc0}, // vts
	{0x3810, 0x00}, // isp x win = 16
	{0x3811, 0x10}, // isp x win
	{0x3812, 0x00}, // isp y win = 6
	{0x3813, 0x06}, // isp y win
	{0x3814, 0x11}, // x inc
	{0x3815, 0x11}, // y inc
	{0x3817, 0x00}, // hsync start
	{0x3820, 0x40}, // flip off, v bin off
	{0x3821, 0x06}, // mirror on, v bin off
	{0x4004, 0x04}, // black line number
	{0x4005, 0x1a}, // blc always update
	{0x350b, 0x40}, // gain = 4x
	{0x4837, 0x17}, // MIPI global timing
	//{0x0100, 0x01},

	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * Pclk 84Mhz
 * linelength 2816(0xb00)
 * framelength 992(0x3e0)
 * grabwindow_width 1296
 * grabwindow_height 972
 * max_framerate 30fps
 * mipi_datarate per lane 420Mbps
 */
static const struct regval ov5648_1296x972_regs[] = {
	// 1296x972 30fps 2 lane MIPI 420Mbps/lane
	{0x0100, 0x00},
	{0x3501, 0x3d}, // exposure
	{0x3502, 0x00}, // exposure
	{0x3708, 0x66},
	{0x3709, 0x52},
	{0x370c, 0xcf},
	{0x3800, 0x00}, // xstart = 0
	{0x3801, 0x00}, // x start
	{0x3802, 0x00}, // y start = 0
	{0x3803, 0x00}, // y start
	{0x3804, 0x0a}, // xend = 2623
	{0x3805, 0x3f}, // xend
	{0x3806, 0x07}, // yend = 1955
	{0x3807, 0xa3}, // yend
	{0x3808, 0x05}, // x output size = 1296
	{0x3809, 0x10}, // x output size
	{0x380a, 0x03}, // y output size = 972
	{0x380b, 0xcc}, // y output size

	{0x380c, 0x0b}, // hts = 2816
	{0x380d, 0x00}, // hts
	{0x380e, 0x03}, // vts = 992
	{0x380f, 0xe0}, // vts
	{0x3810, 0x00}, // isp x win = 8
	{0x3811, 0x08}, // isp x win
	{0x3812, 0x00}, // isp y win = 4
	{0x3813, 0x04}, // isp y win
	{0x3814, 0x31}, // x inc
	{0x3815, 0x31}, // y inc
	{0x3817, 0x00}, // hsync start
	{0x3820, 0x08}, // flip off, v bin off
	{0x3821, 0x07}, // mirror on, h bin on
	{0x4004, 0x02}, // black line number
	{0x4005, 0x18}, // blc level trigger
	{0x350b, 0x80}, // gain = 8x
	{0x4837, 0x17}, // MIPI global timing
	//{0x0100, 0x01},

	{REG_NULL, 0x00}
};

static const struct ov5648_mode supported_modes_2lane[] = {
	{
		.width = 2592,
		.height = 1944,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.exp_def = 0x0450,
		.hts_def = 0x0b00,
		.vts_def = 0x07c0,
		.reg_list = ov5648_2592x1944_regs,
	},
	{
		.width = 1296,
		.height = 972,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x03d0,
		.hts_def = 0x0b00,
		.vts_def = 0x03e0,
		.reg_list = ov5648_1296x972_regs,
	},
};

static const struct ov5648_mode *supported_modes;

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ
};

static const char * const ov5648_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int ov5648_write_reg(struct i2c_client *client, u16 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	//dev_info(&client->dev, "%s(%d) enter!\n", __func__, __LINE__);
	//dev_info(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2) {
		dev_err(&client->dev,
			   "write reg(0x%x val:0x%x)failed !\n", reg, val);
		return -EIO;
	}
	return 0;
}

static int ov5648_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov5648_write_reg(client, regs[i].addr,
				       OV5648_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int ov5648_read_reg(struct i2c_client *client, u16 reg,
					unsigned int len, u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

/* Check Register value */
#ifdef CHECK_REG_VALUE
static int ov5648_reg_verify(struct i2c_client *client,
				const struct regval *regs)
{
	u32 i;
	int ret = 0;
	u32 value;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = ov5648_read_reg(client, regs[i].addr,
			  OV5648_REG_VALUE_08BIT, &value);
		if (value != regs[i].val) {
			dev_info(&client->dev, "%s:0x%04x is 0x%08x \
					instead of 0x%08x\n", __func__,
					regs[i].addr, value, regs[i].val);
		}
	}
	return ret;
}
#endif

static int ov5648_get_reso_dist(const struct ov5648_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov5648_mode *
ov5648_find_best_fit(struct ov5648 *ov5648,
			struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	int i;

	for (i = 0; i < ov5648->cfg_num; i++) {
		dist = ov5648_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov5648_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov5648 *ov5648 = to_ov5648(sd);
	const struct ov5648_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&ov5648->mutex);

	mode = ov5648_find_best_fit(ov5648, fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov5648->mutex);
		return -ENOTTY;
#endif
	} else {
		ov5648->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov5648->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov5648->vblank, vblank_def,
					 OV5648_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&ov5648->mutex);

	return 0;
}

static int ov5648_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov5648 *ov5648 = to_ov5648(sd);
	const struct ov5648_mode *mode = ov5648->cur_mode;

	mutex_lock(&ov5648->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov5648->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ov5648->mutex);

	return 0;
}

static int ov5648_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov5648_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov5648 *ov5648 = to_ov5648(sd);

	if (fse->index >= ov5648->cfg_num)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov5648_enable_test_pattern(struct ov5648 *ov5648, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV5648_TEST_PATTERN_ENABLE;
	else
		val = OV5648_TEST_PATTERN_DISABLE;

	return ov5648_write_reg(ov5648->client, OV5648_REG_TEST_PATTERN,
				OV5648_REG_VALUE_08BIT, val);
}

static int ov5648_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct ov5648 *ov5648 = to_ov5648(sd);
	const struct ov5648_mode *mode = ov5648->cur_mode;

	mutex_lock(&ov5648->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ov5648->mutex);

	return 0;
}

static void ov5648_get_module_inf(struct ov5648 *ov5648,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, OV5648_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, ov5648->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, ov5648->len_name, sizeof(inf->base.lens));
}

static long ov5648_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov5648 *ov5648 = to_ov5648(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		ov5648_get_module_inf(ov5648, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = ov5648_write_reg(ov5648->client, OV5648_REG_CTRL_MODE,
				OV5648_REG_VALUE_08BIT, OV5648_MODE_STREAMING);
		else
			ret = ov5648_write_reg(ov5648->client, OV5648_REG_CTRL_MODE,
				OV5648_REG_VALUE_08BIT, OV5648_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov5648_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = ov5648_ioctl(sd, cmd, inf);
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
			ret = ov5648_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov5648_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __ov5648_start_stream(struct ov5648 *ov5648)
{
	int ret;

	ret = ov5648_write_array(ov5648->client, ov5648->cur_mode->reg_list);
	if (ret)
		return ret;

#ifdef CHECK_REG_VALUE
	usleep_range(10000, 20000);
	/*  verify default values to make sure everything has */
	/*  been written correctly as expected */
	dev_info(&ov5648->client->dev, "%s:Check register value!\n",
				__func__);
	ret = ov5648_reg_verify(ov5648->client, ov5648_global_regs);
	if (ret)
		return ret;

	ret = ov5648_reg_verify(ov5648->client, ov5648->cur_mode->reg_list);
	if (ret)
		return ret;
#endif

	/* In case these controls are set before streaming */
	mutex_unlock(&ov5648->mutex);
	ret = v4l2_ctrl_handler_setup(&ov5648->ctrl_handler);
	mutex_lock(&ov5648->mutex);
	if (ret)
		return ret;
	ret = ov5648_write_reg(ov5648->client, OV5648_REG_CTRL_MODE,
				OV5648_REG_VALUE_08BIT, OV5648_MODE_STREAMING);
	return ret;
}

static int __ov5648_stop_stream(struct ov5648 *ov5648)
{
	return ov5648_write_reg(ov5648->client, OV5648_REG_CTRL_MODE,
				OV5648_REG_VALUE_08BIT, OV5648_MODE_SW_STANDBY);
}

static int ov5648_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov5648 *ov5648 = to_ov5648(sd);
	struct i2c_client *client = ov5648->client;
	int ret = 0;

	dev_info(&client->dev, "%s(%d) enter!\n", __func__, __LINE__);
	mutex_lock(&ov5648->mutex);
	on = !!on;
	if (on == ov5648->streaming)
		goto unlock_and_return;

	if (on) {
		dev_info(&client->dev, "stream on!!!\n");
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov5648_start_stream(ov5648);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		dev_info(&client->dev, "stream off!!!\n");
		__ov5648_stop_stream(ov5648);
		pm_runtime_put(&client->dev);
	}

	ov5648->streaming = on;

unlock_and_return:
	mutex_unlock(&ov5648->mutex);

	return ret;
}

static int ov5648_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov5648 *ov5648 = to_ov5648(sd);
	struct i2c_client *client = ov5648->client;
	int ret = 0;

	mutex_lock(&ov5648->mutex);

	/* If the power state is not modified - no work to do. */
	if (ov5648->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = ov5648_write_array(ov5648->client, ov5648_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ov5648->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		ov5648->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ov5648->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov5648_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV5648_XVCLK_FREQ / 1000 / 1000);
}

static int __ov5648_power_on(struct ov5648 *ov5648)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov5648->client->dev;

	if (!IS_ERR(ov5648->power_gpio))
		gpiod_set_value_cansleep(ov5648->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(ov5648->pins_default)) {
		ret = pinctrl_select_state(ov5648->pinctrl,
					   ov5648->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(ov5648->xvclk, OV5648_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(ov5648->xvclk) != OV5648_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(ov5648->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(ov5648->reset_gpio))
		gpiod_set_value_cansleep(ov5648->reset_gpio, 1);

	ret = regulator_bulk_enable(OV5648_NUM_SUPPLIES, ov5648->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov5648->reset_gpio))
		gpiod_set_value_cansleep(ov5648->reset_gpio, 0);

	if (!IS_ERR(ov5648->pwdn_gpio))
		gpiod_set_value_cansleep(ov5648->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov5648_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov5648->xvclk);

	return ret;
}

static void __ov5648_power_off(struct ov5648 *ov5648)
{
	int ret;
	struct device *dev = &ov5648->client->dev;

	if (!IS_ERR(ov5648->pwdn_gpio))
		gpiod_set_value_cansleep(ov5648->pwdn_gpio, 0);
	clk_disable_unprepare(ov5648->xvclk);
	if (!IS_ERR(ov5648->reset_gpio))
		gpiod_set_value_cansleep(ov5648->reset_gpio, 1);
	if (!IS_ERR_OR_NULL(ov5648->pins_sleep)) {
		ret = pinctrl_select_state(ov5648->pinctrl,
					   ov5648->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(ov5648->power_gpio))
		gpiod_set_value_cansleep(ov5648->power_gpio, 0);

	regulator_bulk_disable(OV5648_NUM_SUPPLIES, ov5648->supplies);
}

static int ov5648_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5648 *ov5648 = to_ov5648(sd);

	return __ov5648_power_on(ov5648);
}

static int ov5648_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5648 *ov5648 = to_ov5648(sd);

	__ov5648_power_off(ov5648);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov5648_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov5648 *ov5648 = to_ov5648(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov5648_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov5648->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov5648->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov5648_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct ov5648 *ov5648 = to_ov5648(sd);

	if (fie->index >= ov5648->cfg_num)
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int ov5648_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (OV5648_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops ov5648_pm_ops = {
	SET_RUNTIME_PM_OPS(ov5648_runtime_suspend,
			   ov5648_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov5648_internal_ops = {
	.open = ov5648_open,
};
#endif

static const struct v4l2_subdev_core_ops ov5648_core_ops = {
	.s_power = ov5648_s_power,
	.ioctl = ov5648_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov5648_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ov5648_video_ops = {
	.s_stream = ov5648_s_stream,
	.g_frame_interval = ov5648_g_frame_interval,
	.g_mbus_config = ov5648_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops ov5648_pad_ops = {
	.enum_mbus_code = ov5648_enum_mbus_code,
	.enum_frame_size = ov5648_enum_frame_sizes,
	.enum_frame_interval = ov5648_enum_frame_interval,
	.get_fmt = ov5648_get_fmt,
	.set_fmt = ov5648_set_fmt,
};

static const struct v4l2_subdev_ops ov5648_subdev_ops = {
	.core	= &ov5648_core_ops,
	.video	= &ov5648_video_ops,
	.pad	= &ov5648_pad_ops,
};

static int ov5648_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov5648 *ov5648 = container_of(ctrl->handler,
					     struct ov5648, ctrl_handler);
	struct i2c_client *client = ov5648->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov5648->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov5648->exposure,
					 ov5648->exposure->minimum, max,
					 ov5648->exposure->step,
					 ov5648->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */

		ret = ov5648_write_reg(ov5648->client, OV5648_REG_EXPOSURE,
				       OV5648_REG_VALUE_24BIT, ctrl->val << 4);

		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov5648_write_reg(ov5648->client, OV5648_REG_GAIN_L,
				       OV5648_REG_VALUE_08BIT,
				       ctrl->val & OV5648_GAIN_L_MASK);
		ret |= ov5648_write_reg(ov5648->client, OV5648_REG_GAIN_H,
				       OV5648_REG_VALUE_08BIT,
				       (ctrl->val >> OV5648_DIGI_GAIN_H_SHIFT) &
				       OV5648_GAIN_H_MASK);
		break;
	case V4L2_CID_VBLANK:

		ret = ov5648_write_reg(ov5648->client, OV5648_REG_VTS,
				       OV5648_REG_VALUE_16BIT,
				       ctrl->val + ov5648->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov5648_enable_test_pattern(ov5648, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov5648_ctrl_ops = {
	.s_ctrl = ov5648_set_ctrl,
};

static int ov5648_initialize_controls(struct ov5648 *ov5648)
{
	const struct ov5648_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &ov5648->ctrl_handler;
	mode = ov5648->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &ov5648->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, ov5648->pixel_rate, 1, ov5648->pixel_rate);

	h_blank = mode->hts_def - mode->width;
	ov5648->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov5648->hblank)
		ov5648->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov5648->vblank = v4l2_ctrl_new_std(handler, &ov5648_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV5648_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	ov5648->exposure = v4l2_ctrl_new_std(handler, &ov5648_ctrl_ops,
				V4L2_CID_EXPOSURE, OV5648_EXPOSURE_MIN,
				exposure_max, OV5648_EXPOSURE_STEP,
				mode->exp_def);

	ov5648->anal_gain = v4l2_ctrl_new_std(handler, &ov5648_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	/* Digital gain */
	ov5648->digi_gain = v4l2_ctrl_new_std(handler, &ov5648_ctrl_ops,
				V4L2_CID_DIGITAL_GAIN, OV5648_DIGI_GAIN_MIN,
				OV5648_DIGI_GAIN_MAX, OV5648_DIGI_GAIN_STEP,
				OV5648_DIGI_GAIN_DEFAULT);

	ov5648->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov5648_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov5648_test_pattern_menu) - 1,
				0, 0, ov5648_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov5648->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov5648->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov5648_check_sensor_id(struct ov5648 *ov5648,
				  struct i2c_client *client)
{
	struct device *dev = &ov5648->client->dev;
	u32 id = 0;
	int ret;

	ret = ov5648_read_reg(client, OV5648_REG_CHIP_ID,
			      OV5648_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int ov5648_configure_regulators(struct ov5648 *ov5648)
{
	int i;

	for (i = 0; i < OV5648_NUM_SUPPLIES; i++)
		ov5648->supplies[i].supply = ov5648_supply_names[i];

	return devm_regulator_bulk_get(&ov5648->client->dev,
				       OV5648_NUM_SUPPLIES,
				       ov5648->supplies);
}

static int ov5648_parse_of(struct ov5648 *ov5648)
{
	struct device *dev = &ov5648->client->dev;
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

	ov5648->lane_num = rval;
	if (2 == ov5648->lane_num) {
		ov5648->cur_mode = &supported_modes_2lane[0];
		supported_modes = supported_modes_2lane;
		ov5648->cfg_num = ARRAY_SIZE(supported_modes_2lane);

		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		ov5648->pixel_rate = MIPI_FREQ * 2U * ov5648->lane_num / 10U;
		dev_info(dev, "lane_num(%d)  pixel_rate(%u)\n",
				 ov5648->lane_num, ov5648->pixel_rate);
	} else {
		dev_err(dev, "unsupported lane_num(%d)\n", ov5648->lane_num);
		return -1;
	}
	return 0;
}

static int ov5648_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov5648 *ov5648;
	struct v4l2_subdev *sd;
	char facing[2] = "b";
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov5648 = devm_kzalloc(dev, sizeof(*ov5648), GFP_KERNEL);
	if (!ov5648)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov5648->module_index);
	if (ret) {
		dev_warn(dev, "could not get module index!\n");
		ov5648->module_index = 0;
	}
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov5648->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov5648->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov5648->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ov5648->client = client;

	ov5648->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov5648->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov5648->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(ov5648->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	ov5648->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov5648->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios, maybe no use\n");

	ov5648->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov5648->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov5648_configure_regulators(ov5648);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}
	ret = ov5648_parse_of(ov5648);
	if (ret != 0)
		return -EINVAL;

	ov5648->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov5648->pinctrl)) {
		ov5648->pins_default =
			pinctrl_lookup_state(ov5648->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov5648->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov5648->pins_sleep =
			pinctrl_lookup_state(ov5648->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov5648->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&ov5648->mutex);

	sd = &ov5648->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov5648_subdev_ops);
	ret = ov5648_initialize_controls(ov5648);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov5648_power_on(ov5648);
	if (ret)
		goto err_free_handler;

	ret = ov5648_check_sensor_id(ov5648, client);
	if (ret < 0) {
		dev_info(&client->dev, "%s(%d) Check id  failed\n"
				  "check following information:\n"
				  "Power/PowerDown/Reset/Mclk/I2cBus !!\n",
				  __func__, __LINE__);
		goto err_power_off;
	}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov5648_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov5648->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov5648->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov5648->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov5648->module_index, facing,
		 OV5648_NAME, dev_name(sd->dev));

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
	__ov5648_power_off(ov5648);
err_free_handler:
	v4l2_ctrl_handler_free(&ov5648->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov5648->mutex);

	return ret;
}

static int ov5648_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5648 *ov5648 = to_ov5648(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov5648->ctrl_handler);
	mutex_destroy(&ov5648->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov5648_power_off(ov5648);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov5648_of_match[] = {
	{ .compatible = "ovti,ov5648" },
	{},
};
MODULE_DEVICE_TABLE(of, ov5648_of_match);
#endif

static const struct i2c_device_id ov5648_match_id[] = {
	{ "ovti,ov5648", 0 },
	{ },
};

static struct i2c_driver ov5648_i2c_driver = {
	.driver = {
		.name = OV5648_NAME,
		.pm = &ov5648_pm_ops,
		.of_match_table = of_match_ptr(ov5648_of_match),
	},
	.probe		= &ov5648_probe,
	.remove		= &ov5648_remove,
	.id_table	= ov5648_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov5648_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov5648_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov5648 sensor driver");
MODULE_LICENSE("GPL v2");
