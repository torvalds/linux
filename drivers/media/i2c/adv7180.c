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
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/videodev2.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#define ADV7180_STD_AD_PAL_BG_NTSC_J_SECAM		0x0
#define ADV7180_STD_AD_PAL_BG_NTSC_J_SECAM_PED		0x1
#define ADV7180_STD_AD_PAL_N_NTSC_J_SECAM		0x2
#define ADV7180_STD_AD_PAL_N_NTSC_M_SECAM		0x3
#define ADV7180_STD_NTSC_J				0x4
#define ADV7180_STD_NTSC_M				0x5
#define ADV7180_STD_PAL60				0x6
#define ADV7180_STD_NTSC_443				0x7
#define ADV7180_STD_PAL_BG				0x8
#define ADV7180_STD_PAL_N				0x9
#define ADV7180_STD_PAL_M				0xa
#define ADV7180_STD_PAL_M_PED				0xb
#define ADV7180_STD_PAL_COMB_N				0xc
#define ADV7180_STD_PAL_COMB_N_PED			0xd
#define ADV7180_STD_PAL_SECAM				0xe
#define ADV7180_STD_PAL_SECAM_PED			0xf

#define ADV7180_REG_INPUT_CONTROL			0x0000
#define ADV7180_INPUT_CONTROL_INSEL_MASK		0x0f

#define ADV7182_REG_INPUT_VIDSEL			0x0002

#define ADV7180_REG_OUTPUT_CONTROL			0x0003
#define ADV7180_REG_EXTENDED_OUTPUT_CONTROL		0x0004
#define ADV7180_EXTENDED_OUTPUT_CONTROL_NTSCDIS		0xC5

#define ADV7180_REG_AUTODETECT_ENABLE			0x0007
#define ADV7180_AUTODETECT_DEFAULT			0x7f
/* Contrast */
#define ADV7180_REG_CON		0x0008	/*Unsigned */
#define ADV7180_CON_MIN		0
#define ADV7180_CON_DEF		128
#define ADV7180_CON_MAX		255
/* Brightness*/
#define ADV7180_REG_BRI		0x000a	/*Signed */
#define ADV7180_BRI_MIN		-128
#define ADV7180_BRI_DEF		0
#define ADV7180_BRI_MAX		127
/* Hue */
#define ADV7180_REG_HUE		0x000b	/*Signed, inverted */
#define ADV7180_HUE_MIN		-127
#define ADV7180_HUE_DEF		0
#define ADV7180_HUE_MAX		128

#define ADV7180_REG_CTRL		0x000e
#define ADV7180_CTRL_IRQ_SPACE		0x20

#define ADV7180_REG_PWR_MAN		0x0f
#define ADV7180_PWR_MAN_ON		0x04
#define ADV7180_PWR_MAN_OFF		0x24
#define ADV7180_PWR_MAN_RES		0x80

#define ADV7180_REG_STATUS1		0x0010
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

#define ADV7180_REG_IDENT 0x0011
#define ADV7180_ID_7180 0x18

#define ADV7180_REG_STATUS3		0x0013
#define ADV7180_REG_ANALOG_CLAMP_CTL	0x0014
#define ADV7180_REG_SHAP_FILTER_CTL_1	0x0017
#define ADV7180_REG_CTRL_2		0x001d
#define ADV7180_REG_VSYNC_FIELD_CTL_1	0x0031
#define ADV7180_REG_MANUAL_WIN_CTL_1	0x003d
#define ADV7180_REG_MANUAL_WIN_CTL_2	0x003e
#define ADV7180_REG_MANUAL_WIN_CTL_3	0x003f
#define ADV7180_REG_LOCK_CNT		0x0051
#define ADV7180_REG_CVBS_TRIM		0x0052
#define ADV7180_REG_CLAMP_ADJ		0x005a
#define ADV7180_REG_RES_CIR		0x005f
#define ADV7180_REG_DIFF_MODE		0x0060

#define ADV7180_REG_ICONF1		0x2040
#define ADV7180_ICONF1_ACTIVE_LOW	0x01
#define ADV7180_ICONF1_PSYNC_ONLY	0x10
#define ADV7180_ICONF1_ACTIVE_TO_CLR	0xC0
/* Saturation */
#define ADV7180_REG_SD_SAT_CB	0x00e3	/*Unsigned */
#define ADV7180_REG_SD_SAT_CR	0x00e4	/*Unsigned */
#define ADV7180_SAT_MIN		0
#define ADV7180_SAT_DEF		128
#define ADV7180_SAT_MAX		255

#define ADV7180_IRQ1_LOCK	0x01
#define ADV7180_IRQ1_UNLOCK	0x02
#define ADV7180_REG_ISR1	0x2042
#define ADV7180_REG_ICR1	0x2043
#define ADV7180_REG_IMR1	0x2044
#define ADV7180_REG_IMR2	0x2048
#define ADV7180_IRQ3_AD_CHANGE	0x08
#define ADV7180_REG_ISR3	0x204A
#define ADV7180_REG_ICR3	0x204B
#define ADV7180_REG_IMR3	0x204C
#define ADV7180_REG_IMR4	0x2050

#define ADV7180_REG_NTSC_V_BIT_END	0x00E6
#define ADV7180_NTSC_V_BIT_END_MANUAL_NVEND	0x4F

#define ADV7180_REG_VPP_SLAVE_ADDR	0xFD
#define ADV7180_REG_CSI_SLAVE_ADDR	0xFE

#define ADV7180_REG_ACE_CTRL1		0x4080
#define ADV7180_REG_ACE_CTRL5		0x4084
#define ADV7180_REG_FLCONTROL		0x40e0
#define ADV7180_FLCONTROL_FL_ENABLE 0x1

#define ADV7180_REG_RST_CLAMP	0x809c
#define ADV7180_REG_AGC_ADJ1	0x80b6
#define ADV7180_REG_AGC_ADJ2	0x80c0

#define ADV7180_CSI_REG_PWRDN	0x00
#define ADV7180_CSI_PWRDN	0x80

