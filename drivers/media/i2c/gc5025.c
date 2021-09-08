// SPDX-License-Identifier: GPL-2.0
/*
 * gc5025 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 add quick stream on/off
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
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC5025_LANES			2
#define GC5025_BITS_PER_SAMPLE		10
#define GC5025_LINK_FREQ_MHZ		432000000
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define GC5025_PIXEL_RATE		(GC5025_LINK_FREQ_MHZ * 2 * 2 / 10)
#define GC5025_XVCLK_FREQ		24000000

#define CHIP_ID				0x5025
#define GC5025_REG_CHIP_ID_H		0xf0
#define GC5025_REG_CHIP_ID_L		0xf1

#define GC5025_REG_SET_PAGE		0xfe
#define GC5025_SET_PAGE_ONE		0x00

#define GC5025_REG_CTRL_MODE		0x3f
#define GC5025_MODE_SW_STANDBY		0x01
#define GC5025_MODE_STREAMING		0x91

#define GC5025_REG_EXPOSURE_H		0x03
#define GC5025_REG_EXPOSURE_L		0x04
#define	GC5025_EXPOSURE_MIN		4
#define	GC5025_EXPOSURE_STEP		1
#define GC5025_VTS_MAX			0x1fff

#define GC5025_REG_AGAIN		0xb6
#define GC5025_REG_DGAIN_INT		0xb1
#define GC5025_REG_DGAIN_FRAC		0xb2
#define GC5025_GAIN_MIN			64
#define GC5025_GAIN_MAX			1024
#define GC5025_GAIN_STEP		1
#define GC5025_GAIN_DEFAULT		64

#define GC5025_REG_VTS_H		0x07
#define GC5025_REG_VTS_L		0x08

#define REG_NULL			0xFF

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define GC5025_NAME			"gc5025"

static const char * const gc5025_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define GC5025_NUM_SUPPLIES ARRAY_SIZE(gc5025_supply_names)

#define IMAGE_NORMAL_MIRROR
#define DD_PARAM_QTY_5025	200
#define INFO_ROM_START_5025	0x08
#define INFO_WIDTH_5025		0x08
#define WB_ROM_START_5025	0x88
#define WB_WIDTH_5025		0x05
#define GOLDEN_ROM_START_5025	0xe0
#define GOLDEN_WIDTH_5025	0x05
#define WINDOW_WIDTH		0x0a30
#define WINDOW_HEIGHT		0x079c

struct gc5025_otp_info {
	u32 flag; //bit[7]: info bit[6]:wb bit[3]:dd
	u32 module_id;
	u32 lens_id;
	u16 vcm_id;
	u16 vcm_driver_id;
	u32 year;
	u32 month;
	u32 day;
	u32 rg_ratio;
	u32 bg_ratio;
	u32 golden_rg;
	u32 golden_bg;
	u16 dd_param_x[DD_PARAM_QTY_5025];
	u16 dd_param_y[DD_PARAM_QTY_5025];
	u16 dd_param_type[DD_PARAM_QTY_5025];
	u16 dd_cnt;
};

struct gc5025_id_name {
	u32 id;
	char name[RKMODULE_NAME_LEN];
};

static const struct gc5025_id_name gc5025_module_info[] = {
	{0x0d, "CameraKing"},
	{0x00, "Unknown"}
};

static const struct gc5025_id_name gc5025_lens_info[] = {
	{0xa9, "CK5502"},
	{0x00, "Unknown"}
};

struct regval {
	u8 addr;
	u8 val;
};

struct gc5025_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct gc5025 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct gpio_desc	*power_gpio;
	struct regulator_bulk_data supplies[GC5025_NUM_SUPPLIES];

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
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct gc5025_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32 Dgain_ratio;
	bool DR_State;
	struct gc5025_otp_info *otp;
	struct rkmodule_inf	module_inf;
	struct rkmodule_awb_cfg	awb_cfg;
};

#define to_gc5025(sd) container_of(sd, struct gc5025, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval gc5025_2592x1944_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 656Mbps
 */
