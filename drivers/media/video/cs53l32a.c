/*
 * cs53l32a (Adaptec AVC-2010 and AVC-2410) i2c ivtv driver.
 * Copyright (C) 2005  Martin Vaughan
 *
 * Audio source switching for Adaptec AVC-2410 added by Trev Jackson
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

MODULE_DESCRIPTION("i2c device driver for cs53l32a Audio ADC");
MODULE_AUTHOR("Martin Vaughan");
MODULE_LICENSE("GPL");

static int debug;

module_param(debug, bool, 0644);

MODULE_PARM_DESC(debug, "Debugging messages\n\t\t\t0=Off (default), 1=On");

static unsigned short normal_i2c[] = { 0x22 >> 1, I2C_CLIENT_END };


I2C_CLIENT_INSMOD;

/* ----------------------------------------------------------------------- */

static int cs53l32a_write(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int cs53l32a_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int cs53l32a_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	struct v4l2_routing *route = arg;
	struct v4l2_control *ctrl = arg;

	switch (cmd) {
	case VIDIOC_INT_G_AUDIO_ROUTING:
		route->input = (cs53l32a_read(client, 0x01) >> 4) & 3;
		route->output = 0;
		break;

	case VIDIOC_INT_S_AUDIO_ROUTING:
		/* There are 2 physical inputs, but the second input can be
		   placed in two modes, the first mode bypasses the PGA (gain),
		   the second goes through the PGA. Hence there are three
		   possible inputs to choose from. */
		if (route->input > 2) {
			v4l_err(client, "Invalid input %d.\n", route->input);
			return -EINVAL;
		}
		cs53l32a_write(client, 0x01, 0x01 + (route->input << 4));
		break;

	case VIDIOC_G_CTRL:
		if (ctrl->id == V4L2_CID_AUDIO_MUTE) {
			ctrl->value = (cs53l32a_read(client, 0x03) & 0xc0) != 0;
			break;
		}
		if (ctrl->id != V4L2_CID_AUDIO_VOLUME)
			return -EINVAL;
		ctrl->value = (s8)cs53l32a_read(client, 0x04);
		break;

	case VIDIOC_S_CTRL:
		if (ctrl->id == V4L2_CID_AUDIO_MUTE) {
			cs53l32a_write(client, 0x03, ctrl->value ? 0xf0 : 0x30);
			break;
		}
		if (ctrl->id != V4L2_CID_AUDIO_VOLUME)
			return -EINVAL;
		if (ctrl->value > 12 || ctrl->value < -96)
			return -EINVAL;
		cs53l32a_write(client, 0x04, (u8) ctrl->value);
		cs53l32a_write(client, 0x05, (u8) ctrl->value);
		break;

	case VIDIOC_G_CHIP_IDENT:
		return v4l2_chip_ident_i2c_client(client,
				arg, V4L2_IDENT_CS53l32A, 0);

	case VIDIOC_LOG_STATUS:
		{
			u8 v = cs53l32a_read(client, 0x01);
			u8 m = cs53l32a_read(client, 0x03);
			s8 vol = cs53l32a_read(client, 0x04);

			v4l_info(client, "Input:  %d%s\n", (v >> 4) & 3,
				      (m & 0xC0) ? " (muted)" : "");
			v4l_info(client, "Volume: %d dB\n", vol);
			break;
		}

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

static int cs53l32a_probe(struct i2c_client *client)
{
	int i;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	snprintf(client->name, sizeof(client->name) - 1, "cs53l32a");

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	for (i = 1; i <= 7; i++) {
		u8 v = cs53l32a_read(client, i);

		v4l_dbg(1, debug, client, "Read Reg %d %02x\n", i, v);
	}

	/* Set cs53l32a internal register for Adaptec 2010/2410 setup */

	cs53l32a_write(client, 0x01, (u8) 0x21);
	cs53l32a_write(client, 0x02, (u8) 0x29);
	cs53l32a_write(client, 0x03, (u8) 0x30);
	cs53l32a_write(client, 0x04, (u8) 0x00);
	cs53l32a_write(client, 0x05, (u8) 0x00);
	cs53l32a_write(client, 0x06, (u8) 0x00);
	cs53l32a_write(client, 0x07, (u8) 0x00);

	/* Display results, should be 0x21,0x29,0x30,0x00,0x00,0x00,0x00 */

	for (i = 1; i <= 7; i++) {
		u8 v = cs53l32a_read(client, i);

		v4l_dbg(1, debug, client, "Read Reg %d %02x\n", i, v);
	}
	return 0;
}

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "cs53l32a",
	.driverid = I2C_DRIVERID_CS53L32A,
	.command = cs53l32a_command,
	.probe = cs53l32a_probe,
};