#define ADV7180_INPUT_CVBS_AIN1 0x00
#define ADV7180_INPUT_CVBS_AIN2 0x01
#define ADV7180_INPUT_CVBS_AIN3 0x02
#define ADV7180_INPUT_CVBS_AIN4 0x03
#define ADV7180_INPUT_CVBS_AIN5 0x04
#define ADV7180_INPUT_CVBS_AIN6 0x05
#define ADV7180_INPUT_SVIDEO_AIN1_AIN2 0x06
#define ADV7180_INPUT_SVIDEO_AIN3_AIN4 0x07
#define ADV7180_INPUT_SVIDEO_AIN5_AIN6 0x08
#define ADV7180_INPUT_YPRPB_AIN1_AIN2_AIN3 0x09
#define ADV7180_INPUT_YPRPB_AIN4_AIN5_AIN6 0x0a

#define ADV7182_INPUT_CVBS_AIN1 0x00
#define ADV7182_INPUT_CVBS_AIN2 0x01
#define ADV7182_INPUT_CVBS_AIN3 0x02
#define ADV7182_INPUT_CVBS_AIN4 0x03
#define ADV7182_INPUT_CVBS_AIN5 0x04
#define ADV7182_INPUT_CVBS_AIN6 0x05
#define ADV7182_INPUT_CVBS_AIN7 0x06
#define ADV7182_INPUT_CVBS_AIN8 0x07
#define ADV7182_INPUT_SVIDEO_AIN1_AIN2 0x08
#define ADV7182_INPUT_SVIDEO_AIN3_AIN4 0x09
#define ADV7182_INPUT_SVIDEO_AIN5_AIN6 0x0a
#define ADV7182_INPUT_SVIDEO_AIN7_AIN8 0x0b
#define ADV7182_INPUT_YPRPB_AIN1_AIN2_AIN3 0x0c
#define ADV7182_INPUT_YPRPB_AIN4_AIN5_AIN6 0x0d
#define ADV7182_INPUT_DIFF_CVBS_AIN1_AIN2 0x0e
#define ADV7182_INPUT_DIFF_CVBS_AIN3_AIN4 0x0f
#define ADV7182_INPUT_DIFF_CVBS_AIN5_AIN6 0x10
#define ADV7182_INPUT_DIFF_CVBS_AIN7_AIN8 0x11

#define ADV7180_DEFAULT_CSI_I2C_ADDR 0x44
#define ADV7180_DEFAULT_VPP_I2C_ADDR 0x42

#define V4L2_CID_ADV_FAST_SWITCH	(V4L2_CID_USER_ADV7180_BASE + 0x00)

struct adv7180_state;

#define ADV7180_FLAG_RESET_POWERED	BIT(0)
#define ADV7180_FLAG_V2			BIT(1)
#define ADV7180_FLAG_MIPI_CSI2		BIT(2)
#define ADV7180_FLAG_I2P		BIT(3)

struct adv7180_chip_info {
	unsigned int flags;
	unsigned int valid_input_mask;
	int (*set_std)(struct adv7180_state *st, unsigned int std);
	int (*select_input)(struct adv7180_state *st, unsigned int input);
	int (*init)(struct adv7180_state *state);
};

struct adv7180_state {
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_subdev	sd;
	struct media_pad	pad;
	struct mutex		mutex; /* mutual excl. when accessing chip */
	int			irq;
	struct gpio_desc	*pwdn_gpio;
	v4l2_std_id		curr_norm;
	bool			powered;
	bool			streaming;
	u8			input;

	struct i2c_client	*client;
	unsigned int		register_page;
	struct i2c_client	*csi_client;
	struct i2c_client	*vpp_client;
	const struct adv7180_chip_info *chip_info;
	enum v4l2_field		field;
};
#define to_adv7180_sd(_ctrl) (&container_of(_ctrl->handler,		\
					    struct adv7180_state,	\
					    ctrl_hdl)->sd)

static int adv7180_select_page(struct adv7180_state *state, unsigned int page)
{
	if (state->register_page != page) {
		i2c_smbus_write_byte_data(state->client, ADV7180_REG_CTRL,
			page);
		state->register_page = page;
	}

	return 0;
}

static int adv7180_write(struct adv7180_state *state, unsigned int reg,
	unsigned int value)
{
	lockdep_assert_held(&state->mutex);
	adv7180_select_page(state, reg >> 8);
	return i2c_smbus_write_byte_data(state->client, reg & 0xff, value);
}

static int adv7180_read(struct adv7180_state *state, unsigned int reg)
{
	lockdep_assert_held(&state->mutex);
	adv7180_select_page(state, reg >> 8);
	return i2c_smbus_read_byte_data(state->client, reg & 0xff);
}

static int adv7180_csi_write(struct adv7180_state *state, unsigned int reg,
	unsigned int value)
{
	return i2c_smbus_write_byte_data(state->csi_client, reg, value);
}

static int adv7180_set_video_standard(struct adv7180_state *state,
	unsigned int std)
{
	return state->chip_info->set_std(state, std);
}

static int adv7180_vpp_write(struct adv7180_state *state, unsigned int reg,
	unsigned int value)
{
	return i2c_smbus_write_byte_data(state->vpp_client, reg, value);
}

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
		return ADV7180_STD_PAL60;
	if (std == V4L2_STD_NTSC_443)
		return ADV7180_STD_NTSC_443;
	if (std == V4L2_STD_PAL_N)
		return ADV7180_STD_PAL_N;
	if (std == V4L2_STD_PAL_M)
		return ADV7180_STD_PAL_M;
	if (std == V4L2_STD_PAL_Nc)
		return ADV7180_STD_PAL_COMB_N;

	if (std & V4L2_STD_PAL)
		return ADV7180_STD_PAL_BG;
	if (std & V4L2_STD_NTSC)
		return ADV7180_STD_NTSC_M;
	if (std & V4L2_STD_SECAM)
		return ADV7180_STD_PAL_SECAM;

	return -EINVAL;
}