static const struct regval gc5025_global_regs[] = {
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xf7, 0x01},
	{0xf8, 0x11},
	{0xf9, 0x00},
	{0xfa, 0xa0},
	{0xfc, 0x2a},
	{0xfe, 0x03},
	{0x01, 0x07},
	{0xfc, 0x2e},
	{0xfe, 0x00},
	{0x88, 0x03},
	{0x03, 0x07},
	{0x04, 0xC0},
	{0x05, 0x02},
	{0x06, 0x58},
	{0x08, 0x20},
	{0x0a, 0x1c},
	{0x0c, 0x04},
	{0x0d, 0x07},
	{0x0e, 0x9c},
	{0x0f, 0x0a},
	{0x10, 0x30},
	{0x17, 0xc0},
	{0x18, 0x02},
	{0x19, 0x17},
	{0x1a, 0x1a},
	{0x1e, 0x90},
	{0x1f, 0xb0},
	{0x20, 0x2b},
	{0x21, 0x2b},
	{0x26, 0x2b},
	{0x25, 0xc1},
	{0x27, 0x64},
	{0x28, 0x00},
	{0x29, 0x3f},
	{0x2b, 0x80},
	{0x30, 0x11},
	{0x31, 0x20},
	{0x32, 0xa0},
	{0x33, 0x00},
	{0x34, 0x55},
	{0x3a, 0x00},
	{0x3b, 0x00},
	{0x81, 0x60},
	{0xcb, 0x02},
	{0xcd, 0x2d},
	{0xcf, 0x50},
	{0xd0, 0xb3},
	{0xd1, 0x18},
	{0xd9, 0xaa},
	{0xdc, 0x03},
	{0xdd, 0xaa},
	{0xe0, 0x00},
	{0xe1, 0x0a},
	{0xe3, 0x2a},
	{0xe4, 0xa0},
	{0xe5, 0x06},
	{0xe6, 0x10},
	{0xe7, 0xc2},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0x80, 0x10},
	{0x89, 0x03},
	{0xfe, 0x01},
	{0x88, 0xf7},
	{0x8a, 0x03},
	{0x8e, 0xc7},
	{0xfe, 0x00},
	{0x40, 0x22},
	{0x41, 0x28},
	{0x42, 0x04},
	{0x4e, 0x0f},
	{0x4f, 0xf0},
	{0x67, 0x0c},
	{0xae, 0x40},
	{0xaf, 0x04},
	{0x60, 0x00},
	{0x61, 0x80},
	{0xb0, 0x58},
	{0xb1, 0x01},
	{0xb2, 0x00},
	{0xb6, 0x00},
	{0x91, 0x00},
	{0x92, 0x02},
	{0x94, 0x03},
	{0xfe, 0x03},
	{0x02, 0x03},
	{0x03, 0x8e},
	{0x06, 0x80},
	{0x15, 0x00},
	{0x16, 0x09},
	{0x18, 0x0a},
	{0x21, 0x10},
	{0x22, 0x05},
	{0x23, 0x20},
	{0x24, 0x02},
	{0x25, 0x20},
	{0x26, 0x08},
	{0x29, 0x06},
	{0x2a, 0x0a},
	{0x2b, 0x08},
	{0xfe, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval gc5025_doublereset_reg[] = {
	{0xfe, 0x00},
	{0x1c, 0x1c},
	{0x2f, 0x4a},
	{0x38, 0x02},
	{0x39, 0x00},
	{0x3c, 0x02},
	{0x3d, 0x02},
	{0xd3, 0xcc},
	{0x43, 0x03},
	{0x1d, 0x13},
	{REG_NULL, 0x00},
};

static const struct regval gc5025_disable_doublereset_reg[] = {
	{0xfe, 0x00},
	{0x1c, 0x2c},
	{0x2f, 0x4d},
	{0x38, 0x04},
	{0x39, 0x02},
	{0x3c, 0x08},
	{0x3d, 0x0f},
	{0xd3, 0xc4},
	{0x43, 0x08},
	{0x1d, 0x00},
	{REG_NULL, 0x00},
};

static const struct gc5025_mode supported_modes[] = {
	{
		.width = 2592,
		.height = 1944,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x07C0,
		.hts_def = 0x12C0,
		.vts_def = 0x07D0,
		.reg_list = gc5025_2592x1944_regs,
	},
};

static const s64 link_freq_menu_items[] = {
	GC5025_LINK_FREQ_MHZ
};

/* Write registers up to 4 at a time */
static int gc5025_write_reg(struct i2c_client *client, u8 reg, u8 val)
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
		"gc5025 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int gc5025_write_array(struct i2c_client *client,
				const struct regval *regs)
{
	u32 i = 0;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc5025_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int gc5025_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"gc5025 read reg:0x%x failed !\n", reg);

	return ret;
}

static int gc5025_get_reso_dist(const struct gc5025_mode *mode,
	struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct gc5025_mode *
gc5025_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = gc5025_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc5025_set_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	struct gc5025 *gc5025 = to_gc5025(sd);
	const struct gc5025_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc5025->mutex);

	mode = gc5025_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc5025->mutex);
		return -ENOTTY;
#endif
	} else {
		gc5025->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc5025->hblank, h_blank,
			h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc5025->vblank, vblank_def,
			GC5025_VTS_MAX - mode->height,
			1, vblank_def);
	}

	mutex_unlock(&gc5025->mutex);

	return 0;
}

static int gc5025_get_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	struct gc5025 *gc5025 = to_gc5025(sd);
	const struct gc5025_mode *mode = gc5025->cur_mode;

	mutex_lock(&gc5025->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc5025->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc5025->mutex);

	return 0;
}

static int gc5025_enum_mbus_code(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SRGGB10_1X10;

	return 0;
}

static int gc5025_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int gc5025_g_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_frame_interval *fi)
{
	struct gc5025 *gc5025 = to_gc5025(sd);
	const struct gc5025_mode *mode = gc5025->cur_mode;

	mutex_lock(&gc5025->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc5025->mutex);

	return 0;
}

static int gc5025_otp_read_reg(struct i2c_client *client,
	int page,
	int address)
{
	int ret = 0;
	u8 val = 0;
	u8 addr_high = 0;

	ret = gc5025_write_reg(client, 0xfe, 0x00);
	ret |= gc5025_read_reg(client, 0xd4, &addr_high);
	switch (page) {
	case 0:
		addr_high &= 0xfb;
		break;
	case 1:
		addr_high |= 0x04;
		break;
	default:
		break;
	}
	addr_high &= 0xfc;
	addr_high |= (address & 0x300) >> 8;
	ret |= gc5025_write_reg(client, 0xD4, addr_high);
	ret |= gc5025_write_reg(client, 0xD5, address & 0xff);
	ret |= gc5025_write_reg(client, 0xF3, 0x20);
	ret |= gc5025_read_reg(client, 0xD7, &val);
	if (ret != 0)
		return ret;
	return val;
}

static int gc5025_otp_enable(struct gc5025 *gc5025)
{
	struct i2c_client *client = gc5025->client;
	u8 otp_clk = 0;
	u8 otp_en = 0;
	int ret = 0;

	ret  = gc5025_write_reg(client, 0xfe, 0x00);
	ret |= gc5025_write_reg(client, 0xf7, 0x01);
	ret |= gc5025_write_reg(client, 0xf8, 0x11);
	ret |= gc5025_write_reg(client, 0xf9, 0x00);
	ret |= gc5025_write_reg(client, 0xfa, 0xa0);
	ret |= gc5025_write_reg(client, 0xfc, 0x2a);
	ret |= gc5025_write_reg(client, 0xfe, 0x03);
	ret |= gc5025_write_reg(client, 0x01, 0x07);
	ret |= gc5025_write_reg(client, 0xfc, 0x2e);
	ret |= gc5025_write_reg(client, 0xfe, 0x00);
	usleep_range(10, 20);
	ret |= gc5025_write_reg(client, 0x88, 0x03);
	ret |= gc5025_write_reg(client, 0xe7, 0xcc);
	ret |= gc5025_write_reg(client, 0xfc, 0x2e);
	ret |= gc5025_write_reg(client, 0xfa, 0xb0);

	ret |= gc5025_read_reg(client, 0xfa, &otp_clk);
	ret |= gc5025_read_reg(client, 0xd4, &otp_en);
	otp_clk |= 0x10;
	otp_en |= 0x80;
	ret |= gc5025_write_reg(client, 0xfa, otp_clk);
	ret |= gc5025_write_reg(client, 0xd4, otp_en);
	usleep_range(100, 200);
	return ret;
}

