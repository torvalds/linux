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

MODULE_DESCRIPTION("vp27smpx driver");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL");

static unsigned short normal_i2c[] = { 0xb6 >> 1, I2C_CLIENT_END };


I2C_CLIENT_INSMOD;

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

	if (i2c_master_send(client, data, sizeof(data)) != sizeof(data)) {
		v4l_err(client, "%s: I/O error setting audmode\n", client->name);
	}
	else {
		state->audmode = audmode;
	}
}

static int vp27smpx_command(struct i2c_client *client, unsigned int cmd,
			  void *arg)
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
		return v4l2_chip_ident_i2c_client(client, arg, V4L2_IDENT_VP27SMPX, 0);

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

static struct i2c_driver i2c_driver;

static int vp27smpx_attach(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct vp27smpx_state *state;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;

	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver;
	snprintf(client->name, sizeof(client->name) - 1, "vp27smpx");

	v4l_info(client, "chip found @ 0x%x (%s)\n", address << 1, adapter->name);

	state = kzalloc(sizeof(struct vp27smpx_state), GFP_KERNEL);
	if (state == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	state->audmode = V4L2_TUNER_MODE_STEREO;
	i2c_set_clientdata(client, state);

	/* initialize vp27smpx */
	vp27smpx_set_audmode(client, state->audmode);
	i2c_attach_client(client);

	return 0;
}

static int vp27smpx_probe(struct i2c_adapter *adapter)
{
	if (adapter->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adapter, &addr_data, vp27smpx_attach);
	return 0;
}

static int vp27smpx_detach(struct i2c_client *client)
{
	struct vp27smpx_state *state = i2c_get_clientdata(client);
	int err;

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}
	kfree(state);
	kfree(client);

	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver i2c_driver = {
	.driver = {
		.name = "vp27smpx",
	},
	.id             = I2C_DRIVERID_VP27SMPX,
	.attach_adapter = vp27smpx_probe,
	.detach_client  = vp27smpx_detach,
	.command        = vp27smpx_command,
};


static int __init vp27smpx_init_module(void)
{
	return i2c_add_driver(&i2c_driver);
}

static void __exit vp27smpx_cleanup_module(void)
{
	i2c_del_driver(&i2c_driver);
}

module_init(vp27smpx_init_module);
module_exit(vp27smpx_cleanup_module);
