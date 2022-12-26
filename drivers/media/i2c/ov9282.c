// SPDX-License-Identifier: GPL-2.0-only
/*
 * OmniVision ov9282 Camera Sensor Driver
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include <asm/unaligned.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Streaming Mode */
#define OV9282_REG_MODE_SELECT	0x0100
#define OV9282_MODE_STANDBY	0x00
#define OV9282_MODE_STREAMING	0x01

#define OV9282_REG_PLL_CTRL_0D	0x030d
#define OV9282_PLL_CTRL_0D_RAW8		0x60
#define OV9282_PLL_CTRL_0D_RAW10	0x50

#define OV9282_REG_TIMING_HTS	0x380c
#define OV9282_TIMING_HTS_MAX	0x7fff

/* Lines per frame */
#define OV9282_REG_LPFR		0x380e

/* Chip ID */
#define OV9282_REG_ID		0x300a
#define OV9282_ID		0x9281

/* Exposure control */
#define OV9282_REG_EXPOSURE	0x3500
#define OV9282_EXPOSURE_MIN	1
#define OV9282_EXPOSURE_OFFSET	12
#define OV9282_EXPOSURE_STEP	1
#define OV9282_EXPOSURE_DEFAULT	0x0282

/* Analog gain control */
#define OV9282_REG_AGAIN	0x3509
#define OV9282_AGAIN_MIN	0x10
#define OV9282_AGAIN_MAX	0xff
#define OV9282_AGAIN_STEP	1
#define OV9282_AGAIN_DEFAULT	0x10

/* Group hold register */
#define OV9282_REG_HOLD		0x3308

#define OV9282_REG_ANA_CORE_2	0x3662
#define OV9282_ANA_CORE2_RAW8	0x07
#define OV9282_ANA_CORE2_RAW10	0x05

#define OV9282_REG_TIMING_FORMAT_1	0x3820
#define OV9282_REG_TIMING_FORMAT_2	0x3821
#define OV9282_FLIP_BIT			BIT(2)

#define OV9282_REG_MIPI_CTRL00	0x4800
#define OV9282_GATED_CLOCK	BIT(5)

/* Input clock rate */
#define OV9282_INCLK_RATE	24000000

/* CSI2 HW configuration */
#define OV9282_LINK_FREQ	400000000
#define OV9282_NUM_DATA_LANES	2

/* Pixel rate */
#define OV9282_PIXEL_RATE_10BIT		(OV9282_LINK_FREQ * 2 * \
					 OV9282_NUM_DATA_LANES / 10)
#define OV9282_PIXEL_RATE_8BIT		(OV9282_LINK_FREQ * 2 * \
					 OV9282_NUM_DATA_LANES / 8)

/*
 * OV9282 native and active pixel array size.
 * 8 dummy rows/columns on each edge of a 1280x800 active array
 */
#define OV9282_NATIVE_WIDTH		1296U
#define OV9282_NATIVE_HEIGHT		816U
#define OV9282_PIXEL_ARRAY_LEFT		8U
#define OV9282_PIXEL_ARRAY_TOP		8U
#define OV9282_PIXEL_ARRAY_WIDTH	1280U
#define OV9282_PIXEL_ARRAY_HEIGHT	800U

#define OV9282_REG_MIN		0x00
#define OV9282_REG_MAX		0xfffff

static const char * const ov9282_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV9282_NUM_SUPPLIES ARRAY_SIZE(ov9282_supply_names)

/**
 * struct ov9282_reg - ov9282 sensor register
 * @address: Register address
 * @val: Register value
 */
struct ov9282_reg {
	u16 address;
	u8 val;
};

/**
 * struct ov9282_reg_list - ov9282 sensor register list
 * @num_of_regs: Number of registers in the list
 * @regs: Pointer to register list
 */
struct ov9282_reg_list {
	u32 num_of_regs;
	const struct ov9282_reg *regs;
};

/**
 * struct ov9282_mode - ov9282 sensor mode structure
 * @width: Frame width
 * @height: Frame height
 * @hblank_min: Minimum horizontal blanking in lines for non-continuous[0] and
 *		continuous[1] clock modes
 * @vblank: Vertical blanking in lines
 * @vblank_min: Minimum vertical blanking in lines
 * @vblank_max: Maximum vertical blanking in lines
 * @link_freq_idx: Link frequency index
 * @crop: on-sensor cropping for this mode
 * @reg_list: Register list for sensor mode
 */
struct ov9282_mode {
	u32 width;
	u32 height;
	u32 hblank_min[2];
	u32 vblank;
	u32 vblank_min;
	u32 vblank_max;
	u32 link_freq_idx;
	struct v4l2_rect crop;
	struct ov9282_reg_list reg_list;
};

