// SPDX-License-Identifier: GPL-2.0
/*
 * jx_f37 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 * v0.0x01.0x04 support mirror/flip
 */

#define DEBUG
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/rk-camera-module.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/version.h>
#include <linux/rk-preisp.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define JX_F37_XVCLK_FREQ		24000000

#define JX_F37_LANES			1

#define CHIP_ID_H			0x0F
#define CHIP_ID_L			0x37
#define JX_F37_PIDH_ADDR		0x0a
#define JX_F37_PIDL_ADDR		0x0b

#define JX_F37_REG_CTRL_MODE		0x12
#define JX_F37_MODE_SLEEP_MODE		BIT(6)


#define JX_F37_MAX_SMPL_START		0x8f

#define JX_F37_SHORT_EXPO_REG		0x05	/* Exposure Bits 8-15 */

#define JX_F37_LONG_EXPO_HIGH_REG	0x02	/* Exposure Bits 8-15 */
#define JX_F37_LONG_EXPO_LOW_REG	0x01	/* Exposure Bits 0-7 */
#define JX_F37_FETCH_HIGH_BYTE_EXP(VAL) (((VAL) >> 8) & 0xFF)	/* 8-15 Bits */
#define JX_F37_FETCH_LOW_BYTE_EXP(VAL) ((VAL) & 0xFF)	/* 0-7 Bits */
#define	JX_F37_EXPOSURE_MIN		4
#define	JX_F37_EXPOSURE_STEP		1
#define JX_F37_VTS_MAX			0xffff

#define JX_F37_SMPL_START_S_REG		0x06
#define JX_F37_SMPL_START_S_VAL		0x23

#define JX_F37_LONG_GAIN_REG		0x00	/* Bits 0 -7 */
#define	ANALOG_GAIN_MIN			0x00
#define	ANALOG_GAIN_MAX			0x3f
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x0

#define JX_F37_REG_HIGH_VTS			0x23
#define JX_F37_REG_LOW_VTS			0X22
#define JX_F37_FETCH_HIGH_BYTE_VTS(VAL) (((VAL) >> 8) & 0xFF)	/* 8-15 Bits */
#define JX_F37_FETCH_LOW_BYTE_VTS(VAL) ((VAL) & 0xFF)	/* 0-7 Bits */

#define JX_F37_FLIP_MIRROR_REG		0x12

#define REG_NULL			0xFF
#define REG_DELAY			0xFE

#define JX_F37_NAME			"jx_f37"

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define USED_SYS_DEBUG

static const char * const jx_f37_supply_names[] = {
	"vcc2v8_dvp",		/* Analog power */
	"vcc1v8_dvp",		/* Digital I/O power */
};

#define JX_F37_NUM_SUPPLIES ARRAY_SIZE(jx_f37_supply_names)

struct regval {
	u8 addr;
	u8 val;
};

enum jx_f37_max_pad {
	PAD0, /* link to isp */
	PAD1, /* link to csi wr0 | hdr x2:L x3:M */
	PAD2, /* link to csi wr1 | hdr      x3:L */
	PAD3, /* link to csi wr2 | hdr x2:M x3:S */
	PAD_MAX,
};

