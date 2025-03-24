// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * wm8775 - driver version 0.0.1
 *
 * Copyright (C) 2004 Ulf Eklund <ivtv at eklund.to>
 *
 * Based on saa7115 driver
 *
 * Copyright (C) 2005 Hans Verkuil <hverkuil@xs4all.nl>
 * - Cleanup
 * - V4L2 API update
 * - sound fixes
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/i2c/wm8775.h>

MODULE_DESCRIPTION("wm8775 driver");
MODULE_AUTHOR("Ulf Eklund, Hans Verkuil");
MODULE_LICENSE("GPL");



/* ----------------------------------------------------------------------- */

enum {
	R7 = 7, R11 = 11,
	R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R23 = 23,
	TOT_REGS
};

#define ALC_HOLD 0x85 /* R17: use zero cross detection, ALC hold time 42.6 ms */
#define ALC_EN 0x100  /* R17: ALC enable */

struct wm8775_state {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *mute;
	struct v4l2_ctrl *vol;
	struct v4l2_ctrl *bal;
	struct v4l2_ctrl *loud;
	u8 input;		/* Last selected input (0-0xf) */
};

static inline struct wm8775_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct wm8775_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct wm8775_state, hdl)->sd;
}

static int wm8775_write(struct v4l2_subdev *sd, int reg, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i;

	if (reg < 0 || reg >= TOT_REGS) {
		v4l2_err(sd, "Invalid register R%d\n", reg);
		return -1;
	}

	for (i = 0; i < 3; i++)
		if (i2c_smbus_write_byte_data(client,
				(reg << 1) | (val >> 8), val & 0xff) == 0)
			return 0;
	v4l2_err(sd, "I2C: cannot write %03x to register R%d\n", val, reg);
	return -1;
}

static void wm8775_set_audio(struct v4l2_subdev *sd, int quietly)
{
	struct wm8775_state *state = to_state(sd);
	u8 vol_l, vol_r;
	int muted = 0 != state->mute->val;
	u16 volume = (u16)state->vol->val;
	u16 balance = (u16)state->bal->val;

	/* normalize ( 65535 to 0 -> 255 to 0 (+24dB to -103dB) ) */
	vol_l = (min(65536 - balance, 32768) * volume) >> 23;
	vol_r = (min(balance, (u16)32768) * volume) >> 23;

	/* Mute */
	if (muted || quietly)
		wm8775_write(sd, R21, 0x0c0 | state->input);

	wm8775_write(sd, R14, vol_l | 0x100); /* 0x100= Left channel ADC zero cross enable */
	wm8775_write(sd, R15, vol_r | 0x100); /* 0x100= Right channel ADC zero cross enable */

	/* Un-mute */
	if (!muted)
		wm8775_write(sd, R21, state->input);
}

static int wm8775_s_routing(struct v4l2_subdev *sd,
			    u32 input, u32 output, u32 config)
{
	struct wm8775_state *state = to_state(sd);

	/* There are 4 inputs and one output. Zero or more inputs
	   are multiplexed together to the output. Hence there are
	   16 combinations.
	   If only one input is active (the normal case) then the
	   input values 1, 2, 4 or 8 should be used. */
	if (input > 15) {
		v4l2_err(sd, "Invalid input %d.\n", input);
		return -EINVAL;
	}
	state->input = input;
	if (v4l2_ctrl_g_ctrl(state->mute))
		return 0;
	if (!v4l2_ctrl_g_ctrl(state->vol))
		return 0;
	wm8775_set_audio(sd, 1);
	return 0;
}

static int wm8775_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BALANCE:
		wm8775_set_audio(sd, 0);
		return 0;
	case V4L2_CID_AUDIO_LOUDNESS:
		wm8775_write(sd, R17, (ctrl->val ? ALC_EN : 0) | ALC_HOLD);
		return 0;
	}
	return -EINVAL;
}

static int wm8775_log_status(struct v4l2_subdev *sd)
{
	struct wm8775_state *state = to_state(sd);

	v4l2_info(sd, "Input: %d\n", state->input);
	v4l2_ctrl_handler_log_status(&state->hdl, sd->name);
	return 0;
}

