// SPDX-License-Identifier: GPL-2.0
/*
 * sc2239 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 add quick stream support.
 */
//#define DEBUG
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/rk-camera-module.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC2239_LANES			1
#define SC2239_BITS_PER_SAMPLE		10
#define SC2239_LINK_FREQ		371250000	// 742.5Mbps

#define SC2239_PIXEL_RATE		(SC2239_LINK_FREQ * 2 * \
					SC2239_LANES / SC2239_BITS_PER_SAMPLE)

#define SC2239_XVCLK_FREQ		24000000

#define CHIP_ID				0xcb10
#define SC2239_REG_CHIP_ID		0x3107

#define SC2239_REG_CTRL_MODE		0x0100
#define SC2239_MODE_SW_STANDBY		0x0
#define SC2239_MODE_STREAMING		BIT(0)

#define SC2239_REG_EXPOSURE		0x3e00
#define	SC2239_EXPOSURE_MIN		1
#define	SC2239_EXPOSURE_STEP		1
#define SC2239_VTS_MAX			0xffff

#define SC2239_REG_COARSE_AGAIN		0x3e08
#define SC2239_REG_FINE_AGAIN		0x3e09
#define	ANALOG_GAIN_MIN			0x20
#define	ANALOG_GAIN_MAX			0x1F8
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x40

#define SC4238_GROUP_UPDATE_ADDRESS	0x3812
#define SC4238_GROUP_UPDATE_START_DATA	0x00
#define SC4238_GROUP_UPDATE_END_DATA	0x30

#define SC2239_REG_TEST_PATTERN		0x4501
#define SC2239_TEST_PATTERN_BIT_MASK	BIT(3)

#define SC2239_REG_VTS			0x320e

#define REG_NULL			0xFFFF
#define DELAY_MS			0xEEEE	/* Array delay token */

#define SC2239_REG_VALUE_08BIT		1
#define SC2239_REG_VALUE_16BIT		2
#define SC2239_REG_VALUE_24BIT		3

#define PIX_FORMAT MEDIA_BUS_FMT_SBGGR10_1X10

#define SC2239_NAME			"sc2239"

static const char * const sc2239_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC2239_NUM_SUPPLIES ARRAY_SIZE(sc2239_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc2239_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct sc2239 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC2239_NUM_SUPPLIES];
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
	u32			cur_vts;
	bool			streaming;
	bool			power_on;
	const struct sc2239_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			old_gain;
};

#define to_sc2239(sd) container_of(sd, struct sc2239, subdev)

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 742.5Mbps, 1 lane
 */
static const struct regval sc2239_global_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x301f, 0x34},
	{0x3038, 0x44},
	{0x3253, 0x12},
	{0x3301, 0x05},
	{0x3304, 0xa8},
	{0x3306, 0x68},
	{0x3308, 0x10},
	{0x3309, 0x48},
	{0x330a, 0x01},
	{0x330b, 0x40},
	{0x331e, 0xa1},
	{0x331f, 0x41},
	{0x3333, 0x10},
	{0x3364, 0x17},
	{0x3390, 0x08},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x08},
	{0x3394, 0x0d},
	{0x3395, 0x70},
	{0x33af, 0x20},
	{0x360f, 0x01},
	{0x3630, 0x00},
	{0x3634, 0x64},
	{0x3637, 0x10},
	{0x363c, 0x05},
	{0x3670, 0x0c},
	{0x3671, 0xc2},
	{0x3672, 0x02},
	{0x3673, 0x02},
	{0x3677, 0x84},
	{0x3678, 0x84},
	{0x3679, 0x8e},
	{0x367a, 0x18},
	{0x367b, 0x38},
	{0x367e, 0x08},
	{0x367f, 0x38},
	{0x3690, 0x64},
	{0x3691, 0x64},
	{0x3692, 0x64},
	{0x369c, 0x08},
	{0x369d, 0x18},
	{0x36ea, 0x75},
	{0x36ed, 0x24},
	{0x36fa, 0x75},
	{0x36fb, 0x00},
	{0x36fc, 0x10},
	{0x36fd, 0x34},
	{0x3904, 0x08},
	{0x3908, 0x82},
	{0x3933, 0x82},
	{0x3934, 0x1b},
	{0x3940, 0x77},
	{0x3941, 0x18},
	{0x3942, 0x02},
	{0x3943, 0x1c},
	{0x3944, 0x0b},
	{0x3945, 0x80},
	{0x3e01, 0x8c},
	{0x3e02, 0x20},
	{0x4509, 0x20},
	{0x4800, 0x64},
	{0x4819, 0x09},
	{0x481b, 0x05},
	{0x481d, 0x14},
	{0x4821, 0x0a},
	{0x4823, 0x05},
	{0x5000, 0x06},
	{0x5780, 0x7f},
	{0x5781, 0x04},
	{0x5782, 0x03},
	{0x5783, 0x02},
	{0x5784, 0x01},
	{0x5785, 0x18},
	{0x5786, 0x10},
	{0x5787, 0x08},
	{0x5788, 0x02},
	{0x5789, 0x20},
	{0x578a, 0x7f},
	{0x36e9, 0x29},
	{0x36f9, 0x29},
	{DELAY_MS, 0x0a},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 742.5Mbps, 1lane
 */
