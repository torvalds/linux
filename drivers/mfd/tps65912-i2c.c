/*
 * tps65912-i2c.c  --  I2C access for TI TPS65912x PMIC
 *
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Margarita Olaya Cabrera <magi@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This driver is based on wm8350 implementation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps65912.h>

static int tps65912_i2c_read(struct tps65912 *tps65912, u8 reg,
				  int bytes, void *dest)
{
	struct i2c_client *i2c = tps65912->control_data;
	struct i2c_msg xfer[2];
	int ret;

	/* Write register */
	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &reg;

	/* Read data */
	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = bytes;
	xfer[1].buf = dest;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (ret == 2)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;
	return ret;
}

static int tps65912_i2c_write(struct tps65912 *tps65912, u8 reg,
				   int bytes, void *src)
{
	struct i2c_client *i2c = tps65912->control_data;
	/* we add 1 byte for device register */
	u8 msg[TPS6591X_MAX_REGISTER + 1];
	int ret;

	if (bytes > TPS6591X_MAX_REGISTER)
		return -EINVAL;

	msg[0] = reg;
	memcpy(&msg[1], src, bytes);

	ret = i2c_master_send(i2c, msg, bytes + 1);
	if (ret < 0)
		return ret;
	if (ret != bytes + 1)
		return -EIO;

	return 0;
}

static int tps65912_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct tps65912 *tps65912;

	tps65912 = kzalloc(sizeof(struct tps65912), GFP_KERNEL);
	if (tps65912 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, tps65912);
	tps65912->dev = &i2c->dev;
	tps65912->control_data = i2c;
	tps65912->read = tps65912_i2c_read;
	tps65912->write = tps65912_i2c_write;

	return tps65912_device_init(tps65912);
}

static int tps65912_i2c_remove(struct i2c_client *i2c)
{
	struct tps65912 *tps65912 = i2c_get_clientdata(i2c);

	tps65912_device_exit(tps65912);

	return 0;
}

static const struct i2c_device_id tps65912_i2c_id[] = {
	{"tps65912", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tps65912_i2c_id);

static struct i2c_driver tps65912_i2c_driver = {
	.driver = {
		   .name = "tps65912",
		   .owner = THIS_MODULE,
	},
	.probe = tps65912_i2c_probe,
	.remove = tps65912_i2c_remove,
	.id_table = tps65912_i2c_id,
};

static int __init tps65912_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&tps65912_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register TPS65912 I2C driver: %d\n", ret);

	return ret;
}
/* init early so consumer devices can complete system boot */
subsys_initcall(tps65912_i2c_init);

static void __exit tps65912_i2c_exit(void)
{
	i2c_del_driver(&tps65912_i2c_driver);
}
module_exit(tps65912_i2c_exit);

MODULE_AUTHOR("Margarita Olaya	<magi@slimlogic.co.uk>");
MODULE_DESCRIPTION("TPS6591x chip family multi-function driver");
MODULE_LICENSE("GPL");
