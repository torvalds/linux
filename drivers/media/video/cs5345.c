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


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>

MODULE_DESCRIPTION("i2c device driver for cs5345 Audio ADC");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL");

static int debug;

module_param(debug, bool, 0644);

MODULE_PARM_DESC(debug, "Debugging messages, 0=Off (default), 1=On");


/* ----------------------------------------------------------------------- */

static inline int cs5345_write(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int cs5345_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte_data(client, reg);
}

static int cs5345_s_routing(struct v4l2_subdev *sd,
			    u32 input, u32 output, u32 config)
{
	if ((input & 0xf) > 6) {
		v4l2_err(sd, "Invalid input %d.\n", input);
		return -EINVAL;
	}
	cs5345_write(sd, 0x09, input & 0xf);
	cs5345_write(sd, 0x05, input & 0xf0);
	return 0;
}

static int cs5345_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	if (ctrl->id == V4L2_CID_AUDIO_MUTE) {
		ctrl->value = (cs5345_read(sd, 0x04) & 0x08) != 0;
		return 0;
	}
	if (ctrl->id != V4L2_CID_AUDIO_VOLUME)
		return -EINVAL;
	ctrl->value = cs5345_read(sd, 0x07) & 0x3f;
	if (ctrl->value >= 32)
		ctrl->value = ctrl->value - 64;
	return 0;
}

static int cs5345_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	if (ctrl->id == V4L2_CID_AUDIO_MUTE) {
		cs5345_write(sd, 0x04, ctrl->value ? 0x80 : 0);
		return 0;
	}
	if (ctrl->id != V4L2_CID_AUDIO_VOLUME)
		return -EINVAL;
	if (ctrl->value > 24 || ctrl->value < -24)
		return -EINVAL;
	cs5345_write(sd, 0x07, ((u8)ctrl->value) & 0x3f);
	cs5345_write(sd, 0x08, ((u8)ctrl->value) & 0x3f);
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int cs5345_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	reg->size = 1;
	reg->val = cs5345_read(sd, reg->reg & 0x1f);
	return 0;
}

static int cs5345_s_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	cs5345_write(sd, reg->reg & 0x1f, reg->val & 0xff);
	return 0;
}
#endif

static int cs5345_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_CS5345, 0);
}

static int cs5345_log_status(struct v4l2_subdev *sd)
{
	u8 v = cs5345_read(sd, 0x09) & 7;
	u8 m = cs5345_read(sd, 0x04);
	int vol = cs5345_read(sd, 0x08) & 0x3f;

	v4l2_info(sd, "Input:  %d%s\n", v,
			(m & 0x80) ? " (muted)" : "");
	if (vol >= 32)
		vol = vol - 64;
	v4l2_info(sd, "Volume: %d dB\n", vol);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops cs5345_core_ops = {
	.log_status = cs5345_log_status,
	.g_chip_ident = cs5345_g_chip_ident,
	.g_ctrl = cs5345_g_ctrl,
	.s_ctrl = cs5345_s_ctrl,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = cs5345_g_register,
	.s_register = cs5345_s_register,
#endif
};

static const struct v4l2_subdev_audio_ops cs5345_audio_ops = {
	.s_routing = cs5345_s_routing,
};

static const struct v4l2_subdev_ops cs5345_ops = {
	.core = &cs5345_core_ops,
	.audio = &cs5345_audio_ops,
};

/* ----------------------------------------------------------------------- */

static int cs5345_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	sd = kzalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (sd == NULL)
		return -ENOMEM;
	v4l2_i2c_subdev_init(sd, client, &cs5345_ops);

	cs5345_write(sd, 0x02, 0x00);
	cs5345_write(sd, 0x04, 0x01);
	cs5345_write(sd, 0x09, 0x01);
	return 0;
}

/* ----------------------------------------------------------------------- */

static int cs5345_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(sd);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id cs5345_id[] = {
	{ "cs5345", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs5345_id);

static struct i2c_driver cs5345_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "cs5345",
	},
	.probe		= cs5345_probe,
	.remove		= cs5345_remove,
	.id_table	= cs5345_id,
};

static __init int init_cs5345(void)
{
	return i2c_add_driver(&cs5345_driver);
}

static __exit void exit_cs5345(void)
{
	i2c_del_driver(&cs5345_driver);
}

module_init(init_cs5345);
module_exit(exit_cs5345);
