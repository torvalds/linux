// SPDX-License-Identifier: GPL-2.0
/*
 * hi556 driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 init version
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define MIPI_FREQ	440000000U
#define HI556_PIXEL_RATE		(440000000LL * 2LL * 2LL / 10)
#define HI556_XVCLK_FREQ		24000000

#define CHIP_ID				0x0556
#define HI556_REG_CHIP_ID		0x0f16

#define HI556_REG_CTRL_MODE		0x0A00
#define HI556_MODE_SW_STANDBY		0x00
#define HI556_MODE_STREAMING		0x01

#define HI556_REG_EXPOSURE_H		0x0073
#define HI556_REG_EXPOSURE_M		0x0074
#define HI556_REG_EXPOSURE_L		0x0075

#define HI556_FETCH_HIGH_BYTE_EXP(VAL)	(((VAL) >> 16) & 0xF)	/* 4 Bits */
#define HI556_FETCH_MIDDLE_BYTE_EXP(VAL) (((VAL) >> 8) & 0xFF)	/* 8 Bits */
#define HI556_FETCH_LOW_BYTE_EXP(VAL)	((VAL) & 0xFF)	/* 8 Bits */

#define	HI556_EXPOSURE_MIN		4
#define	HI556_EXPOSURE_STEP		1
#define HI556_VTS_MAX			0x7fff

#define HI556_REG_GAIN			0x0077
#define HI556_GAIN_MASK			0xff

#define	ANALOG_GAIN_MIN			0x00
#define	ANALOG_GAIN_MAX			0xF0
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x10

#define HI556_REG_GROUP	0x0046

#define HI556_REG_TEST_PATTERN		0x0A05
#define	HI556_TEST_PATTERN_ENABLE	0x01
#define	HI556_TEST_PATTERN_DISABLE	0x0
#define HI556_REG_TEST_PATTERN_SELECT	0x0201

#define HI556_REG_VTS			0x0006
#define HI556_FLIP_MIRROR_REG	0x000e
#define HI556_FETCH_MIRROR(VAL, ENABLE)	(ENABLE ? VAL | 0x01 : VAL & 0xfe)
#define HI556_FETCH_FLIP(VAL, ENABLE)	(ENABLE ? VAL | 0x02 : VAL & 0xfd)
#define REG_NULL			0xFFFF
#define DELAY_MS			0xEEEE	/* Array delay token */

#define HI556_REG_VALUE_08BIT		1
#define HI556_REG_VALUE_16BIT		2
#define HI556_REG_VALUE_24BIT		3

#define HI556_LANES			2
#define HI556_BITS_PER_SAMPLE		10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define HI556_NAME			"hi556"
#define HI556_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SGBRG10_1X10

struct hi556_otp_info {
	int flag; // bit[7]: info, bit[6]:wb
	int module_id;
	int lens_id;
	int year;
	int month;
	int day;
	int rg_ratio;
	int bg_ratio;
};

static const char * const hi556_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define HI556_NUM_SUPPLIES ARRAY_SIZE(hi556_supply_names)

struct regval {
	u16 addr;
	u16 val;
};

struct hi556_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	u32 hdr_mode;
};

struct hi556 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[HI556_NUM_SUPPLIES];

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
	const struct hi556_mode *cur_mode;
	unsigned int lane_num;
	unsigned int cfg_num;
	unsigned int pixel_rate;
	u32			module_index;
	struct hi556_otp_info *otp;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	struct rkmodule_awb_cfg	awb_cfg;
};

#define to_hi556(sd) container_of(sd, struct hi556, subdev)

/*
 * Xclk 24Mhz
 * Pclk 176Mhz
 * linelength 2816(0xb00)
 * framelength 1988(0x7c0)
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 30fps
 * MIPI speed(Mbps) : 840Mbps x 2Lane
 */
