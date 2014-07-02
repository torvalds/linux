/*
 * adv7180.c Analog Devices ADV7180 video decoder driver
 * Copyright (c) 2009 Intel Corporation
 * Copyright (C) 2013 Cogent Embedded, Inc.
 * Copyright (C) 2013 Renesas Solutions Corp.
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <media/v4l2-ioctl.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <linux/mutex.h>

#define ADV7180_INPUT_CONTROL_REG			0x00
#define ADV7180_INPUT_CONTROL_AD_PAL_BG_NTSC_J_SECAM	0x00
#define ADV7180_INPUT_CONTROL_AD_PAL_BG_NTSC_J_SECAM_PED 0x10
#define ADV7180_INPUT_CONTROL_AD_PAL_N_NTSC_J_SECAM	0x20
#define ADV7180_INPUT_CONTROL_AD_PAL_N_NTSC_M_SECAM	0x30
#define ADV7180_INPUT_CONTROL_NTSC_J			0x40
#define ADV7180_INPUT_CONTROL_NTSC_M			0x50
#define ADV7180_INPUT_CONTROL_PAL60			0x60
#define ADV7180_INPUT_CONTROL_NTSC_443			0x70
#define ADV7180_INPUT_CONTROL_PAL_BG			0x80
#define ADV7180_INPUT_CONTROL_PAL_N			0x90
#define ADV7180_INPUT_CONTROL_PAL_M			0xa0
#define ADV7180_INPUT_CONTROL_PAL_M_PED			0xb0
#define ADV7180_INPUT_CONTROL_PAL_COMB_N		0xc0
#define ADV7180_INPUT_CONTROL_PAL_COMB_N_PED		0xd0
#define ADV7180_INPUT_CONTROL_PAL_SECAM			0xe0
#define ADV7180_INPUT_CONTROL_PAL_SECAM_PED		0xf0
#define ADV7180_INPUT_CONTROL_INSEL_MASK		0x0f

#define ADV7180_EXTENDED_OUTPUT_CONTROL_REG		0x04
#define ADV7180_EXTENDED_OUTPUT_CONTROL_NTSCDIS		0xC5

#define ADV7180_AUTODETECT_ENABLE_REG			0x07
#define ADV7180_AUTODETECT_DEFAULT			0x7f
/* Contrast */
#define ADV7180_CON_REG		0x08	/*Unsigned */
#define ADV7180_CON_MIN		0
#define ADV7180_CON_DEF		128
#define ADV7180_CON_MAX		255
/* Brightness*/
#define ADV7180_BRI_REG		0x0a	/*Signed */
#define ADV7180_BRI_MIN		-128
#define ADV7180_BRI_DEF		0
#define ADV7180_BRI_MAX		127
/* Hue */
#define ADV7180_HUE_REG		0x0b	/*Signed, inverted */
#define ADV7180_HUE_MIN		-127
#define ADV7180_HUE_DEF		0
#define ADV7180_HUE_MAX		128

#define ADV7180_ADI_CTRL_REG				0x0e
#define ADV7180_ADI_CTRL_IRQ_SPACE			0x20

#define ADV7180_PWR_MAN_REG		0x0f
#define ADV7180_PWR_MAN_ON		0x04
#define ADV7180_PWR_MAN_OFF		0x24
#define ADV7180_PWR_MAN_RES		0x80

#define ADV7180_STATUS1_REG				0x10
#define ADV7180_STATUS1_IN_LOCK		0x01
#define ADV7180_STATUS1_AUTOD_MASK	0x70
#define ADV7180_STATUS1_AUTOD_NTSM_M_J	0x00
#define ADV7180_STATUS1_AUTOD_NTSC_4_43 0x10
#define ADV7180_STATUS1_AUTOD_PAL_M	0x20
#define ADV7180_STATUS1_AUTOD_PAL_60	0x30
#define ADV7180_STATUS1_AUTOD_PAL_B_G	0x40
#define ADV7180_STATUS1_AUTOD_SECAM	0x50
#define ADV7180_STATUS1_AUTOD_PAL_COMB	0x60
#define ADV7180_STATUS1_AUTOD_SECAM_525	0x70

