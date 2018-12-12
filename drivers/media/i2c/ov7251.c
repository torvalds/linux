// SPDX-License-Identifier: GPL-2.0
/*
 * ov7251 driver
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

/* 48Mhz */
#define OV7251_PIXEL_RATE		(48 * 1000 * 1000)
#define OV7251_XVCLK_FREQ		24000000

#define CHIP_ID				0x007750
#define OV7251_REG_CHIP_ID		0x300a
#define OV7251_REG_MOD_VENDOR_ID	0x3d10
#define OV7251_REG_OPT_LOAD_CTRL	0x3d81

#define OV7251_REG_CTRL_MODE		0x0100
#define OV7251_MODE_SW_STANDBY		0x0
#define OV7251_MODE_STREAMING		BIT(0)

#define OV7251_REG_EXPOSURE		0x3500
#define OV7251_EXPOSURE_MIN		4
#define OV7251_EXPOSURE_STEP		0xf
#define OV7251_VTS_MAX			0xffff

#define OV7251_REG_ANALOG_GAIN		0x350a
#define ANALOG_GAIN_MASK		0x3ff
#define ANALOG_GAIN_MIN			0x10
#define ANALOG_GAIN_MAX			0x3e0
#define ANALOG_GAIN_STEP		1
#define ANALOG_GAIN_DEFAULT		0x20

#define OV7251_REG_TEST_PATTERN		0x5e00
#define	OV7251_TEST_PATTERN_ENABLE	0x80
#define	OV7251_TEST_PATTERN_DISABLE	0x0

#define OV7251_REG_VTS			0x380e

#define REG_NULL			0xFFFF

#define OV7251_REG_VALUE_08BIT		1
#define OV7251_REG_VALUE_16BIT		2
#define OV7251_REG_VALUE_24BIT		3

static const char * const ov7251_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power  not needed*/
};

#define OV7251_NUM_SUPPLIES ARRAY_SIZE(ov7251_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov7251_mode {
	u32 width;
	u32 height;
	u32 max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct ov7251 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV7251_NUM_SUPPLIES];

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
	const struct ov7251_mode *cur_mode;
};

#define to_ov7251(sd) container_of(sd, struct ov7251, subdev)

/*
 * Xclk 24Mhz
 * Pclk 48Mhz
 * PCLK = HTS * VTS * FPS
 * linelength 928(0x3a0)
 * framelength 1720(0x6b8)
 * grabwindow_width 640
 * grabwindow_height 480
 * max_framerate 30fps
 * mipi_datarate per lane 640Mbps
 */