static const struct regval sc2239_1920x1080_regs_1lane[] = {
	{REG_NULL, 0x00},
};

static const struct sc2239_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0400,
		.hts_def = 0x44C * 2,
		.vts_def = 0x0465,
		.reg_list = sc2239_1920x1080_regs_1lane,
	},
};

static const char * const sc2239_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

static const s64 link_freq_menu_items[] = {
	SC2239_LINK_FREQ
};

/* Write registers up to 4 at a time */
static int sc2239_write_reg(struct i2c_client *client,
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

static int sc2239_write_array(struct i2c_client *client,
	const struct regval *regs)
{
	u32 i;
	int delay_ms = 0, ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (regs[i].addr == DELAY_MS) {
			delay_ms = regs[i].val;
			dev_info(&client->dev, "delay(%d) ms !\n", delay_ms);
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
			i++;
			continue;
		}
		ret = sc2239_write_reg(client, regs[i].addr,
				       SC2239_REG_VALUE_08BIT, regs[i].val);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int sc2239_read_reg(struct i2c_client *client,
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

static int sc2239_get_reso_dist(const struct sc2239_mode *mode,
	struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc2239_mode *
sc2239_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc2239_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int sc2239_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc2239 *sc2239 = to_sc2239(sd);
	const struct sc2239_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc2239->mutex);

	mode = sc2239_find_best_fit(fmt);
	fmt->format.code = PIX_FORMAT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc2239->mutex);
		return -ENOTTY;
#endif
	} else {
		sc2239->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc2239->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc2239->vblank, vblank_def,
					 SC2239_VTS_MAX - mode->height,
					 1, vblank_def);
		sc2239->cur_fps = mode->max_fps;
		sc2239->cur_vts = mode->vts_def;
	}

	mutex_unlock(&sc2239->mutex);

	return 0;
}

static int sc2239_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc2239 *sc2239 = to_sc2239(sd);
	const struct sc2239_mode *mode = sc2239->cur_mode;

	mutex_lock(&sc2239->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc2239->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = PIX_FORMAT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&sc2239->mutex);

	return 0;
}

static int sc2239_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = PIX_FORMAT;

	return 0;
}

static int sc2239_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != PIX_FORMAT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int sc2239_enable_test_pattern(struct sc2239 *sc2239, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = sc2239_read_reg(sc2239->client, SC2239_REG_TEST_PATTERN,
			       SC2239_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= SC2239_TEST_PATTERN_BIT_MASK;
	else
		val &= ~SC2239_TEST_PATTERN_BIT_MASK;

	ret |= sc2239_write_reg(sc2239->client, SC2239_REG_TEST_PATTERN,
				SC2239_REG_VALUE_08BIT, val);
	return ret;
}

static void sc2239_get_module_inf(struct sc2239 *sc2239,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, SC2239_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, sc2239->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, sc2239->len_name, sizeof(inf->base.lens));
}

