/*
 * An I2C driver for the Philips PCF8563 RTC
 * Copyright 2005-06 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 * Maintainers: http://www.nslu2-linux.org/
 *
 * based on the other drivers in this same directory.
 *
 * http://www.semiconductors.philips.com/acrobat/datasheets/PCF8563-04.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/err.h>

#define DRV_VERSION "0.4.3"

#define PCF8563_REG_ST1		0x00 /* status */
#define PCF8563_REG_ST2		0x01
#define PCF8563_BIT_AIE		(1 << 1)
#define PCF8563_BIT_AF		(1 << 3)

#define PCF8563_REG_SC		0x02 /* datetime */
#define PCF8563_REG_MN		0x03
#define PCF8563_REG_HR		0x04
#define PCF8563_REG_DM		0x05
#define PCF8563_REG_DW		0x06
#define PCF8563_REG_MO		0x07
#define PCF8563_REG_YR		0x08

#define PCF8563_REG_AMN		0x09 /* alarm */

#define PCF8563_REG_CLKO	0x0D /* clock out */
#define PCF8563_REG_TMRC	0x0E /* timer control */
#define PCF8563_REG_TMR		0x0F /* timer */

#define PCF8563_SC_LV		0x80 /* low voltage */
#define PCF8563_MO_C		0x80 /* century */

static struct i2c_driver pcf8563_driver;

struct pcf8563 {
	struct rtc_device *rtc;
	/*
	 * The meaning of MO_C bit varies by the chip type.
	 * From PCF8563 datasheet: this bit is toggled when the years
	 * register overflows from 99 to 00
	 *   0 indicates the century is 20xx
	 *   1 indicates the century is 19xx
	 * From RTC8564 datasheet: this bit indicates change of
	 * century. When the year digit data overflows from 99 to 00,
	 * this bit is set. By presetting it to 0 while still in the
	 * 20th century, it will be set in year 2000, ...
	 * There seems no reliable way to know how the system use this
	 * bit.  So let's do it heuristically, assuming we are live in
	 * 1970...2069.
	 */
	int c_polarity;	/* 0: MO_C=1 means 19xx, otherwise MO_C=1 means 20xx */
	int voltage_low; /* incicates if a low_voltage was detected */

	struct i2c_client *client;
};

