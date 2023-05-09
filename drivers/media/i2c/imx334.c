// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony imx334 sensor driver
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include <asm/unaligned.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Streaming Mode */
#define IMX334_REG_MODE_SELECT	0x3000
#define IMX334_MODE_STANDBY	0x01
#define IMX334_MODE_STREAMING	0x00

/* Lines per frame */
#define IMX334_REG_LPFR		0x3030

/* Chip ID */
#define IMX334_REG_ID		0x3044
#define IMX334_ID		0x1e

/* Exposure control */
#define IMX334_REG_SHUTTER	0x3058
#define IMX334_EXPOSURE_MIN	1
#define IMX334_EXPOSURE_OFFSET	5
#define IMX334_EXPOSURE_STEP	1
#define IMX334_EXPOSURE_DEFAULT	0x0648

/* Analog gain control */
#define IMX334_REG_AGAIN	0x30e8
#define IMX334_AGAIN_MIN	0
#define IMX334_AGAIN_MAX	240
#define IMX334_AGAIN_STEP	1
#define IMX334_AGAIN_DEFAULT	0

/* Group hold register */
#define IMX334_REG_HOLD		0x3001

/* Input clock rate */
#define IMX334_INCLK_RATE	24000000

/* CSI2 HW configuration */
#define IMX334_LINK_FREQ	891000000
#define IMX334_NUM_DATA_LANES	4

#define IMX334_REG_MIN		0x00
#define IMX334_REG_MAX		0xfffff

/**
 * struct imx334_reg - imx334 sensor register
 * @address: Register address
 * @val: Register value
 */
struct imx334_reg {
	u16 address;
	u8 val;
};

/**
 * struct imx334_reg_list - imx334 sensor register list
 * @num_of_regs: Number of registers in the list
 * @regs: Pointer to register list
 */
struct imx334_reg_list {
	u32 num_of_regs;
	const struct imx334_reg *regs;
};

/**
 * struct imx334_mode - imx334 sensor mode structure
 * @width: Frame width
 * @height: Frame height
 * @hblank: Horizontal blanking in lines
 * @vblank: Vertical blanking in lines
 * @vblank_min: Minimal vertical blanking in lines
 * @vblank_max: Maximum vertical blanking in lines
 * @pclk: Sensor pixel clock
 * @link_freq_idx: Link frequency index
 * @reg_list: Register list for sensor mode
 */
struct imx334_mode {
	u32 width;
	u32 height;
	u32 hblank;
	u32 vblank;
	u32 vblank_min;
	u32 vblank_max;
	u64 pclk;
	u32 link_freq_idx;
	struct imx334_reg_list reg_list;
};

/**
 * struct imx334 - imx334 sensor device structure
 * @dev: Pointer to generic device
 * @client: Pointer to i2c client
 * @sd: V4L2 sub-device
 * @pad: Media pad. Only one pad supported
 * @reset_gpio: Sensor reset gpio
 * @inclk: Sensor input clock
 * @ctrl_handler: V4L2 control handler
 * @link_freq_ctrl: Pointer to link frequency control
 * @pclk_ctrl: Pointer to pixel clock control
 * @hblank_ctrl: Pointer to horizontal blanking control
 * @vblank_ctrl: Pointer to vertical blanking control
 * @exp_ctrl: Pointer to exposure control
 * @again_ctrl: Pointer to analog gain control
 * @vblank: Vertical blanking in lines
 * @cur_mode: Pointer to current selected sensor mode
 * @mutex: Mutex for serializing sensor controls
 * @cur_code: current selected format code
 * @streaming: Flag indicating streaming state
 */
struct imx334 {
	struct device *dev;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct gpio_desc *reset_gpio;
	struct clk *inclk;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq_ctrl;
	struct v4l2_ctrl *pclk_ctrl;
	struct v4l2_ctrl *hblank_ctrl;
	struct v4l2_ctrl *vblank_ctrl;
	struct {
		struct v4l2_ctrl *exp_ctrl;
		struct v4l2_ctrl *again_ctrl;
	};
	u32 vblank;
	const struct imx334_mode *cur_mode;
	struct mutex mutex;
	u32 cur_code;
	bool streaming;
};

static const s64 link_freq[] = {
	IMX334_LINK_FREQ,
};