struct jx_f37_mode {
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

struct jx_f37 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[JX_F37_NUM_SUPPLIES];
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
	const struct jx_f37_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;

	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_jx_f37(sd) container_of(sd, struct jx_f37, subdev)

static const struct regval jx_f37_1080p_linear_1lane_30fps[] = {
	{0x12, 0x60},
	{0x48, 0x85},
	{0x48, 0x05},
	{0x0E, 0x11},
	{0x0F, 0x14},
	{0x10, 0x48},
	{0x11, 0x80},
	{0x0D, 0xF0},
	{0x5F, 0x41},
	{0x60, 0x20},
	{0x58, 0x12},
	{0x57, 0x60},
	{0x9D, 0x00},
	{0x20, 0x00},
	{0x21, 0x05},
	{0x22, 0x65},
	{0x23, 0x04},
	{0x24, 0xC0},
	{0x25, 0x38},
	{0x26, 0x43},
	{0x27, 0x9A},
	{0x28, 0x15},
	{0x29, 0x04},
	{0x2A, 0x8A},
	{0x2B, 0x14},
	{0x2C, 0x00},
	{0x2D, 0x00},
	{0x2E, 0x14},
	{0x2F, 0x44},
	{0x41, 0xC8},
	{0x42, 0x3B},
	{0x47, 0x42},
	{0x76, 0x60},
	{0x77, 0x09},
	{0x1D, 0x00},
	{0x1E, 0x04},
	{0x6C, 0x50},
	{0x6E, 0x2C},
	{0x70, 0xD0},
	{0x71, 0xD3},
	{0x72, 0xD4},
	{0x73, 0x58},
	{0x74, 0x02},
	{0x78, 0x96},
	{0x89, 0x01},
	{0x6B, 0x20},
	{0x86, 0x40},
	{0x31, 0x08},
	{0x32, 0x27},
	{0x33, 0x60},
	{0x34, 0x5E},
	{0x35, 0x5E},
	{0x3A, 0xAF},
	{0x3B, 0x00},
	{0x3C, 0x48},
	{0x3D, 0x5B},
	{0x3E, 0xFF},
	{0x3F, 0xA8},
	{0x40, 0xFF},
	{0x56, 0xB2},
	{0x59, 0x9E},
	{0x5A, 0x04},
	{0x85, 0x4D},
	{0x8A, 0x04},
	{0x91, 0x13},
	{0x9B, 0x03},
	{0x9C, 0xE1},
	{0xA9, 0x78},
	{0x5B, 0xB0},
	{0x5C, 0x71},
	{0x5D, 0x46},
	{0x5E, 0x14},
	{0x62, 0x01},
	{0x63, 0x0F},
	{0x64, 0xC0},
	{0x65, 0x02},
	{0x67, 0x65},
	{0x66, 0x04},
	{0x68, 0x00},
	{0x69, 0x7C},
	{0x6A, 0x12},
	{0x7A, 0x80},
	{0x82, 0x21},
	{0x8F, 0x91},
	{0xAE, 0x30},
	{0x13, 0x81},
	{0x96, 0x04},
	{0x4A, 0x05},
	{0x7E, 0xCD},
	{0x50, 0x02},
	{0x49, 0x10},
	{0xAF, 0x12},
	{0x80, 0x41},
	{0x7B, 0x4A},
	{0x7C, 0x08},
	{0x7F, 0x57},
	{0x90, 0x00},
	{0x8C, 0xFF},
	{0x8D, 0xC7},
	{0x8E, 0x00},
	{0x8B, 0x01},
	{0x0C, 0x00},
	{0x81, 0x74},
	{0x19, 0x20},
	{0x46, 0x00},
	{0x12, 0x20},
	{0x48, 0x85},
	{0x48, 0x05},
	{REG_NULL, 0x0},
};

static const struct regval jx_f37_1080p_hdr_1lane_15fps[] = {
	{0x12, 0x68},
	{0x48, 0x85},
	{0x48, 0x05},
	{0x0E, 0x11},
	{0x0F, 0x14},
	{0x10, 0x48},
	{0x11, 0x80},
	{0x0D, 0xF0},
	{0x5F, 0x41},
	{0x60, 0x20},
	{0x58, 0x12},
	{0x57, 0x60},
	{0x9D, 0x00},
	{0x20, 0x00},
	{0x21, 0x05},
	{0x22, 0xCA},
	{0x23, 0x08},
	{0x24, 0xC0},
	{0x25, 0x38},
	{0x26, 0x43},
	{0x27, 0x98},
	{0x28, 0x29},
	{0x29, 0x04},
	{0x2A, 0x8A},
	{0x2B, 0x14},
	{0x2C, 0x02},
	{0x2D, 0x00},
	{0x2E, 0x14},
	{0x2F, 0x44},
	{0x41, 0xC5},
	{0x42, 0x3B},
	{0x47, 0x42},
	{0x76, 0x60},
	{0x77, 0x09},
	{0x80, 0x41},
	{0xAF, 0x22},
	{0xAB, 0x00},
	{0x46, 0x14}, /* Short frame use the same gain as long frame */
	{0x1D, 0x00},
	{0x1E, 0x04},
	{0x6C, 0x50},
	{0x6E, 0x2C},
	{0x70, 0xD0},
	{0x71, 0xD3},
	{0x72, 0xD4},
	{0x73, 0x58},
	{0x74, 0x02},
	{0x78, 0x96},
	{0x89, 0x81},
	{0x6B, 0x20},
	{0x86, 0x40},
	{0x31, 0x08},
	{0x32, 0x27},
	{0x33, 0x60},
	{0x34, 0x5E},
	{0x35, 0x5E},
	{0x3A, 0xAF},
	{0x3B, 0x00},
	{0x3C, 0x48},
	{0x3D, 0x5B},
	{0x3E, 0xFF},
	{0x3F, 0xA8},
	{0x40, 0xFF},
	{0x56, 0xB2},
	{0x59, 0x9E},
	{0x5A, 0x04},
	{0x85, 0x4D},
	{0x8A, 0x04},
	{0x91, 0x13},
	{0x9B, 0x43},
	{0x9C, 0xE1},
	{0xA9, 0x78},
	{0x5B, 0xB0},
	{0x5C, 0x71},
	{0x5D, 0xF6},
	{0x5E, 0x14},
	{0x62, 0x01},
	{0x63, 0x0F},
	{0x64, 0xC0},
	{0x65, 0x02},
	{0x67, 0x65},
	{0x66, 0x04},
	{0x68, 0x00},
	{0x69, 0x7C},
	{0x6A, 0x12},
	{0x7A, 0x80},
	{0x82, 0x21},
	{0x8F, 0x91},
	{0xAE, 0x30},
	{0x13, 0x81},
	{0x96, 0x04},
	{0x4A, 0x05},
	{0x7E, 0xCD},
	{0x50, 0x02},
	{0x49, 0x10},
	{0xAF, 0x12},
	{0x7B, 0x4A},
	{0x7C, 0x08},
	{0x7F, 0x57},
	{0x90, 0x00},
	{0x8C, 0xFF},
	{0x8D, 0xC7},
	{0x8E, 0x00},
	{0x8B, 0x01},
	{0x0C, 0x00},
	{0x81, 0x74},
	{0x19, 0x20},
	{0x07, 0x03},
	{0x1B, 0x4F},
	{0x06, JX_F37_MAX_SMPL_START},
	{0x03, 0xFF},
	{0x04, 0xFF},
	{0x12, 0x28},
	{0x48, 0x85},
	{0x48, 0x05},
	{REG_NULL, 0x0},
};

static const struct jx_f37_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x00ff,
		.hts_def = 0x0500 * 2,
		.vts_def = 0x0465,
		.reg_list = jx_f37_1080p_linear_1lane_30fps,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.exp_def = 0x00ff,
		.hts_def = 0x0500 * 2,
		.vts_def = 0x08ca,
		.reg_list = jx_f37_1080p_hdr_1lane_15fps,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

#define JX_F37_LINK_FREQ_432MHZ		(432000000)
#define JX_F37_PIXEL_RATE	(JX_F37_LINK_FREQ_432MHZ * 2 * JX_F37_LANES / 10)
static const s64 link_freq_menu_items[] = {
	JX_F37_LINK_FREQ_432MHZ
};

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 jx_f37_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, JX_F37_XVCLK_FREQ / 1000 / 1000);
}