static int pcf8563_read_block_data(struct i2c_client *client, unsigned char reg,
				   unsigned char length, unsigned char *buf)
{
	struct i2c_msg msgs[] = {
		{/* setup read ptr */
			.addr = client->addr,
			.len = 1,
			.buf = &reg,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = buf
		},
	};

	if ((i2c_transfer(client->adapter, msgs, 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	return 0;
}

static int pcf8563_write_block_data(struct i2c_client *client,
				   unsigned char reg, unsigned char length,
				   unsigned char *buf)
{
	int i, err;

	for (i = 0; i < length; i++) {
		unsigned char data[2] = { reg + i, buf[i] };

		err = i2c_master_send(client, data, sizeof(data));
		if (err != sizeof(data)) {
			dev_err(&client->dev,
				"%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, data[0], data[1]);
			return -EIO;
		}
	}

	return 0;
}

static int pcf8563_set_alarm_mode(struct i2c_client *client, bool on)
{
	unsigned char buf[2];
	int err;

	err = pcf8563_read_block_data(client, PCF8563_REG_ST2, 1, buf + 1);
	if (err < 0)
		return err;

	if (on)
		buf[1] |= PCF8563_BIT_AIE;
	else
		buf[1] &= ~PCF8563_BIT_AIE;

	buf[1] &= ~PCF8563_BIT_AF;
	buf[0] = PCF8563_REG_ST2;

	err = pcf8563_write_block_data(client, PCF8563_REG_ST2, 1, buf + 1);
	if (err < 0) {
		dev_err(&client->dev, "%s: write error\n", __func__);
		return -EIO;
	}

	return 0;
}

static int pcf8563_get_alarm_mode(struct i2c_client *client, unsigned char *en,
				  unsigned char *pen)
{
	unsigned char buf;
	int err;

	err = pcf8563_read_block_data(client, PCF8563_REG_ST2, 1, &buf);
	if (err)
		return err;

	if (en)
		*en = !!(buf & PCF8563_BIT_AIE);
	if (pen)
		*pen = !!(buf & PCF8563_BIT_AF);

	return 0;
}

static irqreturn_t pcf8563_irq(int irq, void *dev_id)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(dev_id);
	int err;
	char pending;

	err = pcf8563_get_alarm_mode(pcf8563->client, NULL, &pending);
	if (err < 0)
		return err;

	if (pending) {
		rtc_update_irq(pcf8563->rtc, 1, RTC_IRQF | RTC_AF);
		pcf8563_set_alarm_mode(pcf8563->client, 1);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/*
 * In the routines that deal directly with the pcf8563 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int pcf8563_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(client);
	unsigned char buf[9];
	int err;

	err = pcf8563_read_block_data(client, PCF8563_REG_ST1, 9, buf);
	if (err)
		return err;

	if (buf[PCF8563_REG_SC] & PCF8563_SC_LV) {
		pcf8563->voltage_low = 1;
		dev_info(&client->dev,
			"low voltage detected, date/time is not reliable.\n");
	}

	dev_dbg(&client->dev,
		"%s: raw data is st1=%02x, st2=%02x, sec=%02x, min=%02x, hr=%02x, "
		"mday=%02x, wday=%02x, mon=%02x, year=%02x\n",
		__func__,
		buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6], buf[7],
		buf[8]);


	tm->tm_sec = bcd2bin(buf[PCF8563_REG_SC] & 0x7F);
	tm->tm_min = bcd2bin(buf[PCF8563_REG_MN] & 0x7F);
	tm->tm_hour = bcd2bin(buf[PCF8563_REG_HR] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = bcd2bin(buf[PCF8563_REG_DM] & 0x3F);
	tm->tm_wday = buf[PCF8563_REG_DW] & 0x07;
	tm->tm_mon = bcd2bin(buf[PCF8563_REG_MO] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(buf[PCF8563_REG_YR]);
	if (tm->tm_year < 70)
		tm->tm_year += 100;	/* assume we are in 1970...2069 */
	/* detect the polarity heuristically. see note above. */
	pcf8563->c_polarity = (buf[PCF8563_REG_MO] & PCF8563_MO_C) ?
		(tm->tm_year >= 100) : (tm->tm_year < 100);

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* the clock can give out invalid datetime, but we cannot return
	 * -EINVAL otherwise hwclock will refuse to set the time on bootup.
	 */
	if (rtc_valid_tm(tm) < 0)
		dev_err(&client->dev, "retrieved date/time is not valid.\n");

	return 0;
}

static int pcf8563_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(client);
	int err;
	unsigned char buf[9];

	dev_dbg(&client->dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* hours, minutes and seconds */
	buf[PCF8563_REG_SC] = bin2bcd(tm->tm_sec);
	buf[PCF8563_REG_MN] = bin2bcd(tm->tm_min);
	buf[PCF8563_REG_HR] = bin2bcd(tm->tm_hour);

	buf[PCF8563_REG_DM] = bin2bcd(tm->tm_mday);

	/* month, 1 - 12 */
	buf[PCF8563_REG_MO] = bin2bcd(tm->tm_mon + 1);

	/* year and century */
	buf[PCF8563_REG_YR] = bin2bcd(tm->tm_year % 100);
	if (pcf8563->c_polarity ? (tm->tm_year >= 100) : (tm->tm_year < 100))
		buf[PCF8563_REG_MO] |= PCF8563_MO_C;

	buf[PCF8563_REG_DW] = tm->tm_wday & 0x07;

	err = pcf8563_write_block_data(client, PCF8563_REG_SC,
				9 - PCF8563_REG_SC, buf + PCF8563_REG_SC);
	if (err)
		return err;

	return 0;
}

#ifdef CONFIG_RTC_INTF_DEV
static int pcf8563_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(to_i2c_client(dev));
	struct rtc_time tm;

	switch (cmd) {
	case RTC_VL_READ:
		if (pcf8563->voltage_low)
			dev_info(dev, "low voltage detected, date/time is not reliable.\n");

		if (copy_to_user((void __user *)arg, &pcf8563->voltage_low,
					sizeof(int)))
			return -EFAULT;
		return 0;
	case RTC_VL_CLR:
		/*
		 * Clear the VL bit in the seconds register in case
		 * the time has not been set already (which would
		 * have cleared it). This does not really matter
		 * because of the cached voltage_low value but do it
		 * anyway for consistency.
		 */
		if (pcf8563_get_datetime(to_i2c_client(dev), &tm))
			pcf8563_set_datetime(to_i2c_client(dev), &tm);

		/* Clear the cached value. */
		pcf8563->voltage_low = 0;

		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}
#else
#define pcf8563_rtc_ioctl NULL
#endif

static int pcf8563_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return pcf8563_get_datetime(to_i2c_client(dev), tm);
}

static int pcf8563_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return pcf8563_set_datetime(to_i2c_client(dev), tm);
}

static int pcf8563_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[4];
	int err;

	err = pcf8563_read_block_data(client, PCF8563_REG_AMN, 4, buf);
	if (err)
		return err;

	dev_dbg(&client->dev,
		"%s: raw data is min=%02x, hr=%02x, mday=%02x, wday=%02x\n",
		__func__, buf[0], buf[1], buf[2], buf[3]);

	tm->time.tm_min = bcd2bin(buf[0] & 0x7F);
	tm->time.tm_hour = bcd2bin(buf[1] & 0x7F);
	tm->time.tm_mday = bcd2bin(buf[2] & 0x1F);
	tm->time.tm_wday = bcd2bin(buf[3] & 0x7);
	tm->time.tm_mon = -1;
	tm->time.tm_year = -1;
	tm->time.tm_yday = -1;
	tm->time.tm_isdst = -1;

	err = pcf8563_get_alarm_mode(client, &tm->enabled, &tm->pending);
	if (err < 0)
		return err;

	dev_dbg(&client->dev, "%s: tm is mins=%d, hours=%d, mday=%d, wday=%d,"
		" enabled=%d, pending=%d\n", __func__, tm->time.tm_min,
		tm->time.tm_hour, tm->time.tm_mday, tm->time.tm_wday,
		tm->enabled, tm->pending);

	return 0;
}

static int pcf8563_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[4];
	int err;

	dev_dbg(dev, "%s, min=%d hour=%d wday=%d mday=%d "
		"enabled=%d pending=%d\n", __func__,
		tm->time.tm_min, tm->time.tm_hour, tm->time.tm_wday,
		tm->time.tm_mday, tm->enabled, tm->pending);

	buf[0] = bin2bcd(tm->time.tm_min);
	buf[1] = bin2bcd(tm->time.tm_hour);
	buf[2] = bin2bcd(tm->time.tm_mday);
	buf[3] = tm->time.tm_wday & 0x07;

	err = pcf8563_write_block_data(client, PCF8563_REG_AMN, 4, buf);
	if (err)
		return err;

	return pcf8563_set_alarm_mode(client, 1);
}

