// SPDX-License-Identifier: GPL-2.0
/*
 * Omnivision OV2680 CMOS Image Sensor driver
 *
 * Copyright (C) 2018 Linaro Ltd
 *
 * Based on OV5640 Sensor Driver
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2014-2017 Mentor Graphics Inc.
 *
 */

#include <asm/unaligned.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define OV2680_XVCLK_VALUE	24000000

#define OV2680_CHIP_ID		0x2680

#define OV2680_REG_STREAM_CTRL		0x0100
#define OV2680_REG_SOFT_RESET		0x0103

#define OV2680_REG_CHIP_ID_HIGH		0x300a
#define OV2680_REG_CHIP_ID_LOW		0x300b

#define OV2680_REG_R_MANUAL		0x3503
#define OV2680_REG_GAIN_PK		0x350a
#define OV2680_REG_EXPOSURE_PK_HIGH	0x3500
#define OV2680_REG_TIMING_HTS		0x380c
#define OV2680_REG_TIMING_VTS		0x380e
#define OV2680_REG_FORMAT1		0x3820
#define OV2680_REG_FORMAT2		0x3821

#define OV2680_REG_ISP_CTRL00		0x5080

#define OV2680_FRAME_RATE		30

#define OV2680_REG_VALUE_8BIT		1
#define OV2680_REG_VALUE_16BIT		2
#define OV2680_REG_VALUE_24BIT		3

#define OV2680_WIDTH_MAX		1600
#define OV2680_HEIGHT_MAX		1200

enum ov2680_mode_id {
	OV2680_MODE_QUXGA_800_600,
	OV2680_MODE_720P_1280_720,
	OV2680_MODE_UXGA_1600_1200,
	OV2680_MODE_MAX,
};

struct reg_value {
	u16 reg_addr;
	u8 val;
};

static const char * const ov2680_supply_name[] = {
	"DOVDD",
	"DVDD",
	"AVDD",
};

#define OV2680_NUM_SUPPLIES ARRAY_SIZE(ov2680_supply_name)

struct ov2680_mode_info {
	const char *name;
	enum ov2680_mode_id id;
	u32 width;
	u32 height;
	const struct reg_value *reg_data;
	u32 reg_data_size;
};

struct ov2680_ctrls {
	struct v4l2_ctrl_handler handler;
	struct {
		struct v4l2_ctrl *auto_exp;
		struct v4l2_ctrl *exposure;
	};
	struct {
		struct v4l2_ctrl *auto_gain;
		struct v4l2_ctrl *gain;
	};

	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *test_pattern;
};

struct ov2680_dev {
	struct i2c_client		*i2c_client;
	struct v4l2_subdev		sd;

	struct media_pad		pad;
	struct clk			*xvclk;
	u32				xvclk_freq;
	struct regulator_bulk_data	supplies[OV2680_NUM_SUPPLIES];

	struct gpio_desc		*reset_gpio;
	struct mutex			lock; /* protect members */

	bool				mode_pending_changes;
	bool				is_enabled;
	bool				is_streaming;

	struct ov2680_ctrls		ctrls;
	struct v4l2_mbus_framefmt	fmt;
	struct v4l2_fract		frame_interval;

	const struct ov2680_mode_info	*current_mode;
};

static const char * const test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Random Data",
	"Square",
	"Black Image",
};

static const int ov2680_hv_flip_bayer_order[] = {
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
};

static const struct reg_value ov2680_setting_30fps_QUXGA_800_600[] = {
	{0x3086, 0x01}, {0x370a, 0x23}, {0x3808, 0x03}, {0x3809, 0x20},
	{0x380a, 0x02}, {0x380b, 0x58}, {0x380c, 0x06}, {0x380d, 0xac},
	{0x380e, 0x02}, {0x380f, 0x84}, {0x3811, 0x04}, {0x3813, 0x04},
	{0x3814, 0x31}, {0x3815, 0x31}, {0x3820, 0xc0}, {0x4008, 0x00},
	{0x4009, 0x03}, {0x4837, 0x1e}, {0x3501, 0x4e}, {0x3502, 0xe0},
};

