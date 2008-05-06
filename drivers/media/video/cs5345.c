/*
 * cs5345 Cirrus Logic 24-bit, 192 kHz Stereo Audio ADC
 * Copyright (C) 2007 Hans Verkuil
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


#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-i2c-drv.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>

MODULE_DESCRIPTION("i2c device driver for cs5345 Audio ADC");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL");

static int debug;

module_param(debug, bool, 0644);

MODULE_PARM_DESC(debug, "Debugging messages\n\t\t\t0=Off (default), 1=On");


/* ----------------------------------------------------------------------- */

static inline int cs5345_write(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int cs5345_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int cs5345_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	struct v4l2_routing *route = arg;
	struct v4l2_control *ctrl = arg;

	switch (cmd) {
	case VIDIOC_INT_G_AUDIO_ROUTING:
		route->input = cs5345_read(client, 0x09) & 7;
		route->input |= cs5345_read(client, 0x05) & 0x70;
		route->output = 0;
		break;

	case VIDIOC_INT_S_AUDIO_ROUTING:
		if ((route->input & 0xf) > 6) {
			v4l_err(client, "Invalid input %d.\n", route->input);
			return -EINVAL;
		}
		cs5345_write(client, 0x09, route->input & 0xf);
		cs5345_write(client, 0x05, route->input & 0xf0);
		break;

	case VIDIOC_G_CTRL:
		if (ctrl->id == V4L2_CID_AUDIO_MUTE) {
			ctrl->value = (cs5345_read(client, 0x04) & 0x08) != 0;
			break;
		}
		if (ctrl->id != V4L2_CID_AUDIO_VOLUME)
			return -EINVAL;
		ctrl->value = cs5345_read(client, 0x07) & 0x3f;
		if (ctrl->value >= 32)
			ctrl->value = ctrl->value - 64;
		break;

	case VIDIOC_S_CTRL:
		break;
		if (ctrl->id == V4L2_CID_AUDIO_MUTE) {
			cs5345_write(client, 0x04, ctrl->value ? 0x80 : 0);
			break;
		}
		if (ctrl->id != V4L2_CID_AUDIO_VOLUME)
			return -EINVAL;
		if (ctrl->value > 24 || ctrl->value < -24)
			return -EINVAL;
		cs5345_write(client, 0x07, ((u8)ctrl->value) & 0x3f);
		cs5345_write(client, 0x08, ((u8)ctrl->value) & 0x3f);
		break;

#ifdef CONFIG_VIDEO_ADV_DEBUG
	case VIDIOC_DBG_G_REGISTER:
	case VIDIOC_DBG_S_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (!v4l2_chip_match_i2c_client(client,
					reg->match_type, reg->match_chip))
			return -EINVAL;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (cmd == VIDIOC_DBG_G_REGISTER)
			reg->val = cs5345_read(client, reg->reg & 0x1f);
		else
			cs5345_write(client, reg->reg & 0x1f, reg->val & 0x1f);
		break;
	}
#endif

	case VIDIOC_G_CHIP_IDENT:
		return v4l2_chip_ident_i2c_client(client,
				arg, V4L2_IDENT_CS5345, 0);

	case VIDIOC_LOG_STATUS:
		{
			u8 v = cs5345_read(client, 0x09) & 7;
			u8 m = cs5345_read(client, 0x04);
			int vol = cs5345_read(client, 0x08) & 0x3f;

			v4l_info(client, "Input:  %d%s\n", v,
				      (m & 0x80) ? " (muted)" : "");
			if (vol >= 32)
				vol = vol - 64;
			v4l_info(client, "Volume: %d dB\n", vol);
			break;
		}

	default:
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static int cs5345_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	cs5345_write(client, 0x02, 0x00);
	cs5345_write(client, 0x04, 0x01);
	cs5345_write(client, 0x09, 0x01);
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "cs5345",
	.driverid = I2C_DRIVERID_CS5345,
	.command = cs5345_command,
	.probe = cs5345_probe,
};