/**
 * struct ov9282 - ov9282 sensor device structure
 * @dev: Pointer to generic device
 * @client: Pointer to i2c client
 * @sd: V4L2 sub-device
 * @pad: Media pad. Only one pad supported
 * @reset_gpio: Sensor reset gpio
 * @inclk: Sensor input clock
 * @supplies: Regulator supplies for the sensor
 * @ctrl_handler: V4L2 control handler
 * @link_freq_ctrl: Pointer to link frequency control
 * @hblank_ctrl: Pointer to horizontal blanking control
 * @vblank_ctrl: Pointer to vertical blanking control
 * @exp_ctrl: Pointer to exposure control
 * @again_ctrl: Pointer to analog gain control
 * @pixel_rate: Pointer to pixel rate control
 * @vblank: Vertical blanking in lines
 * @noncontinuous_clock: Selection of CSI2 noncontinuous clock mode
 * @cur_mode: Pointer to current selected sensor mode
 * @code: Mbus code currently selected
 * @mutex: Mutex for serializing sensor controls
 * @streaming: Flag indicating streaming state
 */
struct ov9282 {
	struct device *dev;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct gpio_desc *reset_gpio;
	struct clk *inclk;
	struct regulator_bulk_data supplies[OV9282_NUM_SUPPLIES];
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq_ctrl;
	struct v4l2_ctrl *hblank_ctrl;
	struct v4l2_ctrl *vblank_ctrl;
	struct {
		struct v4l2_ctrl *exp_ctrl;
		struct v4l2_ctrl *again_ctrl;
	};
	struct v4l2_ctrl *pixel_rate;
	u32 vblank;
	bool noncontinuous_clock;
	const struct ov9282_mode *cur_mode;
	u32 code;
	struct mutex mutex;
	bool streaming;
};

static const s64 link_freq[] = {
	OV9282_LINK_FREQ,
};

/*
 * Common registers
 *
 * Note: Do NOT include a software reset (0x0103, 0x01) in any of these
 * register arrays as some settings are written as part of ov9282_power_on,
 * and the reset will clear them.
 */
static const struct ov9282_reg common_regs[] = {
	{0x0302, 0x32},
	{0x030e, 0x02},
	{0x3001, 0x00},
	{0x3004, 0x00},
	{0x3005, 0x00},
	{0x3006, 0x04},
	{0x3011, 0x0a},
	{0x3013, 0x18},
	{0x301c, 0xf0},
	{0x3022, 0x01},
	{0x3030, 0x10},
	{0x3039, 0x32},
	{0x303a, 0x00},
	{0x3503, 0x08},
	{0x3505, 0x8c},
	{0x3507, 0x03},
	{0x3508, 0x00},
	{0x3610, 0x80},
	{0x3611, 0xa0},
	{0x3620, 0x6e},
	{0x3632, 0x56},
	{0x3633, 0x78},
	{0x3666, 0x00},
	{0x366f, 0x5a},
	{0x3680, 0x84},
	{0x3712, 0x80},
	{0x372d, 0x22},
	{0x3731, 0x80},
	{0x3732, 0x30},
	{0x377d, 0x22},
	{0x3788, 0x02},
	{0x3789, 0xa4},
	{0x378a, 0x00},
	{0x378b, 0x4a},
	{0x3799, 0x20},
	{0x3881, 0x42},
	{0x38a8, 0x02},
	{0x38a9, 0x80},
	{0x38b1, 0x00},
	{0x38c4, 0x00},
	{0x38c5, 0xc0},
	{0x38c6, 0x04},
	{0x38c7, 0x80},
	{0x3920, 0xff},
	{0x4010, 0x40},
	{0x4043, 0x40},
	{0x4307, 0x30},
	{0x4317, 0x00},
	{0x4501, 0x00},
	{0x450a, 0x08},
	{0x4601, 0x04},
	{0x470f, 0x00},
	{0x4f07, 0x00},
	{0x5000, 0x9f},
	{0x5001, 0x00},
	{0x5e00, 0x00},
	{0x5d00, 0x07},
	{0x5d01, 0x00},
	{0x0101, 0x01},
	{0x1000, 0x03},
	{0x5a08, 0x84},
};

static struct ov9282_reg_list common_regs_list = {
	.num_of_regs = ARRAY_SIZE(common_regs),
	.regs = common_regs,
};

#define MODE_1280_800		0
#define MODE_1280_720		1
#define MODE_640_400		2

#define DEFAULT_MODE		MODE_1280_720

/* Sensor mode registers */
static const struct ov9282_reg mode_1280x800_regs[] = {
	{0x3778, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x05},
	{0x3805, 0x0f},
	{0x3806, 0x03},
	{0x3807, 0x2f},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x03},
	{0x380b, 0x20},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x08},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x40},
	{0x3821, 0x00},
	{0x4003, 0x40},
	{0x4008, 0x04},
	{0x4009, 0x0b},
	{0x400c, 0x00},
	{0x400d, 0x07},
	{0x4507, 0x00},
	{0x4509, 0x00},
};