/* Sensor mode registers for 1920x1080@30fps */
static const struct imx334_reg mode_1920x1080_regs[] = {
	{0x3000, 0x01},
	{0x3018, 0x04},
	{0x3030, 0xca},
	{0x3031, 0x08},
	{0x3032, 0x00},
	{0x3034, 0x4c},
	{0x3035, 0x04},
	{0x302c, 0xf0},
	{0x302d, 0x03},
	{0x302e, 0x80},
	{0x302f, 0x07},
	{0x3074, 0xcc},
	{0x3075, 0x02},
	{0x308e, 0xcd},
	{0x308f, 0x02},
	{0x3076, 0x38},
	{0x3077, 0x04},
	{0x3090, 0x38},
	{0x3091, 0x04},
	{0x3308, 0x38},
	{0x3309, 0x04},
	{0x30C6, 0x00},
	{0x30c7, 0x00},
	{0x30ce, 0x00},
	{0x30cf, 0x00},
	{0x30d8, 0x18},
	{0x30d9, 0x0a},
	{0x304c, 0x00},
	{0x304e, 0x00},
	{0x304f, 0x00},
	{0x3050, 0x00},
	{0x30b6, 0x00},
	{0x30b7, 0x00},
	{0x3116, 0x08},
	{0x3117, 0x00},
	{0x31a0, 0x20},
	{0x31a1, 0x0f},
	{0x300c, 0x3b},
	{0x300d, 0x29},
	{0x314c, 0x29},
	{0x314d, 0x01},
	{0x315a, 0x06},
	{0x3168, 0xa0},
	{0x316a, 0x7e},
	{0x319e, 0x02},
	{0x3199, 0x00},
	{0x319d, 0x00},
	{0x31dd, 0x03},
	{0x3300, 0x00},
	{0x341c, 0xff},
	{0x341d, 0x01},
	{0x3a01, 0x03},
	{0x3a18, 0x7f},
	{0x3a19, 0x00},
	{0x3a1a, 0x37},
	{0x3a1b, 0x00},
	{0x3a1c, 0x37},
	{0x3a1d, 0x00},
	{0x3a1e, 0xf7},
	{0x3a1f, 0x00},
	{0x3a20, 0x3f},
	{0x3a21, 0x00},
	{0x3a20, 0x6f},
	{0x3a21, 0x00},
	{0x3a20, 0x3f},
	{0x3a21, 0x00},
	{0x3a20, 0x5f},
	{0x3a21, 0x00},
	{0x3a20, 0x2f},
	{0x3a21, 0x00},
	{0x3078, 0x02},
	{0x3079, 0x00},
	{0x307a, 0x00},
	{0x307b, 0x00},
	{0x3080, 0x02},
	{0x3081, 0x00},
	{0x3082, 0x00},
	{0x3083, 0x00},
	{0x3088, 0x02},
	{0x3094, 0x00},
	{0x3095, 0x00},
	{0x3096, 0x00},
	{0x309b, 0x02},
	{0x309c, 0x00},
	{0x309d, 0x00},
	{0x309e, 0x00},
	{0x30a4, 0x00},
	{0x30a5, 0x00},
	{0x3288, 0x21},
	{0x328a, 0x02},
	{0x3414, 0x05},
	{0x3416, 0x18},
	{0x35Ac, 0x0e},
	{0x3648, 0x01},
	{0x364a, 0x04},
	{0x364c, 0x04},
	{0x3678, 0x01},
	{0x367c, 0x31},
	{0x367e, 0x31},
	{0x3708, 0x02},
	{0x3714, 0x01},
	{0x3715, 0x02},
	{0x3716, 0x02},
	{0x3717, 0x02},
	{0x371c, 0x3d},
	{0x371d, 0x3f},
	{0x372c, 0x00},
	{0x372d, 0x00},
	{0x372e, 0x46},
	{0x372f, 0x00},
	{0x3730, 0x89},
	{0x3731, 0x00},
	{0x3732, 0x08},
	{0x3733, 0x01},
	{0x3734, 0xfe},
	{0x3735, 0x05},
	{0x375d, 0x00},
	{0x375e, 0x00},
	{0x375f, 0x61},
	{0x3760, 0x06},
	{0x3768, 0x1b},
	{0x3769, 0x1b},
	{0x376a, 0x1a},
	{0x376b, 0x19},
	{0x376c, 0x18},
	{0x376d, 0x14},
	{0x376e, 0x0f},
	{0x3776, 0x00},
	{0x3777, 0x00},
	{0x3778, 0x46},
	{0x3779, 0x00},
	{0x377a, 0x08},
	{0x377b, 0x01},
	{0x377c, 0x45},
	{0x377d, 0x01},
	{0x377e, 0x23},
	{0x377f, 0x02},
	{0x3780, 0xd9},
	{0x3781, 0x03},
	{0x3782, 0xf5},
	{0x3783, 0x06},
	{0x3784, 0xa5},
	{0x3788, 0x0f},
	{0x378a, 0xd9},
	{0x378b, 0x03},
	{0x378c, 0xeb},
	{0x378d, 0x05},
	{0x378e, 0x87},
	{0x378f, 0x06},
	{0x3790, 0xf5},
	{0x3792, 0x43},
	{0x3794, 0x7a},
	{0x3796, 0xa1},
	{0x37b0, 0x37},
	{0x3e04, 0x0e},
	{0x30e8, 0x50},
	{0x30e9, 0x00},
	{0x3e04, 0x0e},
	{0x3002, 0x00},
};