static int jx_f37_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr =  client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0) {
		//dev_dbg(&client->dev,
		//	"jx_f37 write reg(0x%x val:0x%x)\n", reg, val);
		return 0;
	}

	dev_err(&client->dev,
		"jx_f37 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int jx_f37_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i, delay_us;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (regs[i].addr == REG_DELAY) {
			delay_us = jx_f37_cal_delay(500 * 1000);
			usleep_range(delay_us, delay_us * 2);
		} else {
			ret = jx_f37_write_reg(client,
				regs[i].addr, regs[i].val);
		}
	}

	return ret;
}

static int jx_f37_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"jx_f37 read reg:0x%x failed !\n", reg);

	return ret;
}

static void jx_f37_update_cur_mode_locked(struct jx_f37 *jx_f37,
					  const struct jx_f37_mode *mode)
{
	s64 h_blank, vblank_def;

	jx_f37->cur_mode = mode;
	h_blank = mode->hts_def - mode->width;
	__v4l2_ctrl_modify_range(jx_f37->hblank, h_blank,
				 h_blank, 1, h_blank);
	vblank_def = mode->vts_def - mode->height;
	__v4l2_ctrl_modify_range(jx_f37->vblank, vblank_def,
				 JX_F37_VTS_MAX - mode->height,
				 1, vblank_def);
}

static int jx_f37_set_hdr_mode_locked(struct jx_f37 *jx_f37, u32 hdr_mode)
{
	int i;

	/*
	 * found the first one that match hdr_mode,
	 * the fmt size shall hand over to .set_fmt.
	 */
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (supported_modes[i].hdr_mode != hdr_mode)
			continue;
		jx_f37_update_cur_mode_locked(jx_f37, &supported_modes[i]);
		return 0;
	}

	return -EINVAL;
}

