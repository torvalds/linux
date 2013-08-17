/*
 * sec-core.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mps11.h>
#include <linux/mfd/samsung/s5m8767.h>
#include <linux/mfd/samsung/rtc.h>
#include <linux/regmap.h>

static struct mfd_cell s5m8751_devs[] = {
	{
		.name = "s5m8751-pmic",
	}, {
		.name = "s5m-charger",
	}, {
		.name = "s5m8751-codec",
	},
};

static struct mfd_cell s5m8763_devs[] = {
	{
		.name = "s5m8763-pmic",
	}, {
		.name = "s5m-rtc",
	}, {
		.name = "s5m-charger",
	},
};

static struct mfd_cell s5m8767_devs[] = {
	{
		.name = "s5m8767-pmic",
	}, {
		.name = "s5m-rtc",
	},
};

static struct mfd_cell s2mps11_devs[] = {
	{
		.name = "s2mps11-pmic",
	}, {
		.name = "s2mps11-rtc",
	},
};

int sec_reg_read(struct sec_pmic_dev *sec_pmic, u8 reg, void *dest)
{
	return regmap_read(sec_pmic->regmap, reg, dest);
}
EXPORT_SYMBOL_GPL(sec_reg_read);

int sec_bulk_read(struct sec_pmic_dev *sec_pmic, u8 reg, int count, u8 *buf)
{
	return regmap_bulk_read(sec_pmic->regmap, reg, buf, count);
}
EXPORT_SYMBOL_GPL(sec_bulk_read);

int sec_reg_write(struct sec_pmic_dev *sec_pmic, u8 reg, u8 value)
{
	return regmap_write(sec_pmic->regmap, reg, value);
}
EXPORT_SYMBOL_GPL(sec_reg_write);

int sec_bulk_write(struct sec_pmic_dev *sec_pmic, u8 reg, int count, u8 *buf)
{
	return regmap_raw_write(sec_pmic->regmap, reg, buf, count);
}
EXPORT_SYMBOL_GPL(sec_bulk_write);

int sec_reg_update(struct sec_pmic_dev *sec_pmic, u8 reg, u8 val, u8 mask)
{
	return regmap_update_bits(sec_pmic->regmap, reg, mask, val);
}
EXPORT_SYMBOL_GPL(sec_reg_update);

int sec_rtc_read(struct sec_pmic_dev *sec_pmic, u8 reg, void *dest)
{
	return regmap_read(sec_pmic->rtc_regmap, reg, dest);
}
EXPORT_SYMBOL_GPL(sec_rtc_read);

int sec_rtc_bulk_read(struct sec_pmic_dev *sec_pmic, u8 reg, int count, u8 *buf)
{
	return regmap_bulk_read(sec_pmic->rtc_regmap, reg, buf, count);
}
EXPORT_SYMBOL_GPL(sec_rtc_bulk_read);

int sec_rtc_write(struct sec_pmic_dev *sec_pmic, u8 reg, u8 value)
{
	return regmap_write(sec_pmic->rtc_regmap, reg, value);
}
EXPORT_SYMBOL_GPL(sec_rtc_write);

int sec_rtc_bulk_write(struct sec_pmic_dev *sec_pmic, u8 reg, int count, u8 *buf)
{
	return regmap_raw_write(sec_pmic->rtc_regmap, reg, buf, count);
}
EXPORT_SYMBOL_GPL(sec_rtc_bulk_write);

int sec_rtc_update(struct sec_pmic_dev *sec_pmic, u8 reg, unsigned int val,
		unsigned int mask)
{
	return regmap_update_bits(sec_pmic->rtc_regmap, reg, mask, val);
}
EXPORT_SYMBOL_GPL(sec_rtc_update);

static struct regmap_config sec_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int sec_pmic_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct sec_pmic_platform_data *pdata = i2c->dev.platform_data;
	struct sec_pmic_dev *sec_pmic;
	int ret = 0;

	sec_pmic = devm_kzalloc(&i2c->dev, sizeof(struct sec_pmic_dev),
				GFP_KERNEL);
	if (sec_pmic == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, sec_pmic);
	sec_pmic->dev = &i2c->dev;
	sec_pmic->i2c = i2c;
	sec_pmic->irq = i2c->irq;
	sec_pmic->type = id->driver_data;

	if (pdata) {
		sec_pmic->device_type = pdata->device_type;
		sec_pmic->ono = pdata->ono;
		sec_pmic->irq_base = pdata->irq_base;
		sec_pmic->wakeup = pdata->wakeup;
	}

	sec_pmic->regmap = regmap_init_i2c(i2c, &sec_regmap_config);
	if (IS_ERR(sec_pmic->regmap)) {
		ret = PTR_ERR(sec_pmic->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		goto err4;
	}
	mutex_init(&sec_pmic->iolock);
	sec_pmic->rtc = i2c_new_dummy(i2c->adapter, RTC_I2C_ADDR);
	if (IS_ERR(sec_pmic->rtc)) {
		ret = PTR_ERR(sec_pmic->rtc);
		dev_err(&i2c->dev, "Address %02x unavailable\n", RTC_I2C_ADDR);
		goto err3;
	}

	i2c_set_clientdata(sec_pmic->rtc, sec_pmic);
	sec_pmic->rtc_regmap = regmap_init_i2c(sec_pmic->rtc, &sec_regmap_config);
	if (IS_ERR(sec_pmic->rtc_regmap)) {
		ret = PTR_ERR(sec_pmic->rtc_regmap);
		dev_err(&sec_pmic->rtc->dev,
				"Failed to allocate register map: %d\n", ret);
		goto err2;
	}

	if (pdata && pdata->cfg_pmic_irq)
		pdata->cfg_pmic_irq();

	sec_irq_init(sec_pmic);

	pm_runtime_set_active(sec_pmic->dev);

	switch (sec_pmic->device_type) {
	case S5M8751X:
		ret = mfd_add_devices(sec_pmic->dev, -1, s5m8751_devs,
					ARRAY_SIZE(s5m8751_devs), NULL, 0);
		break;
	case S5M8763X:
		ret = mfd_add_devices(sec_pmic->dev, -1, s5m8763_devs,
					ARRAY_SIZE(s5m8763_devs), NULL, 0);
		break;
	case S5M8767X:
		ret = mfd_add_devices(sec_pmic->dev, -1, s5m8767_devs,
					ARRAY_SIZE(s5m8767_devs), NULL, 0);
		break;
	case S2MPS11X:
		ret = mfd_add_devices(sec_pmic->dev, -1, s2mps11_devs,
					ARRAY_SIZE(s2mps11_devs), NULL, 0);
		break;
	default:
		/* If this happens the probe function is problem */
		BUG();
	}

	if (ret < 0)
		goto err1;

	return ret;

