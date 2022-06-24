// SPDX-License-Identifier: GPL-2.0
/*
 * thcv241 to thcv244 serdes driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 *
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
#include <linux/compat.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_graph.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define THCV244_LINK_FREQ_742MHZ	742500000UL
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define THCV244_PIXEL_RATE		(THCV244_LINK_FREQ_742MHZ * 2LL * 4LL / 8LL)
#define THCV244_XVCLK_FREQ		24000000

#define THCV244_REG_CTRL_MODE		0x1600
#define THCV244_MODE_SW_STANDBY		0x0
#define THCV244_MODE_STREAMING		0x1a

#define THCV244_ADDR			0x0b
#define THCV241_ADDR			0x34

#define REG_NULL			0xFFFF

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define THCV244_REG_VALUE_08BIT		1
#define THCV244_REG_VALUE_16BIT		2
#define THCV244_REG_VALUE_24BIT		3

#define THCV244_NAME			"thcv244"
#define THCV244_MEDIA_BUS_FMT		MEDIA_BUS_FMT_UYVY8_2X8

static const char * const thcv244_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define THCV244_NUM_SUPPLIES ARRAY_SIZE(thcv244_supply_names)

struct regval {
	u16 i2c_addr;
	u16 addr;
	u8 val;
	u16 delay;
};

struct thcv244_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_idx;
	u32 bpp;
	const struct regval *reg_list;
};

struct thcv244 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[THCV244_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	struct v4l2_fwnode_endpoint bus_cfg;
	bool			streaming;
	bool			power_on;
	bool			hot_plug;
	u8			is_reset;
	const struct thcv244_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_thcv244(sd) container_of(sd, struct thcv244, subdev)

static const struct regval thcv244_global_init_table[] = {
	{0x0b, 0x0050, 0x34, 0x00},
	{0x0b, 0x0070, 0x34, 0x00},
	{0x0b, 0x0090, 0x34, 0x00},
	{0x0b, 0x00B0, 0x34, 0x00},
	{0x0b, 0x0004, 0x03, 0x00},
	{0x0b, 0x0010, 0xF0, 0x00},
	{0x0b, 0x1704, 0x0F, 0x00},
	{0x0b, 0x0102, 0xAA, 0x00},
	{0x0b, 0x0103, 0xAA, 0x00},
	{0x0b, 0x0104, 0x00, 0x00},
	{0x0b, 0x0105, 0x00, 0x00},
	{0x0b, 0x0100, 0x03, 0x00},
	{0x0b, 0x010F, 0x25, 0x00},
	{0x0b, 0x010A, 0x15, 0x00},
	{0x0b, 0x0031, 0x02, 0x00},
	{0x0b, 0x0032, 0x10, 0x00},
	{0x0b, REG_NULL, 0x00, 0x00},
};

static const struct regval thcv244_1080p30_init_table[] = {
	{0x0b, 0x0010, 0xFF, 0x00},
	{0x0b, 0x1010, 0xA1, 0x00},
	{0x0b, 0x1011, 0x06, 0x00},
	{0x0b, 0x1014, 0xA1, 0x00},
	{0x0b, 0x1015, 0x06, 0x00},
	{0x0b, 0x1018, 0xA1, 0x00},
	{0x0b, 0x1019, 0x06, 0x00},
	{0x0b, 0x101C, 0xA1, 0x00},
	{0x0b, 0x101D, 0x06, 0x00},
	{0x0b, 0x1012, 0x00, 0x00},
	{0x0b, 0x1013, 0x01, 0x00},
	{0x0b, 0x1021, 0x28, 0x00},
	{0x0b, 0x1022, 0x02, 0x00},
	{0x0b, 0x1023, 0x11, 0x00},
	{0x0b, 0x1024, 0x00, 0x00},
	{0x0b, 0x1025, 0x00, 0x00},
	{0x0b, 0x1026, 0x00, 0x00},
	{0x0b, 0x1027, 0x07, 0x00},
	{0x0b, 0x1028, 0x00, 0x00},
	{0x0b, 0x1030, 0x18, 0x00},
	{0x0b, 0x1100, 0x01, 0x00},
	{0x0b, 0x1101, 0x01, 0x00},
	{0x0b, 0x1102, 0x01, 0x00},
	{0x0b, 0x1108, 0x01, 0x00},
	{0x0b, 0x1200, 0x01, 0x00},
	{0x0b, 0x1201, 0x01, 0x00},
	{0x0b, 0x1202, 0x01, 0x00},
	{0x0b, 0x1208, 0x01, 0x00},
	{0x0b, 0x1300, 0x01, 0x00},
	{0x0b, 0x1301, 0x01, 0x00},
	{0x0b, 0x1302, 0x01, 0x00},
	{0x0b, 0x1308, 0x01, 0x00},
	{0x0b, 0x1400, 0x01, 0x00},
	{0x0b, 0x1401, 0x01, 0x00},
	{0x0b, 0x1402, 0x01, 0x00},
	{0x0b, 0x1408, 0x01, 0x00},
	{0x0b, 0x1500, 0x01, 0x00},
	{0x0b, 0x1501, 0x0B, 0x00},
	{0x0b, 0x1502, 0x64, 0x00},
	{0x0b, 0x1504, 0x64, 0x00},
	{0x0b, 0x1506, 0x64, 0x00},
	{0x0b, 0x1508, 0x64, 0x00},
	{0x0b, 0x150B, 0xE4, 0x00},
	{0x0b, 0x150C, 0xE5, 0x00},
	{0x0b, 0x150D, 0xE6, 0x00},
	{0x0b, 0x150E, 0xE7, 0x00},
	// {0x0b, 0x1600, 0x1A, 0x00},
	{0x0b, 0x1601, 0x3B, 0x00},
	{0x0b, 0x1605, 0x2B, 0x00},
	{0x0b, 0x1606, 0x44, 0x00},
	{0x0b, 0x1609, 0x0E, 0x00},
	{0x0b, 0x160A, 0x17, 0x00},
	{0x0b, 0x160B, 0x0C, 0x00},
	{0x0b, 0x160D, 0x10, 0x00},
	{0x0b, 0x160E, 0x06, 0x00},
	{0x0b, 0x160F, 0x09, 0x00},
	{0x0b, 0x1610, 0x05, 0x00},
	{0x0b, 0x1611, 0x19, 0x00},
	{0x0b, 0x1612, 0x0D, 0x00},
	{0x0b, 0x1703, 0x01, 0x00},
	{0x0b, 0x1704, 0xFF, 0x00},
	{0x0b, 0x0032, 0x00, 0x00},
	{0x0b, 0x1003, 0x00, 0x00},
	{0x0b, 0x1004, 0x30, 0x00},
	{0x0b, 0x001B, 0x18, 0x00},
	{0x0b, 0x0032, 0x10, 0x00},
	{0x0b, 0x0040, 0x50, 0x00},
	{0x0b, 0x0041, 0x50, 0x00},
	{0x0b, 0x1005, 0x22, 0x00},
	{0x0b, 0x100C, 0x30, 0x00},
	{0x0b, 0x100D, 0x34, 0x00},
	{0x0b, REG_NULL, 0x00, 0x00},
};

static const struct regval thcv241_init_table[] = {
	{0x34, 0xF3, 0x00, 0x00},
	{0x34, 0xF2, 0x22, 0x00},
	{0x34, 0xF0, 0x03, 0x00},
	{0x34, 0xFF, 0x19, 0x00},
	{0x34, 0xF6, 0x15, 0x00},
	{0x34, 0xC9, 0x05, 0x00},
	{0x34, 0xCA, 0x05, 0x00},
	{0x34, 0xFE, 0x21, 0x00},
	{0x34, 0x76, 0x10, 0x00},
	{0x34, 0x0F, 0x01, 0x00},
	{0x34, 0x11, 0x2C, 0x00},
	{0x34, 0x12, 0x00, 0x00},
	{0x34, 0x13, 0x00, 0x00},
	{0x34, 0x14, 0x00, 0x00},
	{0x34, 0x15, 0x44, 0x00},
	{0x34, 0x16, 0x01, 0x00},
	{0x34, 0x00, 0x00, 0x00},
	{0x34, 0x01, 0x00, 0x00},
	{0x34, 0x02, 0x00, 0x00},
	{0x34, 0x55, 0x00, 0x00},
	{0x34, 0x04, 0x00, 0x00},
	{0x34, 0x2B, 0x05, 0x00},
	{0x34, 0x2F, 0x00, 0x00},
	{0x34, 0x2D, 0x13, 0x00},
	{0x34, 0x2C, 0x01, 0x00},
	{0x34, 0x05, 0x01, 0x00},
	{0x34, 0x06, 0x01, 0x00},
	{0x34, 0x27, 0x00, 0x00},
	{0x34, 0x1D, 0x00, 0x00},
	{0x34, 0x1E, 0x00, 0x00},
	{0x34, 0x3D, 0x02, 0x00},
	{0x34, 0x3E, 0x10, 0x00},
	{0x34, 0x3F, 0x03, 0x00},
	{0x34, REG_NULL, 0x00, 0x00},
};

static const struct regval thcv244_reset_init_table[] = {
	{0x0b, 0x1702, 0x01, 0x00},
	{0x0b, 0x1600, 0x00, 0x00},
	{0x0b, 0x1703, 0x00, 0x00},
	{0x0b, 0x1704, 0x00, 0x00},
	{0x0b, 0x1701, 0xFD, 0x00},
	{0x0b, 0x0001, 0x01, 0x50},
	{0x0b, 0x0050, 0x34, 0x00},
	{0x0b, 0x0070, 0x34, 0x00},
	{0x0b, 0x0090, 0x34, 0x00},
	{0x0b, 0x00B0, 0x34, 0x00},
	{0x0b, 0x0004, 0x03, 0x00},
	{0x0b, 0x0010, 0xF0, 0x00},
	{0x0b, 0x1704, 0x01, 0x00},
	{0x0b, 0x0102, 0xAA, 0x00},
	{0x0b, 0x0103, 0xAA, 0x00},
	{0x0b, 0x0104, 0x00, 0x00},
	{0x0b, 0x0105, 0x00, 0x00},
	{0x0b, 0x0100, 0x03, 0x00},
	{0x0b, 0x010F, 0x25, 0x00},
	{0x0b, 0x010A, 0x15, 0x00},
	{0x0b, 0x0031, 0x02, 0x00},
	{0x0b, 0x0032, 0x00, 0x00},
	{0x0b, REG_NULL, 0x00, 0x00},
};

static const struct regval thcv241_reset_init_table[] = {
	{0x34, 0xFE, 0x21, 0x00},
	{0x34, 0x06, 0x00, 0x00},
	{0x34, 0x05, 0x00, 0x00},
	{0x34, 0x21, 0x00, 0x00},
	{0x34, 0x22, 0x00, 0x00},
	{0x34, 0x23, 0x00, 0x00},
	{0x34, 0xFF, 0xAA, 0x00},
	{0x0b, REG_NULL, 0x00, 0x00},
};

static const struct regval thcv241_reset_init_table1[] = {
	{0x34, 0xF3, 0x00, 0x00},
	{0x34, 0xF2, 0x22, 0x00},
	{0x34, 0xF0, 0x03, 0x00},
	{0x34, 0xFF, 0x19, 0x00},
	{0x34, 0xF6, 0x15, 0x00},
	{0x34, 0xFE, 0x21, 0x00},
	{0x34, 0x2D, 0x03, 0x00},
	{0x34, 0x2C, 0x00, 0x00},
	{0x34, 0x21, 0x01, 0x00},
	{0x34, 0x22, 0x01, 0x00},
	{0x34, 0x23, 0x01, 0x00},
	{0x34, 0xFE, 0x00, 0x00},
	{0x0b, REG_NULL, 0x00, 0x00},
};

static const struct thcv244_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.link_freq_idx = 0,
	},
};

static const s64 link_freq_items[] = {
	THCV244_LINK_FREQ_742MHZ,
};

/* Write registers up to 4 at a time */
static int thine_write_reg(struct i2c_client *client, u16 client_addr, u16 reg,
			    u32 reg_len, u32 val_len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	if (val_len > 4)
		return -EINVAL;
	if (reg_len == 2) {
		buf[0] = reg >> 8;
		buf[1] = reg & 0xff;
	} else {
		buf[0] = reg & 0xff;
	}
	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	if (reg_len == 2) {
		buf_i = 2;
		val_i = 4 - val_len;
	} else {
		buf_i = 1;
		val_i = 4 - val_len;
	}
	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];
	client->addr = client_addr;

	if (i2c_master_send(client, buf, val_len + reg_len) != val_len + reg_len) {
		dev_err(&client->dev,
			"%s, i2c_master_send err, client->addr = 0x%x, reg = 0x%x, val = 0x%x\n",
			__func__, client->addr, reg, val);
		return -EIO;
	}

	dev_dbg(&client->dev,
		"%s, i2c_master_send ok, client->addr = 0x%x, reg = 0x%x, val = 0x%x\n",
		__func__, client->addr, reg, val);

	return 0;
}

