// SPDX-License-Identifier: GPL-2.0
/*
 * sc132gs driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
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

#define SC132GS_PIXEL_RATE		(72 * 1000 * 1000)
#define SC132GS_XVCLK_FREQ		24000000

#define CHIP_ID				0x0132
#define SC132GS_REG_CHIP_ID		0x3107

#define SC132GS_REG_CTRL_MODE		0x0100
#define SC132GS_MODE_SW_STANDBY		0x0
#define SC132GS_MODE_STREAMING		BIT(0)

#define SC132GS_REG_EXPOSURE		0x3e01
#define	SC132GS_EXPOSURE_MIN		6
#define	SC132GS_EXPOSURE_STEP		1
#define SC132GS_VTS_MAX			0xffff

#define SC132GS_REG_COARSE_AGAIN	0x3e08
#define SC132GS_REG_FINE_AGAIN		0x3e09
#define	ANALOG_GAIN_MIN			0x20
#define	ANALOG_GAIN_MAX			0x391
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x20

#define SC132GS_REG_TEST_PATTERN	0x4501
#define	SC132GS_TEST_PATTERN_ENABLE	0xcc
#define	SC132GS_TEST_PATTERN_DISABLE	0xc4

#define SC132GS_REG_VTS			0x320e

#define REG_NULL			0xFFFF

#define SC132GS_REG_VALUE_08BIT		1
#define SC132GS_REG_VALUE_16BIT		2
#define SC132GS_REG_VALUE_24BIT		3

#define SC132GS_NAME			"sc132gs"

#define PIX_FORMAT MEDIA_BUS_FMT_Y8_1X8

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define SC132GS_LANES			1
#define SC132GS_BITS_PER_SAMPLE		8

static const char * const sc132gs_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC132GS_NUM_SUPPLIES ARRAY_SIZE(sc132gs_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc132gs_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct sc132gs {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC132GS_NUM_SUPPLIES];
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
	const struct sc132gs_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_sc132gs(sd) container_of(sd, struct sc132gs, subdev)

/*
 * Xclk 24Mhz
 * Pclk 72Mhz
 * linelength 1696(0x06a0)
 * framelength 2122(0x084a)
 * grabwindow_width 1080
 * grabwindow_height 1280
 * mipi 1 lane
 * max_framerate 30fps
 * mipi_datarate per lane 720Mbps
 */
static const struct regval sc132gs_global_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},

	//PLL bypass
	{0x36e9, 0x80},
	{0x36f9, 0x80},

	{0x3018, 0x12},
	{0x3019, 0x0e},
	{0x301a, 0xb4},
	{0x3031, 0x08},
	{0x3032, 0x60},
	{0x3038, 0x44},
	{0x3207, 0x17},
	{0x320c, 0x06},
	{0x320d, 0xa0},
	{0x320e, 0x08},
	{0x320f, 0x4a},
	{0x3250, 0xcc},
	{0x3251, 0x02},
	{0x3252, 0x08},
	{0x3253, 0x45},
	{0x3254, 0x05},
	{0x3255, 0x3b},
	{0x3306, 0x78},
	{0x330a, 0x00},
	{0x330b, 0xc8},
	{0x330f, 0x24},
	{0x3314, 0x80},
	{0x3315, 0x40},
	{0x3317, 0xf0},
	{0x331f, 0x12},
	{0x3364, 0x00},
	{0x3385, 0x41},
	{0x3387, 0x41},
	{0x3389, 0x09},
	{0x33ab, 0x00},
	{0x33ac, 0x00},
	{0x33b1, 0x03},
	{0x33b2, 0x12},
	{0x33f8, 0x02},
	{0x33fa, 0x01},
	{0x3409, 0x08},
	{0x34f0, 0xc0},
	{0x34f1, 0x20},
	{0x34f2, 0x03},
	{0x3622, 0xf5},
	{0x3630, 0x5c},
	{0x3631, 0x80},
	{0x3632, 0xc8},
	{0x3633, 0x32},
	{0x3638, 0x2a},
	{0x3639, 0x07},
	{0x363b, 0x48},
	{0x363c, 0x83},
	{0x363d, 0x10},
	{0x36ea, 0x3a},
	{0x36fa, 0x25},
	{0x36fb, 0x05},
	{0x36fd, 0x04},
	{0x3900, 0x11},
	{0x3901, 0x05},
	{0x3902, 0xc5},
	{0x3904, 0x04},
	{0x3908, 0x91},
	{0x391e, 0x00},
	{0x3e01, 0x53},
	{0x3e02, 0xe0},
	{0x3e09, 0x20},
	{0x3e0e, 0xd2},
	{0x3e14, 0xb0},
	{0x3e1e, 0x7c},
	{0x3e26, 0x20},
	{0x4418, 0x38},
	{0x4503, 0x10},
	{0x4837, 0x14},
	{0x5000, 0x0e},
	{0x540c, 0x51},
	{0x550f, 0x38},
	{0x5780, 0x67},
	{0x5784, 0x10},
	{0x5785, 0x06},
	{0x5787, 0x02},
	{0x5788, 0x00},
	{0x5789, 0x00},
	{0x578a, 0x02},
	{0x578b, 0x00},
	{0x578c, 0x00},
	{0x5790, 0x00},
	{0x5791, 0x00},
	{0x5792, 0x00},
	{0x5793, 0x00},
	{0x5794, 0x00},
	{0x5795, 0x00},
	{0x5799, 0x04},

	{0x3037, 0x00},

	//PLL set
	{0x36e9, 0x24},
	{0x36f9, 0x24},

	{0x0100, 0x01},
	{REG_NULL, 0x00},
};

