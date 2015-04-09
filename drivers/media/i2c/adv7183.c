/*
 * adv7183.c Analog Devices ADV7183 video decoder driver
 *
 * Copyright (c) 2011 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/adv7183.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#include "adv7183_regs.h"

struct adv7183 {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;

	v4l2_std_id std; /* Current set standard */
	u32 input;
	u32 output;
	unsigned reset_pin;
	unsigned oe_pin;
	struct v4l2_mbus_framefmt fmt;
};

/* EXAMPLES USING 27 MHz CLOCK
 * Mode 1 CVBS Input (Composite Video on AIN5)
 * All standards are supported through autodetect, 8-bit, 4:2:2, ITU-R BT.656 output on P15 to P8.
 */
static const unsigned char adv7183_init_regs[] = {
	ADV7183_IN_CTRL, 0x04,           /* CVBS input on AIN5 */
	ADV7183_DIGI_CLAMP_CTRL_1, 0x00, /* Slow down digital clamps */
	ADV7183_SHAP_FILT_CTRL, 0x41,    /* Set CSFM to SH1 */
	ADV7183_ADC_CTRL, 0x16,          /* Power down ADC 1 and ADC 2 */
	ADV7183_CTI_DNR_CTRL_4, 0x04,    /* Set DNR threshold to 4 for flat response */
	/* ADI recommended programming sequence */
	ADV7183_ADI_CTRL, 0x80,
	ADV7183_CTI_DNR_CTRL_4, 0x20,
	0x52, 0x18,
	0x58, 0xED,
	0x77, 0xC5,
	0x7C, 0x93,
	0x7D, 0x00,
	0xD0, 0x48,
	0xD5, 0xA0,
	0xD7, 0xEA,
	ADV7183_SD_SATURATION_CR, 0x3E,
	ADV7183_PAL_V_END, 0x3E,
	ADV7183_PAL_F_TOGGLE, 0x0F,
	ADV7183_ADI_CTRL, 0x00,
};

static inline struct adv7183 *to_adv7183(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7183, sd);
}
static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct adv7183, hdl)->sd;
}

static inline int adv7183_read(struct v4l2_subdev *sd, unsigned char reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte_data(client, reg);
}

static inline int adv7183_write(struct v4l2_subdev *sd, unsigned char reg,
				unsigned char value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_write_byte_data(client, reg, value);
}

static int adv7183_writeregs(struct v4l2_subdev *sd,
		const unsigned char *regs, unsigned int num)
{
	unsigned char reg, data;
	unsigned int cnt = 0;

	if (num & 0x1) {
		v4l2_err(sd, "invalid regs array\n");
		return -1;
	}

	while (cnt < num) {
		reg = *regs++;
		data = *regs++;
		cnt += 2;

		adv7183_write(sd, reg, data);
	}
	return 0;
}

