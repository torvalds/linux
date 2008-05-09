/*
 * vp27smpx - driver version 0.0.1
 *
 * Copyright (C) 2007 Hans Verkuil <hverkuil@xs4all.nl>
 *
 * Based on a tvaudio patch from Takahiro Adachi <tadachi@tadachi-net.com>
 * and Kazuhiko Kawakami <kazz-0@mail.goo.ne.jp>
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
#include <media/v4l2-i2c-drv.h>

MODULE_DESCRIPTION("vp27smpx driver");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL");


/* ----------------------------------------------------------------------- */

struct vp27smpx_state {
	int radio;
	u32 audmode;
};

static void vp27smpx_set_audmode(struct i2c_client *client, u32 audmode)
{
	struct vp27smpx_state *state = i2c_get_clientdata(client);
	u8 data[3] = { 0x00, 0x00, 0x04 };

	switch (audmode) {
	case V4L2_TUNER_MODE_MONO:
	case V4L2_TUNER_MODE_LANG1:
		break;
	case V4L2_TUNER_MODE_STEREO:
	case V4L2_TUNER_MODE_LANG1_LANG2:
		data[1] = 0x01;
		break;
	case V4L2_TUNER_MODE_LANG2:
		data[1] = 0x02;
		break;
	}

	if (i2c_master_send(client, data, sizeof(data)) != sizeof(data))
		v4l_err(client, "%s: I/O error setting audmode\n",
				client->name);
	else
		state->audmode = audmode;
}

static int vp27smpx_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	struct vp27smpx_state *state = i2c_get_clientdata(client);
	struct v4l2_tuner *vt = arg;

	switch (cmd) {
	case AUDC_SET_RADIO:
		state->radio = 1;
		break;

	case VIDIOC_S_STD:
		state->radio = 0;
		break;

	case VIDIOC_S_TUNER:
		if (!state->radio)
			vp27smpx_set_audmode(client, vt->audmode);
		break;

	case VIDIOC_G_TUNER:
		if (state->radio)
			break;
		vt->audmode = state->audmode;
		vt->capability = V4L2_TUNER_CAP_STEREO |
			V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2;
		vt->rxsubchans = V4L2_TUNER_SUB_MONO;
		break;

	case VIDIOC_G_CHIP_IDENT:
		return v4l2_chip_ident_i2c_client(client, arg,
				V4L2_IDENT_VP27SMPX, 0);

	case VIDIOC_LOG_STATUS:
		v4l_info(client, "Audio Mode: %u%s\n", state->audmode,
				state->radio ? " (Radio)" : "");
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

static int vp27smpx_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct vp27smpx_state *state;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	snprintf(client->name, sizeof(client->name) - 1, "vp27smpx");

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = kzalloc(sizeof(struct vp27smpx_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
	state->audmode = V4L2_TUNER_MODE_STEREO;
	i2c_set_clientdata(client, state);

	/* initialize vp27smpx */
	vp27smpx_set_audmode(client, state->audmode);
	return 0;
}

static int vp27smpx_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "vp27smpx",
	.driverid = I2C_DRIVERID_VP27SMPX,
	.command = vp27smpx_command,
	.probe = vp27smpx_probe,
	.remove = vp27smpx_remove,
};