#define ADV7180_IDENT_REG 0x11
#define ADV7180_ID_7180 0x18

#define ADV7180_ICONF1_ADI		0x40
#define ADV7180_ICONF1_ACTIVE_LOW	0x01
#define ADV7180_ICONF1_PSYNC_ONLY	0x10
#define ADV7180_ICONF1_ACTIVE_TO_CLR	0xC0
/* Saturation */
#define ADV7180_SD_SAT_CB_REG	0xe3	/*Unsigned */
#define ADV7180_SD_SAT_CR_REG	0xe4	/*Unsigned */
#define ADV7180_SAT_MIN		0
#define ADV7180_SAT_DEF		128
#define ADV7180_SAT_MAX		255

#define ADV7180_IRQ1_LOCK	0x01
#define ADV7180_IRQ1_UNLOCK	0x02
#define ADV7180_ISR1_ADI	0x42
#define ADV7180_ICR1_ADI	0x43
#define ADV7180_IMR1_ADI	0x44
#define ADV7180_IMR2_ADI	0x48
#define ADV7180_IRQ3_AD_CHANGE	0x08
#define ADV7180_ISR3_ADI	0x4A
#define ADV7180_ICR3_ADI	0x4B
#define ADV7180_IMR3_ADI	0x4C
#define ADV7180_IMR4_ADI	0x50

#define ADV7180_NTSC_V_BIT_END_REG	0xE6
#define ADV7180_NTSC_V_BIT_END_MANUAL_NVEND	0x4F

struct adv7180_state {
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_subdev	sd;
	struct mutex		mutex; /* mutual excl. when accessing chip */
	int			irq;
	v4l2_std_id		curr_norm;
	bool			autodetect;
	bool			powered;
	u8			input;
};
#define to_adv7180_sd(_ctrl) (&container_of(_ctrl->handler,		\
					    struct adv7180_state,	\
					    ctrl_hdl)->sd)

static v4l2_std_id adv7180_std_to_v4l2(u8 status1)
{
	/* in case V4L2_IN_ST_NO_SIGNAL */
	if (!(status1 & ADV7180_STATUS1_IN_LOCK))
		return V4L2_STD_UNKNOWN;

	switch (status1 & ADV7180_STATUS1_AUTOD_MASK) {
	case ADV7180_STATUS1_AUTOD_NTSM_M_J:
		return V4L2_STD_NTSC;
	case ADV7180_STATUS1_AUTOD_NTSC_4_43:
		return V4L2_STD_NTSC_443;
	case ADV7180_STATUS1_AUTOD_PAL_M:
		return V4L2_STD_PAL_M;
	case ADV7180_STATUS1_AUTOD_PAL_60:
		return V4L2_STD_PAL_60;
	case ADV7180_STATUS1_AUTOD_PAL_B_G:
		return V4L2_STD_PAL;
	case ADV7180_STATUS1_AUTOD_SECAM:
		return V4L2_STD_SECAM;
	case ADV7180_STATUS1_AUTOD_PAL_COMB:
		return V4L2_STD_PAL_Nc | V4L2_STD_PAL_N;
	case ADV7180_STATUS1_AUTOD_SECAM_525:
		return V4L2_STD_SECAM;
	default:
		return V4L2_STD_UNKNOWN;
	}
}

