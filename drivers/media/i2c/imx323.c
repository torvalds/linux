// SPDX-License-Identifier: GPL-2.0
/*
 * imx323 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 add enum_frame_interval function.
 * V0.0X01.0X03 add quick stream on/off
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x03)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

/* 74.25Mhz */
#define IMX323_PIXEL_RATE		(74250 * 1000)
#define IMX323_XVCLK_FREQ		37125000

#define CHIP_ID				0xa
#define IMX323_REG_CHIP_ID		0x0112

#define IMX323_REG_CTRL_MODE		0x0100
#define IMX323_MODE_SW_STANDBY		0x0
#define IMX323_MODE_STREAMING		BIT(0)

#define IMX323_REG_EXPOSURE		0x0202
#define IMX323_EXPOSURE_MIN		0
#define IMX323_EXPOSURE_STEP		1
#define IMX323_VTS_MAX			0x465

#define IMX323_REG_ANALOG_GAIN		0x301e
#define ANALOG_GAIN_MIN			0x0
#define ANALOG_GAIN_MAX			0x78
#define ANALOG_GAIN_STEP		1
#define ANALOG_GAIN_DEFAULT		0x10

#define IMX323_REG_VTS			0x0340

#define IMX323_REG_ORIENTATION		0x0101
#define IMX323_ORIENTATION_H		0x1
#define IMX323_ORIENTATION_V		0x2

#define REG_NULL			0xFFFF

#define IMX323_REG_VALUE_08BIT		1
#define IMX323_REG_VALUE_16BIT		2
#define IMX323_REG_VALUE_24BIT		3

/* h_offs 35 v_offs 14 */
#define PIX_FORMAT MEDIA_BUS_FMT_SBGGR10_1X10

#define IMX323_NAME			"imx323"

struct cam_regulator {
	char name[32];
	int val;
};

static const struct cam_regulator imx323_regulator[] = {
	{"avdd", 2800000},	/* Analog power */
	{"dovdd", 1800000},	/* Digital I/O power */
	{"dvdd", 1200000},	/* Digital core power */
};

#define IMX323_NUM_SUPPLIES ARRAY_SIZE(imx323_regulator)

struct regval {
	u16 addr;
	u8 val;
};

struct imx323_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct imx323 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[IMX323_NUM_SUPPLIES];

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
	const struct imx323_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_imx323(sd) container_of(sd, struct imx323, subdev)

/*
 * Xclk 37.125Mhz
 * Pclk 74.25Mhz
 * linelength 2200(0x44c * 2)
 * framelength 1125(0x465)
 * grabwindow_width 1920
 * grabwindow_height 1080
 * max_framerate 30fps
 * dvp bt656 10bit
 */
static const struct regval imx323_regs[] = {
	{0x0100, 0x00},
	{0x0009, 0x3f},
	{0x0340, 0x04},
	{0x0341, 0x65},
	{0x0342, 0x04},
	{0x0343, 0x4c},
	{0x3000, 0x31},
	{0x3002, 0x0f},
	{0x3011, 0x00},
	{0x3013, 0x40},
	{0x3016, 0x3c},
	{0x301a, 0x51},
	{0x301f, 0x73},
	{0x3021, 0x80},
	{0x3022, 0x40},
	{0x3027, 0x20},
	{0x302c, 0x00},
	{0x302d, 0x48}, /* low 10bit */
	{0x304f, 0x47},
	{0x3054, 0x10},
	{0x307a, 0x40},
	{0x307b, 0x02},
	{0x3117, 0x0d},
	{REG_NULL, 0x00},
};

static const struct imx323_mode supported_modes[] = {
	{
		.width = 2200,
		.height = 1125,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0100,
		.hts_def = 0x044c * 2,
		.vts_def = 0x0465,
		.reg_list = imx323_regs,
	}
};

