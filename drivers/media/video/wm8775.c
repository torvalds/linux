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
#include <media/v4l2-i2c-drv-legacy.h>

MODULE_DESCRIPTION("wm8775 driver");
MODULE_AUTHOR("Ulf Eklund, Hans Verkuil");
MODULE_LICENSE("GPL");

static unsigned short normal_i2c[] = { 0x36 >> 1, I2C_CLIENT_END };


I2C_CLIENT_INSMOD;


/* ----------------------------------------------------------------------- */

enum {
	R7 = 7, R11 = 11,
	R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R23 = 23,
	TOT_REGS
};

struct wm8775_state {
	u8 input;		/* Last selected input (0-0xf) */
	u8 muted;
};

static int wm8775_write(struct i2c_client *client, int reg, u16 val)
{
	int i;

	if (reg < 0 || reg >= TOT_REGS) {
		v4l_err(client, "Invalid register R%d\n", reg);
		return -1;
	}

	for (i = 0; i < 3; i++)
		if (i2c_smbus_write_byte_data(client,
				(reg << 1) | (val >> 8), val & 0xff) == 0)
			return 0;
	v4l_err(client, "I2C: cannot write %03x to register R%d\n", val, reg);
	return -1;
}

static int wm8775_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	struct wm8775_state *state = i2c_get_clientdata(client);
	struct v4l2_routing *route = arg;
	struct v4l2_control *ctrl = arg;

	switch (cmd) {
	case VIDIOC_INT_G_AUDIO_ROUTING:
		route->input = state->input;
		route->output = 0;
		break;

	case VIDIOC_INT_S_AUDIO_ROUTING:
		/* There are 4 inputs and one output. Zero or more inputs
		   are multiplexed together to the output. Hence there are
		   16 combinations.
		   If only one input is active (the normal case) then the
		   input values 1, 2, 4 or 8 should be used. */
		if (route->input > 15) {
			v4l_err(client, "Invalid input %d.\n", route->input);
			return -EINVAL;
		}
		state->input = route->input;
		if (state->muted)
			break;
		wm8775_write(client, R21, 0x0c0);
		wm8775_write(client, R14, 0x1d4);
		wm8775_write(client, R15, 0x1d4);
		wm8775_write(client, R21, 0x100 + state->input);
		break;

	case VIDIOC_G_CTRL:
		if (ctrl->id != V4L2_CID_AUDIO_MUTE)
			return -EINVAL;
		ctrl->value = state->muted;
		break;

	case VIDIOC_S_CTRL:
		if (ctrl->id != V4L2_CID_AUDIO_MUTE)
			return -EINVAL;
		state->muted = ctrl->value;
		wm8775_write(client, R21, 0x0c0);
		wm8775_write(client, R14, 0x1d4);
		wm8775_write(client, R15, 0x1d4);
		if (!state->muted)
			wm8775_write(client, R21, 0x100 + state->input);
		break;

	case VIDIOC_G_CHIP_IDENT:
		return v4l2_chip_ident_i2c_client(client,
				arg, V4L2_IDENT_WM8775, 0);

	case VIDIOC_LOG_STATUS:
		v4l_info(client, "Input: %d%s\n", state->input,
			    state->muted ? " (muted)" : "");
		break;

	case VIDIOC_S_FREQUENCY:
		/* If I remove this, then it can happen that I have no
		   sound the first time I tune from static to a valid channel.
		   It's difficult to reproduce and is almost certainly related
		   to the zero cross detect circuit. */
		wm8775_write(client, R21, 0x0c0);
		wm8775_write(client, R14, 0x1d4);
		wm8775_write(client, R15, 0x1d4);
		wm8775_write(client, R21, 0x100 + state->input);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */

static int wm8775_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct wm8775_state *state;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = kmalloc(sizeof(struct wm8775_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
	state->input = 2;
	state->muted = 0;
	i2c_set_clientdata(client, state);

	/* Initialize wm8775 */

	/* RESET */
	wm8775_write(client, R23, 0x000);
	/* Disable zero cross detect timeout */
	wm8775_write(client, R7, 0x000);
	/* Left justified, 24-bit mode */
	wm8775_write(client, R11, 0x021);
	/* Master mode, clock ratio 256fs */
	wm8775_write(client, R12, 0x102);
	/* Powered up */
	wm8775_write(client, R13, 0x000);
	/* ADC gain +2.5dB, enable zero cross */
	wm8775_write(client, R14, 0x1d4);
	/* ADC gain +2.5dB, enable zero cross */
	wm8775_write(client, R15, 0x1d4);
	/* ALC Stereo, ALC target level -1dB FS max gain +8dB */
	wm8775_write(client, R16, 0x1bf);
	/* Enable gain control, use zero cross detection,
	   ALC hold time 42.6 ms */
	wm8775_write(client, R17, 0x185);
	/* ALC gain ramp up delay 34 s, ALC gain ramp down delay 33 ms */
	wm8775_write(client, R18, 0x0a2);
	/* Enable noise gate, threshold -72dBfs */
	wm8775_write(client, R19, 0x005);
	/* Transient window 4ms, lower PGA gain limit -1dB */
	wm8775_write(client, R20, 0x07a);
	/* LRBOTH = 1, use input 2. */
	wm8775_write(client, R21, 0x102);
	return 0;
}

static int wm8775_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8775_id[] = {
	{ "wm8775", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8775_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "wm8775",
	.driverid = I2C_DRIVERID_WM8775,
	.command = wm8775_command,
	.probe = wm8775_probe,
	.remove = wm8775_remove,
	.id_table = wm8775_id,
};