static int v4l2_std_to_adv7180(v4l2_std_id std)
{
	if (std == V4L2_STD_PAL_60)
		return ADV7180_INPUT_CONTROL_PAL60;
	if (std == V4L2_STD_NTSC_443)
		return ADV7180_INPUT_CONTROL_NTSC_443;
	if (std == V4L2_STD_PAL_N)
		return ADV7180_INPUT_CONTROL_PAL_N;
	if (std == V4L2_STD_PAL_M)
		return ADV7180_INPUT_CONTROL_PAL_M;
	if (std == V4L2_STD_PAL_Nc)
		return ADV7180_INPUT_CONTROL_PAL_COMB_N;

	if (std & V4L2_STD_PAL)
		return ADV7180_INPUT_CONTROL_PAL_BG;
	if (std & V4L2_STD_NTSC)
		return ADV7180_INPUT_CONTROL_NTSC_M;
	if (std & V4L2_STD_SECAM)
		return ADV7180_INPUT_CONTROL_PAL_SECAM;

	return -EINVAL;
}

static u32 adv7180_status_to_v4l2(u8 status1)
{
	if (!(status1 & ADV7180_STATUS1_IN_LOCK))
		return V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static int __adv7180_status(struct i2c_client *client, u32 *status,
			    v4l2_std_id *std)
{
	int status1 = i2c_smbus_read_byte_data(client, ADV7180_STATUS1_REG);

	if (status1 < 0)
		return status1;

	if (status)
		*status = adv7180_status_to_v4l2(status1);
	if (std)
		*std = adv7180_std_to_v4l2(status1);

	return 0;
}

static inline struct adv7180_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7180_state, sd);
}

static int adv7180_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct adv7180_state *state = to_state(sd);
	int err = mutex_lock_interruptible(&state->mutex);
	if (err)
		return err;

	/* when we are interrupt driven we know the state */
	if (!state->autodetect || state->irq > 0)
		*std = state->curr_norm;
	else
		err = __adv7180_status(v4l2_get_subdevdata(sd), NULL, std);

	mutex_unlock(&state->mutex);
	return err;
}

static int adv7180_s_routing(struct v4l2_subdev *sd, u32 input,
			     u32 output, u32 config)
{
	struct adv7180_state *state = to_state(sd);
	int ret = mutex_lock_interruptible(&state->mutex);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (ret)
		return ret;

	/* We cannot discriminate between LQFP and 40-pin LFCSP, so accept
	 * all inputs and let the card driver take care of validation
	 */
	if ((input & ADV7180_INPUT_CONTROL_INSEL_MASK) != input)
		goto out;

	ret = i2c_smbus_read_byte_data(client, ADV7180_INPUT_CONTROL_REG);

	if (ret < 0)
		goto out;

	ret &= ~ADV7180_INPUT_CONTROL_INSEL_MASK;
	ret = i2c_smbus_write_byte_data(client,
					ADV7180_INPUT_CONTROL_REG, ret | input);
	state->input = input;
out:
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7180_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct adv7180_state *state = to_state(sd);
	int ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	ret = __adv7180_status(v4l2_get_subdevdata(sd), status, NULL);
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7180_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7180_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	/* all standards -> autodetect */
	if (std == V4L2_STD_ALL) {
		ret =
		    i2c_smbus_write_byte_data(client, ADV7180_INPUT_CONTROL_REG,
				ADV7180_INPUT_CONTROL_AD_PAL_BG_NTSC_J_SECAM
					      | state->input);
		if (ret < 0)
			goto out;

		__adv7180_status(client, NULL, &state->curr_norm);
		state->autodetect = true;
	} else {
		ret = v4l2_std_to_adv7180(std);
		if (ret < 0)
			goto out;

		ret = i2c_smbus_write_byte_data(client,
						ADV7180_INPUT_CONTROL_REG,
						ret | state->input);
		if (ret < 0)
			goto out;

		state->curr_norm = std;
		state->autodetect = false;
	}
	ret = 0;
out:
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7180_set_power(struct adv7180_state *state,
	struct i2c_client *client, bool on)
{
	u8 val;

	if (on)
		val = ADV7180_PWR_MAN_ON;
	else
		val = ADV7180_PWR_MAN_OFF;

	return i2c_smbus_write_byte_data(client, ADV7180_PWR_MAN_REG, val);
}

static int adv7180_s_power(struct v4l2_subdev *sd, int on)
{
	struct adv7180_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	ret = adv7180_set_power(state, client, on);
	if (ret == 0)
		state->powered = on;

	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7180_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_adv7180_sd(ctrl);
	struct adv7180_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = mutex_lock_interruptible(&state->mutex);
	int val;

	if (ret)
		return ret;
	val = ctrl->val;
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ret = i2c_smbus_write_byte_data(client, ADV7180_BRI_REG, val);
		break;
	case V4L2_CID_HUE:
		/*Hue is inverted according to HSL chart */
		ret = i2c_smbus_write_byte_data(client, ADV7180_HUE_REG, -val);
		break;
	case V4L2_CID_CONTRAST:
		ret = i2c_smbus_write_byte_data(client, ADV7180_CON_REG, val);
		break;
	case V4L2_CID_SATURATION:
		/*
		 *This could be V4L2_CID_BLUE_BALANCE/V4L2_CID_RED_BALANCE
		 *Let's not confuse the user, everybody understands saturation
		 */
		ret = i2c_smbus_write_byte_data(client, ADV7180_SD_SAT_CB_REG,
						val);
		if (ret < 0)
			break;
		ret = i2c_smbus_write_byte_data(client, ADV7180_SD_SAT_CR_REG,
						val);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&state->mutex);
	return ret;
}