static long sc2239_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc2239 *sc2239 = to_sc2239(sd);
	long ret = 0;
	u32  stream;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc2239_get_module_inf(sc2239, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);

		if (stream)
			ret = sc2239_write_reg(sc2239->client, SC2239_REG_CTRL_MODE,
				SC2239_REG_VALUE_08BIT, SC2239_MODE_STREAMING);
		else
			ret = sc2239_write_reg(sc2239->client, SC2239_REG_CTRL_MODE,
				SC2239_REG_VALUE_08BIT, SC2239_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc2239_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	u32  stream;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc2239_ioctl(sd, cmd, inf);
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
			ret = sc2239_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = sc2239_ioctl(sd, cmd, &stream);
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

static int sc2239_set_ctrl_gain(struct sc2239 *sc2239, u32 a_gain)
{
	int ret = 0;
	u32 coarse_again, fine_again, fine_again_reg, coarse_again_reg;
	u32 switch_value;

	if ( a_gain != sc2239->old_gain) {
		if (a_gain < 0x40) { /*1x ~ 2x*/
			fine_again = a_gain - 32;
			coarse_again = 0x03;
			fine_again_reg = ((0x01 << 5) & 0x20) |
				(fine_again & 0x1f);
			coarse_again_reg = coarse_again  & 0x1F;
			switch_value = 0x64;
		} else if (a_gain < 0x80) { /*2x ~ 4x*/
			fine_again = (a_gain >> 1) - 32;
			coarse_again = 0x7;
			fine_again_reg = ((0x01 << 5) & 0x20) |
				(fine_again & 0x1f);
			coarse_again_reg = coarse_again  & 0x1F;
			switch_value = 0x64;
		} else if (a_gain < 0x100) { /*4x ~ 8x*/
			fine_again = (a_gain >> 2) - 32;
			coarse_again = 0xf;
			fine_again_reg = ((0x01 << 5) & 0x20) |
				(fine_again & 0x1f);
			coarse_again_reg = coarse_again  & 0x1F;
			switch_value = 0x44;
		} else { /*8x ~ 16x*/
			fine_again = (a_gain >> 3) - 32;
			coarse_again = 0x1f;
			fine_again_reg = ((0x01 << 5) & 0x20) |
				(fine_again & 0x1f);
			coarse_again_reg = coarse_again  & 0x1F;
			switch_value = 0x24;
		}

		ret = sc2239_write_reg(sc2239->client, 0x3634,
			SC2239_REG_VALUE_08BIT, switch_value);
		ret |= sc2239_write_reg(sc2239->client,
			SC2239_REG_COARSE_AGAIN,
			SC2239_REG_VALUE_08BIT,
			coarse_again_reg);
		ret |= sc2239_write_reg(sc2239->client,
			SC2239_REG_FINE_AGAIN,
			SC2239_REG_VALUE_08BIT,
			fine_again_reg);
		sc2239->old_gain = a_gain;
	}
	return ret;
}

static int __sc2239_start_stream(struct sc2239 *sc2239)
{
	int ret;

	ret = sc2239_write_array(sc2239->client, sc2239->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&sc2239->mutex);
	ret = v4l2_ctrl_handler_setup(&sc2239->ctrl_handler);
	mutex_lock(&sc2239->mutex);
	if (ret)
		return ret;

	return sc2239_write_reg(sc2239->client, SC2239_REG_CTRL_MODE,
			SC2239_REG_VALUE_08BIT, SC2239_MODE_STREAMING);
}

static int __sc2239_stop_stream(struct sc2239 *sc2239)
{
	return sc2239_write_reg(sc2239->client, SC2239_REG_CTRL_MODE,
			SC2239_REG_VALUE_08BIT, SC2239_MODE_SW_STANDBY);
}

static int sc2239_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc2239 *sc2239 = to_sc2239(sd);
	struct i2c_client *client = sc2239->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				sc2239->cur_mode->width,
				sc2239->cur_mode->height,
		DIV_ROUND_CLOSEST(sc2239->cur_mode->max_fps.denominator,
				  sc2239->cur_mode->max_fps.numerator));

	mutex_lock(&sc2239->mutex);
	on = !!on;
	if (on == sc2239->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc2239_start_stream(sc2239);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc2239_stop_stream(sc2239);
		pm_runtime_put(&client->dev);
	}

	sc2239->streaming = on;

unlock_and_return:
	mutex_unlock(&sc2239->mutex);

	return ret;
}

static int sc2239_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct sc2239 *sc2239 = to_sc2239(sd);
	const struct sc2239_mode *mode = sc2239->cur_mode;

	if (sc2239->streaming)
		fi->interval = sc2239->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int sc2239_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc2239 *sc2239 = to_sc2239(sd);
	struct i2c_client *client = sc2239->client;
	int ret = 0;

	mutex_lock(&sc2239->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc2239->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		ret = sc2239_write_array(sc2239->client, sc2239_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		sc2239->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc2239->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc2239->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 sc2239_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC2239_XVCLK_FREQ / 1000 / 1000);
}