static int wm8775_s_frequency(struct v4l2_subdev *sd, const struct v4l2_frequency *freq)
{
	wm8775_set_audio(sd, 0);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops wm8775_ctrl_ops = {
	.s_ctrl = wm8775_s_ctrl,
};

static const struct v4l2_subdev_core_ops wm8775_core_ops = {
	.log_status = wm8775_log_status,
};

static const struct v4l2_subdev_tuner_ops wm8775_tuner_ops = {
	.s_frequency = wm8775_s_frequency,
};

static const struct v4l2_subdev_audio_ops wm8775_audio_ops = {
	.s_routing = wm8775_s_routing,
};

static const struct v4l2_subdev_ops wm8775_ops = {
	.core = &wm8775_core_ops,
	.tuner = &wm8775_tuner_ops,
	.audio = &wm8775_audio_ops,
};

/* ----------------------------------------------------------------------- */

/* i2c implementation */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */

static int wm8775_probe(struct i2c_client *client)
{
	struct wm8775_state *state;
	struct v4l2_subdev *sd;
	int err;
	bool is_nova_s = false;

	if (client->dev.platform_data) {
		struct wm8775_platform_data *data = client->dev.platform_data;
		is_nova_s = data->is_nova_s;
	}

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &wm8775_ops);
	state->input = 2;

	v4l2_ctrl_handler_init(&state->hdl, 4);
	state->mute = v4l2_ctrl_new_std(&state->hdl, &wm8775_ctrl_ops,
			V4L2_CID_AUDIO_MUTE, 0, 1, 1, 0);
	state->vol = v4l2_ctrl_new_std(&state->hdl, &wm8775_ctrl_ops,
			V4L2_CID_AUDIO_VOLUME, 0, 65535, (65535+99)/100, 0xCF00); /* 0dB*/
	state->bal = v4l2_ctrl_new_std(&state->hdl, &wm8775_ctrl_ops,
			V4L2_CID_AUDIO_BALANCE, 0, 65535, (65535+99)/100, 32768);
	state->loud = v4l2_ctrl_new_std(&state->hdl, &wm8775_ctrl_ops,
			V4L2_CID_AUDIO_LOUDNESS, 0, 1, 1, 1);
	sd->ctrl_handler = &state->hdl;
	err = state->hdl.error;
	if (err) {
		v4l2_ctrl_handler_free(&state->hdl);
		return err;
	}

	/* Initialize wm8775 */

	/* RESET */
	wm8775_write(sd, R23, 0x000);
	/* Disable zero cross detect timeout */
	wm8775_write(sd, R7, 0x000);
	/* HPF enable, left justified, 24-bit (Philips) mode */
	wm8775_write(sd, R11, 0x021);
	/* Master mode, clock ratio 256fs */
	wm8775_write(sd, R12, 0x102);
	/* Powered up */
	wm8775_write(sd, R13, 0x000);

	if (!is_nova_s) {
		/* ADC gain +2.5dB, enable zero cross */
		wm8775_write(sd, R14, 0x1d4);
		/* ADC gain +2.5dB, enable zero cross */
		wm8775_write(sd, R15, 0x1d4);
		/* ALC Stereo, ALC target level -1dB FS max gain +8dB */
		wm8775_write(sd, R16, 0x1bf);
		/* Enable gain control, use zero cross detection,
		   ALC hold time 42.6 ms */
		wm8775_write(sd, R17, 0x185);
	} else {
		/* ALC stereo, ALC target level -5dB FS, ALC max gain +8dB */
		wm8775_write(sd, R16, 0x1bb);
		/* Set ALC mode and hold time */
		wm8775_write(sd, R17, (state->loud->val ? ALC_EN : 0) | ALC_HOLD);
	}
	/* ALC gain ramp up delay 34 s, ALC gain ramp down delay 33 ms */
	wm8775_write(sd, R18, 0x0a2);
	/* Enable noise gate, threshold -72dBfs */
	wm8775_write(sd, R19, 0x005);
	if (!is_nova_s) {
		/* Transient window 4ms, lower PGA gain limit -1dB */
		wm8775_write(sd, R20, 0x07a);
		/* LRBOTH = 1, use input 2. */
		wm8775_write(sd, R21, 0x102);
	} else {
		/* Transient window 4ms, ALC min gain -5dB  */
		wm8775_write(sd, R20, 0x0fb);

		wm8775_set_audio(sd, 1);      /* set volume/mute/mux */
	}
	return 0;
}

static void wm8775_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct wm8775_state *state = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&state->hdl);
}

static const struct i2c_device_id wm8775_id[] = {
	{ "wm8775" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8775_id);

static struct i2c_driver wm8775_driver = {
	.driver = {
		.name	= "wm8775",
	},
	.probe		= wm8775_probe,
	.remove		= wm8775_remove,
	.id_table	= wm8775_id,
};

module_i2c_driver(wm8775_driver);
