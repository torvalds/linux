// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony imx335 Camera Sensor Driver
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
#define IMX335_REG_MODE_SELECT	0x3000
#define IMX335_MODE_STANDBY	0x01
#define IMX335_MODE_STREAMING	0x00

/* Lines per frame */
#define IMX335_REG_LPFR		0x3030

/* Chip ID */
#define IMX335_REG_ID		0x3912
#define IMX335_ID		0x00

/* Exposure control */
#define IMX335_REG_SHUTTER	0x3058
#define IMX335_EXPOSURE_MIN	1
#define IMX335_EXPOSURE_OFFSET	9
#define IMX335_EXPOSURE_STEP	1
#define IMX335_EXPOSURE_DEFAULT	0x0648

/* Analog gain control */
#define IMX335_REG_AGAIN	0x30e8
#define IMX335_AGAIN_MIN	0
#define IMX335_AGAIN_MAX	240
#define IMX335_AGAIN_STEP	1
#define IMX335_AGAIN_DEFAULT	0

/* Group hold register */
#define IMX335_REG_HOLD		0x3001

/* Input clock rate */
#define IMX335_INCLK_RATE	24000000

/* CSI2 HW configuration */
#define IMX335_LINK_FREQ	594000000
#define IMX335_NUM_DATA_LANES	4

#define IMX335_REG_MIN		0x00
#define IMX335_REG_MAX		0xfffff

/**
 * struct imx335_reg - imx335 sensor register
 * @address: Register address
 * @val: Register value
 */
struct imx335_reg {
	u16 address;
	u8 val;
};

/**
 * struct imx335_reg_list - imx335 sensor register list
 * @num_of_regs: Number of registers in the list
 * @regs: Pointer to register list
 */
struct imx335_reg_list {
	u32 num_of_regs;
	const struct imx335_reg *regs;
};

/**
 * struct imx335_mode - imx335 sensor mode structure
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
struct imx335_mode {
	u32 width;
	u32 height;
	u32 code;
	u32 hblank;
	u32 vblank;
	u32 vblank_min;
	u32 vblank_max;
	u64 pclk;
	u32 link_freq_idx;
	struct imx335_reg_list reg_list;
};

/**
 * struct imx335 - imx335 sensor device structure
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
 */
struct imx335 {
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
	const struct imx335_mode *cur_mode;
	struct mutex mutex;
};

static const s64 link_freq[] = {
	IMX335_LINK_FREQ,
};

/* Sensor mode registers */
static const struct imx335_reg mode_2592x1940_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x00},
	{0x300c, 0x3b},
	{0x300d, 0x2a},
	{0x3018, 0x04},
	{0x302c, 0x3c},
	{0x302e, 0x20},
	{0x3056, 0x94},
	{0x3074, 0xc8},
	{0x3076, 0x28},
	{0x304c, 0x00},
	{0x314c, 0xc6},
	{0x315a, 0x02},
	{0x3168, 0xa0},
	{0x316a, 0x7e},
	{0x31a1, 0x00},
	{0x3288, 0x21},
	{0x328a, 0x02},
	{0x3414, 0x05},
	{0x3416, 0x18},
	{0x3648, 0x01},
	{0x364a, 0x04},
	{0x364c, 0x04},
	{0x3678, 0x01},
	{0x367c, 0x31},
	{0x367e, 0x31},
	{0x3706, 0x10},
	{0x3708, 0x03},
	{0x3714, 0x02},
	{0x3715, 0x02},
	{0x3716, 0x01},
	{0x3717, 0x03},
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
	{0x3740, 0x02},
	{0x375d, 0x00},
	{0x375e, 0x00},
	{0x375f, 0x11},
	{0x3760, 0x01},
	{0x3768, 0x1b},
	{0x3769, 0x1b},
	{0x376a, 0x1b},
	{0x376b, 0x1b},
	{0x376c, 0x1a},
	{0x376d, 0x17},
	{0x376e, 0x0f},
	{0x3776, 0x00},
	{0x3777, 0x00},
	{0x3778, 0x46},
	{0x3779, 0x00},
	{0x377a, 0x89},
	{0x377b, 0x00},
	{0x377c, 0x08},
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
	{0x37b0, 0x36},
	{0x3a00, 0x01},
};