#ifdef USED_SYS_DEBUG
static ssize_t set_hdr_mode(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_f37 *jx_f37 = to_jx_f37(sd);
	int status = 0;
	int ret;

	mutex_lock(&jx_f37->mutex);

	ret = kstrtoint(buf, 0, &status);
	if (!ret) {
		ret = jx_f37_set_hdr_mode_locked(jx_f37, status);
		if (ret)
			dev_err(dev, "hdr_mode(%d) is not supported\n", status);
		else
			dev_info(dev, "Set hdr mode to: %d\n", status);
	}

	mutex_unlock(&jx_f37->mutex);

	return count;
}

static ssize_t show_hdr_mode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_f37 *jx_f37 = to_jx_f37(sd);

	return sprintf(buf, "%u\n", jx_f37->cur_mode->hdr_mode);
}

static struct device_attribute attributes[] = {
	__ATTR(cam_hdr_mode, 0600, show_hdr_mode, set_hdr_mode),
};

static int add_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		if (device_create_file(dev, attributes + i))
			goto undo;
	return 0;
undo:
	for (i--; i >= 0 ; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}
#endif


static int jx_f37_get_reso_dist(const struct jx_f37_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct jx_f37_mode *
jx_f37_find_best_fit(struct jx_f37 *jx_f37, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	u32 cur_hdr_mode = jx_f37->cur_mode->hdr_mode;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		/* Do not change the hdr_mode setting */
		if (supported_modes[i].hdr_mode != cur_hdr_mode)
			continue;
		dist = jx_f37_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int jx_f37_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct jx_f37 *jx_f37 = to_jx_f37(sd);
	const struct jx_f37_mode *mode;

	mutex_lock(&jx_f37->mutex);

	mode = jx_f37_find_best_fit(jx_f37, fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&jx_f37->mutex);
		return -ENOTTY;
#endif
	} else {
		jx_f37_update_cur_mode_locked(jx_f37, mode);
	}

	mutex_unlock(&jx_f37->mutex);

	return 0;
}

static int jx_f37_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct jx_f37 *jx_f37 = to_jx_f37(sd);
	const struct jx_f37_mode *mode = jx_f37->cur_mode;

	mutex_lock(&jx_f37->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&jx_f37->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&jx_f37->mutex);

	return 0;
}

static int jx_f37_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int jx_f37_enum_frame_sizes(struct v4l2_subdev *sd,
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

static void jx_f37_get_module_inf(struct jx_f37 *jx_f37,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, JX_F37_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, jx_f37->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, jx_f37->len_name, sizeof(inf->base.lens));
}

static int jx_f37_set_hdrae(struct jx_f37 *jx_f37,
			    struct preisp_hdrae_exp_s *ae)
{
	struct i2c_client *client = jx_f37->client;
	u32 fh, l_exp_max, l_exp_min, s_exp_max, s_exp_min;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	int ret = 0;

	if (!jx_f37->has_init_exp && !jx_f37->streaming) {
		jx_f37->init_hdrae_exp = *ae;
		jx_f37->has_init_exp = true;
		dev_dbg(&client->dev, "jx_f37 don't stream, record exp for hdr!\n");
		return ret;
	}

	/* The frame height, vts, default value is: 0x08ca */
	fh = jx_f37->vblank->cur.val + jx_f37->cur_mode->height;
	/*
	 * Restriction:
	 *  Short / Long exp line shall be odd value.
	 *
	 *   0x00 <=  Reg_saec1 * 2 <  Smpl_Start_S * 2 - 3
	 *   0x00 <=  Reg_saec1     <= Smpl_Start_S - 3
	 *
	 *   0x01 <= short_exp = Reg_saec1 * 2 + 1 <= Smpl_Start_S * 2 - 5
	 *
	 *   0x01 <= long_exp  < fh - Smpl_Start_S * 2 - 6
	 *
	 *   short_exp + long_exp < fh - 11
	 */

	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_a_gain = ae->long_gain_reg;
	m_a_gain = ae->middle_gain_reg;
	s_a_gain = ae->short_gain_reg;
	dev_dbg(&client->dev,
		"hdrae req: exp (0x%x, 0x%x, 0x%x), gain(0x%x: 0x%x, 0x%x)\n",
		l_exp_time, m_exp_time, s_exp_time,
		l_a_gain, m_a_gain, s_a_gain);

