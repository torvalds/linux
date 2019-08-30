// SPDX-License-Identifier: GPL-2.0-only
/* rtc-ds1343.c
 *
 * Driver for Dallas Semiconductor DS1343 Low Current, SPI Compatible
 * Real Time Clock
 *
 * Author : Raghavendra Chandra Ganiga <ravi23ganiga@gmail.com>
 *	    Ankur Srivastava <sankurece@gmail.com> : DS1343 Nvram Support
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/pm.h>
#include <linux/pm_wakeirq.h>
#include <linux/slab.h>

#define DALLAS_MAXIM_DS1343	0
#define DALLAS_MAXIM_DS1344	1

/* RTC DS1343 Registers */
#define DS1343_SECONDS_REG	0x00
#define DS1343_MINUTES_REG	0x01
#define DS1343_HOURS_REG	0x02
#define DS1343_DAY_REG		0x03
#define DS1343_DATE_REG		0x04
#define DS1343_MONTH_REG	0x05
#define DS1343_YEAR_REG		0x06
#define DS1343_ALM0_SEC_REG	0x07
#define DS1343_ALM0_MIN_REG	0x08
#define DS1343_ALM0_HOUR_REG	0x09
#define DS1343_ALM0_DAY_REG	0x0A
#define DS1343_ALM1_SEC_REG	0x0B
#define DS1343_ALM1_MIN_REG	0x0C
#define DS1343_ALM1_HOUR_REG	0x0D
#define DS1343_ALM1_DAY_REG	0x0E
#define DS1343_CONTROL_REG	0x0F
#define DS1343_STATUS_REG	0x10
#define DS1343_TRICKLE_REG	0x11
#define DS1343_NVRAM		0x20

#define DS1343_NVRAM_LEN	96

/* DS1343 Control Registers bits */
#define DS1343_EOSC		0x80
#define DS1343_DOSF		0x20
#define DS1343_EGFIL		0x10
#define DS1343_SQW		0x08
#define DS1343_INTCN		0x04
#define DS1343_A1IE		0x02
#define DS1343_A0IE		0x01

/* DS1343 Status Registers bits */
#define DS1343_OSF		0x80
#define DS1343_IRQF1		0x02
#define DS1343_IRQF0		0x01

/* DS1343 Trickle Charger Registers bits */
#define DS1343_TRICKLE_MAGIC	0xa0
#define DS1343_TRICKLE_DS1	0x08
#define DS1343_TRICKLE_1K	0x01
#define DS1343_TRICKLE_2K	0x02
#define DS1343_TRICKLE_4K	0x03

static const struct spi_device_id ds1343_id[] = {
	{ "ds1343", DALLAS_MAXIM_DS1343 },
	{ "ds1344", DALLAS_MAXIM_DS1344 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ds1343_id);

struct ds1343_priv {
	struct spi_device *spi;
	struct rtc_device *rtc;
	struct regmap *map;
	struct mutex mutex;
	unsigned int irqen;
	int irq;
	int alarm_sec;
	int alarm_min;
	int alarm_hour;
	int alarm_mday;
};

static int ds1343_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
#ifdef RTC_SET_CHARGE
	case RTC_SET_CHARGE:
	{
		int val;

		if (copy_from_user(&val, (int __user *)arg, sizeof(int)))
			return -EFAULT;

		return regmap_write(priv->map, DS1343_TRICKLE_REG, val);
	}
	break;
#endif
	}

	return -ENOIOCTLCMD;
}

static ssize_t ds1343_show_glitchfilter(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ds1343_priv *priv = dev_get_drvdata(dev);
	int glitch_filt_status, data;

	regmap_read(priv->map, DS1343_CONTROL_REG, &data);

	glitch_filt_status = !!(data & DS1343_EGFIL);

	if (glitch_filt_status)
		return sprintf(buf, "enabled\n");
	else
		return sprintf(buf, "disabled\n");
}

static ssize_t ds1343_store_glitchfilter(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct ds1343_priv *priv = dev_get_drvdata(dev);
	int data;

	regmap_read(priv->map, DS1343_CONTROL_REG, &data);

	if (strncmp(buf, "enabled", 7) == 0)
		data |= DS1343_EGFIL;

	else if (strncmp(buf, "disabled", 8) == 0)
		data &= ~(DS1343_EGFIL);

	else
		return -EINVAL;

	regmap_write(priv->map, DS1343_CONTROL_REG, data);

	return count;
}

static DEVICE_ATTR(glitch_filter, S_IRUGO | S_IWUSR, ds1343_show_glitchfilter,
			ds1343_store_glitchfilter);