err1:
	sec_irq_exit(sec_pmic);
	regmap_exit(sec_pmic->rtc_regmap);
err2:
	i2c_unregister_device(sec_pmic->rtc);
err3:
	regmap_exit(sec_pmic->regmap);
err4:
	return ret;
}

static int sec_pmic_remove(struct i2c_client *i2c)
{
	struct sec_pmic_dev *sec_pmic = i2c_get_clientdata(i2c);

	mfd_remove_devices(sec_pmic->dev);
	sec_irq_exit(sec_pmic);
	regmap_exit(sec_pmic->rtc_regmap);
	i2c_unregister_device(sec_pmic->rtc);
	regmap_exit(sec_pmic->regmap);
	return 0;
}

static const struct i2c_device_id sec_pmic_id[] = {
	{ "sec-pmic", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sec_pmic_id);

#ifdef CONFIG_PM
static int sec_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct sec_pmic_dev *sec_pmic = i2c_get_clientdata(i2c);

	if (sec_pmic->wakeup)
		enable_irq_wake(sec_pmic->irq);

	disable_irq(sec_pmic->irq);
	return 0;
}

static int sec_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct sec_pmic_dev *sec_pmic = i2c_get_clientdata(i2c);

	if (sec_pmic->wakeup)
		disable_irq_wake(sec_pmic->irq);

	enable_irq(sec_pmic->irq);
	return 0;
}
#else
#define sec_suspend	NULL
#define sec_resume	NULL
#endif /* CONFIG_PM */

const struct dev_pm_ops sec_pmic_apm = {
	.suspend = sec_suspend,
	.resume = sec_resume,
};

static struct i2c_driver sec_pmic_driver = {
	.driver = {
		   .name = "sec-pmic",
		   .owner = THIS_MODULE,
		   .pm = &sec_pmic_apm,
	},
	.probe = sec_pmic_probe,
	.remove = sec_pmic_remove,
	.id_table = sec_pmic_id,
};

static int __init sec_pmic_init(void)
{
	return i2c_add_driver(&sec_pmic_driver);
}

subsys_initcall(sec_pmic_init);

static void __exit sec_pmic_exit(void)
{
	i2c_del_driver(&sec_pmic_driver);
}
module_exit(sec_pmic_exit);

MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("Core support for the SAMSUNG MFD");
MODULE_LICENSE("GPL");