static const struct regval hi556_global_regs[] = {
	{0x0a00, 0x0000},
	{0x0e00, 0x0102},
	{0x0e02, 0x0102},
	{0x0e0c, 0x0100},
	{0x2000, 0x7400},
	{0x2002, 0x001c},
	{0x2004, 0x0242},
	{0x2006, 0x0942},
	{0x2008, 0x7007},
	{0x200a, 0x0fd9},
	{0x200c, 0x0259},
	{0x200e, 0x7008},
	{0x2010, 0x160e},
	{0x2012, 0x0047},
	{0x2014, 0x2118},
	{0x2016, 0x0041},
	{0x2018, 0x00d8},
	{0x201a, 0x0145},
	{0x201c, 0x0006},
	{0x201e, 0x0181},
	{0x2020, 0x13cc},
	{0x2022, 0x2057},
	{0x2024, 0x7001},
	{0x2026, 0x0fca},
	{0x2028, 0x00cb},
	{0x202a, 0x009f},
	{0x202c, 0x7002},
	{0x202e, 0x13cc},
	{0x2030, 0x019b},
	{0x2032, 0x014d},
	{0x2034, 0x2987},
	{0x2036, 0x2766},
	{0x2038, 0x0020},
	{0x203a, 0x2060},
	{0x203c, 0x0e5d},
	{0x203e, 0x181d},
	{0x2040, 0x2066},
	{0x2042, 0x20c4},
	{0x2044, 0x5000},
	{0x2046, 0x0005},
	{0x2048, 0x0000},
	{0x204a, 0x01db},
	{0x204c, 0x025a},
	{0x204e, 0x00c0},
	{0x2050, 0x0005},
	{0x2052, 0x0006},
	{0x2054, 0x0ad9},
	{0x2056, 0x0259},
	{0x2058, 0x0618},
	{0x205a, 0x0258},
	{0x205c, 0x2266},
	{0x205e, 0x20c8},
	{0x2060, 0x2060},
	{0x2062, 0x707b},
	{0x2064, 0x0fdd},
	{0x2066, 0x81b8},
	{0x2068, 0x5040},
	{0x206a, 0x0020},
	{0x206c, 0x5060},
	{0x206e, 0x3143},
	{0x2070, 0x5081},
	{0x2072, 0x025c},
	{0x2074, 0x7800},
	{0x2076, 0x7400},
	{0x2078, 0x001c},
	{0x207a, 0x0242},
	{0x207c, 0x0942},
	{0x207e, 0x0bd9},
	{0x2080, 0x0259},
	{0x2082, 0x7008},
	{0x2084, 0x160e},
	{0x2086, 0x0047},
	{0x2088, 0x2118},
	{0x208a, 0x0041},
	{0x208c, 0x00d8},
	{0x208e, 0x0145},
	{0x2090, 0x0006},
	{0x2092, 0x0181},
	{0x2094, 0x13cc},
	{0x2096, 0x2057},
	{0x2098, 0x7001},
	{0x209a, 0x0fca},
	{0x209c, 0x00cb},
	{0x209e, 0x009f},
	{0x20a0, 0x7002},
	{0x20a2, 0x13cc},
	{0x20a4, 0x019b},
	{0x20a6, 0x014d},
	{0x20a8, 0x2987},
	{0x20aa, 0x2766},
	{0x20ac, 0x0020},
	{0x20ae, 0x2060},
	{0x20b0, 0x0e5d},
	{0x20b2, 0x181d},
	{0x20b4, 0x2066},
	{0x20b6, 0x20c4},
	{0x20b8, 0x50a0},
	{0x20ba, 0x0005},
	{0x20bc, 0x0000},
	{0x20be, 0x01db},
	{0x20c0, 0x025a},
	{0x20c2, 0x00c0},
	{0x20c4, 0x0005},
	{0x20c6, 0x0006},
	{0x20c8, 0x0ad9},
	{0x20ca, 0x0259},
	{0x20cc, 0x0618},
	{0x20ce, 0x0258},
	{0x20d0, 0x2266},
	{0x20d2, 0x20c8},
	{0x20d4, 0x2060},
	{0x20d6, 0x707b},
	{0x20d8, 0x0fdd},
	{0x20da, 0x86b8},
	{0x20dc, 0x50e0},
	{0x20de, 0x0020},
	{0x20e0, 0x5100},
	{0x20e2, 0x3143},
	{0x20e4, 0x5121},
	{0x20e6, 0x7800},
	{0x20e8, 0x3140},
	{0x20ea, 0x01c4},
	{0x20ec, 0x01c1},
	{0x20ee, 0x01c0},
	{0x20f0, 0x01c4},
	{0x20f2, 0x2700},
	{0x20f4, 0x3d40},
	{0x20f6, 0x7800},
	{0x20f8, 0xffff},
	{0x27fe, 0xe000},
	{0x3000, 0x60f8},
	{0x3002, 0x187f},
	{0x3004, 0x7060},
	{0x3006, 0x0114},
	{0x3008, 0x60b0},
	{0x300a, 0x1473},
	{0x300c, 0x0013},
	{0x300e, 0x140f},
	{0x3010, 0x0040},
	{0x3012, 0x100f},
	{0x3014, 0x60f8},
	{0x3016, 0x187f},
	{0x3018, 0x7060},
	{0x301a, 0x0114},
	{0x301c, 0x60b0},
	{0x301e, 0x1473},
	{0x3020, 0x0013},
	{0x3022, 0x140f},
	{0x3024, 0x0040},
	{0x3026, 0x000f},
	{0x0b00, 0x0000},
	{0x0b02, 0x0045},
	{0x0b04, 0xb405},
	{0x0b06, 0xc403},
	{0x0b08, 0x0081},
	{0x0b0a, 0x8252},
	{0x0b0c, 0xf814},
	{0x0b0e, 0xc618},
	{0x0b10, 0xa828},
	{0x0b12, 0x004c},
	{0x0b14, 0x4068},
	{0x0b16, 0x0000},
	{0x0f30, 0x6e25},
	{0x0f32, 0x7067},
	{0x0954, 0x0009},
	{0x0956, 0x1100},
	{0x0958, 0xcc80},
	{0x095a, 0x0000},
	{0x0c00, 0x1110},
	{0x0c02, 0x0011},
	{0x0c04, 0x0000},
	{0x0c06, 0x0200},
	{0x0c10, 0x0040},
	{0x0c12, 0x0040},
	{0x0c14, 0x0040},
	{0x0c16, 0x0040},
	{0x0a10, 0x4000},
	{0x3068, 0xf800},
	{0x306a, 0xf876},
	{0x006c, 0x0000},
	{0x005e, 0x0200},
	{0x000e, 0x0100},
	{0x0e0a, 0x0001},
	{0x004a, 0x0100},
	{0x004c, 0x0000},
	{0x004e, 0x0100},
	{0x000c, 0x0022},
	{0x0008, 0x0b00},
	{0x005a, 0x0202},
	{0x0012, 0x000e},
	{0x0018, 0x0a31},
	{0x0022, 0x0008},
	{0x0028, 0x0017},
	{0x0024, 0x0028},
	{0x002a, 0x002d},
	{0x0026, 0x0030},
	{0x002c, 0x07c7},
	{0x002e, 0x1111},
	{0x0030, 0x1111},
	{0x0032, 0x1111},
	{0x0006, 0x0823},
	{0x0a22, 0x0000},
	{0x0a12, 0x0a20},
	{0x0a14, 0x0798},
	{0x003e, 0x0000},
	{0x0074, 0x0821},
	{0x0070, 0x0411},
	{0x0002, 0x0000},
	{0x0a02, 0x0100},
	{0x0a24, 0x0100},
	{0x0076, 0x0000},
	{0x0060, 0x0000},
	{0x0062, 0x0530},
	{0x0064, 0x0500},
	{0x0066, 0x0530},
	{0x0068, 0x0500},
	{0x0122, 0x0300},
	{0x015a, 0xff08},
	{0x0804, 0x0200},
	{0x005c, 0x0102},
	{0x0a1a, 0x0800},
	{0x003c, 0x0101}, //fix framerate
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * Pclk 210Mhz
 * linelength 2816
 * framelength 2083
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 30fps
 * MIPI speed(Mbps): 880Mbps x 2lane
 */
static const struct regval hi556_2592x1944_regs_2lane[] = {
	{0x0a00, 0x0000},
	{0x0b0a, 0x8252},
	{0x0f30, 0x6e25},
	{0x0f32, 0x7067},
	{0x004a, 0x0100},
	{0x004c, 0x0000},
	{0x004e, 0x0000},
	{0x000c, 0x0022},
	{0x0008, 0x0b00},
	{0x005a, 0x0202},
	{0x0012, 0x000e},
	{0x0018, 0x0a31},
	{0x0022, 0x0008},
	{0x0028, 0x0017},
	{0x0024, 0x0028},
	{0x002a, 0x002d},
	{0x0026, 0x0030},
	{0x002c, 0x07c7},
	{0x002e, 0x1111},
	{0x0030, 0x1111},
	{0x0032, 0x1111},
	{0x0006, 0x0823},
	{0x0a22, 0x0000},
	{0x0a12, 0x0a20},
	{0x0a14, 0x0798},
	{0x003e, 0x0000},
	{0x0804, 0x0200},
	{0x0a04, 0x014a},
	{0x090c, 0x0fdc},
	{0x090e, 0x002d},
	{0x0902, 0x4319},
	{0x0914, 0xc10a},
	{0x0916, 0x071f},
	{0x0918, 0x0408},
	{0x091a, 0x0c0d},
	{0x091c, 0x0f09},
	{0x091e, 0x0a00},
	//{0x0a00, 0x0100},
	{REG_NULL, 0x00},
};

static const struct hi556_mode supported_modes_2lane[] = {
	{
		.width = 2592,
		.height = 1944,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0810,
		.hts_def = 0x0B00,
		.vts_def = 0x0823,
		.reg_list = hi556_2592x1944_regs_2lane,
		.hdr_mode = NO_HDR,
	}
};

static const struct hi556_mode *supported_modes;

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ
};

