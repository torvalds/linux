/*
 * RTC client/driver for the Maxim/Dallas DS3232/DS3234 Real-Time Clock
 *
 * Copyright (C) 2009-2011 Freescale Semiconductor.
 * Author: Jack Lan <jack.lan@freescale.com>
 * Copyright (C) 2008 MIMOMax Wireless Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#define DS3232_REG_SECONDS      0x00
#define DS3232_REG_MINUTES      0x01
#define DS3232_REG_HOURS        0x02
#define DS3232_REG_AMPM         0x02
#define DS3232_REG_DAY          0x03
#define DS3232_REG_DATE         0x04
#define DS3232_REG_MONTH        0x05
#define DS3232_REG_CENTURY      0x05
#define DS3232_REG_YEAR         0x06
#define DS3232_REG_ALARM1       0x07       /* Alarm 1 BASE */
#define DS3232_REG_ALARM2       0x0B       /* Alarm 2 BASE */
#define DS3232_REG_CR           0x0E       /* Control register */
#       define DS3232_REG_CR_nEOSC   0x80
#       define DS3232_REG_CR_INTCN   0x04
#       define DS3232_REG_CR_A2IE    0x02
#       define DS3232_REG_CR_A1IE    0x01

#define DS3232_REG_SR           0x0F       /* control/status register */
#       define DS3232_REG_SR_OSF     0x80
#       define DS3232_REG_SR_BSY     0x04
#       define DS3232_REG_SR_A2F     0x02
#       define DS3232_REG_SR_A1F     0x01

struct ds3232 {
	struct device *dev;
	struct regmap *regmap;
	int irq;
	struct rtc_device *rtc;

	bool suspended;
};

static int ds3232_check_rtc_status(struct device *dev)
{
	struct ds3232 *ds3232 = dev_get_drvdata(dev);
	int ret = 0;
	int control, stat;

	ret = regmap_read(ds3232->regmap, DS3232_REG_SR, &stat);
	if (ret)
		return ret;

	if (stat & DS3232_REG_SR_OSF)
		dev_warn(dev,
				"oscillator discontinuity flagged, "
				"time unreliable\n");

	stat &= ~(DS3232_REG_SR_OSF | DS3232_REG_SR_A1F | DS3232_REG_SR_A2F);

	ret = regmap_write(ds3232->regmap, DS3232_REG_SR, stat);
	if (ret)
		return ret;

	/* If the alarm is pending, clear it before requesting
	 * the interrupt, so an interrupt event isn't reported
	 * before everything is initialized.
	 */

	ret = regmap_read(ds3232->regmap, DS3232_REG_CR, &control);
	if (ret)
		return ret;

	control &= ~(DS3232_REG_CR_A1IE | DS3232_REG_CR_A2IE);
	control |= DS3232_REG_CR_INTCN;

	return regmap_write(ds3232->regmap, DS3232_REG_CR, control);
}

static int ds3232_read_time(struct device *dev, struct rtc_time *time)
{
	struct ds3232 *ds3232 = dev_get_drvdata(dev);
	int ret;
	u8 buf[7];
	unsigned int year, month, day, hour, minute, second;
	unsigned int week, twelve_hr, am_pm;
	unsigned int century, add_century = 0;

	ret = regmap_bulk_read(ds3232->regmap, DS3232_REG_SECONDS, buf, 7);
	if (ret)
		return ret;

	second = buf[0];
	minute = buf[1];
	hour = buf[2];
	week = buf[3];
	day = buf[4];
	month = buf[5];
	year = buf[6];

	/* Extract additional information for AM/PM and century */

	twelve_hr = hour & 0x40;
	am_pm = hour & 0x20;
	century = month & 0x80;

	/* Write to rtc_time structure */

	time->tm_sec = bcd2bin(second);
	time->tm_min = bcd2bin(minute);
	if (twelve_hr) {
		/* Convert to 24 hr */
		if (am_pm)
			time->tm_hour = bcd2bin(hour & 0x1F) + 12;
		else
			time->tm_hour = bcd2bin(hour & 0x1F);
	} else {
		time->tm_hour = bcd2bin(hour);
	}

	/* Day of the week in linux range is 0~6 while 1~7 in RTC chip */
	time->tm_wday = bcd2bin(week) - 1;
	time->tm_mday = bcd2bin(day);
	/* linux tm_mon range:0~11, while month range is 1~12 in RTC chip */
	time->tm_mon = bcd2bin(month & 0x7F) - 1;
	if (century)
		add_century = 100;

	time->tm_year = bcd2bin(year) + add_century;

	return rtc_valid_tm(time);
}

