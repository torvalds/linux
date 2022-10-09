// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony imx412 Camera Sensor Driver
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
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Streaming Mode */
#define IMX412_REG_MODE_SELECT	0x0100
#define IMX412_MODE_STANDBY	0x00
#define IMX412_MODE_STREAMING	0x01

/* Lines per frame */
#define IMX412_REG_LPFR		0x0340

/* Chip ID */
#define IMX412_REG_ID		0x0016
#define IMX412_ID		0x577

/* Exposure control */
#define IMX412_REG_EXPOSURE_CIT	0x0202
#define IMX412_EXPOSURE_MIN	8
#define IMX412_EXPOSURE_OFFSET	22
#define IMX412_EXPOSURE_STEP	1
#define IMX412_EXPOSURE_DEFAULT	0x0648

/* Analog gain control */
#define IMX412_REG_AGAIN	0x0204
#define IMX412_AGAIN_MIN	0
#define IMX412_AGAIN_MAX	978
#define IMX412_AGAIN_STEP	1
#define IMX412_AGAIN_DEFAULT	0

/* Group hold register */
#define IMX412_REG_HOLD		0x0104

/* Input clock rate */
#define IMX412_INCLK_RATE	24000000

/* CSI2 HW configuration */
#define IMX412_LINK_FREQ	600000000
#define IMX412_NUM_DATA_LANES	4

#define IMX412_REG_MIN		0x00
#define IMX412_REG_MAX		0xffff

/**
 * struct imx412_reg - imx412 sensor register
 * @address: Register address
 * @val: Register value
 */
struct imx412_reg {
	u16 address;
	u8 val;
};

/**
 * struct imx412_reg_list - imx412 sensor register list
 * @num_of_regs: Number of registers in the list
 * @regs: Pointer to register list
 */
struct imx412_reg_list {
	u32 num_of_regs;
	const struct imx412_reg *regs;
};

/**
 * struct imx412_mode - imx412 sensor mode structure
 * @width: Frame width
 * @height: Frame height
 * @code: Format code
 * @hblank: Horizontal blanking in lines
 * @vblank: Vertical blanking in lines
 * @vblank_min: Minimum vertical blanking in lines
 * @vblank_max: Maximum vertical blanking in lines
 * @pclk: Sensor pixel clock
 * @link_freq_idx: Link frequency index
 * @reg_list: Register list for sensor mode
 */
struct imx412_mode {
	u32 width;
	u32 height;
	u32 code;
	u32 hblank;
	u32 vblank;
	u32 vblank_min;
	u32 vblank_max;
	u64 pclk;
	u32 link_freq_idx;
	struct imx412_reg_list reg_list;
};

static const char * const imx412_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

/**
 * struct imx412 - imx412 sensor device structure
 * @dev: Pointer to generic device
 * @client: Pointer to i2c client
 * @sd: V4L2 sub-device
 * @pad: Media pad. Only one pad supported
 * @reset_gpio: Sensor reset gpio
 * @inclk: Sensor input clock
 * @supplies: Regulator supplies
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
 * @streaming: Flag indicating streaming state
 */
struct imx412 {
	struct device *dev;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct gpio_desc *reset_gpio;
	struct clk *inclk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx412_supply_names)];
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
	const struct imx412_mode *cur_mode;
	struct mutex mutex;
	bool streaming;
};

static const s64 link_freq[] = {
	IMX412_LINK_FREQ,
};