static const struct ov9282_reg mode_1280x720_regs[] = {
	{0x3778, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x05},
	{0x3805, 0x0f},
	{0x3806, 0x02},
	{0x3807, 0xdf},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x02},
	{0x380b, 0xd0},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x08},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x3c},
	{0x3821, 0x84},
	{0x4003, 0x40},
	{0x4008, 0x02},
	{0x4009, 0x05},
	{0x400c, 0x00},
	{0x400d, 0x03},
	{0x4507, 0x00},
	{0x4509, 0x80},
};

static const struct ov9282_reg mode_640x400_regs[] = {
	{0x3778, 0x10},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x05},
	{0x3805, 0x0f},
	{0x3806, 0x03},
	{0x3807, 0x2f},
	{0x3808, 0x02},
	{0x3809, 0x80},
	{0x380a, 0x01},
	{0x380b, 0x90},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x31},
	{0x3815, 0x22},
	{0x3820, 0x60},
	{0x3821, 0x01},
	{0x4008, 0x02},
	{0x4009, 0x05},
	{0x400c, 0x00},
	{0x400d, 0x03},
	{0x4507, 0x03},
	{0x4509, 0x80},
};

/* Supported sensor mode configurations */
static const struct ov9282_mode supported_modes[] = {
	[MODE_1280_800] = {
		.width = 1280,
		.height = 800,
		.hblank_min = { 250, 176 },
		.vblank = 1022,
		.vblank_min = 110,
		.vblank_max = 51540,
		.link_freq_idx = 0,
		.crop = {
			.left = OV9282_PIXEL_ARRAY_LEFT,
			.top = OV9282_PIXEL_ARRAY_TOP,
			.width = 1280,
			.height = 800
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1280x800_regs),
			.regs = mode_1280x800_regs,
		},
	},
	[MODE_1280_720] = {
		.width = 1280,
		.height = 720,
		.hblank_min = { 250, 176 },
		.vblank = 1022,
		.vblank_min = 41,
		.vblank_max = 51540,
		.link_freq_idx = 0,
		.crop = {
			/*
			 * Note that this mode takes the top 720 lines from the
			 * 800 of the sensor. It does not take a middle crop.
			 */
			.left = OV9282_PIXEL_ARRAY_LEFT,
			.top = OV9282_PIXEL_ARRAY_TOP,
			.width = 1280,
			.height = 720
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1280x720_regs),
			.regs = mode_1280x720_regs,
		},
	},
	[MODE_640_400] = {
		.width = 640,
		.height = 400,
		.hblank_min = { 890, 816 },
		.vblank = 1022,
		.vblank_min = 22,
		.vblank_max = 51540,
		.link_freq_idx = 0,
		.crop = {
			.left = OV9282_PIXEL_ARRAY_LEFT,
			.top = OV9282_PIXEL_ARRAY_TOP,
			.width = 1280,
			.height = 800
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_640x400_regs),
			.regs = mode_640x400_regs,
		},
	},
};

/**
 * to_ov9282() - ov9282 V4L2 sub-device to ov9282 device.
 * @subdev: pointer to ov9282 V4L2 sub-device
 *
 * Return: pointer to ov9282 device
 */
static inline struct ov9282 *to_ov9282(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct ov9282, sd);
}