static const char * const imx323_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int imx323_write_reg(struct i2c_client *client, u16 reg,
			    int len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

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

static int imx323_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = imx323_write_reg(client, regs[i].addr,
				       IMX323_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int imx323_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int imx323_get_reso_dist(const struct imx323_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct imx323_mode *
imx323_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = imx323_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int imx323_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx323 *imx323 = to_imx323(sd);
	const struct imx323_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&imx323->mutex);

	mode = imx323_find_best_fit(fmt);
	fmt->format.code = PIX_FORMAT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&imx323->mutex);
		return -ENOTTY;
#endif
	} else {
		imx323->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(imx323->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(imx323->vblank, vblank_def,
					 IMX323_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&imx323->mutex);

	return 0;
}

static int imx323_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx323 *imx323 = to_imx323(sd);
	const struct imx323_mode *mode = imx323->cur_mode;

	mutex_lock(&imx323->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&imx323->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = PIX_FORMAT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&imx323->mutex);

	return 0;
}

static int imx323_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = PIX_FORMAT;

	return 0;
}

static int imx323_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int imx323_enable_test_pattern(struct imx323 *imx323, u32 pattern)
{
	return 0;
}

static void imx323_get_module_inf(struct imx323 *imx323,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, IMX323_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, imx323->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, imx323->len_name, sizeof(inf->base.lens));
}

static long imx323_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx323 *imx323 = to_imx323(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		imx323_get_module_inf(imx323, (struct rkmodule_inf *)arg);
		break;

	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			imx323_write_reg(imx323->client, IMX323_REG_CTRL_MODE,
				IMX323_REG_VALUE_08BIT, IMX323_MODE_STREAMING);
		else
			imx323_write_reg(imx323->client, IMX323_REG_CTRL_MODE,
				IMX323_REG_VALUE_08BIT, IMX323_MODE_SW_STANDBY);
		break;

	case RKMODULE_GET_BT656_INTF_TYPE:
		*(__u32 *)arg = BT656_SONY_RAW;
		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx323_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	__u32 intf;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx323_ioctl(sd, cmd, inf);
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
			ret = imx323_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;

	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = imx323_ioctl(sd, cmd, &stream);
		break;

	case RKMODULE_GET_BT656_INTF_TYPE:
		intf = BT656_SONY_RAW;

		ret = copy_to_user(up, &intf, sizeof(intf));
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __imx323_start_stream(struct imx323 *imx323)
{
	int ret;

	ret = imx323_write_array(imx323->client, imx323->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&imx323->mutex);
	ret = v4l2_ctrl_handler_setup(&imx323->ctrl_handler);
	mutex_lock(&imx323->mutex);
	if (ret)
		return ret;

	return imx323_write_reg(imx323->client, IMX323_REG_CTRL_MODE,
				IMX323_REG_VALUE_08BIT, IMX323_MODE_STREAMING);
}

static int __imx323_stop_stream(struct imx323 *imx323)
{
	return imx323_write_reg(imx323->client, IMX323_REG_CTRL_MODE,
				IMX323_REG_VALUE_08BIT, IMX323_MODE_SW_STANDBY);
}

static int imx323_s_stream(struct v4l2_subdev *sd, int on)
{
	struct imx323 *imx323 = to_imx323(sd);
	struct i2c_client *client = imx323->client;
	int ret = 0;

	mutex_lock(&imx323->mutex);
	on = !!on;
	if (on == imx323->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __imx323_start_stream(imx323);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__imx323_stop_stream(imx323);
		pm_runtime_put(&client->dev);
	}

	imx323->streaming = on;
unlock_and_return:
	mutex_unlock(&imx323->mutex);

	return ret;
}

static int imx323_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx323 *imx323 = to_imx323(sd);
	const struct imx323_mode *mode = imx323->cur_mode;

	mutex_lock(&imx323->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&imx323->mutex);

	return 0;
}

static int imx323_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx323 *imx323 = to_imx323(sd);
	struct i2c_client *client = imx323->client;
	int ret = 0;

	mutex_lock(&imx323->mutex);

	/* If the power state is not modified - no work to do. */
	if (imx323->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		imx323->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		imx323->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&imx323->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 imx323_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, IMX323_XVCLK_FREQ / 1000 / 1000);
}