/* Sensor mode registers */
static const struct imx412_reg mode_4056x3040_regs[] = {
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0x3c7e, 0x08},
	{0x3c7f, 0x02},
	{0x38a8, 0x1f},
	{0x38a9, 0xff},
	{0x38aa, 0x1f},
	{0x38ab, 0xff},
	{0x55d4, 0x00},
	{0x55d5, 0x00},
	{0x55d6, 0x07},
	{0x55d7, 0xff},
	{0x55e8, 0x07},
	{0x55e9, 0xff},
	{0x55ea, 0x00},
	{0x55eb, 0x00},
	{0x575c, 0x07},
	{0x575d, 0xff},
	{0x575e, 0x00},
	{0x575f, 0x00},
	{0x5764, 0x00},
	{0x5765, 0x00},
	{0x5766, 0x07},
	{0x5767, 0xff},
	{0x5974, 0x04},
	{0x5975, 0x01},
	{0x5f10, 0x09},
	{0x5f11, 0x92},
	{0x5f12, 0x32},
	{0x5f13, 0x72},
	{0x5f14, 0x16},
	{0x5f15, 0xba},
	{0x5f17, 0x13},
	{0x5f18, 0x24},
	{0x5f19, 0x60},
	{0x5f1a, 0xe3},
	{0x5f1b, 0xad},
	{0x5f1c, 0x74},
	{0x5f2d, 0x25},
	{0x5f5c, 0xd0},
	{0x6a22, 0x00},
	{0x6a23, 0x1d},
	{0x7ba8, 0x00},
	{0x7ba9, 0x00},
	{0x886b, 0x00},
	{0x9002, 0x0a},
	{0x9004, 0x1a},
	{0x9214, 0x93},
	{0x9215, 0x69},
	{0x9216, 0x93},
	{0x9217, 0x6b},
	{0x9218, 0x93},
	{0x9219, 0x6d},
	{0x921a, 0x57},
	{0x921b, 0x58},
	{0x921c, 0x57},
	{0x921d, 0x59},
	{0x921e, 0x57},
	{0x921f, 0x5a},
	{0x9220, 0x57},
	{0x9221, 0x5b},
	{0x9222, 0x93},
	{0x9223, 0x02},
	{0x9224, 0x93},
	{0x9225, 0x03},
	{0x9226, 0x93},
	{0x9227, 0x04},
	{0x9228, 0x93},
	{0x9229, 0x05},
	{0x922a, 0x98},
	{0x922b, 0x21},
	{0x922c, 0xb2},
	{0x922d, 0xdb},
	{0x922e, 0xb2},
	{0x922f, 0xdc},
	{0x9230, 0xb2},
	{0x9231, 0xdd},
	{0x9232, 0xe2},
	{0x9233, 0xe1},
	{0x9234, 0xb2},
	{0x9235, 0xe2},
	{0x9236, 0xb2},
	{0x9237, 0xe3},
	{0x9238, 0xb7},
	{0x9239, 0xb9},
	{0x923a, 0xb7},
	{0x923b, 0xbb},
	{0x923c, 0xb7},
	{0x923d, 0xbc},
	{0x923e, 0xb7},
	{0x923f, 0xc5},
	{0x9240, 0xb7},
	{0x9241, 0xc7},
	{0x9242, 0xb7},
	{0x9243, 0xc9},
	{0x9244, 0x98},
	{0x9245, 0x56},
	{0x9246, 0x98},
	{0x9247, 0x55},
	{0x9380, 0x00},
	{0x9381, 0x62},
	{0x9382, 0x00},
	{0x9383, 0x56},
	{0x9384, 0x00},
	{0x9385, 0x52},
	{0x9388, 0x00},
	{0x9389, 0x55},
	{0x938a, 0x00},
	{0x938b, 0x55},
	{0x938c, 0x00},
	{0x938d, 0x41},
	{0x5078, 0x01},
	{0x0112, 0x0a},
	{0x0113, 0x0a},
	{0x0114, 0x03},
	{0x0342, 0x11},
	{0x0343, 0xa0},
	{0x0340, 0x0d},
	{0x0341, 0xda},
	{0x3210, 0x00},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0f},
	{0x0349, 0xd7},
	{0x034a, 0x0b},
	{0x034b, 0xdf},
	{0x00e3, 0x00},
	{0x00e4, 0x00},
	{0x00e5, 0x01},
	{0x00fc, 0x0a},
	{0x00fd, 0x0a},
	{0x00fe, 0x0a},
	{0x00ff, 0x0a},
	{0xe013, 0x00},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x00},
	{0x3140, 0x02},
	{0x3241, 0x11},
	{0x3250, 0x03},
	{0x3e10, 0x00},
	{0x3e11, 0x00},
	{0x3f0d, 0x00},
	{0x3f42, 0x00},
	{0x3f43, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x0f},
	{0x040d, 0xd8},
	{0x040e, 0x0b},
	{0x040f, 0xe0},
	{0x034c, 0x0f},
	{0x034d, 0xd8},
	{0x034e, 0x0b},
	{0x034f, 0xe0},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x00},
	{0x0307, 0xc8},
	{0x0309, 0x0a},
	{0x030b, 0x01},
	{0x030d, 0x02},
	{0x030e, 0x01},
	{0x030f, 0x5e},
	{0x0310, 0x00},
	{0x0820, 0x12},
	{0x0821, 0xc0},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3e20, 0x01},
	{0x3e37, 0x00},
	{0x3f50, 0x00},
	{0x3f56, 0x00},
	{0x3f57, 0xe2},
	{0x3c0a, 0x5a},
	{0x3c0b, 0x55},
	{0x3c0c, 0x28},
	{0x3c0d, 0x07},
	{0x3c0e, 0xff},
	{0x3c0f, 0x00},
	{0x3c10, 0x00},
	{0x3c11, 0x02},
	{0x3c12, 0x00},
	{0x3c13, 0x03},
	{0x3c14, 0x00},
	{0x3c15, 0x00},
	{0x3c16, 0x0c},
	{0x3c17, 0x0c},
	{0x3c18, 0x0c},
	{0x3c19, 0x0a},
	{0x3c1a, 0x0a},
	{0x3c1b, 0x0a},
	{0x3c1c, 0x00},
	{0x3c1d, 0x00},
	{0x3c1e, 0x00},
	{0x3c1f, 0x00},
	{0x3c20, 0x00},
	{0x3c21, 0x00},
	{0x3c22, 0x3f},
	{0x3c23, 0x0a},
	{0x3e35, 0x01},
	{0x3f4a, 0x03},
	{0x3f4b, 0xbf},
	{0x3f26, 0x00},
	{0x0202, 0x0d},
	{0x0203, 0xc4},
	{0x0204, 0x00},
	{0x0205, 0x00},
	{0x020e, 0x01},
	{0x020f, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{0xbcf1, 0x00},
};