static int ds3232_set_time(struct device *dev, struct rtc_time *time)
{
	struct ds3232 *ds3232 = dev_get_drvdata(dev);
	u8 buf[7];

	/* Extract time from rtc_time and load into ds3232*/

	buf[0] = bin2bcd(time->tm_sec);
	buf[1] = bin2bcd(time->tm_min);
	buf[2] = bin2bcd(time->tm_hour);
	/* Day of the week in linux range is 0~6 while 1~7 in RTC chip */
	buf[3] = bin2bcd(time->tm_wday + 1);
	buf[4] = bin2bcd(time->tm_mday); /* Date */
	/* linux tm_mon range:0~11, while month range is 1~12 in RTC chip */
	buf[5] = bin2bcd(time->tm_mon + 1);
	if (time->tm_year >= 100) {
		buf[5] |= 0x80;
		buf[6] = bin2bcd(time->tm_year - 100);
	} else {
		buf[6] = bin2bcd(time->tm_year);
	}

	return regmap_bulk_write(ds3232->regmap, DS3232_REG_SECONDS, buf, 7);
}

/*
 * DS3232 has two alarm, we only use alarm1
 * According to linux specification, only support one-shot alarm
 * no periodic alarm mode
 */
static int ds3232_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct ds3232 *ds3232 = dev_get_drvdata(dev);
	int control, stat;
	int ret;
	u8 buf[4];

	ret = regmap_read(ds3232->regmap, DS3232_REG_SR, &stat);
	if (ret)
		goto out;
	ret = regmap_read(ds3232->regmap, DS3232_REG_CR, &control);
	if (ret)
		goto out;
	ret = regmap_bulk_read(ds3232->regmap, DS3232_REG_ALARM1, buf, 4);
	if (ret)
		goto out;

	alarm->time.tm_sec = bcd2bin(buf[0] & 0x7F);
	alarm->time.tm_min = bcd2bin(buf[1] & 0x7F);
	alarm->time.tm_hour = bcd2bin(buf[2] & 0x7F);
	alarm->time.tm_mday = bcd2bin(buf[3] & 0x7F);

	alarm->enabled = !!(control & DS3232_REG_CR_A1IE);
	alarm->pending = !!(stat & DS3232_REG_SR_A1F);

	ret = 0;
out:
	return ret;
}

/*
 * linux rtc-module does not support wday alarm
 * and only 24h time mode supported indeed
 */
static int ds3232_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct ds3232 *ds3232 = dev_get_drvdata(dev);
	int control, stat;
	int ret;
	u8 buf[4];

	if (ds3232->irq <= 0)
		return -EINVAL;

	buf[0] = bin2bcd(alarm->time.tm_sec);
	buf[1] = bin2bcd(alarm->time.tm_min);
	buf[2] = bin2bcd(alarm->time.tm_hour);
	buf[3] = bin2bcd(alarm->time.tm_mday);

	/* clear alarm interrupt enable bit */
	ret = regmap_read(ds3232->regmap, DS3232_REG_CR, &control);
	if (ret)
		goto out;
	control &= ~(DS3232_REG_CR_A1IE | DS3232_REG_CR_A2IE);
	ret = regmap_write(ds3232->regmap, DS3232_REG_CR, control);
	if (ret)
		goto out;

	/* clear any pending alarm flag */
	ret = regmap_read(ds3232->regmap, DS3232_REG_SR, &stat);
	if (ret)
		goto out;
	stat &= ~(DS3232_REG_SR_A1F | DS3232_REG_SR_A2F);
	ret = regmap_write(ds3232->regmap, DS3232_REG_SR, stat);
	if (ret)
		goto out;

	ret = regmap_bulk_write(ds3232->regmap, DS3232_REG_ALARM1, buf, 4);
	if (ret)
		goto out;

	if (alarm->enabled) {
		control |= DS3232_REG_CR_A1IE;
		ret = regmap_write(ds3232->regmap, DS3232_REG_CR, control);
	}
out:
	return ret;
}

