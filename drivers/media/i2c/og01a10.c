// SPDX-License-Identifier: GPL-2.0
/*
 * og01a10 driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 * V0.1.0: MIPI is ok.
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
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
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

#define MIPI_FREQ_600M			600000000

#define OG01A10_PIXEL_RATE		(MIPI_FREQ_600M * 2 / 10 * 2)
#define OG01A10_XVCLK_FREQ		24000000

#define CHIP_ID				0x4701
#define OG01A10_REG_CHIP_ID		0x300a

#define OG01A10_REG_CTRL_MODE		0x0100
#define OG01A10_MODE_SW_STANDBY		0x0
#define OG01A10_MODE_STREAMING		BIT(0)

#define OG01A10_REG_EXPOSURE		0x3501
#define	OG01A10_EXPOSURE_MIN		1
#define	OG01A10_EXPOSURE_STEP		1
#define OG01A10_VTS_MAX			0xffff

#define OG01A10_REG_AGAIN		0x3508
#define OG01A10_REG_DGAIN		0x350A
#define	ANALOG_GAIN_MIN			0x100
#define	ANALOG_GAIN_MAX			0xE880 //0X100*15.5*15
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x400

#define OG01A10_REG_TEST_PATTERN	0x5100
#define	OG01A10_TEST_PATTERN_ENABLE	0x80
#define	OG01A10_TEST_PATTERN_DISABLE	0x00

#define OG01A10_REG_VTS			0x320e

#define REG_NULL			0xFFFF

#define OG01A10_REG_VALUE_08BIT		1
#define OG01A10_REG_VALUE_16BIT		2
#define OG01A10_REG_VALUE_24BIT		3

#define OG01A10_NAME			"og01a10"

#define PIX_FORMAT MEDIA_BUS_FMT_SRGGB10_1X10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define OG01A10_LANES			2
#define OG01A10_BITS_PER_SAMPLE		10

static const char * const og01a10_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OG01A10_NUM_SUPPLIES ARRAY_SIZE(og01a10_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct og01a10_mode {
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

struct og01a10 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*pwdn_gpio;
	struct gpio_desc	*reset_gpio;
	struct regulator_bulk_data supplies[OG01A10_NUM_SUPPLIES];
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
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct og01a10_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_og01a10(sd) container_of(sd, struct og01a10, subdev)

/*
 * Xclk 24Mhz
 * Pclk 120Mhz
 * linelength 1696(0x06a0)
 * framelength 2832(0x0b10)
 * grabwindow_width 1280
 * grabwindow_height 1024
 * mipi 2 lane
 * max_framerate 25fps
 * mipi_datarate per lane 1000Mbps
 */