static int __imx323_power_on(struct imx323 *imx323)
{
	int ret;
	u32 i, delay_us;
	struct device *dev = &imx323->client->dev;

	ret = clk_set_rate(imx323->xvclk, IMX323_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (%d)\n",
			IMX323_XVCLK_FREQ);
		return ret;
	}
	if (clk_get_rate(imx323->xvclk) != IMX323_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on %d\n",
			IMX323_XVCLK_FREQ);
	ret = clk_prepare_enable(imx323->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(imx323->reset_gpio))
		gpiod_set_value_cansleep(imx323->reset_gpio, 0);

	for (i = 0; i < IMX323_NUM_SUPPLIES; i++)
		regulator_set_voltage(imx323->supplies[i].consumer,
			imx323_regulator[i].val,
			imx323_regulator[i].val);

	ret = regulator_bulk_enable(IMX323_NUM_SUPPLIES, imx323->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(imx323->reset_gpio))
		gpiod_set_value_cansleep(imx323->reset_gpio, 1);

	if (!IS_ERR(imx323->pwdn_gpio))
		gpiod_set_value_cansleep(imx323->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = imx323_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(imx323->xvclk);

	return ret;
}

static void __imx323_power_off(struct imx323 *imx323)
{
	if (!IS_ERR(imx323->pwdn_gpio))
		gpiod_set_value_cansleep(imx323->pwdn_gpio, 0);
	clk_disable_unprepare(imx323->xvclk);
	if (!IS_ERR(imx323->reset_gpio))
		gpiod_set_value_cansleep(imx323->reset_gpio, 0);
	regulator_bulk_disable(IMX323_NUM_SUPPLIES, imx323->supplies);
}

static int imx323_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx323 *imx323 = to_imx323(sd);

	return __imx323_power_on(imx323);
}