/* Sensor mode registers for 3840x2160@30fps */
static const struct imx334_reg mode_3840x2160_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x00},
	{0x3018, 0x04},
	{0x37b0, 0x36},
	{0x304c, 0x00},
	{0x300c, 0x3b},
	{0x300d, 0x2a},
	{0x3034, 0x26},
	{0x3035, 0x02},
	{0x314c, 0x29},
	{0x314d, 0x01},
	{0x315a, 0x02},
	{0x3168, 0xa0},
	{0x316a, 0x7e},
	{0x3288, 0x21},
	{0x328a, 0x02},
	{0x302c, 0x3c},
	{0x302d, 0x00},
	{0x302e, 0x00},
	{0x302f, 0x0f},
	{0x3076, 0x70},
	{0x3077, 0x08},
	{0x3090, 0x70},
	{0x3091, 0x08},
	{0x30d8, 0x20},
	{0x30d9, 0x12},
	{0x3308, 0x70},
	{0x3309, 0x08},
	{0x3414, 0x05},
	{0x3416, 0x18},
	{0x35ac, 0x0e},
	{0x3648, 0x01},
	{0x364a, 0x04},
	{0x364c, 0x04},
	{0x3678, 0x01},
	{0x367c, 0x31},
	{0x367e, 0x31},
	{0x3708, 0x02},
	{0x3714, 0x01},
	{0x3715, 0x02},
	{0x3716, 0x02},
	{0x3717, 0x02},
	{0x371c, 0x3d},
	{0x371d, 0x3f},
	{0x372c, 0x00},
	{0x372d, 0x00},
	{0x372e, 0x46},
	{0x372f, 0x00},
	{0x3730, 0x89},
	{0x3731, 0x00},
	{0x3732, 0x08},
	{0x3733, 0x01},
	{0x3734, 0xfe},
	{0x3735, 0x05},
	{0x375d, 0x00},
	{0x375e, 0x00},
	{0x375f, 0x61},
	{0x3760, 0x06},
	{0x3768, 0x1b},
	{0x3769, 0x1b},
	{0x376a, 0x1a},
	{0x376b, 0x19},
	{0x376c, 0x18},
	{0x376d, 0x14},
	{0x376e, 0x0f},
	{0x3776, 0x00},
	{0x3777, 0x00},
	{0x3778, 0x46},
	{0x3779, 0x00},
	{0x377a, 0x08},
	{0x377b, 0x01},
	{0x377c, 0x45},
	{0x377d, 0x01},
	{0x377e, 0x23},
	{0x377f, 0x02},
	{0x3780, 0xd9},
	{0x3781, 0x03},
	{0x3782, 0xf5},
	{0x3783, 0x06},
	{0x3784, 0xa5},
	{0x3788, 0x0f},
	{0x378a, 0xd9},
	{0x378b, 0x03},
	{0x378c, 0xeb},
	{0x378d, 0x05},
	{0x378e, 0x87},
	{0x378f, 0x06},
	{0x3790, 0xf5},
	{0x3792, 0x43},
	{0x3794, 0x7a},
	{0x3796, 0xa1},
	{0x3e04, 0x0e},
	{0x319e, 0x00},
	{0x3a00, 0x01},
	{0x3a18, 0xbf},
	{0x3a19, 0x00},
	{0x3a1a, 0x67},
	{0x3a1b, 0x00},
	{0x3a1c, 0x6f},
	{0x3a1d, 0x00},
	{0x3a1e, 0xd7},
	{0x3a1f, 0x01},
	{0x3a20, 0x6f},
	{0x3a21, 0x00},
	{0x3a22, 0xcf},
	{0x3a23, 0x00},
	{0x3a24, 0x6f},
	{0x3a25, 0x00},
	{0x3a26, 0xb7},
	{0x3a27, 0x00},
	{0x3a28, 0x5f},
	{0x3a29, 0x00},
};

