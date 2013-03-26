/*
 * max77686.c - mfd core driver for the Maxim 8966 and 8997
 *
 * Copyright (C) 2011 Samsung Electronics
 * MyungJoo Ham <myungjoo.ham@smasung.com>
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
 * This driver is based on max8998.c
 */

#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>

#define I2C_ADDR_PMIC	(0x12 >> 1)
#define I2C_ADDR_RTC	(0x0C >> 1)

static struct mfd_cell max77686_devs[] = {
	{ .name = "max77686-pmic", },
	{ .name = "max77686-rtc", },
};

int max77686_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest)
{
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&max77686->iolock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	mutex_unlock(&max77686->iolock);
	if (ret < 0)
		return ret;

	ret &= 0xff;
	*dest = ret;
	return 0;
}
EXPORT_SYMBOL_GPL(max77686_read_reg);

int max77686_bulk_read(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&max77686->iolock);
	ret = i2c_smbus_read_i2c_block_data(i2c, reg, count, buf);
	mutex_unlock(&max77686->iolock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(max77686_bulk_read);

int max77686_write_reg(struct i2c_client *i2c, u8 reg, u8 value)
{
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&max77686->iolock);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	mutex_unlock(&max77686->iolock);
	return ret;
}
EXPORT_SYMBOL_GPL(max77686_write_reg);

int max77686_bulk_write(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&max77686->iolock);
	ret = i2c_smbus_write_i2c_block_data(i2c, reg, count, buf);
	mutex_unlock(&max77686->iolock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(max77686_bulk_write);

int max77686_update_reg(struct i2c_client *i2c, u8 reg, u8 val, u8 mask)
{
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&max77686->iolock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret >= 0) {
		u8 old_val = ret & 0xff;
		u8 new_val = (val & mask) | (old_val & (~mask));
		ret = i2c_smbus_write_byte_data(i2c, reg, new_val);
	}
	mutex_unlock(&max77686->iolock);
	return ret;
}
EXPORT_SYMBOL_GPL(max77686_update_reg);

#if defined(CONFIG_MACH_ODROID_4X12) && defined(CONFIG_RTC_DRV_S3C)

#include <linux/delay.h>
#include <linux/rtc.h>
static	struct max77686_dev *gMax77686;

int max77686_rtc_gettime(struct rtc_time *rtc_tm)
{
    unsigned char tmp;
    
    // Reload Register
    max77686_write_reg(gMax77686->rtc, MAX77686_RTC_UPDATE0, 0x10);  mdelay(100);
    max77686_read_reg(gMax77686->rtc, MAX77686_RTC_SEC	, &tmp );   rtc_tm->tm_sec  = tmp;
    max77686_read_reg(gMax77686->rtc, MAX77686_RTC_MIN	, &tmp );   rtc_tm->tm_min  = tmp;
    max77686_read_reg(gMax77686->rtc, MAX77686_RTC_HOUR	, &tmp );   rtc_tm->tm_hour = tmp & 0x1F;

    if ((tmp & 0x40) && (tmp != 0x4C)) rtc_tm->tm_hour += 12;  // pm
    if (tmp == 0x0C) rtc_tm->tm_hour = 0; // midnight

    max77686_read_reg(gMax77686->rtc, MAX77686_RTC_DOM	, &tmp );   rtc_tm->tm_mday = tmp;
    max77686_read_reg(gMax77686->rtc, MAX77686_RTC_MONTH, &tmp );   rtc_tm->tm_mon  = tmp;
    max77686_read_reg(gMax77686->rtc, MAX77686_RTC_YEAR	, &tmp );   rtc_tm->tm_year = tmp;
	rtc_tm->tm_year += 100;

    printk("%s : %04d.%02d.%02d %02d:%02d:%02d\n", 
		 __func__, 1900 + rtc_tm->tm_year, rtc_tm->tm_mon, rtc_tm->tm_mday,
		 rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);

    rtc_tm->tm_mon -= 1;

    return  0;
}
EXPORT_SYMBOL_GPL(max77686_rtc_gettime);

