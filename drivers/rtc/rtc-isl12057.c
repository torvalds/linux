/*
 * rtc-isl12057 - Driver for Intersil ISL12057 I2C Real Time Clock
 *
 * Copyright (C) 2013, Arnaud EBALARD <arno@natisbad.org>
 *
 * This work is largely based on Intersil ISL1208 driver developed by
 * Hebert Valerio Riedel <hvr@gnu.org>.
 *
 * Detailed datasheet on which this development is based is available here:
 *
 *  http://natisbad.org/NAS2/refs/ISL12057.pdf
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
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rtc.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#define DRV_NAME "rtc-isl12057"

/* RTC section */
#define ISL12057_REG_RTC_SC	0x00	/* Seconds */
#define ISL12057_REG_RTC_MN	0x01	/* Minutes */
#define ISL12057_REG_RTC_HR	0x02	/* Hours */
#define ISL12057_REG_RTC_HR_PM	BIT(5)	/* AM/PM bit in 12h format */
#define ISL12057_REG_RTC_HR_MIL BIT(6)	/* 24h/12h format */
#define ISL12057_REG_RTC_DW	0x03	/* Day of the Week */
#define ISL12057_REG_RTC_DT	0x04	/* Date */
#define ISL12057_REG_RTC_MO	0x05	/* Month */
#define ISL12057_REG_RTC_YR	0x06	/* Year */
#define ISL12057_RTC_SEC_LEN	7

/* Alarm 1 section */
#define ISL12057_REG_A1_SC	0x07	/* Alarm 1 Seconds */
#define ISL12057_REG_A1_MN	0x08	/* Alarm 1 Minutes */
#define ISL12057_REG_A1_HR	0x09	/* Alarm 1 Hours */
#define ISL12057_REG_A1_HR_PM	BIT(5)	/* AM/PM bit in 12h format */
#define ISL12057_REG_A1_HR_MIL	BIT(6)	/* 24h/12h format */
#define ISL12057_REG_A1_DWDT	0x0A	/* Alarm 1 Date / Day of the week */
#define ISL12057_REG_A1_DWDT_B	BIT(6)	/* DW / DT selection bit */
#define ISL12057_A1_SEC_LEN	4

/* Alarm 2 section */
#define ISL12057_REG_A2_MN	0x0B	/* Alarm 2 Minutes */
#define ISL12057_REG_A2_HR	0x0C	/* Alarm 2 Hours */
#define ISL12057_REG_A2_DWDT	0x0D	/* Alarm 2 Date / Day of the week */
#define ISL12057_A2_SEC_LEN	3

/* Control/Status registers */
#define ISL12057_REG_INT	0x0E
#define ISL12057_REG_INT_A1IE	BIT(0)	/* Alarm 1 interrupt enable bit */
#define ISL12057_REG_INT_A2IE	BIT(1)	/* Alarm 2 interrupt enable bit */
#define ISL12057_REG_INT_INTCN	BIT(2)	/* Interrupt control enable bit */
#define ISL12057_REG_INT_RS1	BIT(3)	/* Freq out control bit 1 */
#define ISL12057_REG_INT_RS2	BIT(4)	/* Freq out control bit 2 */
#define ISL12057_REG_INT_EOSC	BIT(7)	/* Oscillator enable bit */

#define ISL12057_REG_SR		0x0F
#define ISL12057_REG_SR_A1F	BIT(0)	/* Alarm 1 interrupt bit */
#define ISL12057_REG_SR_A2F	BIT(1)	/* Alarm 2 interrupt bit */
#define ISL12057_REG_SR_OSF	BIT(7)	/* Oscillator failure bit */

/* Register memory map length */
#define ISL12057_MEM_MAP_LEN	0x10

struct isl12057_rtc_data {
	struct regmap *regmap;
	struct mutex lock;
};

static void isl12057_rtc_regs_to_tm(struct rtc_time *tm, u8 *regs)
{
	tm->tm_sec = bcd2bin(regs[ISL12057_REG_RTC_SC]);
	tm->tm_min = bcd2bin(regs[ISL12057_REG_RTC_MN]);

	if (regs[ISL12057_REG_RTC_HR] & ISL12057_REG_RTC_HR_MIL) { /* AM/PM */
		tm->tm_hour = bcd2bin(regs[ISL12057_REG_RTC_HR] & 0x0f);
		if (regs[ISL12057_REG_RTC_HR] & ISL12057_REG_RTC_HR_PM)
			tm->tm_hour += 12;
	} else {					    /* 24 hour mode */
		tm->tm_hour = bcd2bin(regs[ISL12057_REG_RTC_HR] & 0x3f);
	}

	tm->tm_mday = bcd2bin(regs[ISL12057_REG_RTC_DT]);
	tm->tm_wday = bcd2bin(regs[ISL12057_REG_RTC_DW]) - 1; /* starts at 1 */
	tm->tm_mon  = bcd2bin(regs[ISL12057_REG_RTC_MO]) - 1; /* starts at 1 */
	tm->tm_year = bcd2bin(regs[ISL12057_REG_RTC_YR]) + 100;
}