static int gc5025_otp_disable(struct gc5025 *gc5025)
{
	struct i2c_client *client = gc5025->client;
	u8 otp_clk = 0;
	u8 otp_en = 0;
	int ret = 0;

	ret = gc5025_read_reg(client, 0xfa, &otp_clk);
	ret |= gc5025_read_reg(client, 0xd4, &otp_en);
	otp_clk &= 0xef;
	otp_en &= 0x7f;
	ret |= gc5025_write_reg(client, 0xfa, otp_clk);
	ret |= gc5025_write_reg(client, 0xd4, otp_en);
	return ret;
}

static int gc5025_otp_read(struct gc5025 *gc5025)
{
	int otp_flag, i, j, index, temp, tmpH, tmpL;
	struct gc5025_otp_info *otp_p;
	struct device *dev = &gc5025->client->dev;
	struct i2c_client *client = gc5025->client;
	int checksum = 0;
	int page = 0;
	int total_number = 0;
	u8 m_DD_Otp_Value[182];
	u16 dd_rom_start, offset;
	u8 info_start_add, wb_start_add, golden_start_add;
	u8 check_dd_flag, type;
	u8 dd0 = 0, dd1 = 0, dd2 = 0;
	u16 x, y;
	int cnt = 0;

	otp_p = devm_kzalloc(dev, sizeof(*otp_p), GFP_KERNEL);
	if (!otp_p)
		return -ENOMEM;

	/* OTP info and awb*/
	otp_flag = gc5025_otp_read_reg(client, 1, 0x00);
	for (index = 0; index < 2; index++) {
		switch ((otp_flag >> (4 + 2 * index)) & 0x03) {
		case 0x00:
			dev_err(dev, "%s GC5025_OTP_INFO group %d is Empty!\n",
				__func__, index + 1);
			break;
		case 0x01:
			dev_dbg(dev, "%s GC5025_OTP_INFO group %d is Valid!\n",
				__func__, index + 1);
			checksum = 0;
			info_start_add =
				INFO_ROM_START_5025 + 8 * index * INFO_WIDTH_5025;
			otp_p->module_id = gc5025_otp_read_reg(client,
				1, info_start_add);
			checksum += otp_p->module_id;
			otp_p->lens_id = gc5025_otp_read_reg(client,
				1, info_start_add + 8 * 1);
			checksum += otp_p->lens_id;
			otp_p->vcm_driver_id = gc5025_otp_read_reg(client,
				1, info_start_add + 8 * 2);
			checksum += otp_p->vcm_driver_id;
			otp_p->vcm_id = gc5025_otp_read_reg(client, 1,
				info_start_add + 8 * 3);
			checksum += otp_p->vcm_id;
			otp_p->year = gc5025_otp_read_reg(client, 1,
				info_start_add + 8 * 4);
			checksum += otp_p->year;
			otp_p->month = gc5025_otp_read_reg(client,
				1, info_start_add + 8 * 5);
			checksum += otp_p->month;
			otp_p->day = gc5025_otp_read_reg(client,
				1, info_start_add + 8 * 6);
			checksum += otp_p->day;
			checksum = checksum % 255 + 1;
			temp = gc5025_otp_read_reg(client,
				1, 0x40 + 8 * index * INFO_WIDTH_5025);
			if (checksum == temp) {
				otp_p->flag = 0x80;
				dev_dbg(dev, "fac info: module(0x%x) lens(0x%x) time(%d_%d_%d)\n",
					otp_p->module_id,
					otp_p->lens_id,
					otp_p->year,
					otp_p->month,
					otp_p->day);
			} else {
				dev_err(dev, "otp module info check sum error\n");
			}
		break;
		case 0x02:
		case 0x03:
			dev_err(dev, "%s GC5025_OTP_INFO group %d is Invalid !!\n",
				__func__, index + 1);
			break;
		default:
			break;
		}
		switch ((otp_flag >> (2 * index)) & 0x03) {
		case 0x00:
			dev_err(dev, "%s GC5025_OTP_WB group %d is Empty !\n",
				__func__, index + 1);
			break;
		case 0x01:
			dev_dbg(dev, "%s GC5025_OTP_WB group %d is Valid !!\n",
				__func__, index + 1);
			checksum = 0;
			wb_start_add =
				WB_ROM_START_5025 + 8 * index * WB_WIDTH_5025;
			tmpH = gc5025_otp_read_reg(client,
				1, wb_start_add);
			checksum += tmpH;
			tmpL = gc5025_otp_read_reg(client,
				1, wb_start_add + 8 * 1);
			checksum += tmpL;
			otp_p->rg_ratio = (tmpH << 8) | tmpL;
			tmpH = gc5025_otp_read_reg(client,
				1, wb_start_add + 8 * 2);
			checksum += tmpH;
			tmpL = gc5025_otp_read_reg(client,
				1, wb_start_add + 8 * 3);
			checksum += tmpL;
			otp_p->bg_ratio = (tmpH << 8) | tmpL;
			checksum = checksum % 255 + 1;
			temp = gc5025_otp_read_reg(client,
				1, 0xa8 + 8 * index * WB_WIDTH_5025);
			if (checksum == temp) {
				otp_p->flag = 0x40;
				dev_dbg(dev, "otp:(rg_ratio 0x%x, bg_ratio 0x%x)\n",
					otp_p->rg_ratio, otp_p->bg_ratio);
			}
			break;
		case 0x02:
		case 0x03:
			dev_err(dev, "%s GC5025_OTP_WB group %d is Invalid !!\n",
				__func__, index + 1);
			break;
		default:
			break;
		}
	}
	/* OTP awb golden*/
	otp_flag = gc5025_otp_read_reg(client, 1, 0xd8);
	for (index = 0; index < 2; index++) {
		switch ((otp_flag >> (2 * index)) & 0x03) {
		case 0x00:
			dev_err(dev, "%s GC5025_OTP_GOLDEN group %d is Empty !\n",
				__func__, index + 1);
			break;
		case 0x01:
			dev_dbg(dev, "%s GC5025_OTP_GOLDEN group %d is Valid !!\n",
				__func__, index + 1);
			checksum = 0;
			golden_start_add =
				GOLDEN_ROM_START_5025 + 8 * index * GOLDEN_WIDTH_5025;
			tmpH = gc5025_otp_read_reg(client,
				1, golden_start_add);
			checksum += tmpH;
			tmpL = gc5025_otp_read_reg(client,
				1, golden_start_add + 8 * 1);
			checksum += tmpL;
			otp_p->golden_rg = (tmpH << 8) | tmpL;
			tmpH = gc5025_otp_read_reg(client,
				1, golden_start_add + 8 * 2);
			checksum += tmpH;
			tmpL = gc5025_otp_read_reg(client,
				1, golden_start_add + 8 * 3);
			checksum += tmpL;
			otp_p->golden_bg = (tmpH << 8) | tmpL;
			checksum = checksum % 255 + 1;
			temp = gc5025_otp_read_reg(client,
				1,
				0x100 + 8 * index * GOLDEN_WIDTH_5025);
			if (checksum == temp) {
				dev_dbg(dev, "otp:(golden_rg 0x%x, golden_bg 0x%x)\n",
					otp_p->golden_rg, otp_p->golden_bg);
			}
			break;
		case 0x02:
		case 0x03:
			dev_err(dev, "%s GC5025_OTP_GOLDEN group %d is Invalid !!\n",
				__func__, index + 1);
			break;
		default:
			break;
		}
	}

	/* OTP DD calibration data */
	otp_flag = gc5025_otp_read_reg(client, 0, 0);
	switch (otp_flag & 0x03) {
	case 0x00:
		dev_err(dev, "%s GC5025 OTP:flag_dd is EMPTY!\n",
			__func__);
		break;
	case 0x01:
		dev_dbg(dev, "%s GC5025 OTP:flag_dd is Valid!\n",
			__func__);
		checksum = 0;
		total_number = gc5025_otp_read_reg(client, 0, 0x08) +
			gc5025_otp_read_reg(client, 0, 0x10);
		for (i = 0; i < 126; i++) {
			m_DD_Otp_Value[i] =
				gc5025_otp_read_reg(client,
					0, 0x08 + 8 * i);
			checksum += m_DD_Otp_Value[i];
		}
		for (i = 0; i < 56; i++) {
			m_DD_Otp_Value[126 + i] =
				gc5025_otp_read_reg(client,
					1, 0x148 + 8 * i);
			checksum += m_DD_Otp_Value[126 + i];
		}
		checksum = checksum % 255 + 1;
		temp = gc5025_otp_read_reg(client, 1, 0x308);
		if (checksum == temp) {
			for (i = 0; i < total_number; i++) {
				if (i < 31) {
					page = 0;
					dd_rom_start = 0x18;
					offset = 0;
				} else {
					page = 1;
					dd_rom_start = 0x148;
					offset = 124;//31*4
				}
				check_dd_flag = gc5025_otp_read_reg(client,
					page,
					dd_rom_start + 8 * (4 * i - offset + 3));
				if (check_dd_flag & 0x10) {
					//Read OTP
					type = check_dd_flag & 0x0f;
					dd0 = gc5025_otp_read_reg(client, page,
						dd_rom_start + 8 * (4 * i - offset));
					dd1 = gc5025_otp_read_reg(client,
						page,
						dd_rom_start + 8 * (4 * i - offset + 1));
					dd2 = gc5025_otp_read_reg(client,
						page,
						dd_rom_start + 8 * (4 * i - offset + 2));
					x = ((dd1 & 0x0f) << 8) + dd0;
					y = (dd2 << 4) + ((dd1 & 0xf0) >> 4);

					if (type == 3) {
						for (j = 0; j < 4; j++) {
							otp_p->dd_param_x[cnt] = x;
							otp_p->dd_param_y[cnt] = y + j;
							otp_p->dd_param_type[cnt++] = 2;
						}
					} else if (type == 4) {
						for (j = 0; j < 2; j++) {
							otp_p->dd_param_x[cnt] = x;
							otp_p->dd_param_y[cnt] = y + j;
							otp_p->dd_param_type[cnt++] = 2;
						}
					} else {
						otp_p->dd_param_x[cnt] = x;
						otp_p->dd_param_y[cnt] = y;
						otp_p->dd_param_type[cnt++] = type;
					}
				} else {
					dev_err(dev, "%s GC5025_OTP_DD:check_id[%d] = %x,checkid error!!\n",
						__func__, i, check_dd_flag);
				}
			}
			otp_p->dd_cnt = cnt;
			otp_p->flag |= 0x08;
		}
		break;
	case 0x02:
	case 0x03:
		dev_err(dev, "%s GC5025 OTP:flag_dd is Invalid!\n",
			__func__);
		break;
	default:
		break;
	}

	if (otp_p->flag) {
		gc5025->otp = otp_p;
	} else {
		gc5025->otp = NULL;
		devm_kfree(dev, otp_p);
	}

	return 0;
}

