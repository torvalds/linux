/*
 * Driver for TI BQ32000 RTC.
 *
 * Copyright (C) 2009 Semihalf.
 * Copyright (C) 2014 Pavel Machek <pavel@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * You can get hardware description at
 * http://www.ti.com/lit/ds/symlink/bq32000.pdf
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/bcd.h>

#define BQ32K_SECONDS		0x00	/* Seconds register address */
#define BQ32K_SECONDS_MASK	0x7F	/* Mask over seconds value */
#define BQ32K_STOP		0x80	/* Oscillator Stop flat */

#define BQ32K_MINUTES		0x01	/* Minutes register address */
#define BQ32K_MINUTES_MASK	0x7F	/* Mask over minutes value */
#define BQ32K_OF		0x80	/* Oscillator Failure flag */

#define BQ32K_HOURS_MASK	0x3F	/* Mask over hours value */
#define BQ32K_CENT		0x40	/* Century flag */
#define BQ32K_CENT_EN		0x80	/* Century flag enable bit */

#define BQ32K_CALIBRATION	0x07	/* CAL_CFG1, calibration and control */
#define BQ32K_TCH2		0x08	/* Trickle charge enable */
#define BQ32K_CFG2		0x09	/* Trickle charger control */

struct bq32k_regs {
	uint8_t		seconds;
	uint8_t		minutes;
	uint8_t		cent_hours;
	uint8_t		day;
	uint8_t		date;
	uint8_t		month;
	uint8_t		years;
};

static struct i2c_driver bq32k_driver;

static int bq32k_read(struct device *dev, void *data, uint8_t off, uint8_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &off,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = data,
		}
	};

	if (i2c_transfer(client->adapter, msgs, 2) == 2)
		return 0;

	return -EIO;
}

static int bq32k_write(struct device *dev, void *data, uint8_t off, uint8_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	uint8_t buffer[len + 1];

	buffer[0] = off;
	memcpy(&buffer[1], data, len);

	if (i2c_master_send(client, buffer, len + 1) == len + 1)
		return 0;

	return -EIO;
}

static int bq32k_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct bq32k_regs regs;
	int error;

	error = bq32k_read(dev, &regs, 0, sizeof(regs));
	if (error)
		return error;

	tm->tm_sec = bcd2bin(regs.seconds & BQ32K_SECONDS_MASK);
	tm->tm_min = bcd2bin(regs.minutes & BQ32K_SECONDS_MASK);
	tm->tm_hour = bcd2bin(regs.cent_hours & BQ32K_HOURS_MASK);
	tm->tm_mday = bcd2bin(regs.date);
	tm->tm_wday = bcd2bin(regs.day) - 1;
	tm->tm_mon = bcd2bin(regs.month) - 1;
	tm->tm_year = bcd2bin(regs.years) +
				((regs.cent_hours & BQ32K_CENT) ? 100 : 0);

	return rtc_valid_tm(tm);
}

static int bq32k_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct bq32k_regs regs;

	regs.seconds = bin2bcd(tm->tm_sec);
	regs.minutes = bin2bcd(tm->tm_min);
	regs.cent_hours = bin2bcd(tm->tm_hour) | BQ32K_CENT_EN;
	regs.day = bin2bcd(tm->tm_wday + 1);
	regs.date = bin2bcd(tm->tm_mday);
	regs.month = bin2bcd(tm->tm_mon + 1);

	if (tm->tm_year >= 100) {
		regs.cent_hours |= BQ32K_CENT;
		regs.years = bin2bcd(tm->tm_year - 100);
	} else
		regs.years = bin2bcd(tm->tm_year);

	return bq32k_write(dev, &regs, 0, sizeof(regs));
}

static const struct rtc_class_ops bq32k_rtc_ops = {
	.read_time	= bq32k_rtc_read_time,
	.set_time	= bq32k_rtc_set_time,
};

static int trickle_charger_of_init(struct device *dev, struct device_node *node)
{
	unsigned char reg;
	int error;
	u32 ohms = 0;

	if (of_property_read_u32(node, "trickle-resistor-ohms" , &ohms))
		return 0;

	switch (ohms) {
	case 180+940:
		/*
		 * TCHE[3:0] == 0x05, TCH2 == 1, TCFE == 0 (charging
		 * over diode and 940ohm resistor)
		 */

		if (of_property_read_bool(node, "trickle-diode-disable")) {
			dev_err(dev, "diode and resistor mismatch\n");
			return -EINVAL;
		}
		reg = 0x05;
		break;

	case 180+20000:
		/* diode disabled */

		if (!of_property_read_bool(node, "trickle-diode-disable")) {
			dev_err(dev, "bq32k: diode and resistor mismatch\n");
			return -EINVAL;
		}
		reg = 0x45;
		break;

	default:
		dev_err(dev, "invalid resistor value (%d)\n", ohms);
		return -EINVAL;
	}

	error = bq32k_write(dev, &reg, BQ32K_CFG2, 1);
	if (error)
		return error;

	reg = 0x20;
	error = bq32k_write(dev, &reg, BQ32K_TCH2, 1);
	if (error)
		return error;

	dev_info(dev, "Enabled trickle RTC battery charge.\n");
	return 0;
}

static int bq32k_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct rtc_device *rtc;
	uint8_t reg;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	/* Check Oscillator Stop flag */
	error = bq32k_read(dev, &reg, BQ32K_SECONDS, 1);
	if (!error && (reg & BQ32K_STOP)) {
		dev_warn(dev, "Oscillator was halted. Restarting...\n");
		reg &= ~BQ32K_STOP;
		error = bq32k_write(dev, &reg, BQ32K_SECONDS, 1);
	}
	if (error)
		return error;

	/* Check Oscillator Failure flag */
	error = bq32k_read(dev, &reg, BQ32K_MINUTES, 1);
	if (!error && (reg & BQ32K_OF)) {
		dev_warn(dev, "Oscillator Failure. Check RTC battery.\n");
		reg &= ~BQ32K_OF;
		error = bq32k_write(dev, &reg, BQ32K_MINUTES, 1);
	}
	if (error)
		return error;

	if (client && client->dev.of_node)
		trickle_charger_of_init(dev, client->dev.of_node);

	rtc = devm_rtc_device_register(&client->dev, bq32k_driver.driver.name,
						&bq32k_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	i2c_set_clientdata(client, rtc);

	return 0;
}

static const struct i2c_device_id bq32k_id[] = {
	{ "bq32000", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bq32k_id);

static struct i2c_driver bq32k_driver = {
	.driver = {
		.name	= "bq32k",
		.owner	= THIS_MODULE,
	},
	.probe		= bq32k_probe,
	.id_table	= bq32k_id,
};

module_i2c_driver(bq32k_driver);

MODULE_AUTHOR("Semihalf, Piotr Ziecik <kosmo@semihalf.com>");
MODULE_DESCRIPTION("TI BQ32000 I2C RTC driver");
MODULE_LICENSE("GPL");