static const struct reg_value ov2680_setting_30fps_720P_1280_720[] = {
	{0x3086, 0x00}, {0x3808, 0x05}, {0x3809, 0x00}, {0x380a, 0x02},
	{0x380b, 0xd0}, {0x380c, 0x06}, {0x380d, 0xa8}, {0x380e, 0x05},
	{0x380f, 0x0e}, {0x3811, 0x08}, {0x3813, 0x06}, {0x3814, 0x11},
	{0x3815, 0x11}, {0x3820, 0xc0}, {0x4008, 0x00},
};

static const struct reg_value ov2680_setting_30fps_UXGA_1600_1200[] = {
	{0x3086, 0x00}, {0x3501, 0x4e}, {0x3502, 0xe0}, {0x3808, 0x06},
	{0x3809, 0x40}, {0x380a, 0x04}, {0x380b, 0xb0}, {0x380c, 0x06},
	{0x380d, 0xa8}, {0x380e, 0x05}, {0x380f, 0x0e}, {0x3811, 0x00},
	{0x3813, 0x00}, {0x3814, 0x11}, {0x3815, 0x11}, {0x3820, 0xc0},
	{0x4008, 0x00}, {0x4837, 0x18}
};

static const struct ov2680_mode_info ov2680_mode_init_data = {
	"mode_quxga_800_600", OV2680_MODE_QUXGA_800_600, 800, 600,
	ov2680_setting_30fps_QUXGA_800_600,
	ARRAY_SIZE(ov2680_setting_30fps_QUXGA_800_600),
};

static const struct ov2680_mode_info ov2680_mode_data[OV2680_MODE_MAX] = {
	{"mode_quxga_800_600", OV2680_MODE_QUXGA_800_600,
	 800, 600, ov2680_setting_30fps_QUXGA_800_600,
	 ARRAY_SIZE(ov2680_setting_30fps_QUXGA_800_600)},
	{"mode_720p_1280_720", OV2680_MODE_720P_1280_720,
	 1280, 720, ov2680_setting_30fps_720P_1280_720,
	 ARRAY_SIZE(ov2680_setting_30fps_720P_1280_720)},
	{"mode_uxga_1600_1200", OV2680_MODE_UXGA_1600_1200,
	 1600, 1200, ov2680_setting_30fps_UXGA_1600_1200,
	 ARRAY_SIZE(ov2680_setting_30fps_UXGA_1600_1200)},
};

static struct ov2680_dev *to_ov2680_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov2680_dev, sd);
}

static struct device *ov2680_to_dev(struct ov2680_dev *sensor)
{
	return &sensor->i2c_client->dev;
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov2680_dev,
			     ctrls.handler)->sd;
}

static int __ov2680_write_reg(struct ov2680_dev *sensor, u16 reg,
			      unsigned int len, u32 val)
{
	struct i2c_client *client = sensor->i2c_client;
	u8 buf[6];
	int ret;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	ret = i2c_master_send(client, buf, len + 2);
	if (ret != len + 2) {
		dev_err(&client->dev, "write error: reg=0x%4x: %d\n", reg, ret);
		return -EIO;
	}

	return 0;
}

#define ov2680_write_reg(s, r, v) \
	__ov2680_write_reg(s, r, OV2680_REG_VALUE_8BIT, v)

#define ov2680_write_reg16(s, r, v) \
	__ov2680_write_reg(s, r, OV2680_REG_VALUE_16BIT, v)

#define ov2680_write_reg24(s, r, v) \
	__ov2680_write_reg(s, r, OV2680_REG_VALUE_24BIT, v)

static int __ov2680_read_reg(struct ov2680_dev *sensor, u16 reg,
			     unsigned int len, u32 *val)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msgs[2];
	u8 addr_buf[2] = { reg >> 8, reg & 0xff };
	u8 data_buf[4] = { 0, };
	int ret;

	if (len > 4)
		return -EINVAL;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "read error: reg=0x%4x: %d\n", reg, ret);
		return -EIO;
	}

	*val = get_unaligned_be32(data_buf);

	return 0;
}

#define ov2680_read_reg(s, r, v) \
	__ov2680_read_reg(s, r, OV2680_REG_VALUE_8BIT, v)

#define ov2680_read_reg16(s, r, v) \
	__ov2680_read_reg(s, r, OV2680_REG_VALUE_16BIT, v)

#define ov2680_read_reg24(s, r, v) \
	__ov2680_read_reg(s, r, OV2680_REG_VALUE_24BIT, v)