static const struct regval ov7251_640x480_regs[] = {
	{0x0100, 0x00},
	{0x0103, 0x01},

	{0x3001, 0x62},
	{0x3005, 0x00},
	{0x3012, 0xc0},
	{0x3013, 0xd2},
	{0x3014, 0x04},
	{0x3016, 0x10},
	{0x3017, 0x00},
	{0x3018, 0x00},
	{0x301a, 0x00},
	{0x301b, 0x00},
	{0x301c, 0x20},
	{0x3023, 0x05},
	{0x3037, 0xf0},
	{0x3098, 0x04},
	{0x3099, 0x28},
	{0x309a, 0x05},
	{0x309b, 0x04},
	{0x30b0, 0x0a},
	{0x30b1, 0x01},
	{0x30b3, 0x64},
	{0x30b4, 0x03},
	{0x30b5, 0x05},
	{0x3106, 0xda},
	{0x3500, 0x00},
	{0x3501, 0x1f},
	{0x3502, 0x80},
	{0x3503, 0x07},
	{0x3509, 0x10},
	{0x350b, 0x10},
	{0x3600, 0x1c},
	{0x3602, 0x62},
	{0x3620, 0xb7},
	{0x3622, 0x04},
	{0x3626, 0x21},
	{0x3627, 0x30},
	{0x3630, 0x44},
	{0x3631, 0x35},
	{0x3634, 0x60},
	{0x3636, 0x00},
	{0x3662, 0x01},
	{0x3663, 0x70},
	{0x3664, 0xf0},
	{0x3666, 0x0a},
	{0x3669, 0x1a},
	{0x366a, 0x00},
	{0x366b, 0x50},
	{0x3673, 0x01},
	{0x3674, 0xff},
	{0x3675, 0x03},
	{0x3705, 0xc1},
	{0x3709, 0x40},
	{0x373c, 0x08},
	{0x3742, 0x00},
	{0x3757, 0xb3},
	{0x3788, 0x00},
	{0x37a8, 0x01},
	{0x37a9, 0xc0},
	{0x3800, 0x00},
	{0x3801, 0x04},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x02},
	{0x3805, 0x8b},
	{0x3806, 0x01},
	{0x3807, 0xeb},
	{0x3808, 0x02},
	{0x3809, 0x80},
	{0x380a, 0x01},
	{0x380b, 0xe0},
	{0x380c, 0x03},
	{0x380d, 0xa0},
	{0x380e, 0x06},
	{0x380f, 0xb8},
	{0x3810, 0x00},
	{0x3811, 0x02},
	{0x3812, 0x00},
	{0x3813, 0x02},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x40},
	{0x3821, 0x00},
	{0x382f, 0x0e},
	{0x3832, 0x00},
	{0x3833, 0x05},
	{0x3834, 0x00},
	{0x3835, 0x0c},
	{0x3837, 0x00},
	{0x3b80, 0x00},
	{0x3b81, 0xa5},
	{0x3b82, 0x10},
	{0x3b83, 0x00},
	{0x3b84, 0x08},
	{0x3b85, 0x00},
	{0x3b86, 0x01},
	{0x3b87, 0x00},
	{0x3b88, 0x00},
	{0x3b89, 0x00},
	{0x3b8a, 0x00},
	{0x3b8b, 0x05},
	{0x3b8c, 0x00},
	{0x3b8d, 0x00},
	{0x3b8e, 0x00},
	{0x3b8f, 0x1a},
	{0x3b94, 0x05},
	{0x3b95, 0xf2},
	{0x3b96, 0x40},
	{0x3c00, 0x89},
	{0x3c01, 0x63},
	{0x3c02, 0x01},
	{0x3c03, 0x00},
	{0x3c04, 0x00},
	{0x3c05, 0x03},
	{0x3c06, 0x00},
	{0x3c07, 0x06},
	{0x3c0c, 0x01},
	{0x3c0d, 0xd0},
	{0x3c0e, 0x02},
	{0x3c0f, 0x04},
	{0x4001, 0x42},
	{0x4004, 0x04},
	{0x4005, 0x00},
	{0x404e, 0x01},
	{0x4241, 0x00},
	{0x4242, 0x00},
	{0x4300, 0xff},
	{0x4301, 0x00},
	{0x4501, 0x48},
	{0x4600, 0x00},
	{0x4601, 0x4e},
	{0x4801, 0x0f},
	{0x4806, 0x0f},
	{0x4819, 0xaa},
	{0x4823, 0x3e},
	{0x4837, 0x19},
	{0x4a0d, 0x00},
	{0x4a47, 0x7f},
	{0x4a49, 0xf0},
	{0x4a4b, 0x30},
	{0x5000, 0x85},
	{0x5001, 0x80},
	{REG_NULL, 0x00},
};

static const struct ov7251_mode supported_modes[] = {
	{
		.width = 640,
		.height = 480,
		.max_fps = 30,
		.exp_def = 0x061c,
		.hts_def = 0x03a0,
		.vts_def = 0x06b8,
		.reg_list = ov7251_640x480_regs,
	},
};

#define OV7251_LINK_FREQ_320MHZ		320000000
static const s64 link_freq_menu_items[] = {
	OV7251_LINK_FREQ_320MHZ
};

static const char * const ov7251_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar",
};

/* Write registers up to 4 at a time */
static int ov7251_write_reg(struct i2c_client *client, u16 reg,
			    int len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	usleep_range(1500, 1600);
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

static int ov7251_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov7251_write_reg(client, regs[i].addr,
				       OV7251_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int ov7251_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
			   u32 *val)
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

static int ov7251_get_reso_dist(const struct ov7251_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov7251_mode *
ov7251_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov7251_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov7251_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov7251 *ov7251 = to_ov7251(sd);
	const struct ov7251_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&ov7251->mutex);

	mode = ov7251_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_Y10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov7251->mutex);
		return -ENOTTY;
#endif
	} else {
		ov7251->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov7251->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov7251->vblank, vblank_def,
					 OV7251_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&ov7251->mutex);

	return 0;
}

static int ov7251_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov7251 *ov7251 = to_ov7251(sd);
	const struct ov7251_mode *mode = ov7251->cur_mode;

	mutex_lock(&ov7251->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov7251->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_Y10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ov7251->mutex);

	return 0;
}

static int ov7251_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_Y10_1X10;

	return 0;
}

static int ov7251_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_Y10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov7251_enable_test_pattern(struct ov7251 *ov7251, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV7251_TEST_PATTERN_ENABLE;
	else
		val = OV7251_TEST_PATTERN_DISABLE;

	return ov7251_write_reg(ov7251->client, OV7251_REG_TEST_PATTERN,
				OV7251_REG_VALUE_08BIT, val);
}

