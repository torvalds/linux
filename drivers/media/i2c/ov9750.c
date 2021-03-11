// SPDX-License-Identifier: GPL-2.0
/*
 * ov9750 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
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
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/version.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x5)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define OV9750_LINK_FREQ_400MHZ		400000000
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define OV9750_PIXEL_RATE		(OV9750_LINK_FREQ_400MHZ * 2 * 2 / 10)
#define OV9750_XVCLK_FREQ		24000000

#define CHIP_ID				0x9750
#define OV9750_REG_CHIP_ID		0x300B

#define OV9750_REG_CTRL_MODE		0x0100
#define OV9750_MODE_SW_STANDBY		0x0
#define OV9750_MODE_STREAMING		BIT(0)

#define OV9750_REG_EXPOSURE		0x3500
#define	OV9750_EXPOSURE_MIN		4
#define	OV9750_EXPOSURE_STEP		1
#define OV9750_VTS_MAX			0x7fff

#define OV9750_REG_GAIN_H		0x3508
#define OV9750_REG_GAIN_L		0x3509
#define OV9750_GAIN_H_MASK		0x1f
#define OV9750_GAIN_L_MASK		0xff
#define OV9750_GAIN_MIN			0x0080
#define OV9750_GAIN_MAX			0x1000
#define OV9750_GAIN_STEP		1
#define OV9750_GAIN_DEFAULT		0x0080

#define OV9750_REG_TEST_PATTERN		0x5e00
#define OV9750_TEST_PATTERN_ENABLE	0x80
#define OV9750_TEST_PATTERN_DISABLE	0x0

#define OV9750_REG_VTS			0x380e

#define REG_NULL			0xFFFF
#define REG_DELAY			0xFFFE

#define OV9750_REG_VALUE_08BIT		1
#define OV9750_REG_VALUE_16BIT		2
#define OV9750_REG_VALUE_24BIT		3

#define OV9750_LANES			2
#define OV9750_BITS_PER_SAMPLE		10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define OV9750_NAME			"ov9750"

static const char * const ov9750_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV9750_NUM_SUPPLIES ARRAY_SIZE(ov9750_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov9750_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct ov9750 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV9750_NUM_SUPPLIES];

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
	const struct ov9750_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_ov9750(sd) container_of(sd, struct ov9750, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval ov9750_global_regs[] = {
	{0x0103, 0x01},
	{REG_DELAY, 0x10},
	{0x0100, 0x00},
	{REG_DELAY, 0x10},
	{0x0300, 0x04},
	{0x0302, 0x64},
	{0x0303, 0x00},
	{0x0304, 0x03},
	{0x0305, 0x01},
	{0x0306, 0x01},
	{0x030a, 0x00},
	{0x030b, 0x00},
	{0x030d, 0x1e},
	{0x030e, 0x01},
	{0x030f, 0x04},
	{0x0312, 0x01},
	{0x031e, 0x04},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x21},
	{0x3005, 0xf0},
	{0x3011, 0x00},
	{0x3016, 0x53},
	{0x3018, 0x32},
	{0x301a, 0xf0},
	{0x301b, 0xf0},
	{0x301c, 0xf0},
	{0x301d, 0xf0},
	{0x301e, 0xf0},
	{0x3022, 0x01},
	{0x3031, 0x0a},
	{0x3032, 0x80},
	{0x303c, 0xff},
	{0x303e, 0xff},
	{0x3040, 0xf0},
	{0x3041, 0x00},
	{0x3042, 0xf0},
	{0x3104, 0x01},
	{0x3106, 0x15},
	{0x3107, 0x01},
	{0x3500, 0x00},
	{0x3501, 0x3d},
	{0x3502, 0x00},
	{0x3503, 0x08},
	{0x3504, 0x03},
	{0x3505, 0x83},
	{0x3508, 0x02},
	{0x3509, 0x80},
	{0x3600, 0x65},
	{0x3601, 0x60},
	{0x3602, 0x22},
	{0x3610, 0xe8},
	{0x3611, 0x56},
	{0x3612, 0x48},
	{0x3613, 0x5a},
	{0x3614, 0x91},
	{0x3615, 0x79},
	{0x3617, 0x57},
	{0x3621, 0x90},
	{0x3622, 0x00},
	{0x3623, 0x00},
	{0x3625, 0x07},
	{0x3633, 0x10},
	{0x3634, 0x10},
	{0x3635, 0x14},
	{0x3636, 0x13},
	{0x3650, 0x00},
	{0x3652, 0xff},
	{0x3654, 0x00},
	{0x3653, 0x34},
	{0x3655, 0x20},
	{0x3656, 0xff},
	{0x3657, 0xc4},
	{0x365a, 0xff},
	{0x365b, 0xff},
	{0x365e, 0xff},
	{0x365f, 0x00},
	{0x3668, 0x00},
	{0x366a, 0x07},
	{0x366d, 0x00},
	{0x366e, 0x10},
	{0x3702, 0x1d},
	{0x3703, 0x10},
	{0x3704, 0x14},
	{0x3705, 0x00},
	{0x3706, 0x27},
	{0x3709, 0x24},
	{0x370a, 0x00},
	{0x370b, 0x7d},
	{0x3714, 0x24},
	{0x371a, 0x5e},
	{0x3730, 0x82},
	{0x3733, 0x10},
	{0x373e, 0x18},
	{0x3755, 0x00},
	{0x3758, 0x00},
	{0x375b, 0x13},
	{0x3772, 0x23},
	{0x3773, 0x05},
	{0x3774, 0x16},
	{0x3775, 0x12},
	{0x3776, 0x08},
	{0x37a8, 0x38},
	{0x37b5, 0x36},
	{0x37c2, 0x04},
	{0x37c5, 0x00},
	{0x37c7, 0x38},
	{0x37c8, 0x00},
	{0x37d1, 0x13},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x05},
	{0x3805, 0x0f},
	{0x3806, 0x03},
	{0x3807, 0xcb},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x03},
	{0x380b, 0xc0},
	{0x380c, 0x03},
	{0x380d, 0x2a},
	{0x380e, 0x03},
	{0x380f, 0xdc},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x00},
	{0x3817, 0x00},
	{0x3818, 0x00},
	{0x3819, 0x00},
	{0x3820, 0x80},
	{0x3821, 0x40},
	{0x3826, 0x00},
	{0x3827, 0x08},
	{0x382a, 0x01},
	{0x382b, 0x01},
	{0x3836, 0x02},
	{0x3838, 0x10},
	{0x3861, 0x00},
	{0x3862, 0x00},
	{0x3863, 0x02},
	{0x3b00, 0x00},
	{0x3c00, 0x89},
	{0x3c01, 0xab},
	{0x3c02, 0x01},
	{0x3c03, 0x00},
	{0x3c04, 0x00},
	{0x3c05, 0x03},
	{0x3c06, 0x00},
	{0x3c07, 0x05},
	{0x3c0c, 0x00},
	{0x3c0d, 0x00},
	{0x3c0e, 0x00},
	{0x3c0f, 0x00},
	{0x3c40, 0x00},
	{0x3c41, 0xa3},
	{0x3c43, 0x7d},
	{0x3c56, 0x80},
	{0x3c80, 0x08},
	{0x3c82, 0x01},
	{0x3c83, 0x61},
	{0x3d85, 0x17},
	{0x3f08, 0x08},
	{0x3f0a, 0x00},
	{0x3f0b, 0x30},
	{0x4000, 0xcd},
	{0x4003, 0x40},
	{0x4009, 0x0d},
	{0x4010, 0xf0},
	{0x4011, 0x70},
	{0x4017, 0x10},
	{0x4040, 0x00},
	{0x4041, 0x00},
	{0x4303, 0x00},
	{0x4307, 0x30},
	{0x4500, 0x30},
	{0x4502, 0x40},
	{0x4503, 0x06},
	{0x4508, 0xaa},
	{0x450b, 0x00},
	{0x450c, 0x00},
	{0x4600, 0x00},
	{0x4601, 0x80},
	{0x4700, 0x04},
	{0x4704, 0x00},
	{0x4705, 0x04},
	{0x4837, 0x14},
	{0x484a, 0x3f},
	{0x5000, 0x10},
	{0x5001, 0x01},
	{0x5002, 0x28},
	{0x5004, 0x0c},
	{0x5006, 0x0c},
	{0x5007, 0xe0},
	{0x5008, 0x01},
	{0x5009, 0xb0},
	{0x502a, 0x18},
	{0x5901, 0x00},
	{0x5a01, 0x00},
	{0x5a03, 0x00},
	{0x5a04, 0x0c},
	{0x5a05, 0xe0},
	{0x5a06, 0x09},
	{0x5a07, 0xb0},
	{0x5a08, 0x06},
	{0x5e00, 0x00},
	{0x5e10, 0xfc},
	{0x300f, 0x00},
	{0x3733, 0x10},
	{0x3610, 0xe8},
	{0x3611, 0x56},
	{0x3635, 0x14},
	{0x3636, 0x13},
	{0x3620, 0x84},
	{0x3614, 0x96},
	{0x481f, 0x30},
	{0x3788, 0x00},
	{0x3789, 0x04},
	{0x378a, 0x01},
	{0x378b, 0x60},
	{0x3799, 0x27},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 60fps
 * mipi_datarate per lane 800Mbps
 */