/* Supported sensor mode configurations */
static const struct imx412_mode supported_mode = {
	.width = 4056,
	.height = 3040,
	.hblank = 456,
	.vblank = 506,
	.vblank_min = 506,
	.vblank_max = 32420,
	.pclk = 480000000,
	.link_freq_idx = 0,
	.code = MEDIA_BUS_FMT_SRGGB10_1X10,
	.reg_list = {
		.num_of_regs = ARRAY_SIZE(mode_4056x3040_regs),
		.regs = mode_4056x3040_regs,
	},
};

/**
 * to_imx412() - imx412 V4L2 sub-device to imx412 device.
 * @subdev: pointer to imx412 V4L2 sub-device
 *
 * Return: pointer to imx412 device
 */
static inline struct imx412 *to_imx412(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct imx412, sd);
}

/**
 * imx412_read_reg() - Read registers.
 * @imx412: pointer to imx412 device
 * @reg: register address
 * @len: length of bytes to read. Max supported bytes is 4
 * @val: pointer to register value to be filled.
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_read_reg(struct imx412 *imx412, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx412->sd);
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
 * imx412_write_reg() - Write register
 * @imx412: pointer to imx412 device
 * @reg: register address
 * @len: length of bytes. Max supported bytes is 4
 * @val: register value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_write_reg(struct imx412 *imx412, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx412->sd);
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
 * imx412_write_regs() - Write a list of registers
 * @imx412: pointer to imx412 device
 * @regs: list of registers to be written
 * @len: length of registers array
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_write_regs(struct imx412 *imx412,
			     const struct imx412_reg *regs, u32 len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx412_write_reg(imx412, regs[i].address, 1, regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * imx412_update_controls() - Update control ranges based on streaming mode
 * @imx412: pointer to imx412 device
 * @mode: pointer to imx412_mode sensor mode
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_update_controls(struct imx412 *imx412,
				  const struct imx412_mode *mode)
{
	int ret;

	ret = __v4l2_ctrl_s_ctrl(imx412->link_freq_ctrl, mode->link_freq_idx);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_s_ctrl(imx412->hblank_ctrl, mode->hblank);
	if (ret)
		return ret;

	return __v4l2_ctrl_modify_range(imx412->vblank_ctrl, mode->vblank_min,
					mode->vblank_max, 1, mode->vblank);
}

/**
 * imx412_update_exp_gain() - Set updated exposure and gain
 * @imx412: pointer to imx412 device
 * @exposure: updated exposure value
 * @gain: updated analog gain value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_update_exp_gain(struct imx412 *imx412, u32 exposure, u32 gain)
{
	u32 lpfr, shutter;
	int ret;

	lpfr = imx412->vblank + imx412->cur_mode->height;
	shutter = lpfr - exposure;

	dev_dbg(imx412->dev, "Set exp %u, analog gain %u, shutter %u, lpfr %u",
		exposure, gain, shutter, lpfr);

	ret = imx412_write_reg(imx412, IMX412_REG_HOLD, 1, 1);
	if (ret)
		return ret;

	ret = imx412_write_reg(imx412, IMX412_REG_LPFR, 2, lpfr);
	if (ret)
		goto error_release_group_hold;

	ret = imx412_write_reg(imx412, IMX412_REG_EXPOSURE_CIT, 2, shutter);
	if (ret)
		goto error_release_group_hold;

	ret = imx412_write_reg(imx412, IMX412_REG_AGAIN, 2, gain);

error_release_group_hold:
	imx412_write_reg(imx412, IMX412_REG_HOLD, 1, 0);

	return ret;
}

/**
 * imx412_set_ctrl() - Set subdevice control
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
static int imx412_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx412 *imx412 =
		container_of(ctrl->handler, struct imx412, ctrl_handler);
	u32 analog_gain;
	u32 exposure;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		imx412->vblank = imx412->vblank_ctrl->val;

		dev_dbg(imx412->dev, "Received vblank %u, new lpfr %u",
			imx412->vblank,
			imx412->vblank + imx412->cur_mode->height);

		ret = __v4l2_ctrl_modify_range(imx412->exp_ctrl,
					       IMX412_EXPOSURE_MIN,
					       imx412->vblank +
					       imx412->cur_mode->height -
					       IMX412_EXPOSURE_OFFSET,
					       1, IMX412_EXPOSURE_DEFAULT);
		break;
	case V4L2_CID_EXPOSURE:
		/* Set controls only if sensor is in power on state */
		if (!pm_runtime_get_if_in_use(imx412->dev))
			return 0;

		exposure = ctrl->val;
		analog_gain = imx412->again_ctrl->val;

		dev_dbg(imx412->dev, "Received exp %u, analog gain %u",
			exposure, analog_gain);

		ret = imx412_update_exp_gain(imx412, exposure, analog_gain);

		pm_runtime_put(imx412->dev);

		break;
	default:
		dev_err(imx412->dev, "Invalid control %d", ctrl->id);
		ret = -EINVAL;
	}

	return ret;
}