static void gc5025_get_otp(struct gc5025_otp_info *otp,
			       struct rkmodule_inf *inf)
{
	u32 i;

	/* fac */
	if (otp->flag & 0x80) {
		inf->fac.flag = 1;
		inf->fac.year = otp->year;
		inf->fac.month = otp->month;
		inf->fac.day = otp->day;
		for (i = 0; i < ARRAY_SIZE(gc5025_module_info) - 1; i++) {
			if (gc5025_module_info[i].id == otp->module_id)
				break;
		}
		strlcpy(inf->fac.module, gc5025_module_info[i].name,
			sizeof(inf->fac.module));

		for (i = 0; i < ARRAY_SIZE(gc5025_lens_info) - 1; i++) {
			if (gc5025_lens_info[i].id == otp->lens_id)
				break;
		}
		strlcpy(inf->fac.lens, gc5025_lens_info[i].name,
			sizeof(inf->fac.lens));
	}
	/* awb */
	if (otp->flag & 0x40) {
		inf->awb.flag = 1;
		inf->awb.r_value = otp->rg_ratio;
		inf->awb.b_value = otp->bg_ratio;
		inf->awb.gr_value = 0;
		inf->awb.gb_value = 0;

		inf->awb.golden_r_value = 0;
		inf->awb.golden_b_value = 0;
		inf->awb.golden_gr_value = 0;
		inf->awb.golden_gb_value = 0;
	}
}