static const struct regval ov9750_1280x960_regs[] = {
	{REG_NULL, 0x00},
};

static const struct ov9750_mode supported_modes[] = {
	{
		.width = 1280,
		.height = 960,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.exp_def = 0x03D0,
		.hts_def = 0x0654,//0x32A*2
		.vts_def = 0x03DC,
		.reg_list = ov9750_1280x960_regs,
	},
};

static const s64 link_freq_menu_items[] = {
	OV9750_LINK_FREQ_400MHZ
};

static const char * const ov9750_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int ov9750_write_reg(struct i2c_client *client, u16 reg,
			    u32 len, u32 val)
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

static int ov9750_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (unlikely(regs[i].addr == REG_DELAY))
			usleep_range(regs[i].val, regs[i].val * 2);
		else
			ret = ov9750_write_reg(client, regs[i].addr,
				OV9750_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int ov9750_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int ov9750_get_reso_dist(const struct ov9750_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov9750_mode *
ov9750_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov9750_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov9750_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov9750 *ov9750 = to_ov9750(sd);
	const struct ov9750_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&ov9750->mutex);

	mode = ov9750_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov9750->mutex);
		return -ENOTTY;
#endif
	} else {
		ov9750->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov9750->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov9750->vblank, vblank_def,
					 OV9750_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&ov9750->mutex);

	return 0;
}

