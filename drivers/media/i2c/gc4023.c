// SPDX-License-Identifier: GPL-2.0
/*
 * GC4023 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 init version.
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
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <linux/rk-preisp.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC4023_LANES			2
#define GC4023_BITS_PER_SAMPLE		10
#define GC4023_LINK_FREQ_LINEAR		432000000   //2560*1440

#define GC4023_PIXEL_RATE_LINEAR	(GC4023_LINK_FREQ_LINEAR * 2 / 10 * 2)

#define GC4023_XVCLK_FREQ		27000000

#define CHIP_ID				0x4023
#define GC4023_REG_CHIP_ID_H		0x03f0
#define GC4023_REG_CHIP_ID_L		0x03f1

#define GC4023_REG_CTRL_MODE		0x0100
#define GC4023_MODE_SW_STANDBY		0x00
#define GC4023_MODE_STREAMING		0x09

#define GC4023_REG_EXPOSURE_H		0x0202
#define GC4023_REG_EXPOSURE_L		0x0203
#define GC4023_EXPOSURE_MIN		4
#define GC4023_EXPOSURE_STEP		1
#define GC4023_VTS_MAX			0x7fff

#define GC4023_GAIN_MIN			64
#define GC4023_GAIN_MAX			0xffff
#define GC4023_GAIN_STEP		1
#define GC4023_GAIN_DEFAULT		256

#define GC4023_REG_TEST_PATTERN		0x008c
#define GC4023_TEST_PATTERN_ENABLE	0x11
#define GC4023_TEST_PATTERN_DISABLE	0x0

#define GC4023_REG_VTS_H		0x0340
#define GC4023_REG_VTS_L		0x0341

#define GC4023_MIRROR_REG		0x0063
#define GC4023_MIRROR_BIT_MASK	BIT(0)
#define GC4023_FLIP_REG			0x022c
#define GC4023_FLIP_BIT_MASK	BIT(1)
#define REG_NULL			0xFFFF

#define GC4023_REG_VALUE_08BIT		1
#define GC4023_REG_VALUE_16BIT		2
#define GC4023_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define GC4023_NAME			"gc4023"

static const char * const gc4023_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
	"avdd",		/* Analog power */
};

#define GC4023_NUM_SUPPLIES ARRAY_SIZE(gc4023_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct gc4023_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct gc4023 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct gpio_desc	*pwren_gpio;
	struct regulator_bulk_data supplies[GC4023_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct v4l2_ctrl	*h_flip;
	struct v4l2_ctrl	*v_flip;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct gc4023_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	u32			cur_vts;
	u32			cur_pixel_rate;
	u32			cur_link_freq;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
};

#define to_gc4023(sd) container_of(sd, struct gc4023, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval gc4023_global_regs[] = {
	{REG_NULL, 0x00},
};

