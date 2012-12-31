/*
 * s5m87xx.c
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
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/s5m87xx/s5m-core.h>
#include <linux/mfd/s5m87xx/s5m-pmic.h>
#include <linux/mfd/s5m87xx/s5m-rtc.h>

static struct mfd_cell s5m87xx_devs[] = {
	{
		.name = "s5m8767-pmic",
	}, {
		.name = "s5m8763-pmic",
	}, {
		.name = "s5m-rtc",
	},
};

int s5m_reg_read(struct i2c_client *i2c, u8 reg, u8 *dest)
{
	struct s5m87xx_dev *s5m87xx = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s5m87xx->iolock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	mutex_unlock(&s5m87xx->iolock);
	if (ret < 0)
		return ret;

	ret &= 0xff;
	*dest = ret;
	return 0;
}
EXPORT_SYMBOL(s5m_reg_read);

int s5m_bulk_read(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	struct s5m87xx_dev *s5m87xx = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s5m87xx->iolock);
	ret = i2c_smbus_read_i2c_block_data(i2c, reg, count, buf);
	mutex_unlock(&s5m87xx->iolock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(s5m_bulk_read);

int s5m_reg_write(struct i2c_client *i2c, u8 reg, u8 value)
{
	struct s5m87xx_dev *s5m87xx = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s5m87xx->iolock);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	mutex_unlock(&s5m87xx->iolock);
	return ret;
}
EXPORT_SYMBOL(s5m_reg_write);

int s5m_bulk_write(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	struct s5m87xx_dev *s5m87xx = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s5m87xx->iolock);
	ret = i2c_smbus_write_i2c_block_data(i2c, reg, count, buf);
	mutex_unlock(&s5m87xx->iolock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(s5m_bulk_write);

int s5m_reg_update(struct i2c_client *i2c, u8 reg, u8 val, u8 mask)
{
	struct s5m87xx_dev *s5m87xx = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s5m87xx->iolock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret >= 0) {
		u8 old_val = ret & 0xff;
		u8 new_val = (val & mask) | (old_val & (~mask));
		ret = i2c_smbus_write_byte_data(i2c, reg, new_val);
	}
	mutex_unlock(&s5m87xx->iolock);
	return ret;
}
EXPORT_SYMBOL(s5m_reg_update);

static int s5m87xx_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct s5m_platform_data *pdata = i2c->dev.platform_data;
	struct s5m87xx_dev *s5m87xx;
	int ret = 0;

	s5m87xx = kzalloc(sizeof(struct s5m87xx_dev), GFP_KERNEL);
	if (s5m87xx == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, s5m87xx);
	s5m87xx->dev = &i2c->dev;
	s5m87xx->i2c = i2c;
	s5m87xx->irq = i2c->irq;
	s5m87xx->type = id->driver_data;

	if (pdata) {
		s5m87xx->device_type = pdata->device_type;
		s5m87xx->ono = pdata->ono;
		s5m87xx->irq_base = pdata->irq_base;
		s5m87xx->wakeup = pdata->wakeup;
		s5m87xx->wtsr_smpl = pdata->wtsr_smpl;
	}

	mutex_init(&s5m87xx->iolock);

	s5m87xx->rtc = i2c_new_dummy(i2c->adapter, RTC_I2C_ADDR);
	i2c_set_clientdata(s5m87xx->rtc, s5m87xx);

	if (pdata && pdata->cfg_pmic_irq)
		pdata->cfg_pmic_irq();

	s5m_irq_init(s5m87xx);

	pm_runtime_set_active(s5m87xx->dev);

	ret = mfd_add_devices(s5m87xx->dev, -1,
				s5m87xx_devs, ARRAY_SIZE(s5m87xx_devs),
				NULL, 0);

	if (ret < 0)
		goto err;

	dev_info(s5m87xx->dev ,"S5M87xx MFD probe done!!! \n");
	return ret;

err:
	mfd_remove_devices(s5m87xx->dev);
	s5m_irq_exit(s5m87xx);
	i2c_unregister_device(s5m87xx->rtc);
	kfree(s5m87xx);
	return ret;
}

static int s5m87xx_i2c_remove(struct i2c_client *i2c)
{
	struct s5m87xx_dev *s5m87xx = i2c_get_clientdata(i2c);

	mfd_remove_devices(s5m87xx->dev);
	s5m_irq_exit(s5m87xx);
	i2c_unregister_device(s5m87xx->rtc);
	kfree(s5m87xx);

	return 0;
}

static const struct i2c_device_id s5m87xx_i2c_id[] = {
	{ "s5m87xx", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s5m87xx_i2c_id);

#ifdef CONFIG_PM
static int s5m_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct s5m87xx_dev *s5m87xx = i2c_get_clientdata(i2c);

	if (s5m87xx->wakeup)
		enable_irq_wake(s5m87xx->irq);

	disable_irq(s5m87xx->irq);

	return 0;
}

static int s5m_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct s5m87xx_dev *s5m87xx = i2c_get_clientdata(i2c);

	if (s5m87xx->wakeup)
		disable_irq_wake(s5m87xx->irq);

	enable_irq(s5m87xx->irq);

	return 0;
}
#else
#define s5m_suspend       NULL
#define s5m_resume                NULL
#endif /* CONFIG_PM */

const struct dev_pm_ops s5m87xx_apm = {
	.suspend = s5m_suspend,
	.resume = s5m_resume,
};

static struct i2c_driver s5m87xx_i2c_driver = {
	.driver = {
		   .name = "s5m87xx",
		   .owner = THIS_MODULE,
		   .pm = &s5m87xx_apm,
	},
	.probe = s5m87xx_i2c_probe,
	.remove = s5m87xx_i2c_remove,
	.id_table = s5m87xx_i2c_id,
};

static int __init s5m87xx_i2c_init(void)
{
	return i2c_add_driver(&s5m87xx_i2c_driver);
}

subsys_initcall(s5m87xx_i2c_init);

static void __exit s5m87xx_i2c_exit(void)
{
	i2c_del_driver(&s5m87xx_i2c_driver);
}
module_exit(s5m87xx_i2c_exit);

MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("Core support for the S5M87XX MFD");
MODULE_LICENSE("GPL");