static int ov2680_mod_reg(struct ov2680_dev *sensor, u16 reg, u8 mask, u8 val)
{
	u32 readval;
	int ret;

	ret = ov2680_read_reg(sensor, reg, &readval);
	if (ret < 0)
		return ret;

	readval &= ~mask;
	val &= mask;
	val |= readval;

	return ov2680_write_reg(sensor, reg, val);
}

static int ov2680_load_regs(struct ov2680_dev *sensor,
			    const struct ov2680_mode_info *mode)
{
	const struct reg_value *regs = mode->reg_data;
	unsigned int i;
	int ret = 0;
	u16 reg_addr;
	u8 val;

	for (i = 0; i < mode->reg_data_size; ++i, ++regs) {
		reg_addr = regs->reg_addr;
		val = regs->val;

		ret = ov2680_write_reg(sensor, reg_addr, val);
		if (ret)
			break;
	}

	return ret;
}

static void ov2680_power_up(struct ov2680_dev *sensor)
{
	if (!sensor->reset_gpio)
		return;

	gpiod_set_value(sensor->reset_gpio, 0);
	usleep_range(5000, 10000);
}

static void ov2680_power_down(struct ov2680_dev *sensor)
{
	if (!sensor->reset_gpio)
		return;

	gpiod_set_value(sensor->reset_gpio, 1);
	usleep_range(5000, 10000);
}

static int ov2680_bayer_order(struct ov2680_dev *sensor)
{
	u32 format1;
	u32 format2;
	u32 hv_flip;
	int ret;

	ret = ov2680_read_reg(sensor, OV2680_REG_FORMAT1, &format1);
	if (ret < 0)
		return ret;

	ret = ov2680_read_reg(sensor, OV2680_REG_FORMAT2, &format2);
	if (ret < 0)
		return ret;

	hv_flip = (format2 & BIT(2)  << 1) | (format1 & BIT(2));

	sensor->fmt.code = ov2680_hv_flip_bayer_order[hv_flip];

	return 0;
}

static int ov2680_vflip_enable(struct ov2680_dev *sensor)
{
	int ret;

	ret = ov2680_mod_reg(sensor, OV2680_REG_FORMAT1, BIT(2), BIT(2));
	if (ret < 0)
		return ret;

	return ov2680_bayer_order(sensor);
}

static int ov2680_vflip_disable(struct ov2680_dev *sensor)
{
	int ret;

	ret = ov2680_mod_reg(sensor, OV2680_REG_FORMAT1, BIT(2), BIT(0));
	if (ret < 0)
		return ret;

	return ov2680_bayer_order(sensor);
}

static int ov2680_hflip_enable(struct ov2680_dev *sensor)
{
	int ret;

	ret = ov2680_mod_reg(sensor, OV2680_REG_FORMAT2, BIT(2), BIT(2));
	if (ret < 0)
		return ret;

	return ov2680_bayer_order(sensor);
}

static int ov2680_hflip_disable(struct ov2680_dev *sensor)
{
	int ret;

	ret = ov2680_mod_reg(sensor, OV2680_REG_FORMAT2, BIT(2), BIT(0));
	if (ret < 0)
		return ret;

	return ov2680_bayer_order(sensor);
}

static int ov2680_test_pattern_set(struct ov2680_dev *sensor, int value)
{
	int ret;

	if (!value)
		return ov2680_mod_reg(sensor, OV2680_REG_ISP_CTRL00, BIT(7), 0);

	ret = ov2680_mod_reg(sensor, OV2680_REG_ISP_CTRL00, 0x03, value - 1);
	if (ret < 0)
		return ret;

	ret = ov2680_mod_reg(sensor, OV2680_REG_ISP_CTRL00, BIT(7), BIT(7));
	if (ret < 0)
		return ret;

	return 0;
}

static int ov2680_gain_set(struct ov2680_dev *sensor, bool auto_gain)
{
	struct ov2680_ctrls *ctrls = &sensor->ctrls;
	u32 gain;
	int ret;

	ret = ov2680_mod_reg(sensor, OV2680_REG_R_MANUAL, BIT(1),
			     auto_gain ? 0 : BIT(1));
	if (ret < 0)
		return ret;

	if (auto_gain || !ctrls->gain->is_new)
		return 0;

	gain = ctrls->gain->val;

	ret = ov2680_write_reg16(sensor, OV2680_REG_GAIN_PK, gain);

	return 0;
}

