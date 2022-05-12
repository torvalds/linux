// SPDX-License-Identifier: GPL-2.0
/*
 * s5k3l6xx camera driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01
 * 1.add flip and mirror support
 * 2.fix stream on sequential
 *
 */

// #define DEBUG
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
#include <linux/compat.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define S5K3L6XX_LINK_FREQ_600MHZ	600000000U
#define S5K3L6XX_LINK_FREQ_284MHZ	284000000U
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define S5K3L6XX_PIXEL_RATE		(S5K3L6XX_LINK_FREQ_600MHZ * 2LL * 4LL / 10LL)
#define S5K3L6XX_XVCLK_FREQ		24000000

#define CHIP_ID				0x30c6
#define S5K3L6XX_REG_CHIP_ID		0x0000

#define S5K3L6XX_REG_CTRL_MODE		0x0100
#define S5K3L6XX_MODE_SW_STANDBY	0x0
#define S5K3L6XX_MODE_STREAMING		BIT(0)
#define S5K3L6XX_REG_STREAM_ON		0x3C1E

#define S5K3L6XX_REG_EXPOSURE		0x0202
#define	S5K3L6XX_EXPOSURE_MIN		1
#define	S5K3L6XX_EXPOSURE_STEP		1
#define S5K3L6XX_VTS_MAX		0xfff7

#define S5K3L6XX_REG_ANALOG_GAIN	0x0204
#define S5K3L6XX_GAIN_MIN		0x20
#define S5K3L6XX_GAIN_MAX		0x200
#define S5K3L6XX_GAIN_STEP		1
#define S5K3L6XX_GAIN_DEFAULT		0x100

#define S5K3L6XX_REG_TEST_PATTERN	0x0601
#define	S5K3L6XX_TEST_PATTERN_ENABLE	0x80
#define	S5K3L6XX_TEST_PATTERN_DISABLE	0x0

#define S5K3L6XX_REG_VTS		0x0340

#define REG_NULL			0xFFFF

#define S5K3L6XX_REG_VALUE_08BIT	1
#define S5K3L6XX_REG_VALUE_16BIT	2
#define S5K3L6XX_REG_VALUE_24BIT	3

#define S5K3L6XX_LANES			4
#define S5K3L6XX_BITS_PER_SAMPLE	10

#define S5K3L6XX_CHIP_REVISION_REG	0x0002

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define S5K3L6XX_NAME			"s5k3l6xx"

// #define S5K3L6XX_MIRROR
// #define S5K3L6XX_FLIP
// #define S5K3L6XX_FLIP_MIRROR
#ifdef S5K3L6XX_MIRROR
#define S5K3L6XX_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SRGGB10_1X10
#elif defined S5K3L6XX_FLIP
#define S5K3L6XX_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SBGGR10_1X10
#elif defined S5K3L6XX_FLIP_MIRROR
#define S5K3L6XX_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SGBRG10_1X10
#else
#define S5K3L6XX_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SGRBG10_1X10
#endif

static const char * const s5k3l6xx_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define S5K3L6XX_NUM_SUPPLIES ARRAY_SIZE(s5k3l6xx_supply_names)

struct regval {
	u16 addr;
	u16 val;
};

struct s5k3l6xx_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_idx;
	u32 bpp;
	const struct regval *reg_list;
};

