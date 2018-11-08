// SPDX-License-Identifier: GPL-2.0
/*
 * ov2735 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
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
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

/* 45Mhz * 4 Binning */

#define OV2735_XVCLK_FREQ		24000000
#define REG_NULL			0xFFFF
#define PAGE_SELECT_REG		0xfd
#define PAGE_ZERO			0x00
#define PAGE_ONE			0x01
#define PAGE_TWO			0x02
#define PAGE_OTP			0x04

//PAGE0
#define OV2735_PIDH_ADDR	0x02
#define OV2735_PIDL_ADDR	0x03
#define OV2735_PIDH_MAGIC	0x27
#define OV2735_PIDL_MAGIC	0x35

//PAGE1
#define STREAM_CTRL_REG		0xa0
#define STREAM_ON			0x01
#define STREAM_OFF			0x00

#define UPDOWN_MIRROR_REG	0x3f
#define H_V_NORMAL			0x00
#define H_MIRROR			0x01
#define V_FLIP				0x02
#define MIRROR_AND_FLIP		0x03

#define OV2735_VTS_HIGH_REG		0x0e
#define OV2735_VTS_LOW_REG		0x0f
#define OV2735_COARSE_INTG_TIME_MIN		1
#define OV2735_COARSE_INTG_TIME_MAX		4
#define OV2735_VTS_ENABLE_REG	0x0d
#define OV2735_VTS_ENABLE_VALUE	0x10
#define OV2735_FRAME_SYNC_REG	0x01
#define OV2735_FRAME_SYNC_VALUE	0x01
#define OV2735_REG_TEST_PATTERN 0xb2
#define OV2735_HTS_HIGH_REG		0x09
#define OV2735_HTS_LOW_REG		0x0a
#define OV2735_TEST_PATTERN_ENABLE		BIT(0)
#define OV2735_TEST_PATTERN_DISABLE		0xfe
#define OV2735_FINE_INTG_TIME_MIN		0
#define OV2735_FINE_INTG_TIME_MAX_MARGIN 0
#define OV2735_COARSE_INTG_TIME_MIN		1
#define OV2735_COARSE_INTG_TIME_MAX_MARGIN 4

#define OV2735_AEC_PK_LONG_EXPO_2ND_REG	0x03	/* Exposure Bits 8-15 */
#define OV2735_AEC_PK_LONG_EXPO_1ST_REG	0x04	/* Exposure Bits  0-7 */
#define OV2735_FETCH_2ND_BYTE_EXP(VAL)	((VAL >> 8) & 0xFF)
#define OV2735_FETCH_1ST_BYTE_EXP(VAL)	(VAL & 0xFF)

#define OV2735_AEC_PK_GAIN_REG	0x24	/* GAIN Bits 0 -7 */
#define OV2735_FETCH_LSB_GAIN(VAL)		(VAL & 0x00FF)
#define OV2735_FETCH_MSB_GAIN(VAL)		((VAL >> 8) & 0x01)

#define	OV2735_EXPOSURE_MIN		4
#define	OV2735_EXPOSURE_STEP		1
#define OV2735_VTS_MAX			0xfff
#define	ANALOG_GAIN_MIN			0x10
#define	ANALOG_GAIN_MAX			0xff
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x10

static const char * const ov2735_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV2735_NUM_SUPPLIES ARRAY_SIZE(ov2735_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov2735_mode {
	u32 width;
	u32 height;
	u32 max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct ov2735 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV2735_NUM_SUPPLIES];

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
	const struct ov2735_mode *cur_mode;
};

#define to_ov2735(sd) container_of(sd, struct ov2735, subdev)

static const struct regval ov2735_global_regs[] = {
	{0xfd, 0x00},
	{0x20, 0x01},	// soft reset modify to 0x01
	{0x0, 0x3},	// delay 3ms	// delay 3ms
	{REG_NULL, 0x00},
};

/*
 * Base sensor configs
 * ov2735_init_tab_1920_1080_30fps
 * MCLK:24MHz  1920x1080  30fps   mipi 2lane   420Mbps/lane
 */
