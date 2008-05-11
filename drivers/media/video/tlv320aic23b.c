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
#include <media/v4l2-i2c-drv-legacy.h>

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

	for (i = 0; i < 3; i++)
		if (i2c_smbus_write_byte_data(client,
				(reg << 1) | (val >> 8), val & 0xff) == 0)
			return 0;
	v4l_err(client, "I2C: cannot write %03x to register R%d\n", val, reg);
	return -1;
}

static int tlv320aic23b_command(struct i2c_client *client,
				unsigned int cmd, void *arg)
{
	struct tlv320aic23b_state *state = i2c_get_clientdata(client);
	struct v4l2_control *ctrl = arg;
	u32 *freq = arg;

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

static int tlv320aic23b_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct tlv320aic23b_state *state;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = kmalloc(sizeof(struct tlv320aic23b_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
	state->muted = 0;
	i2c_set_clientdata(client, state);

	/* Initialize tlv320aic23b */

	/* RESET */
	tlv320aic23b_write(client, 15, 0x000);
	/* turn off DAC & mic input */
	tlv320aic23b_write(client, 6, 0x00A);
	/* left-justified, 24-bit, master mode */
	tlv320aic23b_write(client, 7, 0x049);
	/* set gain on both channels to +3.0 dB */
	tlv320aic23b_write(client, 0, 0x119);
	/* set sample rate to 48 kHz */
	tlv320aic23b_write(client, 8, 0x000);
	/* activate digital interface */
	tlv320aic23b_write(client, 9, 0x001);
	return 0;
}

static int tlv320aic23b_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id tlv320aic23b_id[] = {
	{ "tlv320aic23b", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tlv320aic23b_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "tlv320aic23b",
	.driverid = I2C_DRIVERID_TLV320AIC23B,
	.command = tlv320aic23b_command,
	.probe = tlv320aic23b_probe,
	.remove = tlv320aic23b_remove,
	.id_table = tlv320aic23b_id,
};