static const struct imx334_reg raw10_framefmt_regs[] = {
	{0x3050, 0x00},
	{0x319d, 0x00},
	{0x341c, 0xff},
	{0x341d, 0x01},
};

static const struct imx334_reg raw12_framefmt_regs[] = {
	{0x3050, 0x01},
	{0x319d, 0x01},
	{0x341c, 0x47},
	{0x341d, 0x00},
};

static const u32 imx334_mbus_codes[] = {
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SRGGB10_1X10,
};

/* Supported sensor mode configurations */
static const struct imx334_mode supported_modes[] = {
	{
		.width = 3840,
		.height = 2160,
		.hblank = 560,
		.vblank = 2340,
		.vblank_min = 90,
		.vblank_max = 132840,
		.pclk = 594000000,
		.link_freq_idx = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_3840x2160_regs),
			.regs = mode_3840x2160_regs,
		},
	}, {
		.width = 1920,
		.height = 1080,
		.hblank = 2480,
		.vblank = 1170,
		.vblank_min = 45,
		.vblank_max = 132840,
		.pclk = 297000000,
		.link_freq_idx = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1920x1080_regs),
			.regs = mode_1920x1080_regs,
		},
	},
};

/**
 * to_imx334() - imv334 V4L2 sub-device to imx334 device.
 * @subdev: pointer to imx334 V4L2 sub-device
 *
 * Return: pointer to imx334 device
 */
static inline struct imx334 *to_imx334(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct imx334, sd);
}