static u32 adv7180_status_to_v4l2(u8 status1)
{
	if (!(status1 & ADV7180_STATUS1_IN_LOCK))
		return V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static int __adv7180_status(struct adv7180_state *state, u32 *status,
			    v4l2_std_id *std)
{
	int status1 = adv7180_read(state, ADV7180_REG_STATUS1);

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

	if (state->streaming) {
		err = -EBUSY;
		goto unlock;
	}

	err = adv7180_set_video_standard(state,
			ADV7180_STD_AD_PAL_BG_NTSC_J_SECAM);
	if (err)
		goto unlock;

	msleep(100);
	__adv7180_status(state, NULL, std);

	err = v4l2_std_to_adv7180(state->curr_norm);
	if (err < 0)
		goto unlock;

	err = adv7180_set_video_standard(state, err);

unlock:
	mutex_unlock(&state->mutex);
	return err;
}

static int adv7180_s_routing(struct v4l2_subdev *sd, u32 input,
			     u32 output, u32 config)
{
	struct adv7180_state *state = to_state(sd);
	int ret = mutex_lock_interruptible(&state->mutex);

	if (ret)
		return ret;

	if (input > 31 || !(BIT(input) & state->chip_info->valid_input_mask)) {
		ret = -EINVAL;
		goto out;
	}

	ret = state->chip_info->select_input(state, input);

	if (ret == 0)
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

	ret = __adv7180_status(state, status, NULL);
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7180_program_std(struct adv7180_state *state)
{
	int ret;

	ret = v4l2_std_to_adv7180(state->curr_norm);
	if (ret < 0)
		return ret;

	ret = adv7180_set_video_standard(state, ret);
	if (ret < 0)
		return ret;
	return 0;
}

static int adv7180_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7180_state *state = to_state(sd);
	int ret = mutex_lock_interruptible(&state->mutex);

	if (ret)
		return ret;

	/* Make sure we can support this std */
	ret = v4l2_std_to_adv7180(std);
	if (ret < 0)
		goto out;

	state->curr_norm = std;

	ret = adv7180_program_std(state);
out:
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7180_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct adv7180_state *state = to_state(sd);

	*norm = state->curr_norm;

	return 0;
}

static void adv7180_set_power_pin(struct adv7180_state *state, bool on)
{
	if (!state->pwdn_gpio)
		return;

	if (on) {
		gpiod_set_value_cansleep(state->pwdn_gpio, 0);
		usleep_range(5000, 10000);
	} else {
		gpiod_set_value_cansleep(state->pwdn_gpio, 1);
	}
}

static int adv7180_set_power(struct adv7180_state *state, bool on)
{
	u8 val;
	int ret;

	if (on)
		val = ADV7180_PWR_MAN_ON;
	else
		val = ADV7180_PWR_MAN_OFF;

	ret = adv7180_write(state, ADV7180_REG_PWR_MAN, val);
	if (ret)
		return ret;

	if (state->chip_info->flags & ADV7180_FLAG_MIPI_CSI2) {
		if (on) {
			adv7180_csi_write(state, 0xDE, 0x02);
			adv7180_csi_write(state, 0xD2, 0xF7);
			adv7180_csi_write(state, 0xD8, 0x65);
			adv7180_csi_write(state, 0xE0, 0x09);
			adv7180_csi_write(state, 0x2C, 0x00);
			if (state->field == V4L2_FIELD_NONE)
				adv7180_csi_write(state, 0x1D, 0x80);
			adv7180_csi_write(state, 0x00, 0x00);
		} else {
			adv7180_csi_write(state, 0x00, 0x80);
		}
	}

	return 0;
}

static int adv7180_s_power(struct v4l2_subdev *sd, int on)
{
	struct adv7180_state *state = to_state(sd);
	int ret;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	ret = adv7180_set_power(state, on);
	if (ret == 0)
		state->powered = on;

	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7180_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_adv7180_sd(ctrl);
	struct adv7180_state *state = to_state(sd);
	int ret = mutex_lock_interruptible(&state->mutex);
	int val;

	if (ret)
		return ret;
	val = ctrl->val;
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ret = adv7180_write(state, ADV7180_REG_BRI, val);
		break;
	case V4L2_CID_HUE:
		/*Hue is inverted according to HSL chart */
		ret = adv7180_write(state, ADV7180_REG_HUE, -val);
		break;
	case V4L2_CID_CONTRAST:
		ret = adv7180_write(state, ADV7180_REG_CON, val);
		break;
	case V4L2_CID_SATURATION:
		/*
		 *This could be V4L2_CID_BLUE_BALANCE/V4L2_CID_RED_BALANCE
		 *Let's not confuse the user, everybody understands saturation
		 */
		ret = adv7180_write(state, ADV7180_REG_SD_SAT_CB, val);
		if (ret < 0)
			break;
		ret = adv7180_write(state, ADV7180_REG_SD_SAT_CR, val);
		break;
	case V4L2_CID_ADV_FAST_SWITCH:
		if (ctrl->val) {
			/* ADI required write */
			adv7180_write(state, 0x80d9, 0x44);
			adv7180_write(state, ADV7180_REG_FLCONTROL,
				ADV7180_FLCONTROL_FL_ENABLE);
		} else {
			/* ADI required write */
			adv7180_write(state, 0x80d9, 0xc4);
			adv7180_write(state, ADV7180_REG_FLCONTROL, 0x00);
		}
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

static const struct v4l2_ctrl_config adv7180_ctrl_fast_switch = {
	.ops = &adv7180_ctrl_ops,
	.id = V4L2_CID_ADV_FAST_SWITCH,
	.name = "Fast Switching",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
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
	v4l2_ctrl_new_custom(&state->ctrl_hdl, &adv7180_ctrl_fast_switch, NULL);

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

static int adv7180_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_YUYV8_2X8;

	return 0;
}

static int adv7180_mbus_fmt(struct v4l2_subdev *sd,
			    struct v4l2_mbus_framefmt *fmt)
{
	struct adv7180_state *state = to_state(sd);

	fmt->code = MEDIA_BUS_FMT_YUYV8_2X8;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt->width = 720;
	fmt->height = state->curr_norm & V4L2_STD_525_60 ? 480 : 576;

	return 0;
}

static int adv7180_set_field_mode(struct adv7180_state *state)
{
	if (!(state->chip_info->flags & ADV7180_FLAG_I2P))
		return 0;

	if (state->field == V4L2_FIELD_NONE) {
		if (state->chip_info->flags & ADV7180_FLAG_MIPI_CSI2) {
			adv7180_csi_write(state, 0x01, 0x20);
			adv7180_csi_write(state, 0x02, 0x28);
			adv7180_csi_write(state, 0x03, 0x38);
			adv7180_csi_write(state, 0x04, 0x30);
			adv7180_csi_write(state, 0x05, 0x30);
			adv7180_csi_write(state, 0x06, 0x80);
			adv7180_csi_write(state, 0x07, 0x70);
			adv7180_csi_write(state, 0x08, 0x50);
		}
		adv7180_vpp_write(state, 0xa3, 0x00);
		adv7180_vpp_write(state, 0x5b, 0x00);
		adv7180_vpp_write(state, 0x55, 0x80);
	} else {
		if (state->chip_info->flags & ADV7180_FLAG_MIPI_CSI2) {
			adv7180_csi_write(state, 0x01, 0x18);
			adv7180_csi_write(state, 0x02, 0x18);
			adv7180_csi_write(state, 0x03, 0x30);
			adv7180_csi_write(state, 0x04, 0x20);
			adv7180_csi_write(state, 0x05, 0x28);
			adv7180_csi_write(state, 0x06, 0x40);
			adv7180_csi_write(state, 0x07, 0x58);
			adv7180_csi_write(state, 0x08, 0x30);
		}
		adv7180_vpp_write(state, 0xa3, 0x70);
		adv7180_vpp_write(state, 0x5b, 0x80);
		adv7180_vpp_write(state, 0x55, 0x00);
	}

	return 0;
}

static int adv7180_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct adv7180_state *state = to_state(sd);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		format->format = *v4l2_subdev_get_try_format(sd, cfg, 0);
	} else {
		adv7180_mbus_fmt(sd, &format->format);
		format->format.field = state->field;
	}

	return 0;
}