static int ov7251_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct ov7251 *ov7251 = to_ov7251(sd);
	const struct ov7251_mode *mode = ov7251->cur_mode;

	mutex_lock(&ov7251->mutex);
	fi->interval.numerator = 10000;
	fi->interval.denominator = mode->max_fps * 10000;
	mutex_unlock(&ov7251->mutex);

	return 0;
}

static int __ov7251_start_stream(struct ov7251 *ov7251)
{
	int ret;

	ret = ov7251_write_array(ov7251->client, ov7251->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&ov7251->mutex);
	ret = v4l2_ctrl_handler_setup(&ov7251->ctrl_handler);
	mutex_lock(&ov7251->mutex);
	if (ret)
		return ret;

	return ov7251_write_reg(ov7251->client, OV7251_REG_CTRL_MODE,
				OV7251_REG_VALUE_08BIT, OV7251_MODE_STREAMING);
}

static int __ov7251_stop_stream(struct ov7251 *ov7251)
{
	return ov7251_write_reg(ov7251->client, OV7251_REG_CTRL_MODE,
				OV7251_REG_VALUE_08BIT, OV7251_MODE_SW_STANDBY);
}

static int ov7251_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov7251 *ov7251 = to_ov7251(sd);
	struct i2c_client *client = ov7251->client;
	int ret = 0;

	mutex_lock(&ov7251->mutex);
	on = !!on;
	if (on == ov7251->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		ret = __ov7251_start_stream(ov7251);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
		usleep_range(10 * 1000, 12 * 1000);
	} else {
		__ov7251_stop_stream(ov7251);
		pm_runtime_put(&client->dev);
	}

	ov7251->streaming = on;

unlock_and_return:
	mutex_unlock(&ov7251->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov7251_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV7251_XVCLK_FREQ / 1000 / 1000);
}

static int __ov7251_power_on(struct ov7251 *ov7251)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov7251->client->dev;

	ret = clk_prepare_enable(ov7251->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(ov7251->reset_gpio))
		gpiod_set_value_cansleep(ov7251->reset_gpio, 1);

	ret = regulator_bulk_enable(OV7251_NUM_SUPPLIES, ov7251->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	usleep_range(1000, 1100);

	if (!IS_ERR(ov7251->reset_gpio))
		gpiod_set_value_cansleep(ov7251->reset_gpio, 0);

	if (!IS_ERR(ov7251->pwdn_gpio))
		gpiod_set_value_cansleep(ov7251->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov7251_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov7251->xvclk);

	return ret;
}

static void __ov7251_power_off(struct ov7251 *ov7251)
{
	if (!IS_ERR(ov7251->pwdn_gpio))
		gpiod_set_value_cansleep(ov7251->pwdn_gpio, 0);
	clk_disable_unprepare(ov7251->xvclk);
	if (!IS_ERR(ov7251->reset_gpio))
		gpiod_set_value_cansleep(ov7251->reset_gpio, 1);
	regulator_bulk_disable(OV7251_NUM_SUPPLIES, ov7251->supplies);
}

static int ov7251_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov7251 *ov7251 = to_ov7251(sd);

	return __ov7251_power_on(ov7251);
}