/* Supported sensor mode configurations */
static const struct imx335_mode supported_mode = {
	.width = 2592,
	.height = 1940,
	.hblank = 342,
	.vblank = 2560,
	.vblank_min = 2560,
	.vblank_max = 133060,
	.pclk = 396000000,
	.link_freq_idx = 0,
	.code = MEDIA_BUS_FMT_SRGGB12_1X12,
	.reg_list = {
		.num_of_regs = ARRAY_SIZE(mode_2592x1940_regs),
		.regs = mode_2592x1940_regs,
	},
};

/**
 * to_imx335() - imx335 V4L2 sub-device to imx335 device.
 * @subdev: pointer to imx335 V4L2 sub-device
 *
 * Return: pointer to imx335 device
 */
static inline struct imx335 *to_imx335(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct imx335, sd);
}

/**
 * imx335_read_reg() - Read registers.
 * @imx335: pointer to imx335 device
 * @reg: register address
 * @len: length of bytes to read. Max supported bytes is 4
 * @val: pointer to register value to be filled.
 *
 * Big endian register addresses with little endian values.
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_read_reg(struct imx335 *imx335, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx335->sd);
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
 * imx335_write_reg() - Write register
 * @imx335: pointer to imx335 device
 * @reg: register address
 * @len: length of bytes. Max supported bytes is 4
 * @val: register value
 *
 * Big endian register addresses with little endian values.
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_write_reg(struct imx335 *imx335, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx335->sd);
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
 * imx335_write_regs() - Write a list of registers
 * @imx335: pointer to imx335 device
 * @regs: list of registers to be written
 * @len: length of registers array
 *
 * Return: 0 if successful. error code otherwise.
 */
static int imx335_write_regs(struct imx335 *imx335,
			     const struct imx335_reg *regs, u32 len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx335_write_reg(imx335, regs[i].address, 1, regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * imx335_update_controls() - Update control ranges based on streaming mode
 * @imx335: pointer to imx335 device
 * @mode: pointer to imx335_mode sensor mode
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_update_controls(struct imx335 *imx335,
				  const struct imx335_mode *mode)
{
	int ret;

	ret = __v4l2_ctrl_s_ctrl(imx335->link_freq_ctrl, mode->link_freq_idx);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_s_ctrl(imx335->hblank_ctrl, mode->hblank);
	if (ret)
		return ret;

	return __v4l2_ctrl_modify_range(imx335->vblank_ctrl, mode->vblank_min,
					mode->vblank_max, 1, mode->vblank);
}

/**
 * imx335_update_exp_gain() - Set updated exposure and gain
 * @imx335: pointer to imx335 device
 * @exposure: updated exposure value
 * @gain: updated analog gain value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_update_exp_gain(struct imx335 *imx335, u32 exposure, u32 gain)
{
	u32 lpfr, shutter;
	int ret;

	lpfr = imx335->vblank + imx335->cur_mode->height;
	shutter = lpfr - exposure;

	dev_dbg(imx335->dev, "Set exp %u, analog gain %u, shutter %u, lpfr %u",
		exposure, gain, shutter, lpfr);

	ret = imx335_write_reg(imx335, IMX335_REG_HOLD, 1, 1);
	if (ret)
		return ret;

	ret = imx335_write_reg(imx335, IMX335_REG_LPFR, 3, lpfr);
	if (ret)
		goto error_release_group_hold;

	ret = imx335_write_reg(imx335, IMX335_REG_SHUTTER, 3, shutter);
	if (ret)
		goto error_release_group_hold;

	ret = imx335_write_reg(imx335, IMX335_REG_AGAIN, 2, gain);

error_release_group_hold:
	imx335_write_reg(imx335, IMX335_REG_HOLD, 1, 0);

	return ret;
}

/**
 * imx335_set_ctrl() - Set subdevice control
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
static int imx335_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx335 *imx335 =
		container_of(ctrl->handler, struct imx335, ctrl_handler);
	u32 analog_gain;
	u32 exposure;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		imx335->vblank = imx335->vblank_ctrl->val;

		dev_dbg(imx335->dev, "Received vblank %u, new lpfr %u",
			imx335->vblank,
			imx335->vblank + imx335->cur_mode->height);

		ret = __v4l2_ctrl_modify_range(imx335->exp_ctrl,
					       IMX335_EXPOSURE_MIN,
					       imx335->vblank +
					       imx335->cur_mode->height -
					       IMX335_EXPOSURE_OFFSET,
					       1, IMX335_EXPOSURE_DEFAULT);
		break;
	case V4L2_CID_EXPOSURE:
		/* Set controls only if sensor is in power on state */
		if (!pm_runtime_get_if_in_use(imx335->dev))
			return 0;

		exposure = ctrl->val;
		analog_gain = imx335->again_ctrl->val;

		dev_dbg(imx335->dev, "Received exp %u, analog gain %u",
			exposure, analog_gain);

		ret = imx335_update_exp_gain(imx335, exposure, analog_gain);

		pm_runtime_put(imx335->dev);

		break;
	default:
		dev_err(imx335->dev, "Invalid control %d", ctrl->id);
		ret = -EINVAL;
	}

	return ret;
}

