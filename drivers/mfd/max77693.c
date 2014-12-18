/*
 * max77693.c - mfd core driver for the MAX 77693
 *
 * Copyright (C) 2012 Samsung Electronics
 * SangYoung Son <hello.son@smasung.com>
 *
 * This program is not provided / owned by Maxim Integrated Products.
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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77693.h>
#include <linux/mfd/max77693-private.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>

#define I2C_ADDR_PMIC	(0xCC >> 1)	/* Charger, Flash LED */
#define I2C_ADDR_MUIC	(0x4A >> 1)
#define I2C_ADDR_HAPTIC	(0x90 >> 1)

static struct mfd_cell max77693_devs[] = {
	{ .name = "max77693-pmic", },
	{ .name = "max77693-charger", },
	{ .name = "max77693-flash", },
	{ .name = "max77693-muic", },
	{ .name = "max77693-haptic", },
};

int max77693_read_reg(struct regmap *map, u8 reg, u8 *dest)
{
	unsigned int val;
	int ret;

	ret = regmap_read(map, reg, &val);
	*dest = val;

	return ret;
}
EXPORT_SYMBOL_GPL(max77693_read_reg);

int max77693_bulk_read(struct regmap *map, u8 reg, int count, u8 *buf)
{
	int ret;

	ret = regmap_bulk_read(map, reg, buf, count);

	return ret;
}
EXPORT_SYMBOL_GPL(max77693_bulk_read);

int max77693_write_reg(struct regmap *map, u8 reg, u8 value)
{
	int ret;

	ret = regmap_write(map, reg, value);

	return ret;
}
EXPORT_SYMBOL_GPL(max77693_write_reg);

int max77693_bulk_write(struct regmap *map, u8 reg, int count, u8 *buf)
{
	int ret;

	ret = regmap_bulk_write(map, reg, buf, count);

	return ret;
}
EXPORT_SYMBOL_GPL(max77693_bulk_write);

int max77693_update_reg(struct regmap *map, u8 reg, u8 val, u8 mask)
{
	int ret;

	ret = regmap_update_bits(map, reg, mask, val);

	return ret;
}
EXPORT_SYMBOL_GPL(max77693_update_reg);

static const struct regmap_config max77693_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77693_PMIC_REG_END,
};

static int max77693_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct max77693_dev *max77693;
	struct max77693_platform_data *pdata = i2c->dev.platform_data;
	u8 reg_data;
	int ret = 0;

	if (!pdata) {
		dev_err(&i2c->dev, "No platform data found.\n");
		return -EINVAL;
	}

	max77693 = devm_kzalloc(&i2c->dev,
			sizeof(struct max77693_dev), GFP_KERNEL);
	if (max77693 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max77693);
	max77693->dev = &i2c->dev;
	max77693->i2c = i2c;
	max77693->irq = i2c->irq;
	max77693->type = id->driver_data;

	max77693->regmap = devm_regmap_init_i2c(i2c, &max77693_regmap_config);
	if (IS_ERR(max77693->regmap)) {
		ret = PTR_ERR(max77693->regmap);
		dev_err(max77693->dev, "failed to allocate register map: %d\n",
				ret);
		return ret;
	}

	max77693->wakeup = pdata->wakeup;

	ret = max77693_read_reg(max77693->regmap, MAX77693_PMIC_REG_PMIC_ID2,
				&reg_data);
	if (ret < 0) {
		dev_err(max77693->dev, "device not found on this channel\n");
		return ret;
	} else
		dev_info(max77693->dev, "device ID: 0x%x\n", reg_data);

	max77693->muic = i2c_new_dummy(i2c->adapter, I2C_ADDR_MUIC);
	if (!max77693->muic) {
		dev_err(max77693->dev, "Failed to allocate I2C device for MUIC\n");
		return -ENODEV;
	}
	i2c_set_clientdata(max77693->muic, max77693);

	max77693->haptic = i2c_new_dummy(i2c->adapter, I2C_ADDR_HAPTIC);
	if (!max77693->haptic) {
		dev_err(max77693->dev, "Failed to allocate I2C device for Haptic\n");
		ret = -ENODEV;
		goto err_i2c_haptic;
	}
	i2c_set_clientdata(max77693->haptic, max77693);

	/*
	 * Initialize register map for MUIC device because use regmap-muic
	 * instance of MUIC device when irq of max77693 is initialized
	 * before call max77693-muic probe() function.
	 */
	max77693->regmap_muic = devm_regmap_init_i2c(max77693->muic,
					 &max77693_regmap_config);
	if (IS_ERR(max77693->regmap_muic)) {
		ret = PTR_ERR(max77693->regmap_muic);
		dev_err(max77693->dev,
			"failed to allocate register map: %d\n", ret);
		goto err_regmap_muic;
	}

	ret = max77693_irq_init(max77693);
	if (ret < 0)
		goto err_irq;

	pm_runtime_set_active(max77693->dev);

	ret = mfd_add_devices(max77693->dev, -1, max77693_devs,
			      ARRAY_SIZE(max77693_devs), NULL, 0, NULL);
	if (ret < 0)
		goto err_mfd;

	device_init_wakeup(max77693->dev, pdata->wakeup);

	return ret;

err_mfd:
	max77693_irq_exit(max77693);
err_irq:
err_regmap_muic:
	i2c_unregister_device(max77693->haptic);
err_i2c_haptic:
	i2c_unregister_device(max77693->muic);
	return ret;
}

static int max77693_i2c_remove(struct i2c_client *i2c)
{
	struct max77693_dev *max77693 = i2c_get_clientdata(i2c);

	mfd_remove_devices(max77693->dev);
	max77693_irq_exit(max77693);
	i2c_unregister_device(max77693->muic);
	i2c_unregister_device(max77693->haptic);

	return 0;
}

static const struct i2c_device_id max77693_i2c_id[] = {
	{ "max77693", TYPE_MAX77693 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max77693_i2c_id);

static int max77693_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max77693_dev *max77693 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		irq_set_irq_wake(max77693->irq, 1);
	return 0;
}

static int max77693_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max77693_dev *max77693 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		irq_set_irq_wake(max77693->irq, 0);
	return max77693_irq_resume(max77693);
}

static const struct dev_pm_ops max77693_pm = {
	.suspend = max77693_suspend,
	.resume = max77693_resume,
};

static struct i2c_driver max77693_i2c_driver = {
	.driver = {
		   .name = "max77693",
		   .owner = THIS_MODULE,
		   .pm = &max77693_pm,
	},
	.probe = max77693_i2c_probe,
	.remove = max77693_i2c_remove,
	.id_table = max77693_i2c_id,
};

static int __init max77693_i2c_init(void)
{
	return i2c_add_driver(&max77693_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(max77693_i2c_init);

static void __exit max77693_i2c_exit(void)
{
	i2c_del_driver(&max77693_i2c_driver);
}
module_exit(max77693_i2c_exit);

MODULE_DESCRIPTION("MAXIM 77693 multi-function core driver");
MODULE_AUTHOR("SangYoung, Son <hello.son@samsung.com>");
MODULE_LICENSE("GPL");