static int adv7183_log_status(struct v4l2_subdev *sd)
{
	struct adv7183 *decoder = to_adv7183(sd);

	v4l2_info(sd, "adv7183: Input control = 0x%02x\n",
			adv7183_read(sd, ADV7183_IN_CTRL));
	v4l2_info(sd, "adv7183: Video selection = 0x%02x\n",
			adv7183_read(sd, ADV7183_VD_SEL));
	v4l2_info(sd, "adv7183: Output control = 0x%02x\n",
			adv7183_read(sd, ADV7183_OUT_CTRL));
	v4l2_info(sd, "adv7183: Extended output control = 0x%02x\n",
			adv7183_read(sd, ADV7183_EXT_OUT_CTRL));
	v4l2_info(sd, "adv7183: Autodetect enable = 0x%02x\n",
			adv7183_read(sd, ADV7183_AUTO_DET_EN));
	v4l2_info(sd, "adv7183: Contrast = 0x%02x\n",
			adv7183_read(sd, ADV7183_CONTRAST));
	v4l2_info(sd, "adv7183: Brightness = 0x%02x\n",
			adv7183_read(sd, ADV7183_BRIGHTNESS));
	v4l2_info(sd, "adv7183: Hue = 0x%02x\n",
			adv7183_read(sd, ADV7183_HUE));
	v4l2_info(sd, "adv7183: Default value Y = 0x%02x\n",
			adv7183_read(sd, ADV7183_DEF_Y));
	v4l2_info(sd, "adv7183: Default value C = 0x%02x\n",
			adv7183_read(sd, ADV7183_DEF_C));
	v4l2_info(sd, "adv7183: ADI control = 0x%02x\n",
			adv7183_read(sd, ADV7183_ADI_CTRL));
	v4l2_info(sd, "adv7183: Power Management = 0x%02x\n",
			adv7183_read(sd, ADV7183_POW_MANAGE));
	v4l2_info(sd, "adv7183: Status 1 2 and 3 = 0x%02x 0x%02x 0x%02x\n",
			adv7183_read(sd, ADV7183_STATUS_1),
			adv7183_read(sd, ADV7183_STATUS_2),
			adv7183_read(sd, ADV7183_STATUS_3));
	v4l2_info(sd, "adv7183: Ident = 0x%02x\n",
			adv7183_read(sd, ADV7183_IDENT));
	v4l2_info(sd, "adv7183: Analog clamp control = 0x%02x\n",
			adv7183_read(sd, ADV7183_ANAL_CLAMP_CTRL));
	v4l2_info(sd, "adv7183: Digital clamp control 1 = 0x%02x\n",
			adv7183_read(sd, ADV7183_DIGI_CLAMP_CTRL_1));
	v4l2_info(sd, "adv7183: Shaping filter control 1 and 2 = 0x%02x 0x%02x\n",
			adv7183_read(sd, ADV7183_SHAP_FILT_CTRL),
			adv7183_read(sd, ADV7183_SHAP_FILT_CTRL_2));
	v4l2_info(sd, "adv7183: Comb filter control = 0x%02x\n",
			adv7183_read(sd, ADV7183_COMB_FILT_CTRL));
	v4l2_info(sd, "adv7183: ADI control 2 = 0x%02x\n",
			adv7183_read(sd, ADV7183_ADI_CTRL_2));
	v4l2_info(sd, "adv7183: Pixel delay control = 0x%02x\n",
			adv7183_read(sd, ADV7183_PIX_DELAY_CTRL));
	v4l2_info(sd, "adv7183: Misc gain control = 0x%02x\n",
			adv7183_read(sd, ADV7183_MISC_GAIN_CTRL));
	v4l2_info(sd, "adv7183: AGC mode control = 0x%02x\n",
			adv7183_read(sd, ADV7183_AGC_MODE_CTRL));
	v4l2_info(sd, "adv7183: Chroma gain control 1 and 2 = 0x%02x 0x%02x\n",
			adv7183_read(sd, ADV7183_CHRO_GAIN_CTRL_1),
			adv7183_read(sd, ADV7183_CHRO_GAIN_CTRL_2));
	v4l2_info(sd, "adv7183: Luma gain control 1 and 2 = 0x%02x 0x%02x\n",
			adv7183_read(sd, ADV7183_LUMA_GAIN_CTRL_1),
			adv7183_read(sd, ADV7183_LUMA_GAIN_CTRL_2));
	v4l2_info(sd, "adv7183: Vsync field control 1 2 and 3 = 0x%02x 0x%02x 0x%02x\n",
			adv7183_read(sd, ADV7183_VS_FIELD_CTRL_1),
			adv7183_read(sd, ADV7183_VS_FIELD_CTRL_2),
			adv7183_read(sd, ADV7183_VS_FIELD_CTRL_3));
	v4l2_info(sd, "adv7183: Hsync position control 1 2 and 3 = 0x%02x 0x%02x 0x%02x\n",
			adv7183_read(sd, ADV7183_HS_POS_CTRL_1),
			adv7183_read(sd, ADV7183_HS_POS_CTRL_2),
			adv7183_read(sd, ADV7183_HS_POS_CTRL_3));
	v4l2_info(sd, "adv7183: Polarity = 0x%02x\n",
			adv7183_read(sd, ADV7183_POLARITY));
	v4l2_info(sd, "adv7183: ADC control = 0x%02x\n",
			adv7183_read(sd, ADV7183_ADC_CTRL));
	v4l2_info(sd, "adv7183: SD offset Cb and Cr = 0x%02x 0x%02x\n",
			adv7183_read(sd, ADV7183_SD_OFFSET_CB),
			adv7183_read(sd, ADV7183_SD_OFFSET_CR));
	v4l2_info(sd, "adv7183: SD saturation Cb and Cr = 0x%02x 0x%02x\n",
			adv7183_read(sd, ADV7183_SD_SATURATION_CB),
			adv7183_read(sd, ADV7183_SD_SATURATION_CR));
	v4l2_info(sd, "adv7183: Drive strength = 0x%02x\n",
			adv7183_read(sd, ADV7183_DRIVE_STR));
	v4l2_ctrl_handler_log_status(&decoder->hdl, sd->name);
	return 0;
}