/* V4l2 subdevice control ops*/
static const struct v4l2_ctrl_ops imx412_ctrl_ops = {
	.s_ctrl = imx412_set_ctrl,
};

/**
 * imx412_enum_mbus_code() - Enumerate V4L2 sub-device mbus codes
 * @sd: pointer to imx412 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @code: V4L2 sub-device code enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = supported_mode.code;

	return 0;
}

/**
 * imx412_enum_frame_size() - Enumerate V4L2 sub-device frame sizes
 * @sd: pointer to imx412 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fsize: V4L2 sub-device size enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fsize)
{
	if (fsize->index > 0)
		return -EINVAL;

	if (fsize->code != supported_mode.code)
		return -EINVAL;

	fsize->min_width = supported_mode.width;
	fsize->max_width = fsize->min_width;
	fsize->min_height = supported_mode.height;
	fsize->max_height = fsize->min_height;

	return 0;
}

/**
 * imx412_fill_pad_format() - Fill subdevice pad format
 *                            from selected sensor mode
 * @imx412: pointer to imx412 device
 * @mode: pointer to imx412_mode sensor mode
 * @fmt: V4L2 sub-device format need to be filled
 */
static void imx412_fill_pad_format(struct imx412 *imx412,
				   const struct imx412_mode *mode,
				   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = mode->code;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->format.quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;
}