static int ov7251_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov7251 *ov7251 = to_ov7251(sd);

	__ov7251_power_off(ov7251);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov7251_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov7251 *ov7251 = to_ov7251(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov7251_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov7251->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_Y10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov7251->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static const struct dev_pm_ops ov7251_pm_ops = {
	SET_RUNTIME_PM_OPS(ov7251_runtime_suspend,
			   ov7251_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov7251_internal_ops = {
	.open = ov7251_open,
};
#endif

static const struct v4l2_subdev_video_ops ov7251_video_ops = {
	.s_stream = ov7251_s_stream,
	.g_frame_interval = ov7251_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ov7251_pad_ops = {
	.enum_mbus_code = ov7251_enum_mbus_code,
	.enum_frame_size = ov7251_enum_frame_sizes,
	.get_fmt = ov7251_get_fmt,
	.set_fmt = ov7251_set_fmt,
};

static const struct v4l2_subdev_ops ov7251_subdev_ops = {
	.video	= &ov7251_video_ops,
	.pad	= &ov7251_pad_ops,
};

static int ov7251_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov7251 *ov7251 = container_of(ctrl->handler,
					     struct ov7251, ctrl_handler);
	struct i2c_client *client = ov7251->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov7251->cur_mode->height + ctrl->val - 20;
		__v4l2_ctrl_modify_range(ov7251->exposure,
					 ov7251->exposure->minimum, max,
					 ov7251->exposure->step,
					 ov7251->exposure->default_value);
		break;
	}

	if (pm_runtime_get(&client->dev) <= 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = ov7251_write_reg(ov7251->client, OV7251_REG_EXPOSURE,
				       OV7251_REG_VALUE_24BIT, ctrl->val << 4);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov7251_write_reg(ov7251->client, OV7251_REG_ANALOG_GAIN,
				       OV7251_REG_VALUE_16BIT,
				       ctrl->val & ANALOG_GAIN_MASK);
		break;
	case V4L2_CID_VBLANK:
		ret = ov7251_write_reg(ov7251->client, OV7251_REG_VTS,
				       OV7251_REG_VALUE_16BIT,
				       ctrl->val + ov7251->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov7251_enable_test_pattern(ov7251, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov7251_ctrl_ops = {
	.s_ctrl = ov7251_set_ctrl,
};

static int ov7251_initialize_controls(struct ov7251 *ov7251)
{
	const struct ov7251_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &ov7251->ctrl_handler;
	mode = ov7251->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &ov7251->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, OV7251_PIXEL_RATE, 1, OV7251_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	ov7251->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov7251->hblank)
		ov7251->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov7251->vblank = v4l2_ctrl_new_std(handler, &ov7251_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV7251_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 20;
	ov7251->exposure = v4l2_ctrl_new_std(handler, &ov7251_ctrl_ops,
				V4L2_CID_EXPOSURE, OV7251_EXPOSURE_MIN,
				exposure_max, OV7251_EXPOSURE_STEP,
				mode->exp_def);

	ov7251->anal_gain = v4l2_ctrl_new_std(handler, &ov7251_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	ov7251->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov7251_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov7251_test_pattern_menu) - 1,
				0, 0, ov7251_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov7251->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov7251->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov7251_check_sensor_id(struct ov7251 *ov7251,
				  struct i2c_client *client)
{
	struct device *dev = &ov7251->client->dev;
	u32 id = 0;
	int ret;

	ret = ov7251_read_reg(client, OV7251_REG_CHIP_ID,
			      OV7251_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return ret;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int ov7251_configure_regulators(struct ov7251 *ov7251)
{
	size_t i;

	for (i = 0; i < OV7251_NUM_SUPPLIES; i++)
		ov7251->supplies[i].supply = ov7251_supply_names[i];

	return devm_regulator_bulk_get(&ov7251->client->dev,
				       OV7251_NUM_SUPPLIES,
				       ov7251->supplies);
}

static int ov7251_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ov7251 *ov7251;
	struct v4l2_subdev *sd;
	int ret;

	ov7251 = devm_kzalloc(dev, sizeof(*ov7251), GFP_KERNEL);
	if (!ov7251)
		return -ENOMEM;

	ov7251->client = client;
	ov7251->cur_mode = &supported_modes[0];

	ov7251->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov7251->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}
	ret = clk_set_rate(ov7251->xvclk, OV7251_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(ov7251->xvclk) != OV7251_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	ov7251->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov7251->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov7251->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov7251->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov7251_configure_regulators(ov7251);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&ov7251->mutex);

	sd = &ov7251->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov7251_subdev_ops);
	ret = ov7251_initialize_controls(ov7251);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov7251_power_on(ov7251);
	if (ret)
		goto err_free_handler;

	ret = ov7251_check_sensor_id(ov7251, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov7251_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov7251->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&sd->entity, 1, &ov7251->pad, 0);
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
	__ov7251_power_off(ov7251);
err_free_handler:
	v4l2_ctrl_handler_free(&ov7251->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov7251->mutex);

	return ret;
}

static int ov7251_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov7251 *ov7251 = to_ov7251(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov7251->ctrl_handler);
	mutex_destroy(&ov7251->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov7251_power_off(ov7251);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov7251_of_match[] = {
	{ .compatible = "ovti,ov7251" },
	{},
};
MODULE_DEVICE_TABLE(of, ov7251_of_match);
#endif

static const struct i2c_device_id ov7251_match_id[] = {
	{ "ovti,ov7251", 0 },
	{ },
};

static struct i2c_driver ov7251_i2c_driver = {
	.driver = {
		.name = "ov7251",
		.pm = &ov7251_pm_ops,
		.of_match_table = of_match_ptr(ov7251_of_match),
	},
	.probe		= &ov7251_probe,
	.remove		= &ov7251_remove,
	.id_table	= ov7251_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov7251_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov7251_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov7251 sensor driver");
MODULE_LICENSE("GPL v2");