static int ds1343_nvram_write(void *priv, unsigned int off, void *val,
			      size_t bytes)
{
	struct ds1343_priv *ds1343 = priv;

	return regmap_bulk_write(ds1343->map, DS1343_NVRAM + off, val, bytes);
}

static int ds1343_nvram_read(void *priv, unsigned int off, void *val,
			     size_t bytes)
{
	struct ds1343_priv *ds1343 = priv;

	return regmap_bulk_read(ds1343->map, DS1343_NVRAM + off, val, bytes);
}

static ssize_t ds1343_show_tricklecharger(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ds1343_priv *priv = dev_get_drvdata(dev);
	int data;
	char *diodes = "disabled", *resistors = " ";

	regmap_read(priv->map, DS1343_TRICKLE_REG, &data);

	if ((data & 0xf0) == DS1343_TRICKLE_MAGIC) {
		switch (data & 0x0c) {
		case DS1343_TRICKLE_DS1:
			diodes = "one diode,";
			break;

		default:
			diodes = "no diode,";
			break;
		}

		switch (data & 0x03) {
		case DS1343_TRICKLE_1K:
			resistors = "1k Ohm";
			break;

		case DS1343_TRICKLE_2K:
			resistors = "2k Ohm";
			break;

		case DS1343_TRICKLE_4K:
			resistors = "4k Ohm";
			break;

		default:
			diodes = "disabled";
			break;
		}
	}

	return sprintf(buf, "%s %s\n", diodes, resistors);
}

static DEVICE_ATTR(trickle_charger, S_IRUGO, ds1343_show_tricklecharger, NULL);

static int ds1343_sysfs_register(struct device *dev)
{
	int err;

	err = device_create_file(dev, &dev_attr_glitch_filter);
	if (err)
		return err;

	err = device_create_file(dev, &dev_attr_trickle_charger);
	if (!err)
		return 0;

	device_remove_file(dev, &dev_attr_glitch_filter);

	return err;
}

static void ds1343_sysfs_unregister(struct device *dev)
{
	device_remove_file(dev, &dev_attr_glitch_filter);
	device_remove_file(dev, &dev_attr_trickle_charger);
}

static int ds1343_read_time(struct device *dev, struct rtc_time *dt)
{
	struct ds1343_priv *priv = dev_get_drvdata(dev);
	unsigned char buf[7];
	int res;

	res = regmap_bulk_read(priv->map, DS1343_SECONDS_REG, buf, 7);
	if (res)
		return res;

	dt->tm_sec	= bcd2bin(buf[0]);
	dt->tm_min	= bcd2bin(buf[1]);
	dt->tm_hour	= bcd2bin(buf[2] & 0x3F);
	dt->tm_wday	= bcd2bin(buf[3]) - 1;
	dt->tm_mday	= bcd2bin(buf[4]);
	dt->tm_mon	= bcd2bin(buf[5] & 0x1F) - 1;
	dt->tm_year	= bcd2bin(buf[6]) + 100; /* year offset from 1900 */

	return 0;
}

static int ds1343_set_time(struct device *dev, struct rtc_time *dt)
{
	struct ds1343_priv *priv = dev_get_drvdata(dev);
	int res;

	res = regmap_write(priv->map, DS1343_SECONDS_REG,
				bin2bcd(dt->tm_sec));
	if (res)
		return res;

	res = regmap_write(priv->map, DS1343_MINUTES_REG,
				bin2bcd(dt->tm_min));
	if (res)
		return res;

	res = regmap_write(priv->map, DS1343_HOURS_REG,
				bin2bcd(dt->tm_hour) & 0x3F);
	if (res)
		return res;

	res = regmap_write(priv->map, DS1343_DAY_REG,
				bin2bcd(dt->tm_wday + 1));
	if (res)
		return res;

	res = regmap_write(priv->map, DS1343_DATE_REG,
				bin2bcd(dt->tm_mday));
	if (res)
		return res;

	res = regmap_write(priv->map, DS1343_MONTH_REG,
				bin2bcd(dt->tm_mon + 1));
	if (res)
		return res;

	dt->tm_year %= 100;

	res = regmap_write(priv->map, DS1343_YEAR_REG,
				bin2bcd(dt->tm_year));
	if (res)
		return res;

	return 0;
}