struct s5k3l6xx {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[S5K3L6XX_NUM_SUPPLIES];

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
	const struct s5k3l6xx_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_s5k3l6xx(sd) container_of(sd, struct s5k3l6xx, subdev)

static const struct regval s5k3l6xx_4208x3120_30fps_regs[] = {
#ifdef S5K3L6XX_MIRROR
	{0x0100, 0x0001},
#elif defined S5K3L6XX_FLIP
	{0x0100, 0x0002},
#elif defined S5K3L6XX_FLIP_MIRROR
	{0x0100, 0x0003},
#else
	{0x0100, 0x0000},
#endif
	{0x0000, 0x0060},
	{0x0000, 0x30C6},
	{0x0A02, 0x3400},
	{0x3084, 0x1314},
	{0x3266, 0x0001},
	{0x3242, 0x2020},
	{0x306A, 0x2F4C},
	{0x306C, 0xCA01},
	{0x307A, 0x0D20},
	{0x309E, 0x002D},
	{0x3072, 0x0013},
	{0x3074, 0x0977},
	{0x3076, 0x9411},
	{0x3024, 0x0016},
	{0x3070, 0x3D00},
	{0x3002, 0x0E00},
	{0x3006, 0x1000},
	{0x300A, 0x0C00},
	{0x3010, 0x0400},
	{0x3018, 0xC500},
	{0x303A, 0x0204},
	{0x3452, 0x0001},
	{0x3454, 0x0001},
	{0x3456, 0x0001},
	{0x3458, 0x0001},
	{0x345a, 0x0002},
	{0x345C, 0x0014},
	{0x345E, 0x0002},
	{0x3460, 0x0014},
	{0x3464, 0x0006},
	{0x3466, 0x0012},
	{0x3468, 0x0012},
	{0x346A, 0x0012},
	{0x346C, 0x0012},
	{0x346E, 0x0012},
	{0x3470, 0x0012},
	{0x3472, 0x0008},
	{0x3474, 0x0004},
	{0x3476, 0x0044},
	{0x3478, 0x0004},
	{0x347A, 0x0044},
	{0x347E, 0x0006},
	{0x3480, 0x0010},
	{0x3482, 0x0010},
	{0x3484, 0x0010},
	{0x3486, 0x0010},
	{0x3488, 0x0010},
	{0x348A, 0x0010},
	{0x348E, 0x000C},
	{0x3490, 0x004C},
	{0x3492, 0x000C},
	{0x3494, 0x004C},
	{0x3496, 0x0020},
	{0x3498, 0x0006},
	{0x349A, 0x0008},
	{0x349C, 0x0008},
	{0x349E, 0x0008},
	{0x34A0, 0x0008},
	{0x34A2, 0x0008},
	{0x34A4, 0x0008},
	{0x34A8, 0x001A},
	{0x34AA, 0x002A},
	{0x34AC, 0x001A},
	{0x34AE, 0x002A},
	{0x34B0, 0x0080},
	{0x34B2, 0x0006},
	{0x32A2, 0x0000},
	{0x32A4, 0x0000},
	{0x32A6, 0x0000},
	{0x32A8, 0x0000},
	{0x0344, 0x0008},
	{0x0346, 0x0008},
	{0x0348, 0x1077},
	{0x034A, 0x0C37},
	{0x034C, 0x1070},
	{0x034E, 0x0C30},
	{0x0900, 0x0000},
	{0x0380, 0x0001},
	{0x0382, 0x0001},
	{0x0384, 0x0001},
	{0x0386, 0x0001},
	{0x0114, 0x0330},
	{0x0110, 0x0002},
	{0x0136, 0x1800},
	{0x0304, 0x0004},
	{0x0306, 0x0078},
	{0x3C1E, 0x0000},
	{0x030C, 0x0004},
	{0x030E, 0x0064},
	{0x3C16, 0x0000},
	{0x0300, 0x0006},
	{0x0342, 0x1320},
	{0x0340, 0x0CBC},
	{0x38C4, 0x0009},
	{0x38D8, 0x002A},
	{0x38DA, 0x000A},
	{0x38DC, 0x000B},
	{0x38C2, 0x000A},
	{0x38C0, 0x000F},
	{0x38D6, 0x000A},
	{0x38D4, 0x0009},
	{0x38B0, 0x000F},
	{0x3932, 0x1000},
	{0x3934, 0x0180},
	{0x3938, 0x000C},
	{0x0820, 0x04B0},
	{0x380C, 0x0090},
	{0x3064, 0xEFCF},
	{0x309C, 0x0640},
	{0x3090, 0x8800},
	{0x3238, 0x000C},
	{0x314A, 0x5F00},
	{0x32B2, 0x0000},
	{0x32B4, 0x0000},
	{0x32B6, 0x0000},
	{0x32B8, 0x0000},
	{0x3300, 0x0000},
	{0x3400, 0x0000},
	{0x3402, 0x4E42},
	{0x32B2, 0x0006},
	{0x32B4, 0x0006},
	{0x32B6, 0x0006},
	{0x32B8, 0x0006},
	{0x3C34, 0x0008},
	{0x3C36, 0x0000},
	{0x3C38, 0x0000},
	{0x393E, 0x4000},
	{REG_NULL, 0x0000},
};

static const struct regval s5k3l6xx_2104x1560_30fps_regs[] = {
#ifdef S5K3L6XX_MIRROR
	{0x0100, 0x0001},
#elif defined S5K3L6XX_FLIP
	{0x0100, 0x0002},
#elif defined S5K3L6XX_FLIP_MIRROR
	{0x0100, 0x0003},
#else
	{0x0100, 0x0000},
#endif
	{0x0000, 0x0050},
	{0x0000, 0x30C6},
	{0x0A02, 0x3400},
	{0x3084, 0x1314},
	{0x3266, 0x0001},
	{0x3242, 0x2020},
	{0x306A, 0x2F4C},
	{0x306C, 0xCA01},
	{0x307A, 0x0D20},
	{0x309E, 0x002D},
	{0x3072, 0x0013},
	{0x3074, 0x0977},
	{0x3076, 0x9411},
	{0x3024, 0x0016},
	{0x3070, 0x3D00},
	{0x3002, 0x0E00},
	{0x3006, 0x1000},
	{0x300A, 0x0C00},
	{0x3010, 0x0400},
	{0x3018, 0xC500},
	{0x303A, 0x0204},
	{0x3452, 0x0001},
	{0x3454, 0x0001},
	{0x3456, 0x0001},
	{0x3458, 0x0001},
	{0x345a, 0x0002},
	{0x345C, 0x0014},
	{0x345E, 0x0002},
	{0x3460, 0x0014},
	{0x3464, 0x0006},
	{0x3466, 0x0012},
	{0x3468, 0x0012},
	{0x346A, 0x0012},
	{0x346C, 0x0012},
	{0x346E, 0x0012},
	{0x3470, 0x0012},
	{0x3472, 0x0008},
	{0x3474, 0x0004},
	{0x3476, 0x0044},
	{0x3478, 0x0004},
	{0x347A, 0x0044},
	{0x347E, 0x0006},
	{0x3480, 0x0010},
	{0x3482, 0x0010},
	{0x3484, 0x0010},
	{0x3486, 0x0010},
	{0x3488, 0x0010},
	{0x348A, 0x0010},
	{0x348E, 0x000C},
	{0x3490, 0x004C},
	{0x3492, 0x000C},
	{0x3494, 0x004C},
	{0x3496, 0x0020},
	{0x3498, 0x0006},
	{0x349A, 0x0008},
	{0x349C, 0x0008},
	{0x349E, 0x0008},
	{0x34A0, 0x0008},
	{0x34A2, 0x0008},
	{0x34A4, 0x0008},
	{0x34A8, 0x001A},
	{0x34AA, 0x002A},
	{0x34AC, 0x001A},
	{0x34AE, 0x002A},
	{0x34B0, 0x0080},
	{0x34B2, 0x0006},
	{0x32A2, 0x0000},
	{0x32A4, 0x0000},
	{0x32A6, 0x0000},
	{0x32A8, 0x0000},
	{0x3066, 0x7E00},
	{0x3004, 0x0800},
	//mode setting
	{0x0344, 0x0008},
	{0x0346, 0x0008},
	{0x0348, 0x1077},
	{0x034A, 0x0C37},
	{0x034C, 0x0838},
	{0x034E, 0x0618},
	{0x0900, 0x0122},
	{0x0380, 0x0001},
	{0x0382, 0x0001},
	{0x0384, 0x0001},
	{0x0386, 0x0003},
	{0x0114, 0x0330},
	{0x0110, 0x0002},
	{0x0136, 0x1800},
	{0x0304, 0x0004},
	{0x0306, 0x0078},
	{0x3C1E, 0x0000},
	{0x030C, 0x0003},
	{0x030E, 0x0047},
	{0x3C16, 0x0001},
	{0x0300, 0x0006},
	{0x0342, 0x1320},
	{0x0340, 0x0CBC},
	{0x38C4, 0x0004},
	{0x38D8, 0x0011},
	{0x38DA, 0x0005},
	{0x38DC, 0x0005},
	{0x38C2, 0x0005},
	{0x38C0, 0x0004},
	{0x38D6, 0x0004},
	{0x38D4, 0x0004},
	{0x38B0, 0x0007},
	{0x3932, 0x1000},
	{0x3934, 0x0180},
	{0x3938, 0x000C},
	{0x0820, 0x0238},
	{0x380C, 0x0049},
	{0x3064, 0xFFCF},
	{0x309C, 0x0640},
	{0x3090, 0x8000},
	{0x3238, 0x000B},
	{0x314A, 0x5F02},
	{0x3300, 0x0000},
	{0x3400, 0x0000},
	{0x3402, 0x4E46},
	{0x32B2, 0x0008},
	{0x32B4, 0x0008},
	{0x32B6, 0x0008},
	{0x32B8, 0x0008},
	{0x3C34, 0x0048},
	{0x3C36, 0x3000},
	{0x3C38, 0x0020},
	{0x393E, 0x4000},
	{0x303A, 0x0204},
	{0x3034, 0x4B01},
	{0x3036, 0x0029},
	{0x3032, 0x4800},
	{0x320E, 0x049E},
	{REG_NULL, 0x0000},
};

static const struct s5k3l6xx_mode supported_modes[] = {
	{
		.width = 4208,
		.height = 3120,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0cb0,
		.hts_def = 0x1320,
		.vts_def = 0x0cbc,
		.bpp = 10,
		.reg_list = s5k3l6xx_4208x3120_30fps_regs,
		.link_freq_idx = 0,
	},
	{
		.width = 2104,
		.height = 1560,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0cb0,
		.hts_def = 0x1320,
		.vts_def = 0x0cbc,
		.bpp = 10,
		.reg_list = s5k3l6xx_2104x1560_30fps_regs,
		.link_freq_idx = 1,
	},
};

static const s64 link_freq_items[] = {
	S5K3L6XX_LINK_FREQ_600MHZ,
	S5K3L6XX_LINK_FREQ_284MHZ,
};

static const char * const s5k3l6xx_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3"
};

