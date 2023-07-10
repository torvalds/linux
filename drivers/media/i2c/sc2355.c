// SPDX-License-Identifier: GPL-2.0
/*
 * SC2355 driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 * V0.1.0: MIPI is ok.
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)
#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_180M			180000000
#define MIPI_FREQ_360M			360000000

#define PIXEL_RATE_WITH_180M		(MIPI_FREQ_180M * 2 / 10)
#define PIXEL_RATE_WITH_360M		(MIPI_FREQ_360M * 2 / 10)

#define SC2355_XVCLK_FREQ		24000000

#define CHIP_ID				    0xeb2c
#define SC2355_REG_CHIP_ID		0x3107

#define SC2355_REG_CTRL_MODE	0x0100
#define SC2355_MODE_SW_STANDBY	0x0
#define SC2355_MODE_STREAMING	BIT(0)

#define SC2355_REG_EXPOSURE		0x3e01
#define	SC2355_EXPOSURE_MIN		6
#define	SC2355_EXPOSURE_STEP	1
#define SC2355_VTS_MAX			0xffff

#define SC2355_REG_COARSE_AGAIN	0x3e09

#define	ANALOG_GAIN_MIN			0x01
#define	ANALOG_GAIN_MAX			0xF8
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x1f

#define SC2355_REG_TEST_PATTERN	0x4501
#define	SC2355_TEST_PATTERN_ENABLE	0xcc
#define	SC2355_TEST_PATTERN_DISABLE	0xc4

#define SC2355_REG_VTS			0x320e

#define REG_NULL			0xFFFF

#define SC2355_REG_VALUE_08BIT		1
#define SC2355_REG_VALUE_16BIT		2
#define SC2355_REG_VALUE_24BIT		3

#define SC2355_NAME			"sc2355"

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define SC2355_FETCH_3RD_BYTE_EXP(VAL) (((VAL) >> 16) & 0xF)	/* 4 Bits */
#define SC2355_FETCH_2ND_BYTE_EXP(VAL) (((VAL) >> 8) & 0xFF)	/* 8 Bits */
#define SC2355_FETCH_1ST_BYTE_EXP(VAL) ((VAL) & 0xFF)	/* 4 Bits */

//fps set
#define SC2355_FPS  (20)

//default exposure
#define EXP_DEFAULT_TIME_US (8000)

#define SC2355_VTS_30_FPS   0x4e2

#define TIME_MS          1000
#define SC2355_VTS       (SC2355_VTS_30_FPS * 30 / SC2355_FPS)

#define SC2355_TIME_TO_EXP_LINE_US(time_us) \
	(uint16_t)(time_us/1000*30*SC2355_VTS_30_FPS/TIME_MS)

#define SC2355_DEFAULT_EXP_REG \
	SC2355_TIME_TO_EXP_LINE_US(EXP_DEFAULT_TIME_US)

#define SC2355_FSYNC_RISING_MARGIN_TIME (600)//us
#define SC2355_FSYNC_RISING_MARGIN \
	SC2355_TIME_TO_EXP_LINE_US(SC2355_FSYNC_RISING_MARGIN_TIME)

#define SC2355_EXP_REG_TO_FSYNC_RISING(exp_reg) \
	(exp_reg - 15 + SC2355_FSYNC_RISING_MARGIN)

#define SC2355_DEFAULT_FSYNC_RISING \
	SC2355_EXP_REG_TO_FSYNC_RISING(SC2355_DEFAULT_EXP_REG)

#define SC2355_DEFAULT_FSYNC_FALLING (0x4c0)
#define SC2355_DEFAULT_FSYNC_FALLING_BINNING (0x4c0/2 + 2)
#define SC2355_FSYNC_RISING_REG (0x3217)

#define SLAVE_MODE
//slave mode max exp time (RB_ROW)
#define EXP_MAX_TIME_US (13*1000)
#define SC2355_SLAVE_RB_ROW SC2355_TIME_TO_EXP_LINE_US(EXP_MAX_TIME_US)

#define BINNING_MODE

