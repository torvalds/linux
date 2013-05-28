/*
    gpio functions.
    Merging GPIO support into driver:
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ivtv-driver.h"
#include "ivtv-cards.h"
#include "ivtv-gpio.h"
#include "tuner-xc2028.h"
#include <media/tuner.h>
#include <media/v4l2-ctrls.h>

/*
 * GPIO assignment of Yuan MPG600/MPG160
 *
 *    bit 15  14  13  12 |  11  10   9   8 |   7   6   5   4 |   3   2   1   0
 * OUTPUT         IN1 IN0                                       AM3 AM2 AM1 AM0
 *  INPUT                   DM1         DM0
 *
 *   IN* : Input selection
 *          IN1 IN0
 *           1   1  N/A
 *           1   0  Line
 *           0   1  N/A
 *           0   0  Tuner
 *
 *   AM* : Audio Mode
 *          AM3  0: Normal        1: Mixed(Sub+Main channel)
 *          AM2  0: Subchannel    1: Main channel
 *          AM1  0: Stereo        1: Mono
 *          AM0  0: Normal        1: Mute
 *
 *   DM* : Detected tuner audio Mode
 *          DM1  0: Stereo        1: Mono
 *          DM0  0: Multiplex     1: Normal
 *
 * GPIO Initial Settings
 *           MPG600   MPG160
 *     DIR   0x3080   0x7080
 *  OUTPUT   0x000C   0x400C
 *
 *  Special thanks to Makoto Iguchi <iguchi@tahoo.org> and Mr. Anonymous
 *  for analyzing GPIO of MPG160.
 *
 *****************************************************************************
 *
 * GPIO assignment of Avermedia M179 (per information direct from AVerMedia)
 *
 *    bit 15  14  13  12 |  11  10   9   8 |   7   6   5   4 |   3   2   1   0
 * OUTPUT IN0 AM0 IN1               AM1 AM2       IN2     BR0   BR1
 *  INPUT
 *
 *   IN* : Input selection
 *          IN0 IN1 IN2
 *           *   1   *  Mute
 *           0   0   0  Line-In
 *           1   0   0  TV Tuner Audio
 *           0   0   1  FM Audio
 *           1   0   1  Mute
 *
 *   AM* : Audio Mode
 *          AM0 AM1 AM2
 *           0   0   0  TV Tuner Audio: L_OUT=(L+R)/2, R_OUT=SAP
 *           0   0   1  TV Tuner Audio: L_OUT=R_OUT=SAP   (SAP)
 *           0   1   0  TV Tuner Audio: L_OUT=L, R_OUT=R   (stereo)
 *           0   1   1  TV Tuner Audio: mute
 *           1   *   *  TV Tuner Audio: L_OUT=R_OUT=(L+R)/2   (mono)
 *
 *   BR* : Audio Sample Rate (BR stands for bitrate for some reason)
 *          BR0 BR1
 *           0   0   32 kHz
 *           0   1   44.1 kHz
 *           1   0   48 kHz
 *
 *   DM* : Detected tuner audio Mode
 *         Unknown currently
 *
 * Special thanks to AVerMedia Technologies, Inc. and Jiun-Kuei Jung at
 * AVerMedia for providing the GPIO information used to add support
 * for the M179 cards.
 */

/********************* GPIO stuffs *********************/

/* GPIO registers */
#define IVTV_REG_GPIO_IN    0x9008
#define IVTV_REG_GPIO_OUT   0x900c
#define IVTV_REG_GPIO_DIR   0x9020

void ivtv_reset_ir_gpio(struct ivtv *itv)
{
	int curdir, curout;

	if (itv->card->type != IVTV_CARD_PVR_150)
		return;
	IVTV_DEBUG_INFO("Resetting PVR150 IR\n");
	curout = read_reg(IVTV_REG_GPIO_OUT);
	curdir = read_reg(IVTV_REG_GPIO_DIR);
	curdir |= 0x80;
	write_reg(curdir, IVTV_REG_GPIO_DIR);
	curout = (curout & ~0xF) | 1;
	write_reg(curout, IVTV_REG_GPIO_OUT);
	/* We could use something else for smaller time */
	schedule_timeout_interruptible(msecs_to_jiffies(1));
	curout |= 2;
	write_reg(curout, IVTV_REG_GPIO_OUT);
	curdir &= ~0x80;
	write_reg(curdir, IVTV_REG_GPIO_DIR);
}