/* Write registers up to 4 at a time */
static int s5k3l6xx_write_reg(struct i2c_client *client, u16 reg,
			     u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	dev_dbg(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);

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

static int s5k3l6xx_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = s5k3l6xx_write_reg(client, regs[i].addr,
					S5K3L6XX_REG_VALUE_16BIT,
					regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int s5k3l6xx_read_reg(struct i2c_client *client, u16 reg,
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

static int s5k3l6xx_get_reso_dist(const struct s5k3l6xx_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct s5k3l6xx_mode *
s5k3l6xx_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = s5k3l6xx_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int s5k3l6xx_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);
	const struct s5k3l6xx_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;
	u32 lane_num = S5K3L6XX_LANES;

	mutex_lock(&s5k3l6xx->mutex);

	mode = s5k3l6xx_find_best_fit(fmt);
	fmt->format.code = S5K3L6XX_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&s5k3l6xx->mutex);
		return -ENOTTY;
#endif
	} else {
		s5k3l6xx->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(s5k3l6xx->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(s5k3l6xx->vblank, vblank_def,
					 S5K3L6XX_VTS_MAX - mode->height,
					 1, vblank_def);
		pixel_rate = (u32)link_freq_items[mode->link_freq_idx] / mode->bpp * 2 * lane_num;

		__v4l2_ctrl_s_ctrl_int64(s5k3l6xx->pixel_rate,
					 pixel_rate);
		__v4l2_ctrl_s_ctrl(s5k3l6xx->link_freq,
				   mode->link_freq_idx);
	}

	mutex_unlock(&s5k3l6xx->mutex);

	return 0;
}

static int s5k3l6xx_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);
	const struct s5k3l6xx_mode *mode = s5k3l6xx->cur_mode;

	mutex_lock(&s5k3l6xx->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&s5k3l6xx->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = S5K3L6XX_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&s5k3l6xx->mutex);

	return 0;
}