/* V4l2 subdevice control ops*/
static const struct v4l2_ctrl_ops imx335_ctrl_ops = {
	.s_ctrl = imx335_set_ctrl,
};

/**
 * imx335_enum_mbus_code() - Enumerate V4L2 sub-device mbus codes
 * @sd: pointer to imx335 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @code: V4L2 sub-device code enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = supported_mode.code;

	return 0;
}

/**
 * imx335_enum_frame_size() - Enumerate V4L2 sub-device frame sizes
 * @sd: pointer to imx335 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fsize: V4L2 sub-device size enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_enum_frame_size(struct v4l2_subdev *sd,
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
 * imx335_fill_pad_format() - Fill subdevice pad format
 *                            from selected sensor mode
 * @imx335: pointer to imx335 device
 * @mode: pointer to imx335_mode sensor mode
 * @fmt: V4L2 sub-device format need to be filled
 */
static void imx335_fill_pad_format(struct imx335 *imx335,
				   const struct imx335_mode *mode,
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
 * imx335_get_pad_format() - Get subdevice pad format
 * @sd: pointer to imx335 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx335 *imx335 = to_imx335(sd);

	mutex_lock(&imx335->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		imx335_fill_pad_format(imx335, imx335->cur_mode, fmt);
	}

	mutex_unlock(&imx335->mutex);

	return 0;
}

/**
 * imx335_set_pad_format() - Set subdevice pad format
 * @sd: pointer to imx335 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx335 *imx335 = to_imx335(sd);
	const struct imx335_mode *mode;
	int ret = 0;

	mutex_lock(&imx335->mutex);

	mode = &supported_mode;
	imx335_fill_pad_format(imx335, mode, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ret = imx335_update_controls(imx335, mode);
		if (!ret)
			imx335->cur_mode = mode;
	}

	mutex_unlock(&imx335->mutex);

	return ret;
}

/**
 * imx335_init_pad_cfg() - Initialize sub-device pad configuration
 * @sd: pointer to imx335 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_init_pad_cfg(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state)
{
	struct imx335 *imx335 = to_imx335(sd);
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	imx335_fill_pad_format(imx335, &supported_mode, &fmt);

	return imx335_set_pad_format(sd, sd_state, &fmt);
}

/**
 * imx335_start_streaming() - Start sensor stream
 * @imx335: pointer to imx335 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_start_streaming(struct imx335 *imx335)
{
	const struct imx335_reg_list *reg_list;
	int ret;

	/* Write sensor mode registers */
	reg_list = &imx335->cur_mode->reg_list;
	ret = imx335_write_regs(imx335, reg_list->regs,
				reg_list->num_of_regs);
	if (ret) {
		dev_err(imx335->dev, "fail to write initial registers");
		return ret;
	}

	/* Setup handler will write actual exposure and gain */
	ret =  __v4l2_ctrl_handler_setup(imx335->sd.ctrl_handler);
	if (ret) {
		dev_err(imx335->dev, "fail to setup handler");
		return ret;
	}

	/* Start streaming */
	ret = imx335_write_reg(imx335, IMX335_REG_MODE_SELECT,
			       1, IMX335_MODE_STREAMING);
	if (ret) {
		dev_err(imx335->dev, "fail to start streaming");
		return ret;
	}

	/* Initial regulator stabilization period */
	usleep_range(18000, 20000);

	return 0;
}

