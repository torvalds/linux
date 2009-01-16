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
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-i2c-drv-legacy.h>

MODULE_DESCRIPTION("i2c device driver for cs53l32a Audio ADC");
MODULE_AUTHOR("Martin Vaughan");
MODULE_LICENSE("GPL");

static int debug;

module_param(debug, bool, 0644);

MODULE_PARM_DESC(debug, "Debugging messages, 0=Off (default), 1=On");

static unsigned short normal_i2c[] = { 0x22 >> 1, I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

/* ----------------------------------------------------------------------- */

static int cs53l32a_write(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_write_byte_data(client, reg, value);
}

static int cs53l32a_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte_data(client, reg);
}

static int cs53l32a_s_routing(struct v4l2_subdev *sd, const struct v4l2_routing *route)
{
	/* There are 2 physical inputs, but the second input can be
	   placed in two modes, the first mode bypasses the PGA (gain),
	   the second goes through the PGA. Hence there are three
	   possible inputs to choose from. */
	if (route->input > 2) {
		v4l2_err(sd, "Invalid input %d.\n", route->input);
		return -EINVAL;
	}
	cs53l32a_write(sd, 0x01, 0x01 + (route->input << 4));
	return 0;
}

static int cs53l32a_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	if (ctrl->id == V4L2_CID_AUDIO_MUTE) {
		ctrl->value = (cs53l32a_read(sd, 0x03) & 0xc0) != 0;
		return 0;
	}
	if (ctrl->id != V4L2_CID_AUDIO_VOLUME)
		return -EINVAL;
	ctrl->value = (s8)cs53l32a_read(sd, 0x04);
	return 0;
}

static int cs53l32a_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	if (ctrl->id == V4L2_CID_AUDIO_MUTE) {
		cs53l32a_write(sd, 0x03, ctrl->value ? 0xf0 : 0x30);
		return 0;
	}
	if (ctrl->id != V4L2_CID_AUDIO_VOLUME)
		return -EINVAL;
	if (ctrl->value > 12 || ctrl->value < -96)
		return -EINVAL;
	cs53l32a_write(sd, 0x04, (u8) ctrl->value);
	cs53l32a_write(sd, 0x05, (u8) ctrl->value);
	return 0;
}

static int cs53l32a_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client,
			chip, V4L2_IDENT_CS53l32A, 0);
}

static int cs53l32a_log_status(struct v4l2_subdev *sd)
{
	u8 v = cs53l32a_read(sd, 0x01);
	u8 m = cs53l32a_read(sd, 0x03);
	s8 vol = cs53l32a_read(sd, 0x04);

	v4l2_info(sd, "Input:  %d%s\n", (v >> 4) & 3,
			(m & 0xC0) ? " (muted)" : "");
	v4l2_info(sd, "Volume: %d dB\n", vol);
	return 0;
}

static int cs53l32a_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	return v4l2_subdev_command(i2c_get_clientdata(client), cmd, arg);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops cs53l32a_core_ops = {
	.log_status = cs53l32a_log_status,
	.g_chip_ident = cs53l32a_g_chip_ident,
	.g_ctrl = cs53l32a_g_ctrl,
	.s_ctrl = cs53l32a_s_ctrl,
};

static const struct v4l2_subdev_audio_ops cs53l32a_audio_ops = {
	.s_routing = cs53l32a_s_routing,
};

static const struct v4l2_subdev_ops cs53l32a_ops = {
	.core = &cs53l32a_core_ops,
	.audio = &cs53l32a_audio_ops,
};

/* ----------------------------------------------------------------------- */

/* i2c implementation */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */

static int cs53l32a_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	int i;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	if (!id)
		strlcpy(client->name, "cs53l32a", sizeof(client->name));

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	sd = kmalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (sd == NULL)
		return -ENOMEM;
	v4l2_i2c_subdev_init(sd, client, &cs53l32a_ops);

	for (i = 1; i <= 7; i++) {
		u8 v = cs53l32a_read(sd, i);

		v4l2_dbg(1, debug, sd, "Read Reg %d %02x\n", i, v);
	}

	/* Set cs53l32a internal register for Adaptec 2010/2410 setup */

	cs53l32a_write(sd, 0x01, (u8) 0x21);
	cs53l32a_write(sd, 0x02, (u8) 0x29);
	cs53l32a_write(sd, 0x03, (u8) 0x30);
	cs53l32a_write(sd, 0x04, (u8) 0x00);
	cs53l32a_write(sd, 0x05, (u8) 0x00);
	cs53l32a_write(sd, 0x06, (u8) 0x00);
	cs53l32a_write(sd, 0x07, (u8) 0x00);

	/* Display results, should be 0x21,0x29,0x30,0x00,0x00,0x00,0x00 */

	for (i = 1; i <= 7; i++) {
		u8 v = cs53l32a_read(sd, i);

		v4l2_dbg(1, debug, sd, "Read Reg %d %02x\n", i, v);
	}
	return 0;
}

static int cs53l32a_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(sd);
	return 0;
}

static const struct i2c_device_id cs53l32a_id[] = {
	{ "cs53l32a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs53l32a_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "cs53l32a",
	.driverid = I2C_DRIVERID_CS53L32A,
	.command = cs53l32a_command,
	.remove = cs53l32a_remove,
	.probe = cs53l32a_probe,
	.id_table = cs53l32a_id,
};