static const char * const hi556_test_pattern_menu[] = {
	"Disabled",
	"Solid color bar",
	"100% color bars",
	"Fade to gray color bars",
	"PN9",
	"Horizental/Vertical gradient",
	"Check board",
	"Slant",
	"Resolution",
};

/* Write registers up to 4 at a time */
static int hi556_write_reg(struct i2c_client *client, u16 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	dev_dbg(&client->dev, "%s(%d) enter!\n", __func__, __LINE__);
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

	if (i2c_master_send(client, buf, len + 2) != len + 2) {
		dev_err(&client->dev,
			   "write reg(0x%x val:0x%x)failed !\n", reg, val);
		return -EIO;
	}
	return 0;
}

static int hi556_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	int i, delay_ms, ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (regs[i].addr == DELAY_MS) {
			delay_ms = regs[i].val;
			dev_info(&client->dev, "delay(%d) ms !\n", delay_ms);
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
			continue;
		}
		ret = hi556_write_reg(client, regs[i].addr,
				       HI556_REG_VALUE_16BIT, regs[i].val);
		if (ret)
			dev_err(&client->dev, "%s failed !\n", __func__);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int hi556_read_reg(struct i2c_client *client, u16 reg,
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
static int hi556_reg_verify(struct i2c_client *client,
				const struct regval *regs)
{
	u32 i;
	int ret = 0;
	u32 value;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = hi556_read_reg(client, regs[i].addr,
			  HI556_REG_VALUE_16BIT, &value);
		if (value != regs[i].val) {
			dev_info(&client->dev, "%s: 0x%04x is 0x%x instead of 0x%x\n",
				  __func__, regs[i].addr, value, regs[i].val);
		}
	}
	return ret;
}
#endif

