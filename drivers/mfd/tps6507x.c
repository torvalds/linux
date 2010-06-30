/*
 * tps6507x.c  --  TPS6507x chip family multi-function driver
 *
 *  Copyright (c) 2010 RidgeRun (todd.fischer@ridgerun.com)
 *
 * Author: Todd Fischer
 *         todd.fischer@ridgerun.com
 *
 * Credits:
 *
 *    Using code from wm831x-*.c, wm8400-core, Wolfson Microelectronics PLC.
 *
 * For licencing details see kernel-base/COPYING
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps6507x.h>

static struct mfd_cell tps6507x_devs[] = {
	{
		.name = "tps6507x-pmic",
	},
	{
		.name = "tps6507x-ts",
	},
};


static int tps6507x_i2c_read_device(struct tps6507x_dev *tps6507x, char reg,
				  int bytes, void *dest)
{
	struct i2c_client *i2c = tps6507x->i2c_client;
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

static int tps6507x_i2c_write_device(struct tps6507x_dev *tps6507x, char reg,
				   int bytes, void *src)
{
	struct i2c_client *i2c = tps6507x->i2c_client;
	/* we add 1 byte for device register */
	u8 msg[TPS6507X_MAX_REGISTER + 1];
	int ret;

	if (bytes > (TPS6507X_MAX_REGISTER + 1))
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

static int tps6507x_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct tps6507x_dev *tps6507x;
	int ret = 0;

	tps6507x = kzalloc(sizeof(struct tps6507x_dev), GFP_KERNEL);
	if (tps6507x == NULL) {
		kfree(i2c);
		return -ENOMEM;
	}

	i2c_set_clientdata(i2c, tps6507x);
	tps6507x->dev = &i2c->dev;
	tps6507x->i2c_client = i2c;
	tps6507x->read_dev = tps6507x_i2c_read_device;
	tps6507x->write_dev = tps6507x_i2c_write_device;

	ret = mfd_add_devices(tps6507x->dev, -1,
			      tps6507x_devs, ARRAY_SIZE(tps6507x_devs),
			      NULL, 0);

	if (ret < 0)
		goto err;

	return ret;

err:
	mfd_remove_devices(tps6507x->dev);
	kfree(tps6507x);
	return ret;
}

static int tps6507x_i2c_remove(struct i2c_client *i2c)
{
	struct tps6507x_dev *tps6507x = i2c_get_clientdata(i2c);

	mfd_remove_devices(tps6507x->dev);
	kfree(tps6507x);

	return 0;
}

static const struct i2c_device_id tps6507x_i2c_id[] = {
       { "tps6507x", 0 },
       { }
};
MODULE_DEVICE_TABLE(i2c, tps6507x_i2c_id);


static struct i2c_driver tps6507x_i2c_driver = {
	.driver = {
		   .name = "tps6507x",
		   .owner = THIS_MODULE,
	},
	.probe = tps6507x_i2c_probe,
	.remove = tps6507x_i2c_remove,
	.id_table = tps6507x_i2c_id,
};

static int __init tps6507x_i2c_init(void)
{
	return i2c_add_driver(&tps6507x_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(tps6507x_i2c_init);

static void __exit tps6507x_i2c_exit(void)
{
	i2c_del_driver(&tps6507x_i2c_driver);
}
module_exit(tps6507x_i2c_exit);

MODULE_DESCRIPTION("TPS6507x chip family multi-function driver");
MODULE_LICENSE("GPL");