static const char *const SC2355_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC2355_NUM_SUPPLIES ARRAY_SIZE(SC2355_supply_names)

enum {
	LINK_FREQ_180M_INDEX,
	LINK_FREQ_360M_INDEX,
};

struct regval {
	u16 addr;
	u8 val;
};

struct SC2355_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_index;
	u64 pixel_rate;
	const struct regval *reg_list;
	u32 lanes;
	u32 bus_fmt;
};

struct SC2355 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC2355_NUM_SUPPLIES];
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
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	struct v4l2_fract	cur_fps;
	u32			cur_vts;
	bool			streaming;
	bool			power_on;
	const struct SC2355_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_SC2355(sd) container_of(sd, struct SC2355, subdev)

/*
 * Xclk 24Mhz
 * Pclk 72Mhz
 * linelength ()
 * framelength ()
 * grabwindow_width 800
 * grabwindow_height 600
 * mipi 1 lane
 * max_framerate 30fps
 * mipi_datarate per lane 360Mbps
 */
static const struct regval SC2355_1lane_10bit_360Mbps_800x600_30fps_regs[] = {
	{0x0103,0x01},
	{0x0100,0x00},
	{0x36e9,0x80},
	{0x36ea,0x0f},
	{0x36eb,0x24},
	{0x36ed,0x14},
	{0x36e9,0x01},
	{0x301f,0x0c},
	{0x303f,0x82},
	{0x3208,0x03},
	{0x3209,0x20},
	{0x320a,0x02},
	{0x320b,0x58},
	{0x3211,0x02},
	{0x3213,0x02},
	{0x3215,0x31},
	{0x3220,0x01},
	{0x3248,0x02},
	{0x3253,0x0a},
	{0x3301,0xff},
	{0x3302,0xff},
	{0x3303,0x10},
	{0x3306,0x28},
	{0x3307,0x02},
	{0x330a,0x00},
	{0x330b,0xb0},
	{0x3318,0x02},
	{0x3320,0x06},
	{0x3321,0x02},
	{0x3326,0x12},
	{0x3327,0x0e},
	{0x3328,0x03},
	{0x3329,0x0f},
	{0x3364,0x4f},
	{0x33b3,0x40},
	{0x33f9,0x2c},
	{0x33fb,0x38},
	{0x33fc,0x0f},
	{0x33fd,0x1f},
	{0x349f,0x03},
	{0x34a6,0x01},
	{0x34a7,0x1f},
	{0x34a8,0x40},
	{0x34a9,0x30},
	{0x34ab,0xa6},
	{0x34ad,0xa6},
	{0x3622,0x60},
	{0x3623,0x40},
	{0x3624,0x61},
	{0x3625,0x08},
	{0x3626,0x03},
	{0x3630,0xa8},
	{0x3631,0x84},
	{0x3632,0x90},
	{0x3633,0x43},
	{0x3634,0x09},
	{0x3635,0x82},
	{0x3636,0x48},
	{0x3637,0xe4},
	{0x3641,0x22},
	{0x3670,0x0f},
	{0x3674,0xc0},
	{0x3675,0xc0},
	{0x3676,0xc0},
	{0x3677,0x86},
	{0x3678,0x88},
	{0x3679,0x8c},
	{0x367c,0x01},
	{0x367d,0x0f},
	{0x367e,0x01},
	{0x367f,0x0f},
	{0x3690,0x63},
	{0x3691,0x63},
	{0x3692,0x73},
	{0x369c,0x01},
	{0x369d,0x1f},
	{0x369e,0x8a},
	{0x369f,0x9e},
	{0x36a0,0xda},
	{0x36a1,0x01},
	{0x36a2,0x03},
	{0x3900,0x0d},
	{0x3904,0x04},
	{0x3905,0x98},
	{0x391b,0x81},
	{0x391c,0x10},
	{0x391d,0x19},
	{0x3933,0x01},
	{0x3934,0x82},
	{0x3940,0x5d},
	{0x3942,0x01},
	{0x3943,0x82},
	{0x3949,0xc8},
	{0x394b,0x64},
	{0x3952,0x02},
	{0x3e00,0x00},
	{0x3e01,0x4d},
	{0x3e02,0xe0},
	{0x4502,0x34},
	{0x4509,0x30},
	{0x450a,0x71},
	{0x4819,0x09},
	{0x481b,0x05},
	{0x481d,0x13},
	{0x481f,0x04},
	{0x4821,0x0a},
	{0x4823,0x05},
	{0x4825,0x04},
	{0x4827,0x05},
	{0x4829,0x08},
	{0x5000,0x46},
	{0x5900,0xf1}, //fix noise
	{0x5901,0x04},

	//vts
	{0x320e,(SC2355_VTS>>8)&0xff},
	{0x320f,SC2355_VTS&0xff},

	//exp
	{0x3e00,SC2355_FETCH_3RD_BYTE_EXP(SC2355_DEFAULT_EXP_REG)},
	{0x3e01,SC2355_FETCH_2ND_BYTE_EXP(SC2355_DEFAULT_EXP_REG)},
	{0x3e02,SC2355_FETCH_1ST_BYTE_EXP(SC2355_DEFAULT_EXP_REG)},

	//[flip]
	{0x3221,0x3 << 5},

	//[gain=1]
	{0x3e09,0x00},

	//fsync
#ifndef SLAVE_MODE
	{0x300b,0x44},//FSYNC out
	{0x3217,SC2355_DEFAULT_FSYNC_RISING},
	{0x322e,(SC2355_DEFAULT_FSYNC_FALLING_BINNING>>8)&0xff},
	{0x322f,SC2355_DEFAULT_FSYNC_FALLING_BINNING&0xff},
#else
	{0x3222, 0x01 << 1},//slave mode
	{0x300a, 0x00 << 2},//input mode

	{0x3230, (SC2355_SLAVE_RB_ROW >> 8)&0xff},
	{0x3231, SC2355_SLAVE_RB_ROW & 0xff},
#endif

	{REG_NULL, 0x00},
};