static int ds1343_update_alarm(struct device *dev)
{
	struct ds1343_priv *priv = dev_get_drvdata(dev);
	unsigned int control, stat;
	unsigned char buf[4];
	int res = 0;

	res = regmap_read(priv->map, DS1343_CONTROL_REG, &control);
	if (res)
		return res;

	res = regmap_read(priv->map, DS1343_STATUS_REG, &stat);
	if (res)
		return res;

	control &= ~(DS1343_A0IE);
	stat &= ~(DS1343_IRQF0);

	res = regmap_write(priv->map, DS1343_CONTROL_REG, control);
	if (res)
		return res;

	res = regmap_write(priv->map, DS1343_STATUS_REG, stat);
	if (res)
		return res;

	buf[0] = priv->alarm_sec < 0 || (priv->irqen & RTC_UF) ?
		0x80 : bin2bcd(priv->alarm_sec) & 0x7F;
	buf[1] = priv->alarm_min < 0 || (priv->irqen & RTC_UF) ?
		0x80 : bin2bcd(priv->alarm_min) & 0x7F;
	buf[2] = priv->alarm_hour < 0 || (priv->irqen & RTC_UF) ?
		0x80 : bin2bcd(priv->alarm_hour) & 0x3F;
	buf[3] = priv->alarm_mday < 0 || (priv->irqen & RTC_UF) ?
		0x80 : bin2bcd(priv->alarm_mday) & 0x7F;

	res = regmap_bulk_write(priv->map, DS1343_ALM0_SEC_REG, buf, 4);
	if (res)
		return res;

	if (priv->irqen) {
		control |= DS1343_A0IE;
		res = regmap_write(priv->map, DS1343_CONTROL_REG, control);
	}

	return res;
}

static int ds1343_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct ds1343_priv *priv = dev_get_drvdata(dev);
	int res = 0;
	unsigned int stat;

	if (priv->irq <= 0)
		return -EINVAL;

	mutex_lock(&priv->mutex);

	res = regmap_read(priv->map, DS1343_STATUS_REG, &stat);
	if (res)
		goto out;

	alarm->enabled = !!(priv->irqen & RTC_AF);
	alarm->pending = !!(stat & DS1343_IRQF0);

	alarm->time.tm_sec = priv->alarm_sec < 0 ? 0 : priv->alarm_sec;
	alarm->time.tm_min = priv->alarm_min < 0 ? 0 : priv->alarm_min;
	alarm->time.tm_hour = priv->alarm_hour < 0 ? 0 : priv->alarm_hour;
	alarm->time.tm_mday = priv->alarm_mday < 0 ? 0 : priv->alarm_mday;

out:
	mutex_unlock(&priv->mutex);
	return res;
}

static int ds1343_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct ds1343_priv *priv = dev_get_drvdata(dev);
	int res = 0;

	if (priv->irq <= 0)
		return -EINVAL;

	mutex_lock(&priv->mutex);

	priv->alarm_sec = alarm->time.tm_sec;
	priv->alarm_min = alarm->time.tm_min;
	priv->alarm_hour = alarm->time.tm_hour;
	priv->alarm_mday = alarm->time.tm_mday;

	if (alarm->enabled)
		priv->irqen |= RTC_AF;

	res = ds1343_update_alarm(dev);

	mutex_unlock(&priv->mutex);

	return res;
}

static int ds1343_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct ds1343_priv *priv = dev_get_drvdata(dev);
	int res = 0;

	if (priv->irq <= 0)
		return -EINVAL;

	mutex_lock(&priv->mutex);

	if (enabled)
		priv->irqen |= RTC_AF;
	else
		priv->irqen &= ~RTC_AF;

	res = ds1343_update_alarm(dev);

	mutex_unlock(&priv->mutex);

	return res;
}

static irqreturn_t ds1343_thread(int irq, void *dev_id)
{
	struct ds1343_priv *priv = dev_id;
	unsigned int stat, control;
	int res = 0;

	mutex_lock(&priv->mutex);

	res = regmap_read(priv->map, DS1343_STATUS_REG, &stat);
	if (res)
		goto out;

	if (stat & DS1343_IRQF0) {
		stat &= ~DS1343_IRQF0;
		regmap_write(priv->map, DS1343_STATUS_REG, stat);

		res = regmap_read(priv->map, DS1343_CONTROL_REG, &control);
		if (res)
			goto out;

		control &= ~DS1343_A0IE;
		regmap_write(priv->map, DS1343_CONTROL_REG, control);

		rtc_update_irq(priv->rtc, 1, RTC_AF | RTC_IRQF);
	}

out:
	mutex_unlock(&priv->mutex);
	return IRQ_HANDLED;
}

static const struct rtc_class_ops ds1343_rtc_ops = {
	.ioctl		= ds1343_ioctl,
	.read_time	= ds1343_read_time,
	.set_time	= ds1343_set_time,
	.read_alarm	= ds1343_read_alarm,
	.set_alarm	= ds1343_set_alarm,
	.alarm_irq_enable = ds1343_alarm_irq_enable,
};