/* Xceive tuner reset function */
int ivtv_reset_tuner_gpio(void *dev, int component, int cmd, int value)
{
	struct i2c_algo_bit_data *algo = dev;
	struct ivtv *itv = algo->data;
	u32 curout;

	if (cmd != XC2028_TUNER_RESET)
		return 0;
	IVTV_DEBUG_INFO("Resetting tuner\n");
	curout = read_reg(IVTV_REG_GPIO_OUT);
	curout &= ~(1 << itv->card->xceive_pin);
	write_reg(curout, IVTV_REG_GPIO_OUT);
	schedule_timeout_interruptible(msecs_to_jiffies(1));

	curout |= 1 << itv->card->xceive_pin;
	write_reg(curout, IVTV_REG_GPIO_OUT);
	schedule_timeout_interruptible(msecs_to_jiffies(1));
	return 0;
}

static inline struct ivtv *sd_to_ivtv(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ivtv, sd_gpio);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ivtv, hdl_gpio)->sd_gpio;
}

static int subdev_s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	struct ivtv *itv = sd_to_ivtv(sd);
	u16 mask, data;

	mask = itv->card->gpio_audio_freq.mask;
	switch (freq) {
	case 32000:
		data = itv->card->gpio_audio_freq.f32000;
		break;
	case 44100:
		data = itv->card->gpio_audio_freq.f44100;
		break;
	case 48000:
	default:
		data = itv->card->gpio_audio_freq.f48000;
		break;
	}
	if (mask)
		write_reg((read_reg(IVTV_REG_GPIO_OUT) & ~mask) | (data & mask), IVTV_REG_GPIO_OUT);
	return 0;
}

static int subdev_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	struct ivtv *itv = sd_to_ivtv(sd);
	u16 mask;

	mask = itv->card->gpio_audio_detect.mask;
	if (mask == 0 || (read_reg(IVTV_REG_GPIO_IN) & mask))
		vt->rxsubchans = V4L2_TUNER_SUB_STEREO |
			V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
	else
		vt->rxsubchans = V4L2_TUNER_SUB_MONO;
	return 0;
}

static int subdev_s_tuner(struct v4l2_subdev *sd, const struct v4l2_tuner *vt)
{
	struct ivtv *itv = sd_to_ivtv(sd);
	u16 mask, data;

	mask = itv->card->gpio_audio_mode.mask;
	switch (vt->audmode) {
	case V4L2_TUNER_MODE_LANG1:
		data = itv->card->gpio_audio_mode.lang1;
		break;
	case V4L2_TUNER_MODE_LANG2:
		data = itv->card->gpio_audio_mode.lang2;
		break;
	case V4L2_TUNER_MODE_MONO:
		data = itv->card->gpio_audio_mode.mono;
		break;
	case V4L2_TUNER_MODE_STEREO:
	case V4L2_TUNER_MODE_LANG1_LANG2:
	default:
		data = itv->card->gpio_audio_mode.stereo;
		break;
	}
	if (mask)
		write_reg((read_reg(IVTV_REG_GPIO_OUT) & ~mask) | (data & mask), IVTV_REG_GPIO_OUT);
	return 0;
}

static int subdev_s_radio(struct v4l2_subdev *sd)
{
	struct ivtv *itv = sd_to_ivtv(sd);
	u16 mask, data;

	mask = itv->card->gpio_audio_input.mask;
	data = itv->card->gpio_audio_input.radio;
	if (mask)
		write_reg((read_reg(IVTV_REG_GPIO_OUT) & ~mask) | (data & mask), IVTV_REG_GPIO_OUT);
	return 0;
}

static int subdev_s_audio_routing(struct v4l2_subdev *sd,
				  u32 input, u32 output, u32 config)
{
	struct ivtv *itv = sd_to_ivtv(sd);
	u16 mask, data;

	if (input > 2)
		return -EINVAL;
	mask = itv->card->gpio_audio_input.mask;
	switch (input) {
	case 0:
		data = itv->card->gpio_audio_input.tuner;
		break;
	case 1:
		data = itv->card->gpio_audio_input.linein;
		break;
	case 2:
	default:
		data = itv->card->gpio_audio_input.radio;
		break;
	}
	if (mask)
		write_reg((read_reg(IVTV_REG_GPIO_OUT) & ~mask) | (data & mask), IVTV_REG_GPIO_OUT);
	return 0;
}