static int adv7183_g_std(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct adv7183 *decoder = to_adv7183(sd);

	*std = decoder->std;
	return 0;
}

static int adv7183_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7183 *decoder = to_adv7183(sd);
	int reg;

	reg = adv7183_read(sd, ADV7183_IN_CTRL) & 0xF;
	if (std == V4L2_STD_PAL_60)
		reg |= 0x60;
	else if (std == V4L2_STD_NTSC_443)
		reg |= 0x70;
	else if (std == V4L2_STD_PAL_N)
		reg |= 0x90;
	else if (std == V4L2_STD_PAL_M)
		reg |= 0xA0;
	else if (std == V4L2_STD_PAL_Nc)
		reg |= 0xC0;
	else if (std & V4L2_STD_PAL)
		reg |= 0x80;
	else if (std & V4L2_STD_NTSC)
		reg |= 0x50;
	else if (std & V4L2_STD_SECAM)
		reg |= 0xE0;
	else
		return -EINVAL;
	adv7183_write(sd, ADV7183_IN_CTRL, reg);

	decoder->std = std;

	return 0;
}

static int adv7183_reset(struct v4l2_subdev *sd, u32 val)
{
	int reg;

	reg = adv7183_read(sd, ADV7183_POW_MANAGE) | 0x80;
	adv7183_write(sd, ADV7183_POW_MANAGE, reg);
	/* wait 5ms before any further i2c writes are performed */
	usleep_range(5000, 10000);
	return 0;
}

static int adv7183_s_routing(struct v4l2_subdev *sd,
				u32 input, u32 output, u32 config)
{
	struct adv7183 *decoder = to_adv7183(sd);
	int reg;

	if ((input > ADV7183_COMPONENT1) || (output > ADV7183_16BIT_OUT))
		return -EINVAL;

	if (input != decoder->input) {
		decoder->input = input;
		reg = adv7183_read(sd, ADV7183_IN_CTRL) & 0xF0;
		switch (input) {
		case ADV7183_COMPOSITE1:
			reg |= 0x1;
			break;
		case ADV7183_COMPOSITE2:
			reg |= 0x2;
			break;
		case ADV7183_COMPOSITE3:
			reg |= 0x3;
			break;
		case ADV7183_COMPOSITE4:
			reg |= 0x4;
			break;
		case ADV7183_COMPOSITE5:
			reg |= 0x5;
			break;
		case ADV7183_COMPOSITE6:
			reg |= 0xB;
			break;
		case ADV7183_COMPOSITE7:
			reg |= 0xC;
			break;
		case ADV7183_COMPOSITE8:
			reg |= 0xD;
			break;
		case ADV7183_COMPOSITE9:
			reg |= 0xE;
			break;
		case ADV7183_COMPOSITE10:
			reg |= 0xF;
			break;
		case ADV7183_SVIDEO0:
			reg |= 0x6;
			break;
		case ADV7183_SVIDEO1:
			reg |= 0x7;
			break;
		case ADV7183_SVIDEO2:
			reg |= 0x8;
			break;
		case ADV7183_COMPONENT0:
			reg |= 0x9;
			break;
		case ADV7183_COMPONENT1:
			reg |= 0xA;
			break;
		default:
			break;
		}
		adv7183_write(sd, ADV7183_IN_CTRL, reg);
	}

	if (output != decoder->output) {
		decoder->output = output;
		reg = adv7183_read(sd, ADV7183_OUT_CTRL) & 0xC0;
		switch (output) {
		case ADV7183_16BIT_OUT:
			reg |= 0x9;
			break;
		default:
			reg |= 0xC;
			break;
		}
		adv7183_write(sd, ADV7183_OUT_CTRL, reg);
	}

	return 0;
}