static struct regval ov2735_1920_1080_30fps[] = {
	{0xfd, 0x00},
	{0x2f, 0x10},	// clk and pll setting
	{0x34, 0x00},
	{0x30, 0x15},
	{0x33, 0x01},
	{0x35, 0x20},
	{0xfd, 0x01},
	{0x0d, 0x00},	// disable modify VTS
	{0x30, 0x00},
	{0x03, 0x01},	// exposure time, MSB default 0x01
	{0x04, 0x8f},	// exposure time, LSB default 0x8f
	{0x01, 0x01},	// enable of frame sync signal
	{0x09, 0x00},	// HBLANK
	{0x0a, 0x20},
	{0x06, 0x0a},	// VBLANK 8LSB
	{0x24, 0x10},	// gain default 0x10, by yjz
	{0x01, 0x01},
	{0xfb, 0x73},	// ABL
	{0x01, 0x01},
	{0xfd, 0x01},
	{0x1a, 0x6b},	// Timing ctrl
	{0x1c, 0xea},
	{0x16, 0x0c},
	{0x21, 0x00},
	{0x11, 0x63},
	{0x19, 0xc3},
	{0x26, 0x5a},	// ANALOG CTRL
	{0x29, 0x01},
	{0x33, 0x6f},
	{0x2a, 0xd2},
	{0x2c, 0x40},
	{0xd0, 0x02},
	{0xd1, 0x01},
	{0xd2, 0x20},
	{0xd3, 0x04},
	{0xd4, 0x2a},
	{0x50, 0x00},	// Timing ctrl
	{0x51, 0x2c},
	{0x52, 0x29},
	{0x53, 0x00},
	{0x55, 0x44},
	{0x58, 0x29},
	{0x5a, 0x00},
	{0x5b, 0x00},
	{0x5d, 0x00},
	{0x64, 0x2f},
	{0x66, 0x62},
	{0x68, 0x5b},
	{0x75, 0x46},
	{0x76, 0x36},
	{0x77, 0x4f},
	{0x78, 0xef},
	{0x72, 0xcf},
	{0x73, 0x36},
	{0x7d, 0x0d},
	{0x7e, 0x0d},
	{0x8a, 0x77},
	{0x8b, 0x77},
	{0xfd, 0x01},
	{0xb1, 0x83},	// MIPI register ---
	{0xb3, 0x0b},
	{0xb4, 0x14},
	{0x9d, 0x40},
	{0xa1, 0x05},
	{0x94, 0x44},
	{0x95, 0x33},
	{0x96, 0x1f},
	{0x98, 0x45},
	{0x9c, 0x10},
	{0xb5, 0x70},
	{0x25, 0xe0},
	{0x20, 0x7b},
	{0x8f, 0x88},	// H_SIZE_MIPI_8LSB
	{0x91, 0x40},	// V_SIZE_MIPI_8LSB
	{0xfd, 0x01},
	{0xfd, 0x02},
	{0xa1, 0x04},
	{0xa3, 0x40},
	{0xa5, 0x02},
	{0xa7, 0xc4},
	{0xfd, 0x01},
	{0x86, 0x77},	// BLC
	{0x89, 0x77},
	{0x87, 0x74},
	{0x88, 0x74},
	{0xfc, 0xe0},
	{0xfe, 0xe0},
	{0xf0, 0x40},
	{0xf1, 0x40},
	{0xf2, 0x40},
	{0xf3, 0x40},
	//1920x1080
	{0xfd, 0x02},
	{0xa0, 0x00},	// Image vertical start MSB3bits
	{0xa1, 0x08},	// Image vertical start LSB8bits
	{0xa2, 0x04},	// image vertical size  MSB8bits
	{0xa3, 0x38},	// image vertical size  LSB8bits
	{0xa4, 0x00},
	{0xa5, 0x08},	// H start 8Lsb
	{0xa6, 0x03},
	{0xa7, 0xc0},	// Half H size Lsb8bits
	{0xfd, 0x01},
	{0x8e, 0x07},
	{0x8f, 0x80},	// MIPI column number
	{0x90, 0x04},	// MIPI row number
	{0x91, 0x38},
	//TV1080_30fps
	{0xfd, 0x01},
	{0x0d, 0x10},	// enable manual modify the VTS
	{0x0e, 0x04},
	{0x0f, 0xc1},	// Vblank, VTS:0x4c1, 30.037fps
	{0x01, 0x01},		// enable of frame sync signal
	{REG_NULL, 0x00},
};

#define HTS_DEF 0x020
#define VTS_DEF 0x4c1
#define MAX_FPS 30
static const struct ov2735_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = MAX_FPS,
		.exp_def = 0x18f,
		.hts_def = HTS_DEF,
		.vts_def = VTS_DEF,
		.reg_list = ov2735_1920_1080_30fps,
	},
};