static int s5k3l6xx_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = S5K3L6XX_MEDIA_BUS_FMT;

	return 0;
}

static int s5k3l6xx_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != S5K3L6XX_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int s5k3l6xx_enable_test_pattern(struct s5k3l6xx *s5k3l6xx, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | S5K3L6XX_TEST_PATTERN_ENABLE;
	else
		val = S5K3L6XX_TEST_PATTERN_DISABLE;

	return s5k3l6xx_write_reg(s5k3l6xx->client,
				 S5K3L6XX_REG_TEST_PATTERN,
				 S5K3L6XX_REG_VALUE_08BIT,
				 val);
}

static int s5k3l6xx_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);
	const struct s5k3l6xx_mode *mode = s5k3l6xx->cur_mode;

	mutex_lock(&s5k3l6xx->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&s5k3l6xx->mutex);

	return 0;
}

static void s5k3l6xx_get_module_inf(struct s5k3l6xx *s5k3l6xx,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, S5K3L6XX_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, s5k3l6xx->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, s5k3l6xx->len_name, sizeof(inf->base.lens));
}

static long s5k3l6xx_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		s5k3l6xx_get_module_inf(s5k3l6xx, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = s5k3l6xx_write_reg(s5k3l6xx->client,
				 S5K3L6XX_REG_CTRL_MODE,
				 S5K3L6XX_REG_VALUE_08BIT,
				 S5K3L6XX_MODE_STREAMING);
		else
			ret = s5k3l6xx_write_reg(s5k3l6xx->client,
				 S5K3L6XX_REG_CTRL_MODE,
				 S5K3L6XX_REG_VALUE_08BIT,
				 S5K3L6XX_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long s5k3l6xx_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = s5k3l6xx_ioctl(sd, cmd, inf);
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
			ret = s5k3l6xx_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = s5k3l6xx_ioctl(sd, cmd, &stream);
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

static int __s5k3l6xx_start_stream(struct s5k3l6xx *s5k3l6xx)
{
	int ret;

	ret = s5k3l6xx_write_array(s5k3l6xx->client, s5k3l6xx->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&s5k3l6xx->mutex);
	ret = v4l2_ctrl_handler_setup(&s5k3l6xx->ctrl_handler);
	mutex_lock(&s5k3l6xx->mutex);
	if (ret)
		return ret;

	s5k3l6xx_write_reg(s5k3l6xx->client,
				 S5K3L6XX_REG_STREAM_ON,
				 S5K3L6XX_REG_VALUE_08BIT,
				 S5K3L6XX_MODE_STREAMING);
	s5k3l6xx_write_reg(s5k3l6xx->client,
				 S5K3L6XX_REG_CTRL_MODE,
				 S5K3L6XX_REG_VALUE_08BIT,
				 S5K3L6XX_MODE_STREAMING);
	s5k3l6xx_write_reg(s5k3l6xx->client,
				 S5K3L6XX_REG_STREAM_ON,
				 S5K3L6XX_REG_VALUE_08BIT,
				 S5K3L6XX_MODE_SW_STANDBY);
	return 0;
}

static int __s5k3l6xx_stop_stream(struct s5k3l6xx *s5k3l6xx)
{
	return s5k3l6xx_write_reg(s5k3l6xx->client,
				 S5K3L6XX_REG_CTRL_MODE,
				 S5K3L6XX_REG_VALUE_08BIT,
				 S5K3L6XX_MODE_SW_STANDBY);
}

static int s5k3l6xx_s_stream(struct v4l2_subdev *sd, int on)
{
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);
	struct i2c_client *client = s5k3l6xx->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				s5k3l6xx->cur_mode->width,
				s5k3l6xx->cur_mode->height,
		DIV_ROUND_CLOSEST(s5k3l6xx->cur_mode->max_fps.denominator,
				  s5k3l6xx->cur_mode->max_fps.numerator));

	mutex_lock(&s5k3l6xx->mutex);
	on = !!on;
	if (on == s5k3l6xx->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __s5k3l6xx_start_stream(s5k3l6xx);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__s5k3l6xx_stop_stream(s5k3l6xx);
		pm_runtime_put(&client->dev);
	}

	s5k3l6xx->streaming = on;

unlock_and_return:
	mutex_unlock(&s5k3l6xx->mutex);

	return ret;
}