static const struct sc132gs_mode supported_modes[] = {
	{
		.width = 1080,
		.height = 1280,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0148,
		.hts_def = 0x06a0,
		.vts_def = 0x084a,
		.reg_list = sc132gs_global_regs,
	},
};

static const char * const sc132gs_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

#define SC132GS_LINK_FREQ_360MHZ	(360 * 1000 * 1000)

static const s64 link_freq_menu_items[] = {
	SC132GS_LINK_FREQ_360MHZ
};

/* Write registers up to 4 at a time */
static int sc132gs_write_reg(struct i2c_client *client,
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

static int sc132gs_write_array(struct i2c_client *client,
	const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = sc132gs_write_reg(client, regs[i].addr,
					SC132GS_REG_VALUE_08BIT, regs[i].val);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int sc132gs_read_reg(struct i2c_client *client,
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

static int sc132gs_get_reso_dist(const struct sc132gs_mode *mode,
	struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc132gs_mode *
	sc132gs_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc132gs_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}
	return &supported_modes[cur_best_fit];
}

static int sc132gs_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc132gs *sc132gs = to_sc132gs(sd);
	const struct sc132gs_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc132gs->mutex);

	mode = sc132gs_find_best_fit(fmt);
	fmt->format.code = PIX_FORMAT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc132gs->mutex);
		return -ENOTTY;
#endif
	} else {
		sc132gs->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc132gs->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc132gs->vblank, vblank_def,
					 SC132GS_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&sc132gs->mutex);

	return 0;
}

static int sc132gs_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc132gs *sc132gs = to_sc132gs(sd);
	const struct sc132gs_mode *mode = sc132gs->cur_mode;

	mutex_lock(&sc132gs->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc132gs->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = PIX_FORMAT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&sc132gs->mutex);

	return 0;
}

static int sc132gs_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = PIX_FORMAT;

	return 0;
}

static int sc132gs_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int sc132gs_enable_test_pattern(struct sc132gs *sc132gs, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | SC132GS_TEST_PATTERN_ENABLE;
	else
		val = SC132GS_TEST_PATTERN_DISABLE;

	return sc132gs_write_reg(sc132gs->client, SC132GS_REG_TEST_PATTERN,
				 SC132GS_REG_VALUE_08BIT, val);
}

static void sc132gs_get_module_inf(struct sc132gs *sc132gs,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, SC132GS_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, sc132gs->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, sc132gs->len_name, sizeof(inf->base.lens));
}

