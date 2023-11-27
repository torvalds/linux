// SPDX-License-Identifier: GPL-2.0
/*
 * os04d10 driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 first version
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
#include <linux/rk-camera-module.h>
#include <linux/rk-preisp.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include "../platform/rockchip/isp/rkisp_tb_helper.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define OS04D10_LANES			2
#define OS04D10_BITS_PER_SAMPLE		10
#define OS04D10_LINK_FREQ_360		360000000

#define PIXEL_RATE_WITH_360M_10BIT	(OS04D10_LINK_FREQ_360 * 2 * \
					OS04D10_LANES / OS04D10_BITS_PER_SAMPLE)
#define OS04D10_XVCLK_FREQ		24000000

#define OS04D10_REG_PAGE_SEL		0xfd
#define OS04D10_REG_EXP_UPDATE		0x01

#define OS04D10_REG_PAGE_SYS_CTRL	0
#define OS04D10_REG_PAGE_CIS_TIMING	1
#define OS04D10_REG_PAGE_ISP_MIPI	2
#define OS04D10_REG_PAGE_DAC_CODE	3
#define OS04D10_REG_PAGE_DPC_SRAM	4
#define OS04D10_REG_PAGE_CIS_CTRL	5
#define OS04D10_REG_PAGE_OTP_CTRL	6
#define OS04D10_REG_PAGE_CIS_SRAM	7

#define CHIP_ID				0x53044410
#define OS04D10_REG_CHIP_ID		0x02

#define OS04D10_REG_CTRL_MODE		0x20
#define OS04D10_MODE_SW_STANDBY		0x01
#define OS04D10_MODE_STREAMING		0x03

#define OS04D10_REG_EXPOSURE_H		0x03
#define OS04D10_REG_EXPOSURE_L		0x04
#define	OS04D10_EXPOSURE_MIN		1
#define	OS04D10_EXPOSURE_STEP		1
#define OS04D10_VTS_MAX			0x7fff

#define OS04D10_REG_DIG_GAIN_H		0x37
#define OS04D10_REG_DIG_GAIN_L		0x39
#define OS04D10_REG_ANA_GAIN		0x24
#define OS04D10_GAIN_MIN		0x40
#define OS04D10_GAIN_MAX		(31744)	//15.5 * 32 * 64
#define OS04D10_GAIN_STEP		1
#define OS04D10_GAIN_DEFAULT		0x40

#define OS04D10_REG_TEST_PATTERN	0x0c
#define OS04D10_TEST_PATTERN_BIT_MASK	0x01

#define OS04D10_REG_VTS_H		0x05
#define OS04D10_REG_VTS_L		0x06

#define OS04D10_FLIP_MIRROR_REG		0x32

#define OS04D10_FETCH_EXP_H(VAL)		(((VAL) >> 8) & 0xFF)
#define OS04D10_FETCH_EXP_L(VAL)		((VAL) & 0xFF)

#define OS04D10_FETCH_MIRROR(VAL, ENABLE)	(ENABLE ? VAL | 0x01 : VAL & 0xfe)
#define OS04D10_FETCH_FLIP(VAL, ENABLE)		(ENABLE ? VAL | 0x02 : VAL & 0xfd)

#define REG_DELAY			0xFE
#define REG_NULL			0xFF

#define OS04D10_REG_VALUE_08BIT		1
#define OS04D10_REG_VALUE_16BIT		2
#define OS04D10_REG_VALUE_24BIT		3
#define OS04D10_REG_VALUE_32BIT		4

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OS04D10_NAME			"os04d10"

static const char * const os04d10_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OS04D10_NUM_SUPPLIES ARRAY_SIZE(os04d10_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct os04d10_mode {
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

struct os04d10 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct regulator_bulk_data supplies[OS04D10_NUM_SUPPLIES];

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
	struct v4l2_fract	cur_fps;
	bool			streaming;
	bool			power_on;
	const struct os04d10_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	bool			has_init_exp;
	bool			is_thunderboot;
	bool			is_first_streamoff;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_os04d10(sd) container_of(sd, struct os04d10, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval os04d10_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 15fps
 * mipi_datarate per lane 720Mbps, 2lane
 */