static int ov2680_gain_get(struct ov2680_dev *sensor)
{
	u32 gain;
	int ret;

	ret = ov2680_read_reg16(sensor, OV2680_REG_GAIN_PK, &gain);
	if (ret)
		return ret;

	return gain;
}

static int ov2680_exposure_set(struct ov2680_dev *sensor, bool auto_exp)
{
	struct ov2680_ctrls *ctrls = &sensor->ctrls;
	u32 exp;
	int ret;

	ret = ov2680_mod_reg(sensor, OV2680_REG_R_MANUAL, BIT(0),
			     auto_exp ? 0 : BIT(0));
	if (ret < 0)
		return ret;

	if (auto_exp || !ctrls->exposure->is_new)
		return 0;

	exp = (u32)ctrls->exposure->val;
	exp <<= 4;

	return ov2680_write_reg24(sensor, OV2680_REG_EXPOSURE_PK_HIGH, exp);
}

static int ov2680_exposure_get(struct ov2680_dev *sensor)
{
	int ret;
	u32 exp;

	ret = ov2680_read_reg24(sensor, OV2680_REG_EXPOSURE_PK_HIGH, &exp);
	if (ret)
		return ret;

	return exp >> 4;
}

static int ov2680_stream_enable(struct ov2680_dev *sensor)
{
	return ov2680_write_reg(sensor, OV2680_REG_STREAM_CTRL, 1);
}

static int ov2680_stream_disable(struct ov2680_dev *sensor)
{
	return ov2680_write_reg(sensor, OV2680_REG_STREAM_CTRL, 0);
}

static int ov2680_mode_set(struct ov2680_dev *sensor)
{
	struct ov2680_ctrls *ctrls = &sensor->ctrls;
	int ret;

	ret = ov2680_gain_set(sensor, false);
	if (ret < 0)
		return ret;

	ret = ov2680_exposure_set(sensor, false);
	if (ret < 0)
		return ret;

	ret = ov2680_load_regs(sensor, sensor->current_mode);
	if (ret < 0)
		return ret;

	if (ctrls->auto_gain->val) {
		ret = ov2680_gain_set(sensor, true);
		if (ret < 0)
			return ret;
	}

	if (ctrls->auto_exp->val == V4L2_EXPOSURE_AUTO) {
		ret = ov2680_exposure_set(sensor, true);
		if (ret < 0)
			return ret;
	}

	sensor->mode_pending_changes = false;

	return 0;
}

static int ov2680_mode_restore(struct ov2680_dev *sensor)
{
	int ret;

	ret = ov2680_load_regs(sensor, &ov2680_mode_init_data);
	if (ret < 0)
		return ret;

	return ov2680_mode_set(sensor);
}

static int ov2680_power_off(struct ov2680_dev *sensor)
{
	if (!sensor->is_enabled)
		return 0;

	clk_disable_unprepare(sensor->xvclk);
	ov2680_power_down(sensor);
	regulator_bulk_disable(OV2680_NUM_SUPPLIES, sensor->supplies);
	sensor->is_enabled = false;

	return 0;
}

static int ov2680_power_on(struct ov2680_dev *sensor)
{
	struct device *dev = ov2680_to_dev(sensor);
	int ret;

	if (sensor->is_enabled)
		return 0;

	ret = regulator_bulk_enable(OV2680_NUM_SUPPLIES, sensor->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	if (!sensor->reset_gpio) {
		ret = ov2680_write_reg(sensor, OV2680_REG_SOFT_RESET, 0x01);
		if (ret != 0) {
			dev_err(dev, "sensor soft reset failed\n");
			return ret;
		}
		usleep_range(1000, 2000);
	} else {
		ov2680_power_down(sensor);
		ov2680_power_up(sensor);
	}

	ret = clk_prepare_enable(sensor->xvclk);
	if (ret < 0)
		return ret;

	sensor->is_enabled = true;

	/* Set clock lane into LP-11 state */
	ov2680_stream_enable(sensor);
	usleep_range(1000, 2000);
	ov2680_stream_disable(sensor);

	return 0;
}

static int ov2680_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);

	if (on)
		ret = ov2680_power_on(sensor);
	else
		ret = ov2680_power_off(sensor);

	mutex_unlock(&sensor->lock);

	if (on && ret == 0) {
		ret = v4l2_ctrl_handler_setup(&sensor->ctrls.handler);
		if (ret < 0)
			return ret;

		ret = ov2680_mode_restore(sensor);
	}

	return ret;
}

