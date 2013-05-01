/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/tvaudio.h>
#include <media/v4l2-common.h>

#include "wis-i2c.h"

static int write_reg(struct i2c_client *client, int reg, int value)
{
	/* UDA1342 wants MSB first, but SMBus sends LSB first */
	i2c_smbus_write_word_data(client, reg, swab16(value));
	return 0;
}

static int wis_uda1342_command(struct i2c_client *client,
				unsigned int cmd, void *arg)
{
	switch (cmd) {
	case VIDIOC_S_AUDIO:
	{
		int *inp = arg;

		switch (*inp) {
		case TVAUDIO_INPUT_TUNER:
			write_reg(client, 0x00, 0x1441); /* select input 2 */
			break;
		case TVAUDIO_INPUT_EXTERN:
			write_reg(client, 0x00, 0x1241); /* select input 1 */
			break;
		default:
			dev_err(&client->dev, "input %d not supported\n",
				*inp);
			break;
		}
		break;
	}
	default:
		break;
	}
	return 0;
}

static int wis_uda1342_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	dev_dbg(&client->dev, "initializing UDA1342 at address %d on %s\n",
		client->addr, adapter->name);

	write_reg(client, 0x00, 0x8000); /* reset registers */
	write_reg(client, 0x00, 0x1241); /* select input 1 */

	return 0;
}

static int wis_uda1342_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id wis_uda1342_id[] = {
	{ "wis_uda1342", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wis_uda1342_id);

static struct i2c_driver wis_uda1342_driver = {
	.driver = {
		.name	= "WIS UDA1342 I2C driver",
	},
	.probe		= wis_uda1342_probe,
	.remove		= wis_uda1342_remove,
	.command	= wis_uda1342_command,
	.id_table	= wis_uda1342_id,
};

module_i2c_driver(wis_uda1342_driver);

MODULE_LICENSE("GPL v2");