static const struct v4l2_ctrl_ops adv7180_ctrl_ops = {
	.s_ctrl = adv7180_s_ctrl,
};

static int adv7180_init_controls(struct adv7180_state *state)
{
	v4l2_ctrl_handler_init(&state->ctrl_hdl, 4);

	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7180_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, ADV7180_BRI_MIN,
			  ADV7180_BRI_MAX, 1, ADV7180_BRI_DEF);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7180_ctrl_ops,
			  V4L2_CID_CONTRAST, ADV7180_CON_MIN,
			  ADV7180_CON_MAX, 1, ADV7180_CON_DEF);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7180_ctrl_ops,
			  V4L2_CID_SATURATION, ADV7180_SAT_MIN,
			  ADV7180_SAT_MAX, 1, ADV7180_SAT_DEF);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7180_ctrl_ops,
			  V4L2_CID_HUE, ADV7180_HUE_MIN,
			  ADV7180_HUE_MAX, 1, ADV7180_HUE_DEF);
	state->sd.ctrl_handler = &state->ctrl_hdl;
	if (state->ctrl_hdl.error) {
		int err = state->ctrl_hdl.error;

		v4l2_ctrl_handler_free(&state->ctrl_hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&state->ctrl_hdl);

	return 0;
}
static void adv7180_exit_controls(struct adv7180_state *state)
{
	v4l2_ctrl_handler_free(&state->ctrl_hdl);
}

static int adv7180_enum_mbus_fmt(struct v4l2_subdev *sd, unsigned int index,
				 enum v4l2_mbus_pixelcode *code)
{
	if (index > 0)
		return -EINVAL;

	*code = V4L2_MBUS_FMT_YUYV8_2X8;

	return 0;
}

static int adv7180_mbus_fmt(struct v4l2_subdev *sd,
			    struct v4l2_mbus_framefmt *fmt)
{
	struct adv7180_state *state = to_state(sd);

	fmt->code = V4L2_MBUS_FMT_YUYV8_2X8;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt->field = V4L2_FIELD_INTERLACED;
	fmt->width = 720;
	fmt->height = state->curr_norm & V4L2_STD_525_60 ? 480 : 576;

	return 0;
}

static int adv7180_g_mbus_config(struct v4l2_subdev *sd,
				 struct v4l2_mbus_config *cfg)
{
	/*
	 * The ADV7180 sensor supports BT.601/656 output modes.
	 * The BT.656 is default and not yet configurable by s/w.
	 */
	cfg->flags = V4L2_MBUS_MASTER | V4L2_MBUS_PCLK_SAMPLE_RISING |
		     V4L2_MBUS_DATA_ACTIVE_HIGH;
	cfg->type = V4L2_MBUS_BT656;