static const u32 reg_val_table_liner[26][7] = {
//   614   615   218   1467  1468  b8    b9
	{0x00, 0x00, 0x00, 0x07, 0x0f, 0x01, 0x00},
	{0x80, 0x02, 0x00, 0x07, 0x0f, 0x01, 0x0B},
	{0x01, 0x00, 0x00, 0x07, 0x0f, 0x01, 0x19},
	{0x81, 0x02, 0x00, 0x07, 0x0f, 0x01, 0x2A},
	{0x02, 0x00, 0x00, 0x08, 0x10, 0x02, 0x00},
	{0x82, 0x02, 0x00, 0x08, 0x10, 0x02, 0x17},
	{0x03, 0x00, 0x00, 0x08, 0x10, 0x02, 0x33},
	{0x83, 0x02, 0x00, 0x09, 0x11, 0x03, 0x14},
	{0x04, 0x00, 0x00, 0x0A, 0x12, 0x04, 0x00},
	{0x80, 0x02, 0x20, 0x0A, 0x12, 0x04, 0x2F},
	{0x01, 0x00, 0x20, 0x0B, 0x13, 0x05, 0x26},
	{0x81, 0x02, 0x20, 0x0C, 0x14, 0x06, 0x28},
	{0x02, 0x00, 0x20, 0x0D, 0x15, 0x08, 0x00},
	{0x82, 0x02, 0x20, 0x0D, 0x15, 0x09, 0x1E},
	{0x03, 0x00, 0x20, 0x0E, 0x16, 0x0B, 0x0C},
	{0x83, 0x02, 0x20, 0x0E, 0x16, 0x0D, 0x11},
	{0x04, 0x00, 0x20, 0x0F, 0x17, 0x10, 0x00},
	{0x84, 0x02, 0x20, 0x0F, 0x17, 0x12, 0x3D},
	{0x05, 0x00, 0x20, 0x10, 0x18, 0x16, 0x19},
	{0x85, 0x02, 0x20, 0x10, 0x18, 0x1A, 0x22},
	{0xb5, 0x04, 0x20, 0x11, 0x19, 0x20, 0x00},
	{0x85, 0x05, 0x20, 0x11, 0x19, 0x25, 0x3A},
	{0x05, 0x08, 0x20, 0x12, 0x1a, 0x2C, 0x33},
	{0x45, 0x09, 0x20, 0x15, 0x1d, 0x35, 0x05},
	{0x55, 0x0a, 0x20, 0x16, 0x1e, 0x40, 0x00},
};

static const u32 gain_level_table[26] = {
	64,
	76,
	90,
	106,
	128,
	152,
	179,
	212,
	256,
	303,
	358,
	425,
	512,
	607,
	717,
	849,
	1024,
	1213,
	1434,
	1699,
	2048,
	2427,
	2867,
	3398,
	4096,
	0xffff,
};

/*
 * Xclk 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 864Mbps, 2lane
 */
static const struct regval gc4023_linear10bit_2560x1440_regs[] = {
	{0x03fe, 0xf0},
	{0x03fe, 0x00},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x0a38, 0x00},
	{0x0a38, 0x01},
	{0x0a20, 0x07},
	{0x061c, 0x50},
	{0x061d, 0x22},
	{0x061e, 0x78},
	{0x061f, 0x06},
	{0x0a21, 0x10},
	{0x0a34, 0x40},
	{0x0a35, 0x01},
	{0x0a36, 0x60},
	{0x0a37, 0x06},
	{0x0314, 0x50},
	{0x0315, 0x00},
	{0x031c, 0xce},
	{0x0219, 0x47},
	{0x0342, 0x04},
	{0x0343, 0xb0},
	{0x0259, 0x05},
	{0x025a, 0xa0},
	{0x0340, 0x05},
	{0x0341, 0xdc},
	{0x0347, 0x02},
	{0x0348, 0x0a},
	{0x0349, 0x08},
	{0x034a, 0x05},
	{0x034b, 0xa8},

	{0x0094, 0x0a},
	{0x0095, 0x00},
	{0x0096, 0x05},
	{0x0097, 0xa2},

	{0x0099, 0x04},
	{0x009b, 0x04},
	{0x060c, 0x01},
	{0x060e, 0x08},
	{0x060f, 0x05},
	{0x070c, 0x01},
	{0x070e, 0x08},
	{0x070f, 0x05},
	{0x0909, 0x03},
	{0x0902, 0x04},
	{0x0904, 0x0b},
	{0x0907, 0x54},
	{0x0908, 0x06},
	{0x0903, 0x9d},
	{0x072a, 0x18},
	{0x0724, 0x0a},
	{0x0727, 0x0a},
	{0x072a, 0x18},
	{0x072b, 0x08},
	{0x1466, 0x10},
	{0x1468, 0x0f},
	{0x1467, 0x07},
	{0x1469, 0x80},
	{0x146a, 0xbc},
	{0x0707, 0x07},
	{0x0737, 0x0f},
	{0x061a, 0x00},
	{0x1430, 0x80},
	{0x1407, 0x10},
	{0x1408, 0x16},
	{0x1409, 0x03},
	{0x146d, 0x0e},
	{0x146e, 0x32},
	{0x146f, 0x33},
	{0x1470, 0x2c},
	{0x1471, 0x2d},
	{0x1472, 0x3a},
	{0x1473, 0x3a},
	{0x1474, 0x40},
	{0x1475, 0x36},
	{0x1420, 0x14},
	{0x1464, 0x15},
	{0x146c, 0x40},
	{0x146d, 0x40},
	{0x1423, 0x08},
	{0x1428, 0x10},
	{0x1462, 0x08},
	{0x02ce, 0x04},
	{0x143a, 0x0b},
	{0x142b, 0x88},
	{0x0245, 0xc9},
	{0x023a, 0x08},
	{0x02cd, 0x88},
	{0x0612, 0x02},
	{0x0613, 0xc7},
	{0x0243, 0x03},
	{0x021b, 0x09},
	{0x0089, 0x03},
	{0x0040, 0xa3},
	{0x0075, 0x64},
	{0x0004, 0x0f},
	{0x0002, 0xa9},
	{0x0053, 0x0a},
	{0x0205, 0x0c},
	{0x0202, 0x06},
	{0x0203, 0x27},
	{0x0614, 0x00},
	{0x0615, 0x00},
	{0x0181, 0x0c},
	{0x0182, 0x05},
	{0x0185, 0x01},
	{0x0180, 0x46},
	{0x0100, 0x08},
	{0x0106, 0x38},
	{0x010d, 0x80},
	{0x010e, 0x0c},
	{0x0113, 0x02},
	{0x0114, 0x01},
	{0x0115, 0x10},
	//{0x0100, 0x09},
	{REG_NULL, 0x00},
};