static int ov2680_s_g_frame_interval(struct v4l2_subdev *sd,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);

	mutex_lock(&sensor->lock);
	fi->interval = sensor->frame_interval;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int ov2680_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);

	if (sensor->is_streaming == !!enable)
		goto unlock;

	if (enable && sensor->mode_pending_changes) {
		ret = ov2680_mode_set(sensor);
		if (ret < 0)
			goto unlock;
	}

	if (enable)
		ret = ov2680_stream_enable(sensor);
	else
		ret = ov2680_stream_disable(sensor);

	sensor->is_streaming = !!enable;

unlock:
	mutex_unlock(&sensor->lock);

	return ret;
}

static int ov2680_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);

	if (code->pad != 0 || code->index != 0)
		return -EINVAL;

	code->code = sensor->fmt.code;

	return 0;
}

static int ov2680_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	struct v4l2_mbus_framefmt *fmt = NULL;
	int ret = 0;

	if (format->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt = v4l2_subdev_get_try_format(&sensor->sd, sd_state,
						 format->pad);
#else
		ret = -EINVAL;
#endif
	} else {
		fmt = &sensor->fmt;
	}

	if (fmt)
		format->format = *fmt;

	mutex_unlock(&sensor->lock);

	return ret;
}

static int ov2680_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	struct v4l2_mbus_framefmt *fmt = &format->format;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	struct v4l2_mbus_framefmt *try_fmt;
#endif
	const struct ov2680_mode_info *mode;
	int ret = 0;

	if (format->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);

	if (sensor->is_streaming) {
		ret = -EBUSY;
		goto unlock;
	}

	mode = v4l2_find_nearest_size(ov2680_mode_data,
				      ARRAY_SIZE(ov2680_mode_data), width,
				      height, fmt->width, fmt->height);
	if (!mode) {
		ret = -EINVAL;
		goto unlock;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		try_fmt = v4l2_subdev_get_try_format(sd, sd_state, 0);
		format->format = *try_fmt;
#endif
		goto unlock;
	}

	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = sensor->fmt.code;
	fmt->colorspace = sensor->fmt.colorspace;

	sensor->current_mode = mode;
	sensor->fmt = format->format;
	sensor->mode_pending_changes = true;

unlock:
	mutex_unlock(&sensor->lock);

	return ret;
}

static int ov2680_init_cfg(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = {
		.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY
		: V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.width = 800,
			.height = 600,
		}
	};

	return ov2680_set_fmt(sd, sd_state, &fmt);
}

static int ov2680_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	int index = fse->index;

	if (index >= OV2680_MODE_MAX || index < 0)
		return -EINVAL;

	fse->min_width = ov2680_mode_data[index].width;
	fse->min_height = ov2680_mode_data[index].height;
	fse->max_width = ov2680_mode_data[index].width;
	fse->max_height = ov2680_mode_data[index].height;

	return 0;
}

static int ov2680_enum_frame_interval(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_frame_interval_enum *fie)
{
	struct v4l2_fract tpf;

	if (fie->index >= OV2680_MODE_MAX || fie->width > OV2680_WIDTH_MAX ||
	    fie->height > OV2680_HEIGHT_MAX ||
	    fie->which > V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	tpf.denominator = OV2680_FRAME_RATE;
	tpf.numerator = 1;

	fie->interval = tpf;

	return 0;
}

static int ov2680_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	struct ov2680_ctrls *ctrls = &sensor->ctrls;
	int val;

	if (!sensor->is_enabled)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		val = ov2680_gain_get(sensor);
		if (val < 0)
			return val;
		ctrls->gain->val = val;
		break;
	case V4L2_CID_EXPOSURE:
		val = ov2680_exposure_get(sensor);
		if (val < 0)
			return val;
		ctrls->exposure->val = val;
		break;
	}

	return 0;
}