static void gc5025_get_module_inf(struct gc5025 *gc5025,
				  struct rkmodule_inf *inf)
{
	struct gc5025_otp_info *otp = gc5025->otp;

	strlcpy(inf->base.sensor,
		GC5025_NAME,
		sizeof(inf->base.sensor));
	strlcpy(inf->base.module,
		gc5025->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens,
		gc5025->len_name,
		sizeof(inf->base.lens));
	if (otp)
		gc5025_get_otp(otp, inf);
}

static void gc5025_set_module_inf(struct gc5025 *gc5025,
				  struct rkmodule_awb_cfg *cfg)
{
	mutex_lock(&gc5025->mutex);
	memcpy(&gc5025->awb_cfg, cfg, sizeof(*cfg));
	mutex_unlock(&gc5025->mutex);
}

static long gc5025_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc5025 *gc5025 = to_gc5025(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc5025_get_module_inf(gc5025, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_AWB_CFG:
		gc5025_set_module_inf(gc5025, (struct rkmodule_awb_cfg *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream) {
			ret = gc5025_write_reg(gc5025->client,
					       GC5025_REG_SET_PAGE,
					       GC5025_SET_PAGE_ONE);
			ret |= gc5025_write_reg(gc5025->client,
						GC5025_REG_CTRL_MODE,
						GC5025_MODE_STREAMING);
		} else {
			ret = gc5025_write_reg(gc5025->client,
					       GC5025_REG_SET_PAGE,
					       GC5025_SET_PAGE_ONE);
			ret |= gc5025_write_reg(gc5025->client,
						GC5025_REG_CTRL_MODE,
						GC5025_MODE_SW_STANDBY);
		}
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc5025_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = gc5025_ioctl(sd, cmd, inf);
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
			ret = gc5025_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc5025_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}
#endif

/*--------------------------------------------------------------------------*/
static int gc5025_apply_otp(struct gc5025 *gc5025)
{
	int R_gain, G_gain, B_gain, base_gain;
	struct i2c_client *client = gc5025->client;
	struct gc5025_otp_info *otp_p = gc5025->otp;
	struct rkmodule_awb_cfg *awb_cfg = &gc5025->awb_cfg;
	u32 golden_bg_ratio;
	u32 golden_rg_ratio;
	u32 golden_g_value;
	u16 i, j;
	u16 temp_x = 0, temp_y = 0;
	u8 temp_type = 0;
	u8 temp_val0, temp_val1, temp_val2;
	u16 column, ii, iii, jj;

	if (!gc5025->awb_cfg.enable)
		return 0;

	golden_g_value = (awb_cfg->golden_gb_value +
		awb_cfg->golden_gr_value) / 2;
	golden_bg_ratio =
		awb_cfg->golden_b_value * 0x400 / golden_g_value;
	golden_rg_ratio =
		awb_cfg->golden_r_value * 0x400 / golden_g_value;
	/* apply OTP WB Calibration */
	if ((otp_p->flag & 0x40) && golden_bg_ratio && golden_rg_ratio) {
		/* calculate G gain */
		R_gain = golden_rg_ratio * 1000 / otp_p->rg_ratio;
		B_gain = golden_bg_ratio * 1000 / otp_p->bg_ratio;
		G_gain = 1000;
		base_gain = (R_gain < B_gain) ? R_gain : B_gain;
		base_gain = (base_gain < G_gain) ? base_gain : G_gain;

		R_gain = 0x400 * R_gain / (base_gain);
		B_gain = 0x400 * B_gain / (base_gain);
		G_gain = 0x400 * G_gain / (base_gain);

		/* update sensor WB gain */
		gc5025_write_reg(client, 0xfe, 0x00);
		gc5025_write_reg(client, 0xc6,
			(G_gain & 0x7f8) >> 3);
		gc5025_write_reg(client, 0xc7,
			(R_gain & 0x7f8) >> 3);
		gc5025_write_reg(client, 0xc8,
			(B_gain & 0x7f8) >> 3);
		gc5025_write_reg(client, 0xc9,
			(G_gain & 0x7f8) >> 3);
		gc5025_write_reg(client, 0xc4,
			((G_gain & 0X07) << 4) | (R_gain & 0x07));
		gc5025_write_reg(client, 0xc5,
			((B_gain & 0X07) << 4) | (G_gain & 0x07));
		dev_dbg(&client->dev, "apply awb gain: 0x%x, 0x%x, 0x%x\n",
			R_gain, G_gain, B_gain);
	}

	/* apply OTP DD Calibration */
	if (otp_p->flag & 0x08) {
#if defined IMAGE_NORMAL_MIRROR
#elif defined IMAGE_H_MIRROR
		for (i = 0; i < otp_p->dd_cnt; i++) {
			if (otp_p->dd_param_type[i] == 0) {
				otp_p->dd_param_x[i] =
					WINDOW_WIDTH - otp_p->dd_param_x[i] + 1;
			} else if (otp_p->dd_param_type[i] == 1) {
				otp_p->dd_param_x[i] =
					WINDOW_WIDTH - otp_p->dd_param_x[i] - 1;
			} else {
				otp_p->dd_param_x[i] =
					WINDOW_WIDTH - otp_p->dd_param_x[i];
			}
		}
#elif defined IMAGE_V_MIRROR
		for (i = 0; i < otp_p->dd_cnt; i++) {
			otp_p->dd_param_y[i] =
				WINDOW_HEIGHT - otp_p->dd_param_y[i] + 1;
		}
#elif defined IMAGE_HV_MIRROR
		for (i = 0; i < otp_p->dd_cnt; i++) {
			if (otp_p->dd_param_type[i] == 0) {
				otp_p->dd_param_x[i] =
					WINDOW_WIDTH - otp_p->dd_param_x[i] + 1;
				otp_p->dd_param_y[i] =
					WINDOW_HEIGHT - otp_p->dd_param_y[i] + 1;
			} else if (otp_p->dd_param_type[i] == 1) {
				otp_p->dd_param_x[i] =
					WINDOW_WIDTH - otp_p->dd_param_x[i] - 1;
				otp_p->dd_param_y[i] =
					WINDOW_HEIGHT - otp_p->dd_param_y[i] + 1;
			} else {
				otp_p->dd_param_x[i] =
					WINDOW_WIDTH - otp_p->dd_param_x[i];
				otp_p->dd_param_y[i] =
					WINDOW_HEIGHT - otp_p->dd_param_y[i] + 1;
			}
		}
#endif
		//y
		for (i = 0; i < otp_p->dd_cnt - 1; i++) {
			for (j = 0; j < otp_p->dd_cnt - 1 - i; j++) {
				if (otp_p->dd_param_y[j] >
					otp_p->dd_param_y[j + 1]) {
					temp_x = otp_p->dd_param_x[j];
					otp_p->dd_param_x[j] =
						otp_p->dd_param_x[j + 1];
					otp_p->dd_param_x[j + 1] =
						temp_x;
					temp_y =
						otp_p->dd_param_y[j];
					otp_p->dd_param_y[j] =
						otp_p->dd_param_y[j + 1];
					otp_p->dd_param_y[j + 1] =
						temp_y;
					temp_type =
						otp_p->dd_param_type[j];
					otp_p->dd_param_type[j] =
						otp_p->dd_param_type[j + 1];
					otp_p->dd_param_type[j + 1] =
						temp_type;
				}
			}
		}
		//x
		column = 0;
		for (i = 0 ; i < otp_p->dd_cnt - 1; ++i) {
			if (otp_p->dd_param_y[i] == otp_p->dd_param_y[i + 1]) {
				column++;
				if (otp_p->dd_cnt - 2 != i)
					continue;
			}
			if (otp_p->dd_cnt - 2 == i &&
				otp_p->dd_param_y[i] == otp_p->dd_param_y[i + 1]) {
				i = otp_p->dd_cnt - 1;
			}
			iii = i - column;
			for (ii = i - column; ii < i ; ++ii) {
				for (jj = i - column; jj <
					i - (ii - iii); ++jj) {
					if (otp_p->dd_param_x[jj] >
						otp_p->dd_param_x[jj + 1]) {
						temp_x = otp_p->dd_param_x[jj];
						otp_p->dd_param_x[jj] =
							otp_p->dd_param_x[jj + 1];
						otp_p->dd_param_x[jj + 1] =
							temp_x;
						temp_y =
							otp_p->dd_param_y[jj];
						otp_p->dd_param_y[jj] =
							otp_p->dd_param_y[jj + 1];
						otp_p->dd_param_y[jj + 1] =
							temp_y;
						temp_type =
							otp_p->dd_param_type[jj];
						otp_p->dd_param_type[jj] =
							otp_p->dd_param_type[jj + 1];
						otp_p->dd_param_type[jj + 1] =
							temp_type;
					}
				}
			}
			column = 0;
		}

		//write SRAM
		gc5025_write_reg(client, 0xfe, 0x00);
		gc5025_write_reg(client, 0x80, 0x50);
		gc5025_write_reg(client, 0xfe, 0x01);
		gc5025_write_reg(client, 0xa8, 0x00);
		gc5025_write_reg(client, 0x9d, 0x04);
		gc5025_write_reg(client, 0xbe, 0x00);
		gc5025_write_reg(client, 0xa9, 0x01);

		for (i = 0; i < otp_p->dd_cnt; i++) {
			temp_val0 = otp_p->dd_param_x[i] & 0x00ff;
			temp_val1 = ((otp_p->dd_param_y[i] << 4) & 0x00f0) +
				((otp_p->dd_param_x[i] >> 8) & 0x000f);
			temp_val2 = (otp_p->dd_param_y[i] >> 4) & 0xff;
			gc5025_write_reg(client, 0xaa, i);
			gc5025_write_reg(client, 0xac, temp_val0);
			gc5025_write_reg(client, 0xac, temp_val1);
			gc5025_write_reg(client, 0xac, temp_val2);
			gc5025_write_reg(client, 0xac,
				otp_p->dd_param_type[i]);
		}
		gc5025_write_reg(client, 0xbe, 0x01);
		gc5025_write_reg(client, 0xfe, 0x00);
	}
	return 0;
}

static int __gc5025_start_stream(struct gc5025 *gc5025)
{
	int ret;

	ret = gc5025_write_array(gc5025->client, gc5025->cur_mode->reg_list);
	if (ret)
		return ret;

	if (gc5025->DR_State) {
		ret = gc5025_write_array(gc5025->client,
			gc5025_doublereset_reg);
	} else {
		ret = gc5025_write_array(gc5025->client,
			gc5025_disable_doublereset_reg);
	}
	if (ret)
		return ret;
	/* In case these controls are set before streaming */
	mutex_unlock(&gc5025->mutex);
	ret = v4l2_ctrl_handler_setup(&gc5025->ctrl_handler);
	mutex_lock(&gc5025->mutex);
	if (ret)
		return ret;
	if (gc5025->otp) {
		ret = gc5025_otp_enable(gc5025);
		ret |= gc5025_apply_otp(gc5025);
		ret |= gc5025_otp_disable(gc5025);
		if (ret)
			return ret;
	}
	ret = gc5025_write_reg(gc5025->client,
		GC5025_REG_SET_PAGE,
		GC5025_SET_PAGE_ONE);
	ret |= gc5025_write_reg(gc5025->client,
		GC5025_REG_CTRL_MODE,
		GC5025_MODE_STREAMING);
	return ret;
}

static int __gc5025_stop_stream(struct gc5025 *gc5025)
{
	int ret;

	ret = gc5025_write_reg(gc5025->client,
		GC5025_REG_SET_PAGE,
		GC5025_SET_PAGE_ONE);
	ret |= gc5025_write_reg(gc5025->client,
		GC5025_REG_CTRL_MODE,
		GC5025_MODE_SW_STANDBY);
	return ret;
}

static int gc5025_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc5025 *gc5025 = to_gc5025(sd);
	struct i2c_client *client = gc5025->client;
	int ret = 0;

	mutex_lock(&gc5025->mutex);
	on = !!on;
	if (on == gc5025->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc5025_start_stream(gc5025);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc5025_stop_stream(gc5025);
		pm_runtime_put(&client->dev);
	}

	gc5025->streaming = on;

unlock_and_return:
	mutex_unlock(&gc5025->mutex);

	return ret;
}

static int gc5025_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc5025 *gc5025 = to_gc5025(sd);
	struct i2c_client *client = gc5025->client;
	int ret = 0;

	mutex_lock(&gc5025->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc5025->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc5025_write_array(gc5025->client, gc5025_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc5025->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc5025->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc5025->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc5025_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC5025_XVCLK_FREQ / 1000 / 1000);
}

static int __gc5025_power_on(struct gc5025 *gc5025)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc5025->client->dev;

	if (!IS_ERR(gc5025->power_gpio)) {
		gpiod_set_value_cansleep(gc5025->power_gpio, 1);
		usleep_range(5000, 5100);
	}

	if (!IS_ERR_OR_NULL(gc5025->pins_default)) {
		ret = pinctrl_select_state(gc5025->pinctrl,
					   gc5025->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc5025->xvclk, GC5025_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc5025->xvclk) != GC5025_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc5025->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(gc5025->reset_gpio))
		gpiod_set_value_cansleep(gc5025->reset_gpio, 1);

	ret = regulator_bulk_enable(GC5025_NUM_SUPPLIES, gc5025->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	usleep_range(1000, 1100);
	if (!IS_ERR(gc5025->reset_gpio))
		gpiod_set_value_cansleep(gc5025->reset_gpio, 0);

	usleep_range(500, 1000);
	if (!IS_ERR(gc5025->pwdn_gpio))
		gpiod_set_value_cansleep(gc5025->pwdn_gpio, 0);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc5025_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc5025->xvclk);

	return ret;
}