static int ds3232_update_alarm(struct device *dev, unsigned int enabled)
{
	struct ds3232 *ds3232 = dev_get_drvdata(dev);
	int control;
	int ret;

	ret = regmap_read(ds3232->regmap, DS3232_REG_CR, &control);
	if (ret)
		return ret;

	if (enabled)
		/* enable alarm1 interrupt */
		control |= DS3232_REG_CR_A1IE;
	else
		/* disable alarm1 interrupt */
		control &= ~(DS3232_REG_CR_A1IE);
	ret = regmap_write(ds3232->regmap, DS3232_REG_CR, control);

	return ret;
}

static int ds3232_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct ds3232 *ds3232 = dev_get_drvdata(dev);

	if (ds3232->irq <= 0)
		return -EINVAL;

	return ds3232_update_alarm(dev, enabled);
}

static irqreturn_t ds3232_irq(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	struct ds3232 *ds3232 = dev_get_drvdata(dev);
	struct mutex *lock = &ds3232->rtc->ops_lock;
	int ret;
	int stat, control;

	mutex_lock(lock);

	ret = regmap_read(ds3232->regmap, DS3232_REG_SR, &stat);
	if (ret)
		goto unlock;

	if (stat & DS3232_REG_SR_A1F) {
		ret = regmap_read(ds3232->regmap, DS3232_REG_CR, &control);
		if (ret) {
			dev_warn(ds3232->dev,
				 "Read Control Register error %d\n", ret);
		} else {
			/* disable alarm1 interrupt */
			control &= ~(DS3232_REG_CR_A1IE);
			ret = regmap_write(ds3232->regmap, DS3232_REG_CR,
					   control);
			if (ret) {
				dev_warn(ds3232->dev,
					 "Write Control Register error %d\n",
					 ret);
				goto unlock;
			}

			/* clear the alarm pend flag */
			stat &= ~DS3232_REG_SR_A1F;
			ret = regmap_write(ds3232->regmap, DS3232_REG_SR, stat);
			if (ret) {
				dev_warn(ds3232->dev,
					 "Write Status Register error %d\n",
					 ret);
				goto unlock;
			}

			rtc_update_irq(ds3232->rtc, 1, RTC_AF | RTC_IRQF);
		}
	}

unlock:
	mutex_unlock(lock);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops ds3232_rtc_ops = {
	.read_time = ds3232_read_time,
	.set_time = ds3232_set_time,
	.read_alarm = ds3232_read_alarm,
	.set_alarm = ds3232_set_alarm,
	.alarm_irq_enable = ds3232_alarm_irq_enable,
};

static int ds3232_probe(struct device *dev, struct regmap *regmap, int irq,
			const char *name)
{
	struct ds3232 *ds3232;
	int ret;

	ds3232 = devm_kzalloc(dev, sizeof(*ds3232), GFP_KERNEL);
	if (!ds3232)
		return -ENOMEM;

	ds3232->regmap = regmap;
	ds3232->irq = irq;
	ds3232->dev = dev;
	dev_set_drvdata(dev, ds3232);

	ret = ds3232_check_rtc_status(dev);
	if (ret)
		return ret;

	if (ds3232->irq > 0)
		device_init_wakeup(dev, 1);

	ds3232->rtc = devm_rtc_device_register(dev, name, &ds3232_rtc_ops,
						THIS_MODULE);
	if (IS_ERR(ds3232->rtc))
		return PTR_ERR(ds3232->rtc);

	if (ds3232->irq > 0) {
		ret = devm_request_threaded_irq(dev, ds3232->irq, NULL,
						ds3232_irq,
						IRQF_SHARED | IRQF_ONESHOT,
						name, dev);
		if (ret) {
			device_set_wakeup_capable(dev, 0);
			ds3232->irq = 0;
			dev_err(dev, "unable to request IRQ\n");
		}
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ds3232_suspend(struct device *dev)
{
	struct ds3232 *ds3232 = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		if (enable_irq_wake(ds3232->irq))
			dev_warn_once(dev, "Cannot set wakeup source\n");
	}

	return 0;
}

static int ds3232_resume(struct device *dev)
{
	struct ds3232 *ds3232 = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(ds3232->irq);

	return 0;
}
#endif

static const struct dev_pm_ops ds3232_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ds3232_suspend, ds3232_resume)
};

#if IS_ENABLED(CONFIG_I2C)

static int ds3232_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct regmap *regmap;
	static const struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0x13,
	};

	regmap = devm_regmap_init_i2c(client, &config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "%s: regmap allocation failed: %ld\n",
			__func__, PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return ds3232_probe(&client->dev, regmap, client->irq, client->name);
}