#define OV2735_LINK_FREQ_420MHZ		420000000
#define OV2735_PIXEL_RATE		(MAX_FPS * HTS_DEF * VTS_DEF)
static const s64 link_freq_menu_items[] = {
	OV2735_LINK_FREQ_420MHZ
};

static const char * const ov2735_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color",
};

/* Write registers up to 4 at a time */
static int ov2735_write_reg(struct i2c_client *client, u8 reg, u8 val)
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
		"ov2735 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int ov2735_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	int i, ret = 0;

	i = 0;
	while (regs[i].addr != REG_NULL) {
		ret = ov2735_write_reg(client, regs[i].addr, regs[i].val);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}

		i++;
	}

	return ret;
}

/* Read registers up to 4 at a time */
/* sensor register read */
static int ov2735_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"ov2735 read reg:0x%x failed !\n", reg);

	return ret;
}

static int ov2735_get_reso_dist(const struct ov2735_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov2735_mode *
ov2735_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov2735_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov2735_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov2735 *ov2735 = to_ov2735(sd);
	const struct ov2735_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&ov2735->mutex);

	mode = ov2735_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov2735->mutex);
		return -ENOTTY;
#endif
	} else {
		ov2735->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov2735->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov2735->vblank, vblank_def,
					 OV2735_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&ov2735->mutex);

	return 0;
}

static int ov2735_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov2735 *ov2735 = to_ov2735(sd);
	const struct ov2735_mode *mode = ov2735->cur_mode;

	mutex_lock(&ov2735->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov2735->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ov2735->mutex);

	return 0;
}

static int ov2735_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov2735_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov2735_enable_test_pattern(struct ov2735 *ov2735, u32 pattern)
{
	int ret;
	u8 val;

	ret = ov2735_read_reg(ov2735->client, OV2735_REG_TEST_PATTERN, &val);
	if (ret < 0)
		return ret;

	switch (pattern) {
	case 0:
		val &= ~OV2735_TEST_PATTERN_ENABLE;
		break;
	case 1:
		val |= OV2735_TEST_PATTERN_ENABLE;
		break;
	}

	return ov2735_write_reg(ov2735->client,
				 OV2735_REG_TEST_PATTERN,
				 val);
}

static int __ov2735_start_stream(struct ov2735 *ov2735)
{
	int ret;

	ret = ov2735_write_array(ov2735->client, ov2735_global_regs);
	if (ret)
		return ret;
	mdelay(3);
	ret = ov2735_write_array(ov2735->client, ov2735->cur_mode->reg_list);
	if (ret)
		return ret;
	ret = ov2735_write_reg(ov2735->client, PAGE_SELECT_REG, PAGE_ONE);
	if (ret)
		return ret;
	/* In case these controls are set before streaming */
	mutex_unlock(&ov2735->mutex);
	ret = v4l2_ctrl_handler_setup(&ov2735->ctrl_handler);
	mutex_lock(&ov2735->mutex);
	if (ret)
		return ret;

	ret |= ov2735_write_reg(ov2735->client, STREAM_CTRL_REG, STREAM_ON);

	return ret;
}

static int __ov2735_stop_stream(struct ov2735 *ov2735)
{
	int ret;

	ret = ov2735_write_reg(ov2735->client, PAGE_SELECT_REG, PAGE_ONE);
	ret |= ov2735_write_reg(ov2735->client, STREAM_CTRL_REG, STREAM_OFF);

	return ret;
}

static int ov2735_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov2735 *ov2735 = to_ov2735(sd);
	struct i2c_client *client = ov2735->client;
	int ret = 0;

	mutex_lock(&ov2735->mutex);
	on = !!on;
	if (on == ov2735->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov2735_start_stream(ov2735);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ov2735_stop_stream(ov2735);
		pm_runtime_put(&client->dev);
	}

	ov2735->streaming = on;

unlock_and_return:
	mutex_unlock(&ov2735->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov2735_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV2735_XVCLK_FREQ / 1000 / 1000);
}

