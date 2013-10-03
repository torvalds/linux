/*
 * An I2C driver for the NXP PCF2127 RTC
 * Copyright 2013 Til-Technologies
 *
 * Author: Renaud Cerrato <r.cerrato@til-technologies.fr>
 *
 * based on the other drivers in this same directory.
 *
 * http://www.nxp.com/documents/data_sheet/PCF2127AT.pdf
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

#define DRV_VERSION "0.0.1"

#define PCF2127_REG_CTRL1       (0x00)  /* Control Register 1 */
#define PCF2127_REG_CTRL2       (0x01)  /* Control Register 2 */
#define PCF2127_REG_CTRL3       (0x02)  /* Control Register 3 */
#define PCF2127_REG_SC          (0x03)  /* datetime */
#define PCF2127_REG_MN          (0x04)
#define PCF2127_REG_HR          (0x05)
#define PCF2127_REG_DM          (0x06)
#define PCF2127_REG_DW          (0x07)
#define PCF2127_REG_MO          (0x08)
#define PCF2127_REG_YR          (0x09)

static struct i2c_driver pcf2127_driver;

struct pcf2127 {
	struct rtc_device *rtc;
	int voltage_low; /* indicates if a low_voltage was detected */
};

/*
 * In the routines that deal directly with the pcf2127 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int pcf2127_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct pcf2127 *pcf2127 = i2c_get_clientdata(client);
	unsigned char buf[10] = { PCF2127_REG_CTRL1 };

	/* read registers */
	if (i2c_master_send(client, buf, 1) != 1 ||
		i2c_master_recv(client, buf, sizeof(buf)) != sizeof(buf)) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	if (buf[PCF2127_REG_CTRL3] & 0x04) {
		pcf2127->voltage_low = 1;
		dev_info(&client->dev,
			"low voltage detected, date/time is not reliable.\n");
	}

	dev_dbg(&client->dev,
		"%s: raw data is cr1=%02x, cr2=%02x, cr3=%02x, "
		"sec=%02x, min=%02x, hr=%02x, "
		"mday=%02x, wday=%02x, mon=%02x, year=%02x\n",
		__func__,
		buf[0], buf[1], buf[2],
		buf[3], buf[4], buf[5],
		buf[6], buf[7], buf[8], buf[9]);


	tm->tm_sec = bcd2bin(buf[PCF2127_REG_SC] & 0x7F);
	tm->tm_min = bcd2bin(buf[PCF2127_REG_MN] & 0x7F);
	tm->tm_hour = bcd2bin(buf[PCF2127_REG_HR] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = bcd2bin(buf[PCF2127_REG_DM] & 0x3F);
	tm->tm_wday = buf[PCF2127_REG_DW] & 0x07;
	tm->tm_mon = bcd2bin(buf[PCF2127_REG_MO] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(buf[PCF2127_REG_YR]);
	if (tm->tm_year < 70)
		tm->tm_year += 100;	/* assume we are in 1970...2069 */

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

static int pcf2127_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	unsigned char buf[8];
	int i = 0, err;

	dev_dbg(&client->dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* start register address */
	buf[i++] = PCF2127_REG_SC;

	/* hours, minutes and seconds */
	buf[i++] = bin2bcd(tm->tm_sec);
	buf[i++] = bin2bcd(tm->tm_min);
	buf[i++] = bin2bcd(tm->tm_hour);
	buf[i++] = bin2bcd(tm->tm_mday);
	buf[i++] = tm->tm_wday & 0x07;

	/* month, 1 - 12 */
	buf[i++] = bin2bcd(tm->tm_mon + 1);

	/* year */
	buf[i++] = bin2bcd(tm->tm_year % 100);

	/* write register's data */
	err = i2c_master_send(client, buf, i);
	if (err != i) {
		dev_err(&client->dev,
			"%s: err=%d", __func__, err);
		return -EIO;
	}

	return 0;
}

#ifdef CONFIG_RTC_INTF_DEV
static int pcf2127_rtc_ioctl(struct device *dev,
				unsigned int cmd, unsigned long arg)
{
	struct pcf2127 *pcf2127 = i2c_get_clientdata(to_i2c_client(dev));

	switch (cmd) {
	case RTC_VL_READ:
		if (pcf2127->voltage_low)
			dev_info(dev, "low voltage detected, date/time is not reliable.\n");

		if (copy_to_user((void __user *)arg, &pcf2127->voltage_low,
					sizeof(int)))
			return -EFAULT;
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}
#else
#define pcf2127_rtc_ioctl NULL
#endif

static int pcf2127_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return pcf2127_get_datetime(to_i2c_client(dev), tm);
}

static int pcf2127_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return pcf2127_set_datetime(to_i2c_client(dev), tm);
}

static const struct rtc_class_ops pcf2127_rtc_ops = {
	.ioctl		= pcf2127_rtc_ioctl,
	.read_time	= pcf2127_rtc_read_time,
	.set_time	= pcf2127_rtc_set_time,
};

static int pcf2127_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct pcf2127 *pcf2127;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pcf2127 = devm_kzalloc(&client->dev, sizeof(struct pcf2127),
				GFP_KERNEL);
	if (!pcf2127)
		return -ENOMEM;

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	i2c_set_clientdata(client, pcf2127);

	pcf2127->rtc = devm_rtc_device_register(&client->dev,
				pcf2127_driver.driver.name,
				&pcf2127_rtc_ops, THIS_MODULE);

	if (IS_ERR(pcf2127->rtc))
		return PTR_ERR(pcf2127->rtc);

	return 0;
}

static const struct i2c_device_id pcf2127_id[] = {
	{ "pcf2127", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf2127_id);

#ifdef CONFIG_OF
static const struct of_device_id pcf2127_of_match[] = {
	{ .compatible = "nxp,pcf2127" },
	{}
};
MODULE_DEVICE_TABLE(of, pcf2127_of_match);
#endif

static struct i2c_driver pcf2127_driver = {
	.driver		= {
		.name	= "rtc-pcf2127",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pcf2127_of_match),
	},
	.probe		= pcf2127_probe,
	.id_table	= pcf2127_id,
};

module_i2c_driver(pcf2127_driver);

MODULE_AUTHOR("Renaud Cerrato <r.cerrato@til-technologies.fr>");
MODULE_DESCRIPTION("NXP PCF2127 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