static const struct gc4023_mode supported_modes[] = {
	{
		.width = 2560,
		.height = 1442,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0100,
		.hts_def = 0x0AA0,
		.vts_def = 0x05DC,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.reg_list = gc4023_linear10bit_2560x1440_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	GC4023_LINK_FREQ_LINEAR,
};

static const char * const gc4023_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int gc4023_write_reg(struct i2c_client *client, u16 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

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

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int gc4023_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc4023_write_reg(client, regs[i].addr,
				       GC4023_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int gc4023_read_reg(struct i2c_client *client, u16 reg,
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

static int gc4023_get_reso_dist(const struct gc4023_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
			abs(mode->height - framefmt->height);
}

static const struct gc4023_mode *
gc4023_find_best_fit(struct gc4023 *gc4023, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < gc4023->cfg_num; i++) {
		dist = gc4023_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc4023_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc4023 *gc4023 = to_gc4023(sd);
	const struct gc4023_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc4023->mutex);

	mode = gc4023_find_best_fit(gc4023, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc4023->mutex);
		return -ENOTTY;
#endif
	} else {
		gc4023->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc4023->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc4023->vblank, vblank_def,
					 GC4023_VTS_MAX - mode->height,
					 1, vblank_def);

		gc4023->cur_link_freq = 0;
		gc4023->cur_pixel_rate = GC4023_PIXEL_RATE_LINEAR;

		__v4l2_ctrl_s_ctrl_int64(gc4023->pixel_rate,
					 gc4023->cur_pixel_rate);
		__v4l2_ctrl_s_ctrl(gc4023->link_freq,
				   gc4023->cur_link_freq);
		gc4023->cur_vts = mode->vts_def;
	}
	mutex_unlock(&gc4023->mutex);

	return 0;
}

static int gc4023_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc4023 *gc4023 = to_gc4023(sd);
	const struct gc4023_mode *mode = gc4023->cur_mode;

	mutex_lock(&gc4023->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc4023->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc4023->mutex);

	return 0;
}

static int gc4023_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct gc4023 *gc4023 = to_gc4023(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = gc4023->cur_mode->bus_fmt;

	return 0;
}