static int adv7180_set_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct adv7180_state *state = to_state(sd);
	struct v4l2_mbus_framefmt *framefmt;

	switch (format->format.field) {
	case V4L2_FIELD_NONE:
		if (!(state->chip_info->flags & ADV7180_FLAG_I2P))
			format->format.field = V4L2_FIELD_INTERLACED;
		break;
	default:
		format->format.field = V4L2_FIELD_INTERLACED;
		break;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		framefmt = &format->format;
		if (state->field != format->format.field) {
			state->field = format->format.field;
			adv7180_set_power(state, false);
			adv7180_set_field_mode(state);
			adv7180_set_power(state, true);
		}
	} else {
		framefmt = v4l2_subdev_get_try_format(sd, cfg, 0);
		*framefmt = format->format;
	}

	return adv7180_mbus_fmt(sd, framefmt);
}

static int adv7180_g_mbus_config(struct v4l2_subdev *sd,
				 struct v4l2_mbus_config *cfg)
{
	struct adv7180_state *state = to_state(sd);

	if (state->chip_info->flags & ADV7180_FLAG_MIPI_CSI2) {
		cfg->type = V4L2_MBUS_CSI2;
		cfg->flags = V4L2_MBUS_CSI2_1_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else {
		/*
		 * The ADV7180 sensor supports BT.601/656 output modes.
		 * The BT.656 is default and not yet configurable by s/w.
		 */
		cfg->flags = V4L2_MBUS_MASTER | V4L2_MBUS_PCLK_SAMPLE_RISING |
				 V4L2_MBUS_DATA_ACTIVE_HIGH;
		cfg->type = V4L2_MBUS_BT656;
	}

	return 0;
}

static int adv7180_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *cropcap)
{
	struct adv7180_state *state = to_state(sd);

	if (state->curr_norm & V4L2_STD_525_60) {
		cropcap->pixelaspect.numerator = 11;
		cropcap->pixelaspect.denominator = 10;
	} else {
		cropcap->pixelaspect.numerator = 54;
		cropcap->pixelaspect.denominator = 59;
	}

	return 0;
}

static int adv7180_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	*norm = V4L2_STD_ALL;
	return 0;
}

static int adv7180_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct adv7180_state *state = to_state(sd);
	int ret;

	/* It's always safe to stop streaming, no need to take the lock */
	if (!enable) {
		state->streaming = enable;
		return 0;
	}

	/* Must wait until querystd released the lock */
	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;
	state->streaming = enable;
	mutex_unlock(&state->mutex);
	return 0;
}