static long sc132gs_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc132gs *sc132gs = to_sc132gs(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc132gs_get_module_inf(sc132gs, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc132gs_write_reg(sc132gs->client, SC132GS_REG_CTRL_MODE,
				SC132GS_REG_VALUE_08BIT, SC132GS_MODE_STREAMING);
		else
			ret = sc132gs_write_reg(sc132gs->client, SC132GS_REG_CTRL_MODE,
				SC132GS_REG_VALUE_08BIT, SC132GS_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc132gs_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc132gs_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = sc132gs_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int sc132gs_set_ctrl_gain(struct sc132gs *sc132gs, u32 a_gain)
{
	int ret = 0;
	u32 coarse_again, fine_again, fine_again_reg, coarse_again_reg;

	if (a_gain < 0x20)
		a_gain = 0x20;
	if (a_gain > 0x391)
		a_gain = 0x391;

	if (a_gain < 0x3a) {/*1x~1.813*/
		fine_again = a_gain;
		coarse_again = 0x03;
		fine_again_reg = fine_again & 0x3f;
		coarse_again_reg = coarse_again & 0x3F;
		if (fine_again_reg >= 0x39)
			fine_again_reg = 0x39;
	} else if (a_gain < 0x72) {/*1.813~3.568x*/
		fine_again = (a_gain - 0x3a) * 1000 / 1755 + 0x20;
		coarse_again = 0x23;
		if (fine_again > 0x3f)
			fine_again = 0x3f;
		fine_again_reg = fine_again & 0x3f;
		coarse_again_reg = coarse_again & 0x3F;
	} else if (a_gain < 0xe8) { /*3.568x~7.250x*/
		fine_again = (a_gain - 0x72) * 1000 / 3682 + 0x20;
		coarse_again = 0x27;
		if (fine_again > 0x3f)
			fine_again = 0x3f;
		fine_again_reg = fine_again & 0x3f;
		coarse_again_reg = coarse_again & 0x3F;
	} else if (a_gain < 0x1d0) { /*7.250x~14.5x*/
		fine_again = (a_gain - 0xe8) * 100 / 725 + 0x20;
		coarse_again = 0x2f;
		if (fine_again > 0x3f)
			fine_again = 0x3f;
		fine_again_reg = fine_again & 0x3f;
		coarse_again_reg = coarse_again & 0x3F;
	} else { /*14.5x~28.547*/
		fine_again = (a_gain - 0x1d0) * 1000 / 14047 + 0x20;
		coarse_again = 0x3f;
		if (fine_again > 0x3f)
			fine_again = 0x3f;
		fine_again_reg = fine_again & 0x3f;
		coarse_again_reg = coarse_again & 0x3F;
	}
	ret |= sc132gs_write_reg(sc132gs->client,
		SC132GS_REG_COARSE_AGAIN,
		SC132GS_REG_VALUE_08BIT,
		coarse_again_reg);
	ret |= sc132gs_write_reg(sc132gs->client,
		SC132GS_REG_FINE_AGAIN,
		SC132GS_REG_VALUE_08BIT,
		fine_again_reg);
	return ret;
}

static int __sc132gs_start_stream(struct sc132gs *sc132gs)
{
	int ret;

	ret = sc132gs_write_array(sc132gs->client, sc132gs->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&sc132gs->mutex);
	ret = v4l2_ctrl_handler_setup(&sc132gs->ctrl_handler);
	mutex_lock(&sc132gs->mutex);
	if (ret)
		return ret;

	return sc132gs_write_reg(sc132gs->client, SC132GS_REG_CTRL_MODE,
			SC132GS_REG_VALUE_08BIT, SC132GS_MODE_STREAMING);
}

static int __sc132gs_stop_stream(struct sc132gs *sc132gs)
{
	return sc132gs_write_reg(sc132gs->client, SC132GS_REG_CTRL_MODE,
			SC132GS_REG_VALUE_08BIT, SC132GS_MODE_SW_STANDBY);
}

static int sc132gs_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc132gs *sc132gs = to_sc132gs(sd);
	struct i2c_client *client = sc132gs->client;
	int ret = 0;

	mutex_lock(&sc132gs->mutex);
	on = !!on;
	if (on == sc132gs->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc132gs_start_stream(sc132gs);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc132gs_stop_stream(sc132gs);
		pm_runtime_put(&client->dev);
	}

	sc132gs->streaming = on;

unlock_and_return:
	mutex_unlock(&sc132gs->mutex);

	return ret;
}

static int sc132gs_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc132gs *sc132gs = to_sc132gs(sd);
	struct i2c_client *client = sc132gs->client;
	int ret = 0;

	mutex_lock(&sc132gs->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc132gs->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		sc132gs->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc132gs->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc132gs->mutex);

	return ret;
}

static int sc132gs_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct sc132gs *sc132gs = to_sc132gs(sd);
	const struct sc132gs_mode *mode = sc132gs->cur_mode;

	mutex_lock(&sc132gs->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&sc132gs->mutex);

	return 0;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 sc132gs_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC132GS_XVCLK_FREQ / 1000 / 1000);
}