static int __ov2735_power_on(struct ov2735 *ov2735)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov2735->client->dev;

	if (!IS_ERR(ov2735->pwdn_gpio)) {
		gpiod_set_value_cansleep(ov2735->pwdn_gpio, 1);
		usleep_range(2000, 5000);
	}

	ret = regulator_bulk_enable(OV2735_NUM_SUPPLIES, ov2735->supplies);
	usleep_range(20000, 50000);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov2735->pwdn_gpio)) {
		gpiod_set_value_cansleep(ov2735->pwdn_gpio, 0);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(ov2735->reset_gpio)) {
		gpiod_set_value_cansleep(ov2735->reset_gpio, 1);
		usleep_range(2000, 5000);
	}
	if (!IS_ERR(ov2735->xvclk)) {
		ret = clk_prepare_enable(ov2735->xvclk);
		if (ret < 0)
			dev_info(dev, "Failed to enable xvclk\n");
	}

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov2735_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov2735->xvclk);

	return ret;
}

static void __ov2735_power_off(struct ov2735 *ov2735)
{
	if (!IS_ERR(ov2735->pwdn_gpio))
		gpiod_set_value_cansleep(ov2735->pwdn_gpio, 0);
	clk_disable_unprepare(ov2735->xvclk);
	if (!IS_ERR(ov2735->reset_gpio))
		gpiod_set_value_cansleep(ov2735->reset_gpio, 1);
	regulator_bulk_disable(OV2735_NUM_SUPPLIES, ov2735->supplies);
}

static int ov2735_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2735 *ov2735 = to_ov2735(sd);

	return __ov2735_power_on(ov2735);
}