/**
 * imx412_get_pad_format() - Get subdevice pad format
 * @sd: pointer to imx412 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx412 *imx412 = to_imx412(sd);

	mutex_lock(&imx412->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		imx412_fill_pad_format(imx412, imx412->cur_mode, fmt);
	}

	mutex_unlock(&imx412->mutex);

	return 0;
}

/**
 * imx412_set_pad_format() - Set subdevice pad format
 * @sd: pointer to imx412 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx412 *imx412 = to_imx412(sd);
	const struct imx412_mode *mode;
	int ret = 0;

	mutex_lock(&imx412->mutex);

	mode = &supported_mode;
	imx412_fill_pad_format(imx412, mode, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ret = imx412_update_controls(imx412, mode);
		if (!ret)
			imx412->cur_mode = mode;
	}

	mutex_unlock(&imx412->mutex);

	return ret;
}

/**
 * imx412_init_pad_cfg() - Initialize sub-device pad configuration
 * @sd: pointer to imx412 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_init_pad_cfg(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state)
{
	struct imx412 *imx412 = to_imx412(sd);
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	imx412_fill_pad_format(imx412, &supported_mode, &fmt);

	return imx412_set_pad_format(sd, sd_state, &fmt);
}

/**
 * imx412_start_streaming() - Start sensor stream
 * @imx412: pointer to imx412 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_start_streaming(struct imx412 *imx412)
{
	const struct imx412_reg_list *reg_list;
	int ret;

	/* Write sensor mode registers */
	reg_list = &imx412->cur_mode->reg_list;
	ret = imx412_write_regs(imx412, reg_list->regs,
				reg_list->num_of_regs);
	if (ret) {
		dev_err(imx412->dev, "fail to write initial registers");
		return ret;
	}

	/* Setup handler will write actual exposure and gain */
	ret =  __v4l2_ctrl_handler_setup(imx412->sd.ctrl_handler);
	if (ret) {
		dev_err(imx412->dev, "fail to setup handler");
		return ret;
	}

	/* Delay is required before streaming*/
	usleep_range(7400, 8000);

	/* Start streaming */
	ret = imx412_write_reg(imx412, IMX412_REG_MODE_SELECT,
			       1, IMX412_MODE_STREAMING);
	if (ret) {
		dev_err(imx412->dev, "fail to start streaming");
		return ret;
	}

	return 0;
}

/**
 * imx412_stop_streaming() - Stop sensor stream
 * @imx412: pointer to imx412 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_stop_streaming(struct imx412 *imx412)
{
	return imx412_write_reg(imx412, IMX412_REG_MODE_SELECT,
				1, IMX412_MODE_STANDBY);
}

/**
 * imx412_set_stream() - Enable sensor streaming
 * @sd: pointer to imx412 subdevice
 * @enable: set to enable sensor streaming
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx412 *imx412 = to_imx412(sd);
	int ret;

	mutex_lock(&imx412->mutex);

	if (imx412->streaming == enable) {
		mutex_unlock(&imx412->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_resume_and_get(imx412->dev);
		if (ret)
			goto error_unlock;

		ret = imx412_start_streaming(imx412);
		if (ret)
			goto error_power_off;
	} else {
		imx412_stop_streaming(imx412);
		pm_runtime_put(imx412->dev);
	}

	imx412->streaming = enable;

	mutex_unlock(&imx412->mutex);

	return 0;

error_power_off:
	pm_runtime_put(imx412->dev);
error_unlock:
	mutex_unlock(&imx412->mutex);

	return ret;
}

/**
 * imx412_detect() - Detect imx412 sensor
 * @imx412: pointer to imx412 device
 *
 * Return: 0 if successful, -EIO if sensor id does not match
 */
static int imx412_detect(struct imx412 *imx412)
{
	int ret;
	u32 val;

	ret = imx412_read_reg(imx412, IMX412_REG_ID, 2, &val);
	if (ret)
		return ret;

	if (val != IMX412_ID) {
		dev_err(imx412->dev, "chip id mismatch: %x!=%x",
			IMX412_ID, val);
		return -ENXIO;
	}

	return 0;
}

