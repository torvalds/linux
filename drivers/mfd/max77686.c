/*
 * max77686.c - mfd core driver for the Maxim 77686
 *
 * Copyright (C) 2012 Samsung Electronics
 * Chiwoong Byun <woong.byun@smasung.com>
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
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
 *
 * This driver is based on max8997.c
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>
#include <linux/err.h>

#define I2C_ADDR_RTC	(0x0C >> 1)

static struct mfd_cell max77686_devs[] = {
	{ .name = "max77686-pmic", },
	{ .name = "max77686-rtc", },
};

static struct regmap_config max77686_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

#ifdef CONFIG_OF
static struct of_device_id __devinitdata max77686_pmic_dt_match[] = {
	{.compatible = "maxim,max77686",        .data = 0},
	{},
};

static struct max77686_platform_data *max77686_i2c_parse_dt_pdata(struct device
								  *dev)
{
	struct max77686_platform_data *pd;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		dev_err(dev, "could not allocate memory for pdata\n");
		return NULL;
	}

	dev->platform_data = pd;
	return pd;
}
#else
static struct max77686_platform_data *max77686_i2c_parse_dt_pdata(struct device
								  *dev)
{
	return 0;
}
#endif

static int max77686_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct max77686_dev *max77686 = NULL;
	struct max77686_platform_data *pdata = i2c->dev.platform_data;
	unsigned int data;
	int ret = 0;

	if (i2c->dev.of_node)
		pdata = max77686_i2c_parse_dt_pdata(&i2c->dev);

	if (!pdata) {
		ret = -EIO;
		dev_err(&i2c->dev, "No platform data found.\n");
		goto err;
	}

	max77686 = kzalloc(sizeof(struct max77686_dev), GFP_KERNEL);
	if (max77686 == NULL)
		return -ENOMEM;

	max77686->regmap = regmap_init_i2c(i2c, &max77686_regmap_config);
	if (IS_ERR(max77686->regmap)) {
		ret = PTR_ERR(max77686->regmap);
		dev_err(max77686->dev, "Failed to allocate register map: %d\n",
				ret);
		kfree(max77686);
		return ret;
	}

	i2c_set_clientdata(i2c, max77686);
	max77686->dev = &i2c->dev;
	max77686->i2c = i2c;
	max77686->type = id->driver_data;

	max77686->wakeup = pdata->wakeup;
	max77686->irq_gpio = pdata->irq_gpio;
	max77686->irq = i2c->irq;

	if (regmap_read(max77686->regmap,
			 MAX77686_REG_DEVICE_ID, &data) < 0) {
		dev_err(max77686->dev,
			"device not found on this channel (this is not an error)\n");
		ret = -ENODEV;
		goto err;
	} else
		dev_info(max77686->dev, "device found\n");

	max77686->rtc = i2c_new_dummy(i2c->adapter, I2C_ADDR_RTC);
	i2c_set_clientdata(max77686->rtc, max77686);

	max77686_irq_init(max77686);

	ret = mfd_add_devices(max77686->dev, -1, max77686_devs,
			      ARRAY_SIZE(max77686_devs), NULL, 0);

	if (ret < 0)
		goto err_mfd;

	return ret;

err_mfd:
	mfd_remove_devices(max77686->dev);
	i2c_unregister_device(max77686->rtc);
err:
	kfree(max77686);
	return ret;
}

static int max77686_i2c_remove(struct i2c_client *i2c)
{
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);

	mfd_remove_devices(max77686->dev);
	i2c_unregister_device(max77686->rtc);
	kfree(max77686);

	return 0;
}

static const struct i2c_device_id max77686_i2c_id[] = {
	{ "max77686", TYPE_MAX77686 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max77686_i2c_id);

static struct i2c_driver max77686_i2c_driver = {
	.driver = {
		   .name = "max77686",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(max77686_pmic_dt_match),
	},
	.probe = max77686_i2c_probe,
	.remove = max77686_i2c_remove,
	.id_table = max77686_i2c_id,
};

static int __init max77686_i2c_init(void)
{
	return i2c_add_driver(&max77686_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(max77686_i2c_init);

static void __exit max77686_i2c_exit(void)
{
	i2c_del_driver(&max77686_i2c_driver);
}
module_exit(max77686_i2c_exit);

MODULE_DESCRIPTION("MAXIM 77686 multi-function core driver");
MODULE_AUTHOR("Chiwoong Byun <woong.byun@samsung.com>");
MODULE_LICENSE("GPL");