static const struct regval os04d10_linear_10_2568x1448_regs[] = {
	{0xfd, 0x00},
	{0x20, 0x00},
	{0x20, 0x01},
	{0x20, 0x01},
	{0x20, 0x01},
	{0x20, 0x01},
	{0x41, 0xa8},
	{0x45, 0x24},
	{0x31, 0x20},
	{0x38, 0x15},
	{0xfd, 0x01},
	{0x03, 0x00},
	{0x04, 0x04},
	{0x05, 0x05},
	{0x06, 0xc3},
	{0x24, 0xff},
	{0x02, 0x01},
	{0x42, 0x5a},
	{0x47, 0x0c},
	{0x45, 0x02},
	{0x48, 0x0c},
	{0x4b, 0x88},
	{0xd4, 0x05},
	{0xd5, 0xd2},
	{0xd7, 0x05},
	{0xd8, 0xd2},
	{0x50, 0x01},
	{0x51, 0x11},
	{0x52, 0x18},
	{0x53, 0x01},
	{0x54, 0x01},
	{0x55, 0x01},
	{0x57, 0x08},
	{0x5c, 0x40},
	{0x7c, 0x06},
	{0x7d, 0x05},
	{0x7e, 0x05},
	{0x7f, 0x05},
	{0x90, 0x60},
	{0x91, 0x0f},
	{0x92, 0x35},
	{0x93, 0x36},
	{0x94, 0x0f},
	{0x95, 0x7e},
	{0x98, 0x5d},
	{0xa8, 0x50},
	{0xaa, 0x14},
	{0xab, 0x05},
	{0xac, 0x14},
	{0xad, 0x05},
	{0xae, 0x4a},
	{0xaf, 0x0e},
	{0xb2, 0x07},
	{0xb3, 0x0c},
	{0xc9, 0x28},
	{0xca, 0x5e},
	{0xcb, 0x5e},
	{0xcc, 0x5e},
	{0xcd, 0x5e},
	{0xce, 0x5c},
	{0xcf, 0x5c},
	{0xd0, 0x5c},
	{0xd1, 0x5c},
	{0xd2, 0x7c},
	{0xd3, 0x7c},
	{0xdb, 0x0f},
	{0xfd, 0x01},
	{0x46, 0x77},
	{0xdd, 0x00},
	{0xde, 0x3f},
	{0xfd, 0x03},
	{0x2b, 0x0a},
	{0x01, 0x22},
	{0x02, 0x03},
	{0x00, 0x06},
	{0x2a, 0x22},
	{0x29, 0x0b},
	{0x1e, 0x10},
	{0x1f, 0x02},
	{0x1a, 0x24},
	{0x1b, 0x62},
	{0x1c, 0xce},
	{0x1d, 0xd3},
	{0x04, 0x0f},
	{0x36, 0x00},
	{0x37, 0x05},
	{0x38, 0x09},
	{0x39, 0x19},
	{0x3a, 0x38},
	{0x3b, 0x22},
	{0x3c, 0x22},
	{0x3d, 0x22},
	{0x3e, 0x03},
	{0xfd, 0x02},
	{0xce, 0x65},
	{0xfd, 0x03},
	{0x03, 0x30},
	{0x05, 0x00},
	{0x12, 0x70},
	{0x13, 0x70},
	{0x16, 0x13},
	{0x21, 0xca},
	{0x27, 0x95},
	{0x2c, 0x55},
	{0x2d, 0x08},
	{0x2e, 0xca},
	{0x3f, 0xe7},
	{0xfd, 0x00},
	{0x8b, 0x01},
	{0x8d, 0x00},
	{0xfd, 0x01},
	{0x01, 0x02},
	{0xfd, 0x05},
	{0xc4, 0x62},
	{0xc5, 0x62},
	{0xc6, 0x62},
	{0xc7, 0x62},
	{0xf0, 0x40},
	{0xf1, 0x40},
	{0xf2, 0x40},
	{0xf3, 0x40},
	{0xf4, 0x00},
	{0xf9, 0x03},
	{0xfa, 0x5d},
	{0xfb, 0x6b},
	{0xb1, 0x01},
	{REG_NULL, 0x00},
};