/**
 * ov9282_read_reg() - Read registers.
 * @ov9282: pointer to ov9282 device
 * @reg: register address
 * @len: length of bytes to read. Max supported bytes is 4
 * @val: pointer to register value to be filled.
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_read_reg(struct ov9282 *ov9282, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov9282->sd);
	struct i2c_msg msgs[2] = {0};
	u8 addr_buf[2] = {0};
	u8 data_buf[4] = {0};
	int ret;

	if (WARN_ON(len > 4))
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

/**
 * ov9282_write_reg() - Write register
 * @ov9282: pointer to ov9282 device
 * @reg: register address
 * @len: length of bytes. Max supported bytes is 4
 * @val: register value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_write_reg(struct ov9282 *ov9282, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov9282->sd);
	u8 buf[6] = {0};

	if (WARN_ON(len > 4))
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/**
 * ov9282_write_regs() - Write a list of registers
 * @ov9282: pointer to ov9282 device
 * @regs: list of registers to be written
 * @len: length of registers array
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_write_regs(struct ov9282 *ov9282,
			     const struct ov9282_reg *regs, u32 len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = ov9282_write_reg(ov9282, regs[i].address, 1, regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * ov9282_update_controls() - Update control ranges based on streaming mode
 * @ov9282: pointer to ov9282 device
 * @mode: pointer to ov9282_mode sensor mode
 * @fmt: pointer to the requested mode
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_update_controls(struct ov9282 *ov9282,
				  const struct ov9282_mode *mode,
				  const struct v4l2_subdev_format *fmt)
{
	u32 hblank_min;
	s64 pixel_rate;
	int ret;

	ret = __v4l2_ctrl_s_ctrl(ov9282->link_freq_ctrl, mode->link_freq_idx);
	if (ret)
		return ret;

	pixel_rate = (fmt->format.code == MEDIA_BUS_FMT_Y10_1X10) ?
		OV9282_PIXEL_RATE_10BIT : OV9282_PIXEL_RATE_8BIT;
	ret = __v4l2_ctrl_modify_range(ov9282->pixel_rate, pixel_rate,
				       pixel_rate, 1, pixel_rate);
	if (ret)
		return ret;

	hblank_min = mode->hblank_min[ov9282->noncontinuous_clock ? 0 : 1];
	ret =  __v4l2_ctrl_modify_range(ov9282->hblank_ctrl, hblank_min,
					OV9282_TIMING_HTS_MAX - mode->width, 1,
					hblank_min);
	if (ret)
		return ret;

	return __v4l2_ctrl_modify_range(ov9282->vblank_ctrl, mode->vblank_min,
					mode->vblank_max, 1, mode->vblank);
}

/**
 * ov9282_update_exp_gain() - Set updated exposure and gain
 * @ov9282: pointer to ov9282 device
 * @exposure: updated exposure value
 * @gain: updated analog gain value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_update_exp_gain(struct ov9282 *ov9282, u32 exposure, u32 gain)
{
	int ret;

	dev_dbg(ov9282->dev, "Set exp %u, analog gain %u",
		exposure, gain);

	ret = ov9282_write_reg(ov9282, OV9282_REG_HOLD, 1, 1);
	if (ret)
		return ret;

	ret = ov9282_write_reg(ov9282, OV9282_REG_EXPOSURE, 3, exposure << 4);
	if (ret)
		goto error_release_group_hold;

	ret = ov9282_write_reg(ov9282, OV9282_REG_AGAIN, 1, gain);

error_release_group_hold:
	ov9282_write_reg(ov9282, OV9282_REG_HOLD, 1, 0);

	return ret;
}

static int ov9282_set_ctrl_hflip(struct ov9282 *ov9282, int value)
{
	u32 current_val;
	int ret = ov9282_read_reg(ov9282, OV9282_REG_TIMING_FORMAT_2, 1,
				  &current_val);
	if (ret)
		return ret;

	if (value)
		current_val |= OV9282_FLIP_BIT;
	else
		current_val &= ~OV9282_FLIP_BIT;

	return ov9282_write_reg(ov9282, OV9282_REG_TIMING_FORMAT_2, 1,
				current_val);
}

static int ov9282_set_ctrl_vflip(struct ov9282 *ov9282, int value)
{
	u32 current_val;
	int ret = ov9282_read_reg(ov9282, OV9282_REG_TIMING_FORMAT_1, 1,
				  &current_val);
	if (ret)
		return ret;

	if (value)
		current_val |= OV9282_FLIP_BIT;
	else
		current_val &= ~OV9282_FLIP_BIT;

	return ov9282_write_reg(ov9282, OV9282_REG_TIMING_FORMAT_1, 1,
				current_val);
}

/**
 * ov9282_set_ctrl() - Set subdevice control
 * @ctrl: pointer to v4l2_ctrl structure
 *
 * Supported controls:
 * - V4L2_CID_VBLANK
 * - cluster controls:
 *   - V4L2_CID_ANALOGUE_GAIN
 *   - V4L2_CID_EXPOSURE
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov9282 *ov9282 =
		container_of(ctrl->handler, struct ov9282, ctrl_handler);
	u32 analog_gain;
	u32 exposure;
	u32 lpfr;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		ov9282->vblank = ov9282->vblank_ctrl->val;

		dev_dbg(ov9282->dev, "Received vblank %u, new lpfr %u",
			ov9282->vblank,
			ov9282->vblank + ov9282->cur_mode->height);

		ret = __v4l2_ctrl_modify_range(ov9282->exp_ctrl,
					       OV9282_EXPOSURE_MIN,
					       ov9282->vblank +
					       ov9282->cur_mode->height -
					       OV9282_EXPOSURE_OFFSET,
					       1, OV9282_EXPOSURE_DEFAULT);
		break;
	}

	/* Set controls only if sensor is in power on state */
	if (!pm_runtime_get_if_in_use(ov9282->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		exposure = ctrl->val;
		analog_gain = ov9282->again_ctrl->val;

		dev_dbg(ov9282->dev, "Received exp %u, analog gain %u",
			exposure, analog_gain);

		ret = ov9282_update_exp_gain(ov9282, exposure, analog_gain);
		break;
	case V4L2_CID_VBLANK:
		lpfr = ov9282->vblank + ov9282->cur_mode->height;
		ret = ov9282_write_reg(ov9282, OV9282_REG_LPFR, 2, lpfr);
		break;
	case V4L2_CID_HFLIP:
		ret = ov9282_set_ctrl_hflip(ov9282, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = ov9282_set_ctrl_vflip(ov9282, ctrl->val);
		break;
	case V4L2_CID_HBLANK:
		ret = ov9282_write_reg(ov9282, OV9282_REG_TIMING_HTS, 2,
				       (ctrl->val + ov9282->cur_mode->width) >> 1);
		break;
	default:
		dev_err(ov9282->dev, "Invalid control %d", ctrl->id);
		ret = -EINVAL;
	}

	pm_runtime_put(ov9282->dev);

	return ret;
}

/* V4l2 subdevice control ops*/
static const struct v4l2_ctrl_ops ov9282_ctrl_ops = {
	.s_ctrl = ov9282_set_ctrl,
};

/**
 * ov9282_enum_mbus_code() - Enumerate V4L2 sub-device mbus codes
 * @sd: pointer to ov9282 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @code: V4L2 sub-device code enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->index) {
	case 0:
		code->code = MEDIA_BUS_FMT_Y10_1X10;
		break;
	case 1:
		code->code = MEDIA_BUS_FMT_Y8_1X8;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * ov9282_enum_frame_size() - Enumerate V4L2 sub-device frame sizes
 * @sd: pointer to ov9282 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fsize: V4L2 sub-device size enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fsize)
{
	if (fsize->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fsize->code != MEDIA_BUS_FMT_Y10_1X10 &&
	    fsize->code != MEDIA_BUS_FMT_Y8_1X8)
		return -EINVAL;

	fsize->min_width = supported_modes[fsize->index].width;
	fsize->max_width = fsize->min_width;
	fsize->min_height = supported_modes[fsize->index].height;
	fsize->max_height = fsize->min_height;

	return 0;
}

/**
 * ov9282_fill_pad_format() - Fill subdevice pad format
 *                            from selected sensor mode
 * @ov9282: pointer to ov9282 device
 * @mode: pointer to ov9282_mode sensor mode
 * @code: mbus code to be stored
 * @fmt: V4L2 sub-device format need to be filled
 */
static void ov9282_fill_pad_format(struct ov9282 *ov9282,
				   const struct ov9282_mode *mode,
				   u32 code,
				   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = code;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->format.quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;
}

/**
 * ov9282_get_pad_format() - Get subdevice pad format
 * @sd: pointer to ov9282 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct ov9282 *ov9282 = to_ov9282(sd);

	mutex_lock(&ov9282->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		ov9282_fill_pad_format(ov9282, ov9282->cur_mode, ov9282->code,
				       fmt);
	}

	mutex_unlock(&ov9282->mutex);

	return 0;
}

/**
 * ov9282_set_pad_format() - Set subdevice pad format
 * @sd: pointer to ov9282 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct ov9282 *ov9282 = to_ov9282(sd);
	const struct ov9282_mode *mode;
	u32 code;
	int ret = 0;

	mutex_lock(&ov9282->mutex);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width,
				      fmt->format.height);
	if (fmt->format.code == MEDIA_BUS_FMT_Y8_1X8)
		code = MEDIA_BUS_FMT_Y8_1X8;
	else
		code = MEDIA_BUS_FMT_Y10_1X10;

	ov9282_fill_pad_format(ov9282, mode, code, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ret = ov9282_update_controls(ov9282, mode, fmt);
		if (!ret) {
			ov9282->cur_mode = mode;
			ov9282->code = code;
		}
	}

	mutex_unlock(&ov9282->mutex);

	return ret;
}

/**
 * ov9282_init_pad_cfg() - Initialize sub-device pad configuration
 * @sd: pointer to ov9282 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_init_pad_cfg(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state)
{
	struct ov9282 *ov9282 = to_ov9282(sd);
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	ov9282_fill_pad_format(ov9282, &supported_modes[DEFAULT_MODE],
			       ov9282->code, &fmt);

	return ov9282_set_pad_format(sd, sd_state, &fmt);
}

static const struct v4l2_rect *
__ov9282_get_pad_crop(struct ov9282 *ov9282,
		      struct v4l2_subdev_state *sd_state,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&ov9282->sd, sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &ov9282->cur_mode->crop;
	}

	return NULL;
}

static int ov9282_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		struct ov9282 *ov9282 = to_ov9282(sd);

		mutex_lock(&ov9282->mutex);
		sel->r = *__ov9282_get_pad_crop(ov9282, sd_state, sel->pad,
						sel->which);
		mutex_unlock(&ov9282->mutex);

		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = OV9282_NATIVE_WIDTH;
		sel->r.height = OV9282_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = OV9282_PIXEL_ARRAY_TOP;
		sel->r.left = OV9282_PIXEL_ARRAY_LEFT;
		sel->r.width = OV9282_PIXEL_ARRAY_WIDTH;
		sel->r.height = OV9282_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

/**
 * ov9282_start_streaming() - Start sensor stream
 * @ov9282: pointer to ov9282 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_start_streaming(struct ov9282 *ov9282)
{
	const struct ov9282_reg bitdepth_regs[2][2] = {
		{
			{OV9282_REG_PLL_CTRL_0D, OV9282_PLL_CTRL_0D_RAW10},
			{OV9282_REG_ANA_CORE_2, OV9282_ANA_CORE2_RAW10},
		}, {
			{OV9282_REG_PLL_CTRL_0D, OV9282_PLL_CTRL_0D_RAW8},
			{OV9282_REG_ANA_CORE_2, OV9282_ANA_CORE2_RAW8},
		}
	};
	const struct ov9282_reg_list *reg_list;
	int bitdepth_index;
	int ret;

	/* Write common registers */
	ret = ov9282_write_regs(ov9282, common_regs_list.regs,
				common_regs_list.num_of_regs);
	if (ret) {
		dev_err(ov9282->dev, "fail to write common registers");
		return ret;
	}

	bitdepth_index = ov9282->code == MEDIA_BUS_FMT_Y10_1X10 ? 0 : 1;
	ret = ov9282_write_regs(ov9282, bitdepth_regs[bitdepth_index], 2);
	if (ret) {
		dev_err(ov9282->dev, "fail to write bitdepth regs");
		return ret;
	}

	/* Write sensor mode registers */
	reg_list = &ov9282->cur_mode->reg_list;
	ret = ov9282_write_regs(ov9282, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(ov9282->dev, "fail to write initial registers");
		return ret;
	}

	/* Setup handler will write actual exposure and gain */
	ret =  __v4l2_ctrl_handler_setup(ov9282->sd.ctrl_handler);
	if (ret) {
		dev_err(ov9282->dev, "fail to setup handler");
		return ret;
	}

	/* Start streaming */
	ret = ov9282_write_reg(ov9282, OV9282_REG_MODE_SELECT,
			       1, OV9282_MODE_STREAMING);
	if (ret) {
		dev_err(ov9282->dev, "fail to start streaming");
		return ret;
	}

	return 0;
}