/*
 * Xclk 24Mhz
 * Pclk 72Mhz
 * linelength ()
 * framelength ()
 * grabwindow_width 1600
 * grabwindow_height 1200
 * mipi 1 lane
 * max_framerate 30fps
 */
static const struct regval SC2355_1lane_10bit_1600x1200_30fps_regs[] = {
	{0x0103,0x01},
	{0x0100,0x00},

	{0x301f,0x01},
	{0x3248,0x02},
	{0x3253,0x0a},
	{0x3301,0xff},
	{0x3302,0xff},
	{0x3303,0x10},
	{0x3306,0x28},
	{0x3307,0x02},
	{0x330a,0x00},
	{0x330b,0xb0},
	{0x3318,0x02},
	{0x3320,0x06},
	{0x3321,0x02},
	{0x3326,0x12},
	{0x3327,0x0e},
	{0x3328,0x03},
	{0x3329,0x0f},
	{0x3364,0x4f},
	{0x33b3,0x40},
	{0x33f9,0x2c},
	{0x33fb,0x38},
	{0x33fc,0x0f},
	{0x33fd,0x1f},
	{0x349f,0x03},
	{0x34a6,0x01},
	{0x34a7,0x1f},
	{0x34a8,0x40},
	{0x34a9,0x30},
	{0x34ab,0xa6},
	{0x34ad,0xa6},
	{0x3622,0x60},
	{0x3623,0x40},
	{0x3624,0x61},
	{0x3625,0x08},
	{0x3626,0x03},
	{0x3630,0xa8},
	{0x3631,0x84},
	{0x3632,0x90},
	{0x3633,0x43},
	{0x3634,0x09},
	{0x3635,0x82},
	{0x3636,0x48},
	{0x3637,0xe4},
	{0x3641,0x22},
	{0x3670,0x0f},
	{0x3674,0xc0},
	{0x3675,0xc0},
	{0x3676,0xc0},
	{0x3677,0x86},
	{0x3678,0x88},
	{0x3679,0x8c},
	{0x367c,0x01},
	{0x367d,0x0f},
	{0x367e,0x01},
	{0x367f,0x0f},
	{0x3690,0x63},
	{0x3691,0x63},
	{0x3692,0x73},
	{0x369c,0x01},
	{0x369d,0x1f},
	{0x369e,0x8a},
	{0x369f,0x9e},
	{0x36a0,0xda},
	{0x36a1,0x01},
	{0x36a2,0x03},
	{0x36e9,0x01},
	{0x36ea,0x0f},
	{0x36eb,0x25},
	{0x36ed,0x04},
	{0x3900,0x0d},
	{0x3904,0x06},
	{0x3905,0x98},
	{0x391b,0x81},
	{0x391c,0x10},
	{0x391d,0x19},
	{0x3933,0x01},
	{0x3934,0x82},
	{0x3940,0x5d},
	{0x3942,0x01},
	{0x3943,0x82},
	{0x3949,0xc8},
	{0x394b,0x64},
	{0x3952,0x02},

	//vts
	{0x320e,(SC2355_VTS>>8)&0xff},
	{0x320f,SC2355_VTS&0xff},

	{0x3e00, SC2355_FETCH_3RD_BYTE_EXP(SC2355_DEFAULT_EXP_REG)},
	{0x3e01, SC2355_FETCH_2ND_BYTE_EXP(SC2355_DEFAULT_EXP_REG)},
	{0x3e02, SC2355_FETCH_1ST_BYTE_EXP(SC2355_DEFAULT_EXP_REG)},
	{0x4502,0x34},
	{0x4509,0x30},
	{0x450a,0x71},

	//[flip]
	{0x3221,0x3 << 5},
	//[gain=2]
	{0x3e09,0x01},

#ifndef SLAVE_MODE
	{0x300b,0x44},//FSYNC out
	{0x3217,SC2355_DEFAULT_FSYNC_RISING},
	{0x322e,(SC2355_DEFAULT_FSYNC_FALLING>>8)&0xff},
	{0x322f,SC2355_DEFAULT_FSYNC_FALLING&0xff},
#else
	{0x3222, 0x01 << 1},//slave mode
	{0x300a, 0x00 << 2},//input mode

	{0x3230, (SC2355_SLAVE_RB_ROW >> 8)&0xff},//input mode
	{0x3231, SC2355_SLAVE_RB_ROW & 0xff},//input mode
#endif
};