static int isl12057_rtc_tm_to_regs(u8 *regs, struct rtc_time *tm)
{
	/*
	 * The clock has an 8 bit wide bcd-coded register for the year.
	 * tm_year is an offset from 1900 and we are interested in the
	 * 2000-2099 range, so any value less than 100 is invalid.
	 */
	if (tm->tm_year < 100)
		return -EINVAL;

	regs[ISL12057_REG_RTC_SC] = bin2bcd(tm->tm_sec);
	regs[ISL12057_REG_RTC_MN] = bin2bcd(tm->tm_min);
	regs[ISL12057_REG_RTC_HR] = bin2bcd(tm->tm_hour); /* 24-hour format */
	regs[ISL12057_REG_RTC_DT] = bin2bcd(tm->tm_mday);
	regs[ISL12057_REG_RTC_MO] = bin2bcd(tm->tm_mon + 1);
	regs[ISL12057_REG_RTC_YR] = bin2bcd(tm->tm_year - 100);
	regs[ISL12057_REG_RTC_DW] = bin2bcd(tm->tm_wday + 1);

	return 0;
}

/*
 * Try and match register bits w/ fixed null values to see whether we
 * are dealing with an ISL12057. Note: this function is called early
 * during init and hence does need mutex protection.
 */
static int isl12057_i2c_validate_chip(struct regmap *regmap)
{
	u8 regs[ISL12057_MEM_MAP_LEN];
	static const u8 mask[ISL12057_MEM_MAP_LEN] = { 0x80, 0x80, 0x80, 0xf8,
						       0xc0, 0x60, 0x00, 0x00,
						       0x00, 0x00, 0x00, 0x00,
						       0x00, 0x00, 0x60, 0x7c };
	int ret, i;

	ret = regmap_bulk_read(regmap, 0, regs, ISL12057_MEM_MAP_LEN);
	if (ret)
		return ret;

	for (i = 0; i < ISL12057_MEM_MAP_LEN; ++i) {
		if (regs[i] & mask[i])	/* check if bits are cleared */
			return -ENODEV;
	}

	return 0;
}

static int isl12057_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct isl12057_rtc_data *data = dev_get_drvdata(dev);
	u8 regs[ISL12057_RTC_SEC_LEN];
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_bulk_read(data->regmap, ISL12057_REG_RTC_SC, regs,
			       ISL12057_RTC_SEC_LEN);
	mutex_unlock(&data->lock);

	if (ret) {
		dev_err(dev, "%s: RTC read failed\n", __func__);
		return ret;
	}

	isl12057_rtc_regs_to_tm(tm, regs);

	return rtc_valid_tm(tm);
}

static int isl12057_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct isl12057_rtc_data *data = dev_get_drvdata(dev);
	u8 regs[ISL12057_RTC_SEC_LEN];
	int ret;

	ret = isl12057_rtc_tm_to_regs(regs, tm);
	if (ret)
		return ret;

	mutex_lock(&data->lock);
	ret = regmap_bulk_write(data->regmap, ISL12057_REG_RTC_SC, regs,
				ISL12057_RTC_SEC_LEN);
	mutex_unlock(&data->lock);

	if (ret)
		dev_err(dev, "%s: RTC write failed\n", __func__);

	return ret;
}

/*
 * Check current RTC status and enable/disable what needs to be. Return 0 if
 * everything went ok and a negative value upon error. Note: this function
 * is called early during init and hence does need mutex protection.
 */
static int isl12057_check_rtc_status(struct device *dev, struct regmap *regmap)
{
	int ret;

	/* Enable oscillator if not already running */
	ret = regmap_update_bits(regmap, ISL12057_REG_INT,
				 ISL12057_REG_INT_EOSC, 0);
	if (ret < 0) {
		dev_err(dev, "Unable to enable oscillator\n");
		return ret;
	}

	/* Clear oscillator failure bit if needed */
	ret = regmap_update_bits(regmap, ISL12057_REG_SR,
				 ISL12057_REG_SR_OSF, 0);
	if (ret < 0) {
		dev_err(dev, "Unable to clear oscillator failure bit\n");
		return ret;
	}

	/* Clear alarm bit if needed */
	ret = regmap_update_bits(regmap, ISL12057_REG_SR,
				 ISL12057_REG_SR_A1F, 0);
	if (ret < 0) {
		dev_err(dev, "Unable to clear alarm bit\n");
		return ret;
	}

	return 0;
}

static const struct rtc_class_ops rtc_ops = {
	.read_time = isl12057_rtc_read_time,
	.set_time = isl12057_rtc_set_time,
};

static struct regmap_config isl12057_rtc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int isl12057_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct isl12057_rtc_data *data;
	struct rtc_device *rtc;
	struct regmap *regmap;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENODEV;

	regmap = devm_regmap_init_i2c(client, &isl12057_rtc_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "regmap allocation failed: %d\n", ret);
		return ret;
	}

	ret = isl12057_i2c_validate_chip(regmap);
	if (ret)
		return ret;

	ret = isl12057_check_rtc_status(dev, regmap);
	if (ret)
		return ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->lock);
	data->regmap = regmap;
	dev_set_drvdata(dev, data);

	rtc = devm_rtc_device_register(dev, DRV_NAME, &rtc_ops, THIS_MODULE);
	return PTR_ERR_OR_ZERO(rtc);
}

#ifdef CONFIG_OF
static struct of_device_id isl12057_dt_match[] = {
	{ .compatible = "isl,isl12057" },
	{ },
};
#endif

static const struct i2c_device_id isl12057_id[] = {
	{ "isl12057", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isl12057_id);

static struct i2c_driver isl12057_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(isl12057_dt_match),
	},
	.probe	  = isl12057_probe,
	.id_table = isl12057_id,
};
module_i2c_driver(isl12057_driver);

MODULE_AUTHOR("Arnaud EBALARD <arno@natisbad.org>");
MODULE_DESCRIPTION("Intersil ISL12057 RTC driver");
MODULE_LICENSE("GPL");