/**
 * ov9282_stop_streaming() - Stop sensor stream
 * @ov9282: pointer to ov9282 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_stop_streaming(struct ov9282 *ov9282)
{
	return ov9282_write_reg(ov9282, OV9282_REG_MODE_SELECT,
				1, OV9282_MODE_STANDBY);
}

/**
 * ov9282_set_stream() - Enable sensor streaming
 * @sd: pointer to ov9282 subdevice
 * @enable: set to enable sensor streaming
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov9282 *ov9282 = to_ov9282(sd);
	int ret;

	mutex_lock(&ov9282->mutex);

	if (ov9282->streaming == enable) {
		mutex_unlock(&ov9282->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_resume_and_get(ov9282->dev);
		if (ret)
			goto error_unlock;

		ret = ov9282_start_streaming(ov9282);
		if (ret)
			goto error_power_off;
	} else {
		ov9282_stop_streaming(ov9282);
		pm_runtime_put(ov9282->dev);
	}

	ov9282->streaming = enable;

	mutex_unlock(&ov9282->mutex);

	return 0;

error_power_off:
	pm_runtime_put(ov9282->dev);
error_unlock:
	mutex_unlock(&ov9282->mutex);

	return ret;
}

/**
 * ov9282_detect() - Detect ov9282 sensor
 * @ov9282: pointer to ov9282 device
 *
 * Return: 0 if successful, -EIO if sensor id does not match
 */