static const struct os04d10_mode supported_modes[] = {
	{
		.width = 2568,
		.height = 1448,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.exp_def = 0x0080,
		.hts_def = 0x05dc * 2,
		.vts_def = 0xB83,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = os04d10_linear_10_2568x1448_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}
};

static const s64 link_freq_menu_items[] = {
	OS04D10_LINK_FREQ_360
};

static const char * const os04d10_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int os04d10_write_reg(struct i2c_client *client, u8 reg, u8 val)
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
		"os04d10 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int os04d10_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = os04d10_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int os04d10_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"os04d10 read reg:0x%x failed !\n", reg);

	return ret;
}

static int os04d10_set_gain_reg(struct os04d10 *os04d10, u32 gain)
{
	struct i2c_client *client = os04d10->client;
	u32 again = 0, dgain = 0;
	int ret = 0;

	/* again max is 15.5 gain convert thread is 15.5 * 1 * 64 = 992*/
	if (gain < OS04D10_GAIN_MIN) {
		again = 0x10;
		dgain = 0x0040;
	} else if (gain < 992) {
		again = gain >> 2;
		dgain = 0x0040;
	} else if (gain < OS04D10_GAIN_MAX) {
		again = 0xF8;
		dgain = gain * 64 / 992;
	} else {
		again = 0xF8;
		dgain = 0x07ff;
	}

	dev_dbg(&client->dev, "again: 0x%04x dgain: 0x%08x\n", again, dgain);
	ret = os04d10_write_reg(os04d10->client, OS04D10_REG_PAGE_SEL, OS04D10_REG_PAGE_CIS_TIMING);
	ret |= os04d10_write_reg(os04d10->client,
				 OS04D10_REG_ANA_GAIN,
				 again);
	ret |= os04d10_write_reg(os04d10->client, OS04D10_REG_EXP_UPDATE, 0x01);
	ret |= os04d10_write_reg(os04d10->client, OS04D10_REG_PAGE_SEL, OS04D10_REG_PAGE_CIS_CTRL);
	ret |= os04d10_write_reg(os04d10->client,
				 OS04D10_REG_DIG_GAIN_H,
				 (dgain >> 8) & 0xff);
	ret |= os04d10_write_reg(os04d10->client,
				 OS04D10_REG_DIG_GAIN_L,
				 dgain & 0xff);
	ret |= os04d10_write_reg(os04d10->client, OS04D10_REG_EXP_UPDATE, 0x01);

	return ret;
}

static int os04d10_get_reso_dist(const struct os04d10_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct os04d10_mode *
os04d10_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = os04d10_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int os04d10_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct os04d10 *os04d10 = to_os04d10(sd);
	const struct os04d10_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&os04d10->mutex);

	mode = os04d10_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&os04d10->mutex);
		return -ENOTTY;
#endif
	} else {
		os04d10->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(os04d10->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(os04d10->vblank, vblank_def,
					 OS04D10_VTS_MAX - mode->height,
					 1, vblank_def);
		os04d10->cur_fps = mode->max_fps;
	}

	mutex_unlock(&os04d10->mutex);

	return 0;
}

static int os04d10_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct os04d10 *os04d10 = to_os04d10(sd);
	const struct os04d10_mode *mode = os04d10->cur_mode;

	mutex_lock(&os04d10->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&os04d10->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		/* format info: width/height/data type/virctual channel */
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&os04d10->mutex);

	return 0;
}