static int s5k3l6xx_s_power(struct v4l2_subdev *sd, int on)
{
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);
	struct i2c_client *client = s5k3l6xx->client;
	int ret = 0;

	mutex_lock(&s5k3l6xx->mutex);

	/* If the power state is not modified - no work to do. */
	if (s5k3l6xx->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		s5k3l6xx->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		s5k3l6xx->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&s5k3l6xx->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 s5k3l6xx_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, S5K3L6XX_XVCLK_FREQ / 1000 / 1000);
}

static int __s5k3l6xx_power_on(struct s5k3l6xx *s5k3l6xx)
{
	int ret;
	u32 delay_us;
	struct device *dev = &s5k3l6xx->client->dev;

	if (!IS_ERR(s5k3l6xx->power_gpio))
		gpiod_set_value_cansleep(s5k3l6xx->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(s5k3l6xx->pins_default)) {
		ret = pinctrl_select_state(s5k3l6xx->pinctrl,
					   s5k3l6xx->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(s5k3l6xx->xvclk, S5K3L6XX_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(s5k3l6xx->xvclk) != S5K3L6XX_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(s5k3l6xx->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(s5k3l6xx->reset_gpio))
		gpiod_set_value_cansleep(s5k3l6xx->reset_gpio, 0);

	ret = regulator_bulk_enable(S5K3L6XX_NUM_SUPPLIES, s5k3l6xx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(s5k3l6xx->reset_gpio))
		gpiod_set_value_cansleep(s5k3l6xx->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(s5k3l6xx->pwdn_gpio))
		gpiod_set_value_cansleep(s5k3l6xx->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = s5k3l6xx_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(s5k3l6xx->xvclk);

	return ret;
}

static void __s5k3l6xx_power_off(struct s5k3l6xx *s5k3l6xx)
{
	int ret;
	struct device *dev = &s5k3l6xx->client->dev;

	if (!IS_ERR(s5k3l6xx->pwdn_gpio))
		gpiod_set_value_cansleep(s5k3l6xx->pwdn_gpio, 0);
	clk_disable_unprepare(s5k3l6xx->xvclk);
	if (!IS_ERR(s5k3l6xx->reset_gpio))
		gpiod_set_value_cansleep(s5k3l6xx->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(s5k3l6xx->pins_sleep)) {
		ret = pinctrl_select_state(s5k3l6xx->pinctrl,
					   s5k3l6xx->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(s5k3l6xx->power_gpio))
		gpiod_set_value_cansleep(s5k3l6xx->power_gpio, 0);

	regulator_bulk_disable(S5K3L6XX_NUM_SUPPLIES, s5k3l6xx->supplies);
}

static int s5k3l6xx_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);

	return __s5k3l6xx_power_on(s5k3l6xx);
}

static int s5k3l6xx_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);

	__s5k3l6xx_power_off(s5k3l6xx);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int s5k3l6xx_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct s5k3l6xx_mode *def_mode = &supported_modes[0];

	mutex_lock(&s5k3l6xx->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = S5K3L6XX_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&s5k3l6xx->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int s5k3l6xx_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != S5K3L6XX_MEDIA_BUS_FMT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;

	return 0;
}

static int s5k3l6xx_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *config)
{
	if (2 == S5K3L6XX_LANES) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_2_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else if (4 == S5K3L6XX_LANES) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_4_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	}

	return 0;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH_2096 2096
#define DST_HEIGHT_1560 1560

static int s5k3l6xx_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		if (s5k3l6xx->cur_mode->width == 2104) {
			sel->r.left = CROP_START(s5k3l6xx->cur_mode->width, DST_WIDTH_2096);
			sel->r.width = DST_WIDTH_2096;
			sel->r.top = CROP_START(s5k3l6xx->cur_mode->height, DST_HEIGHT_1560);
			sel->r.height = DST_HEIGHT_1560;
		} else {
			sel->r.left = CROP_START(s5k3l6xx->cur_mode->width,
							s5k3l6xx->cur_mode->width);
			sel->r.width = s5k3l6xx->cur_mode->width;
			sel->r.top = CROP_START(s5k3l6xx->cur_mode->height,
							s5k3l6xx->cur_mode->height);
			sel->r.height = s5k3l6xx->cur_mode->height;
		}
		return 0;
	}

	return -EINVAL;
}