static int gc4023_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc4023 *gc4023 = to_gc4023(sd);

	if (fse->index >= gc4023->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int gc4023_enable_test_pattern(struct gc4023 *gc4023, u32 pattern)
{
	u32 val;

	if (pattern)
		val = GC4023_TEST_PATTERN_ENABLE;
	else
		val = GC4023_TEST_PATTERN_DISABLE;

	return gc4023_write_reg(gc4023->client, GC4023_REG_TEST_PATTERN,
				GC4023_REG_VALUE_08BIT, val);
}

static int gc4023_set_gain_reg(struct gc4023 *gc4023, u32 gain)
{
	int i;
	int total;
	u32 tol_dig_gain = 0;

	if (gain < 64)
		gain = 64;
	total = sizeof(gain_level_table) / sizeof(u32) - 1;
	for (i = 0; i < total; i++) {
		if (gain_level_table[i] <= gain &&
		    gain < gain_level_table[i + 1])
			break;
	}
	tol_dig_gain = gain * 64 / gain_level_table[i];
	if (i >= total)
		i = total - 1;

	gc4023_write_reg(gc4023->client, 0x614,
			 GC4023_REG_VALUE_08BIT, reg_val_table_liner[i][0]);
	gc4023_write_reg(gc4023->client, 0x615,
			 GC4023_REG_VALUE_08BIT, reg_val_table_liner[i][1]);
	gc4023_write_reg(gc4023->client, 0x218,
			 GC4023_REG_VALUE_08BIT, reg_val_table_liner[i][2]);
	gc4023_write_reg(gc4023->client, 0x1467,
			 GC4023_REG_VALUE_08BIT, reg_val_table_liner[i][3]);
	gc4023_write_reg(gc4023->client, 0x1468,
			 GC4023_REG_VALUE_08BIT, reg_val_table_liner[i][4]);
	gc4023_write_reg(gc4023->client, 0xb8,
			 GC4023_REG_VALUE_08BIT, reg_val_table_liner[i][5]);
	gc4023_write_reg(gc4023->client, 0xb9,
			 GC4023_REG_VALUE_08BIT, reg_val_table_liner[i][6]);


	gc4023_write_reg(gc4023->client, 0x20e,
			 GC4023_REG_VALUE_08BIT, (tol_dig_gain >> 6));
	gc4023_write_reg(gc4023->client, 0x20f,
			 GC4023_REG_VALUE_08BIT, ((tol_dig_gain & 0x3f) << 2));
	return 0;
}

static int gc4023_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc4023 *gc4023 = to_gc4023(sd);
	const struct gc4023_mode *mode = gc4023->cur_mode;

	mutex_lock(&gc4023->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc4023->mutex);

	return 0;
}