static int __sc132gs_power_on(struct sc132gs *sc132gs)
{
	int ret;
	u32 delay_us;
	struct device *dev = &sc132gs->client->dev;

	if (!IS_ERR_OR_NULL(sc132gs->pins_default)) {
		ret = pinctrl_select_state(sc132gs->pinctrl,
					   sc132gs->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = clk_set_rate(sc132gs->xvclk, SC132GS_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc132gs->xvclk) != SC132GS_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc132gs->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(SC132GS_NUM_SUPPLIES, sc132gs->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc132gs->pwdn_gpio))
		gpiod_set_value_cansleep(sc132gs->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = sc132gs_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(sc132gs->xvclk);

	return ret;
}

static void __sc132gs_power_off(struct sc132gs *sc132gs)
{
	int ret;

	if (!IS_ERR(sc132gs->pwdn_gpio))
		gpiod_set_value_cansleep(sc132gs->pwdn_gpio, 0);
	clk_disable_unprepare(sc132gs->xvclk);
	if (!IS_ERR_OR_NULL(sc132gs->pins_sleep)) {
		ret = pinctrl_select_state(sc132gs->pinctrl,
					   sc132gs->pins_sleep);
		if (ret < 0)
			dev_dbg(&sc132gs->client->dev, "could not set pins\n");
	}
	regulator_bulk_disable(SC132GS_NUM_SUPPLIES, sc132gs->supplies);
}

static int sc132gs_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc132gs *sc132gs = to_sc132gs(sd);

	return __sc132gs_power_on(sc132gs);
}

static int sc132gs_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc132gs *sc132gs = to_sc132gs(sd);

	__sc132gs_power_off(sc132gs);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc132gs_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc132gs *sc132gs = to_sc132gs(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc132gs_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc132gs->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = PIX_FORMAT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc132gs->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc132gs_enum_frame_interval(struct v4l2_subdev *sd,
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

static int sc132gs_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (SC132GS_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops sc132gs_pm_ops = {
	SET_RUNTIME_PM_OPS(sc132gs_runtime_suspend,
			   sc132gs_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc132gs_internal_ops = {
	.open = sc132gs_open,
};
#endif

static const struct v4l2_subdev_core_ops sc132gs_core_ops = {
	.s_power = sc132gs_s_power,
	.ioctl = sc132gs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc132gs_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc132gs_video_ops = {
	.s_stream = sc132gs_s_stream,
	.g_frame_interval = sc132gs_g_frame_interval,
	.g_mbus_config = sc132gs_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops sc132gs_pad_ops = {
	.enum_mbus_code = sc132gs_enum_mbus_code,
	.enum_frame_size = sc132gs_enum_frame_sizes,
	.enum_frame_interval = sc132gs_enum_frame_interval,
	.get_fmt = sc132gs_get_fmt,
	.set_fmt = sc132gs_set_fmt,
};

static const struct v4l2_subdev_ops sc132gs_subdev_ops = {
	.core	= &sc132gs_core_ops,
	.video	= &sc132gs_video_ops,
	.pad	= &sc132gs_pad_ops,
};

static int sc132gs_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc132gs *sc132gs = container_of(ctrl->handler,
					       struct sc132gs, ctrl_handler);
	struct i2c_client *client = sc132gs->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc132gs->cur_mode->height + ctrl->val - 6;
		__v4l2_ctrl_modify_range(sc132gs->exposure,
					 sc132gs->exposure->minimum, max,
					 sc132gs->exposure->step,
					 sc132gs->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = sc132gs_write_reg(sc132gs->client, SC132GS_REG_EXPOSURE,
			SC132GS_REG_VALUE_16BIT, ctrl->val << 4);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = sc132gs_set_ctrl_gain(sc132gs, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = sc132gs_write_reg(sc132gs->client, SC132GS_REG_VTS,
					SC132GS_REG_VALUE_16BIT,
					ctrl->val + sc132gs->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc132gs_enable_test_pattern(sc132gs, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc132gs_ctrl_ops = {
	.s_ctrl = sc132gs_set_ctrl,
};

static int sc132gs_initialize_controls(struct sc132gs *sc132gs)
{
	const struct sc132gs_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &sc132gs->ctrl_handler;
	mode = sc132gs->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &sc132gs->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, SC132GS_PIXEL_RATE, 1, SC132GS_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	sc132gs->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (sc132gs->hblank)
		sc132gs->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	sc132gs->vblank = v4l2_ctrl_new_std(handler, &sc132gs_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				SC132GS_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 6;
	sc132gs->exposure = v4l2_ctrl_new_std(handler, &sc132gs_ctrl_ops,
				V4L2_CID_EXPOSURE, SC132GS_EXPOSURE_MIN,
				exposure_max, SC132GS_EXPOSURE_STEP,
				mode->exp_def);

	sc132gs->anal_gain = v4l2_ctrl_new_std(handler, &sc132gs_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	sc132gs->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&sc132gs_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(sc132gs_test_pattern_menu) - 1,
				0, 0, sc132gs_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc132gs->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc132gs->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc132gs_check_sensor_id(struct sc132gs *sc132gs,
				  struct i2c_client *client)
{
	struct device *dev = &sc132gs->client->dev;
	u32 id = 0;
	int ret;

	ret = sc132gs_read_reg(client, SC132GS_REG_CHIP_ID,
			      SC132GS_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC132GS CHIP ID = 0x%04x sensor\n", CHIP_ID);

	return 0;
}

static int sc132gs_configure_regulators(struct sc132gs *sc132gs)
{
	unsigned int i;

	for (i = 0; i < SC132GS_NUM_SUPPLIES; i++)
		sc132gs->supplies[i].supply = sc132gs_supply_names[i];

	return devm_regulator_bulk_get(&sc132gs->client->dev,
				       SC132GS_NUM_SUPPLIES,
				       sc132gs->supplies);
}

static int sc132gs_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc132gs *sc132gs;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	sc132gs = devm_kzalloc(dev, sizeof(*sc132gs), GFP_KERNEL);
	if (!sc132gs)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc132gs->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc132gs->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc132gs->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc132gs->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	sc132gs->client = client;
	sc132gs->cur_mode = &supported_modes[0];

	sc132gs->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc132gs->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc132gs->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc132gs->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");
	ret = sc132gs_configure_regulators(sc132gs);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	sc132gs->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc132gs->pinctrl)) {
		sc132gs->pins_default =
			pinctrl_lookup_state(sc132gs->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc132gs->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc132gs->pins_sleep =
			pinctrl_lookup_state(sc132gs->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc132gs->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}
	mutex_init(&sc132gs->mutex);

	sd = &sc132gs->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc132gs_subdev_ops);
	ret = sc132gs_initialize_controls(sc132gs);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc132gs_power_on(sc132gs);
	if (ret)
		goto err_free_handler;

	ret = sc132gs_check_sensor_id(sc132gs, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc132gs_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc132gs->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc132gs->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc132gs->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc132gs->module_index, facing,
		 SC132GS_NAME, dev_name(sd->dev));
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
	__sc132gs_power_off(sc132gs);
err_free_handler:
	v4l2_ctrl_handler_free(&sc132gs->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc132gs->mutex);

	return ret;
}

static int sc132gs_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc132gs *sc132gs = to_sc132gs(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc132gs->ctrl_handler);
	mutex_destroy(&sc132gs->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc132gs_power_off(sc132gs);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc132gs_of_match[] = {
	{ .compatible = "smartsens,sc132gs" },
	{},
};
MODULE_DEVICE_TABLE(of, sc132gs_of_match);
#endif

static const struct i2c_device_id sc132gs_match_id[] = {
	{ "smartsens,sc132gs", 0 },
	{ },
};

static struct i2c_driver sc132gs_i2c_driver = {
	.driver = {
		.name = SC132GS_NAME,
		.pm = &sc132gs_pm_ops,
		.of_match_table = of_match_ptr(sc132gs_of_match),
	},
	.probe		= &sc132gs_probe,
	.remove		= &sc132gs_remove,
	.id_table	= sc132gs_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc132gs_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc132gs_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Smartsens sc132gs sensor driver");
MODULE_LICENSE("GPL v2");