/**
 * imx412_parse_hw_config() - Parse HW configuration and check if supported
 * @imx412: pointer to imx412 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_parse_hw_config(struct imx412 *imx412)
{
	struct fwnode_handle *fwnode = dev_fwnode(imx412->dev);
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
	imx412->reset_gpio = devm_gpiod_get_optional(imx412->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx412->reset_gpio)) {
		dev_err(imx412->dev, "failed to get reset gpio %ld",
			PTR_ERR(imx412->reset_gpio));
		return PTR_ERR(imx412->reset_gpio);
	}

	/* Get sensor input clock */
	imx412->inclk = devm_clk_get(imx412->dev, NULL);
	if (IS_ERR(imx412->inclk)) {
		dev_err(imx412->dev, "could not get inclk");
		return PTR_ERR(imx412->inclk);
	}

	rate = clk_get_rate(imx412->inclk);
	if (rate != IMX412_INCLK_RATE) {
		dev_err(imx412->dev, "inclk frequency mismatch");
		return -EINVAL;
	}

	/* Get optional DT defined regulators */
	for (i = 0; i < ARRAY_SIZE(imx412_supply_names); i++)
		imx412->supplies[i].supply = imx412_supply_names[i];

	ret = devm_regulator_bulk_get(imx412->dev,
				      ARRAY_SIZE(imx412_supply_names),
				      imx412->supplies);
	if (ret)
		return ret;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != IMX412_NUM_DATA_LANES) {
		dev_err(imx412->dev,
			"number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(imx412->dev, "no link frequencies defined");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++)
		if (bus_cfg.link_frequencies[i] == IMX412_LINK_FREQ)
			goto done_endpoint_free;

	ret = -EINVAL;

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

/* V4l2 subdevice ops */
static const struct v4l2_subdev_video_ops imx412_video_ops = {
	.s_stream = imx412_set_stream,
};

static const struct v4l2_subdev_pad_ops imx412_pad_ops = {
	.init_cfg = imx412_init_pad_cfg,
	.enum_mbus_code = imx412_enum_mbus_code,
	.enum_frame_size = imx412_enum_frame_size,
	.get_fmt = imx412_get_pad_format,
	.set_fmt = imx412_set_pad_format,
};

static const struct v4l2_subdev_ops imx412_subdev_ops = {
	.video = &imx412_video_ops,
	.pad = &imx412_pad_ops,
};

/**
 * imx412_power_on() - Sensor power on sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx412 *imx412 = to_imx412(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(imx412_supply_names),
				    imx412->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}

	gpiod_set_value_cansleep(imx412->reset_gpio, 0);

	ret = clk_prepare_enable(imx412->inclk);
	if (ret) {
		dev_err(imx412->dev, "fail to enable inclk");
		goto error_reset;
	}

	usleep_range(1000, 1200);

	return 0;

error_reset:
	gpiod_set_value_cansleep(imx412->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(imx412_supply_names),
			       imx412->supplies);

	return ret;
}

/**
 * imx412_power_off() - Sensor power off sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx412 *imx412 = to_imx412(sd);

	clk_disable_unprepare(imx412->inclk);

	gpiod_set_value_cansleep(imx412->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(imx412_supply_names),
			       imx412->supplies);

	return 0;
}

/**
 * imx412_init_controls() - Initialize sensor subdevice controls
 * @imx412: pointer to imx412 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_init_controls(struct imx412 *imx412)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &imx412->ctrl_handler;
	const struct imx412_mode *mode = imx412->cur_mode;
	u32 lpfr;
	int ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 6);
	if (ret)
		return ret;

	/* Serialize controls with sensor device */
	ctrl_hdlr->lock = &imx412->mutex;

	/* Initialize exposure and gain */
	lpfr = mode->vblank + mode->height;
	imx412->exp_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					     &imx412_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX412_EXPOSURE_MIN,
					     lpfr - IMX412_EXPOSURE_OFFSET,
					     IMX412_EXPOSURE_STEP,
					     IMX412_EXPOSURE_DEFAULT);

	imx412->again_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					       &imx412_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       IMX412_AGAIN_MIN,
					       IMX412_AGAIN_MAX,
					       IMX412_AGAIN_STEP,
					       IMX412_AGAIN_DEFAULT);

	v4l2_ctrl_cluster(2, &imx412->exp_ctrl);

	imx412->vblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx412_ctrl_ops,
						V4L2_CID_VBLANK,
						mode->vblank_min,
						mode->vblank_max,
						1, mode->vblank);

	/* Read only controls */
	imx412->pclk_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					      &imx412_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      mode->pclk, mode->pclk,
					      1, mode->pclk);

	imx412->link_freq_ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&imx412_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							ARRAY_SIZE(link_freq) -
							1,
							mode->link_freq_idx,
							link_freq);
	if (imx412->link_freq_ctrl)
		imx412->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx412->hblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx412_ctrl_ops,
						V4L2_CID_HBLANK,
						IMX412_REG_MIN,
						IMX412_REG_MAX,
						1, mode->hblank);
	if (imx412->hblank_ctrl)
		imx412->hblank_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	if (ctrl_hdlr->error) {
		dev_err(imx412->dev, "control init failed: %d",
			ctrl_hdlr->error);
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ctrl_hdlr->error;
	}

	imx412->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

