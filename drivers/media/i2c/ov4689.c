// SPDX-License-Identifier: GPL-2.0
/*
 * ov4689 driver
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
#include <linux/pinctrl/consumer.h>

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define OV4689_LINK_FREQ_500MHZ		500000000
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define OV4689_PIXEL_RATE		(OV4689_LINK_FREQ_500MHZ * 2 * 2 / 10)
#define OV4689_XVCLK_FREQ		24000000

#define CHIP_ID				0x004688
#define OV4689_REG_CHIP_ID		0x300a

#define OV4689_REG_CTRL_MODE		0x0100
#define OV4689_MODE_SW_STANDBY		0x0
#define OV4689_MODE_STREAMING		BIT(0)

#define OV4689_REG_EXPOSURE		0x3500
#define	OV4689_EXPOSURE_MIN		4
#define	OV4689_EXPOSURE_STEP		1
#define OV4689_VTS_MAX			0x7fff

#define OV4689_REG_GAIN_H		0x3508
#define OV4689_REG_GAIN_L		0x3509
#define OV4689_GAIN_H_MASK		0x07
#define OV4689_GAIN_H_SHIFT		8
#define OV4689_GAIN_L_MASK		0xff
#define OV4689_GAIN_MIN			0x10
#define OV4689_GAIN_MAX			0xf8
#define OV4689_GAIN_STEP		1
#define OV4689_GAIN_DEFAULT		0x10

#define OV4689_REG_TEST_PATTERN		0x5040
#define OV4689_TEST_PATTERN_ENABLE	0x80
#define OV4689_TEST_PATTERN_DISABLE	0x0

#define OV4689_REG_VTS			0x380e

#define REG_NULL			0xFFFF

#define OV4689_REG_VALUE_08BIT		1
#define OV4689_REG_VALUE_16BIT		2
#define OV4689_REG_VALUE_24BIT		3

#define OV4689_LANES			2
#define OV4689_BITS_PER_SAMPLE		10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

static const char * const ov4689_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV4689_NUM_SUPPLIES ARRAY_SIZE(ov4689_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov4689_mode {
	u32 width;
	u32 height;
	u32 max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct ov4689 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV4689_NUM_SUPPLIES];

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
	const struct ov4689_mode *cur_mode;
};

#define to_ov4689(sd) container_of(sd, struct ov4689, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval ov4689_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 1008Mbps
 */