static const struct regval og01a10_global_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x0300, 0x0a},
	{0x0301, 0x29},
	{0x0302, 0x31},
	{0x0303, 0x04},
	{0x0304, 0x00},
	{0x0305, 0xfa},
	{0x0306, 0x00},
	{0x0307, 0x01},
	{0x0308, 0x02},
	{0x0309, 0x00},
	{0x0310, 0x00},
	{0x0311, 0x00},
	{0x0312, 0x07},
	{0x0313, 0x00},
	{0x0314, 0x00},
	{0x0315, 0x00},
	{0x0320, 0x02},
	{0x0321, 0x01},
	{0x0322, 0x01},
	{0x0323, 0x04},
	{0x0324, 0x01},
	{0x0325, 0xc2},
	{0x0326, 0xce},
	{0x0327, 0x04},
	{0x0329, 0x02},
	{0x032a, 0x04},
	{0x032b, 0x04},
	{0x032c, 0x02},
	{0x032d, 0x01},
	{0x032e, 0x00},
	{0x300d, 0x02},
	{0x300e, 0x04},
	{0x3021, 0x08},
	{0x301e, 0x03},
	{0x3103, 0x00},
	{0x3106, 0x08},
	{0x3107, 0x40},
	{0x3216, 0x01},
	{0x3217, 0x00},
	{0x3218, 0xc0},
	{0x3219, 0x55},
	{0x3500, 0x00},
	{0x3501, 0x01},
	{0x3502, 0x8a},
	{0x3506, 0x01},
	{0x3507, 0x72},
	{0x3508, 0x04},
	{0x3509, 0x00},
	{0x350a, 0x01},
	{0x350b, 0x00},
	{0x350c, 0x00},
	{0x3541, 0x00},
	{0x3542, 0x40},
	{0x3605, 0xe0},
	{0x3606, 0x41},
	{0x3614, 0x20},
	{0x3620, 0x0b},
	{0x3630, 0x07},
	{0x3636, 0xa0},
	{0x3637, 0xf9},
	{0x3638, 0x09},
	{0x3639, 0x38},
	{0x363f, 0x09},
	{0x3640, 0x17},
	{0x3662, 0x04},
	{0x3665, 0x80},
	{0x3670, 0x68},
	{0x3674, 0x00},
	{0x3677, 0x3f},
	{0x3679, 0x00},
	{0x369f, 0x19},
	{0x36a0, 0x03},
	{0x36a2, 0x19},
	{0x36a3, 0x03},
	{0x370d, 0x66},
	{0x370f, 0x00},
	{0x3710, 0x03},
	{0x3715, 0x03},
	{0x3716, 0x03},
	{0x3717, 0x06},
	{0x3733, 0x00},
	{0x3778, 0x00},
	{0x37a8, 0x0f},
	{0x37a9, 0x01},
	{0x37aa, 0x07},
	{0x37bd, 0x1c},
	{0x37c1, 0x2f},
	{0x37c3, 0x09},
	{0x37c8, 0x1d},
	{0x37ca, 0x30},
	{0x37df, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x05},
	{0x3805, 0x0f},
	{0x3806, 0x04},
	{0x3807, 0x0f},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x04},
	{0x380b, 0x00},
	{0x380c, 0x06},
	{0x380d, 0xa0},
	{0x380e, 0x0b},
	{0x380f, 0x10},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x08},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x44},
	{0x3821, 0x04},
	{0x3826, 0x00},
	{0x3827, 0x00},
	{0x382a, 0x08},
	{0x382b, 0x52},
	{0x382d, 0xba},
	{0x383d, 0x14},
	{0x384a, 0xa2},
	{0x3866, 0x0e},
	{0x3867, 0x07},
	{0x3884, 0x00},
	{0x3885, 0x08},
	{0x3893, 0x68},
	{0x3894, 0x2a},
	{0x3898, 0x00},
	{0x3899, 0x31},
	{0x389a, 0x04},
	{0x389b, 0x00},
	{0x389c, 0x0b},
	{0x389d, 0xad},
	{0x389f, 0x08},
	{0x38a0, 0x00},
	{0x38a1, 0x00},
	{0x38a8, 0x70},
	{0x38ac, 0xea},
	{0x38b2, 0x00},
	{0x38b3, 0x08},
	{0x38bc, 0x20},
	{0x38c4, 0x0c},
	{0x38c5, 0x3a},
	{0x38c7, 0x3a},
	{0x38e1, 0xc0},
	{0x38ec, 0x3c},
	{0x38f0, 0x09},
	{0x38f1, 0x6f},
	{0x38fe, 0x3c},
	{0x391e, 0x01},
	{0x391f, 0x00},
	{0x3920, 0xff},
	{0x3921, 0x00},
	{0x3922, 0x00},
	{0x3923, 0x00},
	{0x3924, 0x05},
	{0x3925, 0x00},
	{0x3926, 0x00},
	{0x3927, 0x00},
	{0x3928, 0x1a},
	{0x3929, 0x01},
	{0x392a, 0xb4},
	{0x392b, 0x00},
	{0x392c, 0x10},
	{0x392f, 0x40},
	{0x4000, 0xcf},
	{0x4003, 0x40},
	{0x4008, 0x00},
	{0x4009, 0x07},
	{0x400a, 0x02},
	{0x400b, 0x54},
	{0x400c, 0x00},
	{0x400d, 0x07},
	{0x4010, 0xc0},
	{0x4012, 0x02},
	{0x4014, 0x04},
	{0x4015, 0x04},
	{0x4017, 0x02},
	{0x4042, 0x01},
	{0x4306, 0x04},
	{0x4307, 0x12},
	{0x4509, 0x00},
	{0x450b, 0x83},
	{0x4604, 0x68},
	{0x4608, 0x0a},
	{0x4700, 0x06},
	{0x4800, 0x64},
	{0x481b, 0x3c},
	{0x4825, 0x32},
	{0x4833, 0x18},
	{0x4837, 0x10},
	{0x4850, 0x40},
	{0x4860, 0x00},
	{0x4861, 0xec},
	{0x4864, 0x00},
	{0x4883, 0x00},
	{0x4888, 0x90},
	{0x4889, 0x05},
	{0x488b, 0x04},
	{0x4f00, 0x04},
	{0x4f10, 0x04},
	{0x4f21, 0x01},
	{0x4f22, 0x40},
	{0x4f23, 0x44},
	{0x4f24, 0x51},
	{0x4f25, 0x41},
	{0x5000, 0x1f},
	{0x500a, 0x00},
	{0x5100, 0x00},
	{0x5111, 0x20},
	{0x3020, 0x20},
	{0x3613, 0x03},
	{0x38c9, 0x02},
	{0x5304, 0x01},
	{0x3620, 0x08},
	{0x3639, 0x58},
	{0x363a, 0x10},
	{0x3674, 0x04},
	{0x3780, 0xff},
	{0x3781, 0xff},
	{0x3782, 0x00},
	{0x3783, 0x01},
	{0x3798, 0xa3},
	{0x37aa, 0x10},
	{0x38a8, 0xf0},
	{0x38c4, 0x09},
	{0x38c5, 0xb0},
	{0x38df, 0x80},
	{0x38ff, 0x05},
	{0x4010, 0xf1},
	{0x4011, 0x70},
	{0x3667, 0x80},
	{0x4d00, 0x4a},
	{0x4d01, 0x18},
	{0x4d02, 0xbb},
	{0x4d03, 0xde},
	{0x4d04, 0x93},
	{0x4d05, 0xff},
	{0x4d09, 0x0a},
	{0x4f22, 0x00},
	{0x37aa, 0x16},
	{0x3606, 0x42},
	{0x3605, 0x00},
	{0x36a2, 0x17},
	{0x300d, 0x0a},
	{0x4d00, 0x4d},
	{0x4d01, 0x95},
	{0x3d8C, 0x70},
	{0x3d8d, 0xE9},
	{0x5300, 0x00},
	{0x5301, 0x10},
	{0x5302, 0x00},
	{0x5303, 0xE3},
	{0x3d88, 0x00},
	{0x3d89, 0x10},
	{0x3d8a, 0x00},
	{0x3d8b, 0xE3},
	{REG_NULL, 0x00},
};

