/*
 * An I2C driver for the PCF85063 RTC
 * Copyright 2014 Rose Technology
 *
 * Author: Søren Andersen <san@rosetechnology.dk>
 * Maintainers: http://www.nslu2-linux.org/
 *
 * based on the other drivers in this same directory.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/module.h>

/*
 * Information for this driver was pulled from the following datasheets.
 *
 *  http://www.nxp.com/documents/data_sheet/PCF85063A.pdf
 *  http://www.nxp.com/documents/data_sheet/PCF85063TP.pdf
 *
 *  PCF85063A -- Rev. 6 — 18 November 2015
 *  PCF85063TP -- Rev. 4 — 6 May 2015
*/

#define PCF85063_REG_CTRL1		0x00 /* status */
#define PCF85063_REG_CTRL1_STOP		BIT(5)
#define PCF85063_REG_CTRL2		0x01

#define PCF85063_REG_SC			0x04 /* datetime */
#define PCF85063_REG_SC_OS		0x80
#define PCF85063_REG_MN			0x05
#define PCF85063_REG_HR			0x06
#define PCF85063_REG_DM			0x07
#define PCF85063_REG_DW			0x08
#define PCF85063_REG_MO			0x09
#define PCF85063_REG_YR			0x0A

static struct i2c_driver pcf85063_driver;

static int pcf85063_stop_clock(struct i2c_client *client, u8 *ctrl1)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(client, PCF85063_REG_CTRL1);
	if (ret < 0) {
		dev_err(&client->dev, "Failing to stop the clock\n");
		return -EIO;
	}

	/* stop the clock */
	ret |= PCF85063_REG_CTRL1_STOP;

	ret = i2c_smbus_write_byte_data(client, PCF85063_REG_CTRL1, ret);
	if (ret < 0) {
		dev_err(&client->dev, "Failing to stop the clock\n");
		return -EIO;
	}

	*ctrl1 = ret;

	return 0;
}

static int pcf85063_start_clock(struct i2c_client *client, u8 ctrl1)
{
	s32 ret;

	/* start the clock */
	ctrl1 &= PCF85063_REG_CTRL1_STOP;

	ret = i2c_smbus_write_byte_data(client, PCF85063_REG_CTRL1, ctrl1);
	if (ret < 0) {
		dev_err(&client->dev, "Failing to start the clock\n");
		return -EIO;
	}

	return 0;
}

static int pcf85063_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	int rc;
	u8 regs[7];

	/*
	 * while reading, the time/date registers are blocked and not updated
	 * anymore until the access is finished. To not lose a second
	 * event, the access must be finished within one second. So, read all
	 * time/date registers in one turn.
	 */
	rc = i2c_smbus_read_i2c_block_data(client, PCF85063_REG_SC,
					   sizeof(regs), regs);
	if (rc != sizeof(regs)) {
		dev_err(&client->dev, "date/time register read error\n");
		return -EIO;
	}

	/* if the clock has lost its power it makes no sense to use its time */
	if (regs[0] & PCF85063_REG_SC_OS) {
		dev_warn(&client->dev, "Power loss detected, invalid time\n");
		return -EINVAL;
	}

	tm->tm_sec = bcd2bin(regs[0] & 0x7F);
	tm->tm_min = bcd2bin(regs[1] & 0x7F);
	tm->tm_hour = bcd2bin(regs[2] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = bcd2bin(regs[3] & 0x3F);
	tm->tm_wday = regs[4] & 0x07;
	tm->tm_mon = bcd2bin(regs[5] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(regs[6]);
	tm->tm_year += 100;

	return 0;
}

static int pcf85063_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	int rc;
	u8 regs[7];
	u8 ctrl1;

	if ((tm->tm_year < 100) || (tm->tm_year > 199))
		return -EINVAL;

	/*
	 * to accurately set the time, reset the divider chain and keep it in
	 * reset state until all time/date registers are written
	 */
	rc = pcf85063_stop_clock(client, &ctrl1);
	if (rc != 0)
		return rc;

	/* hours, minutes and seconds */
	regs[0] = bin2bcd(tm->tm_sec) & 0x7F; /* clear OS flag */

	regs[1] = bin2bcd(tm->tm_min);
	regs[2] = bin2bcd(tm->tm_hour);

	/* Day of month, 1 - 31 */
	regs[3] = bin2bcd(tm->tm_mday);

	/* Day, 0 - 6 */
	regs[4] = tm->tm_wday & 0x07;

	/* month, 1 - 12 */
	regs[5] = bin2bcd(tm->tm_mon + 1);

	/* year and century */
	regs[6] = bin2bcd(tm->tm_year - 100);

	/* write all registers at once */
	rc = i2c_smbus_write_i2c_block_data(client, PCF85063_REG_SC,
					    sizeof(regs), regs);
	if (rc < 0) {
		dev_err(&client->dev, "date/time register write error\n");
		return rc;
	}

	/*
	 * Write the control register as a separate action since the size of
	 * the register space is different between the PCF85063TP and
	 * PCF85063A devices.  The rollover point can not be used.
	 */
	rc = pcf85063_start_clock(client, ctrl1);
	if (rc != 0)
		return rc;

	return 0;
}

static const struct rtc_class_ops pcf85063_rtc_ops = {
	.read_time	= pcf85063_rtc_read_time,
	.set_time	= pcf85063_rtc_set_time
};

static int pcf85063_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct rtc_device *rtc;
	int err;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	err = i2c_smbus_read_byte_data(client, PCF85063_REG_CTRL1);
	if (err < 0) {
		dev_err(&client->dev, "RTC chip is not present\n");
		return err;
	}

	rtc = devm_rtc_device_register(&client->dev,
				       pcf85063_driver.driver.name,
				       &pcf85063_rtc_ops, THIS_MODULE);

	return PTR_ERR_OR_ZERO(rtc);
}

static const struct i2c_device_id pcf85063_id[] = {
	{ "pcf85063", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf85063_id);

#ifdef CONFIG_OF
static const struct of_device_id pcf85063_of_match[] = {
	{ .compatible = "nxp,pcf85063" },
	{}
};
MODULE_DEVICE_TABLE(of, pcf85063_of_match);
#endif

static struct i2c_driver pcf85063_driver = {
	.driver		= {
		.name	= "rtc-pcf85063",
		.of_match_table = of_match_ptr(pcf85063_of_match),
	},
	.probe		= pcf85063_probe,
	.id_table	= pcf85063_id,
};

module_i2c_driver(pcf85063_driver);

MODULE_AUTHOR("Søren Andersen <san@rosetechnology.dk>");
MODULE_DESCRIPTION("PCF85063 RTC driver");
MODULE_LICENSE("GPL");