static const struct SC2355_mode supported_modes[] = {
#ifdef BINNING_MODE
	{
		.width = 800,
		.height = 600,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = SC2355_DEFAULT_EXP_REG,
		.hts_def = 0x640,
		.vts_def = SC2355_VTS,
		.link_freq_index = LINK_FREQ_360M_INDEX,
		.pixel_rate      = PIXEL_RATE_WITH_360M,
		.reg_list = SC2355_1lane_10bit_360Mbps_800x600_30fps_regs,
		.lanes    = 1,
		.bus_fmt  = MEDIA_BUS_FMT_Y10_1X10,
	},
#else
	{
		.width = 1600,
		.height = 1200,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = SC2355_DEFAULT_EXP_REG,
		.hts_def = 0x640,
		.vts_def = SC2355_VTS,
		.link_freq_index = LINK_FREQ_360M_INDEX,
		.pixel_rate      = PIXEL_RATE_WITH_360M,
		.reg_list = SC2355_1lane_10bit_1600x1200_30fps_regs,
		.lanes    = 1,
		.bus_fmt  = MEDIA_BUS_FMT_Y10_1X10,
	},
#endif
};

static const char *const SC2355_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_180M,
	MIPI_FREQ_360M,
};

/* Write registers up to 4 at a time */
static int SC2355_write_reg(struct i2c_client *client,
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

static int SC2355_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = SC2355_write_reg(client, regs[i].addr,
					SC2355_REG_VALUE_08BIT, regs[i].val);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int SC2355_read_reg(struct i2c_client *client,
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

static int SC2355_get_reso_dist(const struct SC2355_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct SC2355_mode *
SC2355_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = SC2355_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist < cur_best_fit_dist) &&
		    (supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}
	return &supported_modes[cur_best_fit];
}

static int SC2355_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct SC2355 *SC2355 = to_SC2355(sd);
	const struct SC2355_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&SC2355->mutex);

	mode = SC2355_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&SC2355->mutex);
		return -ENOTTY;