static int ov9282_detect(struct ov9282 *ov9282)
{
	int ret;
	u32 val;

	ret = ov9282_read_reg(ov9282, OV9282_REG_ID, 2, &val);
	if (ret)
		return ret;

	if (val != OV9282_ID) {
		dev_err(ov9282->dev, "chip id mismatch: %x!=%x",
			OV9282_ID, val);
		return -ENXIO;
	}

	return 0;
}

static int ov9282_configure_regulators(struct ov9282 *ov9282)
{
	unsigned int i;

	for (i = 0; i < OV9282_NUM_SUPPLIES; i++)
		ov9282->supplies[i].supply = ov9282_supply_names[i];

	return devm_regulator_bulk_get(ov9282->dev,
				       OV9282_NUM_SUPPLIES,
				       ov9282->supplies);
}

/**
 * ov9282_parse_hw_config() - Parse HW configuration and check if supported
 * @ov9282: pointer to ov9282 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_parse_hw_config(struct ov9282 *ov9282)
{
	struct fwnode_handle *fwnode = dev_fwnode(ov9282->dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *ep;
	unsigned long rate;
	unsigned int i;
	int ret;

	if (!fwnode)
		return -ENXIO;

	/* Request optional reset pin */
	ov9282->reset_gpio = devm_gpiod_get_optional(ov9282->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(ov9282->reset_gpio)) {
		dev_err(ov9282->dev, "failed to get reset gpio %ld",
			PTR_ERR(ov9282->reset_gpio));
		return PTR_ERR(ov9282->reset_gpio);
	}

	/* Get sensor input clock */
	ov9282->inclk = devm_clk_get(ov9282->dev, NULL);
	if (IS_ERR(ov9282->inclk)) {
		dev_err(ov9282->dev, "could not get inclk");
		return PTR_ERR(ov9282->inclk);
	}

	ret = ov9282_configure_regulators(ov9282);
	if (ret) {
		dev_err(ov9282->dev, "Failed to get power regulators\n");
		return ret;
	}

	rate = clk_get_rate(ov9282->inclk);
	if (rate != OV9282_INCLK_RATE) {
		dev_err(ov9282->dev, "inclk frequency mismatch");
		return -EINVAL;
	}

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	ov9282->noncontinuous_clock =
		bus_cfg.bus.mipi_csi2.flags & V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != OV9282_NUM_DATA_LANES) {
		dev_err(ov9282->dev,
			"number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(ov9282->dev, "no link frequencies defined");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++)
		if (bus_cfg.link_frequencies[i] == OV9282_LINK_FREQ)
			goto done_endpoint_free;

	ret = -EINVAL;

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

/* V4l2 subdevice ops */
static const struct v4l2_subdev_core_ops ov9282_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops ov9282_video_ops = {
	.s_stream = ov9282_set_stream,
};

static const struct v4l2_subdev_pad_ops ov9282_pad_ops = {
	.init_cfg = ov9282_init_pad_cfg,
	.enum_mbus_code = ov9282_enum_mbus_code,
	.enum_frame_size = ov9282_enum_frame_size,
	.get_fmt = ov9282_get_pad_format,
	.set_fmt = ov9282_set_pad_format,
	.get_selection = ov9282_get_selection,
};

static const struct v4l2_subdev_ops ov9282_subdev_ops = {
	.core = &ov9282_core_ops,
	.video = &ov9282_video_ops,
	.pad = &ov9282_pad_ops,
};

/**
 * ov9282_power_on() - Sensor power on sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov9282 *ov9282 = to_ov9282(sd);
	int ret;

	ret = regulator_bulk_enable(OV9282_NUM_SUPPLIES, ov9282->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		return ret;
	}

	usleep_range(400, 600);

	gpiod_set_value_cansleep(ov9282->reset_gpio, 1);

	ret = clk_prepare_enable(ov9282->inclk);
	if (ret) {
		dev_err(ov9282->dev, "fail to enable inclk");
		goto error_reset;
	}

	usleep_range(400, 600);

	ret = ov9282_write_reg(ov9282, OV9282_REG_MIPI_CTRL00, 1,
			       ov9282->noncontinuous_clock ?
					OV9282_GATED_CLOCK : 0);
	if (ret) {
		dev_err(ov9282->dev, "fail to write MIPI_CTRL00");
		goto error_clk;
	}

	return 0;

error_clk:
	clk_disable_unprepare(ov9282->inclk);
error_reset:
	gpiod_set_value_cansleep(ov9282->reset_gpio, 0);

	regulator_bulk_disable(OV9282_NUM_SUPPLIES, ov9282->supplies);

	return ret;
}

/**
 * ov9282_power_off() - Sensor power off sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov9282 *ov9282 = to_ov9282(sd);

	gpiod_set_value_cansleep(ov9282->reset_gpio, 0);

	clk_disable_unprepare(ov9282->inclk);

	regulator_bulk_disable(OV9282_NUM_SUPPLIES, ov9282->supplies);

	return 0;
}

/**
 * ov9282_init_controls() - Initialize sensor subdevice controls
 * @ov9282: pointer to ov9282 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_init_controls(struct ov9282 *ov9282)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &ov9282->ctrl_handler;
	const struct ov9282_mode *mode = ov9282->cur_mode;
	struct v4l2_fwnode_device_properties props;
	u32 hblank_min;
	u32 lpfr;
	int ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret)
		return ret;

	/* Serialize controls with sensor device */
	ctrl_hdlr->lock = &ov9282->mutex;

	/* Initialize exposure and gain */
	lpfr = mode->vblank + mode->height;
	ov9282->exp_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					     &ov9282_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     OV9282_EXPOSURE_MIN,
					     lpfr - OV9282_EXPOSURE_OFFSET,
					     OV9282_EXPOSURE_STEP,
					     OV9282_EXPOSURE_DEFAULT);

	ov9282->again_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					       &ov9282_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       OV9282_AGAIN_MIN,
					       OV9282_AGAIN_MAX,
					       OV9282_AGAIN_STEP,
					       OV9282_AGAIN_DEFAULT);

	v4l2_ctrl_cluster(2, &ov9282->exp_ctrl);

	ov9282->vblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&ov9282_ctrl_ops,
						V4L2_CID_VBLANK,
						mode->vblank_min,
						mode->vblank_max,
						1, mode->vblank);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov9282_ctrl_ops, V4L2_CID_VFLIP,
			  0, 1, 1, 1);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov9282_ctrl_ops, V4L2_CID_HFLIP,
			  0, 1, 1, 1);

	/* Read only controls */
	ov9282->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov9282_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       OV9282_PIXEL_RATE_10BIT,
					       OV9282_PIXEL_RATE_10BIT, 1,
					       OV9282_PIXEL_RATE_10BIT);

	ov9282->link_freq_ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&ov9282_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							ARRAY_SIZE(link_freq) -
							1,
							mode->link_freq_idx,
							link_freq);
	if (ov9282->link_freq_ctrl)
		ov9282->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	hblank_min = mode->hblank_min[ov9282->noncontinuous_clock ? 0 : 1];
	ov9282->hblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&ov9282_ctrl_ops,
						V4L2_CID_HBLANK,
						hblank_min,
						OV9282_TIMING_HTS_MAX - mode->width,
						1, hblank_min);

	ret = v4l2_fwnode_device_parse(ov9282->dev, &props);
	if (!ret) {
		/* Failure sets ctrl_hdlr->error, which we check afterwards anyway */
		v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ov9282_ctrl_ops,
						&props);
	}

	if (ctrl_hdlr->error || ret) {
		dev_err(ov9282->dev, "control init failed: %d",
			ctrl_hdlr->error);
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ctrl_hdlr->error;
	}

	ov9282->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