static int subdev_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct ivtv *itv = sd_to_ivtv(sd);
	u16 mask, data;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		mask = itv->card->gpio_audio_mute.mask;
		data = ctrl->val ? itv->card->gpio_audio_mute.mute : 0;
		if (mask)
			write_reg((read_reg(IVTV_REG_GPIO_OUT) & ~mask) |
					(data & mask), IVTV_REG_GPIO_OUT);
		return 0;
	}
	return -EINVAL;
}


static int subdev_log_status(struct v4l2_subdev *sd)
{
	struct ivtv *itv = sd_to_ivtv(sd);

	IVTV_INFO("GPIO status: DIR=0x%04x OUT=0x%04x IN=0x%04x\n",
			read_reg(IVTV_REG_GPIO_DIR), read_reg(IVTV_REG_GPIO_OUT),
			read_reg(IVTV_REG_GPIO_IN));
	v4l2_ctrl_handler_log_status(&itv->hdl_gpio, sd->name);
	return 0;
}

static int subdev_s_video_routing(struct v4l2_subdev *sd,
				  u32 input, u32 output, u32 config)
{
	struct ivtv *itv = sd_to_ivtv(sd);
	u16 mask, data;

	if (input > 2) /* 0:Tuner 1:Composite 2:S-Video */
		return -EINVAL;
	mask = itv->card->gpio_video_input.mask;
	if (input == 0)
		data = itv->card->gpio_video_input.tuner;
	else if (input == 1)
		data = itv->card->gpio_video_input.composite;
	else
		data = itv->card->gpio_video_input.svideo;
	if (mask)
		write_reg((read_reg(IVTV_REG_GPIO_OUT) & ~mask) | (data & mask), IVTV_REG_GPIO_OUT);
	return 0;
}

static const struct v4l2_ctrl_ops gpio_ctrl_ops = {
	.s_ctrl = subdev_s_ctrl,
};

static const struct v4l2_subdev_core_ops subdev_core_ops = {
	.log_status = subdev_log_status,
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
};

static const struct v4l2_subdev_tuner_ops subdev_tuner_ops = {
	.s_radio = subdev_s_radio,
	.g_tuner = subdev_g_tuner,
	.s_tuner = subdev_s_tuner,
};

static const struct v4l2_subdev_audio_ops subdev_audio_ops = {
	.s_clock_freq = subdev_s_clock_freq,
	.s_routing = subdev_s_audio_routing,
};

static const struct v4l2_subdev_video_ops subdev_video_ops = {
	.s_routing = subdev_s_video_routing,
};

static const struct v4l2_subdev_ops subdev_ops = {
	.core = &subdev_core_ops,
	.tuner = &subdev_tuner_ops,
	.audio = &subdev_audio_ops,
	.video = &subdev_video_ops,
};

int ivtv_gpio_init(struct ivtv *itv)
{
	u16 pin = 0;

	if (itv->card->xceive_pin)
		pin = 1 << itv->card->xceive_pin;

	if ((itv->card->gpio_init.direction | pin) == 0)
		return 0;

	IVTV_DEBUG_INFO("GPIO initial dir: %08x out: %08x\n",
		   read_reg(IVTV_REG_GPIO_DIR), read_reg(IVTV_REG_GPIO_OUT));

	/* init output data then direction */
	write_reg(itv->card->gpio_init.initial_value | pin, IVTV_REG_GPIO_OUT);
	write_reg(itv->card->gpio_init.direction | pin, IVTV_REG_GPIO_DIR);
	v4l2_subdev_init(&itv->sd_gpio, &subdev_ops);
	snprintf(itv->sd_gpio.name, sizeof(itv->sd_gpio.name), "%s-gpio", itv->v4l2_dev.name);
	itv->sd_gpio.grp_id = IVTV_HW_GPIO;
	v4l2_ctrl_handler_init(&itv->hdl_gpio, 1);
	v4l2_ctrl_new_std(&itv->hdl_gpio, &gpio_ctrl_ops,
			V4L2_CID_AUDIO_MUTE, 0, 1, 1, 0);
	if (itv->hdl_gpio.error)
		return itv->hdl_gpio.error;
	itv->sd_gpio.ctrl_handler = &itv->hdl_gpio;
	v4l2_ctrl_handler_setup(&itv->hdl_gpio);
	return v4l2_device_register_subdev(&itv->v4l2_dev, &itv->sd_gpio);
}