static const struct og01a10_mode supported_modes[] = {
	{
		.width = 1280,
		.height = 1024,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.exp_def = 0x0b00,
		.hts_def = 0x06a0,
		.vts_def = 0x0b10,
		.reg_list = og01a10_global_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const char * const og01a10_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

#define OG01A10_LINK_FREQ_240MHZ	(240 * 1000 * 1000)

static const s64 link_freq_menu_items[] = {
	OG01A10_LINK_FREQ_240MHZ
};

/* Write registers up to 4 at a time */
static int og01a10_write_reg(struct i2c_client *client,
	u16 reg, u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;
	u32 ret;

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

	ret = i2c_master_send(client, buf, len + 2);
	if (ret != len + 2)
		return -EIO;

	return 0;
}

static int og01a10_write_array(struct i2c_client *client,
	const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = og01a10_write_reg(client, regs[i].addr,
					OG01A10_REG_VALUE_08BIT, regs[i].val);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int og01a10_read_reg(struct i2c_client *client,
	u16 reg, unsigned int len, u32 *val)
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

static int og01a10_get_reso_dist(const struct og01a10_mode *mode,
	struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct og01a10_mode *
	og01a10_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = og01a10_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}
	return &supported_modes[cur_best_fit];
}

static int og01a10_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct og01a10 *og01a10 = to_og01a10(sd);
	const struct og01a10_mode *mode;
	s64 h_blank, vblank_def;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	mutex_lock(&og01a10->mutex);

	mode = og01a10_find_best_fit(fmt);
	fmt->format.code = PIX_FORMAT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&og01a10->mutex);
		return -ENOTTY;
#endif
	} else {
		og01a10->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(og01a10->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(og01a10->vblank, vblank_def,
					 OG01A10_VTS_MAX - mode->height,
					 1, vblank_def);
		if (mode->hdr_mode == NO_HDR) {
			dst_link_freq = 0;
			dst_pixel_rate = OG01A10_PIXEL_RATE;
		}
		__v4l2_ctrl_s_ctrl_int64(og01a10->pixel_rate,
					 dst_pixel_rate);
		__v4l2_ctrl_s_ctrl(og01a10->link_freq,
				   dst_link_freq);
	}

	mutex_unlock(&og01a10->mutex);

	return 0;
}