static int adv7183_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	int val = ctrl->val;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (val < 0)
			val = 127 - val;
		adv7183_write(sd, ADV7183_BRIGHTNESS, val);
		break;
	case V4L2_CID_CONTRAST:
		adv7183_write(sd, ADV7183_CONTRAST, val);
		break;
	case V4L2_CID_SATURATION:
		adv7183_write(sd, ADV7183_SD_SATURATION_CB, val >> 8);
		adv7183_write(sd, ADV7183_SD_SATURATION_CR, (val & 0xFF));
		break;
	case V4L2_CID_HUE:
		adv7183_write(sd, ADV7183_SD_OFFSET_CB, val >> 8);
		adv7183_write(sd, ADV7183_SD_OFFSET_CR, (val & 0xFF));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int adv7183_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct adv7183 *decoder = to_adv7183(sd);
	int reg;

	/* enable autodetection block */
	reg = adv7183_read(sd, ADV7183_IN_CTRL) & 0xF;
	adv7183_write(sd, ADV7183_IN_CTRL, reg);

	/* wait autodetection switch */
	mdelay(10);

	/* get autodetection result */
	reg = adv7183_read(sd, ADV7183_STATUS_1);
	switch ((reg >> 0x4) & 0x7) {
	case 0:
		*std &= V4L2_STD_NTSC;
		break;
	case 1:
		*std &= V4L2_STD_NTSC_443;
		break;
	case 2:
		*std &= V4L2_STD_PAL_M;
		break;
	case 3:
		*std &= V4L2_STD_PAL_60;
		break;
	case 4:
		*std &= V4L2_STD_PAL;
		break;
	case 5:
		*std &= V4L2_STD_SECAM;
		break;
	case 6:
		*std &= V4L2_STD_PAL_Nc;
		break;
	case 7:
		*std &= V4L2_STD_SECAM;
		break;
	default:
		*std = V4L2_STD_UNKNOWN;
		break;
	}

	/* after std detection, write back user set std */
	adv7183_s_std(sd, decoder->std);
	return 0;
}

static int adv7183_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	int reg;

	*status = V4L2_IN_ST_NO_SIGNAL;
	reg = adv7183_read(sd, ADV7183_STATUS_1);
	if (reg < 0)
		return reg;
	if (reg & 0x1)
		*status = 0;
	return 0;
}

static int adv7183_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_UYVY8_2X8;
	return 0;
}

static int adv7183_try_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	struct adv7183 *decoder = to_adv7183(sd);

	fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	if (decoder->std & V4L2_STD_525_60) {
		fmt->field = V4L2_FIELD_SEQ_TB;
		fmt->width = 720;
		fmt->height = 480;
	} else {
		fmt->field = V4L2_FIELD_SEQ_BT;
		fmt->width = 720;
		fmt->height = 576;
	}
	return 0;
}

static int adv7183_s_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	struct adv7183 *decoder = to_adv7183(sd);

	adv7183_try_mbus_fmt(sd, fmt);
	decoder->fmt = *fmt;
	return 0;
}

static int adv7183_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct adv7183 *decoder = to_adv7183(sd);

	if (format->pad)
		return -EINVAL;

	format->format = decoder->fmt;
	return 0;
}

static int adv7183_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct adv7183 *decoder = to_adv7183(sd);

	if (enable)
		gpio_set_value(decoder->oe_pin, 0);
	else
		gpio_set_value(decoder->oe_pin, 1);
	udelay(1);
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int adv7183_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	reg->val = adv7183_read(sd, reg->reg & 0xff);
	reg->size = 1;
	return 0;
}

static int adv7183_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	adv7183_write(sd, reg->reg & 0xff, reg->val & 0xff);
	return 0;
}
#endif

static const struct v4l2_ctrl_ops adv7183_ctrl_ops = {
	.s_ctrl = adv7183_s_ctrl,
};

static const struct v4l2_subdev_core_ops adv7183_core_ops = {
	.log_status = adv7183_log_status,
	.reset = adv7183_reset,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = adv7183_g_register,
	.s_register = adv7183_s_register,
#endif
};