/**
 * imx334_read_reg() - Read registers.
 * @imx334: pointer to imx334 device
 * @reg: register address
 * @len: length of bytes to read. Max supported bytes is 4
 * @val: pointer to register value to be filled.
 *
 * Big endian register addresses with little endian values.
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_read_reg(struct imx334 *imx334, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx334->sd);
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
	msgs[1].buf = data_buf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_le32(data_buf);

	return 0;
}

/**
 * imx334_write_reg() - Write register
 * @imx334: pointer to imx334 device
 * @reg: register address
 * @len: length of bytes. Max supported bytes is 4
 * @val: register value
 *
 * Big endian register addresses with little endian values.
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_write_reg(struct imx334 *imx334, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx334->sd);
	u8 buf[6] = {0};

	if (WARN_ON(len > 4))
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_le32(val, buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/**
 * imx334_write_regs() - Write a list of registers
 * @imx334: pointer to imx334 device
 * @regs: list of registers to be written
 * @len: length of registers array
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_write_regs(struct imx334 *imx334,
			     const struct imx334_reg *regs, u32 len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx334_write_reg(imx334, regs[i].address, 1, regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * imx334_update_controls() - Update control ranges based on streaming mode
 * @imx334: pointer to imx334 device
 * @mode: pointer to imx334_mode sensor mode
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_update_controls(struct imx334 *imx334,
				  const struct imx334_mode *mode)
{
	int ret;

	ret = __v4l2_ctrl_s_ctrl(imx334->link_freq_ctrl, mode->link_freq_idx);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_modify_range(imx334->hblank_ctrl, mode->hblank,
				       mode->hblank, 1, mode->hblank);
	if (ret)
		return ret;

	return __v4l2_ctrl_modify_range(imx334->vblank_ctrl, mode->vblank_min,
					mode->vblank_max, 1, mode->vblank);
}

/**
 * imx334_update_exp_gain() - Set updated exposure and gain
 * @imx334: pointer to imx334 device
 * @exposure: updated exposure value
 * @gain: updated analog gain value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_update_exp_gain(struct imx334 *imx334, u32 exposure, u32 gain)
{
	u32 lpfr, shutter;
	int ret;

	lpfr = imx334->vblank + imx334->cur_mode->height;
	shutter = lpfr - exposure;

	dev_dbg(imx334->dev, "Set long exp %u analog gain %u sh0 %u lpfr %u",
		exposure, gain, shutter, lpfr);

	ret = imx334_write_reg(imx334, IMX334_REG_HOLD, 1, 1);
	if (ret)
		return ret;

	ret = imx334_write_reg(imx334, IMX334_REG_LPFR, 3, lpfr);
	if (ret)
		goto error_release_group_hold;

	ret = imx334_write_reg(imx334, IMX334_REG_SHUTTER, 3, shutter);
	if (ret)
		goto error_release_group_hold;

	ret = imx334_write_reg(imx334, IMX334_REG_AGAIN, 1, gain);

error_release_group_hold:
	imx334_write_reg(imx334, IMX334_REG_HOLD, 1, 0);

	return ret;
}

/**
 * imx334_set_ctrl() - Set subdevice control
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
static int imx334_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx334 *imx334 =
		container_of(ctrl->handler, struct imx334, ctrl_handler);
	u32 analog_gain;
	u32 exposure;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		imx334->vblank = imx334->vblank_ctrl->val;

		dev_dbg(imx334->dev, "Received vblank %u, new lpfr %u",
			imx334->vblank,
			imx334->vblank + imx334->cur_mode->height);

		ret = __v4l2_ctrl_modify_range(imx334->exp_ctrl,
					       IMX334_EXPOSURE_MIN,
					       imx334->vblank +
					       imx334->cur_mode->height -
					       IMX334_EXPOSURE_OFFSET,
					       1, IMX334_EXPOSURE_DEFAULT);
		break;
	case V4L2_CID_EXPOSURE:

		/* Set controls only if sensor is in power on state */
		if (!pm_runtime_get_if_in_use(imx334->dev))
			return 0;

		exposure = ctrl->val;
		analog_gain = imx334->again_ctrl->val;

		dev_dbg(imx334->dev, "Received exp %u analog gain %u",
			exposure, analog_gain);

		ret = imx334_update_exp_gain(imx334, exposure, analog_gain);

		pm_runtime_put(imx334->dev);

		break;
	case V4L2_CID_HBLANK:
		ret = 0;
		break;
	default:
		dev_err(imx334->dev, "Invalid control %d", ctrl->id);
		ret = -EINVAL;
	}

	return ret;
}

/* V4l2 subdevice control ops*/
static const struct v4l2_ctrl_ops imx334_ctrl_ops = {
	.s_ctrl = imx334_set_ctrl,
};

static int imx334_get_format_code(struct imx334 *imx334, u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx334_mbus_codes); i++) {
		if (imx334_mbus_codes[i] == code)
			return imx334_mbus_codes[i];
	}

	return imx334_mbus_codes[0];
}

/**
 * imx334_enum_mbus_code() - Enumerate V4L2 sub-device mbus codes
 * @sd: pointer to imx334 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device state
 * @code: V4L2 sub-device code enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(imx334_mbus_codes))
		return -EINVAL;

	code->code = imx334_mbus_codes[code->index];

	return 0;
}

/**
 * imx334_enum_frame_size() - Enumerate V4L2 sub-device frame sizes
 * @sd: pointer to imx334 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device state
 * @fsize: V4L2 sub-device size enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fsize)
{
	struct imx334 *imx334 = to_imx334(sd);
	u32 code;

	if (fsize->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	code = imx334_get_format_code(imx334, fsize->code);

	if (fsize->code != code)
		return -EINVAL;

	fsize->min_width = supported_modes[fsize->index].width;
	fsize->max_width = fsize->min_width;
	fsize->min_height = supported_modes[fsize->index].height;
	fsize->max_height = fsize->min_height;

	return 0;
}

/**
 * imx334_fill_pad_format() - Fill subdevice pad format
 *                            from selected sensor mode
 * @imx334: pointer to imx334 device
 * @mode: pointer to imx334_mode sensor mode
 * @fmt: V4L2 sub-device format need to be filled
 */
static void imx334_fill_pad_format(struct imx334 *imx334,
				   const struct imx334_mode *mode,
				   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->format.quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;
}