static int hi556_get_reso_dist(const struct hi556_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct hi556_mode *
hi556_find_best_fit(struct hi556 *hi556,
			struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < hi556->cfg_num; i++) {
		dist = hi556_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int hi556_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct hi556 *hi556 = to_hi556(sd);
	const struct hi556_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&hi556->mutex);

	mode = hi556_find_best_fit(hi556, fmt);
	fmt->format.code = HI556_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&hi556->mutex);
		return -ENOTTY;
#endif
	} else {
		hi556->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(hi556->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(hi556->vblank, vblank_def,
					 HI556_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&hi556->mutex);

	return 0;
}

static int hi556_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct hi556 *hi556 = to_hi556(sd);
	const struct hi556_mode *mode = hi556->cur_mode;

	mutex_lock(&hi556->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&hi556->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = HI556_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&hi556->mutex);

	return 0;
}

static int hi556_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = HI556_MEDIA_BUS_FMT;

	return 0;
}

static int hi556_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct hi556 *hi556 = to_hi556(sd);

	if (fse->index >= hi556->cfg_num)
		return -EINVAL;

	if (fse->code != HI556_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int hi556_enable_test_pattern(struct hi556 *hi556, u32 pattern)
{

	if (pattern) {
		hi556_write_reg(hi556->client, HI556_REG_TEST_PATTERN,
						HI556_REG_VALUE_08BIT, HI556_TEST_PATTERN_ENABLE);
		hi556_write_reg(hi556->client, HI556_REG_TEST_PATTERN_SELECT,
						HI556_REG_VALUE_08BIT, 0x01 << (pattern - 1));
	} else {
		hi556_write_reg(hi556->client, HI556_REG_TEST_PATTERN,
						HI556_REG_VALUE_08BIT, HI556_TEST_PATTERN_DISABLE);
	}
	return 0;
}

static int hi556_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct hi556 *hi556 = to_hi556(sd);
	const struct hi556_mode *mode = hi556->cur_mode;

	fi->interval = mode->max_fps;

	return 0;
}