static int thcv244_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = thine_write_reg(client, THCV244_ADDR, regs[i].addr, 2,
					THCV244_REG_VALUE_08BIT,
					regs[i].val);
		msleep(regs[i].delay);
	}

	return ret;
}

static int thcv241_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = thine_write_reg(client, THCV241_ADDR, regs[i].addr, 1,
					THCV244_REG_VALUE_08BIT,
					regs[i].val);
		msleep(regs[i].delay);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int __maybe_unused thcv244_read_reg(struct i2c_client *client, u16 reg,
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

static int thcv244_get_reso_dist(const struct thcv244_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct thcv244_mode *
thcv244_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = thcv244_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int thcv244_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct thcv244 *thcv244 = to_thcv244(sd);
	const struct thcv244_mode *mode;

	mutex_lock(&thcv244->mutex);

	mode = thcv244_find_best_fit(fmt);
	fmt->format.code = THCV244_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&thcv244->mutex);
		return -ENOTTY;
#endif
	} else {
		if (thcv244->streaming) {
			mutex_unlock(&thcv244->mutex);
			return -EBUSY;
		}
	}

	mutex_unlock(&thcv244->mutex);

	return 0;
}

static int thcv244_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct thcv244 *thcv244 = to_thcv244(sd);
	const struct thcv244_mode *mode = thcv244->cur_mode;

	mutex_lock(&thcv244->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&thcv244->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = THCV244_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&thcv244->mutex);

	return 0;
}