static const struct regval ov4689_2688x1520_regs[] = {
	{0x0103, 0x01},
	{0x3638, 0x00},
	{0x0300, 0x00},
	{0x0302, 0x2a},
	{0x0303, 0x00},
	{0x0304, 0x03},
	{0x030b, 0x00},
	{0x030d, 0x1e},
	{0x030e, 0x04},
	{0x030f, 0x01},
	{0x0312, 0x01},
	{0x031e, 0x00},
	{0x3000, 0x20},
	{0x3002, 0x00},
	{0x3018, 0x32},
	{0x3020, 0x93},
	{0x3021, 0x03},
	{0x3022, 0x01},
	{0x3031, 0x0a},
	{0x303f, 0x0c},
	{0x3305, 0xf1},
	{0x3307, 0x04},
	{0x3309, 0x29},
	{0x3500, 0x00},
	{0x3501, 0x60},
	{0x3502, 0x00},
	{0x3503, 0x04},
	{0x3504, 0x00},
	{0x3505, 0x00},
	{0x3506, 0x00},
	{0x3507, 0x00},
	{0x3508, 0x00},
	{0x3509, 0x80},
	{0x350a, 0x00},
	{0x350b, 0x00},
	{0x350c, 0x00},
	{0x350d, 0x00},
	{0x350e, 0x00},
	{0x350f, 0x80},
	{0x3510, 0x00},
	{0x3511, 0x00},
	{0x3512, 0x00},
	{0x3513, 0x00},
	{0x3514, 0x00},
	{0x3515, 0x80},
	{0x3516, 0x00},
	{0x3517, 0x00},
	{0x3518, 0x00},
	{0x3519, 0x00},
	{0x351a, 0x00},
	{0x351b, 0x80},
	{0x351c, 0x00},
	{0x351d, 0x00},
	{0x351e, 0x00},
	{0x351f, 0x00},
	{0x3520, 0x00},
	{0x3521, 0x80},
	{0x3522, 0x08},
	{0x3524, 0x08},
	{0x3526, 0x08},
	{0x3528, 0x08},
	{0x352a, 0x08},
	{0x3602, 0x00},
	{0x3603, 0x40},
	{0x3604, 0x02},
	{0x3605, 0x00},
	{0x3606, 0x00},
	{0x3607, 0x00},
	{0x3609, 0x12},
	{0x360a, 0x40},
	{0x360c, 0x08},
	{0x360f, 0xe5},
	{0x3608, 0x8f},
	{0x3611, 0x00},
	{0x3613, 0xf7},
	{0x3616, 0x58},
	{0x3619, 0x99},
	{0x361b, 0x60},
	{0x361c, 0x7a},
	{0x361e, 0x79},
	{0x361f, 0x02},
	{0x3632, 0x00},
	{0x3633, 0x10},
	{0x3634, 0x10},
	{0x3635, 0x10},
	{0x3636, 0x15},
	{0x3646, 0x86},
	{0x364a, 0x0b},
	{0x3700, 0x17},
	{0x3701, 0x22},
	{0x3703, 0x10},
	{0x370a, 0x37},
	{0x3705, 0x00},
	{0x3706, 0x63},
	{0x3709, 0x3c},
	{0x370b, 0x01},
	{0x370c, 0x30},
	{0x3710, 0x24},
	{0x3711, 0x0c},
	{0x3716, 0x00},
	{0x3720, 0x28},
	{0x3729, 0x7b},
	{0x372a, 0x84},
	{0x372b, 0xbd},
	{0x372c, 0xbc},
	{0x372e, 0x52},
	{0x373c, 0x0e},
	{0x373e, 0x33},
	{0x3743, 0x10},
	{0x3744, 0x88},
	{0x3745, 0xc0},
	{0x374a, 0x43},
	{0x374c, 0x00},
	{0x374e, 0x23},
	{0x3751, 0x7b},
	{0x3752, 0x84},
	{0x3753, 0xbd},
	{0x3754, 0xbc},
	{0x3756, 0x52},
	{0x375c, 0x00},
	{0x3760, 0x00},
	{0x3761, 0x00},
	{0x3762, 0x00},
	{0x3763, 0x00},
	{0x3764, 0x00},
	{0x3767, 0x04},
	{0x3768, 0x04},
	{0x3769, 0x08},
	{0x376a, 0x08},
	{0x376b, 0x20},
	{0x376c, 0x00},
	{0x376d, 0x00},
	{0x376e, 0x00},
	{0x3773, 0x00},
	{0x3774, 0x51},
	{0x3776, 0xbd},
	{0x3777, 0xbd},
	{0x3781, 0x18},
	{0x3783, 0x25},
	{0x3798, 0x1b},
	{0x3800, 0x00},
	{0x3801, 0x08},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x0a},
	{0x3805, 0x97},
	{0x3806, 0x05},
	{0x3807, 0xfb},
	{0x3808, 0x0a},
	{0x3809, 0x80},
	{0x380a, 0x05},
	{0x380b, 0xf0},
	{0x380c, 0x0a},
	{0x380d, 0x18},
	{0x380e, 0x06},
	{0x380f, 0x12},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3819, 0x01},
	{0x3820, 0x00},
	{0x3821, 0x06},
	{0x3829, 0x00},
	{0x382a, 0x01},
	{0x382b, 0x01},
	{0x382d, 0x7f},
	{0x3830, 0x04},
	{0x3836, 0x01},
	{0x3837, 0x00},
	{0x3841, 0x02},
	{0x3846, 0x08},
	{0x3847, 0x07},
	{0x3d85, 0x36},
	{0x3d8c, 0x71},
	{0x3d8d, 0xcb},
	{0x3f0a, 0x00},
	{0x4000, 0xf1},
	{0x4001, 0x40},
	{0x4002, 0x04},
	{0x4003, 0x14},
	{0x400e, 0x00},
	{0x4011, 0x00},
	{0x401a, 0x00},
	{0x401b, 0x00},
	{0x401c, 0x00},
	{0x401d, 0x00},
	{0x401f, 0x00},
	{0x4020, 0x00},
	{0x4021, 0x10},
	{0x4022, 0x07},
	{0x4023, 0xcf},
	{0x4024, 0x09},
	{0x4025, 0x60},
	{0x4026, 0x09},
	{0x4027, 0x6f},
	{0x4028, 0x00},
	{0x4029, 0x02},
	{0x402a, 0x06},
	{0x402b, 0x04},
	{0x402c, 0x02},
	{0x402d, 0x02},
	{0x402e, 0x0e},
	{0x402f, 0x04},
	{0x4302, 0xff},
	{0x4303, 0xff},
	{0x4304, 0x00},
	{0x4305, 0x00},
	{0x4306, 0x00},
	{0x4308, 0x02},
	{0x4500, 0x6c},
	{0x4501, 0xc4},
	{0x4502, 0x40},
	{0x4503, 0x01},
	{0x4601, 0xa7},
	{0x4800, 0x04},
	{0x4813, 0x08},
	{0x481f, 0x40},
	{0x4829, 0x78},
	{0x4837, 0x10},
	{0x4b00, 0x2a},
	{0x4b0d, 0x00},
	{0x4d00, 0x04},
	{0x4d01, 0x42},
	{0x4d02, 0xd1},
	{0x4d03, 0x93},
	{0x4d04, 0xf5},
	{0x4d05, 0xc1},
	{0x5000, 0xf3},
	{0x5001, 0x11},
	{0x5004, 0x00},
	{0x500a, 0x00},
	{0x500b, 0x00},
	{0x5032, 0x00},
	{0x5040, 0x00},
	{0x5050, 0x0c},
	{0x5500, 0x00},
	{0x5501, 0x10},
	{0x5502, 0x01},
	{0x5503, 0x0f},
	{0x8000, 0x00},
	{0x8001, 0x00},
	{0x8002, 0x00},
	{0x8003, 0x00},
	{0x8004, 0x00},
	{0x8005, 0x00},
	{0x8006, 0x00},
	{0x8007, 0x00},
	{0x8008, 0x00},
	{0x3638, 0x00},
	{REG_NULL, 0x00},
};