static int adv7180_subscribe_event(struct v4l2_subdev *sd,
				   struct v4l2_fh *fh,
				   struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_subdev_video_ops adv7180_video_ops = {
	.s_std = adv7180_s_std,
	.g_std = adv7180_g_std,
	.querystd = adv7180_querystd,
	.g_input_status = adv7180_g_input_status,
	.s_routing = adv7180_s_routing,
	.g_mbus_config = adv7180_g_mbus_config,
	.cropcap = adv7180_cropcap,
	.g_tvnorms = adv7180_g_tvnorms,
	.s_stream = adv7180_s_stream,
};

static const struct v4l2_subdev_core_ops adv7180_core_ops = {
	.s_power = adv7180_s_power,
	.subscribe_event = adv7180_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_pad_ops adv7180_pad_ops = {
	.enum_mbus_code = adv7180_enum_mbus_code,
	.set_fmt = adv7180_set_pad_format,
	.get_fmt = adv7180_get_pad_format,
};

static const struct v4l2_subdev_ops adv7180_ops = {
	.core = &adv7180_core_ops,
	.video = &adv7180_video_ops,
	.pad = &adv7180_pad_ops,
};

static irqreturn_t adv7180_irq(int irq, void *devid)
{
	struct adv7180_state *state = devid;
	u8 isr3;

	mutex_lock(&state->mutex);
	isr3 = adv7180_read(state, ADV7180_REG_ISR3);
	/* clear */
	adv7180_write(state, ADV7180_REG_ICR3, isr3);

	if (isr3 & ADV7180_IRQ3_AD_CHANGE) {
		static const struct v4l2_event src_ch = {
			.type = V4L2_EVENT_SOURCE_CHANGE,
			.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
		};

		v4l2_subdev_notify_event(&state->sd, &src_ch);
	}
	mutex_unlock(&state->mutex);

	return IRQ_HANDLED;
}

static int adv7180_init(struct adv7180_state *state)
{
	int ret;

	/* ITU-R BT.656-4 compatible */
	ret = adv7180_write(state, ADV7180_REG_EXTENDED_OUTPUT_CONTROL,
			ADV7180_EXTENDED_OUTPUT_CONTROL_NTSCDIS);
	if (ret < 0)
		return ret;

	/* Manually set V bit end position in NTSC mode */
	return adv7180_write(state, ADV7180_REG_NTSC_V_BIT_END,
					ADV7180_NTSC_V_BIT_END_MANUAL_NVEND);
}

static int adv7180_set_std(struct adv7180_state *state, unsigned int std)
{
	return adv7180_write(state, ADV7180_REG_INPUT_CONTROL,
		(std << 4) | state->input);
}

static int adv7180_select_input(struct adv7180_state *state, unsigned int input)
{
	int ret;

	ret = adv7180_read(state, ADV7180_REG_INPUT_CONTROL);
	if (ret < 0)
		return ret;

	ret &= ~ADV7180_INPUT_CONTROL_INSEL_MASK;
	ret |= input;
	return adv7180_write(state, ADV7180_REG_INPUT_CONTROL, ret);
}

static int adv7182_init(struct adv7180_state *state)
{
	if (state->chip_info->flags & ADV7180_FLAG_MIPI_CSI2)
		adv7180_write(state, ADV7180_REG_CSI_SLAVE_ADDR,
			ADV7180_DEFAULT_CSI_I2C_ADDR << 1);

	if (state->chip_info->flags & ADV7180_FLAG_I2P)
		adv7180_write(state, ADV7180_REG_VPP_SLAVE_ADDR,
			ADV7180_DEFAULT_VPP_I2C_ADDR << 1);

	if (state->chip_info->flags & ADV7180_FLAG_V2) {
		/* ADI recommended writes for improved video quality */
		adv7180_write(state, 0x0080, 0x51);
		adv7180_write(state, 0x0081, 0x51);
		adv7180_write(state, 0x0082, 0x68);
	}

	/* ADI required writes */
	if (state->chip_info->flags & ADV7180_FLAG_MIPI_CSI2) {
		adv7180_write(state, ADV7180_REG_OUTPUT_CONTROL, 0x4e);
		adv7180_write(state, ADV7180_REG_EXTENDED_OUTPUT_CONTROL, 0x57);
		adv7180_write(state, ADV7180_REG_CTRL_2, 0xc0);
	} else {
		if (state->chip_info->flags & ADV7180_FLAG_V2)
			adv7180_write(state,
				      ADV7180_REG_EXTENDED_OUTPUT_CONTROL,
				      0x17);
		else
			adv7180_write(state,
				      ADV7180_REG_EXTENDED_OUTPUT_CONTROL,
				      0x07);
		adv7180_write(state, ADV7180_REG_OUTPUT_CONTROL, 0x0c);
		adv7180_write(state, ADV7180_REG_CTRL_2, 0x40);
	}

	adv7180_write(state, 0x0013, 0x00);

	return 0;
}

static int adv7182_set_std(struct adv7180_state *state, unsigned int std)
{
	return adv7180_write(state, ADV7182_REG_INPUT_VIDSEL, std << 4);
}

enum adv7182_input_type {
	ADV7182_INPUT_TYPE_CVBS,
	ADV7182_INPUT_TYPE_DIFF_CVBS,
	ADV7182_INPUT_TYPE_SVIDEO,
	ADV7182_INPUT_TYPE_YPBPR,
};

static enum adv7182_input_type adv7182_get_input_type(unsigned int input)
{
	switch (input) {
	case ADV7182_INPUT_CVBS_AIN1:
	case ADV7182_INPUT_CVBS_AIN2:
	case ADV7182_INPUT_CVBS_AIN3:
	case ADV7182_INPUT_CVBS_AIN4:
	case ADV7182_INPUT_CVBS_AIN5:
	case ADV7182_INPUT_CVBS_AIN6:
	case ADV7182_INPUT_CVBS_AIN7:
	case ADV7182_INPUT_CVBS_AIN8:
		return ADV7182_INPUT_TYPE_CVBS;
	case ADV7182_INPUT_SVIDEO_AIN1_AIN2:
	case ADV7182_INPUT_SVIDEO_AIN3_AIN4:
	case ADV7182_INPUT_SVIDEO_AIN5_AIN6:
	case ADV7182_INPUT_SVIDEO_AIN7_AIN8:
		return ADV7182_INPUT_TYPE_SVIDEO;
	case ADV7182_INPUT_YPRPB_AIN1_AIN2_AIN3:
	case ADV7182_INPUT_YPRPB_AIN4_AIN5_AIN6:
		return ADV7182_INPUT_TYPE_YPBPR;
	case ADV7182_INPUT_DIFF_CVBS_AIN1_AIN2:
	case ADV7182_INPUT_DIFF_CVBS_AIN3_AIN4:
	case ADV7182_INPUT_DIFF_CVBS_AIN5_AIN6:
	case ADV7182_INPUT_DIFF_CVBS_AIN7_AIN8:
		return ADV7182_INPUT_TYPE_DIFF_CVBS;
	default: /* Will never happen */
		return 0;
	}
}

/* ADI recommended writes to registers 0x52, 0x53, 0x54 */
static unsigned int adv7182_lbias_settings[][3] = {
	[ADV7182_INPUT_TYPE_CVBS] = { 0xCB, 0x4E, 0x80 },
	[ADV7182_INPUT_TYPE_DIFF_CVBS] = { 0xC0, 0x4E, 0x80 },
	[ADV7182_INPUT_TYPE_SVIDEO] = { 0x0B, 0xCE, 0x80 },
	[ADV7182_INPUT_TYPE_YPBPR] = { 0x0B, 0x4E, 0xC0 },
};

static unsigned int adv7280_lbias_settings[][3] = {
	[ADV7182_INPUT_TYPE_CVBS] = { 0xCD, 0x4E, 0x80 },
	[ADV7182_INPUT_TYPE_DIFF_CVBS] = { 0xC0, 0x4E, 0x80 },
	[ADV7182_INPUT_TYPE_SVIDEO] = { 0x0B, 0xCE, 0x80 },
	[ADV7182_INPUT_TYPE_YPBPR] = { 0x0B, 0x4E, 0xC0 },
};

static int adv7182_select_input(struct adv7180_state *state, unsigned int input)
{
	enum adv7182_input_type input_type;
	unsigned int *lbias;
	unsigned int i;
	int ret;

	ret = adv7180_write(state, ADV7180_REG_INPUT_CONTROL, input);
	if (ret)
		return ret;

	/* Reset clamp circuitry - ADI recommended writes */
	adv7180_write(state, ADV7180_REG_RST_CLAMP, 0x00);
	adv7180_write(state, ADV7180_REG_RST_CLAMP, 0xff);

	input_type = adv7182_get_input_type(input);

	switch (input_type) {
	case ADV7182_INPUT_TYPE_CVBS:
	case ADV7182_INPUT_TYPE_DIFF_CVBS:
		/* ADI recommends to use the SH1 filter */
		adv7180_write(state, ADV7180_REG_SHAP_FILTER_CTL_1, 0x41);
		break;
	default:
		adv7180_write(state, ADV7180_REG_SHAP_FILTER_CTL_1, 0x01);
		break;
	}

	if (state->chip_info->flags & ADV7180_FLAG_V2)
		lbias = adv7280_lbias_settings[input_type];
	else
		lbias = adv7182_lbias_settings[input_type];

	for (i = 0; i < ARRAY_SIZE(adv7182_lbias_settings[0]); i++)
		adv7180_write(state, ADV7180_REG_CVBS_TRIM + i, lbias[i]);

	if (input_type == ADV7182_INPUT_TYPE_DIFF_CVBS) {
		/* ADI required writes to make differential CVBS work */
		adv7180_write(state, ADV7180_REG_RES_CIR, 0xa8);
		adv7180_write(state, ADV7180_REG_CLAMP_ADJ, 0x90);
		adv7180_write(state, ADV7180_REG_DIFF_MODE, 0xb0);
		adv7180_write(state, ADV7180_REG_AGC_ADJ1, 0x08);
		adv7180_write(state, ADV7180_REG_AGC_ADJ2, 0xa0);
	} else {
		adv7180_write(state, ADV7180_REG_RES_CIR, 0xf0);
		adv7180_write(state, ADV7180_REG_CLAMP_ADJ, 0xd0);
		adv7180_write(state, ADV7180_REG_DIFF_MODE, 0x10);
		adv7180_write(state, ADV7180_REG_AGC_ADJ1, 0x9c);
		adv7180_write(state, ADV7180_REG_AGC_ADJ2, 0x00);
	}

	return 0;
}

static const struct adv7180_chip_info adv7180_info = {
	.flags = ADV7180_FLAG_RESET_POWERED,
	/* We cannot discriminate between LQFP and 40-pin LFCSP, so accept
	 * all inputs and let the card driver take care of validation
	 */
	.valid_input_mask = BIT(ADV7180_INPUT_CVBS_AIN1) |
		BIT(ADV7180_INPUT_CVBS_AIN2) |
		BIT(ADV7180_INPUT_CVBS_AIN3) |
		BIT(ADV7180_INPUT_CVBS_AIN4) |
		BIT(ADV7180_INPUT_CVBS_AIN5) |
		BIT(ADV7180_INPUT_CVBS_AIN6) |
		BIT(ADV7180_INPUT_SVIDEO_AIN1_AIN2) |
		BIT(ADV7180_INPUT_SVIDEO_AIN3_AIN4) |
		BIT(ADV7180_INPUT_SVIDEO_AIN5_AIN6) |
		BIT(ADV7180_INPUT_YPRPB_AIN1_AIN2_AIN3) |
		BIT(ADV7180_INPUT_YPRPB_AIN4_AIN5_AIN6),
	.init = adv7180_init,
	.set_std = adv7180_set_std,
	.select_input = adv7180_select_input,
};

static const struct adv7180_chip_info adv7182_info = {
	.valid_input_mask = BIT(ADV7182_INPUT_CVBS_AIN1) |
		BIT(ADV7182_INPUT_CVBS_AIN2) |
		BIT(ADV7182_INPUT_CVBS_AIN3) |
		BIT(ADV7182_INPUT_CVBS_AIN4) |
		BIT(ADV7182_INPUT_SVIDEO_AIN1_AIN2) |
		BIT(ADV7182_INPUT_SVIDEO_AIN3_AIN4) |
		BIT(ADV7182_INPUT_YPRPB_AIN1_AIN2_AIN3) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN1_AIN2) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN3_AIN4),
	.init = adv7182_init,
	.set_std = adv7182_set_std,
	.select_input = adv7182_select_input,
};