#endif
	} else {
		SC2355->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(SC2355->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(SC2355->vblank, vblank_def,
					 SC2355_VTS_MAX - mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl_int64(SC2355->pixel_rate, mode->pixel_rate);
		__v4l2_ctrl_s_ctrl(SC2355->link_freq, mode->link_freq_index);
		SC2355->cur_vts = mode->vts_def;
		SC2355->cur_fps = mode->max_fps;
	}

	mutex_unlock(&SC2355->mutex);

	return 0;
}

static int SC2355_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct SC2355 *SC2355 = to_SC2355(sd);
	const struct SC2355_mode *mode = SC2355->cur_mode;

	mutex_lock(&SC2355->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&SC2355->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&SC2355->mutex);

	return 0;
}

static int SC2355_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct SC2355 *SC2355 = to_SC2355(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = SC2355->cur_mode->bus_fmt;

	return 0;
}

static int SC2355_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int SC2355_enable_test_pattern(struct SC2355 *SC2355, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | SC2355_TEST_PATTERN_ENABLE;
	else
		val = SC2355_TEST_PATTERN_DISABLE;

	return SC2355_write_reg(SC2355->client, SC2355_REG_TEST_PATTERN,
				 SC2355_REG_VALUE_08BIT, val);
}

static void SC2355_get_module_inf(struct SC2355 *SC2355,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC2355_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, SC2355->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, SC2355->len_name, sizeof(inf->base.lens));
}

static long SC2355_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct SC2355 *SC2355 = to_SC2355(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		SC2355_get_module_inf(SC2355, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = SC2355_write_reg(SC2355->client, SC2355_REG_CTRL_MODE,
						SC2355_REG_VALUE_08BIT, SC2355_MODE_STREAMING);
		else
			ret = SC2355_write_reg(SC2355->client, SC2355_REG_CTRL_MODE,
						SC2355_REG_VALUE_08BIT, SC2355_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long SC2355_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = SC2355_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;

		ret = SC2355_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int SC2355_set_ctrl_gain(struct SC2355 *SC2355, u32 a_gain)
{
	int ret = 0;

	if (a_gain > 1)
		a_gain = ((a_gain + 1) >> 1) << 1;
	a_gain -=1;

	ret |= SC2355_write_reg(SC2355->client,
				 SC2355_REG_COARSE_AGAIN,
				 SC2355_REG_VALUE_08BIT,
				 a_gain);

	return ret;
}


static int __SC2355_start_stream(struct SC2355 *SC2355)
{
	int ret;

	ret = SC2355_write_array(SC2355->client, SC2355->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&SC2355->mutex);
	ret = v4l2_ctrl_handler_setup(&SC2355->ctrl_handler);
	mutex_lock(&SC2355->mutex);
	if (ret)
		return ret;

	ret = SC2355_write_reg(SC2355->client, SC2355_REG_CTRL_MODE,
			SC2355_REG_VALUE_08BIT, SC2355_MODE_STREAMING);

	return ret;
}

static int __SC2355_stop_stream(struct SC2355 *SC2355)
{
	return SC2355_write_reg(SC2355->client, SC2355_REG_CTRL_MODE,
				 SC2355_REG_VALUE_08BIT, SC2355_MODE_SW_STANDBY);
}

static int SC2355_s_stream(struct v4l2_subdev *sd, int on)
{
	struct SC2355 *SC2355 = to_SC2355(sd);
	struct i2c_client *client = SC2355->client;
	unsigned int fps;
	int ret = 0;

	mutex_lock(&SC2355->mutex);
	on = !!on;
	if (on == SC2355->streaming)
		goto unlock_and_return;

	fps = DIV_ROUND_CLOSEST(SC2355->cur_mode->max_fps.denominator,
				SC2355->cur_mode->max_fps.numerator);

	dev_info(&SC2355->client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
		 SC2355->cur_mode->width,
		 SC2355->cur_mode->height,
		 fps);

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __SC2355_start_stream(SC2355);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__SC2355_stop_stream(SC2355);
		pm_runtime_put(&client->dev);
	}

	SC2355->streaming = on;

unlock_and_return:
	mutex_unlock(&SC2355->mutex);

	return ret;
}

static int SC2355_s_power(struct v4l2_subdev *sd, int on)
{
	struct SC2355 *SC2355 = to_SC2355(sd);
	struct i2c_client *client = SC2355->client;
	int ret = 0;

	mutex_lock(&SC2355->mutex);

	/* If the power state is not modified - no work to do. */
	if (SC2355->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		SC2355->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		SC2355->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&SC2355->mutex);

	return ret;
}

static int SC2355_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct SC2355 *SC2355 = to_SC2355(sd);
	const struct SC2355_mode *mode = SC2355->cur_mode;

	if (SC2355->streaming)
		fi->interval = SC2355->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 SC2355_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC2355_XVCLK_FREQ / 1000 / 1000);
}

