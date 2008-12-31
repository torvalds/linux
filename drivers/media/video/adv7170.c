/*
 * adv7170 - adv7170, adv7171 video encoder driver version 0.0.1
 *
 * Copyright (C) 2002 Maxim Yevtyushkin <max@linuxmedialabs.com>
 *
 * Based on adv7176 driver by:
 *
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 * Copyright (C) 1999 Wolfgang Scherr <scherr@net4you.net>
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *    - some corrections for Pinnacle Systems Inc. DC10plus card.
 *
 * Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *    - moved over to linux>=2.4.x i2c protocol (1/1/2003)
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
#include <linux/video_encoder.h>
#include <media/v4l2-common.h>
#include <media/v4l2-i2c-drv-legacy.h>

MODULE_DESCRIPTION("Analog Devices ADV7170 video encoder driver");
MODULE_AUTHOR("Maxim Yevtyushkin");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/* ----------------------------------------------------------------------- */

struct adv7170 {
	unsigned char reg[128];

	int norm;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

static char *inputs[] = { "pass_through", "play_back" };
static char *norms[] = { "PAL", "NTSC" };

/* ----------------------------------------------------------------------- */

static inline int adv7170_write(struct i2c_client *client, u8 reg, u8 value)
{
	struct adv7170 *encoder = i2c_get_clientdata(client);

	encoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int adv7170_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int adv7170_write_block(struct i2c_client *client,
		     const u8 *data, unsigned int len)
{
	int ret = -1;
	u8 reg;

	/* the adv7170 has an autoincrement function, use it if
	 * the adapter understands raw I2C */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		/* do raw I2C, not smbus compatible */
		struct adv7170 *encoder = i2c_get_clientdata(client);
		u8 block_data[32];
		int block_len;

		while (len >= 2) {
			block_len = 0;
			block_data[block_len++] = reg = data[0];
			do {
				block_data[block_len++] =
				    encoder->reg[reg++] = data[1];
				len -= 2;
				data += 2;
			} while (len >= 2 && data[0] == reg && block_len < 32);
			ret = i2c_master_send(client, block_data, block_len);
			if (ret < 0)
				break;
		}
	} else {
		/* do some slow I2C emulation kind of thing */
		while (len >= 2) {
			reg = *data++;
			ret = adv7170_write(client, reg, *data++);
			if (ret < 0)
				break;
			len -= 2;
		}
	}
	return ret;
}

/* ----------------------------------------------------------------------- */

#define TR0MODE     0x4c
#define TR0RST	    0x80

#define TR1CAPT	    0x00
#define TR1PLAY	    0x00

static const unsigned char init_NTSC[] = {
	0x00, 0x10,		// MR0
	0x01, 0x20,		// MR1
	0x02, 0x0e,		// MR2 RTC control: bits 2 and 1
	0x03, 0x80,		// MR3
	0x04, 0x30,		// MR4
	0x05, 0x00,		// Reserved
	0x06, 0x00,		// Reserved
	0x07, TR0MODE,		// TM0
	0x08, TR1CAPT,		// TM1
	0x09, 0x16,		// Fsc0
	0x0a, 0x7c,		// Fsc1
	0x0b, 0xf0,		// Fsc2
	0x0c, 0x21,		// Fsc3
	0x0d, 0x00,		// Subcarrier Phase
	0x0e, 0x00,		// Closed Capt. Ext 0
	0x0f, 0x00,		// Closed Capt. Ext 1
	0x10, 0x00,		// Closed Capt. 0
	0x11, 0x00,		// Closed Capt. 1
	0x12, 0x00,		// Pedestal Ctl 0
	0x13, 0x00,		// Pedestal Ctl 1
	0x14, 0x00,		// Pedestal Ctl 2
	0x15, 0x00,		// Pedestal Ctl 3
	0x16, 0x00,		// CGMS_WSS_0
	0x17, 0x00,		// CGMS_WSS_1
	0x18, 0x00,		// CGMS_WSS_2
	0x19, 0x00,		// Teletext Ctl
};

static const unsigned char init_PAL[] = {
	0x00, 0x71,		// MR0
	0x01, 0x20,		// MR1
	0x02, 0x0e,		// MR2 RTC control: bits 2 and 1
	0x03, 0x80,		// MR3
	0x04, 0x30,		// MR4
	0x05, 0x00,		// Reserved
	0x06, 0x00,		// Reserved
	0x07, TR0MODE,		// TM0
	0x08, TR1CAPT,		// TM1
	0x09, 0xcb,		// Fsc0
	0x0a, 0x8a,		// Fsc1
	0x0b, 0x09,		// Fsc2
	0x0c, 0x2a,		// Fsc3
	0x0d, 0x00,		// Subcarrier Phase
	0x0e, 0x00,		// Closed Capt. Ext 0
	0x0f, 0x00,		// Closed Capt. Ext 1
	0x10, 0x00,		// Closed Capt. 0
	0x11, 0x00,		// Closed Capt. 1
	0x12, 0x00,		// Pedestal Ctl 0
	0x13, 0x00,		// Pedestal Ctl 1
	0x14, 0x00,		// Pedestal Ctl 2
	0x15, 0x00,		// Pedestal Ctl 3
	0x16, 0x00,		// CGMS_WSS_0
	0x17, 0x00,		// CGMS_WSS_1
	0x18, 0x00,		// CGMS_WSS_2
	0x19, 0x00,		// Teletext Ctl
};


static int adv7170_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	struct adv7170 *encoder = i2c_get_clientdata(client);