static int pcf8563_irq_enable(struct device *dev, unsigned int enabled)
{
	return pcf8563_set_alarm_mode(to_i2c_client(dev), !!enabled);
}

static const struct rtc_class_ops pcf8563_rtc_ops = {
	.ioctl		= pcf8563_rtc_ioctl,
	.read_time	= pcf8563_rtc_read_time,
	.set_time	= pcf8563_rtc_set_time,
	.read_alarm	= pcf8563_rtc_read_alarm,
	.set_alarm	= pcf8563_rtc_set_alarm,
	.alarm_irq_enable = pcf8563_irq_enable,
};

static int pcf8563_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct pcf8563 *pcf8563;
	int err;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pcf8563 = devm_kzalloc(&client->dev, sizeof(struct pcf8563),
				GFP_KERNEL);
	if (!pcf8563)
		return -ENOMEM;

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	i2c_set_clientdata(client, pcf8563);
	pcf8563->client = client;
	device_set_wakeup_capable(&client->dev, 1);

	pcf8563->rtc = devm_rtc_device_register(&client->dev,
				pcf8563_driver.driver.name,
				&pcf8563_rtc_ops, THIS_MODULE);

	if (IS_ERR(pcf8563->rtc))
		return PTR_ERR(pcf8563->rtc);

	if (client->irq > 0) {
		err = devm_request_threaded_irq(&client->dev, client->irq,
				NULL, pcf8563_irq,
				IRQF_SHARED|IRQF_ONESHOT|IRQF_TRIGGER_FALLING,
				pcf8563->rtc->name, client);
		if (err) {
			dev_err(&client->dev, "unable to request IRQ %d\n",
								client->irq);
			return err;
		}

	}

	return 0;
}

static const struct i2c_device_id pcf8563_id[] = {
	{ "pcf8563", 0 },
	{ "rtc8564", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf8563_id);

#ifdef CONFIG_OF
static const struct of_device_id pcf8563_of_match[] = {
	{ .compatible = "nxp,pcf8563" },
	{}
};
MODULE_DEVICE_TABLE(of, pcf8563_of_match);
#endif

static struct i2c_driver pcf8563_driver = {
	.driver		= {
		.name	= "rtc-pcf8563",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pcf8563_of_match),
	},
	.probe		= pcf8563_probe,
	.id_table	= pcf8563_id,
};

module_i2c_driver(pcf8563_driver);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("Philips PCF8563/Epson RTC8564 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