/**
 * imx334_get_pad_format() - Get subdevice pad format
 * @sd: pointer to imx334 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device state
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx334 *imx334 = to_imx334(sd);

	mutex_lock(&imx334->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		fmt->format.code = imx334->cur_code;
		imx334_fill_pad_format(imx334, imx334->cur_mode, fmt);
	}

	mutex_unlock(&imx334->mutex);

	return 0;
}

/**
 * imx334_set_pad_format() - Set subdevice pad format
 * @sd: pointer to imx334 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device state
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx334 *imx334 = to_imx334(sd);
	const struct imx334_mode *mode;
	int ret = 0;

	mutex_lock(&imx334->mutex);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	imx334_fill_pad_format(imx334, mode, fmt);
	fmt->format.code = imx334_get_format_code(imx334, fmt->format.code);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else if (imx334->cur_mode != mode || imx334->cur_code != fmt->format.code) {
		imx334->cur_code = fmt->format.code;
		ret = imx334_update_controls(imx334, mode);
		if (!ret)
			imx334->cur_mode = mode;
	}

	mutex_unlock(&imx334->mutex);

	return ret;
}

/**
 * imx334_init_pad_cfg() - Initialize sub-device pad configuration
 * @sd: pointer to imx334 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device state
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_init_pad_cfg(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state)
{
	struct imx334 *imx334 = to_imx334(sd);
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	imx334_fill_pad_format(imx334, &supported_modes[0], &fmt);

	return imx334_set_pad_format(sd, sd_state, &fmt);
}

static int imx334_set_framefmt(struct imx334 *imx334)
{
	switch (imx334->cur_code) {
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return imx334_write_regs(imx334, raw10_framefmt_regs,
					 ARRAY_SIZE(raw10_framefmt_regs));

	case MEDIA_BUS_FMT_SRGGB12_1X12:
		return imx334_write_regs(imx334, raw12_framefmt_regs,
					 ARRAY_SIZE(raw12_framefmt_regs));
	}

	return -EINVAL;
}

/**
 * imx334_start_streaming() - Start sensor stream
 * @imx334: pointer to imx334 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_start_streaming(struct imx334 *imx334)
{
	const struct imx334_reg_list *reg_list;
	int ret;

	/* Write sensor mode registers */
	reg_list = &imx334->cur_mode->reg_list;
	ret = imx334_write_regs(imx334, reg_list->regs,
				reg_list->num_of_regs);
	if (ret) {
		dev_err(imx334->dev, "fail to write initial registers");
		return ret;
	}

	ret = imx334_set_framefmt(imx334);
	if (ret) {
		dev_err(imx334->dev, "%s failed to set frame format: %d\n",
			__func__, ret);
		return ret;
	}

	/* Setup handler will write actual exposure and gain */
	ret =  __v4l2_ctrl_handler_setup(imx334->sd.ctrl_handler);
	if (ret) {
		dev_err(imx334->dev, "fail to setup handler");
		return ret;
	}

	/* Start streaming */
	ret = imx334_write_reg(imx334, IMX334_REG_MODE_SELECT,
			       1, IMX334_MODE_STREAMING);
	if (ret) {
		dev_err(imx334->dev, "fail to start streaming");
		return ret;
	}

	return 0;
}

/**
 * imx334_stop_streaming() - Stop sensor stream
 * @imx334: pointer to imx334 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_stop_streaming(struct imx334 *imx334)
{
	return imx334_write_reg(imx334, IMX334_REG_MODE_SELECT,
				1, IMX334_MODE_STANDBY);
}

/**
 * imx334_set_stream() - Enable sensor streaming
 * @sd: pointer to imx334 subdevice
 * @enable: set to enable sensor streaming
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx334 *imx334 = to_imx334(sd);
	int ret;

	mutex_lock(&imx334->mutex);

	if (imx334->streaming == enable) {
		mutex_unlock(&imx334->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_resume_and_get(imx334->dev);
		if (ret < 0)
			goto error_unlock;

		ret = imx334_start_streaming(imx334);
		if (ret)
			goto error_power_off;
	} else {
		imx334_stop_streaming(imx334);
		pm_runtime_put(imx334->dev);
	}

	imx334->streaming = enable;

	mutex_unlock(&imx334->mutex);

	return 0;

error_power_off:
	pm_runtime_put(imx334->dev);
error_unlock:
	mutex_unlock(&imx334->mutex);

	return ret;
}

/**
 * imx334_detect() - Detect imx334 sensor
 * @imx334: pointer to imx334 device
 *
 * Return: 0 if successful, -EIO if sensor id does not match
 */