static const struct adv7180_chip_info adv7280_info = {
	.flags = ADV7180_FLAG_V2 | ADV7180_FLAG_I2P,
	.valid_input_mask = BIT(ADV7182_INPUT_CVBS_AIN1) |
		BIT(ADV7182_INPUT_CVBS_AIN2) |
		BIT(ADV7182_INPUT_CVBS_AIN3) |
		BIT(ADV7182_INPUT_CVBS_AIN4) |
		BIT(ADV7182_INPUT_SVIDEO_AIN1_AIN2) |
		BIT(ADV7182_INPUT_SVIDEO_AIN3_AIN4) |
		BIT(ADV7182_INPUT_YPRPB_AIN1_AIN2_AIN3),
	.init = adv7182_init,
	.set_std = adv7182_set_std,
	.select_input = adv7182_select_input,
};

static const struct adv7180_chip_info adv7280_m_info = {
	.flags = ADV7180_FLAG_V2 | ADV7180_FLAG_MIPI_CSI2 | ADV7180_FLAG_I2P,
	.valid_input_mask = BIT(ADV7182_INPUT_CVBS_AIN1) |
		BIT(ADV7182_INPUT_CVBS_AIN2) |
		BIT(ADV7182_INPUT_CVBS_AIN3) |
		BIT(ADV7182_INPUT_CVBS_AIN4) |
		BIT(ADV7182_INPUT_CVBS_AIN5) |
		BIT(ADV7182_INPUT_CVBS_AIN6) |
		BIT(ADV7182_INPUT_CVBS_AIN7) |
		BIT(ADV7182_INPUT_CVBS_AIN8) |
		BIT(ADV7182_INPUT_SVIDEO_AIN1_AIN2) |
		BIT(ADV7182_INPUT_SVIDEO_AIN3_AIN4) |
		BIT(ADV7182_INPUT_SVIDEO_AIN5_AIN6) |
		BIT(ADV7182_INPUT_SVIDEO_AIN7_AIN8) |
		BIT(ADV7182_INPUT_YPRPB_AIN1_AIN2_AIN3) |
		BIT(ADV7182_INPUT_YPRPB_AIN4_AIN5_AIN6),
	.init = adv7182_init,
	.set_std = adv7182_set_std,
	.select_input = adv7182_select_input,
};