	if (jx_f37->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
	}

	//s_exp_time = clamp_val(s_exp_time, 1, MAX_SMPL_START * 2 - 3);
	//smpl_start = (s_exp_time + 3) / 2;
	//jx_f37_write_reg(client, 0xc0, JX_F37_SMPL_START_S_REG);
	//jx_f37_write_reg(client, 0xc1, smpl_start);

	s_exp_min = 1;
	s_exp_max = JX_F37_MAX_SMPL_START * 2 - 5;
	s_exp_time = clamp_val(s_exp_time, s_exp_min, s_exp_max);
	s_exp_time |= 0x1;

	jx_f37_write_reg(client, 0xc0, JX_F37_SHORT_EXPO_REG);
	jx_f37_write_reg(client, 0xc1, (s_exp_time - 1) / 2);

	l_exp_min = 1;
	l_exp_max = fh - JX_F37_MAX_SMPL_START * 2 - 6 - 1; /* Make it odd */
	l_exp_time = clamp_val(l_exp_time, l_exp_min, l_exp_max);
	l_exp_time |= 0x1;

	jx_f37_write_reg(client, 0xc2, JX_F37_LONG_EXPO_HIGH_REG);
	jx_f37_write_reg(client, 0xc3, JX_F37_FETCH_HIGH_BYTE_EXP(l_exp_time));
	jx_f37_write_reg(client, 0xc4, JX_F37_LONG_EXPO_LOW_REG);
	jx_f37_write_reg(client, 0xc5, JX_F37_FETCH_LOW_BYTE_EXP(l_exp_time));

	/* Short frame gain is ignored */
	jx_f37_write_reg(client, 0xc6, JX_F37_LONG_GAIN_REG);
	jx_f37_write_reg(client, 0xc7, l_a_gain);

	/* Trigger group write function */
	jx_f37_write_reg(client, 0x1f, 0x80);

	dev_dbg(&client->dev,
		"hdrae final: smpl_start: %d, exp (0x%x, 0x%x), gain(0x%x: 0x%x)\n"
		"             l_exp[%d, %d], s_exp[%d, %d], fh = %d\n",
		JX_F37_MAX_SMPL_START, l_exp_time, s_exp_time, l_a_gain, s_a_gain,
		l_exp_min, l_exp_max, s_exp_min, s_exp_max, fh);

	return ret;
}

static long jx_f37_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct jx_f37 *jx_f37 = to_jx_f37(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		ret = jx_f37_set_hdrae(jx_f37, arg);
		break;
	case RKMODULE_GET_MODULE_INFO:
		jx_f37_get_module_inf(jx_f37, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = jx_f37->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = jx_f37->cur_mode->width;
		h = jx_f37->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				jx_f37_update_cur_mode_locked(jx_f37, &supported_modes[i]);
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&jx_f37->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long jx_f37_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct preisp_hdrae_exp_s *hdrae;
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_inf *inf;
	long ret;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = jx_f37_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
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

		ret = jx_f37_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret)
				ret = -EFAULT;
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdr, up, sizeof(*hdr));
		if (!ret)
			ret = jx_f37_ioctl(sd, cmd, hdr);
		else
			ret = -EFAULT;
		kfree(hdr);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdrae, up, sizeof(*hdrae));
		if (!ret)
			ret = jx_f37_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int jx_f37_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct jx_f37 *jx_f37 = to_jx_f37(sd);
	const struct jx_f37_mode *mode = jx_f37->cur_mode;

	mutex_lock(&jx_f37->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&jx_f37->mutex);

	return 0;
}

static int jx_f37_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	struct jx_f37 *jx_f37 = to_jx_f37(sd);
	const struct jx_f37_mode *mode = jx_f37->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (JX_F37_LANES - 1) |
		      V4L2_MBUS_CSI2_CHANNEL_0 |
		      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	else if (mode->hdr_mode == HDR_X2)
		val = 1 << (JX_F37_LANES - 1) |
		      V4L2_MBUS_CSI2_CHANNEL_0 |
		      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		      V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2;
	config->flags = val;

	return 0;
}