static int imx323_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx323 *imx323 = to_imx323(sd);

	__imx323_power_off(imx323);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int imx323_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx323 *imx323 = to_imx323(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct imx323_mode *def_mode = &supported_modes[0];

	mutex_lock(&imx323->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = PIX_FORMAT;
	try_fmt->field = V4L2_FIELD_NONE;
	mutex_unlock(&imx323->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int imx323_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_BT656;
	config->flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_HIGH |
			V4L2_MBUS_PCLK_SAMPLE_FALLING;
	return 0;
}

static int imx323_enum_frame_interval(struct v4l2_subdev *sd,
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

static const struct dev_pm_ops imx323_pm_ops = {
	SET_RUNTIME_PM_OPS(imx323_runtime_suspend,
			   imx323_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops imx323_internal_ops = {
	.open = imx323_open,
};
#endif

static const struct v4l2_subdev_core_ops imx323_core_ops = {
	.s_power = imx323_s_power,
	.ioctl = imx323_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx323_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx323_video_ops = {
	.s_stream = imx323_s_stream,
	.g_mbus_config = imx323_g_mbus_config,
	.g_frame_interval = imx323_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx323_pad_ops = {
	.enum_mbus_code = imx323_enum_mbus_code,
	.enum_frame_size = imx323_enum_frame_sizes,
	.enum_frame_interval = imx323_enum_frame_interval,
	.get_fmt = imx323_get_fmt,
	.set_fmt = imx323_set_fmt,
};

static const struct v4l2_subdev_ops imx323_subdev_ops = {
	.core	= &imx323_core_ops,
	.video	= &imx323_video_ops,
	.pad	= &imx323_pad_ops,
};

static int imx323_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx323 *imx323 = container_of(ctrl->handler,
					     struct imx323, ctrl_handler);
	struct i2c_client *client = imx323->client;
	int ret = 0;

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = imx323_write_reg(imx323->client, IMX323_REG_EXPOSURE,
				       IMX323_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = imx323_write_reg(imx323->client, IMX323_REG_ANALOG_GAIN,
				       IMX323_REG_VALUE_08BIT, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = imx323_write_reg(imx323->client, IMX323_REG_VTS,
				       IMX323_REG_VALUE_16BIT,
				       ctrl->val + imx323->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx323_enable_test_pattern(imx323, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx323_ctrl_ops = {
	.s_ctrl = imx323_set_ctrl,
};

static int imx323_initialize_controls(struct imx323 *imx323)
{
	const struct imx323_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &imx323->ctrl_handler;
	mode = imx323->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &imx323->mutex;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, IMX323_PIXEL_RATE, 1, IMX323_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	imx323->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (imx323->hblank)
		imx323->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	imx323->vblank = v4l2_ctrl_new_std(handler, &imx323_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				IMX323_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 1;
	imx323->exposure = v4l2_ctrl_new_std(handler, &imx323_ctrl_ops,
				V4L2_CID_EXPOSURE, IMX323_EXPOSURE_MIN,
				exposure_max, IMX323_EXPOSURE_STEP,
				mode->exp_def);

	imx323->anal_gain = v4l2_ctrl_new_std(handler, &imx323_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	imx323->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&imx323_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(imx323_test_pattern_menu) - 1,
				0, 0, imx323_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&imx323->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	imx323->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int imx323_check_sensor_id(struct imx323 *imx323,
				  struct i2c_client *client)
{
	struct device *dev = &imx323->client->dev;
	u32 id = 0;
	int ret;

	ret = imx323_read_reg(client, IMX323_REG_CHIP_ID,
			      IMX323_REG_VALUE_08BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected IMX323 sensor\n");

	return 0;
}

static int imx323_configure_regulators(struct imx323 *imx323)
{
	u32 i;

	for (i = 0; i < IMX323_NUM_SUPPLIES; i++)
		imx323->supplies[i].supply =
			imx323_regulator[i].name;

	return devm_regulator_bulk_get(&imx323->client->dev,
				       IMX323_NUM_SUPPLIES,
				       imx323->supplies);
}

static int imx323_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct imx323 *imx323;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	imx323 = devm_kzalloc(dev, sizeof(*imx323), GFP_KERNEL);
	if (!imx323)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &imx323->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &imx323->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &imx323->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &imx323->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	imx323->client = client;
	imx323->cur_mode = &supported_modes[0];

	imx323->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(imx323->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	imx323->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx323->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx323->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(imx323->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = imx323_configure_regulators(imx323);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&imx323->mutex);

	sd = &imx323->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx323_subdev_ops);
	ret = imx323_initialize_controls(imx323);
	if (ret)
		goto err_destroy_mutex;

	ret = __imx323_power_on(imx323);
	if (ret)
		goto err_free_handler;

	ret = imx323_check_sensor_id(imx323, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &imx323_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	imx323->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx323->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(imx323->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 imx323->module_index, facing,
		 IMX323_NAME, dev_name(sd->dev));
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
	__imx323_power_off(imx323);
err_free_handler:
	v4l2_ctrl_handler_free(&imx323->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&imx323->mutex);

	return ret;
}

static int imx323_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx323 *imx323 = to_imx323(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&imx323->ctrl_handler);
	mutex_destroy(&imx323->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__imx323_power_off(imx323);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id imx323_of_match[] = {
	{ .compatible = "sony,imx323" },
	{},
};
MODULE_DEVICE_TABLE(of, imx323_of_match);
#endif

static const struct i2c_device_id imx323_match_id[] = {
	{ "sony,imx323", 0 },
	{ },
};

static struct i2c_driver imx323_i2c_driver = {
	.driver = {
		.name = IMX323_NAME,
		.pm = &imx323_pm_ops,
		.of_match_table = of_match_ptr(imx323_of_match),
	},
	.probe		= &imx323_probe,
	.remove		= &imx323_remove,
	.id_table	= imx323_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&imx323_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&imx323_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony imx323 sensor driver");
MODULE_LICENSE("GPL v2");