/**
 * imx335_stop_streaming() - Stop sensor stream
 * @imx335: pointer to imx335 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_stop_streaming(struct imx335 *imx335)
{
	return imx335_write_reg(imx335, IMX335_REG_MODE_SELECT,
				1, IMX335_MODE_STANDBY);
}

/**
 * imx335_set_stream() - Enable sensor streaming
 * @sd: pointer to imx335 subdevice
 * @enable: set to enable sensor streaming
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx335 *imx335 = to_imx335(sd);
	int ret;

	mutex_lock(&imx335->mutex);

	if (enable) {
		ret = pm_runtime_resume_and_get(imx335->dev);
		if (ret)
			goto error_unlock;

		ret = imx335_start_streaming(imx335);
		if (ret)
			goto error_power_off;
	} else {
		imx335_stop_streaming(imx335);
		pm_runtime_put(imx335->dev);
	}

	mutex_unlock(&imx335->mutex);

	return 0;

error_power_off:
	pm_runtime_put(imx335->dev);
error_unlock:
	mutex_unlock(&imx335->mutex);

	return ret;
}

/**
 * imx335_detect() - Detect imx335 sensor
 * @imx335: pointer to imx335 device
 *
 * Return: 0 if successful, -EIO if sensor id does not match
 */
static int imx335_detect(struct imx335 *imx335)
{
	int ret;
	u32 val;

	ret = imx335_read_reg(imx335, IMX335_REG_ID, 2, &val);
	if (ret)
		return ret;

	if (val != IMX335_ID) {
		dev_err(imx335->dev, "chip id mismatch: %x!=%x",
			IMX335_ID, val);
		return -ENXIO;
	}

	return 0;
}

/**
 * imx335_parse_hw_config() - Parse HW configuration and check if supported
 * @imx335: pointer to imx335 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_parse_hw_config(struct imx335 *imx335)
{
	struct fwnode_handle *fwnode = dev_fwnode(imx335->dev);
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
	imx335->reset_gpio = devm_gpiod_get_optional(imx335->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx335->reset_gpio)) {
		dev_err(imx335->dev, "failed to get reset gpio %ld",
			PTR_ERR(imx335->reset_gpio));
		return PTR_ERR(imx335->reset_gpio);
	}

	/* Get sensor input clock */
	imx335->inclk = devm_clk_get(imx335->dev, NULL);
	if (IS_ERR(imx335->inclk)) {
		dev_err(imx335->dev, "could not get inclk");
		return PTR_ERR(imx335->inclk);
	}

	rate = clk_get_rate(imx335->inclk);
	if (rate != IMX335_INCLK_RATE) {
		dev_err(imx335->dev, "inclk frequency mismatch");
		return -EINVAL;
	}

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != IMX335_NUM_DATA_LANES) {
		dev_err(imx335->dev,
			"number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(imx335->dev, "no link frequencies defined");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++)
		if (bus_cfg.link_frequencies[i] == IMX335_LINK_FREQ)
			goto done_endpoint_free;

	ret = -EINVAL;

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

/* V4l2 subdevice ops */
static const struct v4l2_subdev_video_ops imx335_video_ops = {
	.s_stream = imx335_set_stream,
};

static const struct v4l2_subdev_pad_ops imx335_pad_ops = {
	.init_cfg = imx335_init_pad_cfg,
	.enum_mbus_code = imx335_enum_mbus_code,
	.enum_frame_size = imx335_enum_frame_size,
	.get_fmt = imx335_get_pad_format,
	.set_fmt = imx335_set_pad_format,
};

static const struct v4l2_subdev_ops imx335_subdev_ops = {
	.video = &imx335_video_ops,
	.pad = &imx335_pad_ops,
};

/**
 * imx335_power_on() - Sensor power on sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx335 *imx335 = to_imx335(sd);
	int ret;

	gpiod_set_value_cansleep(imx335->reset_gpio, 1);

	ret = clk_prepare_enable(imx335->inclk);
	if (ret) {
		dev_err(imx335->dev, "fail to enable inclk");
		goto error_reset;
	}

	usleep_range(20, 22);

	return 0;

error_reset:
	gpiod_set_value_cansleep(imx335->reset_gpio, 0);

	return ret;
}

/**
 * imx335_power_off() - Sensor power off sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx335 *imx335 = to_imx335(sd);

	gpiod_set_value_cansleep(imx335->reset_gpio, 0);

	clk_disable_unprepare(imx335->inclk);

	return 0;
}

/**
 * imx335_init_controls() - Initialize sensor subdevice controls
 * @imx335: pointer to imx335 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_init_controls(struct imx335 *imx335)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &imx335->ctrl_handler;
	const struct imx335_mode *mode = imx335->cur_mode;
	u32 lpfr;
	int ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 6);
	if (ret)
		return ret;

	/* Serialize controls with sensor device */
	ctrl_hdlr->lock = &imx335->mutex;

	/* Initialize exposure and gain */
	lpfr = mode->vblank + mode->height;
	imx335->exp_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					     &imx335_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX335_EXPOSURE_MIN,
					     lpfr - IMX335_EXPOSURE_OFFSET,
					     IMX335_EXPOSURE_STEP,
					     IMX335_EXPOSURE_DEFAULT);

	imx335->again_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					       &imx335_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       IMX335_AGAIN_MIN,
					       IMX335_AGAIN_MAX,
					       IMX335_AGAIN_STEP,
					       IMX335_AGAIN_DEFAULT);

	v4l2_ctrl_cluster(2, &imx335->exp_ctrl);

	imx335->vblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx335_ctrl_ops,
						V4L2_CID_VBLANK,
						mode->vblank_min,
						mode->vblank_max,
						1, mode->vblank);

	/* Read only controls */
	imx335->pclk_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					      &imx335_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      mode->pclk, mode->pclk,
					      1, mode->pclk);

	imx335->link_freq_ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&imx335_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							ARRAY_SIZE(link_freq) -
							1,
							mode->link_freq_idx,
							link_freq);
	if (imx335->link_freq_ctrl)
		imx335->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx335->hblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx335_ctrl_ops,
						V4L2_CID_HBLANK,
						IMX335_REG_MIN,
						IMX335_REG_MAX,
						1, mode->hblank);
	if (imx335->hblank_ctrl)
		imx335->hblank_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	if (ctrl_hdlr->error) {
		dev_err(imx335->dev, "control init failed: %d",
			ctrl_hdlr->error);
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ctrl_hdlr->error;
	}

	imx335->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