static int thcv244_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = THCV244_MEDIA_BUS_FMT;

	return 0;
}

static int thcv244_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != THCV244_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int thcv244_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct thcv244 *thcv244 = to_thcv244(sd);
	const struct thcv244_mode *mode = thcv244->cur_mode;

	mutex_lock(&thcv244->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&thcv244->mutex);

	return 0;
}

static void thcv244_get_module_inf(struct thcv244 *thcv244,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, THCV244_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, thcv244->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, thcv244->len_name, sizeof(inf->base.lens));
}

static void thcv244_get_vicap_rst_inf(struct thcv244 *thcv244,
				   struct rkmodule_vicap_reset_info *rst_info)
{
	struct i2c_client *client = thcv244->client;

	rst_info->is_reset = thcv244->hot_plug;
	thcv244->hot_plug = false;
	rst_info->src = RKCIF_RESET_SRC_ERR_HOTPLUG;
	if (rst_info->is_reset)
		dev_info(&client->dev, "%s: rst_info->is_reset:%d.\n",
			__func__, rst_info->is_reset);
}

static void thcv244_set_vicap_rst_inf(struct thcv244 *thcv244,
				   struct rkmodule_vicap_reset_info rst_info)
{
	thcv244->is_reset = rst_info.is_reset;
}