static int os04d10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct os04d10 *os04d10 = to_os04d10(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = os04d10->cur_mode->bus_fmt;

	return 0;
}

static int os04d10_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int os04d10_enable_test_pattern(struct os04d10 *os04d10, u32 pattern)
{
	u8 val = 0;
	int ret = 0;

	ret = os04d10_write_reg(os04d10->client,
				OS04D10_REG_PAGE_SEL,
				OS04D10_REG_PAGE_CIS_CTRL);
	ret |= os04d10_read_reg(os04d10->client, OS04D10_REG_TEST_PATTERN, &val);
	if (pattern)
		val |= OS04D10_TEST_PATTERN_BIT_MASK;
	else
		val &= ~OS04D10_TEST_PATTERN_BIT_MASK;

	ret |= os04d10_write_reg(os04d10->client, OS04D10_REG_TEST_PATTERN, val);
	return ret;
}

static int os04d10_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct os04d10 *os04d10 = to_os04d10(sd);
	const struct os04d10_mode *mode = os04d10->cur_mode;

	if (os04d10->streaming)
		fi->interval = os04d10->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int os04d10_g_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct os04d10 *os04d10 = to_os04d10(sd);
	const struct os04d10_mode *mode = os04d10->cur_mode;

	u32 val = 1 << (OS04D10_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	if (mode->hdr_mode != NO_HDR)
		val |= V4L2_MBUS_CSI2_CHANNEL_1;
	if (mode->hdr_mode == HDR_X3)
		val |= V4L2_MBUS_CSI2_CHANNEL_2;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void os04d10_get_module_inf(struct os04d10 *os04d10,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, OS04D10_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, os04d10->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, os04d10->len_name, sizeof(inf->base.lens));
}