int max77686_rtc_settime(struct rtc_time *tm)
{
	int year = tm->tm_year - 100;

	printk("%s : %04d.%02d.%02d %02d:%02d:%02d\n",
		 __func__, 1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec);

	max77686_write_reg(gMax77686->rtc, MAX77686_RTC_SEC	 , tm->tm_sec);
	max77686_write_reg(gMax77686->rtc, MAX77686_RTC_MIN	 , tm->tm_min);

	if (tm->tm_hour > 12) { tm->tm_hour = (tm->tm_hour % 12) | 0x40; } // pm
	if (tm->tm_hour == 12) { tm->tm_hour |= 0x40; } // midday
	if (tm->tm_hour == 0) tm->tm_hour = 0x0C; // midnight
		
	max77686_write_reg(gMax77686->rtc, MAX77686_RTC_HOUR , tm->tm_hour);
	max77686_write_reg(gMax77686->rtc, MAX77686_RTC_DOM	 , tm->tm_mday);
	max77686_write_reg(gMax77686->rtc, MAX77686_RTC_MONTH, tm->tm_mon + 1);
	max77686_write_reg(gMax77686->rtc, MAX77686_RTC_YEAR , year);

	max77686_write_reg(gMax77686->rtc, MAX77686_RTC_UPDATE0, 0x01);  mdelay(100);
	return 0;
}
EXPORT_SYMBOL_GPL(max77686_rtc_settime);
#endif

static int max77686_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct max77686_dev *max77686;
	struct max77686_platform_data *pdata = i2c->dev.platform_data;
	int ret = 0;

	max77686 = kzalloc(sizeof(struct max77686_dev), GFP_KERNEL);
	if (max77686 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max77686);
	max77686->dev = &i2c->dev;
	max77686->i2c = i2c;
	max77686->type = id->driver_data;
	max77686->irq = i2c->irq;

	if (!pdata)
		goto err;

	max77686->irq_base = pdata->irq_base;
	max77686->ono = pdata->ono;

	mutex_init(&max77686->iolock);

	max77686->rtc = i2c_new_dummy(i2c->adapter, I2C_ADDR_RTC);
	i2c_set_clientdata(max77686->rtc, max77686);

	pm_runtime_set_active(max77686->dev);

	max77686_irq_init(max77686);

	mfd_add_devices(max77686->dev, -1, max77686_devs,
			ARRAY_SIZE(max77686_devs),
			NULL, 0);

	/*
	 * TODO: enable others (flash, muic, rtc, battery, ...) and
	 * check the return value
	 */

	if (ret < 0)
		goto err_mfd;

	/* MAX77686 has a power button input. */
	device_init_wakeup(max77686->dev, pdata->wakeup);

#if defined(CONFIG_MACH_ODROID_4X12) && defined(CONFIG_RTC_DRV_S3C)
    gMax77686 = max77686;
#endif    

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
	{ "max77686", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max8998_i2c_id);