static const struct adv7180_chip_info adv7281_info = {
	.flags = ADV7180_FLAG_V2 | ADV7180_FLAG_MIPI_CSI2,
	.valid_input_mask = BIT(ADV7182_INPUT_CVBS_AIN1) |
		BIT(ADV7182_INPUT_CVBS_AIN2) |
		BIT(ADV7182_INPUT_CVBS_AIN7) |
		BIT(ADV7182_INPUT_CVBS_AIN8) |
		BIT(ADV7182_INPUT_SVIDEO_AIN1_AIN2) |
		BIT(ADV7182_INPUT_SVIDEO_AIN7_AIN8) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN1_AIN2) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN7_AIN8),
	.init = adv7182_init,
	.set_std = adv7182_set_std,
	.select_input = adv7182_select_input,
};

static const struct adv7180_chip_info adv7281_m_info = {
	.flags = ADV7180_FLAG_V2 | ADV7180_FLAG_MIPI_CSI2,
	.valid_input_mask = BIT(ADV7182_INPUT_CVBS_AIN1) |
		BIT(ADV7182_INPUT_CVBS_AIN2) |
		BIT(ADV7182_INPUT_CVBS_AIN3) |
		BIT(ADV7182_INPUT_CVBS_AIN4) |
		BIT(ADV7182_INPUT_CVBS_AIN7) |
		BIT(ADV7182_INPUT_CVBS_AIN8) |
		BIT(ADV7182_INPUT_SVIDEO_AIN1_AIN2) |
		BIT(ADV7182_INPUT_SVIDEO_AIN3_AIN4) |
		BIT(ADV7182_INPUT_SVIDEO_AIN7_AIN8) |
		BIT(ADV7182_INPUT_YPRPB_AIN1_AIN2_AIN3) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN1_AIN2) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN3_AIN4) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN7_AIN8),
	.init = adv7182_init,
	.set_std = adv7182_set_std,
	.select_input = adv7182_select_input,
};

static const struct adv7180_chip_info adv7281_ma_info = {
	.flags = ADV7180_FLAG_V2 | ADV7180_FLAG_MIPI_CSI2,
	.valid_input_mask = BIT(ADV7182_INPUT_CVBS_AIN1) |
		BIT(ADV7182_INPUT_CVBS_AIN2) |
		BIT(ADV7182_INPUT_CVBS_AIN3) |
		BIT(ADV7182_INPUT_CVBS_AIN4) |
		BIT(ADV7182_INPUT_CVBS_AIN5) |
		BIT(ADV7182_INPUT_CVBS_AIN6) |
		BIT(ADV7182_INPUT_CVBS_AIN7) |
		BIT(ADV7182_INPUT_CVBS_AIN8) |
		BIT(ADV7182_INPUT_SVIDEO_AIN1_AIN2) |
		BIT(ADV7182_INPUT_SVIDEO_AIN3_AIN4) |
		BIT(ADV7182_INPUT_SVIDEO_AIN5_AIN6) |
		BIT(ADV7182_INPUT_SVIDEO_AIN7_AIN8) |
		BIT(ADV7182_INPUT_YPRPB_AIN1_AIN2_AIN3) |
		BIT(ADV7182_INPUT_YPRPB_AIN4_AIN5_AIN6) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN1_AIN2) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN3_AIN4) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN5_AIN6) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN7_AIN8),
	.init = adv7182_init,
	.set_std = adv7182_set_std,
	.select_input = adv7182_select_input,
};

static const struct adv7180_chip_info adv7282_info = {
	.flags = ADV7180_FLAG_V2 | ADV7180_FLAG_I2P,
	.valid_input_mask = BIT(ADV7182_INPUT_CVBS_AIN1) |
		BIT(ADV7182_INPUT_CVBS_AIN2) |
		BIT(ADV7182_INPUT_CVBS_AIN7) |
		BIT(ADV7182_INPUT_CVBS_AIN8) |
		BIT(ADV7182_INPUT_SVIDEO_AIN1_AIN2) |
		BIT(ADV7182_INPUT_SVIDEO_AIN7_AIN8) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN1_AIN2) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN7_AIN8),
	.init = adv7182_init,
	.set_std = adv7182_set_std,
	.select_input = adv7182_select_input,
};

static const struct adv7180_chip_info adv7282_m_info = {
	.flags = ADV7180_FLAG_V2 | ADV7180_FLAG_MIPI_CSI2 | ADV7180_FLAG_I2P,
	.valid_input_mask = BIT(ADV7182_INPUT_CVBS_AIN1) |
		BIT(ADV7182_INPUT_CVBS_AIN2) |
		BIT(ADV7182_INPUT_CVBS_AIN3) |
		BIT(ADV7182_INPUT_CVBS_AIN4) |
		BIT(ADV7182_INPUT_CVBS_AIN7) |
		BIT(ADV7182_INPUT_CVBS_AIN8) |
		BIT(ADV7182_INPUT_SVIDEO_AIN1_AIN2) |
		BIT(ADV7182_INPUT_SVIDEO_AIN3_AIN4) |
		BIT(ADV7182_INPUT_SVIDEO_AIN7_AIN8) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN1_AIN2) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN3_AIN4) |
		BIT(ADV7182_INPUT_DIFF_CVBS_AIN7_AIN8),
	.init = adv7182_init,
	.set_std = adv7182_set_std,
	.select_input = adv7182_select_input,
};