static int ov2680_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	struct ov2680_ctrls *ctrls = &sensor->ctrls;

	if (!sensor->is_enabled)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		return ov2680_gain_set(sensor, !!ctrl->val);
	case V4L2_CID_GAIN:
		return ov2680_gain_set(sensor, !!ctrls->auto_gain->val);
	case V4L2_CID_EXPOSURE_AUTO:
		return ov2680_exposure_set(sensor, !!ctrl->val);
	case V4L2_CID_EXPOSURE:
		return ov2680_exposure_set(sensor, !!ctrls->auto_exp->val);
	case V4L2_CID_VFLIP:
		if (sensor->is_streaming)
			return -EBUSY;
		if (ctrl->val)
			return ov2680_vflip_enable(sensor);
		else
			return ov2680_vflip_disable(sensor);
	case V4L2_CID_HFLIP:
		if (sensor->is_streaming)
			return -EBUSY;
		if (ctrl->val)
			return ov2680_hflip_enable(sensor);
		else
			return ov2680_hflip_disable(sensor);
	case V4L2_CID_TEST_PATTERN:
		return ov2680_test_pattern_set(sensor, ctrl->val);
	default:
		break;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops ov2680_ctrl_ops = {
	.g_volatile_ctrl = ov2680_g_volatile_ctrl,
	.s_ctrl = ov2680_s_ctrl,
};

static const struct v4l2_subdev_core_ops ov2680_core_ops = {
	.s_power = ov2680_s_power,
};

static const struct v4l2_subdev_video_ops ov2680_video_ops = {
	.g_frame_interval	= ov2680_s_g_frame_interval,
	.s_frame_interval	= ov2680_s_g_frame_interval,
	.s_stream		= ov2680_s_stream,
};

static const struct v4l2_subdev_pad_ops ov2680_pad_ops = {
	.init_cfg		= ov2680_init_cfg,
	.enum_mbus_code		= ov2680_enum_mbus_code,
	.get_fmt		= ov2680_get_fmt,
	.set_fmt		= ov2680_set_fmt,
	.enum_frame_size	= ov2680_enum_frame_size,
	.enum_frame_interval	= ov2680_enum_frame_interval,
};

static const struct v4l2_subdev_ops ov2680_subdev_ops = {
	.core	= &ov2680_core_ops,
	.video	= &ov2680_video_ops,
	.pad	= &ov2680_pad_ops,
};

static int ov2680_mode_init(struct ov2680_dev *sensor)
{
	const struct ov2680_mode_info *init_mode;

	/* set initial mode */
	sensor->fmt.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	sensor->fmt.width = 800;
	sensor->fmt.height = 600;
	sensor->fmt.field = V4L2_FIELD_NONE;
	sensor->fmt.colorspace = V4L2_COLORSPACE_SRGB;

	sensor->frame_interval.denominator = OV2680_FRAME_RATE;
	sensor->frame_interval.numerator = 1;

	init_mode = &ov2680_mode_init_data;

	sensor->current_mode = init_mode;

	sensor->mode_pending_changes = true;

	return 0;
}

static int ov2680_v4l2_register(struct ov2680_dev *sensor)
{
	const struct v4l2_ctrl_ops *ops = &ov2680_ctrl_ops;
	struct ov2680_ctrls *ctrls = &sensor->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret = 0;

	v4l2_i2c_subdev_init(&sensor->sd, sensor->i2c_client,
			     &ov2680_subdev_ops);

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sensor->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(hdl, 7);

	hdl->lock = &sensor->lock;

	ctrls->vflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP, 0, 1, 1, 0);
	ctrls->hflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP, 0, 1, 1, 0);

	ctrls->test_pattern = v4l2_ctrl_new_std_menu_items(hdl,
					&ov2680_ctrl_ops, V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(test_pattern_menu) - 1,
					0, 0, test_pattern_menu);

	ctrls->auto_exp = v4l2_ctrl_new_std_menu(hdl, ops,
						 V4L2_CID_EXPOSURE_AUTO,
						 V4L2_EXPOSURE_MANUAL, 0,
						 V4L2_EXPOSURE_AUTO);

	ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE,
					    0, 32767, 1, 0);

	ctrls->auto_gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_AUTOGAIN,
					     0, 1, 1, 1);
	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN, 0, 2047, 1, 0);

	if (hdl->error) {
		ret = hdl->error;
		goto cleanup_entity;
	}

	ctrls->gain->flags |= V4L2_CTRL_FLAG_VOLATILE;
	ctrls->exposure->flags |= V4L2_CTRL_FLAG_VOLATILE;
	ctrls->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
	ctrls->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	v4l2_ctrl_auto_cluster(2, &ctrls->auto_gain, 0, true);
	v4l2_ctrl_auto_cluster(2, &ctrls->auto_exp, 1, true);

	sensor->sd.ctrl_handler = hdl;

	ret = v4l2_async_register_subdev(&sensor->sd);
	if (ret < 0)
		goto cleanup_entity;

	return 0;