static int __SC2355_power_on(struct SC2355 *SC2355)
{
	int ret;
	u32 delay_us;
	struct device *dev = &SC2355->client->dev;

	if (!IS_ERR_OR_NULL(SC2355->pins_default)) {
		ret = pinctrl_select_state(SC2355->pinctrl,
					   SC2355->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = clk_set_rate(SC2355->xvclk, SC2355_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(SC2355->xvclk) != SC2355_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(SC2355->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(SC2355_NUM_SUPPLIES, SC2355->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(SC2355->reset_gpio))
		gpiod_set_value_cansleep(SC2355->reset_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR(SC2355->pwdn_gpio))
		gpiod_set_value_cansleep(SC2355->pwdn_gpio, 1);

	if (!IS_ERR(SC2355->reset_gpio))
		gpiod_set_value_cansleep(SC2355->reset_gpio, 0);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = SC2355_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(SC2355->xvclk);

	return ret;
}

static void __SC2355_power_off(struct SC2355 *SC2355)
{
	int ret;

	if (!IS_ERR(SC2355->reset_gpio))
		gpiod_set_value_cansleep(SC2355->reset_gpio, 1);

	if (!IS_ERR(SC2355->pwdn_gpio))
		gpiod_set_value_cansleep(SC2355->pwdn_gpio, 0);
	clk_disable_unprepare(SC2355->xvclk);
	if (!IS_ERR_OR_NULL(SC2355->pins_sleep)) {
		ret = pinctrl_select_state(SC2355->pinctrl,
					   SC2355->pins_sleep);
		if (ret < 0)
			dev_dbg(&SC2355->client->dev, "could not set pins\n");
	}
	regulator_bulk_disable(SC2355_NUM_SUPPLIES, SC2355->supplies);
}

static int SC2355_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct SC2355 *SC2355 = to_SC2355(sd);

	return __SC2355_power_on(SC2355);
}

static int SC2355_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct SC2355 *SC2355 = to_SC2355(sd);

	__SC2355_power_off(SC2355);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int SC2355_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct SC2355 *SC2355 = to_SC2355(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct SC2355_mode *def_mode = &supported_modes[0];

	mutex_lock(&SC2355->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&SC2355->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int SC2355_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int SC2355_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *config)
{
	u32 val = 0;
	struct SC2355 *SC2355 = to_SC2355(sd);

	val = 1 << (SC2355->cur_mode->lanes - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops SC2355_pm_ops = {
	SET_RUNTIME_PM_OPS(SC2355_runtime_suspend,
			   SC2355_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops SC2355_internal_ops = {
	.open = SC2355_open,
};
#endif

static const struct v4l2_subdev_core_ops SC2355_core_ops = {
	.s_power = SC2355_s_power,
	.ioctl = SC2355_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = SC2355_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops SC2355_video_ops = {
	.s_stream = SC2355_s_stream,
	.g_frame_interval = SC2355_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops SC2355_pad_ops = {
	.enum_mbus_code = SC2355_enum_mbus_code,
	.enum_frame_size = SC2355_enum_frame_sizes,
	.enum_frame_interval = SC2355_enum_frame_interval,
	.get_fmt = SC2355_get_fmt,
	.set_fmt = SC2355_set_fmt,
	.get_mbus_config = SC2355_g_mbus_config,
};

static const struct v4l2_subdev_ops SC2355_subdev_ops = {
	.core	= &SC2355_core_ops,
	.video	= &SC2355_video_ops,
	.pad	= &SC2355_pad_ops,
};

static void SC2355_modify_fps_info(struct SC2355 *SC2355)
{
	const struct SC2355_mode *mode = SC2355->cur_mode;

	SC2355->cur_fps.denominator = mode->max_fps.denominator * SC2355->cur_vts /
				       mode->vts_def;
}

static int SC2355_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct SC2355 *SC2355 = container_of(ctrl->handler,
					       struct SC2355, ctrl_handler);
	struct i2c_client *client = SC2355->client;
	s64 max;
	int ret = 0;
#ifndef SLAVE_MODE
	u16 rising_reg;
#endif

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = SC2355->cur_mode->height + ctrl->val - 6;
		__v4l2_ctrl_modify_range(SC2355->exposure,
					 SC2355->exposure->minimum, max,
					 SC2355->exposure->step,
					 SC2355->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = SC2355_write_reg(SC2355->client, SC2355_REG_EXPOSURE,
					SC2355_REG_VALUE_16BIT, ctrl->val << 4);

#ifndef SLAVE_MODE
		/* set fsync rising */
		rising_reg = SC2355_EXP_REG_TO_FSYNC_RISING(ctrl->val);
		if (rising_reg > 0xff) {
			rising_reg = 0xff;
			dev_warn(&client->dev,
					"error: rising reg exceed max val 0xff.\n");
		}

		dev_info(&client->dev, "rising: reg:%d\n", rising_reg);

		ret |= SC2355_write_reg(SC2355->client, SC2355_FSYNC_RISING_REG,
					SC2355_REG_VALUE_08BIT, rising_reg);
#endif

		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = SC2355_set_ctrl_gain(SC2355, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = SC2355_write_reg(SC2355->client, SC2355_REG_VTS,
					SC2355_REG_VALUE_16BIT,
					ctrl->val + SC2355->cur_mode->height);
		if (!ret)
			SC2355->cur_vts = ctrl->val + SC2355->cur_mode->height;
		if (SC2355->cur_vts != SC2355->cur_mode->vts_def)
			SC2355_modify_fps_info(SC2355);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = SC2355_enable_test_pattern(SC2355, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops SC2355_ctrl_ops = {
	.s_ctrl = SC2355_set_ctrl,
};

static int SC2355_initialize_controls(struct SC2355 *SC2355)
{
	const struct SC2355_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &SC2355->ctrl_handler;
	mode = SC2355->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &SC2355->mutex;

	SC2355->link_freq = v4l2_ctrl_new_int_menu(handler,
						NULL, V4L2_CID_LINK_FREQ,
						ARRAY_SIZE(link_freq_menu_items) - 1, 0,
						link_freq_menu_items);

	SC2355->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
						V4L2_CID_PIXEL_RATE,
						0, PIXEL_RATE_WITH_360M,
						1, mode->pixel_rate);

	__v4l2_ctrl_s_ctrl(SC2355->link_freq, mode->pixel_rate);

	h_blank = mode->hts_def - mode->width;
	SC2355->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (SC2355->hblank)
		SC2355->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	SC2355->cur_vts = mode->vts_def;
	SC2355->cur_fps = mode->max_fps;
	SC2355->vblank = v4l2_ctrl_new_std(handler, &SC2355_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    SC2355_VTS_MAX - mode->height,
					    1, vblank_def);

	exposure_max = mode->vts_def - 6;
	SC2355->exposure = v4l2_ctrl_new_std(handler, &SC2355_ctrl_ops,
					      V4L2_CID_EXPOSURE, SC2355_EXPOSURE_MIN,
					      exposure_max, SC2355_EXPOSURE_STEP,
					      mode->exp_def);

	SC2355->anal_gain = v4l2_ctrl_new_std(handler, &SC2355_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
					       ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
					       ANALOG_GAIN_DEFAULT);

	SC2355->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							     &SC2355_ctrl_ops, V4L2_CID_TEST_PATTERN,
							     ARRAY_SIZE(SC2355_test_pattern_menu) - 1,
							     0, 0, SC2355_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&SC2355->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	SC2355->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int SC2355_check_sensor_id(struct SC2355 *SC2355,
				   struct i2c_client *client)
{
	struct device *dev = &SC2355->client->dev;
	u32 id = 0;
	int ret;

	ret = SC2355_read_reg(client, SC2355_REG_CHIP_ID,
			       SC2355_REG_VALUE_16BIT, &id);
	if (ret || id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC2355 CHIP ID = 0x%04x sensor\n", CHIP_ID);

	return 0;
}

static int SC2355_configure_regulators(struct SC2355 *SC2355)
{
	unsigned int i;

	for (i = 0; i < SC2355_NUM_SUPPLIES; i++)
		SC2355->supplies[i].supply = SC2355_supply_names[i];

	return devm_regulator_bulk_get(&SC2355->client->dev,
				       SC2355_NUM_SUPPLIES,
				       SC2355->supplies);
}

static int SC2355_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct SC2355 *SC2355;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	SC2355 = devm_kzalloc(dev, sizeof(*SC2355), GFP_KERNEL);
	if (!SC2355)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &SC2355->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &SC2355->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &SC2355->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &SC2355->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	SC2355->client = client;
	SC2355->cur_mode = &supported_modes[0];

	SC2355->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(SC2355->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	SC2355->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(SC2355->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	SC2355->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(SC2355->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");
	ret = SC2355_configure_regulators(SC2355);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	SC2355->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(SC2355->pinctrl)) {
		SC2355->pins_default =
			pinctrl_lookup_state(SC2355->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(SC2355->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		SC2355->pins_sleep =
			pinctrl_lookup_state(SC2355->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(SC2355->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}
	mutex_init(&SC2355->mutex);

	sd = &SC2355->subdev;
	v4l2_i2c_subdev_init(sd, client, &SC2355_subdev_ops);
	ret = SC2355_initialize_controls(SC2355);
	if (ret)
		goto err_destroy_mutex;

	ret = __SC2355_power_on(SC2355);
	if (ret)
		goto err_free_handler;

	ret = SC2355_check_sensor_id(SC2355, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &SC2355_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	SC2355->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &SC2355->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(SC2355->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 SC2355->module_index, facing,
		 SC2355_NAME, dev_name(sd->dev));
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
	__SC2355_power_off(SC2355);
err_free_handler:
	v4l2_ctrl_handler_free(&SC2355->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&SC2355->mutex);

	return ret;
}

static int SC2355_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct SC2355 *SC2355 = to_SC2355(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&SC2355->ctrl_handler);
	mutex_destroy(&SC2355->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__SC2355_power_off(SC2355);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id SC2355_of_match[] = {
	{ .compatible = "smartsens,sc2355" },
	{},
};
MODULE_DEVICE_TABLE(of, SC2355_of_match);
#endif

static const struct i2c_device_id SC2355_match_id[] = {
	{ "smartsens,sc2355", 0 },
	{ },
};

static struct i2c_driver SC2355_i2c_driver = {
	.driver = {
		.name = SC2355_NAME,
		.pm = &SC2355_pm_ops,
		.of_match_table = of_match_ptr(SC2355_of_match),
	},
	.probe		= &SC2355_probe,
	.remove		= &SC2355_remove,
	.id_table	= SC2355_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&SC2355_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&SC2355_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Smartsens SC2355 sensor driver");
MODULE_LICENSE("GPL");
