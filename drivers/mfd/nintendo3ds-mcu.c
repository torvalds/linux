/*
 * nintendo3ds-mcu.c  --  Nintendo 3DS MCU multi-function driver
 *
 *  Copyright (C) 2016 Sergi Granell <xerpi.g.12@gmail.com>
 *
 * Credits:
 *
 *    Using code from tps6507x.c
 *
 * For licencing details see kernel-base/COPYING
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/nintendo3ds-mcu.h>

static const struct mfd_cell nintendo3ds_mcu_devs[] = {
	{
		.name = "nintendo3ds-rtc",
		.of_compatible = "nintendo3ds,nintendo3ds-rtc"
	},
	{
		.name = "nintendo3ds-powercontrol",
		.of_compatible = "nintendo3ds,nintendo3ds-powercontrol"
	},
};

static int nintendo3ds_mcu_i2c_read_device(struct nintendo3ds_mcu_dev *nintendo3ds_mcu, char reg,
				  int bytes, void *dest)
{
	struct i2c_client *i2c = nintendo3ds_mcu->i2c_client;
	struct i2c_msg xfer[2];
	int ret;

	/* Select register */
	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = sizeof(reg);
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

static int nintendo3ds_mcu_i2c_write_device(struct nintendo3ds_mcu_dev *nintendo3ds_mcu, char reg,
				   int bytes, void *src)
{
	struct i2c_client *i2c = nintendo3ds_mcu->i2c_client;
	struct i2c_msg xfer[2];
	int ret;

	/* Select register */
	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = sizeof(reg);
	xfer[0].buf = &reg;

	/* Write data */
	xfer[1].addr = i2c->addr;
	xfer[1].flags = 0;
	xfer[1].len = bytes;
	xfer[1].buf = src;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (ret == 2)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

	return ret;
}

static int nintendo3ds_mcu_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct nintendo3ds_mcu_dev *nintendo3ds_mcu;

	nintendo3ds_mcu = devm_kzalloc(&i2c->dev, sizeof(struct nintendo3ds_mcu_dev),
				GFP_KERNEL);
	if (nintendo3ds_mcu == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, nintendo3ds_mcu);
	nintendo3ds_mcu->dev = &i2c->dev;
	nintendo3ds_mcu->i2c_client = i2c;
	nintendo3ds_mcu->read_device = nintendo3ds_mcu_i2c_read_device;
	nintendo3ds_mcu->write_device = nintendo3ds_mcu_i2c_write_device;

	return mfd_add_devices(nintendo3ds_mcu->dev, -1, nintendo3ds_mcu_devs,
			       ARRAY_SIZE(nintendo3ds_mcu_devs), NULL, 0, NULL);
}

static int nintendo3ds_mcu_i2c_remove(struct i2c_client *i2c)
{
	struct nintendo3ds_mcu_dev *nintendo3ds_mcu = i2c_get_clientdata(i2c);

	mfd_remove_devices(nintendo3ds_mcu->dev);
	return 0;
}

static const struct i2c_device_id nintendo3ds_mcu_i2c_id[] = {
       { "nintendo3ds-mcu", 0 },
       { }
};
MODULE_DEVICE_TABLE(i2c, nintendo3ds_mcu_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id nintendo3ds_mcu_of_match[] = {
	{.compatible = "nintendo3ds,nintendo3ds-mcu", },
	{},
};
MODULE_DEVICE_TABLE(of, nintendo3ds_mcu_of_match);
#endif

static struct i2c_driver nintendo3ds_mcu_i2c_driver = {
	.driver = {
		   .name = "nintendo3ds-mcu",
		   .of_match_table = of_match_ptr(nintendo3ds_mcu_of_match),
	},
	.probe = nintendo3ds_mcu_i2c_probe,
	.remove = nintendo3ds_mcu_i2c_remove,
	.id_table = nintendo3ds_mcu_i2c_id,
};

static int __init nintendo3ds_mcu_i2c_init(void)
{
	return i2c_add_driver(&nintendo3ds_mcu_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(nintendo3ds_mcu_i2c_init);

static void __exit nintendo3ds_mcu_i2c_exit(void)
{
	i2c_del_driver(&nintendo3ds_mcu_i2c_driver);
}
module_exit(nintendo3ds_mcu_i2c_exit);

MODULE_DESCRIPTION("Nintendo 3DS MCU multi-function driver");
MODULE_AUTHOR("Sergi Granell, <xerpi.g.12@gmail.com>");
MODULE_LICENSE("GPL");