static const struct ov4689_mode supported_modes[] = {
	{
		.width = 2688,
		.height = 1520,
		.max_fps = 30,
		.exp_def = 0x0600,
		.hts_def = 0x0a18,
		.vts_def = 0x0612,
		.reg_list = ov4689_2688x1520_regs,
	},
};

static const s64 link_freq_menu_items[] = {
	OV4689_LINK_FREQ_500MHZ
};

static const char * const ov4689_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int ov4689_write_reg(struct i2c_client *client, u16 reg,
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

static int ov4689_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov4689_write_reg(client, regs[i].addr,
				       OV4689_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int ov4689_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int ov4689_get_reso_dist(const struct ov4689_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov4689_mode *
ov4689_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov4689_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov4689_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov4689 *ov4689 = to_ov4689(sd);
	const struct ov4689_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&ov4689->mutex);

	mode = ov4689_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov4689->mutex);
		return -ENOTTY;
#endif
	} else {
		ov4689->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov4689->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov4689->vblank, vblank_def,
					 OV4689_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&ov4689->mutex);

	return 0;
}

static int ov4689_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov4689 *ov4689 = to_ov4689(sd);
	const struct ov4689_mode *mode = ov4689->cur_mode;

	mutex_lock(&ov4689->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov4689->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ov4689->mutex);

	return 0;
}

static int ov4689_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov4689_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int ov4689_enable_test_pattern(struct ov4689 *ov4689, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV4689_TEST_PATTERN_ENABLE;
	else
		val = OV4689_TEST_PATTERN_DISABLE;

	return ov4689_write_reg(ov4689->client, OV4689_REG_TEST_PATTERN,
				OV4689_REG_VALUE_08BIT, val);
}

static int __ov4689_start_stream(struct ov4689 *ov4689)
{
	int ret;

	ret = ov4689_write_array(ov4689->client, ov4689_global_regs);
	if (ret)
		return ret;

	ret = ov4689_write_array(ov4689->client, ov4689->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&ov4689->mutex);
	ret = v4l2_ctrl_handler_setup(&ov4689->ctrl_handler);
	mutex_lock(&ov4689->mutex);
	if (ret)
		return ret;

	return ov4689_write_reg(ov4689->client, OV4689_REG_CTRL_MODE,
				OV4689_REG_VALUE_08BIT, OV4689_MODE_STREAMING);
}