static const struct dev_pm_ops s5k3l6xx_pm_ops = {
	SET_RUNTIME_PM_OPS(s5k3l6xx_runtime_suspend,
			   s5k3l6xx_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops s5k3l6xx_internal_ops = {
	.open = s5k3l6xx_open,
};
#endif

static const struct v4l2_subdev_core_ops s5k3l6xx_core_ops = {
	.s_power = s5k3l6xx_s_power,
	.ioctl = s5k3l6xx_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = s5k3l6xx_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops s5k3l6xx_video_ops = {
	.s_stream = s5k3l6xx_s_stream,
	.g_frame_interval = s5k3l6xx_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops s5k3l6xx_pad_ops = {
	.enum_mbus_code = s5k3l6xx_enum_mbus_code,
	.enum_frame_size = s5k3l6xx_enum_frame_sizes,
	.enum_frame_interval = s5k3l6xx_enum_frame_interval,
	.get_fmt = s5k3l6xx_get_fmt,
	.set_fmt = s5k3l6xx_set_fmt,
	.get_selection = s5k3l6xx_get_selection,
	.get_mbus_config = s5k3l6xx_g_mbus_config,
};

static const struct v4l2_subdev_ops s5k3l6xx_subdev_ops = {
	.core	= &s5k3l6xx_core_ops,
	.video	= &s5k3l6xx_video_ops,
	.pad	= &s5k3l6xx_pad_ops,
};

static int s5k3l6xx_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5k3l6xx *s5k3l6xx = container_of(ctrl->handler,
					     struct s5k3l6xx, ctrl_handler);
	struct i2c_client *client = s5k3l6xx->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = s5k3l6xx->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(s5k3l6xx->exposure,
					 s5k3l6xx->exposure->minimum, max,
					 s5k3l6xx->exposure->step,
					 s5k3l6xx->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = s5k3l6xx_write_reg(s5k3l6xx->client,
					S5K3L6XX_REG_EXPOSURE,
					S5K3L6XX_REG_VALUE_16BIT,
					ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = s5k3l6xx_write_reg(s5k3l6xx->client,
					S5K3L6XX_REG_ANALOG_GAIN,
					S5K3L6XX_REG_VALUE_16BIT,
					ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = s5k3l6xx_write_reg(s5k3l6xx->client,
					S5K3L6XX_REG_VTS,
					S5K3L6XX_REG_VALUE_16BIT,
					ctrl->val + s5k3l6xx->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = s5k3l6xx_enable_test_pattern(s5k3l6xx, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops s5k3l6xx_ctrl_ops = {
	.s_ctrl = s5k3l6xx_set_ctrl,
};

static int s5k3l6xx_initialize_controls(struct s5k3l6xx *s5k3l6xx)
{
	const struct s5k3l6xx_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_pixel_rate = 0;
	u32 lane_num = S5K3L6XX_LANES;

	handler = &s5k3l6xx->ctrl_handler;
	mode = s5k3l6xx->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &s5k3l6xx->mutex;

	s5k3l6xx->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			1, 0, link_freq_items);

	dst_pixel_rate = (u32)link_freq_items[mode->link_freq_idx] / mode->bpp * 2 * lane_num;

	s5k3l6xx->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, S5K3L6XX_PIXEL_RATE,
			1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(s5k3l6xx->link_freq,
			   mode->link_freq_idx);

	h_blank = mode->hts_def - mode->width;
	s5k3l6xx->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (s5k3l6xx->hblank)
		s5k3l6xx->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	s5k3l6xx->vblank = v4l2_ctrl_new_std(handler, &s5k3l6xx_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				S5K3L6XX_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	s5k3l6xx->exposure = v4l2_ctrl_new_std(handler, &s5k3l6xx_ctrl_ops,
				V4L2_CID_EXPOSURE, S5K3L6XX_EXPOSURE_MIN,
				exposure_max, S5K3L6XX_EXPOSURE_STEP,
				mode->exp_def);

	s5k3l6xx->anal_gain = v4l2_ctrl_new_std(handler, &s5k3l6xx_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, S5K3L6XX_GAIN_MIN,
				S5K3L6XX_GAIN_MAX, S5K3L6XX_GAIN_STEP,
				S5K3L6XX_GAIN_DEFAULT);

	s5k3l6xx->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&s5k3l6xx_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(s5k3l6xx_test_pattern_menu) - 1,
				0, 0, s5k3l6xx_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&s5k3l6xx->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	s5k3l6xx->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int s5k3l6xx_check_sensor_id(struct s5k3l6xx *s5k3l6xx,
				   struct i2c_client *client)
{
	struct device *dev = &s5k3l6xx->client->dev;
	u32 id = 0;
	int ret;

	ret = s5k3l6xx_read_reg(client, S5K3L6XX_REG_CHIP_ID,
			       S5K3L6XX_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	ret = s5k3l6xx_read_reg(client, S5K3L6XX_CHIP_REVISION_REG,
			       S5K3L6XX_REG_VALUE_08BIT, &id);
	if (ret) {
		dev_err(dev, "Read chip revision register error\n");
		return ret;
	}

	dev_info(dev, "Detected Samsung %04x sensor, REVISION 0x%x\n", CHIP_ID, id);

	return 0;
}

static int s5k3l6xx_configure_regulators(struct s5k3l6xx *s5k3l6xx)
{
	unsigned int i;

	for (i = 0; i < S5K3L6XX_NUM_SUPPLIES; i++)
		s5k3l6xx->supplies[i].supply = s5k3l6xx_supply_names[i];

	return devm_regulator_bulk_get(&s5k3l6xx->client->dev,
				       S5K3L6XX_NUM_SUPPLIES,
				       s5k3l6xx->supplies);
}

static int s5k3l6xx_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct s5k3l6xx *s5k3l6xx;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	s5k3l6xx = devm_kzalloc(dev, sizeof(*s5k3l6xx), GFP_KERNEL);
	if (!s5k3l6xx)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &s5k3l6xx->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &s5k3l6xx->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &s5k3l6xx->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &s5k3l6xx->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	s5k3l6xx->client = client;
	s5k3l6xx->cur_mode = &supported_modes[0];

	s5k3l6xx->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(s5k3l6xx->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	s5k3l6xx->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(s5k3l6xx->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	s5k3l6xx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(s5k3l6xx->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	s5k3l6xx->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(s5k3l6xx->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = s5k3l6xx_configure_regulators(s5k3l6xx);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	s5k3l6xx->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(s5k3l6xx->pinctrl)) {
		s5k3l6xx->pins_default =
			pinctrl_lookup_state(s5k3l6xx->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(s5k3l6xx->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		s5k3l6xx->pins_sleep =
			pinctrl_lookup_state(s5k3l6xx->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(s5k3l6xx->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&s5k3l6xx->mutex);

	sd = &s5k3l6xx->subdev;
	v4l2_i2c_subdev_init(sd, client, &s5k3l6xx_subdev_ops);
	ret = s5k3l6xx_initialize_controls(s5k3l6xx);
	if (ret)
		goto err_destroy_mutex;

	ret = __s5k3l6xx_power_on(s5k3l6xx);
	if (ret)
		goto err_free_handler;

	ret = s5k3l6xx_check_sensor_id(s5k3l6xx, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &s5k3l6xx_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	s5k3l6xx->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &s5k3l6xx->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(s5k3l6xx->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 s5k3l6xx->module_index, facing,
		 S5K3L6XX_NAME, dev_name(sd->dev));
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
	__s5k3l6xx_power_off(s5k3l6xx);
err_free_handler:
	v4l2_ctrl_handler_free(&s5k3l6xx->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&s5k3l6xx->mutex);

	return ret;
}

static int s5k3l6xx_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k3l6xx *s5k3l6xx = to_s5k3l6xx(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&s5k3l6xx->ctrl_handler);
	mutex_destroy(&s5k3l6xx->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__s5k3l6xx_power_off(s5k3l6xx);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id s5k3l6xx_of_match[] = {
	{ .compatible = "samsung,s5k3l6xx" },
	{},
};
MODULE_DEVICE_TABLE(of, s5k3l6xx_of_match);
#endif

static const struct i2c_device_id s5k3l6xx_match_id[] = {
	{ "samsung,s5k3l6xx", 0 },
	{},
};

static struct i2c_driver s5k3l6xx_i2c_driver = {
	.driver = {
		.name = S5K3L6XX_NAME,
		.pm = &s5k3l6xx_pm_ops,
		.of_match_table = of_match_ptr(s5k3l6xx_of_match),
	},
	.probe		= &s5k3l6xx_probe,
	.remove		= &s5k3l6xx_remove,
	.id_table	= s5k3l6xx_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&s5k3l6xx_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&s5k3l6xx_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Samsung s5k3l6xx sensor driver");
MODULE_LICENSE("GPL v2");
