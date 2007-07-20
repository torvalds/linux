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
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>

MODULE_DESCRIPTION("wm8739 driver");
MODULE_AUTHOR("T. Adachi, Hans Verkuil");
MODULE_LICENSE("GPL");

static int debug = 0;
static unsigned short normal_i2c[] = { 0x34 >> 1, 0x36 >> 1, I2C_CLIENT_END };

module_param(debug, int, 0644);

MODULE_PARM_DESC(debug, "Debug level (0-1)");


I2C_CLIENT_INSMOD;

/* ------------------------------------------------------------------------ */

enum {
	R0 = 0, R1,
	R5 = 5, R6, R7, R8, R9, R15 = 15,
	TOT_REGS
};

struct wm8739_state {
	u32 clock_freq;
	u8 muted;
	u16 volume;
	u16 balance;
	u8 vol_l; 		/* +12dB to -34.5dB 1.5dB step (5bit) def:0dB */
	u8 vol_r; 		/* +12dB to -34.5dB 1.5dB step (5bit) def:0dB */
};

/* ------------------------------------------------------------------------ */

static int wm8739_write(struct i2c_client *client, int reg, u16 val)
{
	int i;

	if (reg < 0 || reg >= TOT_REGS) {
		v4l_err(client, "Invalid register R%d\n", reg);
		return -1;
	}

	v4l_dbg(1, debug, client, "write: %02x %02x\n", reg, val);

	for (i = 0; i < 3; i++) {
		if (i2c_smbus_write_byte_data(client, (reg << 1) |
					(val >> 8), val & 0xff) == 0) {
			return 0;
		}
	}
	v4l_err(client, "I2C: cannot write %03x to register R%d\n", val, reg);
	return -1;
}

/* write regs to set audio volume etc */
static void wm8739_set_audio(struct i2c_client *client)
{
	struct wm8739_state *state = i2c_get_clientdata(client);
	u16 mute = state->muted ? 0x80 : 0;

	/* Volume setting: bits 0-4, 0x1f = 12 dB, 0x00 = -34.5 dB
	 * Default setting: 0x17 = 0 dB
	 */
	wm8739_write(client, R0, (state->vol_l & 0x1f) | mute);
	wm8739_write(client, R1, (state->vol_r & 0x1f) | mute);
}

static int wm8739_get_ctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct wm8739_state *state = i2c_get_clientdata(client);

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

static int wm8739_set_ctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct wm8739_state *state = i2c_get_clientdata(client);
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
	wm8739_set_audio(client);
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
	},{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 1,
		.default_value = 1,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
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

static int wm8739_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct wm8739_state *state = i2c_get_clientdata(client);

	switch (cmd) {
	case VIDIOC_INT_AUDIO_CLOCK_FREQ:
	{
		u32 audiofreq = *(u32 *)arg;

		state->clock_freq = audiofreq;
		wm8739_write(client, R9, 0x000);	/* de-activate */
		switch (audiofreq) {
		case 44100:
			wm8739_write(client, R8, 0x020); /* 256fps, fs=44.1k     */
			break;
		case 48000:
			wm8739_write(client, R8, 0x000); /* 256fps, fs=48k       */
			break;
		case 32000:
			wm8739_write(client, R8, 0x018); /* 256fps, fs=32k       */
			break;
		default:
			break;
		}
		wm8739_write(client, R9, 0x001);	/* activate */
		break;
	}

	case VIDIOC_G_CTRL:
		return wm8739_get_ctrl(client, arg);

	case VIDIOC_S_CTRL:
		return wm8739_set_ctrl(client, arg);

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *qc = arg;
		int i;

		for (i = 0; i < ARRAY_SIZE(wm8739_qctrl); i++)
			if (qc->id && qc->id == wm8739_qctrl[i].id) {
				memcpy(qc, &wm8739_qctrl[i], sizeof(*qc));
				return 0;
			}
		return -EINVAL;
	}

	case VIDIOC_G_CHIP_IDENT:
		return v4l2_chip_ident_i2c_client(client, arg, V4L2_IDENT_WM8739, 0);

	case VIDIOC_LOG_STATUS:
		v4l_info(client, "Frequency: %u Hz\n", state->clock_freq);
		v4l_info(client, "Volume L:  %02x%s\n", state->vol_l & 0x1f,
				state->muted ? " (muted)" : "");
		v4l_info(client, "Volume R:  %02x%s\n", state->vol_r & 0x1f,
				state->muted ? " (muted)" : "");
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* ------------------------------------------------------------------------ */

/* i2c implementation */

static struct i2c_driver i2c_driver;

static int wm8739_attach(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct wm8739_state *state;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == NULL)
		return -ENOMEM;

	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver;
	snprintf(client->name, sizeof(client->name) - 1, "wm8739");

	v4l_info(client, "chip found @ 0x%x (%s)\n", address << 1, adapter->name);

	state = kmalloc(sizeof(struct wm8739_state), GFP_KERNEL);
	if (state == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	state->vol_l = 0x17; /* 0dB */
	state->vol_r = 0x17; /* 0dB */
	state->muted = 0;
	state->balance = 32768;
	/* normalize (12dB(31) to -34.5dB(0) [0dB(23)] -> 65535 to 0) */
	state->volume = ((long)state->vol_l + 1) * 65535 / 31;
	state->clock_freq = 48000;
	i2c_set_clientdata(client, state);

	/* initialize wm8739 */
	wm8739_write(client, R15, 0x00); /* reset */
	wm8739_write(client, R5, 0x000); /* filter setting, high path, offet clear */
	wm8739_write(client, R6, 0x000); /* ADC, OSC, Power Off mode Disable */
	wm8739_write(client, R7, 0x049); /* Digital Audio interface format */
					 /* Enable Master mode */
					 /* 24 bit, MSB first/left justified */
	wm8739_write(client, R8, 0x000); /* sampling control */
					 /* normal, 256fs, 48KHz sampling rate */
	wm8739_write(client, R9, 0x001); /* activate */
	wm8739_set_audio(client); 	 /* set volume/mute */

	i2c_attach_client(client);

	return 0;
}

static int wm8739_probe(struct i2c_adapter *adapter)
{
	if (adapter->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adapter, &addr_data, wm8739_attach);
	return 0;
}

static int wm8739_detach(struct i2c_client *client)
{
	struct wm8739_state *state = i2c_get_clientdata(client);
	int err;

	err = i2c_detach_client(client);
	if (err)
		return err;

	kfree(state);
	kfree(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver i2c_driver = {
	.driver = {
		.name = "wm8739",
	},
	.id = I2C_DRIVERID_WM8739,
	.attach_adapter = wm8739_probe,
	.detach_client  = wm8739_detach,
	.command = wm8739_command,
};


static int __init wm8739_init_module(void)
{
	return i2c_add_driver(&i2c_driver);
}

static void __exit wm8739_cleanup_module(void)
{
	i2c_del_driver(&i2c_driver);
}

module_init(wm8739_init_module);
module_exit(wm8739_cleanup_module);
