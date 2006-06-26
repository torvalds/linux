/*
 * tlv320aic23b - driver version 0.0.1
 *
 * Copyright (C) 2006 Scott Alfter <salfter@ssai.us>
 *
 * Based on wm8775 driver
 *
 * Copyright (C) 2004 Ulf Eklund <ivtv at eklund.to>
 * Copyright (C) 2005 Hans Verkuil <hverkuil@xs4all.nl>
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

MODULE_DESCRIPTION("tlv320aic23b driver");
MODULE_AUTHOR("Scott Alfter, Ulf Eklund, Hans Verkuil");
MODULE_LICENSE("GPL");

static unsigned short normal_i2c[] = { 0x34 >> 1, I2C_CLIENT_END };


I2C_CLIENT_INSMOD;

/* ----------------------------------------------------------------------- */

struct tlv320aic23b_state {
	u8 muted;
};

static int tlv320aic23b_write(struct i2c_client *client, int reg, u16 val)
{
	int i;

	if ((reg < 0 || reg > 9) && (reg != 15)) {
		v4l_err(client, "Invalid register R%d\n", reg);
		return -1;
	}

	for (i = 0; i < 3; i++) {
		if (i2c_smbus_write_byte_data(client, (reg << 1) |
					(val >> 8), val & 0xff) == 0) {
			return 0;
		}
	}
	v4l_err(client, "I2C: cannot write %03x to register R%d\n", val, reg);
	return -1;
}

static int tlv320aic23b_command(struct i2c_client *client, unsigned int cmd,
			  void *arg)
{
	struct tlv320aic23b_state *state = i2c_get_clientdata(client);
	struct v4l2_control *ctrl = arg;
	u32* freq = arg;

	switch (cmd) {
	case VIDIOC_INT_AUDIO_CLOCK_FREQ:
		switch (*freq) {
			case 32000: /* set sample rate to 32 kHz */
				tlv320aic23b_write(client, 8, 0x018);
				break;
			case 44100: /* set sample rate to 44.1 kHz */
				tlv320aic23b_write(client, 8, 0x022);
				break;
			case 48000: /* set sample rate to 48 kHz */
				tlv320aic23b_write(client, 8, 0x000);
				break;
			default:
				return -EINVAL;
		}
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
		tlv320aic23b_write(client, 0, 0x180); /* mute both channels */
		/* set gain on both channels to +3.0 dB */
		if (!state->muted)
			tlv320aic23b_write(client, 0, 0x119);
		break;

	case VIDIOC_LOG_STATUS:
		v4l_info(client, "Input: %s\n",
			    state->muted ? "muted" : "active");
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

static int tlv320aic23b_attach(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct tlv320aic23b_state *state;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;

	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver;
	snprintf(client->name, sizeof(client->name) - 1, "tlv320aic23b");

	v4l_info(client, "chip found @ 0x%x (%s)\n", address << 1, adapter->name);

	state = kmalloc(sizeof(struct tlv320aic23b_state), GFP_KERNEL);
	if (state == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	state->muted = 0;
	i2c_set_clientdata(client, state);

	/* initialize tlv320aic23b */
	tlv320aic23b_write(client, 15, 0x000);	/* RESET */
	tlv320aic23b_write(client, 6, 0x00A);   /* turn off DAC & mic input */
	tlv320aic23b_write(client, 7, 0x049);   /* left-justified, 24-bit, master mode */
	tlv320aic23b_write(client, 0, 0x119);   /* set gain on both channels to +3.0 dB */
	tlv320aic23b_write(client, 8, 0x000);   /* set sample rate to 48 kHz */
	tlv320aic23b_write(client, 9, 0x001);   /* activate digital interface */

	i2c_attach_client(client);

	return 0;
}

static int tlv320aic23b_probe(struct i2c_adapter *adapter)
{
	if (adapter->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adapter, &addr_data, tlv320aic23b_attach);
	return 0;
}

static int tlv320aic23b_detach(struct i2c_client *client)
{
	int err;

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}
	kfree(client);

	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver i2c_driver = {
	.driver = {
		.name = "tlv320aic23b",
	},
	.id             = I2C_DRIVERID_TLV320AIC23B,
	.attach_adapter = tlv320aic23b_probe,
	.detach_client  = tlv320aic23b_detach,
	.command        = tlv320aic23b_command,
};


static int __init tlv320aic23b_init_module(void)
{
	return i2c_add_driver(&i2c_driver);
}

static void __exit tlv320aic23b_cleanup_module(void)
{
	i2c_del_driver(&i2c_driver);
}

module_init(tlv320aic23b_init_module);
module_exit(tlv320aic23b_cleanup_module);