static int ds1343_probe(struct spi_device *spi)
{
	struct ds1343_priv *priv;
	struct regmap_config config = { .reg_bits = 8, .val_bits = 8,
					.write_flag_mask = 0x80, };
	unsigned int data;
	int res;
	struct nvmem_config nvmem_cfg = {
		.name = "ds1343-",
		.word_size = 1,
		.stride = 1,
		.size = DS1343_NVRAM_LEN,
		.reg_read = ds1343_nvram_read,
		.reg_write = ds1343_nvram_write,
	};

	priv = devm_kzalloc(&spi->dev, sizeof(struct ds1343_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi = spi;
	mutex_init(&priv->mutex);

	/* RTC DS1347 works in spi mode 3 and
	 * its chip select is active high
	 */
	spi->mode = SPI_MODE_3 | SPI_CS_HIGH;
	spi->bits_per_word = 8;
	res = spi_setup(spi);
	if (res)
		return res;

	spi_set_drvdata(spi, priv);

	priv->map = devm_regmap_init_spi(spi, &config);

	if (IS_ERR(priv->map)) {
		dev_err(&spi->dev, "spi regmap init failed for rtc ds1343\n");
		return PTR_ERR(priv->map);
	}

	res = regmap_read(priv->map, DS1343_SECONDS_REG, &data);
	if (res)
		return res;

	regmap_read(priv->map, DS1343_CONTROL_REG, &data);
	data |= DS1343_INTCN;
	data &= ~(DS1343_EOSC | DS1343_A1IE | DS1343_A0IE);
	regmap_write(priv->map, DS1343_CONTROL_REG, data);

	regmap_read(priv->map, DS1343_STATUS_REG, &data);
	data &= ~(DS1343_OSF | DS1343_IRQF1 | DS1343_IRQF0);
	regmap_write(priv->map, DS1343_STATUS_REG, data);

	priv->rtc = devm_rtc_allocate_device(&spi->dev);
	if (IS_ERR(priv->rtc))
		return PTR_ERR(priv->rtc);

	priv->rtc->nvram_old_abi = true;
	priv->rtc->ops = &ds1343_rtc_ops;

	res = rtc_register_device(priv->rtc);
	if (res)
		return res;

	nvmem_cfg.priv = priv;
	rtc_nvmem_register(priv->rtc, &nvmem_cfg);

	priv->irq = spi->irq;

	if (priv->irq >= 0) {
		res = devm_request_threaded_irq(&spi->dev, spi->irq, NULL,
						ds1343_thread, IRQF_ONESHOT,
						"ds1343", priv);
		if (res) {
			priv->irq = -1;
			dev_err(&spi->dev,
				"unable to request irq for rtc ds1343\n");
		} else {
			device_init_wakeup(&spi->dev, true);
			dev_pm_set_wake_irq(&spi->dev, spi->irq);
		}
	}

	res = ds1343_sysfs_register(&spi->dev);
	if (res)
		dev_err(&spi->dev,
			"unable to create sysfs entries for rtc ds1343\n");

	return 0;
}

static int ds1343_remove(struct spi_device *spi)
{
	struct ds1343_priv *priv = spi_get_drvdata(spi);

	if (spi->irq) {
		mutex_lock(&priv->mutex);
		priv->irqen &= ~RTC_AF;
		mutex_unlock(&priv->mutex);

		dev_pm_clear_wake_irq(&spi->dev);
		device_init_wakeup(&spi->dev, false);
		devm_free_irq(&spi->dev, spi->irq, priv);
	}

	spi_set_drvdata(spi, NULL);

	ds1343_sysfs_unregister(&spi->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int ds1343_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);

	if (spi->irq >= 0 && device_may_wakeup(dev))
		enable_irq_wake(spi->irq);

	return 0;
}

static int ds1343_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);

	if (spi->irq >= 0 && device_may_wakeup(dev))
		disable_irq_wake(spi->irq);

	return 0;
}

#endif

static SIMPLE_DEV_PM_OPS(ds1343_pm, ds1343_suspend, ds1343_resume);

static struct spi_driver ds1343_driver = {
	.driver = {
		.name = "ds1343",
		.pm = &ds1343_pm,
	},
	.probe = ds1343_probe,
	.remove = ds1343_remove,
	.id_table = ds1343_id,
};

module_spi_driver(ds1343_driver);

MODULE_DESCRIPTION("DS1343 RTC SPI Driver");
MODULE_AUTHOR("Raghavendra Chandra Ganiga <ravi23ganiga@gmail.com>,"
		"Ankur Srivastava <sankurece@gmail.com>");
MODULE_LICENSE("GPL v2");