/**
 * imx335_probe() - I2C client device binding
 * @client: pointer to i2c client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx335_probe(struct i2c_client *client)
{
	struct imx335 *imx335;
	int ret;

	imx335 = devm_kzalloc(&client->dev, sizeof(*imx335), GFP_KERNEL);
	if (!imx335)
		return -ENOMEM;

	imx335->dev = &client->dev;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx335->sd, client, &imx335_subdev_ops);

	ret = imx335_parse_hw_config(imx335);
	if (ret) {
		dev_err(imx335->dev, "HW configuration is not supported");
		return ret;
	}

	mutex_init(&imx335->mutex);

	ret = imx335_power_on(imx335->dev);
	if (ret) {
		dev_err(imx335->dev, "failed to power-on the sensor");
		goto error_mutex_destroy;
	}

	/* Check module identity */
	ret = imx335_detect(imx335);
	if (ret) {
		dev_err(imx335->dev, "failed to find sensor: %d", ret);
		goto error_power_off;
	}

	/* Set default mode to max resolution */
	imx335->cur_mode = &supported_mode;
	imx335->vblank = imx335->cur_mode->vblank;

	ret = imx335_init_controls(imx335);
	if (ret) {
		dev_err(imx335->dev, "failed to init controls: %d", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	imx335->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx335->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx335->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx335->sd.entity, 1, &imx335->pad);
	if (ret) {
		dev_err(imx335->dev, "failed to init entity pads: %d", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx335->sd);
	if (ret < 0) {
		dev_err(imx335->dev,
			"failed to register async subdev: %d", ret);
		goto error_media_entity;
	}

	pm_runtime_set_active(imx335->dev);
	pm_runtime_enable(imx335->dev);
	pm_runtime_idle(imx335->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&imx335->sd.entity);
error_handler_free:
	v4l2_ctrl_handler_free(imx335->sd.ctrl_handler);
error_power_off:
	imx335_power_off(imx335->dev);
error_mutex_destroy:
	mutex_destroy(&imx335->mutex);

	return ret;
}

/**
 * imx335_remove() - I2C client device unbinding
 * @client: pointer to I2C client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static void imx335_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx335 *imx335 = to_imx335(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx335_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&imx335->mutex);
}

static const struct dev_pm_ops imx335_pm_ops = {
	SET_RUNTIME_PM_OPS(imx335_power_off, imx335_power_on, NULL)
};

static const struct of_device_id imx335_of_match[] = {
	{ .compatible = "sony,imx335" },
	{ }
};

MODULE_DEVICE_TABLE(of, imx335_of_match);

static struct i2c_driver imx335_driver = {
	.probe = imx335_probe,
	.remove = imx335_remove,
	.driver = {
		.name = "imx335",
		.pm = &imx335_pm_ops,
		.of_match_table = imx335_of_match,
	},
};

module_i2c_driver(imx335_driver);

MODULE_DESCRIPTION("Sony imx335 sensor driver");
MODULE_LICENSE("GPL");