static int og01a10_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct og01a10 *og01a10 = to_og01a10(sd);
	const struct og01a10_mode *mode = og01a10->cur_mode;

	mutex_lock(&og01a10->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&og01a10->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = PIX_FORMAT;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&og01a10->mutex);

	return 0;
}

static int og01a10_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = PIX_FORMAT;

	return 0;
}

static int og01a10_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != PIX_FORMAT)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int og01a10_enable_test_pattern(struct og01a10 *og01a10, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OG01A10_TEST_PATTERN_ENABLE;
	else
		val = OG01A10_TEST_PATTERN_DISABLE;

	return og01a10_write_reg(og01a10->client, OG01A10_REG_TEST_PATTERN,
				 OG01A10_REG_VALUE_08BIT, val);
}

static void og01a10_get_module_inf(struct og01a10 *og01a10,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, OG01A10_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, og01a10->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, og01a10->len_name, sizeof(inf->base.lens));
}

static long og01a10_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct og01a10 *og01a10 = to_og01a10(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		og01a10_get_module_inf(og01a10, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		if (hdr_cfg->hdr_mode != 0)
			ret = -1;
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = og01a10->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);

		if (stream)
			ret = og01a10_write_reg(og01a10->client, OG01A10_REG_CTRL_MODE,
				OG01A10_REG_VALUE_08BIT, OG01A10_MODE_STREAMING);
		else
			ret = og01a10_write_reg(og01a10->client, OG01A10_REG_CTRL_MODE,
				OG01A10_REG_VALUE_08BIT, OG01A10_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long og01a10_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}
		ret = og01a10_ioctl(sd, cmd, inf);
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
		ret = og01a10_ioctl(sd, cmd, hdr);
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
		if (copy_from_user(hdr, up, sizeof(*hdr)))
			return -EFAULT;
		ret = og01a10_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = og01a10_ioctl(sd, cmd, &stream);
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

static int __og01a10_start_stream(struct og01a10 *og01a10)
{
	int ret;

	ret = og01a10_write_array(og01a10->client, og01a10->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&og01a10->mutex);
	ret = v4l2_ctrl_handler_setup(&og01a10->ctrl_handler);
	mutex_lock(&og01a10->mutex);
	if (ret)
		return ret;

	return og01a10_write_reg(og01a10->client, OG01A10_REG_CTRL_MODE,
			OG01A10_REG_VALUE_08BIT, OG01A10_MODE_STREAMING);
}

static int __og01a10_stop_stream(struct og01a10 *og01a10)
{
	return og01a10_write_reg(og01a10->client, OG01A10_REG_CTRL_MODE,
			OG01A10_REG_VALUE_08BIT, OG01A10_MODE_SW_STANDBY);
}

static int og01a10_s_stream(struct v4l2_subdev *sd, int on)
{
	struct og01a10 *og01a10 = to_og01a10(sd);
	struct i2c_client *client = og01a10->client;
	int ret = 0;

	mutex_lock(&og01a10->mutex);
	on = !!on;
	if (on == og01a10->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __og01a10_start_stream(og01a10);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__og01a10_stop_stream(og01a10);
		pm_runtime_put(&client->dev);
	}

	og01a10->streaming = on;

unlock_and_return:
	mutex_unlock(&og01a10->mutex);

	return ret;
}

static int og01a10_s_power(struct v4l2_subdev *sd, int on)
{
	struct og01a10 *og01a10 = to_og01a10(sd);
	struct i2c_client *client = og01a10->client;
	int ret = 0;

	mutex_lock(&og01a10->mutex);

	/* If the power state is not modified - no work to do. */
	if (og01a10->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		og01a10->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		og01a10->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&og01a10->mutex);

	return ret;
}

static int og01a10_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct og01a10 *og01a10 = to_og01a10(sd);
	const struct og01a10_mode *mode = og01a10->cur_mode;

	fi->interval = mode->max_fps;
	return 0;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 og01a10_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OG01A10_XVCLK_FREQ / 1000 / 1000);
}