static int ov2735_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2735 *ov2735 = to_ov2735(sd);

	__ov2735_power_off(ov2735);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov2735_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov2735 *ov2735 = to_ov2735(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov2735_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov2735->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov2735->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static const struct dev_pm_ops ov2735_pm_ops = {
	SET_RUNTIME_PM_OPS(ov2735_runtime_suspend,
			   ov2735_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov2735_internal_ops = {
	.open = ov2735_open,
};
#endif

static const struct v4l2_subdev_video_ops ov2735_video_ops = {
	.s_stream = ov2735_s_stream,
};

static const struct v4l2_subdev_pad_ops ov2735_pad_ops = {
	.enum_mbus_code = ov2735_enum_mbus_code,
	.enum_frame_size = ov2735_enum_frame_sizes,
	.get_fmt = ov2735_get_fmt,
	.set_fmt = ov2735_set_fmt,
};

static const struct v4l2_subdev_ops ov2735_subdev_ops = {
	.video	= &ov2735_video_ops,
	.pad	= &ov2735_pad_ops,
};

static int ov2735_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov2735 *ov2735 = container_of(ctrl->handler,
					     struct ov2735, ctrl_handler);
	struct i2c_client *client = ov2735->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov2735->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov2735->exposure,
					 ov2735->exposure->minimum, max,
					 ov2735->exposure->step,
					 ov2735->exposure->default_value);
		break;
	}
	if (pm_runtime_get(&client->dev) <= 0)
		return 0;

	ret = ov2735_write_reg(client, PAGE_SELECT_REG, PAGE_ONE);
	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret |= ov2735_write_reg(client,
			 OV2735_AEC_PK_LONG_EXPO_2ND_REG,
			 OV2735_FETCH_2ND_BYTE_EXP(ctrl->val));
		ret |= ov2735_write_reg(client,
			 OV2735_AEC_PK_LONG_EXPO_1ST_REG,
			 OV2735_FETCH_1ST_BYTE_EXP(ctrl->val));
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret |= ov2735_write_reg(client, OV2735_AEC_PK_GAIN_REG,
			ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret |= ov2735_write_reg(client, OV2735_VTS_ENABLE_REG,
			 OV2735_VTS_ENABLE_VALUE);
		ret |= ov2735_write_reg(client, OV2735_VTS_LOW_REG,
			 (ctrl->val + ov2735->cur_mode->height) & 0xFF);
		ret |= ov2735_write_reg(client, OV2735_VTS_HIGH_REG,
			 ((ctrl->val + ov2735->cur_mode->height) >> 8) & 0x0F);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov2735_enable_test_pattern(ov2735, ctrl->val);

		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}
	ret |= ov2735_write_reg(client, OV2735_FRAME_SYNC_REG,
			 OV2735_FRAME_SYNC_VALUE);

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov2735_ctrl_ops = {
	.s_ctrl = ov2735_set_ctrl,
};

static int ov2735_initialize_controls(struct ov2735 *ov2735)
{
	const struct ov2735_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &ov2735->ctrl_handler;
	mode = ov2735->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 7);
	if (ret)
		return ret;
	handler->lock = &ov2735->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, OV2735_PIXEL_RATE, 1, OV2735_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	ov2735->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov2735->hblank)
		ov2735->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov2735->vblank = v4l2_ctrl_new_std(handler, &ov2735_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV2735_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	ov2735->exposure = v4l2_ctrl_new_std(handler, &ov2735_ctrl_ops,
				V4L2_CID_EXPOSURE, OV2735_EXPOSURE_MIN,
				exposure_max, OV2735_EXPOSURE_STEP,
				mode->exp_def);

	ov2735->anal_gain = v4l2_ctrl_new_std(handler, &ov2735_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	ov2735->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov2735_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov2735_test_pattern_menu) - 1,
				0, 0, ov2735_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov2735->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov2735->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov2735_check_sensor_id(struct ov2735 *ov2735,
				  struct i2c_client *client)
{
	struct device *dev = &ov2735->client->dev;
	int ret;

	u8 pidh = 0x55, pidl = 0xaa;

	ret = ov2735_write_reg(ov2735->client, PAGE_SELECT_REG, PAGE_ZERO);
	ret |= ov2735_read_reg(ov2735->client, OV2735_PIDH_ADDR, &pidh);
	ret |= ov2735_read_reg(ov2735->client, OV2735_PIDL_ADDR, &pidl);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if ((pidh == OV2735_PIDH_MAGIC) && (pidl == OV2735_PIDL_MAGIC)) {
		dev_info(dev,
			"Found cameraID 0x%02x%02x\n", pidh, pidl);
	} else {
		dev_err(dev,
			"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
			OV2735_PIDH_MAGIC, OV2735_PIDL_MAGIC, pidh, pidl);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	dev_err(dev, "failed with error (%d)\n", ret);
	return ret;
}

static int ov2735_configure_regulators(struct ov2735 *ov2735)
{
	size_t i;

	for (i = 0; i < OV2735_NUM_SUPPLIES; i++)
		ov2735->supplies[i].supply = ov2735_supply_names[i];

	return devm_regulator_bulk_get(&ov2735->client->dev,
				       OV2735_NUM_SUPPLIES,
				       ov2735->supplies);
}

static int ov2735_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ov2735 *ov2735;
	struct v4l2_subdev *sd;
	int ret;

	ov2735 = devm_kzalloc(dev, sizeof(*ov2735), GFP_KERNEL);
	if (!ov2735)
		return -ENOMEM;

	ov2735->client = client;
	ov2735->cur_mode = &supported_modes[0];

	ov2735->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov2735->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}
	ret = clk_set_rate(ov2735->xvclk, OV2735_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(ov2735->xvclk) != OV2735_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	ov2735->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov2735->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov2735->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov2735->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov2735_configure_regulators(ov2735);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&ov2735->mutex);

	sd = &ov2735->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov2735_subdev_ops);
	ret = ov2735_initialize_controls(ov2735);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov2735_power_on(ov2735);
	if (ret)
		goto err_free_handler;

	ret = ov2735_check_sensor_id(ov2735, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov2735_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov2735->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&sd->entity, 1, &ov2735->pad, 0);
	if (ret < 0)
		goto err_power_off;
#endif

	ret = v4l2_async_register_subdev(sd);
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
	__ov2735_power_off(ov2735);
err_free_handler:
	v4l2_ctrl_handler_free(&ov2735->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov2735->mutex);

	return ret;
}

static int ov2735_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2735 *ov2735 = to_ov2735(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov2735->ctrl_handler);
	mutex_destroy(&ov2735->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov2735_power_off(ov2735);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov2735_of_match[] = {
	{ .compatible = "ovti,ov2735" },
	{},
};
MODULE_DEVICE_TABLE(of, ov2735_of_match);
#endif

static const struct i2c_device_id ov2735_match_id[] = {
	{ "ovti,ov2735", 0 },
	{ },
};

static struct i2c_driver ov2735_i2c_driver = {
	.driver = {
		.name = "ov2735",
		.pm = &ov2735_pm_ops,
		.of_match_table = of_match_ptr(ov2735_of_match),
	},
	.probe		= &ov2735_probe,
	.remove		= &ov2735_remove,
	.id_table	= ov2735_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov2735_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov2735_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov2735 sensor driver");
MODULE_LICENSE("GPL v2");