static int imx334_detect(struct imx334 *imx334)
{
	int ret;
	u32 val;

	ret = imx334_read_reg(imx334, IMX334_REG_ID, 2, &val);
	if (ret)
		return ret;

	if (val != IMX334_ID) {
		dev_err(imx334->dev, "chip id mismatch: %x!=%x",
			IMX334_ID, val);
		return -ENXIO;
	}

	return 0;
}

/**
 * imx334_parse_hw_config() - Parse HW configuration and check if supported
 * @imx334: pointer to imx334 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_parse_hw_config(struct imx334 *imx334)
{
	struct fwnode_handle *fwnode = dev_fwnode(imx334->dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *ep;
	unsigned long rate;
	int ret;
	int i;

	if (!fwnode)
		return -ENXIO;

	/* Request optional reset pin */
	imx334->reset_gpio = devm_gpiod_get_optional(imx334->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx334->reset_gpio)) {
		dev_err(imx334->dev, "failed to get reset gpio %ld",
			PTR_ERR(imx334->reset_gpio));
		return PTR_ERR(imx334->reset_gpio);
	}

	/* Get sensor input clock */
	imx334->inclk = devm_clk_get(imx334->dev, NULL);
	if (IS_ERR(imx334->inclk)) {
		dev_err(imx334->dev, "could not get inclk");
		return PTR_ERR(imx334->inclk);
	}

	rate = clk_get_rate(imx334->inclk);
	if (rate != IMX334_INCLK_RATE) {
		dev_err(imx334->dev, "inclk frequency mismatch");
		return -EINVAL;
	}

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != IMX334_NUM_DATA_LANES) {
		dev_err(imx334->dev,
			"number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(imx334->dev, "no link frequencies defined");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++)
		if (bus_cfg.link_frequencies[i] == IMX334_LINK_FREQ)
			goto done_endpoint_free;

	ret = -EINVAL;

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

/* V4l2 subdevice ops */
static const struct v4l2_subdev_video_ops imx334_video_ops = {
	.s_stream = imx334_set_stream,
};

static const struct v4l2_subdev_pad_ops imx334_pad_ops = {
	.init_cfg = imx334_init_pad_cfg,
	.enum_mbus_code = imx334_enum_mbus_code,
	.enum_frame_size = imx334_enum_frame_size,
	.get_fmt = imx334_get_pad_format,
	.set_fmt = imx334_set_pad_format,
};

static const struct v4l2_subdev_ops imx334_subdev_ops = {
	.video = &imx334_video_ops,
	.pad = &imx334_pad_ops,
};

/**
 * imx334_power_on() - Sensor power on sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx334 *imx334 = to_imx334(sd);
	int ret;

	gpiod_set_value_cansleep(imx334->reset_gpio, 1);

	ret = clk_prepare_enable(imx334->inclk);
	if (ret) {
		dev_err(imx334->dev, "fail to enable inclk");
		goto error_reset;
	}

	usleep_range(18000, 20000);

	return 0;

error_reset:
	gpiod_set_value_cansleep(imx334->reset_gpio, 0);

	return ret;
}

/**
 * imx334_power_off() - Sensor power off sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx334 *imx334 = to_imx334(sd);

	gpiod_set_value_cansleep(imx334->reset_gpio, 0);

	clk_disable_unprepare(imx334->inclk);

	return 0;
}

/**
 * imx334_init_controls() - Initialize sensor subdevice controls
 * @imx334: pointer to imx334 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_init_controls(struct imx334 *imx334)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &imx334->ctrl_handler;
	const struct imx334_mode *mode = imx334->cur_mode;
	u32 lpfr;
	int ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 6);
	if (ret)
		return ret;

	/* Serialize controls with sensor device */
	ctrl_hdlr->lock = &imx334->mutex;

	/* Initialize exposure and gain */
	lpfr = mode->vblank + mode->height;
	imx334->exp_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					     &imx334_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX334_EXPOSURE_MIN,
					     lpfr - IMX334_EXPOSURE_OFFSET,
					     IMX334_EXPOSURE_STEP,
					     IMX334_EXPOSURE_DEFAULT);

	imx334->again_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					       &imx334_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       IMX334_AGAIN_MIN,
					       IMX334_AGAIN_MAX,
					       IMX334_AGAIN_STEP,
					       IMX334_AGAIN_DEFAULT);

	v4l2_ctrl_cluster(2, &imx334->exp_ctrl);

	imx334->vblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx334_ctrl_ops,
						V4L2_CID_VBLANK,
						mode->vblank_min,
						mode->vblank_max,
						1, mode->vblank);

	/* Read only controls */
	imx334->pclk_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					      &imx334_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      mode->pclk, mode->pclk,
					      1, mode->pclk);

	imx334->link_freq_ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&imx334_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							ARRAY_SIZE(link_freq) -
							1,
							mode->link_freq_idx,
							link_freq);
	if (imx334->link_freq_ctrl)
		imx334->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx334->hblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx334_ctrl_ops,
						V4L2_CID_HBLANK,
						IMX334_REG_MIN,
						IMX334_REG_MAX,
						1, mode->hblank);
	if (imx334->hblank_ctrl)
		imx334->hblank_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	if (ctrl_hdlr->error) {
		dev_err(imx334->dev, "control init failed: %d",
			ctrl_hdlr->error);
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ctrl_hdlr->error;
	}

	imx334->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

