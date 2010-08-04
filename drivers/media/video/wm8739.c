/*
 * wm8739
 *
 * Copyright (C) 2005 T. Adachi <tadachi@tadachi-net.com>
 *
 * Copyright (C) 2005 Hans Verkuil <hverkuil@xs4all.nl>
 * - Cleanup
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-i2c-drv.h>

MODULE_DESCRIPTION("wm8739 driver");
MODULE_AUTHOR("T. Adachi, Hans Verkuil");
MODULE_LICENSE("GPL");

static int debug;

module_param(debug, int, 0644);

MODULE_PARM_DESC(debug, "Debug level (0-1)");


/* ------------------------------------------------------------------------ */

enum {
	R0 = 0, R1,
	R5 = 5, R6, R7, R8, R9, R15 = 15,
	TOT_REGS
};

struct wm8739_state {
	struct v4l2_subdev sd;
	u32 clock_freq;
	u8 muted;
	u16 volume;
	u16 balance;
	u8 vol_l; 		/* +12dB to -34.5dB 1.5dB step (5bit) def:0dB */
	u8 vol_r; 		/* +12dB to -34.5dB 1.5dB step (5bit) def:0dB */
};

static inline struct wm8739_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct wm8739_state, sd);
}

/* ------------------------------------------------------------------------ */

static int wm8739_write(struct v4l2_subdev *sd, int reg, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i;

	if (reg < 0 || reg >= TOT_REGS) {
		v4l2_err(sd, "Invalid register R%d\n", reg);
		return -1;
	}

	v4l2_dbg(1, debug, sd, "write: %02x %02x\n", reg, val);

	for (i = 0; i < 3; i++)
		if (i2c_smbus_write_byte_data(client,
				(reg << 1) | (val >> 8), val & 0xff) == 0)
			return 0;
	v4l2_err(sd, "I2C: cannot write %03x to register R%d\n", val, reg);
	return -1;
}

/* write regs to set audio volume etc */
static void wm8739_set_audio(struct v4l2_subdev *sd)
{
	struct wm8739_state *state = to_state(sd);
	u16 mute = state->muted ? 0x80 : 0;

	/* Volume setting: bits 0-4, 0x1f = 12 dB, 0x00 = -34.5 dB
	 * Default setting: 0x17 = 0 dB
	 */
	wm8739_write(sd, R0, (state->vol_l & 0x1f) | mute);
	wm8739_write(sd, R1, (state->vol_r & 0x1f) | mute);
}

static int wm8739_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct wm8739_state *state = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = state->muted;
		break;

	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = state->volume;
		break;

	case V4L2_CID_AUDIO_BALANCE:
		ctrl->value = state->balance;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int wm8739_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct wm8739_state *state = to_state(sd);
	unsigned int work_l, work_r;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		state->muted = ctrl->value;
		break;

	case V4L2_CID_AUDIO_VOLUME:
		state->volume = ctrl->value;
		break;

	case V4L2_CID_AUDIO_BALANCE:
		state->balance = ctrl->value;
		break;

	default:
		return -EINVAL;
	}

	/* normalize ( 65535 to 0 -> 31 to 0 (12dB to -34.5dB) ) */
	work_l = (min(65536 - state->balance, 32768) * state->volume) / 32768;
	work_r = (min(state->balance, (u16)32768) * state->volume) / 32768;

	state->vol_l = (long)work_l * 31 / 65535;
	state->vol_r = (long)work_r * 31 / 65535;

	/* set audio volume etc. */
	wm8739_set_audio(sd);
	return 0;
}

/* ------------------------------------------------------------------------ */

static struct v4l2_queryctrl wm8739_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 58880,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 1,
		.default_value = 1,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	}, {
		.id            = V4L2_CID_AUDIO_BALANCE,
		.name          = "Balance",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 32768,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}
};

/* ------------------------------------------------------------------------ */

static int wm8739_s_clock_freq(struct v4l2_subdev *sd, u32 audiofreq)
{
	struct wm8739_state *state = to_state(sd);

	state->clock_freq = audiofreq;
	/* de-activate */
	wm8739_write(sd, R9, 0x000);
	switch (audiofreq) {
	case 44100:
		/* 256fps, fs=44.1k */
		wm8739_write(sd, R8, 0x020);
		break;
	case 48000:
		/* 256fps, fs=48k */
		wm8739_write(sd, R8, 0x000);
		break;
	case 32000:
		/* 256fps, fs=32k */
		wm8739_write(sd, R8, 0x018);
		break;
	default:
		break;
	}
	/* activate */
	wm8739_write(sd, R9, 0x001);
	return 0;
}

static int wm8739_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wm8739_qctrl); i++)
		if (qc->id && qc->id == wm8739_qctrl[i].id) {
			memcpy(qc, &wm8739_qctrl[i], sizeof(*qc));
			return 0;
		}
	return -EINVAL;
}

static int wm8739_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_WM8739, 0);
}

static int wm8739_log_status(struct v4l2_subdev *sd)
{
	struct wm8739_state *state = to_state(sd);

	v4l2_info(sd, "Frequency: %u Hz\n", state->clock_freq);
	v4l2_info(sd, "Volume L:  %02x%s\n", state->vol_l & 0x1f,
			state->muted ? " (muted)" : "");
	v4l2_info(sd, "Volume R:  %02x%s\n", state->vol_r & 0x1f,
			state->muted ? " (muted)" : "");
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops wm8739_core_ops = {
	.log_status = wm8739_log_status,
	.g_chip_ident = wm8739_g_chip_ident,
	.queryctrl = wm8739_queryctrl,
	.g_ctrl = wm8739_g_ctrl,
	.s_ctrl = wm8739_s_ctrl,
};

static const struct v4l2_subdev_audio_ops wm8739_audio_ops = {
	.s_clock_freq = wm8739_s_clock_freq,
};

static const struct v4l2_subdev_ops wm8739_ops = {
	.core = &wm8739_core_ops,
	.audio = &wm8739_audio_ops,
};

/* ------------------------------------------------------------------------ */

/* i2c implementation */

static int wm8739_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct wm8739_state *state;
	struct v4l2_subdev *sd;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = kmalloc(sizeof(struct wm8739_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &wm8739_ops);
	state->vol_l = 0x17; /* 0dB */
	state->vol_r = 0x17; /* 0dB */
	state->muted = 0;
	state->balance = 32768;
	/* normalize (12dB(31) to -34.5dB(0) [0dB(23)] -> 65535 to 0) */
	state->volume = ((long)state->vol_l + 1) * 65535 / 31;
	state->clock_freq = 48000;

	/* Initialize wm8739 */

	/* reset */
	wm8739_write(sd, R15, 0x00);
	/* filter setting, high path, offet clear */
	wm8739_write(sd, R5, 0x000);
	/* ADC, OSC, Power Off mode Disable */
	wm8739_write(sd, R6, 0x000);
	/* Digital Audio interface format:
	   Enable Master mode, 24 bit, MSB first/left justified */
	wm8739_write(sd, R7, 0x049);
	/* sampling control: normal, 256fs, 48KHz sampling rate */
	wm8739_write(sd, R8, 0x000);
	/* activate */
	wm8739_write(sd, R9, 0x001);
	/* set volume/mute */
	wm8739_set_audio(sd);
	return 0;
}

static int wm8739_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id wm8739_id[] = {
	{ "wm8739", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8739_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "wm8739",
	.probe = wm8739_probe,
	.remove = wm8739_remove,
	.id_table = wm8739_id,
};