static int __og01a10_power_on(struct og01a10 *og01a10)
{
	int ret;
	u32 delay_us;
	struct device *dev = &og01a10->client->dev;

	if (!IS_ERR_OR_NULL(og01a10->pins_default)) {
		ret = pinctrl_select_state(og01a10->pinctrl,
					   og01a10->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = clk_set_rate(og01a10->xvclk, OG01A10_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(og01a10->xvclk) != OG01A10_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(og01a10->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(OG01A10_NUM_SUPPLIES, og01a10->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}
	if (!IS_ERR(og01a10->reset_gpio))
		gpiod_set_value_cansleep(og01a10->reset_gpio, 1);
	if (!IS_ERR(og01a10->reset_gpio))
		gpiod_set_value_cansleep(og01a10->reset_gpio, 1);
	usleep_range(1000, 2000);
	if (!IS_ERR(og01a10->pwdn_gpio))
		gpiod_set_value_cansleep(og01a10->pwdn_gpio, 0);
	else
		dev_err(dev, "Failed to pwdn_gpio 1\n");
	if (!IS_ERR(og01a10->reset_gpio))
		gpiod_set_value_cansleep(og01a10->reset_gpio, 0);
	else
		dev_err(dev, "Failed to reset_gpio 1\n");
	usleep_range(10000, 20000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = og01a10_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(og01a10->xvclk);

	return ret;
}

static void __og01a10_power_off(struct og01a10 *og01a10)
{
	int ret;

	if (!IS_ERR(og01a10->reset_gpio))
		gpiod_set_value_cansleep(og01a10->reset_gpio, 1);

	if (!IS_ERR(og01a10->pwdn_gpio))
		gpiod_set_value_cansleep(og01a10->pwdn_gpio, 0);

	clk_disable_unprepare(og01a10->xvclk);
	if (!IS_ERR_OR_NULL(og01a10->pins_sleep)) {
		ret = pinctrl_select_state(og01a10->pinctrl,
					   og01a10->pins_sleep);
		if (ret < 0)
			dev_dbg(&og01a10->client->dev, "could not set pins\n");
	}
	regulator_bulk_disable(OG01A10_NUM_SUPPLIES, og01a10->supplies);
}

static int og01a10_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct og01a10 *og01a10 = to_og01a10(sd);

	return __og01a10_power_on(og01a10);
}

static int og01a10_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct og01a10 *og01a10 = to_og01a10(sd);

	__og01a10_power_off(og01a10);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int og01a10_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct og01a10 *og01a10 = to_og01a10(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct og01a10_mode *def_mode = &supported_modes[0];

	mutex_lock(&og01a10->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = PIX_FORMAT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&og01a10->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int og01a10_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != PIX_FORMAT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static int og01a10_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (OG01A10_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops og01a10_pm_ops = {
	SET_RUNTIME_PM_OPS(og01a10_runtime_suspend,
			   og01a10_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops og01a10_internal_ops = {
	.open = og01a10_open,
};
#endif

static const struct v4l2_subdev_core_ops og01a10_core_ops = {
	.s_power = og01a10_s_power,
	.ioctl = og01a10_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = og01a10_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops og01a10_video_ops = {
	.s_stream = og01a10_s_stream,
	.g_frame_interval = og01a10_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops og01a10_pad_ops = {
	.enum_mbus_code = og01a10_enum_mbus_code,
	.enum_frame_size = og01a10_enum_frame_sizes,
	.enum_frame_interval = og01a10_enum_frame_interval,
	.get_fmt = og01a10_get_fmt,
	.set_fmt = og01a10_set_fmt,
	.get_mbus_config = og01a10_g_mbus_config,
};

static const struct v4l2_subdev_ops og01a10_subdev_ops = {
	.core	= &og01a10_core_ops,
	.video	= &og01a10_video_ops,
	.pad	= &og01a10_pad_ops,
};

static int og01a10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct og01a10 *og01a10 = container_of(ctrl->handler,
					       struct og01a10, ctrl_handler);
	struct i2c_client *client = og01a10->client;
	s64 max;
	int ret = 0;
	u32 again, dgain;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = og01a10->cur_mode->height + ctrl->val - 6;
		__v4l2_ctrl_modify_range(og01a10->exposure,
					 og01a10->exposure->minimum, max,
					 og01a10->exposure->step,
					 og01a10->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = og01a10_write_reg(og01a10->client, OG01A10_REG_EXPOSURE,
			OG01A10_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (ctrl->val > 3968) {
			again = 3968;
			dgain = 1024 * ctrl->val / 3968;
		} else {
			again = ctrl->val;
			dgain = 1024;
		}
		ret = og01a10_write_reg(og01a10->client, OG01A10_REG_AGAIN,
			OG01A10_REG_VALUE_16BIT, again);
		ret |= og01a10_write_reg(og01a10->client, OG01A10_REG_DGAIN,
			OG01A10_REG_VALUE_24BIT, dgain << 6);
		break;
	case V4L2_CID_VBLANK:
		ret = og01a10_write_reg(og01a10->client, OG01A10_REG_VTS,
					OG01A10_REG_VALUE_16BIT,
					ctrl->val + og01a10->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = og01a10_enable_test_pattern(og01a10, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops og01a10_ctrl_ops = {
	.s_ctrl = og01a10_set_ctrl,
};

static int og01a10_initialize_controls(struct og01a10 *og01a10)
{
	const struct og01a10_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &og01a10->ctrl_handler;
	mode = og01a10->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &og01a10->mutex;

	og01a10->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
		V4L2_CID_LINK_FREQ,
		ARRAY_SIZE(link_freq_menu_items) - 1, 0,
		link_freq_menu_items);
	__v4l2_ctrl_s_ctrl(og01a10->link_freq, 0);

	og01a10->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_PIXEL_RATE, 0, OG01A10_PIXEL_RATE, 1, OG01A10_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	og01a10->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (og01a10->hblank)
		og01a10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	og01a10->vblank = v4l2_ctrl_new_std(handler, &og01a10_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OG01A10_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 6;
	og01a10->exposure = v4l2_ctrl_new_std(handler, &og01a10_ctrl_ops,
				V4L2_CID_EXPOSURE, OG01A10_EXPOSURE_MIN,
				exposure_max, OG01A10_EXPOSURE_STEP,
				mode->exp_def);

	og01a10->anal_gain = v4l2_ctrl_new_std(handler, &og01a10_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	og01a10->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&og01a10_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(og01a10_test_pattern_menu) - 1,
				0, 0, og01a10_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&og01a10->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	og01a10->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int og01a10_check_sensor_id(struct og01a10 *og01a10,
				  struct i2c_client *client)
{
	struct device *dev = &og01a10->client->dev;
	u32 id = 0;
	int ret;

	ret = og01a10_read_reg(client, OG01A10_REG_CHIP_ID,
			       OG01A10_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OG01A10 CHIP ID = 0x%04x sensor\n", CHIP_ID);

	return 0;
}

static int og01a10_configure_regulators(struct og01a10 *og01a10)
{
	unsigned int i;

	for (i = 0; i < OG01A10_NUM_SUPPLIES; i++)
		og01a10->supplies[i].supply = og01a10_supply_names[i];

	return devm_regulator_bulk_get(&og01a10->client->dev,
				       OG01A10_NUM_SUPPLIES,
				       og01a10->supplies);
}

static int og01a10_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct og01a10 *og01a10;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	og01a10 = devm_kzalloc(dev, sizeof(*og01a10), GFP_KERNEL);
	if (!og01a10)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &og01a10->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &og01a10->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &og01a10->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &og01a10->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	og01a10->client = client;
	og01a10->cur_mode = &supported_modes[0];

	og01a10->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(og01a10->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	og01a10->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(og01a10->reset_gpio))
		dev_err(dev, "Failed to get reset-gpios\n");

	og01a10->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(og01a10->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");
	ret = og01a10_configure_regulators(og01a10);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	og01a10->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(og01a10->pinctrl)) {
		og01a10->pins_default =
			pinctrl_lookup_state(og01a10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(og01a10->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		og01a10->pins_sleep =
			pinctrl_lookup_state(og01a10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(og01a10->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}
	mutex_init(&og01a10->mutex);

	sd = &og01a10->subdev;
	v4l2_i2c_subdev_init(sd, client, &og01a10_subdev_ops);
	ret = og01a10_initialize_controls(og01a10);
	if (ret)
		goto err_destroy_mutex;

	ret = __og01a10_power_on(og01a10);
	if (ret)
		goto err_free_handler;

	ret = og01a10_check_sensor_id(og01a10, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &og01a10_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	og01a10->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &og01a10->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(og01a10->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 og01a10->module_index, facing,
		 OG01A10_NAME, dev_name(sd->dev));
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
	__og01a10_power_off(og01a10);
err_free_handler:
	v4l2_ctrl_handler_free(&og01a10->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&og01a10->mutex);

	return ret;
}

static int og01a10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct og01a10 *og01a10 = to_og01a10(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&og01a10->ctrl_handler);
	mutex_destroy(&og01a10->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__og01a10_power_off(og01a10);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id og01a10_of_match[] = {
	{ .compatible = "ovti,og01a10" },
	{},
};
MODULE_DEVICE_TABLE(of, og01a10_of_match);
#endif

static const struct i2c_device_id og01a10_match_id[] = {
	{ "ovti,og01a10", 0 },
	{ },
};

static struct i2c_driver og01a10_i2c_driver = {
	.driver = {
		.name = OG01A10_NAME,
		.pm = &og01a10_pm_ops,
		.of_match_table = of_match_ptr(og01a10_of_match),
	},
	.probe		= &og01a10_probe,
	.remove		= &og01a10_remove,
	.id_table	= og01a10_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&og01a10_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&og01a10_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Ovti og01a10 sensor driver");
MODULE_LICENSE("GPL");