static long os04d10_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct os04d10 *os04d10 = to_os04d10(sd);
	struct rkmodule_hdr_cfg *hdr;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		os04d10_get_module_inf(os04d10, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = os04d10->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = os04d10_write_reg(os04d10->client,
						OS04D10_REG_CTRL_MODE,
						OS04D10_MODE_STREAMING);
		else
			ret = os04d10_write_reg(os04d10->client,
						OS04D10_REG_CTRL_MODE,
						OS04D10_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long os04d10_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = os04d10_ioctl(sd, cmd, inf);
		if (!ret) {
			if (copy_to_user(up, inf, sizeof(*inf)))
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

		ret = os04d10_ioctl(sd, cmd, hdr);
		if (!ret) {
			if (copy_to_user(up, hdr, sizeof(*hdr)))
				ret = -EFAULT;
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = os04d10_ioctl(sd, cmd, &stream);
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

static int __os04d10_start_stream(struct os04d10 *os04d10)
{
	int ret;

	if (!os04d10->is_thunderboot) {
		ret = os04d10_write_array(os04d10->client, os04d10->cur_mode->reg_list);
		if (ret)
			return ret;
		/* In case these controls are set before streaming */
		ret = __v4l2_ctrl_handler_setup(&os04d10->ctrl_handler);
		if (ret)
			return ret;
		if (os04d10->has_init_exp && os04d10->cur_mode->hdr_mode != NO_HDR) {
			ret = os04d10_ioctl(&os04d10->subdev, PREISP_CMD_SET_HDRAE_EXP,
				&os04d10->init_hdrae_exp);
			if (ret) {
				dev_err(&os04d10->client->dev,
					"init exp fail in hdr mode\n");
				return ret;
			}
		}
	}

	os04d10_write_reg(os04d10->client, OS04D10_REG_PAGE_SEL, OS04D10_REG_PAGE_SYS_CTRL);
	return os04d10_write_reg(os04d10->client, OS04D10_REG_CTRL_MODE, OS04D10_MODE_STREAMING);
}

static int __os04d10_stop_stream(struct os04d10 *os04d10)
{
	os04d10->has_init_exp = false;
	if (os04d10->is_thunderboot) {
		os04d10->is_first_streamoff = true;
		pm_runtime_put(&os04d10->client->dev);
	}

	os04d10_write_reg(os04d10->client, OS04D10_REG_PAGE_SEL, OS04D10_REG_PAGE_SYS_CTRL);
	return os04d10_write_reg(os04d10->client, OS04D10_REG_CTRL_MODE, OS04D10_MODE_SW_STANDBY);
}

static int __os04d10_power_on(struct os04d10 *os04d10);
static int os04d10_s_stream(struct v4l2_subdev *sd, int on)
{
	struct os04d10 *os04d10 = to_os04d10(sd);
	struct i2c_client *client = os04d10->client;
	int ret = 0;

	mutex_lock(&os04d10->mutex);
	on = !!on;
	if (on == os04d10->streaming)
		goto unlock_and_return;
	if (on) {
		if (os04d10->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			os04d10->is_thunderboot = false;
			__os04d10_power_on(os04d10);
		}
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		ret = __os04d10_start_stream(os04d10);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__os04d10_stop_stream(os04d10);
		pm_runtime_put(&client->dev);
	}

	os04d10->streaming = on;
unlock_and_return:
	mutex_unlock(&os04d10->mutex);
	return ret;
}

static int os04d10_s_power(struct v4l2_subdev *sd, int on)
{
	struct os04d10 *os04d10 = to_os04d10(sd);
	struct i2c_client *client = os04d10->client;
	int ret = 0;

	mutex_lock(&os04d10->mutex);

	/* If the power state is not modified - no work to do. */
	if (os04d10->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		if (!os04d10->is_thunderboot) {
			ret = os04d10_write_array(os04d10->client, os04d10_global_regs);
			if (ret) {
				v4l2_err(sd, "could not set init registers\n");
				pm_runtime_put_noidle(&client->dev);
				goto unlock_and_return;
			}
		}

		os04d10->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		os04d10->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&os04d10->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 os04d10_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OS04D10_XVCLK_FREQ / 1000 / 1000);
}

static int __os04d10_power_on(struct os04d10 *os04d10)
{
	int ret;
	u32 delay_us;
	struct device *dev = &os04d10->client->dev;

	if (!IS_ERR_OR_NULL(os04d10->pins_default)) {
		ret = pinctrl_select_state(os04d10->pinctrl,
					   os04d10->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	if (os04d10->is_thunderboot)
		return 0;

	if (!IS_ERR(os04d10->reset_gpio))
		gpiod_set_value_cansleep(os04d10->reset_gpio, 0);

	usleep_range(5000, 6000);

	ret = regulator_bulk_enable(OS04D10_NUM_SUPPLIES, os04d10->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(os04d10->reset_gpio))
		gpiod_set_value_cansleep(os04d10->reset_gpio, 1);

	usleep_range(500, 1000);

	if (!IS_ERR(os04d10->reset_gpio))
		usleep_range(8000, 10000);
	else
		usleep_range(12000, 16000);

	usleep_range(12000, 16000);

	ret = clk_set_rate(os04d10->xvclk, OS04D10_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(os04d10->xvclk) != OS04D10_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(os04d10->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = os04d10_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(os04d10->xvclk);

	return ret;
}

static void __os04d10_power_off(struct os04d10 *os04d10)
{
	int ret;
	struct device *dev = &os04d10->client->dev;

	clk_disable_unprepare(os04d10->xvclk);
	if (os04d10->is_thunderboot) {
		if (os04d10->is_first_streamoff) {
			os04d10->is_thunderboot = false;
			os04d10->is_first_streamoff = false;
		} else {
			return;
		}
	}

	clk_disable_unprepare(os04d10->xvclk);
	if (!IS_ERR(os04d10->reset_gpio))
		gpiod_set_value_cansleep(os04d10->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(os04d10->pins_sleep)) {
		ret = pinctrl_select_state(os04d10->pinctrl,
					   os04d10->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(OS04D10_NUM_SUPPLIES, os04d10->supplies);
}

static int os04d10_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os04d10 *os04d10 = to_os04d10(sd);

	return __os04d10_power_on(os04d10);
}

static int os04d10_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os04d10 *os04d10 = to_os04d10(sd);

	__os04d10_power_off(os04d10);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int os04d10_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct os04d10 *os04d10 = to_os04d10(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct os04d10_mode *def_mode = &supported_modes[0];

	mutex_lock(&os04d10->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&os04d10->mutex);
	/* No crop or compose */

	return 0;
}
#endif

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
static int os04d10_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = DST_WIDTH;
		sel->r.top = 0;
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}

static int os04d10_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops os04d10_pm_ops = {
	SET_RUNTIME_PM_OPS(os04d10_runtime_suspend,
			   os04d10_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops os04d10_internal_ops = {
	.open = os04d10_open,
};
#endif

static const struct v4l2_subdev_core_ops os04d10_core_ops = {
	.s_power = os04d10_s_power,
	.ioctl = os04d10_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = os04d10_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops os04d10_video_ops = {
	.s_stream = os04d10_s_stream,
	.g_frame_interval = os04d10_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops os04d10_pad_ops = {
	.enum_mbus_code = os04d10_enum_mbus_code,
	.enum_frame_size = os04d10_enum_frame_sizes,
	.enum_frame_interval = os04d10_enum_frame_interval,
	.get_fmt = os04d10_get_fmt,
	.set_fmt = os04d10_set_fmt,
	.get_selection = os04d10_get_selection,
	.get_mbus_config = os04d10_g_mbus_config,
};

static const struct v4l2_subdev_ops os04d10_subdev_ops = {
	.core	= &os04d10_core_ops,
	.video	= &os04d10_video_ops,
	.pad	= &os04d10_pad_ops,
};

static void os04d10_modify_fps_info(struct os04d10 *os04d10)
{
	const struct os04d10_mode *mode = os04d10->cur_mode;

	os04d10->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
				      os04d10->cur_vts;
}

static int os04d10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct os04d10 *os04d10 = container_of(ctrl->handler,
					       struct os04d10, ctrl_handler);
	struct i2c_client *client = os04d10->client;
	s64 max;
	int ret = 0;
	u8 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = os04d10->cur_mode->height + ctrl->val - 9;
		__v4l2_ctrl_modify_range(os04d10->exposure,
					 os04d10->exposure->minimum, max,
					 os04d10->exposure->step,
					 os04d10->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set exposure 0x%x\n", ctrl->val);
		if (os04d10->cur_mode->hdr_mode == NO_HDR) {

			ret = os04d10_write_reg(os04d10->client,
						OS04D10_REG_PAGE_SEL,
						OS04D10_REG_PAGE_CIS_TIMING);
			/* 4 least significant bits of expsoure are fractional part */
			ret |= os04d10_write_reg(os04d10->client,
						 OS04D10_REG_EXPOSURE_H,
						 (u8)OS04D10_FETCH_EXP_H(ctrl->val));
			ret |= os04d10_write_reg(os04d10->client,
						 OS04D10_REG_EXPOSURE_L,
						 (u8)OS04D10_FETCH_EXP_L(ctrl->val));
			ret |= os04d10_write_reg(os04d10->client,
						 OS04D10_REG_EXP_UPDATE,
						 0x01);
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_dbg(&client->dev, "set gain 0x%x\n", ctrl->val);
		if (os04d10->cur_mode->hdr_mode == NO_HDR)
			ret = os04d10_set_gain_reg(os04d10, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set vblank 0x%x\n", ctrl->val);
		ret = os04d10_write_reg(os04d10->client,
					OS04D10_REG_PAGE_SEL,
					OS04D10_REG_PAGE_CIS_TIMING);
		ret |= os04d10_write_reg(os04d10->client,
					 OS04D10_REG_VTS_H,
					 ((ctrl->val + os04d10->cur_mode->height) >> 8) & 0xff);
		ret |= os04d10_write_reg(os04d10->client,
					 OS04D10_REG_VTS_L,
					 (ctrl->val + os04d10->cur_mode->height) & 0xff);
		ret |= os04d10_write_reg(os04d10->client, OS04D10_REG_EXP_UPDATE, 0x01);
		os04d10->cur_vts = ctrl->val + os04d10->cur_mode->height;
		os04d10_modify_fps_info(os04d10);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = os04d10_enable_test_pattern(os04d10, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		dev_dbg(&client->dev, "set hflip 0x%x\n", ctrl->val);
		ret = os04d10_write_reg(os04d10->client,
					OS04D10_REG_PAGE_SEL,
					OS04D10_REG_PAGE_CIS_TIMING);
		ret |= os04d10_read_reg(os04d10->client,
				       OS04D10_FLIP_MIRROR_REG,
				       &val);
		ret |= os04d10_write_reg(os04d10->client,
					 OS04D10_FLIP_MIRROR_REG,
					 OS04D10_FETCH_MIRROR(val, ctrl->val));
		ret |= os04d10_write_reg(os04d10->client, OS04D10_REG_EXP_UPDATE, 0x01);
		break;
	case V4L2_CID_VFLIP:
		dev_dbg(&client->dev, "set vflip 0x%x\n", ctrl->val);
		ret = os04d10_write_reg(os04d10->client,
					OS04D10_REG_PAGE_SEL,
					OS04D10_REG_PAGE_CIS_TIMING);
		ret |= os04d10_read_reg(os04d10->client,
				       OS04D10_FLIP_MIRROR_REG,
				       &val);
		ret |= os04d10_write_reg(os04d10->client,
					 OS04D10_FLIP_MIRROR_REG,
					 OS04D10_FETCH_FLIP(val, ctrl->val));
		ret |= os04d10_write_reg(os04d10->client, OS04D10_REG_EXP_UPDATE, 0x01);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops os04d10_ctrl_ops = {
	.s_ctrl = os04d10_set_ctrl,
};

static int os04d10_initialize_controls(struct os04d10 *os04d10)
{
	const struct os04d10_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &os04d10->ctrl_handler;
	mode = os04d10->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &os04d10->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, PIXEL_RATE_WITH_360M_10BIT, 1, PIXEL_RATE_WITH_360M_10BIT);

	h_blank = mode->hts_def - mode->width;
	os04d10->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (os04d10->hblank)
		os04d10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	os04d10->vblank = v4l2_ctrl_new_std(handler, &os04d10_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    OS04D10_VTS_MAX - mode->height,
					    1, vblank_def);
	exposure_max = mode->vts_def - 9;
	os04d10->exposure = v4l2_ctrl_new_std(handler, &os04d10_ctrl_ops,
					      V4L2_CID_EXPOSURE, OS04D10_EXPOSURE_MIN,
					      exposure_max, OS04D10_EXPOSURE_STEP,
					      mode->exp_def);
	os04d10->anal_gain = v4l2_ctrl_new_std(handler, &os04d10_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, OS04D10_GAIN_MIN,
					       OS04D10_GAIN_MAX, OS04D10_GAIN_STEP,
					       OS04D10_GAIN_DEFAULT);
	os04d10->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &os04d10_ctrl_ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(os04d10_test_pattern_menu) - 1,
					0, 0, os04d10_test_pattern_menu);
	v4l2_ctrl_new_std(handler, &os04d10_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &os04d10_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&os04d10->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	os04d10->subdev.ctrl_handler = handler;
	os04d10->has_init_exp = false;
	os04d10->cur_fps = mode->max_fps;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int os04d10_check_sensor_id(struct os04d10 *os04d10,
				   struct i2c_client *client)
{
	struct device *dev = &os04d10->client->dev;
	u8 id = 0;
	u32 val = 0;
	int ret = 0, i = 0;

	if (os04d10->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	for (i = 0; i < OS04D10_REG_VALUE_32BIT; i++) {
		ret |= os04d10_read_reg(client, OS04D10_REG_CHIP_ID + i, &id);
		val = (val << 8) | id;
	}

	if (val != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%08x), ret(%d)\n", val, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%08x sensor\n", CHIP_ID);

	return 0;
}

static int os04d10_configure_regulators(struct os04d10 *os04d10)
{
	unsigned int i;

	for (i = 0; i < OS04D10_NUM_SUPPLIES; i++)
		os04d10->supplies[i].supply = os04d10_supply_names[i];

	return devm_regulator_bulk_get(&os04d10->client->dev,
				       OS04D10_NUM_SUPPLIES,
				       os04d10->supplies);
}

static int os04d10_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct os04d10 *os04d10;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	int i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	os04d10 = devm_kzalloc(dev, sizeof(*os04d10), GFP_KERNEL);
	if (!os04d10)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &os04d10->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &os04d10->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &os04d10->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &os04d10->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	os04d10->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);

	os04d10->client = client;
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			os04d10->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(supported_modes))
		os04d10->cur_mode = &supported_modes[0];

	os04d10->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(os04d10->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	os04d10->reset_gpio = devm_gpiod_get(dev, "reset", os04d10->is_thunderboot ? GPIOD_ASIS : GPIOD_OUT_LOW);
	if (IS_ERR(os04d10->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	if (!IS_ERR(os04d10->reset_gpio))
		gpiod_set_value_cansleep(os04d10->reset_gpio, 0);

	os04d10->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(os04d10->pinctrl)) {
		os04d10->pins_default =
			pinctrl_lookup_state(os04d10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(os04d10->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		os04d10->pins_sleep =
			pinctrl_lookup_state(os04d10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(os04d10->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = os04d10_configure_regulators(os04d10);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&os04d10->mutex);

	sd = &os04d10->subdev;
	v4l2_i2c_subdev_init(sd, client, &os04d10_subdev_ops);
	ret = os04d10_initialize_controls(os04d10);
	if (ret)
		goto err_destroy_mutex;

	ret = __os04d10_power_on(os04d10);
	if (ret)
		goto err_free_handler;

	ret = os04d10_check_sensor_id(os04d10, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &os04d10_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	os04d10->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &os04d10->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(os04d10->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 os04d10->module_index, facing,
		 OS04D10_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	if (os04d10->is_thunderboot)
		pm_runtime_get_sync(dev);
	else
		pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__os04d10_power_off(os04d10);
err_free_handler:
	v4l2_ctrl_handler_free(&os04d10->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&os04d10->mutex);

	return ret;
}

static int os04d10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os04d10 *os04d10 = to_os04d10(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&os04d10->ctrl_handler);
	mutex_destroy(&os04d10->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__os04d10_power_off(os04d10);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id os04d10_of_match[] = {
	{ .compatible = "ovti,os04d10" },
	{},
};
MODULE_DEVICE_TABLE(of, os04d10_of_match);
#endif

static const struct i2c_device_id os04d10_match_id[] = {
	{ "ovti,os04d10", 0 },
	{ },
};

static struct i2c_driver os04d10_i2c_driver = {
	.driver = {
		.name = OS04D10_NAME,
		.pm = &os04d10_pm_ops,
		.of_match_table = of_match_ptr(os04d10_of_match),
	},
	.probe		= &os04d10_probe,
	.remove		= &os04d10_remove,
	.id_table	= os04d10_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&os04d10_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&os04d10_i2c_driver);
}

#if defined(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) && !defined(CONFIG_INITCALL_ASYNC)
subsys_initcall(sensor_mod_init);
#else
device_initcall_sync(sensor_mod_init);
#endif
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("ovti os04d10 sensor driver");
MODULE_LICENSE("GPL");