static int ov9750_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov9750 *ov9750 = to_ov9750(sd);
	const struct ov9750_mode *mode = ov9750->cur_mode;

	mutex_lock(&ov9750->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov9750->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ov9750->mutex);

	return 0;
}

static int ov9750_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov9750_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int ov9750_enable_test_pattern(struct ov9750 *ov9750, u32 pattern)
{
	u32 val;

	if (pattern)
		val = ((pattern - 1) < 2) | OV9750_TEST_PATTERN_ENABLE;
	else
		val = OV9750_TEST_PATTERN_DISABLE;

	return ov9750_write_reg(ov9750->client, OV9750_REG_TEST_PATTERN,
				OV9750_REG_VALUE_08BIT, val);
}

static int ov9750_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct ov9750 *ov9750 = to_ov9750(sd);
	const struct ov9750_mode *mode = ov9750->cur_mode;

	mutex_lock(&ov9750->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ov9750->mutex);

	return 0;
}

static void ov9750_get_module_inf(struct ov9750 *ov9750,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, OV9750_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, ov9750->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, ov9750->len_name, sizeof(inf->base.lens));
}

static long ov9750_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov9750 *ov9750 = to_ov9750(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		ov9750_get_module_inf(ov9750, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = ov9750_write_reg(ov9750->client, OV9750_REG_CTRL_MODE,
				OV9750_REG_VALUE_08BIT, OV9750_MODE_STREAMING);
		else
			ret = ov9750_write_reg(ov9750->client, OV9750_REG_CTRL_MODE,
				OV9750_REG_VALUE_08BIT, OV9750_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov9750_compat_ioctl32(struct v4l2_subdev *sd,
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
		ret = ov9750_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov9750_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __ov9750_start_stream(struct ov9750 *ov9750)
{
	int ret;

	ret = ov9750_write_array(ov9750->client, ov9750->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&ov9750->mutex);
	ret = v4l2_ctrl_handler_setup(&ov9750->ctrl_handler);
	mutex_lock(&ov9750->mutex);
	if (ret)
		return ret;

	return ov9750_write_reg(ov9750->client, OV9750_REG_CTRL_MODE,
				OV9750_REG_VALUE_08BIT, OV9750_MODE_STREAMING);
}

static int __ov9750_stop_stream(struct ov9750 *ov9750)
{
	return ov9750_write_reg(ov9750->client, OV9750_REG_CTRL_MODE,
				OV9750_REG_VALUE_08BIT, OV9750_MODE_SW_STANDBY);
}

static int ov9750_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov9750 *ov9750 = to_ov9750(sd);
	struct i2c_client *client = ov9750->client;
	int ret = 0;

	mutex_lock(&ov9750->mutex);
	on = !!on;
	if (on == ov9750->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov9750_start_stream(ov9750);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ov9750_stop_stream(ov9750);
		pm_runtime_put(&client->dev);
	}

	ov9750->streaming = on;

unlock_and_return:
	mutex_unlock(&ov9750->mutex);

	return ret;
}

static int ov9750_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov9750 *ov9750 = to_ov9750(sd);
	struct i2c_client *client = ov9750->client;
	int ret = 0;

	mutex_lock(&ov9750->mutex);

	/* If the power state is not modified - no work to do. */
	if (ov9750->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		ret = ov9750_write_array(ov9750->client, ov9750_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		ov9750->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		ov9750->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ov9750->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov9750_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV9750_XVCLK_FREQ / 1000 / 1000);
}

static int __ov9750_power_on(struct ov9750 *ov9750)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov9750->client->dev;

	if (!IS_ERR_OR_NULL(ov9750->pins_default)) {
		ret = pinctrl_select_state(ov9750->pinctrl,
					   ov9750->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = clk_set_rate(ov9750->xvclk, OV9750_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(ov9750->xvclk) != OV9750_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(ov9750->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(ov9750->reset_gpio))
		gpiod_set_value_cansleep(ov9750->reset_gpio, 0);

	ret = regulator_bulk_enable(OV9750_NUM_SUPPLIES, ov9750->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov9750->reset_gpio))
		gpiod_set_value_cansleep(ov9750->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(ov9750->pwdn_gpio))
		gpiod_set_value_cansleep(ov9750->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov9750_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov9750->xvclk);

	return ret;
}

static void __ov9750_power_off(struct ov9750 *ov9750)
{
	int ret;

	if (!IS_ERR(ov9750->pwdn_gpio))
		gpiod_set_value_cansleep(ov9750->pwdn_gpio, 0);
	clk_disable_unprepare(ov9750->xvclk);
	if (!IS_ERR(ov9750->reset_gpio))
		gpiod_set_value_cansleep(ov9750->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(ov9750->pins_sleep)) {
		ret = pinctrl_select_state(ov9750->pinctrl,
					   ov9750->pins_sleep);
		if (ret < 0)
			dev_dbg(&ov9750->client->dev, "could not set pins\n");
	}
	regulator_bulk_disable(OV9750_NUM_SUPPLIES, ov9750->supplies);
}

static int ov9750_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9750 *ov9750 = to_ov9750(sd);

	return __ov9750_power_on(ov9750);
}

static int ov9750_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9750 *ov9750 = to_ov9750(sd);

	__ov9750_power_off(ov9750);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov9750_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov9750 *ov9750 = to_ov9750(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov9750_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov9750->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov9750->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov9750_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int ov9750_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (OV9750_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops ov9750_pm_ops = {
	SET_RUNTIME_PM_OPS(ov9750_runtime_suspend,
			   ov9750_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov9750_internal_ops = {
	.open = ov9750_open,
};
#endif

static const struct v4l2_subdev_core_ops ov9750_core_ops = {
	.s_power = ov9750_s_power,
	.ioctl = ov9750_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov9750_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ov9750_video_ops = {
	.s_stream = ov9750_s_stream,
	.g_frame_interval = ov9750_g_frame_interval,
	.g_mbus_config = ov9750_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops ov9750_pad_ops = {
	.enum_mbus_code = ov9750_enum_mbus_code,
	.enum_frame_size = ov9750_enum_frame_sizes,
	.enum_frame_interval = ov9750_enum_frame_interval,
	.get_fmt = ov9750_get_fmt,
	.set_fmt = ov9750_set_fmt,
};

static const struct v4l2_subdev_ops ov9750_subdev_ops = {
	.core	= &ov9750_core_ops,
	.video	= &ov9750_video_ops,
	.pad	= &ov9750_pad_ops,
};

static int ov9750_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov9750 *ov9750 = container_of(ctrl->handler,
					     struct ov9750, ctrl_handler);
	struct i2c_client *client = ov9750->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov9750->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov9750->exposure,
					 ov9750->exposure->minimum, max,
					 ov9750->exposure->step,
					 ov9750->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = ov9750_write_reg(ov9750->client, OV9750_REG_EXPOSURE,
				       OV9750_REG_VALUE_24BIT, ctrl->val << 4);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov9750_write_reg(ov9750->client, OV9750_REG_GAIN_H,
				       OV9750_REG_VALUE_08BIT,
				       (ctrl->val >> 8) & OV9750_GAIN_H_MASK);
		ret |= ov9750_write_reg(ov9750->client, OV9750_REG_GAIN_L,
				       OV9750_REG_VALUE_08BIT,
				       ctrl->val & OV9750_GAIN_L_MASK);
		break;
	case V4L2_CID_VBLANK:
		ret = ov9750_write_reg(ov9750->client, OV9750_REG_VTS,
				       OV9750_REG_VALUE_16BIT,
				       ctrl->val + ov9750->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov9750_enable_test_pattern(ov9750, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov9750_ctrl_ops = {
	.s_ctrl = ov9750_set_ctrl,
};

static int ov9750_initialize_controls(struct ov9750 *ov9750)
{
	const struct ov9750_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &ov9750->ctrl_handler;
	mode = ov9750->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &ov9750->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, OV9750_PIXEL_RATE, 1, OV9750_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	ov9750->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov9750->hblank)
		ov9750->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov9750->vblank = v4l2_ctrl_new_std(handler, &ov9750_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV9750_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	ov9750->exposure = v4l2_ctrl_new_std(handler, &ov9750_ctrl_ops,
				V4L2_CID_EXPOSURE, OV9750_EXPOSURE_MIN,
				exposure_max, OV9750_EXPOSURE_STEP,
				mode->exp_def);

	ov9750->anal_gain = v4l2_ctrl_new_std(handler, &ov9750_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OV9750_GAIN_MIN,
				OV9750_GAIN_MAX, OV9750_GAIN_STEP,
				OV9750_GAIN_DEFAULT);

	ov9750->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov9750_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov9750_test_pattern_menu) - 1,
				0, 0, ov9750_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov9750->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov9750->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov9750_check_sensor_id(struct ov9750 *ov9750,
				  struct i2c_client *client)
{
	struct device *dev = &ov9750->client->dev;
	u32 id = 0;
	int ret;

	ret = ov9750_read_reg(client, OV9750_REG_CHIP_ID,
			      OV9750_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%04x sensor\n", id);

	return 0;
}

static int ov9750_configure_regulators(struct ov9750 *ov9750)
{
	unsigned int i;

	for (i = 0; i < OV9750_NUM_SUPPLIES; i++)
		ov9750->supplies[i].supply = ov9750_supply_names[i];

	return devm_regulator_bulk_get(&ov9750->client->dev,
				       OV9750_NUM_SUPPLIES,
				       ov9750->supplies);
}

static int ov9750_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov9750 *ov9750;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov9750 = devm_kzalloc(dev, sizeof(*ov9750), GFP_KERNEL);
	if (!ov9750)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov9750->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov9750->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov9750->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov9750->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ov9750->client = client;
	ov9750->cur_mode = &supported_modes[0];

	ov9750->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov9750->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov9750->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov9750->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov9750->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov9750->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ov9750->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov9750->pinctrl)) {
		ov9750->pins_default =
			pinctrl_lookup_state(ov9750->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov9750->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov9750->pins_sleep =
			pinctrl_lookup_state(ov9750->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov9750->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = ov9750_configure_regulators(ov9750);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&ov9750->mutex);

	sd = &ov9750->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov9750_subdev_ops);
	ret = ov9750_initialize_controls(ov9750);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov9750_power_on(ov9750);
	if (ret)
		goto err_free_handler;

	ret = ov9750_check_sensor_id(ov9750, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov9750_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov9750->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov9750->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov9750->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov9750->module_index, facing,
		 OV9750_NAME, dev_name(sd->dev));
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
	__ov9750_power_off(ov9750);
err_free_handler:
	v4l2_ctrl_handler_free(&ov9750->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov9750->mutex);

	return ret;
}

static int ov9750_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9750 *ov9750 = to_ov9750(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov9750->ctrl_handler);
	mutex_destroy(&ov9750->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov9750_power_off(ov9750);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov9750_of_match[] = {
	{ .compatible = "ovti,ov9750" },
	{},
};
MODULE_DEVICE_TABLE(of, ov9750_of_match);
#endif

static const struct i2c_device_id ov9750_match_id[] = {
	{ "ovti,ov9750", 0 },
	{ },
};

static struct i2c_driver ov9750_i2c_driver = {
	.driver = {
		.name = OV9750_NAME,
		.pm = &ov9750_pm_ops,
		.of_match_table = of_match_ptr(ov9750_of_match),
	},
	.probe		= &ov9750_probe,
	.remove		= &ov9750_remove,
	.id_table	= ov9750_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov9750_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov9750_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov9750 sensor driver");
MODULE_LICENSE("GPL v2");