/**
 * ov9282_probe() - I2C client device binding
 * @client: pointer to i2c client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int ov9282_probe(struct i2c_client *client)
{
	struct ov9282 *ov9282;
	int ret;

	ov9282 = devm_kzalloc(&client->dev, sizeof(*ov9282), GFP_KERNEL);
	if (!ov9282)
		return -ENOMEM;

	ov9282->dev = &client->dev;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&ov9282->sd, client, &ov9282_subdev_ops);
	v4l2_i2c_subdev_set_name(&ov9282->sd, client,
				 device_get_match_data(ov9282->dev), NULL);

	ret = ov9282_parse_hw_config(ov9282);
	if (ret) {
		dev_err(ov9282->dev, "HW configuration is not supported");
		return ret;
	}

	mutex_init(&ov9282->mutex);

	ret = ov9282_power_on(ov9282->dev);
	if (ret) {
		dev_err(ov9282->dev, "failed to power-on the sensor");
		goto error_mutex_destroy;
	}

	/* Check module identity */
	ret = ov9282_detect(ov9282);
	if (ret) {
		dev_err(ov9282->dev, "failed to find sensor: %d", ret);
		goto error_power_off;
	}

	/* Set default mode to first mode */
	ov9282->cur_mode = &supported_modes[DEFAULT_MODE];
	ov9282->code = MEDIA_BUS_FMT_Y10_1X10;
	ov9282->vblank = ov9282->cur_mode->vblank;

	ret = ov9282_init_controls(ov9282);
	if (ret) {
		dev_err(ov9282->dev, "failed to init controls: %d", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	ov9282->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	ov9282->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	ov9282->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov9282->sd.entity, 1, &ov9282->pad);
	if (ret) {
		dev_err(ov9282->dev, "failed to init entity pads: %d", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&ov9282->sd);
	if (ret < 0) {
		dev_err(ov9282->dev,
			"failed to register async subdev: %d", ret);
		goto error_media_entity;
	}

	pm_runtime_set_active(ov9282->dev);
	pm_runtime_enable(ov9282->dev);
	pm_runtime_idle(ov9282->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&ov9282->sd.entity);
error_handler_free:
	v4l2_ctrl_handler_free(ov9282->sd.ctrl_handler);
error_power_off:
	ov9282_power_off(ov9282->dev);
error_mutex_destroy:
	mutex_destroy(&ov9282->mutex);

	return ret;
}

/**
 * ov9282_remove() - I2C client device unbinding
 * @client: pointer to I2C client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static void ov9282_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9282 *ov9282 = to_ov9282(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ov9282_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&ov9282->mutex);
}

static const struct dev_pm_ops ov9282_pm_ops = {
	SET_RUNTIME_PM_OPS(ov9282_power_off, ov9282_power_on, NULL)
};

static const struct of_device_id ov9282_of_match[] = {
	{ .compatible = "ovti,ov9281", .data = "ov9281" },
	{ .compatible = "ovti,ov9282", .data = "ov9282" },
	{ }
};

MODULE_DEVICE_TABLE(of, ov9282_of_match);

static struct i2c_driver ov9282_driver = {
	.probe_new = ov9282_probe,
	.remove = ov9282_remove,
	.driver = {
		.name = "ov9282",
		.pm = &ov9282_pm_ops,
		.of_match_table = ov9282_of_match,
	},
};

module_i2c_driver(ov9282_driver);

MODULE_DESCRIPTION("OmniVision ov9282 sensor driver");
MODULE_LICENSE("GPL");