static int gc4023_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct gc4023 *gc4023 = to_gc4023(sd);
	const struct gc4023_mode *mode = gc4023->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (GC4023_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	if (mode->hdr_mode == HDR_X2)
		val = 1 << (GC4023_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void gc4023_get_module_inf(struct gc4023 *gc4023,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, GC4023_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, gc4023->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, gc4023->len_name, sizeof(inf->base.lens));
}

static int gc4023_get_channel_info(struct gc4023 *gc4023, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = gc4023->cur_mode->vc[ch_info->index];
	ch_info->width = gc4023->cur_mode->width;
	ch_info->height = gc4023->cur_mode->height;
	ch_info->bus_fmt = gc4023->cur_mode->bus_fmt;
	return 0;
}

static long gc4023_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc4023 *gc4023 = to_gc4023(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;
	struct rkmodule_channel_info *ch_info;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc4023_get_module_inf(gc4023, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = gc4023->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = gc4023->cur_mode->width;
		h = gc4023->cur_mode->height;
		for (i = 0; i < gc4023->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				gc4023->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == gc4023->cfg_num) {
			dev_err(&gc4023->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = gc4023->cur_mode->hts_def -
			    gc4023->cur_mode->width;
			h = gc4023->cur_mode->vts_def -
			    gc4023->cur_mode->height;
			__v4l2_ctrl_modify_range(gc4023->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(gc4023->vblank, h,
						 GC4023_VTS_MAX -
						 gc4023->cur_mode->height,
						 1, h);

		gc4023->cur_link_freq = 0;
		gc4023->cur_pixel_rate = GC4023_PIXEL_RATE_LINEAR;

		__v4l2_ctrl_s_ctrl_int64(gc4023->pixel_rate,
					 gc4023->cur_pixel_rate);
		__v4l2_ctrl_s_ctrl(gc4023->link_freq,
				   gc4023->cur_link_freq);
		gc4023->cur_vts = gc4023->cur_mode->vts_def;
		}
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			ret = gc4023_write_reg(gc4023->client, GC4023_REG_CTRL_MODE,
				GC4023_REG_VALUE_08BIT, GC4023_MODE_STREAMING);
		else
			ret = gc4023_write_reg(gc4023->client, GC4023_REG_CTRL_MODE,
				GC4023_REG_VALUE_08BIT, GC4023_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = gc4023_get_channel_info(gc4023, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc4023_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	long ret;
	u32 stream = 0;
	struct rkmodule_channel_info *ch_info;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc4023_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc4023_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret)
				ret = -EFAULT;
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdr, up, sizeof(*hdr));
		if (!ret)
			ret = gc4023_ioctl(sd, cmd, hdr);
		else
			ret = -EFAULT;
		kfree(hdr);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc4023_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc4023_ioctl(sd, cmd, ch_info);
		if (!ret) {
			ret = copy_to_user(up, ch_info, sizeof(*ch_info));
			if (ret)
				ret = -EFAULT;
		}
		kfree(ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __gc4023_start_stream(struct gc4023 *gc4023)
{
	int ret;

	ret = gc4023_write_array(gc4023->client, gc4023->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&gc4023->ctrl_handler);
	if (gc4023->has_init_exp && gc4023->cur_mode->hdr_mode != NO_HDR) {
		ret = gc4023_ioctl(&gc4023->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&gc4023->init_hdrae_exp);
		if (ret) {
			dev_err(&gc4023->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	if (ret)
		return ret;

	ret |= gc4023_write_reg(gc4023->client, GC4023_REG_CTRL_MODE,
				GC4023_REG_VALUE_08BIT, GC4023_MODE_STREAMING);

	return ret;
}

static int __gc4023_stop_stream(struct gc4023 *gc4023)
{
	gc4023->has_init_exp = false;
	return gc4023_write_reg(gc4023->client, GC4023_REG_CTRL_MODE,
				GC4023_REG_VALUE_08BIT, GC4023_MODE_SW_STANDBY);
}

static int gc4023_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc4023 *gc4023 = to_gc4023(sd);
	struct i2c_client *client = gc4023->client;
	int ret = 0;

	mutex_lock(&gc4023->mutex);
	on = !!on;
	if (on == gc4023->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc4023_start_stream(gc4023);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc4023_stop_stream(gc4023);
		pm_runtime_put(&client->dev);
	}

	gc4023->streaming = on;

unlock_and_return:
	mutex_unlock(&gc4023->mutex);

	return ret;
}

static int gc4023_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc4023 *gc4023 = to_gc4023(sd);
	struct i2c_client *client = gc4023->client;
	int ret = 0;

	mutex_lock(&gc4023->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc4023->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc4023_write_array(gc4023->client, gc4023_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc4023->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc4023->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc4023->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc4023_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC4023_XVCLK_FREQ / 1000 / 1000);
}

static int __gc4023_power_on(struct gc4023 *gc4023)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc4023->client->dev;

	if (!IS_ERR_OR_NULL(gc4023->pins_default)) {
		ret = pinctrl_select_state(gc4023->pinctrl,
					   gc4023->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc4023->xvclk, GC4023_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc4023->xvclk) != GC4023_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc4023->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(gc4023->reset_gpio))
		gpiod_set_value_cansleep(gc4023->reset_gpio, 0);

	if (!IS_ERR(gc4023->pwdn_gpio))
		gpiod_set_value_cansleep(gc4023->pwdn_gpio, 0);

	usleep_range(500, 1000);
	ret = regulator_bulk_enable(GC4023_NUM_SUPPLIES, gc4023->supplies);

	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(gc4023->pwren_gpio))
		gpiod_set_value_cansleep(gc4023->pwren_gpio, 1);

	usleep_range(1000, 1100);
	if (!IS_ERR(gc4023->pwdn_gpio))
		gpiod_set_value_cansleep(gc4023->pwdn_gpio, 1);
	usleep_range(100, 150);
	if (!IS_ERR(gc4023->reset_gpio))
		gpiod_set_value_cansleep(gc4023->reset_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc4023_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc4023->xvclk);

	return ret;
}

static void __gc4023_power_off(struct gc4023 *gc4023)
{
	int ret;
	struct device *dev = &gc4023->client->dev;

	if (!IS_ERR(gc4023->pwdn_gpio))
		gpiod_set_value_cansleep(gc4023->pwdn_gpio, 0);
	clk_disable_unprepare(gc4023->xvclk);
	if (!IS_ERR(gc4023->reset_gpio))
		gpiod_set_value_cansleep(gc4023->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(gc4023->pins_sleep)) {
		ret = pinctrl_select_state(gc4023->pinctrl,
					   gc4023->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(GC4023_NUM_SUPPLIES, gc4023->supplies);
	if (!IS_ERR(gc4023->pwren_gpio))
		gpiod_set_value_cansleep(gc4023->pwren_gpio, 0);
}

static int gc4023_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc4023 *gc4023 = to_gc4023(sd);

	return __gc4023_power_on(gc4023);
}

static int gc4023_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc4023 *gc4023 = to_gc4023(sd);

	__gc4023_power_off(gc4023);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc4023_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc4023 *gc4023 = to_gc4023(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc4023_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc4023->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc4023->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int gc4023_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc4023 *gc4023 = to_gc4023(sd);

	if (fie->index >= gc4023->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH 2560
#define DST_HEIGHT 1440

/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */

static int gc4023_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct gc4023 *gc4023 = to_gc4023(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = CROP_START(gc4023->cur_mode->width, DST_WIDTH);
		sel->r.width = DST_WIDTH;
		sel->r.top = CROP_START(gc4023->cur_mode->height, DST_HEIGHT);
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops gc4023_pm_ops = {
	SET_RUNTIME_PM_OPS(gc4023_runtime_suspend,
			   gc4023_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc4023_internal_ops = {
	.open = gc4023_open,
};
#endif

static const struct v4l2_subdev_core_ops gc4023_core_ops = {
	.s_power = gc4023_s_power,
	.ioctl = gc4023_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc4023_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc4023_video_ops = {
	.s_stream = gc4023_s_stream,
	.g_frame_interval = gc4023_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc4023_pad_ops = {
	.enum_mbus_code = gc4023_enum_mbus_code,
	.enum_frame_size = gc4023_enum_frame_sizes,
	.enum_frame_interval = gc4023_enum_frame_interval,
	.get_fmt = gc4023_get_fmt,
	.set_fmt = gc4023_set_fmt,
	.get_selection = gc4023_get_selection,
	.get_mbus_config = gc4023_g_mbus_config,
};

static const struct v4l2_subdev_ops gc4023_subdev_ops = {
	.core	= &gc4023_core_ops,
	.video	= &gc4023_video_ops,
	.pad	= &gc4023_pad_ops,
};

static int gc4023_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc4023 *gc4023 = container_of(ctrl->handler,
					     struct gc4023, ctrl_handler);
	struct i2c_client *client = gc4023->client;
	s64 max;
	int ret = 0;
	int mirror = 0, flip = 0;

	/*Propagate change of current control to all related controls*/
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/*Update max exposure while meeting expected vblanking*/
		max = gc4023->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc4023->exposure,
					 gc4023->exposure->minimum,
					 max,
					 gc4023->exposure->step,
					 gc4023->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = gc4023_write_reg(gc4023->client, GC4023_REG_EXPOSURE_H,
				       GC4023_REG_VALUE_08BIT,
				       ctrl->val >> 8);
		ret |= gc4023_write_reg(gc4023->client, GC4023_REG_EXPOSURE_L,
					GC4023_REG_VALUE_08BIT,
					ctrl->val & 0xff);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = gc4023_set_gain_reg(gc4023, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		gc4023->cur_vts = ctrl->val + gc4023->cur_mode->height;
		ret = gc4023_write_reg(gc4023->client, GC4023_REG_VTS_H,
				       GC4023_REG_VALUE_08BIT,
				       gc4023->cur_vts >> 8);
		ret |= gc4023_write_reg(gc4023->client, GC4023_REG_VTS_L,
					GC4023_REG_VALUE_08BIT,
					gc4023->cur_vts & 0xff);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = gc4023_enable_test_pattern(gc4023, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = gc4023_read_reg(gc4023->client, GC4023_MIRROR_REG,
				      GC4023_REG_VALUE_08BIT, &mirror);
		if (ctrl->val)
			mirror |= GC4023_MIRROR_BIT_MASK;
		else
			mirror &= ~GC4023_MIRROR_BIT_MASK;
		ret |= gc4023_write_reg(gc4023->client, GC4023_MIRROR_REG,
					GC4023_REG_VALUE_08BIT, mirror);
		break;
	case V4L2_CID_VFLIP:
		ret = gc4023_read_reg(gc4023->client, GC4023_FLIP_REG,
				      GC4023_REG_VALUE_08BIT, &flip);
		if (ctrl->val)
			flip |= GC4023_FLIP_BIT_MASK;
		else
			flip &= ~GC4023_FLIP_BIT_MASK;
		ret |= gc4023_write_reg(gc4023->client, GC4023_FLIP_REG,
					GC4023_REG_VALUE_08BIT, flip);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc4023_ctrl_ops = {
	.s_ctrl = gc4023_set_ctrl,
};

static int gc4023_initialize_controls(struct gc4023 *gc4023)
{
	const struct gc4023_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc4023->ctrl_handler;
	mode = gc4023->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &gc4023->mutex;

	gc4023->link_freq = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
						   1, 0, link_freq_menu_items);

	gc4023->cur_link_freq = 0;
	gc4023->cur_pixel_rate = GC4023_PIXEL_RATE_LINEAR;


	__v4l2_ctrl_s_ctrl(gc4023->link_freq,
			   gc4023->cur_link_freq);

	gc4023->pixel_rate = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, GC4023_PIXEL_RATE_LINEAR, 1, GC4023_PIXEL_RATE_LINEAR);

	h_blank = mode->hts_def - mode->width;
	gc4023->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (gc4023->hblank)
		gc4023->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc4023->cur_vts = mode->vts_def;
	gc4023->vblank = v4l2_ctrl_new_std(handler, &gc4023_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   GC4023_VTS_MAX - mode->height,
					    1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc4023->exposure = v4l2_ctrl_new_std(handler, &gc4023_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     GC4023_EXPOSURE_MIN,
					     exposure_max,
					     GC4023_EXPOSURE_STEP,
					     mode->exp_def);

	gc4023->anal_gain = v4l2_ctrl_new_std(handler, &gc4023_ctrl_ops,
					      V4L2_CID_ANALOGUE_GAIN,
					      GC4023_GAIN_MIN,
					      GC4023_GAIN_MAX,
					      GC4023_GAIN_STEP,
					      GC4023_GAIN_DEFAULT);

	gc4023->test_pattern =
		v4l2_ctrl_new_std_menu_items(handler,
					     &gc4023_ctrl_ops,
				V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(gc4023_test_pattern_menu) - 1,
				0, 0, gc4023_test_pattern_menu);

	gc4023->h_flip = v4l2_ctrl_new_std(handler, &gc4023_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	gc4023->v_flip = v4l2_ctrl_new_std(handler, &gc4023_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&gc4023->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc4023->subdev.ctrl_handler = handler;
	gc4023->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc4023_check_sensor_id(struct gc4023 *gc4023,
				  struct i2c_client *client)
{
	struct device *dev = &gc4023->client->dev;
	u16 id = 0;
	u32 reg_H = 0;
	u32 reg_L = 0;
	int ret;

	ret = gc4023_read_reg(client, GC4023_REG_CHIP_ID_H,
			      GC4023_REG_VALUE_08BIT, &reg_H);
	ret |= gc4023_read_reg(client, GC4023_REG_CHIP_ID_L,
			       GC4023_REG_VALUE_08BIT, &reg_L);

	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (!(reg_H == (CHIP_ID >> 8) || reg_L == (CHIP_ID & 0xff))) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "detected gc%04x sensor\n", id);
	return 0;
}

static int gc4023_configure_regulators(struct gc4023 *gc4023)
{
	unsigned int i;

	for (i = 0; i < GC4023_NUM_SUPPLIES; i++)
		gc4023->supplies[i].supply = gc4023_supply_names[i];

	return devm_regulator_bulk_get(&gc4023->client->dev,
				       GC4023_NUM_SUPPLIES,
				       gc4023->supplies);
}

static int gc4023_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc4023 *gc4023;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	gc4023 = devm_kzalloc(dev, sizeof(*gc4023), GFP_KERNEL);
	if (!gc4023)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc4023->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc4023->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc4023->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc4023->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc4023->client = client;
	gc4023->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < gc4023->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			gc4023->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == gc4023->cfg_num)
		gc4023->cur_mode = &supported_modes[0];

	gc4023->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc4023->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc4023->pwren_gpio = devm_gpiod_get(dev, "pwren", GPIOD_OUT_LOW);
	if (IS_ERR(gc4023->pwren_gpio))
		dev_warn(dev, "Failed to get pwren-gpios\n");

	gc4023->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc4023->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc4023->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc4023->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	gc4023->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc4023->pinctrl)) {
		gc4023->pins_default =
			pinctrl_lookup_state(gc4023->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc4023->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc4023->pins_sleep =
			pinctrl_lookup_state(gc4023->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc4023->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = gc4023_configure_regulators(gc4023);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&gc4023->mutex);

	sd = &gc4023->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc4023_subdev_ops);
	ret = gc4023_initialize_controls(gc4023);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc4023_power_on(gc4023);
	if (ret)
		goto err_free_handler;

	usleep_range(3000, 4000);

	ret = gc4023_check_sensor_id(gc4023, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc4023_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc4023->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc4023->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc4023->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc4023->module_index, facing,
		 GC4023_NAME, dev_name(sd->dev));
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
	__gc4023_power_off(gc4023);
err_free_handler:
	v4l2_ctrl_handler_free(&gc4023->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc4023->mutex);

	return ret;
}

static int gc4023_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc4023 *gc4023 = to_gc4023(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc4023->ctrl_handler);
	mutex_destroy(&gc4023->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc4023_power_off(gc4023);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc4023_of_match[] = {
	{ .compatible = "galaxycore,gc4023" },
	{},
};
MODULE_DEVICE_TABLE(of, gc4023_of_match);
#endif

static const struct i2c_device_id gc4023_match_id[] = {
	{ "galaxycore,gc4023", 0 },
	{ },
};

static struct i2c_driver gc4023_i2c_driver = {
	.driver = {
		.name = GC4023_NAME,
		.pm = &gc4023_pm_ops,
		.of_match_table = of_match_ptr(gc4023_of_match),
	},
	.probe		= &gc4023_probe,
	.remove		= &gc4023_remove,
	.id_table	= gc4023_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc4023_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc4023_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("galaxycore gc4023 sensor driver");
MODULE_LICENSE("GPL");