cleanup_entity:
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(hdl);

	return ret;
}

static int ov2680_get_regulators(struct ov2680_dev *sensor)
{
	int i;

	for (i = 0; i < OV2680_NUM_SUPPLIES; i++)
		sensor->supplies[i].supply = ov2680_supply_name[i];

	return devm_regulator_bulk_get(&sensor->i2c_client->dev,
				       OV2680_NUM_SUPPLIES,
				       sensor->supplies);
}

static int ov2680_check_id(struct ov2680_dev *sensor)
{
	struct device *dev = ov2680_to_dev(sensor);
	u32 chip_id;
	int ret;

	ov2680_power_on(sensor);

	ret = ov2680_read_reg16(sensor, OV2680_REG_CHIP_ID_HIGH, &chip_id);
	if (ret < 0) {
		dev_err(dev, "failed to read chip id high\n");
		return -ENODEV;
	}

	if (chip_id != OV2680_CHIP_ID) {
		dev_err(dev, "chip id: 0x%04x does not match expected 0x%04x\n",
			chip_id, OV2680_CHIP_ID);
		return -ENODEV;
	}

	return 0;
}

static int ov2680_parse_dt(struct ov2680_dev *sensor)
{
	struct device *dev = ov2680_to_dev(sensor);
	int ret;

	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	ret = PTR_ERR_OR_ZERO(sensor->reset_gpio);
	if (ret < 0) {
		dev_dbg(dev, "error while getting reset gpio: %d\n", ret);
		return ret;
	}

	sensor->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sensor->xvclk)) {
		dev_err(dev, "xvclk clock missing or invalid\n");
		return PTR_ERR(sensor->xvclk);
	}

	sensor->xvclk_freq = clk_get_rate(sensor->xvclk);
	if (sensor->xvclk_freq != OV2680_XVCLK_VALUE) {
		dev_err(dev, "wrong xvclk frequency %d HZ, expected: %d Hz\n",
			sensor->xvclk_freq, OV2680_XVCLK_VALUE);
		return -EINVAL;
	}

	return 0;
}

static int ov2680_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov2680_dev *sensor;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->i2c_client = client;

	ret = ov2680_parse_dt(sensor);
	if (ret < 0)
		return -EINVAL;

	ret = ov2680_mode_init(sensor);
	if (ret < 0)
		return ret;

	ret = ov2680_get_regulators(sensor);
	if (ret < 0) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	mutex_init(&sensor->lock);

	ret = ov2680_check_id(sensor);
	if (ret < 0)
		goto lock_destroy;

	ret = ov2680_v4l2_register(sensor);
	if (ret < 0)
		goto lock_destroy;

	dev_info(dev, "ov2680 init correctly\n");

	return 0;

lock_destroy:
	dev_err(dev, "ov2680 init fail: %d\n", ret);
	mutex_destroy(&sensor->lock);

	return ret;
}

static void ov2680_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2680_dev *sensor = to_ov2680_dev(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	mutex_destroy(&sensor->lock);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
}

static int __maybe_unused ov2680_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2680_dev *sensor = to_ov2680_dev(sd);

	if (sensor->is_streaming)
		ov2680_stream_disable(sensor);

	return 0;
}

static int __maybe_unused ov2680_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	int ret;

	if (sensor->is_streaming) {
		ret = ov2680_stream_enable(sensor);
		if (ret < 0)
			goto stream_disable;
	}

	return 0;

stream_disable:
	ov2680_stream_disable(sensor);
	sensor->is_streaming = false;

	return ret;
}

static const struct dev_pm_ops ov2680_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ov2680_suspend, ov2680_resume)
};

static const struct of_device_id ov2680_dt_ids[] = {
	{ .compatible = "ovti,ov2680" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ov2680_dt_ids);

static struct i2c_driver ov2680_i2c_driver = {
	.driver = {
		.name  = "ov2680",
		.pm = &ov2680_pm_ops,
		.of_match_table	= of_match_ptr(ov2680_dt_ids),
	},
	.probe		= ov2680_probe,
	.remove		= ov2680_remove,
};
module_i2c_driver(ov2680_i2c_driver);

MODULE_AUTHOR("Rui Miguel Silva <rui.silva@linaro.org>");
MODULE_DESCRIPTION("OV2680 CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