/**
 * imx412_probe() - I2C client device binding
 * @client: pointer to i2c client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx412_probe(struct i2c_client *client)
{
	struct imx412 *imx412;
	int ret;

	imx412 = devm_kzalloc(&client->dev, sizeof(*imx412), GFP_KERNEL);
	if (!imx412)
		return -ENOMEM;

	imx412->dev = &client->dev;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx412->sd, client, &imx412_subdev_ops);

	ret = imx412_parse_hw_config(imx412);
	if (ret) {
		dev_err(imx412->dev, "HW configuration is not supported");
		return ret;
	}

	mutex_init(&imx412->mutex);

	ret = imx412_power_on(imx412->dev);
	if (ret) {
		dev_err(imx412->dev, "failed to power-on the sensor");
		goto error_mutex_destroy;
	}

	/* Check module identity */
	ret = imx412_detect(imx412);
	if (ret) {
		dev_err(imx412->dev, "failed to find sensor: %d", ret);
		goto error_power_off;
	}

	/* Set default mode to max resolution */
	imx412->cur_mode = &supported_mode;
	imx412->vblank = imx412->cur_mode->vblank;

	ret = imx412_init_controls(imx412);
	if (ret) {
		dev_err(imx412->dev, "failed to init controls: %d", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	imx412->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx412->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx412->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx412->sd.entity, 1, &imx412->pad);
	if (ret) {
		dev_err(imx412->dev, "failed to init entity pads: %d", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx412->sd);
	if (ret < 0) {
		dev_err(imx412->dev,
			"failed to register async subdev: %d", ret);
		goto error_media_entity;
	}

	pm_runtime_set_active(imx412->dev);
	pm_runtime_enable(imx412->dev);
	pm_runtime_idle(imx412->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&imx412->sd.entity);
error_handler_free:
	v4l2_ctrl_handler_free(imx412->sd.ctrl_handler);
error_power_off:
	imx412_power_off(imx412->dev);
error_mutex_destroy:
	mutex_destroy(&imx412->mutex);

	return ret;
}

/**
 * imx412_remove() - I2C client device unbinding
 * @client: pointer to I2C client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static void imx412_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx412 *imx412 = to_imx412(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx412_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&imx412->mutex);
}

static const struct dev_pm_ops imx412_pm_ops = {
	SET_RUNTIME_PM_OPS(imx412_power_off, imx412_power_on, NULL)
};

static const struct of_device_id imx412_of_match[] = {
	{ .compatible = "sony,imx412" },
	{ }
};

MODULE_DEVICE_TABLE(of, imx412_of_match);

static struct i2c_driver imx412_driver = {
	.probe_new = imx412_probe,
	.remove = imx412_remove,
	.driver = {
		.name = "imx412",
		.pm = &imx412_pm_ops,
		.of_match_table = imx412_of_match,
	},
};

module_i2c_driver(imx412_driver);

MODULE_DESCRIPTION("Sony imx412 sensor driver");
MODULE_LICENSE("GPL");