static const struct v4l2_subdev_video_ops adv7183_video_ops = {
	.g_std = adv7183_g_std,
	.s_std = adv7183_s_std,
	.s_routing = adv7183_s_routing,
	.querystd = adv7183_querystd,
	.g_input_status = adv7183_g_input_status,
	.try_mbus_fmt = adv7183_try_mbus_fmt,
	.s_mbus_fmt = adv7183_s_mbus_fmt,
	.s_stream = adv7183_s_stream,
};

static const struct v4l2_subdev_pad_ops adv7183_pad_ops = {
	.enum_mbus_code = adv7183_enum_mbus_code,
	.get_fmt = adv7183_get_fmt,
};

static const struct v4l2_subdev_ops adv7183_ops = {
	.core = &adv7183_core_ops,
	.video = &adv7183_video_ops,
	.pad = &adv7183_pad_ops,
};

static int adv7183_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct adv7183 *decoder;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *hdl;
	int ret;
	struct v4l2_mbus_framefmt fmt;
	const unsigned *pin_array;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	pin_array = client->dev.platform_data;
	if (pin_array == NULL)
		return -EINVAL;

	decoder = devm_kzalloc(&client->dev, sizeof(*decoder), GFP_KERNEL);
	if (decoder == NULL)
		return -ENOMEM;

	decoder->reset_pin = pin_array[0];
	decoder->oe_pin = pin_array[1];

	if (devm_gpio_request_one(&client->dev, decoder->reset_pin,
				  GPIOF_OUT_INIT_LOW, "ADV7183 Reset")) {
		v4l_err(client, "failed to request GPIO %d\n", decoder->reset_pin);
		return -EBUSY;
	}

	if (devm_gpio_request_one(&client->dev, decoder->oe_pin,
				  GPIOF_OUT_INIT_HIGH,
				  "ADV7183 Output Enable")) {
		v4l_err(client, "failed to request GPIO %d\n", decoder->oe_pin);
		return -EBUSY;
	}

	sd = &decoder->sd;
	v4l2_i2c_subdev_init(sd, client, &adv7183_ops);

	hdl = &decoder->hdl;
	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &adv7183_ctrl_ops,
			V4L2_CID_BRIGHTNESS, -128, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, &adv7183_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 0xFF, 1, 0x80);
	v4l2_ctrl_new_std(hdl, &adv7183_ctrl_ops,
			V4L2_CID_SATURATION, 0, 0xFFFF, 1, 0x8080);
	v4l2_ctrl_new_std(hdl, &adv7183_ctrl_ops,
			V4L2_CID_HUE, 0, 0xFFFF, 1, 0x8080);
	/* hook the control handler into the driver */
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		ret = hdl->error;

		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	/* v4l2 doesn't support an autodetect standard, pick PAL as default */
	decoder->std = V4L2_STD_PAL;
	decoder->input = ADV7183_COMPOSITE4;
	decoder->output = ADV7183_8BIT_OUT;

	/* reset chip */
	/* reset pulse width at least 5ms */
	mdelay(10);
	gpio_set_value(decoder->reset_pin, 1);
	/* wait 5ms before any further i2c writes are performed */
	mdelay(5);

	adv7183_writeregs(sd, adv7183_init_regs, ARRAY_SIZE(adv7183_init_regs));
	adv7183_s_std(sd, decoder->std);
	fmt.width = 720;
	fmt.height = 576;
	adv7183_s_mbus_fmt(sd, &fmt);

	/* initialize the hardware to the default control values */
	ret = v4l2_ctrl_handler_setup(hdl);
	if (ret) {
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	return 0;
}

static int adv7183_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	return 0;
}

static const struct i2c_device_id adv7183_id[] = {
	{"adv7183", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, adv7183_id);

static struct i2c_driver adv7183_driver = {
	.driver = {
		.owner  = THIS_MODULE,
		.name   = "adv7183",
	},
	.probe          = adv7183_probe,
	.remove         = adv7183_remove,
	.id_table       = adv7183_id,
};

module_i2c_driver(adv7183_driver);

MODULE_DESCRIPTION("Analog Devices ADV7183 video decoder driver");
MODULE_AUTHOR("Scott Jiang <Scott.Jiang.Linux@gmail.com>");
MODULE_LICENSE("GPL v2");