static long thcv244_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct thcv244 *thcv244 = to_thcv244(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		thcv244_get_module_inf(thcv244, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = thine_write_reg(thcv244->client, THCV244_ADDR,
				 THCV244_REG_CTRL_MODE, 2,
				 THCV244_REG_VALUE_08BIT,
				 THCV244_MODE_STREAMING);
		else
			ret = thine_write_reg(thcv244->client, THCV244_ADDR,
				 THCV244_REG_CTRL_MODE, 2,
				 THCV244_REG_VALUE_08BIT,
				 THCV244_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		thcv244_get_vicap_rst_inf(thcv244,
			(struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		thcv244_set_vicap_rst_inf(thcv244,
			*(struct rkmodule_vicap_reset_info *)arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long thcv244_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_vicap_reset_info *vicap_rst_inf;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = thcv244_ioctl(sd, cmd, inf);
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
			ret = thcv244_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = thcv244_ioctl(sd, cmd, vicap_rst_inf);
		if (!ret) {
			ret = copy_to_user(up, vicap_rst_inf, sizeof(*vicap_rst_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(vicap_rst_inf, up, sizeof(*vicap_rst_inf));
		if (!ret)
			ret = thcv244_ioctl(sd, cmd, vicap_rst_inf);
		else
			ret = -EFAULT;
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = thcv244_ioctl(sd, cmd, &stream);
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

static int thcv244_thcv241_init(struct thcv244 *thcv244)
{
	struct device *dev = &thcv244->client->dev;
	int ret;

	ret = thcv244_write_array(thcv244->client, thcv244_global_init_table);
	ret |= thine_write_reg(thcv244->client, THCV241_ADDR, 0x00fe,
					2, THCV244_REG_VALUE_08BIT, 0x11);
	ret |= thine_write_reg(thcv244->client, THCV244_ADDR, 0x0032,
					2, THCV244_REG_VALUE_08BIT, 0x00);
	ret |= thcv241_write_array(thcv244->client, thcv241_init_table);
	ret |= thcv244_write_array(thcv244->client, thcv244_1080p30_init_table);
	ret |= thine_write_reg(thcv244->client, THCV244_ADDR, 0x0032,
					2, THCV244_REG_VALUE_08BIT, 0x00);
	ret |= thine_write_reg(thcv244->client, THCV241_ADDR, 0xfe,
					1, THCV244_REG_VALUE_08BIT, 0x21);
	ret |= thine_write_reg(thcv244->client, THCV241_ADDR, 0x3e,
					1, THCV244_REG_VALUE_08BIT, 0x00);
	msleep(200);
	ret |= thine_write_reg(thcv244->client, THCV241_ADDR, 0x3e,
					1, THCV244_REG_VALUE_08BIT, 0x10);
	ret |= thine_write_reg(thcv244->client, THCV244_ADDR, 0x1600,
					2, THCV244_REG_VALUE_08BIT, 0x00);
	if (ret)
		dev_err(dev, "fail to init thcv244 and thcv 241!\n");

	return ret;
}

static int thcv244_thcv241_reset_initial(struct thcv244 *thcv244)
{
	struct device *dev = &thcv244->client->dev;
	int ret;

	ret = thcv244_write_array(thcv244->client, thcv244_reset_init_table);

	ret |= thcv241_write_array(thcv244->client, thcv241_reset_init_table);
	ret |= thine_write_reg(thcv244->client, THCV244_ADDR, 0x0032,
					2, THCV244_REG_VALUE_08BIT, 0x10);
	ret |= thine_write_reg(thcv244->client, THCV241_ADDR, 0x00fe,
					1, THCV244_REG_VALUE_08BIT, 0x11);
	ret |= thine_write_reg(thcv244->client, THCV244_ADDR, 0x0032,
					2, THCV244_REG_VALUE_08BIT, 0x00);
	ret |= thcv241_write_array(thcv244->client, thcv241_reset_init_table1);

	if (ret)
		dev_err(dev, "fail to reset thcv244 and thcv 241!\n");

	return ret;
}

static int __thcv244_start_stream(struct thcv244 *thcv244)
{
	int ret;

	ret = thcv244_thcv241_reset_initial(thcv244);

	ret |= thcv244_thcv241_init(thcv244);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&thcv244->mutex);
	ret = v4l2_ctrl_handler_setup(&thcv244->ctrl_handler);
	mutex_lock(&thcv244->mutex);
	if (ret)
		return ret;

	return thine_write_reg(thcv244->client, THCV244_ADDR,
					THCV244_REG_CTRL_MODE, 2,
					THCV244_REG_VALUE_08BIT,
					THCV244_MODE_STREAMING);
}

static int __thcv244_stop_stream(struct thcv244 *thcv244)
{
	return thine_write_reg(thcv244->client, THCV244_ADDR,
					THCV244_REG_CTRL_MODE, 2,
					THCV244_REG_VALUE_08BIT,
					THCV244_MODE_SW_STANDBY);
}

static int thcv244_s_stream(struct v4l2_subdev *sd, int on)
{
	struct thcv244 *thcv244 = to_thcv244(sd);
	struct i2c_client *client = thcv244->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				thcv244->cur_mode->width,
				thcv244->cur_mode->height,
		DIV_ROUND_CLOSEST(thcv244->cur_mode->max_fps.denominator,
				  thcv244->cur_mode->max_fps.numerator));

	mutex_lock(&thcv244->mutex);
	on = !!on;
	if (on == thcv244->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __thcv244_start_stream(thcv244);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__thcv244_stop_stream(thcv244);
		pm_runtime_put(&client->dev);
	}

	thcv244->streaming = on;

unlock_and_return:
	mutex_unlock(&thcv244->mutex);

	return ret;
}

static int thcv244_s_power(struct v4l2_subdev *sd, int on)
{
	struct thcv244 *thcv244 = to_thcv244(sd);
	struct i2c_client *client = thcv244->client;
	int ret = 0;

	mutex_lock(&thcv244->mutex);

	/* If the power state is not modified - no work to do. */
	if (thcv244->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		thcv244->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		thcv244->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&thcv244->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 thcv244_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, THCV244_XVCLK_FREQ / 1000 / 1000);
}

static int __thcv244_power_on(struct thcv244 *thcv244)
{
	int ret;
	u32 delay_us;
	struct device *dev = &thcv244->client->dev;

	if (!IS_ERR(thcv244->power_gpio))
		gpiod_set_value_cansleep(thcv244->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(thcv244->pins_default)) {
		ret = pinctrl_select_state(thcv244->pinctrl,
					   thcv244->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	if (!IS_ERR(thcv244->reset_gpio))
		gpiod_set_value_cansleep(thcv244->reset_gpio, 0);

	ret = regulator_bulk_enable(THCV244_NUM_SUPPLIES, thcv244->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(thcv244->reset_gpio))
		gpiod_set_value_cansleep(thcv244->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(thcv244->pwdn_gpio))
		gpiod_set_value_cansleep(thcv244->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = thcv244_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(thcv244->xvclk);

	return ret;
}

static void __thcv244_power_off(struct thcv244 *thcv244)
{
	int ret;
	struct device *dev = &thcv244->client->dev;

	if (!IS_ERR(thcv244->pwdn_gpio))
		gpiod_set_value_cansleep(thcv244->pwdn_gpio, 0);
	clk_disable_unprepare(thcv244->xvclk);
	if (!IS_ERR(thcv244->reset_gpio))
		gpiod_set_value_cansleep(thcv244->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(thcv244->pins_sleep)) {
		ret = pinctrl_select_state(thcv244->pinctrl,
					   thcv244->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(thcv244->power_gpio))
		gpiod_set_value_cansleep(thcv244->power_gpio, 0);

	regulator_bulk_disable(THCV244_NUM_SUPPLIES, thcv244->supplies);
}

static int thcv244_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct thcv244 *thcv244 = to_thcv244(sd);

	return __thcv244_power_on(thcv244);
}

static int thcv244_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct thcv244 *thcv244 = to_thcv244(sd);

	__thcv244_power_off(thcv244);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int thcv244_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct thcv244 *thcv244 = to_thcv244(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct thcv244_mode *def_mode = &supported_modes[0];

	mutex_lock(&thcv244->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = THCV244_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&thcv244->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int thcv244_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != THCV244_MEDIA_BUS_FMT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;

	return 0;
}

static int thcv244_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *config)
{
	struct thcv244 *thcv244 = to_thcv244(sd);
	u32 lane_num = thcv244->bus_cfg.bus.mipi_csi2.num_data_lanes;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = 1 << (lane_num - 1) |
			V4L2_MBUS_CSI2_CHANNELS |
			V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	return 0;
}

static int thcv244_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct thcv244 *thcv244 = to_thcv244(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = thcv244->cur_mode->width;
		sel->r.top = 0;
		sel->r.height = thcv244->cur_mode->height;
		return 0;
	}

	return -EINVAL;
}

static const struct dev_pm_ops thcv244_pm_ops = {
	SET_RUNTIME_PM_OPS(thcv244_runtime_suspend,
			   thcv244_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops thcv244_internal_ops = {
	.open = thcv244_open,
};
#endif

static const struct v4l2_subdev_core_ops thcv244_core_ops = {
	.s_power = thcv244_s_power,
	.ioctl = thcv244_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = thcv244_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops thcv244_video_ops = {
	.s_stream = thcv244_s_stream,
	.g_frame_interval = thcv244_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops thcv244_pad_ops = {
	.enum_mbus_code = thcv244_enum_mbus_code,
	.enum_frame_size = thcv244_enum_frame_sizes,
	.enum_frame_interval = thcv244_enum_frame_interval,
	.get_fmt = thcv244_get_fmt,
	.set_fmt = thcv244_set_fmt,
	.get_selection = thcv244_get_selection,
	.get_mbus_config = thcv244_g_mbus_config,
};

static const struct v4l2_subdev_ops thcv244_subdev_ops = {
	.core	= &thcv244_core_ops,
	.video	= &thcv244_video_ops,
	.pad	= &thcv244_pad_ops,
};

static int thcv244_initialize_controls(struct thcv244 *thcv244)
{
	const struct thcv244_mode *mode;
	struct v4l2_ctrl_handler *handler;
	int ret;

	handler = &thcv244->ctrl_handler;
	mode = thcv244->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 2);
	if (ret)
		return ret;
	handler->lock = &thcv244->mutex;

	thcv244->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			1, 0, link_freq_items);

	thcv244->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, THCV244_PIXEL_RATE,
			1, THCV244_PIXEL_RATE);

	__v4l2_ctrl_s_ctrl(thcv244->link_freq,
			   mode->link_freq_idx);

	if (handler->error) {
		ret = handler->error;
		dev_err(&thcv244->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	thcv244->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int thcv244_check_sensor_id(struct thcv244 *thcv244,
				   struct i2c_client *client)
{
	return 0;
}

static int thcv244_configure_regulators(struct thcv244 *thcv244)
{
	unsigned int i;

	for (i = 0; i < THCV244_NUM_SUPPLIES; i++)
		thcv244->supplies[i].supply = thcv244_supply_names[i];

	return devm_regulator_bulk_get(&thcv244->client->dev,
					THCV244_NUM_SUPPLIES,
					thcv244->supplies);
}

static int thcv244_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct thcv244 *thcv244;
	struct v4l2_subdev *sd;
	struct device_node *endpoint;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	thcv244 = devm_kzalloc(dev, sizeof(*thcv244), GFP_KERNEL);
	if (!thcv244)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &thcv244->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &thcv244->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &thcv244->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &thcv244->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	thcv244->client = client;
	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint),
		&thcv244->bus_cfg);
	if (ret) {
		dev_err(dev, "Failed to get bus cfg\n");
		return ret;
	}

	thcv244->cur_mode = &supported_modes[0];

	thcv244->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(thcv244->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	thcv244->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(thcv244->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	thcv244->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(thcv244->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = thcv244_configure_regulators(thcv244);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	thcv244->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(thcv244->pinctrl)) {
		thcv244->pins_default =
			pinctrl_lookup_state(thcv244->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(thcv244->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		thcv244->pins_sleep =
			pinctrl_lookup_state(thcv244->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(thcv244->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&thcv244->mutex);

	sd = &thcv244->subdev;
	v4l2_i2c_subdev_init(sd, client, &thcv244_subdev_ops);
	ret = thcv244_initialize_controls(thcv244);
	if (ret)
		goto err_destroy_mutex;

	ret = __thcv244_power_on(thcv244);
	if (ret)
		goto err_free_handler;

	ret = thcv244_check_sensor_id(thcv244, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &thcv244_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	thcv244->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &thcv244->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(thcv244->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 thcv244->module_index, facing,
		 THCV244_NAME, dev_name(sd->dev));
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
	__thcv244_power_off(thcv244);
err_free_handler:
	v4l2_ctrl_handler_free(&thcv244->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&thcv244->mutex);

	return ret;
}

static int thcv244_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct thcv244 *thcv244 = to_thcv244(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&thcv244->ctrl_handler);
	mutex_destroy(&thcv244->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__thcv244_power_off(thcv244);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id thcv244_of_match[] = {
	{ .compatible = "thine,thcv244" },
	{},
};
MODULE_DEVICE_TABLE(of, thcv244_of_match);
#endif

static const struct i2c_device_id thcv244_match_id[] = {
	{ "thine,thcv244", 0 },
	{},
};

static struct i2c_driver thcv244_i2c_driver = {
	.driver = {
		.name = THCV244_NAME,
		.pm = &thcv244_pm_ops,
		.of_match_table = of_match_ptr(thcv244_of_match),
	},
	.probe		= &thcv244_probe,
	.remove		= &thcv244_remove,
	.id_table	= thcv244_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&thcv244_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&thcv244_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Thine thcv244 sensor driver");
MODULE_LICENSE("GPL");