static int __ov4689_stop_stream(struct ov4689 *ov4689)
{
	return ov4689_write_reg(ov4689->client, OV4689_REG_CTRL_MODE,
				OV4689_REG_VALUE_08BIT, OV4689_MODE_SW_STANDBY);
}

static int ov4689_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov4689 *ov4689 = to_ov4689(sd);
	struct i2c_client *client = ov4689->client;
	int ret = 0;

	mutex_lock(&ov4689->mutex);
	on = !!on;
	if (on == ov4689->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov4689_start_stream(ov4689);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ov4689_stop_stream(ov4689);
		pm_runtime_put(&client->dev);
	}

	ov4689->streaming = on;

unlock_and_return:
	mutex_unlock(&ov4689->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov4689_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV4689_XVCLK_FREQ / 1000 / 1000);
}

static int __ov4689_power_on(struct ov4689 *ov4689)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov4689->client->dev;

	if (!IS_ERR_OR_NULL(ov4689->pins_default)) {
		ret = pinctrl_select_state(ov4689->pinctrl,
					   ov4689->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = clk_prepare_enable(ov4689->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(ov4689->reset_gpio))
		gpiod_set_value_cansleep(ov4689->reset_gpio, 0);

	ret = regulator_bulk_enable(OV4689_NUM_SUPPLIES, ov4689->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov4689->reset_gpio))
		gpiod_set_value_cansleep(ov4689->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(ov4689->pwdn_gpio))
		gpiod_set_value_cansleep(ov4689->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov4689_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov4689->xvclk);

	return ret;
}