static int __sc2239_power_on(struct sc2239 *sc2239)
{
	int ret;
	u32 delay_us;
	struct device *dev = &sc2239->client->dev;

	ret = clk_set_rate(sc2239->xvclk, SC2239_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc2239->xvclk) != SC2239_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc2239->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(sc2239->reset_gpio))
		gpiod_set_value_cansleep(sc2239->reset_gpio, 0);

	ret = regulator_bulk_enable(SC2239_NUM_SUPPLIES, sc2239->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc2239->reset_gpio))
		gpiod_set_value_cansleep(sc2239->reset_gpio, 1);

	if (!IS_ERR(sc2239->pwdn_gpio))
		gpiod_set_value_cansleep(sc2239->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = sc2239_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(sc2239->xvclk);

	return ret;
}

static void __sc2239_power_off(struct sc2239 *sc2239)
{
	if (!IS_ERR(sc2239->pwdn_gpio))
		gpiod_set_value_cansleep(sc2239->pwdn_gpio, 0);
	clk_disable_unprepare(sc2239->xvclk);
	if (!IS_ERR(sc2239->reset_gpio))
		gpiod_set_value_cansleep(sc2239->reset_gpio, 0);

	regulator_bulk_disable(SC2239_NUM_SUPPLIES, sc2239->supplies);
}

static int sc2239_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc2239 *sc2239 = to_sc2239(sd);

	return __sc2239_power_on(sc2239);
}

