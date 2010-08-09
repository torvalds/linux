/*
 * max8698.c - mfd core driver for the Maxim 8998
 *
 *  Copyright (C) 2009-2010 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *  Marek Szyprowski <m.szyprowski@samsung.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max8998.h>
#include <linux/mfd/max8998-private.h>

static struct mfd_cell max8998_devs[] = {
	{
		.name = "max8998-pmic",
	}
};

static int max8998_i2c_device_read(struct max8998_dev *max8998, u8 reg, u8 *dest)
{
	struct i2c_client *client = max8998->i2c_client;
	int ret;

	mutex_lock(&max8998->iolock);
	ret = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&max8998->iolock);
	if (ret < 0)
		return ret;

	ret &= 0xff;
	*dest = ret;
	return 0;
}

static int max8998_i2c_device_write(struct max8998_dev *max8998, u8 reg, u8 value)
{
	struct i2c_client *client = max8998->i2c_client;
	int ret;

	mutex_lock(&max8998->iolock);
	ret = i2c_smbus_write_byte_data(client, reg, value);
	mutex_unlock(&max8998->iolock);
	return ret;
}

static int max8998_i2c_device_update(struct max8998_dev *max8998, u8 reg,
				     u8 val, u8 mask)
{
	struct i2c_client *client = max8998->i2c_client;
	int ret;

	mutex_lock(&max8998->iolock);
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret >= 0) {
		u8 old_val = ret & 0xff;
		u8 new_val = (val & mask) | (old_val & (~mask));
		ret = i2c_smbus_write_byte_data(client, reg, new_val);
		if (ret >= 0)
			ret = 0;
	}
	mutex_unlock(&max8998->iolock);
	return ret;
}

static int max8998_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct max8998_dev *max8998;
	int ret = 0;

	max8998 = kzalloc(sizeof(struct max8998_dev), GFP_KERNEL);
	if (max8998 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max8998);
	max8998->dev = &i2c->dev;
	max8998->i2c_client = i2c;
	max8998->dev_read = max8998_i2c_device_read;
	max8998->dev_write = max8998_i2c_device_write;
	max8998->dev_update = max8998_i2c_device_update;
	mutex_init(&max8998->iolock);

	ret = mfd_add_devices(max8998->dev, -1,
			      max8998_devs, ARRAY_SIZE(max8998_devs),
			      NULL, 0);
	if (ret < 0)
		goto err;

	return ret;

err:
	mfd_remove_devices(max8998->dev);
	kfree(max8998);
	return ret;
}

static int max8998_i2c_remove(struct i2c_client *i2c)
{
	struct max8998_dev *max8998 = i2c_get_clientdata(i2c);

	mfd_remove_devices(max8998->dev);
	kfree(max8998);

	return 0;
}

static const struct i2c_device_id max8998_i2c_id[] = {
       { "max8998", 0 },
       { }
};
MODULE_DEVICE_TABLE(i2c, max8998_i2c_id);

static struct i2c_driver max8998_i2c_driver = {
	.driver = {
		   .name = "max8998",
		   .owner = THIS_MODULE,
	},
	.probe = max8998_i2c_probe,
	.remove = max8998_i2c_remove,
	.id_table = max8998_i2c_id,
};

static int __init max8998_i2c_init(void)
{
	return i2c_add_driver(&max8998_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(max8998_i2c_init);

static void __exit max8998_i2c_exit(void)
{
	i2c_del_driver(&max8998_i2c_driver);
}
module_exit(max8998_i2c_exit);

MODULE_DESCRIPTION("MAXIM 8998 multi-function core driver");
MODULE_AUTHOR("Kyungmin Park <kyungmin.park@samsung.com>");
MODULE_LICENSE("GPL");