	switch (cmd) {
	case 0:
#if 0
		/* This is just for testing!!! */
		adv7170_write_block(client, init_common,
				    sizeof(init_common));
		adv7170_write(client, 0x07, TR0MODE | TR0RST);
		adv7170_write(client, 0x07, TR0MODE);
#endif
		break;

	case ENCODER_GET_CAPABILITIES:
	{
		struct video_encoder_capability *cap = arg;

		cap->flags = VIDEO_ENCODER_PAL |
			     VIDEO_ENCODER_NTSC;
		cap->inputs = 2;
		cap->outputs = 1;
		break;
	}

	case ENCODER_SET_NORM:
	{
		int iarg = *(int *) arg;

		v4l_dbg(1, debug, client, "set norm %d\n", iarg);

		switch (iarg) {
		case VIDEO_MODE_NTSC:
			adv7170_write_block(client, init_NTSC,
					    sizeof(init_NTSC));
			if (encoder->input == 0)
				adv7170_write(client, 0x02, 0x0e);	// Enable genlock
			adv7170_write(client, 0x07, TR0MODE | TR0RST);
			adv7170_write(client, 0x07, TR0MODE);
			break;

		case VIDEO_MODE_PAL:
			adv7170_write_block(client, init_PAL,
					    sizeof(init_PAL));
			if (encoder->input == 0)
				adv7170_write(client, 0x02, 0x0e);	// Enable genlock
			adv7170_write(client, 0x07, TR0MODE | TR0RST);
			adv7170_write(client, 0x07, TR0MODE);
			break;

		default:
			v4l_dbg(1, debug, client, "illegal norm: %d\n", iarg);
			return -EINVAL;
		}
		v4l_dbg(1, debug, client, "switched to %s\n", norms[iarg]);
		encoder->norm = iarg;
		break;
	}

	case ENCODER_SET_INPUT:
	{
		int iarg = *(int *) arg;

		/* RJ: *iarg = 0: input is from decoder
		 *iarg = 1: input is from ZR36060
		 *iarg = 2: color bar */

		v4l_dbg(1, debug, client, "set input from %s\n",
			iarg == 0 ? "decoder" : "ZR36060");

		switch (iarg) {
		case 0:
			adv7170_write(client, 0x01, 0x20);
			adv7170_write(client, 0x08, TR1CAPT);	/* TR1 */
			adv7170_write(client, 0x02, 0x0e);	// Enable genlock
			adv7170_write(client, 0x07, TR0MODE | TR0RST);
			adv7170_write(client, 0x07, TR0MODE);
			/* udelay(10); */
			break;

		case 1:
			adv7170_write(client, 0x01, 0x00);
			adv7170_write(client, 0x08, TR1PLAY);	/* TR1 */
			adv7170_write(client, 0x02, 0x08);
			adv7170_write(client, 0x07, TR0MODE | TR0RST);
			adv7170_write(client, 0x07, TR0MODE);
			/* udelay(10); */
			break;

		default:
			v4l_dbg(1, debug, client, "illegal input: %d\n", iarg);
			return -EINVAL;
		}
		v4l_dbg(1, debug, client, "switched to %s\n", inputs[iarg]);
		encoder->input = iarg;
		break;
	}

	case ENCODER_SET_OUTPUT:
	{
		int *iarg = arg;

		/* not much choice of outputs */
		if (*iarg != 0) {
			return -EINVAL;
		}
		break;
	}

	case ENCODER_ENABLE_OUTPUT:
	{
		int *iarg = arg;

		encoder->enable = !!*iarg;
		break;
	}

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static unsigned short normal_i2c[] = {
	0xd4 >> 1, 0xd6 >> 1,	/* adv7170 IDs */
	0x54 >> 1, 0x56 >> 1,	/* adv7171 IDs */
	I2C_CLIENT_END
};

I2C_CLIENT_INSMOD;

static int adv7170_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct adv7170 *encoder;
	int i;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	encoder = kzalloc(sizeof(struct adv7170), GFP_KERNEL);
	if (encoder == NULL)
		return -ENOMEM;
	encoder->norm = VIDEO_MODE_NTSC;
	encoder->input = 0;
	encoder->enable = 1;
	i2c_set_clientdata(client, encoder);

	i = adv7170_write_block(client, init_NTSC, sizeof(init_NTSC));
	if (i >= 0) {
		i = adv7170_write(client, 0x07, TR0MODE | TR0RST);
		i = adv7170_write(client, 0x07, TR0MODE);
		i = adv7170_read(client, 0x12);
		v4l_dbg(1, debug, client, "revision %d\n", i & 1);
	}
	if (i < 0)
		v4l_dbg(1, debug, client, "init error 0x%x\n", i);
	return 0;
}

static int adv7170_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id adv7170_id[] = {
	{ "adv7170", 0 },
	{ "adv7171", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7170_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "adv7170",
	.driverid = I2C_DRIVERID_ADV7170,
	.command = adv7170_command,
	.probe = adv7170_probe,
	.remove = adv7170_remove,
	.id_table = adv7170_id,
};