static int __jx_f37_start_stream(struct jx_f37 *jx_f37)
{
	int ret;

	ret = jx_f37_write_array(jx_f37->client, jx_f37->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */

	mutex_unlock(&jx_f37->mutex);
	ret = v4l2_ctrl_handler_setup(&jx_f37->ctrl_handler);
	mutex_lock(&jx_f37->mutex);
	if (ret)
		return ret;

	if (jx_f37->has_init_exp && jx_f37->cur_mode->hdr_mode != NO_HDR) {
		ret = jx_f37_ioctl(&jx_f37->subdev, PREISP_CMD_SET_HDRAE_EXP,
				   &jx_f37->init_hdrae_exp);
		if (ret) {
			dev_err(&jx_f37->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
		jx_f37->has_init_exp = false;
	}

	return 0;
}

static int __jx_f37_stop_stream(struct jx_f37 *jx_f37)
{
	int ret;
	u8 val;

	ret = jx_f37_read_reg(jx_f37->client, JX_F37_REG_CTRL_MODE, &val);
	if (ret) {
		dev_err(&jx_f37->client->dev, "%s: read reg failed, %d\n",
			__func__, ret);
		return ret;
	}
	return jx_f37_write_reg(jx_f37->client, JX_F37_REG_CTRL_MODE,
				val | JX_F37_MODE_SLEEP_MODE);
}

static int jx_f37_s_stream(struct v4l2_subdev *sd, int on)
{
	struct jx_f37 *jx_f37 = to_jx_f37(sd);
	struct i2c_client *client = jx_f37->client;
	int ret = 0;

	mutex_lock(&jx_f37->mutex);
	on = !!on;
	if (on == jx_f37->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __jx_f37_start_stream(jx_f37);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
		dev_info(&client->dev, "hdr_mode %d\n", jx_f37->cur_mode->hdr_mode);
	} else {
		__jx_f37_stop_stream(jx_f37);
		pm_runtime_put(&client->dev);
	}

	jx_f37->streaming = on;

unlock_and_return:
	mutex_unlock(&jx_f37->mutex);

	return ret;
}

static int jx_f37_s_power(struct v4l2_subdev *sd, int on)
{
	struct jx_f37 *jx_f37 = to_jx_f37(sd);
	struct i2c_client *client = jx_f37->client;
	int ret = 0;

	mutex_lock(&jx_f37->mutex);

	/* If the power state is not modified - no work to do. */
	if (jx_f37->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		/*
		 * Enter sleep state to make sure not mipi output
		 * during rkisp init.
		 */
		__jx_f37_stop_stream(jx_f37);
		jx_f37->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		jx_f37->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&jx_f37->mutex);

	return ret;
}

static int __jx_f37_power_on(struct jx_f37 *jx_f37)
{
	int ret;
	u32 delay_us;
	struct device *dev = &jx_f37->client->dev;

	ret = clk_set_rate(jx_f37->xvclk, JX_F37_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(jx_f37->xvclk) != JX_F37_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(jx_f37->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(jx_f37->reset_gpio))
		gpiod_set_value_cansleep(jx_f37->reset_gpio, 1);

	ret = regulator_bulk_enable(JX_F37_NUM_SUPPLIES, jx_f37->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	/* According to datasheet, at least 10ms for reset duration */
	usleep_range(10 * 1000, 15 * 1000);

	if (!IS_ERR(jx_f37->reset_gpio))
		gpiod_set_value_cansleep(jx_f37->reset_gpio, 0);

	if (!IS_ERR(jx_f37->pwdn_gpio))
		gpiod_set_value_cansleep(jx_f37->pwdn_gpio, 0);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = jx_f37_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(jx_f37->xvclk);

	return ret;
}

static void __jx_f37_power_off(struct jx_f37 *jx_f37)
{
	if (!IS_ERR(jx_f37->pwdn_gpio))
		gpiod_set_value_cansleep(jx_f37->pwdn_gpio, 1);
	clk_disable_unprepare(jx_f37->xvclk);
	if (!IS_ERR(jx_f37->reset_gpio))
		gpiod_set_value_cansleep(jx_f37->reset_gpio, 1);
	regulator_bulk_disable(JX_F37_NUM_SUPPLIES, jx_f37->supplies);
}

static int jx_f37_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_f37 *jx_f37 = to_jx_f37(sd);

	return __jx_f37_power_on(jx_f37);
}

static int jx_f37_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_f37 *jx_f37 = to_jx_f37(sd);

	__jx_f37_power_off(jx_f37);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int jx_f37_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct jx_f37 *jx_f37 = to_jx_f37(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct jx_f37_mode *def_mode = &supported_modes[0];

	mutex_lock(&jx_f37->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&jx_f37->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int jx_f37_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;

	return 0;
}

static const struct dev_pm_ops jx_f37_pm_ops = {
	SET_RUNTIME_PM_OPS(jx_f37_runtime_suspend,
			   jx_f37_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops jx_f37_internal_ops = {
	.open = jx_f37_open,
};
#endif

static const struct v4l2_subdev_core_ops jx_f37_core_ops = {
	.s_power = jx_f37_s_power,
	.ioctl = jx_f37_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = jx_f37_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops jx_f37_video_ops = {
	.s_stream = jx_f37_s_stream,
	.g_frame_interval = jx_f37_g_frame_interval,
	.g_mbus_config = jx_f37_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops jx_f37_pad_ops = {
	.enum_mbus_code = jx_f37_enum_mbus_code,
	.enum_frame_size = jx_f37_enum_frame_sizes,
	.enum_frame_interval = jx_f37_enum_frame_interval,
	.get_fmt = jx_f37_get_fmt,
	.set_fmt = jx_f37_set_fmt,
};

static const struct v4l2_subdev_ops jx_f37_subdev_ops = {
	.core	= &jx_f37_core_ops,
	.video	= &jx_f37_video_ops,
	.pad	= &jx_f37_pad_ops,
};

static int jx_f37_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct jx_f37 *jx_f37 = container_of(ctrl->handler,
					     struct jx_f37, ctrl_handler);
	struct i2c_client *client = jx_f37->client;
	s64 max;
	u8 val = 0;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = jx_f37->cur_mode->height + ctrl->val;
		__v4l2_ctrl_modify_range(jx_f37->exposure,
					 jx_f37->exposure->minimum, max,
					 jx_f37->exposure->step,
					 jx_f37->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set expo: val: %d\n", ctrl->val);
		/* 4 least significant bits of expsoure are fractional part */
		ret = jx_f37_write_reg(jx_f37->client,
				JX_F37_LONG_EXPO_HIGH_REG,
				JX_F37_FETCH_HIGH_BYTE_EXP(ctrl->val));
		ret |= jx_f37_write_reg(jx_f37->client,
				JX_F37_LONG_EXPO_LOW_REG,
				JX_F37_FETCH_LOW_BYTE_EXP(ctrl->val));
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_dbg(&client->dev, "set a-gain: val: %d\n", ctrl->val);
		ret |= jx_f37_write_reg(jx_f37->client,
			JX_F37_LONG_GAIN_REG, ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		break;
	case V4L2_CID_HFLIP:
		ret = jx_f37_read_reg(jx_f37->client, JX_F37_FLIP_MIRROR_REG,
				       &val);
		if (ctrl->val)
			val |= BIT(5);
		else
			val &= ~BIT(5);
		ret |= jx_f37_write_reg(jx_f37->client, JX_F37_FLIP_MIRROR_REG,
					val);
		break;
	case V4L2_CID_VFLIP:
		ret = jx_f37_read_reg(jx_f37->client, JX_F37_FLIP_MIRROR_REG,
				       &val);
		if (ctrl->val)
			val |= BIT(4);
		else
			val &= ~BIT(4);
		ret |= jx_f37_write_reg(jx_f37->client, JX_F37_FLIP_MIRROR_REG,
					val);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set vblank: val: %d\n", ctrl->val);
		ret |= jx_f37_write_reg(jx_f37->client, JX_F37_REG_HIGH_VTS,
			JX_F37_FETCH_HIGH_BYTE_VTS((ctrl->val + jx_f37->cur_mode->height)));
		ret |= jx_f37_write_reg(jx_f37->client, JX_F37_REG_LOW_VTS,
			JX_F37_FETCH_LOW_BYTE_VTS((ctrl->val + jx_f37->cur_mode->height)));
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops jx_f37_ctrl_ops = {
	.s_ctrl = jx_f37_set_ctrl,
};

static int jx_f37_initialize_controls(struct jx_f37 *jx_f37)
{
	const struct jx_f37_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &jx_f37->ctrl_handler;
	mode = jx_f37->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &jx_f37->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, JX_F37_PIXEL_RATE, 1, JX_F37_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	jx_f37->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (jx_f37->hblank)
		jx_f37->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	jx_f37->vblank = v4l2_ctrl_new_std(handler, &jx_f37_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				JX_F37_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def;
	jx_f37->exposure = v4l2_ctrl_new_std(handler, &jx_f37_ctrl_ops,
				V4L2_CID_EXPOSURE, JX_F37_EXPOSURE_MIN,
				exposure_max, JX_F37_EXPOSURE_STEP,
				mode->exp_def);

	jx_f37->anal_gain = v4l2_ctrl_new_std(handler, &jx_f37_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	v4l2_ctrl_new_std(handler, &jx_f37_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &jx_f37_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&jx_f37->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	jx_f37->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int jx_f37_check_sensor_id(struct jx_f37 *jx_f37,
				  struct i2c_client *client)
{
	struct device *dev = &jx_f37->client->dev;
	u8 id_h = 0;
	u8 id_l = 0;
	int ret;

	ret = jx_f37_read_reg(client, JX_F37_PIDH_ADDR, &id_h);
	ret |= jx_f37_read_reg(client, JX_F37_PIDL_ADDR, &id_l);
	if (id_h != CHIP_ID_H && id_l != CHIP_ID_L) {
		dev_err(dev, "Wrong camera sensor id(0x%02x%02x)\n",
			id_h, id_l);
		return -EINVAL;
	}

	dev_info(dev, "Detected jx_f37 (0x%02x%02x) sensor\n",
		id_h, id_l);

	return ret;
}

static int jx_f37_configure_regulators(struct jx_f37 *jx_f37)
{
	unsigned int i;

	for (i = 0; i < JX_F37_NUM_SUPPLIES; i++)
		jx_f37->supplies[i].supply = jx_f37_supply_names[i];

	return devm_regulator_bulk_get(&jx_f37->client->dev,
				       JX_F37_NUM_SUPPLIES,
				       jx_f37->supplies);
}

static int jx_f37_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct jx_f37 *jx_f37;
	struct v4l2_subdev *sd;
	char facing[2];
	u32 hdr_mode;
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	jx_f37 = devm_kzalloc(dev, sizeof(*jx_f37), GFP_KERNEL);
	if (!jx_f37)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &jx_f37->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &jx_f37->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &jx_f37->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &jx_f37->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	jx_f37->client = client;

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	if (ret || (jx_f37_set_hdr_mode_locked(jx_f37, hdr_mode))) {
		jx_f37->cur_mode = &supported_modes[0];
		dev_warn(dev, "Bad dts hdr_mode value! Use default mode\n");
	}

	jx_f37->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(jx_f37->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	jx_f37->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(jx_f37->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	jx_f37->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(jx_f37->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = jx_f37_configure_regulators(jx_f37);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&jx_f37->mutex);

	sd = &jx_f37->subdev;
	v4l2_i2c_subdev_init(sd, client, &jx_f37_subdev_ops);
	ret = jx_f37_initialize_controls(jx_f37);
	if (ret)
		goto err_destroy_mutex;

	ret = __jx_f37_power_on(jx_f37);
	if (ret)
		goto err_free_handler;

	ret = jx_f37_check_sensor_id(jx_f37, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &jx_f37_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	jx_f37->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &jx_f37->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(jx_f37->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 jx_f37->module_index, facing,
		 JX_F37_NAME, dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

#ifdef USED_SYS_DEBUG
	add_sysfs_interfaces(dev);
#endif
	dev_info(dev, "probe successful\n");

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__jx_f37_power_off(jx_f37);
err_free_handler:
	v4l2_ctrl_handler_free(&jx_f37->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&jx_f37->mutex);

	dev_err(dev, "probe failed\n");

	return ret;
}

static int jx_f37_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_f37 *jx_f37 = to_jx_f37(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&jx_f37->ctrl_handler);
	mutex_destroy(&jx_f37->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__jx_f37_power_off(jx_f37);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id jx_f37_of_match[] = {
	{ .compatible = "soi,jx_f37" },
	{},
};
MODULE_DEVICE_TABLE(of, jx_f37_of_match);
#endif

static const struct i2c_device_id jx_f37_match_id[] = {
	{ "soi,jx_f37", 0 },
	{ },
};

static struct i2c_driver jx_f37_i2c_driver = {
	.driver = {
		.name = JX_F37_NAME,
		.pm = &jx_f37_pm_ops,
		.of_match_table = of_match_ptr(jx_f37_of_match),
	},
	.probe		= &jx_f37_probe,
	.remove		= &jx_f37_remove,
	.id_table	= jx_f37_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&jx_f37_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&jx_f37_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("SOI jx_f37 sensor driver");
MODULE_LICENSE("GPL v2");