/**
 * imx334_probe() - I2C client device binding
 * @client: pointer to i2c client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_probe(struct i2c_client *client)
{
	struct imx334 *imx334;
	int ret;

	imx334 = devm_kzalloc(&client->dev, sizeof(*imx334), GFP_KERNEL);
	if (!imx334)
		return -ENOMEM;

	imx334->dev = &client->dev;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx334->sd, client, &imx334_subdev_ops);

	ret = imx334_parse_hw_config(imx334);
	if (ret) {
		dev_err(imx334->dev, "HW configuration is not supported");
		return ret;
	}

	mutex_init(&imx334->mutex);

	ret = imx334_power_on(imx334->dev);
	if (ret) {
		dev_err(imx334->dev, "failed to power-on the sensor");
		goto error_mutex_destroy;
	}

	/* Check module identity */
	ret = imx334_detect(imx334);
	if (ret) {
		dev_err(imx334->dev, "failed to find sensor: %d", ret);
		goto error_power_off;
	}

	/* Set default mode to max resolution */
	imx334->cur_mode = &supported_modes[0];
	imx334->cur_code = imx334_mbus_codes[0];
	imx334->vblank = imx334->cur_mode->vblank;

	ret = imx334_init_controls(imx334);
	if (ret) {
		dev_err(imx334->dev, "failed to init controls: %d", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	imx334->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx334->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx334->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx334->sd.entity, 1, &imx334->pad);
	if (ret) {
		dev_err(imx334->dev, "failed to init entity pads: %d", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx334->sd);
	if (ret < 0) {
		dev_err(imx334->dev,
			"failed to register async subdev: %d", ret);
		goto error_media_entity;
	}

	pm_runtime_set_active(imx334->dev);
	pm_runtime_enable(imx334->dev);
	pm_runtime_idle(imx334->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&imx334->sd.entity);
error_handler_free:
	v4l2_ctrl_handler_free(imx334->sd.ctrl_handler);
error_power_off:
	imx334_power_off(imx334->dev);
error_mutex_destroy:
	mutex_destroy(&imx334->mutex);

	return ret;
}

/**
 * imx334_remove() - I2C client device unbinding
 * @client: pointer to I2C client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static void imx334_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx334 *imx334 = to_imx334(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	pm_runtime_suspended(&client->dev);

	mutex_destroy(&imx334->mutex);
}

static const struct dev_pm_ops imx334_pm_ops = {
	SET_RUNTIME_PM_OPS(imx334_power_off, imx334_power_on, NULL)
};

static const struct of_device_id imx334_of_match[] = {
	{ .compatible = "sony,imx334" },
	{ }
};

MODULE_DEVICE_TABLE(of, imx334_of_match);

static struct i2c_driver imx334_driver = {
	.probe_new = imx334_probe,
	.remove = imx334_remove,
	.driver = {
		.name = "imx334",
		.pm = &imx334_pm_ops,
		.of_match_table = imx334_of_match,
	},
};

module_i2c_driver(imx334_driver);

MODULE_DESCRIPTION("Sony imx334 sensor driver");
MODULE_LICENSE("GPL");