static int hi556_g_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	u32 val = 1 << (HI556_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void hi556_get_module_inf(struct hi556 *hi556,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, HI556_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, hi556->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, hi556->len_name, sizeof(inf->base.lens));

}

static void hi556_set_awb_cfg(struct hi556 *hi556,
				 struct rkmodule_awb_cfg *cfg)
{
	mutex_lock(&hi556->mutex);
	memcpy(&hi556->awb_cfg, cfg, sizeof(*cfg));
	mutex_unlock(&hi556->mutex);
}

static long hi556_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct hi556 *hi556 = to_hi556(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		hi556_get_module_inf(hi556, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_AWB_CFG:
		hi556_set_awb_cfg(hi556, (struct rkmodule_awb_cfg *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = hi556_write_reg(hi556->client, HI556_REG_CTRL_MODE,
				HI556_REG_VALUE_08BIT, HI556_MODE_STREAMING);
		else
			ret = hi556_write_reg(hi556->client, HI556_REG_CTRL_MODE,
				HI556_REG_VALUE_08BIT, HI556_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long hi556_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *awb_cfg;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = hi556_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		awb_cfg = kzalloc(sizeof(*awb_cfg), GFP_KERNEL);
		if (!awb_cfg) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(awb_cfg, up, sizeof(*awb_cfg))) {
			kfree(awb_cfg);
			return -EFAULT;
		}
		ret = hi556_ioctl(sd, cmd, awb_cfg);
		kfree(awb_cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;
		ret = hi556_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __hi556_start_stream(struct hi556 *hi556)
{
	int ret;

	ret = hi556_write_array(hi556->client, hi556->cur_mode->reg_list);
	if (ret)
		return ret;

#ifdef CHECK_REG_VALUE
	usleep_range(10000, 20000);
	/*  verify default values to make sure everything has */
	/*  been written correctly as expected */
	dev_info(&hi556->client->dev, "%s:Check register value!\n",
				__func__);
	ret = hi556_reg_verify(hi556->client, hi556_global_regs);
	if (ret)
		return ret;

	ret = hi556_reg_verify(hi556->client, hi556->cur_mode->reg_list);
	if (ret)
		return ret;
#endif

	/* In case these controls are set before streaming */
	mutex_unlock(&hi556->mutex);
	ret = v4l2_ctrl_handler_setup(&hi556->ctrl_handler);
	mutex_lock(&hi556->mutex);
	if (ret)
		return ret;

	if (ret)
		dev_info(&hi556->client->dev, "APPly otp failed!\n");

	ret = hi556_write_reg(hi556->client, HI556_REG_CTRL_MODE,
				HI556_REG_VALUE_08BIT, HI556_MODE_STREAMING);
	return ret;
}

static int __hi556_stop_stream(struct hi556 *hi556)
{
	return hi556_write_reg(hi556->client, HI556_REG_CTRL_MODE,
				HI556_REG_VALUE_08BIT, HI556_MODE_SW_STANDBY);
}

static int hi556_s_stream(struct v4l2_subdev *sd, int on)
{
	struct hi556 *hi556 = to_hi556(sd);
	struct i2c_client *client = hi556->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				hi556->cur_mode->width,
				hi556->cur_mode->height,
		DIV_ROUND_CLOSEST(hi556->cur_mode->max_fps.denominator,
		hi556->cur_mode->max_fps.numerator));

	mutex_lock(&hi556->mutex);
	on = !!on;
	if (on == hi556->streaming)
		goto unlock_and_return;

	if (on) {
		dev_info(&client->dev, "stream on!!!\n");
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __hi556_start_stream(hi556);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		dev_info(&client->dev, "stream off!!!\n");
		__hi556_stop_stream(hi556);
		pm_runtime_put(&client->dev);
	}

	hi556->streaming = on;

unlock_and_return:
	mutex_unlock(&hi556->mutex);

	return ret;
}

static int hi556_s_power(struct v4l2_subdev *sd, int on)
{
	struct hi556 *hi556 = to_hi556(sd);
	struct i2c_client *client = hi556->client;
	int ret = 0;

	dev_info(&client->dev, "%s(%d) on(%d)\n", __func__, __LINE__, on);
	mutex_lock(&hi556->mutex);

	/* If the power state is not modified - no work to do. */
	if (hi556->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = hi556_write_array(hi556->client, hi556_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		hi556->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		hi556->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&hi556->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 hi556_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, HI556_XVCLK_FREQ / 1000 / 1000);
}

static int __hi556_power_on(struct hi556 *hi556)
{
	int ret;
	u32 delay_us;
	struct device *dev = &hi556->client->dev;

	if (!IS_ERR(hi556->power_gpio))
		gpiod_set_value_cansleep(hi556->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(hi556->pins_default)) {
		ret = pinctrl_select_state(hi556->pinctrl,
					   hi556->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(hi556->xvclk, HI556_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(hi556->xvclk) != HI556_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	ret = clk_prepare_enable(hi556->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(HI556_NUM_SUPPLIES, hi556->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(hi556->reset_gpio))
		gpiod_set_value_cansleep(hi556->reset_gpio, 1);

	if (!IS_ERR(hi556->pwdn_gpio))
		gpiod_set_value_cansleep(hi556->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = hi556_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);
	usleep_range(10000, 20000);
	return 0;

disable_clk:
	clk_disable_unprepare(hi556->xvclk);

	return ret;
}

static void __hi556_power_off(struct hi556 *hi556)
{
	int ret;
	struct device *dev = &hi556->client->dev;

	if (!IS_ERR(hi556->pwdn_gpio))
		gpiod_set_value_cansleep(hi556->pwdn_gpio, 0);
	clk_disable_unprepare(hi556->xvclk);
	if (!IS_ERR(hi556->reset_gpio))
		gpiod_set_value_cansleep(hi556->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(hi556->pins_sleep)) {
		ret = pinctrl_select_state(hi556->pinctrl,
					   hi556->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(hi556->power_gpio))
		gpiod_set_value_cansleep(hi556->power_gpio, 0);

	regulator_bulk_disable(HI556_NUM_SUPPLIES, hi556->supplies);
}

static int hi556_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hi556 *hi556 = to_hi556(sd);

	return __hi556_power_on(hi556);
}

static int hi556_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hi556 *hi556 = to_hi556(sd);

	__hi556_power_off(hi556);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int hi556_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct hi556 *hi556 = to_hi556(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct hi556_mode *def_mode = &supported_modes[0];

	mutex_lock(&hi556->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = HI556_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&hi556->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int hi556_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct hi556 *hi556 = to_hi556(sd);

	if (fie->index >= hi556->cfg_num)
		return -EINVAL;

	if (fie->code != HI556_MEDIA_BUS_FMT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;

	return 0;
}

static const struct dev_pm_ops hi556_pm_ops = {
	SET_RUNTIME_PM_OPS(hi556_runtime_suspend,
			   hi556_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops hi556_internal_ops = {
	.open = hi556_open,
};
#endif

static const struct v4l2_subdev_core_ops hi556_core_ops = {
	.s_power = hi556_s_power,
	.ioctl = hi556_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = hi556_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops hi556_video_ops = {
	.s_stream = hi556_s_stream,
	.g_frame_interval = hi556_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops hi556_pad_ops = {
	.enum_mbus_code = hi556_enum_mbus_code,
	.enum_frame_size = hi556_enum_frame_sizes,
	.enum_frame_interval = hi556_enum_frame_interval,
	.get_fmt = hi556_get_fmt,
	.set_fmt = hi556_set_fmt,
	.get_mbus_config = hi556_g_mbus_config,
};

static const struct v4l2_subdev_ops hi556_subdev_ops = {
	.core	= &hi556_core_ops,
	.video	= &hi556_video_ops,
	.pad	= &hi556_pad_ops,
};

static int hi556_set_exposure_reg(struct hi556 *hi556, u32 exposure)
{
	int ret = 0;
	u32 cal_shutter = 0;

	cal_shutter = exposure >> 1;
	cal_shutter = cal_shutter << 1;

	ret = hi556_write_reg(hi556->client, HI556_REG_GROUP,
				   HI556_REG_VALUE_08BIT, 0x01);
	ret |= hi556_write_reg(hi556->client,
				   HI556_REG_EXPOSURE_H,
				   HI556_REG_VALUE_08BIT,
				   HI556_FETCH_HIGH_BYTE_EXP(cal_shutter));
	ret |= hi556_write_reg(hi556->client,
				   HI556_REG_EXPOSURE_M,
				   HI556_REG_VALUE_08BIT,
				   HI556_FETCH_MIDDLE_BYTE_EXP(cal_shutter));
	ret |= hi556_write_reg(hi556->client,
				   HI556_REG_EXPOSURE_L,
				   HI556_REG_VALUE_08BIT,
				   HI556_FETCH_LOW_BYTE_EXP(cal_shutter));
	ret |= hi556_write_reg(hi556->client, HI556_REG_GROUP,
				   HI556_REG_VALUE_08BIT, 0x00);

	return ret;
}

static int hi556_set_gain_reg(struct hi556 *hi556, u32 a_gain)
{
	int ret = 0;

	ret = hi556_write_reg(hi556->client, HI556_REG_GROUP,
				   HI556_REG_VALUE_08BIT, 0x01);
	ret |= hi556_write_reg(hi556->client, HI556_REG_GAIN,
				   HI556_REG_VALUE_08BIT, a_gain);
	ret |= hi556_write_reg(hi556->client, HI556_REG_GROUP,
				   HI556_REG_VALUE_08BIT, 0x00);

	return ret;
}

static int hi556_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hi556 *hi556 = container_of(ctrl->handler,
					     struct hi556, ctrl_handler);
	struct i2c_client *client = hi556->client;
	s64 max;
	u32 val = 0;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = hi556->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(hi556->exposure,
					 hi556->exposure->minimum, max,
					 hi556->exposure->step,
					 hi556->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set exposure value 0x%x\n", ctrl->val);
		/* 4 least significant bits of expsoure are fractional part */
		ret = hi556_set_exposure_reg(hi556, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_dbg(&client->dev, "set analog gain value 0x%x\n", ctrl->val);
		ret = hi556_set_gain_reg(hi556, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set vb value 0x%x\n", ctrl->val);
		ret = hi556_write_reg(hi556->client, HI556_REG_VTS,
				       HI556_REG_VALUE_16BIT,
				       ctrl->val + hi556->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = hi556_enable_test_pattern(hi556, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = hi556_read_reg(hi556->client, HI556_FLIP_MIRROR_REG,
				       HI556_REG_VALUE_08BIT, &val);
		ret |= hi556_write_reg(hi556->client, HI556_FLIP_MIRROR_REG,
					 HI556_REG_VALUE_08BIT,
					 HI556_FETCH_MIRROR(val, ctrl->val));
		break;
	case V4L2_CID_VFLIP:
		ret = hi556_read_reg(hi556->client, HI556_FLIP_MIRROR_REG,
				       HI556_REG_VALUE_08BIT, &val);
		ret |= hi556_write_reg(hi556->client, HI556_FLIP_MIRROR_REG,
					 HI556_REG_VALUE_08BIT,
					 HI556_FETCH_FLIP(val, ctrl->val));
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops hi556_ctrl_ops = {
	.s_ctrl = hi556_set_ctrl,
};

static int hi556_initialize_controls(struct hi556 *hi556)
{
	const struct hi556_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &hi556->ctrl_handler;
	mode = hi556->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &hi556->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, hi556->pixel_rate, 1, hi556->pixel_rate);

	h_blank = mode->hts_def - mode->width;
	hi556->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (hi556->hblank)
		hi556->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	hi556->vblank = v4l2_ctrl_new_std(handler, &hi556_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				HI556_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	hi556->exposure = v4l2_ctrl_new_std(handler, &hi556_ctrl_ops,
				V4L2_CID_EXPOSURE, HI556_EXPOSURE_MIN,
				exposure_max, HI556_EXPOSURE_STEP,
				mode->exp_def);

	hi556->anal_gain = v4l2_ctrl_new_std(handler, &hi556_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	hi556->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&hi556_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(hi556_test_pattern_menu) - 1,
				0, 0, hi556_test_pattern_menu);

	v4l2_ctrl_new_std(handler, &hi556_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &hi556_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&hi556->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	hi556->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int hi556_check_sensor_id(struct hi556 *hi556,
				  struct i2c_client *client)
{
	struct device *dev = &hi556->client->dev;
	u32 id = 0;
	int ret;

	ret = hi556_read_reg(client, HI556_REG_CHIP_ID,
			      HI556_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected Hi%04x sensor\n", CHIP_ID);

	return 0;
}

static int hi556_configure_regulators(struct hi556 *hi556)
{
	unsigned int i;

	for (i = 0; i < HI556_NUM_SUPPLIES; i++)
		hi556->supplies[i].supply = hi556_supply_names[i];

	return devm_regulator_bulk_get(&hi556->client->dev,
				       HI556_NUM_SUPPLIES,
				       hi556->supplies);
}

static int hi556_parse_of(struct hi556 *hi556)
{
	struct device *dev = &hi556->client->dev;
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

	hi556->lane_num = rval;
	if (hi556->lane_num == 2) {
		hi556->cur_mode = &supported_modes_2lane[0];
		supported_modes = supported_modes_2lane;
		hi556->cfg_num = ARRAY_SIZE(supported_modes_2lane);

		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		hi556->pixel_rate = MIPI_FREQ * 2U * hi556->lane_num / 8U;
		dev_info(dev, "lane_num(%d)  pixel_rate(%u)\n",
				 hi556->lane_num, hi556->pixel_rate);
	} else {
		dev_err(dev, "unsupported lane_num(%d)\n", hi556->lane_num);
		return -1;
	}

	return 0;
}

static int hi556_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct hi556 *hi556;
	struct v4l2_subdev *sd;
	char facing[2] = "b";
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	hi556 = devm_kzalloc(dev, sizeof(*hi556), GFP_KERNEL);
	if (!hi556)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &hi556->module_index);
	if (ret) {
		dev_warn(dev, "could not get module index!\n");
		hi556->module_index = 0;
	}
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &hi556->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &hi556->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &hi556->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	hi556->client = client;

	hi556->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(hi556->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	hi556->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(hi556->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	hi556->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(hi556->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios, maybe no use\n");

	hi556->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(hi556->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = hi556_configure_regulators(hi556);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}
	ret = hi556_parse_of(hi556);
	if (ret != 0)
		return -EINVAL;

	hi556->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(hi556->pinctrl)) {
		hi556->pins_default =
			pinctrl_lookup_state(hi556->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(hi556->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		hi556->pins_sleep =
			pinctrl_lookup_state(hi556->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(hi556->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&hi556->mutex);

	sd = &hi556->subdev;
	v4l2_i2c_subdev_init(sd, client, &hi556_subdev_ops);
	ret = hi556_initialize_controls(hi556);
	if (ret)
		goto err_destroy_mutex;

	ret = __hi556_power_on(hi556);
	if (ret)
		goto err_free_handler;

	ret = hi556_check_sensor_id(hi556, client);
	if (ret < 0) {
		dev_info(&client->dev, "%s(%d) Check id  failed\n"
				  "check following information:\n"
				  "Power/PowerDown/Reset/Mclk/I2cBus !!\n",
				  __func__, __LINE__);
		goto err_power_off;
	}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &hi556_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	hi556->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &hi556->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(hi556->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 hi556->module_index, facing,
		 HI556_NAME, dev_name(sd->dev));

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
	__hi556_power_off(hi556);
err_free_handler:
	v4l2_ctrl_handler_free(&hi556->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&hi556->mutex);

	return ret;
}

static int hi556_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hi556 *hi556 = to_hi556(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&hi556->ctrl_handler);
	mutex_destroy(&hi556->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__hi556_power_off(hi556);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id hi556_of_match[] = {
	{ .compatible = "hynix,hi556" },
	{},
};
MODULE_DEVICE_TABLE(of, hi556_of_match);
#endif

static const struct i2c_device_id hi556_match_id[] = {
	{ "hynix,hi556", 0 },
	{ },
};

static struct i2c_driver hi556_i2c_driver = {
	.driver = {
		.name = HI556_NAME,
		.pm = &hi556_pm_ops,
		.of_match_table = of_match_ptr(hi556_of_match),
	},
	.probe		= &hi556_probe,
	.remove		= &hi556_remove,
	.id_table	= hi556_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&hi556_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&hi556_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Hynix hi556 sensor driver");
MODULE_LICENSE("GPL v2");