static void __gc5025_power_off(struct gc5025 *gc5025)
{
	int ret;

	if (!IS_ERR(gc5025->pwdn_gpio))
		gpiod_set_value_cansleep(gc5025->pwdn_gpio, 1);
	clk_disable_unprepare(gc5025->xvclk);
	if (!IS_ERR(gc5025->reset_gpio))
		gpiod_set_value_cansleep(gc5025->reset_gpio, 1);
	if (!IS_ERR_OR_NULL(gc5025->pins_sleep)) {
		ret = pinctrl_select_state(gc5025->pinctrl,
			gc5025->pins_sleep);
		if (ret < 0)
			dev_dbg(&gc5025->client->dev, "could not set pins\n");
	}
	regulator_bulk_disable(GC5025_NUM_SUPPLIES, gc5025->supplies);
}

static int gc5025_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5025 *gc5025 = to_gc5025(sd);

	return __gc5025_power_on(gc5025);
}

static int gc5025_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5025 *gc5025 = to_gc5025(sd);

	__gc5025_power_off(gc5025);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc5025_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc5025 *gc5025 = to_gc5025(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc5025_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc5025->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc5025->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int gc5025_enum_frame_interval(struct v4l2_subdev *sd,
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

static int gc5025_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (GC5025_LANES - 1) |
	V4L2_MBUS_CSI2_CHANNEL_0 |
	V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static int gc5025_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = 2592;
		sel->r.top = 0;
		sel->r.height = 1944;
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops gc5025_pm_ops = {
	SET_RUNTIME_PM_OPS(gc5025_runtime_suspend,
			   gc5025_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc5025_internal_ops = {
	.open = gc5025_open,
};
#endif

static const struct v4l2_subdev_core_ops gc5025_core_ops = {
	.s_power = gc5025_s_power,
	.ioctl = gc5025_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc5025_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc5025_video_ops = {
	.s_stream = gc5025_s_stream,
	.g_frame_interval = gc5025_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc5025_pad_ops = {
	.enum_mbus_code = gc5025_enum_mbus_code,
	.enum_frame_size = gc5025_enum_frame_sizes,
	.enum_frame_interval = gc5025_enum_frame_interval,
	.get_fmt = gc5025_get_fmt,
	.set_fmt = gc5025_set_fmt,
	.get_selection = gc5025_get_selection,
	.get_mbus_config = gc5025_g_mbus_config,
};

static const struct v4l2_subdev_ops gc5025_subdev_ops = {
	.core	= &gc5025_core_ops,
	.video	= &gc5025_video_ops,
	.pad	= &gc5025_pad_ops,
};

static int gc5025_set_exposure_reg(struct gc5025 *gc5025, u32 exposure)
{
	u32 caltime = 0;
	int ret = 0;

	caltime = exposure / 2;
	caltime = caltime * 2;
	gc5025->Dgain_ratio = 256 * exposure / caltime;
	ret = gc5025_write_reg(gc5025->client,
		GC5025_REG_SET_PAGE,
		GC5025_SET_PAGE_ONE);
	if (!gc5025->DR_State) {
		if (caltime <= 10)
			ret |= gc5025_write_reg(gc5025->client, 0xd9, 0xdd);
		else
			ret |= gc5025_write_reg(gc5025->client, 0xd9, 0xaa);
	}
	ret |= gc5025_write_reg(gc5025->client,
		GC5025_REG_EXPOSURE_H,
		(caltime >> 8) & 0x3F);
	ret |= gc5025_write_reg(gc5025->client,
		GC5025_REG_EXPOSURE_L,
		caltime & 0xFF);
	return ret;
}

#define GC5025_ANALOG_GAIN_1 64    /*1.00x*/
#define GC5025_ANALOG_GAIN_2 92   // 1.445x

static int gc5025_set_gain_reg(struct gc5025 *gc5025, u32 a_gain)
{
	int ret = 0;
	u32 temp = 0;

	if (a_gain < 0x40)
		a_gain = 0x40;
	ret = gc5025_write_reg(gc5025->client,
		GC5025_REG_SET_PAGE,
		GC5025_SET_PAGE_ONE);
	if (a_gain >= GC5025_ANALOG_GAIN_1 &&
		a_gain < GC5025_ANALOG_GAIN_2) {
		ret |= gc5025_write_reg(gc5025->client,
			GC5025_REG_AGAIN, 0x0);
		temp = a_gain;
	} else {
		ret |= gc5025_write_reg(gc5025->client,
			GC5025_REG_AGAIN, 0x1);
		temp = 64 * a_gain / GC5025_ANALOG_GAIN_2;
	}
	temp = temp * gc5025->Dgain_ratio / 256;
	ret |= gc5025_write_reg(gc5025->client,
		GC5025_REG_DGAIN_INT,
		temp >> 6);
	ret |= gc5025_write_reg(gc5025->client,
		GC5025_REG_DGAIN_FRAC,
		(temp << 2) & 0xfc);
	return ret;
}

static int gc5025_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc5025 *gc5025 = container_of(ctrl->handler,
					     struct gc5025, ctrl_handler);
	struct i2c_client *client = gc5025->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = gc5025->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc5025->exposure,
			gc5025->exposure->minimum, max,
			gc5025->exposure->step,
			gc5025->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = gc5025_set_exposure_reg(gc5025, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = gc5025_set_gain_reg(gc5025, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = gc5025_write_reg(gc5025->client,
			GC5025_REG_SET_PAGE,
			GC5025_SET_PAGE_ONE);
		ret |= gc5025_write_reg(gc5025->client,
			GC5025_REG_VTS_H,
			((ctrl->val - 24) >> 8) & 0xff);
		ret |= gc5025_write_reg(gc5025->client,
			GC5025_REG_VTS_L,
			(ctrl->val - 24) & 0xff);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			__func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc5025_ctrl_ops = {
	.s_ctrl = gc5025_set_ctrl,
};

static int gc5025_initialize_controls(struct gc5025 *gc5025)
{
	const struct gc5025_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc5025->ctrl_handler;
	mode = gc5025->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &gc5025->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
		0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
		0, GC5025_PIXEL_RATE, 1, GC5025_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	gc5025->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
		h_blank, h_blank, 1, h_blank);
	if (gc5025->hblank)
		gc5025->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc5025->vblank = v4l2_ctrl_new_std(handler, &gc5025_ctrl_ops,
		V4L2_CID_VBLANK, vblank_def,
		GC5025_VTS_MAX - mode->height,
		1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc5025->exposure = v4l2_ctrl_new_std(handler, &gc5025_ctrl_ops,
		V4L2_CID_EXPOSURE, GC5025_EXPOSURE_MIN,
		exposure_max, GC5025_EXPOSURE_STEP,
		mode->exp_def);

	gc5025->anal_gain = v4l2_ctrl_new_std(handler, &gc5025_ctrl_ops,
		V4L2_CID_ANALOGUE_GAIN, GC5025_GAIN_MIN,
		GC5025_GAIN_MAX, GC5025_GAIN_STEP,
		GC5025_GAIN_DEFAULT);

	if (handler->error) {
		ret = handler->error;
		dev_err(&gc5025->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc5025->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc5025_check_sensor_id(struct gc5025 *gc5025,
	struct i2c_client *client)
{
	struct device *dev = &gc5025->client->dev;
	u16 id = 0;
	u8 reg_H = 0;
	u8 reg_L = 0;
	u8 flag_doublereset = 0;
	u8 flag_GC5025A = 0;
	int ret;

	ret = gc5025_read_reg(client, GC5025_REG_CHIP_ID_H, &reg_H);
	ret |= gc5025_read_reg(client, GC5025_REG_CHIP_ID_L, &reg_L);
	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	ret |= gc5025_read_reg(client, 0x26, &flag_doublereset);
	ret |= gc5025_read_reg(client, 0x27, &flag_GC5025A);
	if ((flag_GC5025A & 0x01) == 0x01) {
		dev_warn(dev, "GC5025A sensor!\n");
		gc5025->DR_State = false;
	} else {
		if ((flag_doublereset & 0x03) == 0x01) {
			gc5025->DR_State = false;
			dev_warn(dev, "GC5025 double reset off\n");
		} else {
			gc5025->DR_State = true;
			dev_warn(dev, "GC5025 double reset on\n");
		}
	}
	return ret;
}

static int gc5025_configure_regulators(struct gc5025 *gc5025)
{
	unsigned int i;

	for (i = 0; i < GC5025_NUM_SUPPLIES; i++)
		gc5025->supplies[i].supply = gc5025_supply_names[i];

	return devm_regulator_bulk_get(&gc5025->client->dev,
		GC5025_NUM_SUPPLIES,
		gc5025->supplies);
}

static int gc5025_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc5025 *gc5025;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc5025 = devm_kzalloc(dev, sizeof(*gc5025), GFP_KERNEL);
	if (!gc5025)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
		&gc5025->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
		&gc5025->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
		&gc5025->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
		&gc5025->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	gc5025->client = client;
	gc5025->cur_mode = &supported_modes[0];

	gc5025->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc5025->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc5025->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(gc5025->power_gpio))
		dev_warn(dev, "Failed to get power-gpios\n");

	gc5025->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc5025->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc5025->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc5025->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = gc5025_configure_regulators(gc5025);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	gc5025->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc5025->pinctrl)) {
		gc5025->pins_default =
			pinctrl_lookup_state(gc5025->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc5025->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc5025->pins_sleep =
			pinctrl_lookup_state(gc5025->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc5025->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&gc5025->mutex);

	sd = &gc5025->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc5025_subdev_ops);
	ret = gc5025_initialize_controls(gc5025);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc5025_power_on(gc5025);
	if (ret)
		goto err_free_handler;

	ret = gc5025_check_sensor_id(gc5025, client);
	if (ret)
		goto err_power_off;

	gc5025_otp_enable(gc5025);
	gc5025_otp_read(gc5025);
	gc5025_otp_disable(gc5025);

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc5025_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc5025->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc5025->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc5025->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc5025->module_index, facing,
		 GC5025_NAME, dev_name(sd->dev));
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
	__gc5025_power_off(gc5025);
err_free_handler:
	v4l2_ctrl_handler_free(&gc5025->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc5025->mutex);

	return ret;
}

static int gc5025_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5025 *gc5025 = to_gc5025(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc5025->ctrl_handler);
	mutex_destroy(&gc5025->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc5025_power_off(gc5025);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc5025_of_match[] = {
	{ .compatible = "galaxycore,gc5025" },
	{},
};
MODULE_DEVICE_TABLE(of, gc5025_of_match);
#endif

static const struct i2c_device_id gc5025_match_id[] = {
	{ "galaxycore,gc5025", 0 },
	{ },
};

static struct i2c_driver gc5025_i2c_driver = {
	.driver = {
		.name = GC5025_NAME,
		.pm = &gc5025_pm_ops,
		.of_match_table = of_match_ptr(gc5025_of_match),
	},
	.probe		= &gc5025_probe,
	.remove		= &gc5025_remove,
	.id_table	= gc5025_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc5025_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc5025_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GalaxyCore gc5025 sensor driver");
MODULE_LICENSE("GPL v2");
