/*
 *  tsl2561.c - Linux kernel modules for light to digital convertor
 *
 *  Copyright (C) 2008-2009 Jonathan Cameron <jic23@cam.ac.uk>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Some portions based upon the tsl2550 driver.
 *
 *  This driver could probably be adapted easily to talk to the tsl2560 (smbus)
 *
 *  Needs some work to support the events this can generate.
 *  Todo: Implement interrupt handling.  Currently a hardware bug means
 *  this isn't available on my test board.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include "../iio.h"
#include "../sysfs.h"
#include "light.h"

#define TSL2561_CONTROL_REGISTER 0x00
#define TSL2561_TIMING_REGISTER 0x01
#define TSL2561_THRESHLOW_LOW_REGISTER 0x02
#define TSL2561_THRESHLOW_HIGH_REGISTER 0x03
#define TSL2561_THRESHHIGH_LOW_REGISTER 0x04
#define TSL2561_THRESHHIGH_HIGH_REGISTER 0x05
#define TSL2561_INT_CONTROL_REGISTER 0x06

#define TSL2561_INT_REG_INT_OFF 0x00
#define TSL2561_INT_REG_INT_LEVEL 0x08
#define TSL2561_INT_REG_INT_SMBUS 0x10
#define TSL2561_INT_REG_INT_TEST 0x18

#define TSL2561_ID_REGISTER 0x0A

#define TSL2561_DATA_0_LOW 0x0C
#define TSL2561_DATA_1_LOW 0x0E

/* Control Register Values */
#define TSL2561_CONT_REG_PWR_ON 0x03
#define TSL2561_CONT_REG_PWR_OFF 0x00

/**
 * struct tsl2561_state - device specific state
 * @indio_dev:		the industrialio I/O info structure
 * @client:		i2c client
 * @command_buf:	single command buffer used for all operations
 * @command_buf_lock:	ensure unique access to command_buf
 */
struct tsl2561_state {
	struct iio_dev		*indio_dev;
	struct i2c_client	*client;
	struct tsl2561_command	*command_buf;
	struct mutex		command_buf_lock;
};

/**
 * struct tsl2561_command - command byte for smbus
 * @address:	register address
 * @block:	is this a block r/w
 * @word:	is this a word r/w
 * @clear:	set to 1 to clear pending interrupt
 * @cmd:	select the command register - always 1.
 */
struct tsl2561_command {
	unsigned int address:4;
	unsigned int block:1;
	unsigned int word:1;
	unsigned int clear:1;
	unsigned int cmd:1;
};

static inline void tsl2561_init_command_buf(struct tsl2561_command *buf)
{
	buf->address = 0;
	buf->block = 0;
	buf->word = 0;
	buf->clear = 0;
	buf->cmd = 1;
}

static ssize_t tsl2561_read_val(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int ret = 0, data;
	ssize_t len = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct tsl2561_state *st = indio_dev->dev_data;

	mutex_lock(&st->command_buf_lock);
	st->command_buf->cmd = 1;
	st->command_buf->word = 1;
	st->command_buf->address = this_attr->address;

	data = i2c_smbus_read_word_data(st->client, *(char *)(st->command_buf));
	if (data < 0) {
		ret = data;
		goto error_ret;
	}
	len = sprintf(buf, "%u\n", data);

error_ret:
	mutex_unlock(&st->command_buf_lock);

	return ret ? ret : len;
}

static IIO_DEV_ATTR_LIGHT_INFRARED(0, tsl2561_read_val, TSL2561_DATA_0_LOW);
static IIO_DEV_ATTR_LIGHT_BROAD(0, tsl2561_read_val, TSL2561_DATA_1_LOW);

static struct attribute *tsl2561_attributes[] = {
	&iio_dev_attr_light_infrared0.dev_attr.attr,
	&iio_dev_attr_light_broadspectrum0.dev_attr.attr,
	NULL,
};

static const struct attribute_group tsl2561_attribute_group = {
	.attrs = tsl2561_attributes,
};

static int tsl2561_initialize(struct tsl2561_state *st)
{
	int err;

	mutex_lock(&st->command_buf_lock);
	st->command_buf->word = 0;
	st->command_buf->block = 0;
	st->command_buf->address = TSL2561_CONTROL_REGISTER;
	err = i2c_smbus_write_byte_data(st->client, *(char *)(st->command_buf),
					TSL2561_CONT_REG_PWR_ON);
	if (err)
		goto error_ret;

	st->command_buf->address = TSL2561_INT_CONTROL_REGISTER;
	err = i2c_smbus_write_byte_data(st->client, *(char *)(st->command_buf),
					TSL2561_INT_REG_INT_TEST);

error_ret:
	mutex_unlock(&st->command_buf_lock);

	return err;
}

static int tsl2561_powerdown(struct i2c_client *client)
{
	int err;
	struct tsl2561_command Command = {
		.cmd =  1,
		.clear = 0,
		.word = 0,
		.block = 0,
		.address = TSL2561_CONTROL_REGISTER,
	};

	err = i2c_smbus_write_byte_data(client, *(char *)(&Command),
					TSL2561_CONT_REG_PWR_OFF);
	return (err < 0) ? err : 0;
}
static int __devinit tsl2561_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	int ret = 0, regdone = 0;
	struct tsl2561_state *st = kzalloc(sizeof(*st), GFP_KERNEL);

	if (st == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	i2c_set_clientdata(client, st);
	st->client = client;
	mutex_init(&st->command_buf_lock);

	st->command_buf = kmalloc(sizeof(*st->command_buf), GFP_KERNEL);
	if (st->command_buf == NULL) {
		ret = -ENOMEM;
		goto error_free_state;
	}
	tsl2561_init_command_buf(st->command_buf);

	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_command_buf;
	}
	st->indio_dev->attrs = &tsl2561_attribute_group;
	st->indio_dev->dev.parent = &client->dev;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;
	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_free_iiodev;
	regdone = 1;
	/* Intialize the chip */
	ret = tsl2561_initialize(st);
	if (ret)
		goto error_unregister_iiodev;

	return 0;
error_unregister_iiodev:
error_free_iiodev:
	if (regdone)
		iio_device_unregister(st->indio_dev);
	else
		iio_free_device(st->indio_dev);
error_free_command_buf:
	kfree(st->command_buf);
error_free_state:
	kfree(st);
error_ret:
	return ret;

}

static int __devexit tsl2561_remove(struct i2c_client *client)
{
	struct tsl2561_state *st =  i2c_get_clientdata(client);

	iio_device_unregister(st->indio_dev);
	kfree(st);

	return tsl2561_powerdown(client);
}

static const struct i2c_device_id tsl2561_id[] = {
	{ "tsl2561", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tsl2561_id);


static struct i2c_driver tsl2561_driver = {
	.driver = {
		.name = "tsl2561",
	},
	.probe = tsl2561_probe,
	.remove = __devexit_p(tsl2561_remove),
	.id_table  = tsl2561_id,
};

static __init int tsl2561_init(void)
{
	return i2c_add_driver(&tsl2561_driver);
}
module_init(tsl2561_init);

static __exit void tsl2561_exit(void)
{
	i2c_del_driver(&tsl2561_driver);
}
module_exit(tsl2561_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@cam.ac.uk>");
MODULE_DESCRIPTION("TSL2561 light sensor driver");
MODULE_LICENSE("GPL");