static int sc2239_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc2239 *sc2239 = to_sc2239(sd);

	__sc2239_power_off(sc2239);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc2239_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc2239 *sc2239 = to_sc2239(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc2239_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc2239->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = PIX_FORMAT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc2239->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc2239_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *config)
{
	u32 val = 1 << (SC2239_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static int sc2239_enum_frame_interval(struct v4l2_subdev *sd,
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
	return 0;
}

static const struct dev_pm_ops sc2239_pm_ops = {
	SET_RUNTIME_PM_OPS(sc2239_runtime_suspend,
			   sc2239_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc2239_internal_ops = {
	.open = sc2239_open,
};
#endif

static const struct v4l2_subdev_core_ops sc2239_core_ops = {
	.s_power = sc2239_s_power,
	.ioctl = sc2239_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc2239_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc2239_video_ops = {
	.s_stream = sc2239_s_stream,
	.g_frame_interval = sc2239_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc2239_pad_ops = {
	.enum_mbus_code = sc2239_enum_mbus_code,
	.enum_frame_size = sc2239_enum_frame_sizes,
	.enum_frame_interval = sc2239_enum_frame_interval,
	.get_fmt = sc2239_get_fmt,
	.set_fmt = sc2239_set_fmt,
	.get_mbus_config = sc2239_g_mbus_config,
};

static const struct v4l2_subdev_ops sc2239_subdev_ops = {
	.core	= &sc2239_core_ops,
	.video	= &sc2239_video_ops,
	.pad	= &sc2239_pad_ops,
};

static void sc2239_modify_fps_info(struct sc2239 *sc2239)
{
	const struct sc2239_mode *mode = sc2239->cur_mode;

	sc2239->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
				      sc2239->cur_vts;
}

static int sc2239_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc2239 *sc2239 = container_of(ctrl->handler,
					     struct sc2239, ctrl_handler);
	struct i2c_client *client = sc2239->client;
	s64 max;
	int ret = 0;

	dev_dbg(&client->dev, "ctrl->id(0x%x) val 0x%x\n",
		ctrl->id, ctrl->val);
	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc2239->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(sc2239->exposure,
					 sc2239->exposure->minimum, max,
					 sc2239->exposure->step,
					 sc2239->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = sc2239_write_reg(sc2239->client, SC2239_REG_EXPOSURE,
				       SC2239_REG_VALUE_24BIT, ctrl->val << 5);
		dev_dbg(&client->dev, "set exposure 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = sc2239_set_ctrl_gain(sc2239, ctrl->val);
		dev_dbg(&client->dev, "set analog gain 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = sc2239_write_reg(sc2239->client, SC2239_REG_VTS,
				       SC2239_REG_VALUE_16BIT,
				       ctrl->val + sc2239->cur_mode->height);
		if (!ret)
			sc2239->cur_vts = ctrl->val + sc2239->cur_mode->height;
		if (sc2239->cur_vts != sc2239->cur_mode->vts_def)
			sc2239_modify_fps_info(sc2239);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc2239_enable_test_pattern(sc2239, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc2239_ctrl_ops = {
	.s_ctrl = sc2239_set_ctrl,
};

static int sc2239_initialize_controls(struct sc2239 *sc2239)
{
	const struct sc2239_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &sc2239->ctrl_handler;
	mode = sc2239->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &sc2239->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, SC2239_PIXEL_RATE, 1, SC2239_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	sc2239->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (sc2239->hblank)
		sc2239->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	sc2239->vblank = v4l2_ctrl_new_std(handler, &sc2239_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				SC2239_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	sc2239->exposure = v4l2_ctrl_new_std(handler, &sc2239_ctrl_ops,
				V4L2_CID_EXPOSURE, SC2239_EXPOSURE_MIN,
				exposure_max, SC2239_EXPOSURE_STEP,
				mode->exp_def);

	sc2239->anal_gain = v4l2_ctrl_new_std(handler, &sc2239_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	sc2239->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&sc2239_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(sc2239_test_pattern_menu) - 1,
				0, 0, sc2239_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc2239->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc2239->subdev.ctrl_handler = handler;
	sc2239->old_gain = ANALOG_GAIN_DEFAULT;
	sc2239->cur_fps = mode->max_fps;
	sc2239->cur_vts = mode->vts_def;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc2239_check_sensor_id(struct sc2239 *sc2239,
				  struct i2c_client *client)
{
	struct device *dev = &sc2239->client->dev;
	u32 id = 0;
	int ret;

	ret = sc2239_read_reg(client, SC2239_REG_CHIP_ID,
			      SC2239_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC2239 CHIP ID = 0x%04x sensor\n", CHIP_ID);

	return 0;
}

static int sc2239_configure_regulators(struct sc2239 *sc2239)
{
	unsigned int i;

	for (i = 0; i < SC2239_NUM_SUPPLIES; i++)
		sc2239->supplies[i].supply = sc2239_supply_names[i];

	return devm_regulator_bulk_get(&sc2239->client->dev,
				       SC2239_NUM_SUPPLIES,
				       sc2239->supplies);
}

static int sc2239_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc2239 *sc2239;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	sc2239 = devm_kzalloc(dev, sizeof(*sc2239), GFP_KERNEL);
	if (!sc2239)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc2239->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc2239->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc2239->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc2239->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc2239->client = client;
	sc2239->cur_mode = &supported_modes[0];

	sc2239->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc2239->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc2239->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sc2239->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc2239->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc2239->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");
	ret = sc2239_configure_regulators(sc2239);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc2239->mutex);

	sd = &sc2239->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc2239_subdev_ops);
	ret = sc2239_initialize_controls(sc2239);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc2239_power_on(sc2239);
	if (ret)
		goto err_free_handler;

	ret = sc2239_check_sensor_id(sc2239, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc2239_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc2239->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc2239->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc2239->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc2239->module_index, facing,
		 SC2239_NAME, dev_name(sd->dev));
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
	__sc2239_power_off(sc2239);
err_free_handler:
	v4l2_ctrl_handler_free(&sc2239->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc2239->mutex);

	return ret;
}

static int sc2239_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc2239 *sc2239 = to_sc2239(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc2239->ctrl_handler);
	mutex_destroy(&sc2239->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc2239_power_off(sc2239);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc2239_of_match[] = {
	{ .compatible = "smartsens,sc2239" },
	{},
};
MODULE_DEVICE_TABLE(of, sc2239_of_match);
#endif

static const struct i2c_device_id sc2239_match_id[] = {
	{ "smartsens,sc2239", 0 },
	{ },
};

static struct i2c_driver sc2239_i2c_driver = {
	.driver = {
		.name = SC2239_NAME,
		.pm = &sc2239_pm_ops,
		.of_match_table = of_match_ptr(sc2239_of_match),
	},
	.probe		= &sc2239_probe,
	.remove		= &sc2239_remove,
	.id_table	= sc2239_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc2239_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc2239_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Smartsens sc2239 sensor driver");
MODULE_AUTHOR("zack.zeng");
MODULE_LICENSE("GPL v2");