static const struct i2c_device_id ds3232_id[] = {
	{ "ds3232", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds3232_id);

static const struct of_device_id ds3232_of_match[] = {
	{ .compatible = "dallas,ds3232" },
	{ }
};
MODULE_DEVICE_TABLE(of, ds3232_of_match);

static struct i2c_driver ds3232_driver = {
	.driver = {
		.name = "rtc-ds3232",
		.of_match_table = of_match_ptr(ds3232_of_match),
		.pm	= &ds3232_pm_ops,
	},
	.probe = ds3232_i2c_probe,
	.id_table = ds3232_id,
};

static int ds3232_register_driver(void)
{
	return i2c_add_driver(&ds3232_driver);
}

static void ds3232_unregister_driver(void)
{
	i2c_del_driver(&ds3232_driver);
}

#else

static int ds3232_register_driver(void)
{
	return 0;
}

static void ds3232_unregister_driver(void)
{
}

#endif

#if IS_ENABLED(CONFIG_SPI_MASTER)

static int ds3234_probe(struct spi_device *spi)
{
	int res;
	unsigned int tmp;
	static const struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0x13,
		.write_flag_mask = 0x80,
	};
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "%s: regmap allocation failed: %ld\n",
			__func__, PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);

	res = regmap_read(regmap, DS3232_REG_SECONDS, &tmp);
	if (res)
		return res;

	/* Control settings
	 *
	 * CONTROL_REG
	 * BIT 7	6	5	4	3	2	1	0
	 *     EOSC	BBSQW	CONV	RS2	RS1	INTCN	A2IE	A1IE
	 *
	 *     0	0	0	1	1	1	0	0
	 *
	 * CONTROL_STAT_REG
	 * BIT 7	6	5	4	3	2	1	0
	 *     OSF	BB32kHz	CRATE1	CRATE0	EN32kHz	BSY	A2F	A1F
	 *
	 *     1	0	0	0	1	0	0	0
	 */
	res = regmap_read(regmap, DS3232_REG_CR, &tmp);
	if (res)
		return res;
	res = regmap_write(regmap, DS3232_REG_CR, tmp & 0x1c);
	if (res)
		return res;

	res = regmap_read(regmap, DS3232_REG_SR, &tmp);
	if (res)
		return res;
	res = regmap_write(regmap, DS3232_REG_SR, tmp & 0x88);
	if (res)
		return res;

	/* Print our settings */
	res = regmap_read(regmap, DS3232_REG_CR, &tmp);
	if (res)
		return res;
	dev_info(&spi->dev, "Control Reg: 0x%02x\n", tmp);

	res = regmap_read(regmap, DS3232_REG_SR, &tmp);
	if (res)
		return res;
	dev_info(&spi->dev, "Ctrl/Stat Reg: 0x%02x\n", tmp);

	return ds3232_probe(&spi->dev, regmap, spi->irq, "ds3234");
}

static struct spi_driver ds3234_driver = {
	.driver = {
		.name	 = "ds3234",
	},
	.probe	 = ds3234_probe,
};

static int ds3234_register_driver(void)
{
	return spi_register_driver(&ds3234_driver);
}

static void ds3234_unregister_driver(void)
{
	spi_unregister_driver(&ds3234_driver);
}

#else

static int ds3234_register_driver(void)
{
	return 0;
}

static void ds3234_unregister_driver(void)
{
}

#endif

static int __init ds323x_init(void)
{
	int ret;

	ret = ds3232_register_driver();
	if (ret) {
		pr_err("Failed to register ds3232 driver: %d\n", ret);
		return ret;
	}

	ret = ds3234_register_driver();
	if (ret) {
		pr_err("Failed to register ds3234 driver: %d\n", ret);
		ds3232_unregister_driver();
	}

	return ret;
}
module_init(ds323x_init)

static void __exit ds323x_exit(void)
{
	ds3234_unregister_driver();
	ds3232_unregister_driver();
}
module_exit(ds323x_exit)

MODULE_AUTHOR("Srikanth Srinivasan <srikanth.srinivasan@freescale.com>");
MODULE_AUTHOR("Dennis Aberilla <denzzzhome@yahoo.com>");
MODULE_DESCRIPTION("Maxim/Dallas DS3232/DS3234 RTC Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:ds3234");