static int init_device(struct adv7180_state *state)
{
	int ret;

	mutex_lock(&state->mutex);

	adv7180_set_power_pin(state, true);

	adv7180_write(state, ADV7180_REG_PWR_MAN, ADV7180_PWR_MAN_RES);
	usleep_range(5000, 10000);

	ret = state->chip_info->init(state);
	if (ret)
		goto out_unlock;

	ret = adv7180_program_std(state);
	if (ret)
		goto out_unlock;

	adv7180_set_field_mode(state);

	/* register for interrupts */
	if (state->irq > 0) {
		/* config the Interrupt pin to be active low */
		ret = adv7180_write(state, ADV7180_REG_ICONF1,
						ADV7180_ICONF1_ACTIVE_LOW |
						ADV7180_ICONF1_PSYNC_ONLY);
		if (ret < 0)
			goto out_unlock;

		ret = adv7180_write(state, ADV7180_REG_IMR1, 0);
		if (ret < 0)
			goto out_unlock;

		ret = adv7180_write(state, ADV7180_REG_IMR2, 0);
		if (ret < 0)
			goto out_unlock;

		/* enable AD change interrupts interrupts */
		ret = adv7180_write(state, ADV7180_REG_IMR3,
						ADV7180_IRQ3_AD_CHANGE);
		if (ret < 0)
			goto out_unlock;

		ret = adv7180_write(state, ADV7180_REG_IMR4, 0);
		if (ret < 0)
			goto out_unlock;
	}

out_unlock:
	mutex_unlock(&state->mutex);

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
	if (state == NULL)
		return -ENOMEM;

	state->client = client;
	state->field = V4L2_FIELD_INTERLACED;
	state->chip_info = (struct adv7180_chip_info *)id->driver_data;

	state->pwdn_gpio = devm_gpiod_get_optional(&client->dev, "powerdown",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(state->pwdn_gpio)) {
		ret = PTR_ERR(state->pwdn_gpio);
		v4l_err(client, "request for power pin failed: %d\n", ret);
		return ret;
	}

	if (state->chip_info->flags & ADV7180_FLAG_MIPI_CSI2) {
		state->csi_client = i2c_new_dummy(client->adapter,
				ADV7180_DEFAULT_CSI_I2C_ADDR);
		if (!state->csi_client)
			return -ENOMEM;
	}

	if (state->chip_info->flags & ADV7180_FLAG_I2P) {
		state->vpp_client = i2c_new_dummy(client->adapter,
				ADV7180_DEFAULT_VPP_I2C_ADDR);
		if (!state->vpp_client) {
			ret = -ENOMEM;
			goto err_unregister_csi_client;
		}
	}

	state->irq = client->irq;
	mutex_init(&state->mutex);
	state->curr_norm = V4L2_STD_NTSC;
	if (state->chip_info->flags & ADV7180_FLAG_RESET_POWERED)
		state->powered = true;
	else
		state->powered = false;
	state->input = 0;
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &adv7180_ops);
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	ret = adv7180_init_controls(state);
	if (ret)
		goto err_unregister_vpp_client;

	state->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.flags |= MEDIA_ENT_F_ATV_DECODER;
	ret = media_entity_pads_init(&sd->entity, 1, &state->pad);
	if (ret)
		goto err_free_ctrl;

	ret = init_device(state);
	if (ret)
		goto err_media_entity_cleanup;

	if (state->irq) {
		ret = request_threaded_irq(client->irq, NULL, adv7180_irq,
					   IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
					   KBUILD_MODNAME, state);
		if (ret)
			goto err_media_entity_cleanup;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret)
		goto err_free_irq;

	return 0;

err_free_irq:
	if (state->irq > 0)
		free_irq(client->irq, state);
err_media_entity_cleanup:
	media_entity_cleanup(&sd->entity);
err_free_ctrl:
	adv7180_exit_controls(state);
err_unregister_vpp_client:
	if (state->chip_info->flags & ADV7180_FLAG_I2P)
		i2c_unregister_device(state->vpp_client);
err_unregister_csi_client:
	if (state->chip_info->flags & ADV7180_FLAG_MIPI_CSI2)
		i2c_unregister_device(state->csi_client);
	mutex_destroy(&state->mutex);
	return ret;
}

static int adv7180_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7180_state *state = to_state(sd);

	v4l2_async_unregister_subdev(sd);

	if (state->irq > 0)
		free_irq(client->irq, state);

	media_entity_cleanup(&sd->entity);
	adv7180_exit_controls(state);

	if (state->chip_info->flags & ADV7180_FLAG_I2P)
		i2c_unregister_device(state->vpp_client);
	if (state->chip_info->flags & ADV7180_FLAG_MIPI_CSI2)
		i2c_unregister_device(state->csi_client);

	adv7180_set_power_pin(state, false);

	mutex_destroy(&state->mutex);

	return 0;
}

static const struct i2c_device_id adv7180_id[] = {
	{ "adv7180", (kernel_ulong_t)&adv7180_info },
	{ "adv7182", (kernel_ulong_t)&adv7182_info },
	{ "adv7280", (kernel_ulong_t)&adv7280_info },
	{ "adv7280-m", (kernel_ulong_t)&adv7280_m_info },
	{ "adv7281", (kernel_ulong_t)&adv7281_info },
	{ "adv7281-m", (kernel_ulong_t)&adv7281_m_info },
	{ "adv7281-ma", (kernel_ulong_t)&adv7281_ma_info },
	{ "adv7282", (kernel_ulong_t)&adv7282_info },
	{ "adv7282-m", (kernel_ulong_t)&adv7282_m_info },
	{},
};
MODULE_DEVICE_TABLE(i2c, adv7180_id);

#ifdef CONFIG_PM_SLEEP
static int adv7180_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7180_state *state = to_state(sd);

	return adv7180_set_power(state, false);
}

static int adv7180_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7180_state *state = to_state(sd);
	int ret;

	ret = init_device(state);
	if (ret < 0)
		return ret;

	ret = adv7180_set_power(state, state->powered);
	if (ret)
		return ret;

	return 0;
}

static SIMPLE_DEV_PM_OPS(adv7180_pm_ops, adv7180_suspend, adv7180_resume);
#define ADV7180_PM_OPS (&adv7180_pm_ops)

#else
#define ADV7180_PM_OPS NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id adv7180_of_id[] = {
	{ .compatible = "adi,adv7180", },
	{ .compatible = "adi,adv7182", },
	{ .compatible = "adi,adv7280", },
	{ .compatible = "adi,adv7280-m", },
	{ .compatible = "adi,adv7281", },
	{ .compatible = "adi,adv7281-m", },
	{ .compatible = "adi,adv7281-ma", },
	{ .compatible = "adi,adv7282", },
	{ .compatible = "adi,adv7282-m", },
	{ },
};

MODULE_DEVICE_TABLE(of, adv7180_of_id);
#endif

static struct i2c_driver adv7180_driver = {
	.driver = {
		   .name = KBUILD_MODNAME,
		   .pm = ADV7180_PM_OPS,
		   .of_match_table = of_match_ptr(adv7180_of_id),
		   },
	.probe = adv7180_probe,
	.remove = adv7180_remove,
	.id_table = adv7180_id,
};

module_i2c_driver(adv7180_driver);

MODULE_DESCRIPTION("Analog Devices ADV7180 video decoder driver");
MODULE_AUTHOR("Mocean Laboratories");
MODULE_LICENSE("GPL v2");