u8 max77686_dumpaddr_pmic[] = {
	MAX77686_REG_DEVICE_ID,
	MAX77686_REG_INTSRC,
	MAX77686_REG_INT1,
	MAX77686_REG_INT2,
	MAX77686_REG_INT1MSK,
	MAX77686_REG_INT2MSK,

	MAX77686_REG_STATUS1,
	MAX77686_REG_STATUS2,

	MAX77686_REG_PWRON,
	MAX77686_REG_ONOFFDELAY,
	MAX77686_REG_MRSTB,

	MAX77686_REG_BUCK1CTRL,
	MAX77686_REG_BUCK1OUT,

	MAX77686_REG_BUCK2CTRL1,
	MAX77686_REG_BUCK234FREQ,
	MAX77686_REG_BUCK2DVS1,
	MAX77686_REG_BUCK2DVS2,
	MAX77686_REG_BUCK2DVS3,
	MAX77686_REG_BUCK2DVS4,
	MAX77686_REG_BUCK2DVS5,
	MAX77686_REG_BUCK2DVS6,
	MAX77686_REG_BUCK2DVS7,
	MAX77686_REG_BUCK2DVS8,

	MAX77686_REG_BUCK3CTRL1,
	MAX77686_REG_BUCK3DVS1,
	MAX77686_REG_BUCK3DVS2,
	MAX77686_REG_BUCK3DVS3,
	MAX77686_REG_BUCK3DVS4,
	MAX77686_REG_BUCK3DVS5,
	MAX77686_REG_BUCK3DVS6,
	MAX77686_REG_BUCK3DVS7,
	MAX77686_REG_BUCK3DVS8,

	MAX77686_REG_BUCK4CTRL1,
	MAX77686_REG_BUCK4DVS1,
	MAX77686_REG_BUCK4DVS2,
	MAX77686_REG_BUCK4DVS3,
	MAX77686_REG_BUCK4DVS4,
	MAX77686_REG_BUCK4DVS5,
	MAX77686_REG_BUCK4DVS6,
	MAX77686_REG_BUCK4DVS7,
	MAX77686_REG_BUCK4DVS8,

	MAX77686_REG_BUCK5CTRL,
	MAX77686_REG_BUCK5OUT,
	MAX77686_REG_BUCK6CTRL,
	MAX77686_REG_BUCK6OUT,
	MAX77686_REG_BUCK7CTRL,
	MAX77686_REG_BUCK7OUT,
	MAX77686_REG_BUCK8CTRL,
	MAX77686_REG_BUCK8OUT,
	MAX77686_REG_BUCK9CTRL,
	MAX77686_REG_BUCK9OUT,

	MAX77686_REG_LDO1CTRL1,
	MAX77686_REG_LDO2CTRL1,
	MAX77686_REG_LDO3CTRL1,
	MAX77686_REG_LDO4CTRL1,
	MAX77686_REG_LDO5CTRL1,
	MAX77686_REG_LDO6CTRL1,
	MAX77686_REG_LDO7CTRL1,
	MAX77686_REG_LDO8CTRL1,
	MAX77686_REG_LDO9CTRL1,
	MAX77686_REG_LDO10CTRL1,
	MAX77686_REG_LDO11CTRL1,
	MAX77686_REG_LDO12CTRL1,
	MAX77686_REG_LDO13CTRL1,
	MAX77686_REG_LDO14CTRL1,
	MAX77686_REG_LDO15CTRL1,
	MAX77686_REG_LDO16CTRL1,
	MAX77686_REG_LDO17CTRL1,
	MAX77686_REG_LDO18CTRL1,
	MAX77686_REG_LDO19CTRL1,
	MAX77686_REG_LDO20CTRL1,
	MAX77686_REG_LDO21CTRL1,
	MAX77686_REG_LDO22CTRL1,
	MAX77686_REG_LDO23CTRL1,
	MAX77686_REG_LDO24CTRL1,
	MAX77686_REG_LDO25CTRL1,
	MAX77686_REG_LDO26CTRL1,

	MAX77686_REG_LDO1CTRL2,
	MAX77686_REG_LDO2CTRL2,
	MAX77686_REG_LDO3CTRL2,
	MAX77686_REG_LDO4CTRL2,
	MAX77686_REG_LDO5CTRL2,
	MAX77686_REG_LDO6CTRL2,
	MAX77686_REG_LDO7CTRL2,
	MAX77686_REG_LDO8CTRL2,
	MAX77686_REG_LDO9CTRL2,
	MAX77686_REG_LDO10CTRL2,
	MAX77686_REG_LDO11CTRL2,
	MAX77686_REG_LDO12CTRL2,
	MAX77686_REG_LDO13CTRL2,
	MAX77686_REG_LDO14CTRL2,
	MAX77686_REG_LDO15CTRL2,
	MAX77686_REG_LDO16CTRL2,
	MAX77686_REG_LDO17CTRL2,
	MAX77686_REG_LDO18CTRL2,
	MAX77686_REG_LDO19CTRL2,
	MAX77686_REG_LDO20CTRL2,
	MAX77686_REG_LDO21CTRL2,
	MAX77686_REG_LDO22CTRL2,
	MAX77686_REG_LDO23CTRL2,
	MAX77686_REG_LDO24CTRL2,
	MAX77686_REG_LDO25CTRL2,
	MAX77686_REG_LDO26CTRL2,

	MAX77686_REG_BBAT_CHARGER,
	MAX77686_REG_32KHZ,
};

static int max77686_freeze(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);
	int i;

	for (i = 0; i < ARRAY_SIZE(max77686_dumpaddr_pmic); i++)
		max77686_read_reg(i2c, max77686_dumpaddr_pmic[i],
				&max77686->reg_dump[i]);

	return 0;
}

static int max77686_restore(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);
	int i;

	for (i = 0; i < ARRAY_SIZE(max77686_dumpaddr_pmic); i++)
		max77686_write_reg(i2c, max77686_dumpaddr_pmic[i],
				max77686->reg_dump[i]);

	return 0;
}

static int max77686_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		irq_set_irq_wake(max77686->irq, 1);
	return 0;
}

static int max77686_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		irq_set_irq_wake(max77686->irq, 0);
	return max77686_irq_resume(max77686);
}

const struct dev_pm_ops max77686_pm = {
	.suspend = max77686_suspend,
	.resume = max77686_resume,
	.freeze = max77686_freeze,
	.restore = max77686_restore,
};

static struct i2c_driver max77686_i2c_driver = {
	.driver = {
		   .name = "max77686",
		   .owner = THIS_MODULE,
		   .pm = &max77686_pm,
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

MODULE_DESCRIPTION("MAXIM 8997 multi-function core driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_LICENSE("GPL");