	return 0;
}

static const struct v4l2_subdev_video_ops adv7180_video_ops = {
	.s_std = adv7180_s_std,
	.querystd = adv7180_querystd,
	.g_input_status = adv7180_g_input_status,
	.s_routing = adv7180_s_routing,
	.enum_mbus_fmt = adv7180_enum_mbus_fmt,
	.try_mbus_fmt = adv7180_mbus_fmt,
	.g_mbus_fmt = adv7180_mbus_fmt,
	.s_mbus_fmt = adv7180_mbus_fmt,
	.g_mbus_config = adv7180_g_mbus_config,
};

static const struct v4l2_subdev_core_ops adv7180_core_ops = {
	.s_power = adv7180_s_power,
};

static const struct v4l2_subdev_ops adv7180_ops = {
	.core = &adv7180_core_ops,
	.video = &adv7180_video_ops,
};

static irqreturn_t adv7180_irq(int irq, void *devid)
{
	struct adv7180_state *state = devid;
	struct i2c_client *client = v4l2_get_subdevdata(&state->sd);
	u8 isr3;

	mutex_lock(&state->mutex);
	i2c_smbus_write_byte_data(client, ADV7180_ADI_CTRL_REG,
				  ADV7180_ADI_CTRL_IRQ_SPACE);
	isr3 = i2c_smbus_read_byte_data(client, ADV7180_ISR3_ADI);
	/* clear */
	i2c_smbus_write_byte_data(client, ADV7180_ICR3_ADI, isr3);
	i2c_smbus_write_byte_data(client, ADV7180_ADI_CTRL_REG, 0);

	if (isr3 & ADV7180_IRQ3_AD_CHANGE && state->autodetect)
		__adv7180_status(client, NULL, &state->curr_norm);
	mutex_unlock(&state->mutex);

	return IRQ_HANDLED;
}

static int init_device(struct i2c_client *client, struct adv7180_state *state)
{
	int ret;

	/* Initialize adv7180 */
	/* Enable autodetection */
	if (state->autodetect) {
		ret =
		    i2c_smbus_write_byte_data(client, ADV7180_INPUT_CONTROL_REG,
				ADV7180_INPUT_CONTROL_AD_PAL_BG_NTSC_J_SECAM
					      | state->input);
		if (ret < 0)
			return ret;

		ret =
		    i2c_smbus_write_byte_data(client,
					      ADV7180_AUTODETECT_ENABLE_REG,
					      ADV7180_AUTODETECT_DEFAULT);
		if (ret < 0)
			return ret;
	} else {
		ret = v4l2_std_to_adv7180(state->curr_norm);
		if (ret < 0)
			return ret;

		ret =
		    i2c_smbus_write_byte_data(client, ADV7180_INPUT_CONTROL_REG,
					      ret | state->input);
		if (ret < 0)
			return ret;

	}
	/* ITU-R BT.656-4 compatible */
	ret = i2c_smbus_write_byte_data(client,
			ADV7180_EXTENDED_OUTPUT_CONTROL_REG,
			ADV7180_EXTENDED_OUTPUT_CONTROL_NTSCDIS);
	if (ret < 0)
		return ret;

	/* Manually set V bit end position in NTSC mode */
	ret = i2c_smbus_write_byte_data(client,
					ADV7180_NTSC_V_BIT_END_REG,
					ADV7180_NTSC_V_BIT_END_MANUAL_NVEND);
	if (ret < 0)
		return ret;

	/* read current norm */
	__adv7180_status(client, NULL, &state->curr_norm);

	/* register for interrupts */
	if (state->irq > 0) {
		ret = request_threaded_irq(state->irq, NULL, adv7180_irq,
					   IRQF_ONESHOT, KBUILD_MODNAME, state);
		if (ret)
			return ret;

		ret = i2c_smbus_write_byte_data(client, ADV7180_ADI_CTRL_REG,
						ADV7180_ADI_CTRL_IRQ_SPACE);
		if (ret < 0)
			goto err;

		/* config the Interrupt pin to be active low */
		ret = i2c_smbus_write_byte_data(client, ADV7180_ICONF1_ADI,
						ADV7180_ICONF1_ACTIVE_LOW |
						ADV7180_ICONF1_PSYNC_ONLY);
		if (ret < 0)
			goto err;

		ret = i2c_smbus_write_byte_data(client, ADV7180_IMR1_ADI, 0);
		if (ret < 0)
			goto err;

		ret = i2c_smbus_write_byte_data(client, ADV7180_IMR2_ADI, 0);
		if (ret < 0)
			goto err;

		/* enable AD change interrupts interrupts */
		ret = i2c_smbus_write_byte_data(client, ADV7180_IMR3_ADI,
						ADV7180_IRQ3_AD_CHANGE);
		if (ret < 0)
			goto err;

		ret = i2c_smbus_write_byte_data(client, ADV7180_IMR4_ADI, 0);
		if (ret < 0)
			goto err;

		ret = i2c_smbus_write_byte_data(client, ADV7180_ADI_CTRL_REG,
						0);
		if (ret < 0)
			goto err;
	}

	return 0;

err:
	free_irq(state->irq, state);
	return ret;
}