static void __ov4689_power_off(struct ov4689 *ov4689)
{
	int ret;
	struct device *dev = &ov4689->client->dev;

	if (!IS_ERR(ov4689->pwdn_gpio))
		gpiod_set_value_cansleep(ov4689->pwdn_gpio, 0);
	clk_disable_unprepare(ov4689->xvclk);
	if (!IS_ERR(ov4689->reset_gpio))
		gpiod_set_value_cansleep(ov4689->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(ov4689->pins_sleep)) {
		ret = pinctrl_select_state(ov4689->pinctrl,
					   ov4689->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(OV4689_NUM_SUPPLIES, ov4689->supplies);
}

static int ov4689_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov4689 *ov4689 = to_ov4689(sd);

	return __ov4689_power_on(ov4689);
}

static int ov4689_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov4689 *ov4689 = to_ov4689(sd);

	__ov4689_power_off(ov4689);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov4689_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov4689 *ov4689 = to_ov4689(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov4689_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov4689->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov4689->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static const struct dev_pm_ops ov4689_pm_ops = {
	SET_RUNTIME_PM_OPS(ov4689_runtime_suspend,
			   ov4689_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov4689_internal_ops = {
	.open = ov4689_open,
};
#endif

static const struct v4l2_subdev_video_ops ov4689_video_ops = {
	.s_stream = ov4689_s_stream,
};

static const struct v4l2_subdev_pad_ops ov4689_pad_ops = {
	.enum_mbus_code = ov4689_enum_mbus_code,
	.enum_frame_size = ov4689_enum_frame_sizes,
	.get_fmt = ov4689_get_fmt,
	.set_fmt = ov4689_set_fmt,
};

static const struct v4l2_subdev_ops ov4689_subdev_ops = {
	.video	= &ov4689_video_ops,
	.pad	= &ov4689_pad_ops,
};

static int ov4689_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov4689 *ov4689 = container_of(ctrl->handler,
					     struct ov4689, ctrl_handler);
	struct i2c_client *client = ov4689->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov4689->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov4689->exposure,
					 ov4689->exposure->minimum, max,
					 ov4689->exposure->step,
					 ov4689->exposure->default_value);
		break;
	}

	if (pm_runtime_get(&client->dev) <= 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = ov4689_write_reg(ov4689->client, OV4689_REG_EXPOSURE,
				       OV4689_REG_VALUE_24BIT, ctrl->val << 4);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov4689_write_reg(ov4689->client, OV4689_REG_GAIN_H,
				       OV4689_REG_VALUE_08BIT,
				       (ctrl->val >> OV4689_GAIN_H_SHIFT) & OV4689_GAIN_H_MASK);
		ret |= ov4689_write_reg(ov4689->client, OV4689_REG_GAIN_L,
				       OV4689_REG_VALUE_08BIT,
				       ctrl->val & OV4689_GAIN_L_MASK);
		break;
	case V4L2_CID_VBLANK:
		ret = ov4689_write_reg(ov4689->client, OV4689_REG_VTS,
				       OV4689_REG_VALUE_16BIT,
				       ctrl->val + ov4689->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov4689_enable_test_pattern(ov4689, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov4689_ctrl_ops = {
	.s_ctrl = ov4689_set_ctrl,
};

static int ov4689_initialize_controls(struct ov4689 *ov4689)
{
	const struct ov4689_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &ov4689->ctrl_handler;
	mode = ov4689->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &ov4689->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, OV4689_PIXEL_RATE, 1, OV4689_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	ov4689->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov4689->hblank)
		ov4689->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov4689->vblank = v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV4689_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	ov4689->exposure = v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops,
				V4L2_CID_EXPOSURE, OV4689_EXPOSURE_MIN,
				exposure_max, OV4689_EXPOSURE_STEP,
				mode->exp_def);

	ov4689->anal_gain = v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OV4689_GAIN_MIN,
				OV4689_GAIN_MAX, OV4689_GAIN_STEP,
				OV4689_GAIN_DEFAULT);

	ov4689->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov4689_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov4689_test_pattern_menu) - 1,
				0, 0, ov4689_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov4689->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov4689->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov4689_check_sensor_id(struct ov4689 *ov4689,
				  struct i2c_client *client)
{
	struct device *dev = &ov4689->client->dev;
	u32 id = 0;
	int ret;

	ret = ov4689_read_reg(client, OV4689_REG_CHIP_ID,
			      OV4689_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return ret;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int ov4689_configure_regulators(struct ov4689 *ov4689)
{
	unsigned int i;

	for (i = 0; i < OV4689_NUM_SUPPLIES; i++)
		ov4689->supplies[i].supply = ov4689_supply_names[i];

	return devm_regulator_bulk_get(&ov4689->client->dev,
				       OV4689_NUM_SUPPLIES,
				       ov4689->supplies);
}

static int ov4689_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ov4689 *ov4689;
	struct v4l2_subdev *sd;
	int ret;

	ov4689 = devm_kzalloc(dev, sizeof(*ov4689), GFP_KERNEL);
	if (!ov4689)
		return -ENOMEM;

	ov4689->client = client;
	ov4689->cur_mode = &supported_modes[0];

	ov4689->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov4689->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}
	ret = clk_set_rate(ov4689->xvclk, OV4689_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(ov4689->xvclk) != OV4689_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	ov4689->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov4689->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov4689->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov4689->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ov4689->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov4689->pinctrl)) {
		ov4689->pins_default =
			pinctrl_lookup_state(ov4689->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov4689->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov4689->pins_sleep =
			pinctrl_lookup_state(ov4689->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov4689->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = ov4689_configure_regulators(ov4689);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&ov4689->mutex);

	sd = &ov4689->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov4689_subdev_ops);
	ret = ov4689_initialize_controls(ov4689);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov4689_power_on(ov4689);
	if (ret)
		goto err_free_handler;

	ret = ov4689_check_sensor_id(ov4689, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov4689_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov4689->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&sd->entity, 1, &ov4689->pad, 0);
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
	__ov4689_power_off(ov4689);
err_free_handler:
	v4l2_ctrl_handler_free(&ov4689->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov4689->mutex);

	return ret;
}

static int ov4689_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov4689 *ov4689 = to_ov4689(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov4689->ctrl_handler);
	mutex_destroy(&ov4689->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov4689_power_off(ov4689);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov4689_of_match[] = {
	{ .compatible = "ovti,ov4689" },
	{},
};
MODULE_DEVICE_TABLE(of, ov4689_of_match);
#endif

static const struct i2c_device_id ov4689_match_id[] = {
	{ "ovti,ov4689", 0 },
	{ },
};

static struct i2c_driver ov4689_i2c_driver = {
	.driver = {
		.name = "ov4689",
		.pm = &ov4689_pm_ops,
		.of_match_table = of_match_ptr(ov4689_of_match),
	},
	.probe		= &ov4689_probe,
	.remove		= &ov4689_remove,
	.id_table	= ov4689_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov4689_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov4689_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov4689 sensor driver");
MODULE_LICENSE("GPL v2");