static int adv7180_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adv7180_state *state;
	struct v4l2_subdev *sd;
	int ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
		 client->addr, client->adapter->name);

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (state == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	state->irq = client->irq;
	mutex_init(&state->mutex);
	state->autodetect = true;
	state->powered = true;
	state->input = 0;
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &adv7180_ops);

	ret = adv7180_init_controls(state);
	if (ret)
		goto err_unreg_subdev;
	ret = init_device(client, state);
	if (ret)
		goto err_free_ctrl;

	ret = v4l2_async_register_subdev(sd);
	if (ret)
		goto err_free_irq;

	return 0;

err_free_irq:
	if (state->irq > 0)
		free_irq(client->irq, state);
err_free_ctrl:
	adv7180_exit_controls(state);
err_unreg_subdev:
	mutex_destroy(&state->mutex);
err:
	return ret;
}

static int adv7180_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7180_state *state = to_state(sd);

	v4l2_async_unregister_subdev(sd);

	if (state->irq > 0)
		free_irq(client->irq, state);

	v4l2_device_unregister_subdev(sd);
	adv7180_exit_controls(state);
	mutex_destroy(&state->mutex);
	return 0;
}

static const struct i2c_device_id adv7180_id[] = {
	{KBUILD_MODNAME, 0},
	{},
};

#ifdef CONFIG_PM_SLEEP
static int adv7180_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7180_state *state = to_state(sd);

	return adv7180_set_power(state, client, false);
}

static int adv7180_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7180_state *state = to_state(sd);
	int ret;

	if (state->powered) {
		ret = adv7180_set_power(state, client, true);
		if (ret)
			return ret;
	}
	ret = init_device(client, state);
	if (ret < 0)
		return ret;
	return 0;
}

static SIMPLE_DEV_PM_OPS(adv7180_pm_ops, adv7180_suspend, adv7180_resume);
#define ADV7180_PM_OPS (&adv7180_pm_ops)

#else
#define ADV7180_PM_OPS NULL
#endif

MODULE_DEVICE_TABLE(i2c, adv7180_id);

static struct i2c_driver adv7180_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = KBUILD_MODNAME,
		   .pm = ADV7180_PM_OPS,
		   },
	.probe = adv7180_probe,
	.remove = adv7180_remove,
	.id_table = adv7180_id,
};

module_i2c_driver(adv7180_driver);

MODULE_DESCRIPTION("Analog Devices ADV7180 video decoder driver");
MODULE_AUTHOR("Mocean Laboratories");
MODULE_LICENSE("GPL v2");
